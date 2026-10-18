#include "transport.h"
#include "ssl-diagnostics.h"
#include <freerdp/freerdp.h>
#include <freerdp/gdi/gdi.h>
#include <freerdp/settings.h>
#include <winpr/wlog.h>
#include <algorithm>
#include <atomic>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>
#include <stdexcept>

namespace rds_lab {
namespace {
struct SslSignals {
    std::atomic<unsigned> count{ 0 }, code{ 0 }, alert{ 0 };
};
thread_local SslSignals* current_ssl_signals = nullptr;
BOOL discard_log(const wLogMessage*) noexcept { return TRUE; }
BOOL safe_ssl_log(const wLogMessage* message) noexcept {
    // WLog ERROR text may include credentials, certificates or paths. Do not
    // print any of it. Only this one audited OpenSSL callback is recognized.
    if (!current_ssl_signals || !message || message->Type != WLOG_MESSAGE_TEXT ||
        message->Level != WLOG_ERROR || !message->FunctionName ||
        strcmp(message->FunctionName, "bio_err_print") != 0 || !message->FormatString ||
        strcmp(message->FormatString, "[BIO_do_handshake] %s [%" PRIuz "]") != 0 ||
        !message->TextString) return TRUE;
    const size_t length = strnlen_s(message->TextString, 4097);
    if (length > 4096) return TRUE;
    SafeSslError error;
    if (!parse_handshake_error(std::string_view(message->TextString, length), error)) return TRUE;
    unsigned count = current_ssl_signals->count.load();
    if (count >= 16) return TRUE;
    current_ssl_signals->count = count + 1;
    current_ssl_signals->code = error.code;
    if (error.has_alert) current_ssl_signals->alert = error.alert;
    return TRUE;
}
}
struct Transport::Impl {
    struct Context { rdpContext base; Impl* owner; };
    Config config;
    unsigned index;
    HWND window;
    HANDLE input_event = nullptr;
    std::thread worker;
    std::atomic<bool> stopping{ false };
    std::atomic<bool> ready_for_input{ false };
    std::atomic<unsigned> toggles{ 0 }, focus_syncs{ 0 };
    std::atomic<TransportState> status{ TransportState::Idle };
    std::atomic<unsigned> last_error{ 0 };
    std::atomic<unsigned> frames{ 0 };
    // Bounded diagnostics only: no credentials, certificate contents or names.
    // 1=context,2=configured,3=PreConnect,4=certificate callback,
    // 5=pin accepted,6=PostConnect,7=GDI ready,8=Connect returned,9=frame.
    std::atomic<unsigned> stage{ 0 }, certificate_calls{ 0 }, certificate_flags{ 0 };
    std::atomic<bool> certificate_pin_ok{ false };
    std::atomic<unsigned> selected_protocol{ 0 }, tls_min{ 0 }, tls_max{ 0 }, nla_enabled{ 0 };
    SslSignals ssl_signals;
    std::mutex queue_mutex, frame_mutex, context_mutex;
    rdpContext* active_context = nullptr;
    std::deque<InputEvent> queue;
    std::vector<BYTE> frame;
    int frame_width = 0, frame_height = 0;
    bool pin_verified = false; // connection thread only
    explicit Impl(const Config& source, unsigned seat, HWND destination, unsigned initial_toggles)
        : config(source), index(seat), window(destination), toggles(initial_toggles) {
        input_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!input_event) throw std::runtime_error("Cannot create transport wake event.");
    }
    ~Impl() { if (input_event) CloseHandle(input_event); }
    void state(TransportState next) {
        if (next != TransportState::Connected) ready_for_input = false;
        status = next;
        if (window) PostMessageW(window, state_message, 0, 0);
    }
    void advance(unsigned next) {
        unsigned current = stage.load();
        while (current < next && !stage.compare_exchange_weak(current, next)) {}
    }
    void snapshot(rdpSettings* settings) {
        selected_protocol = freerdp_settings_get_uint32(settings, FreeRDP_SelectedProtocol);
        tls_min = freerdp_settings_get_uint16(settings, FreeRDP_TLSMinVersion);
        tls_max = freerdp_settings_get_uint16(settings, FreeRDP_TLSMaxVersion);
        nla_enabled = freerdp_settings_get_bool(settings, FreeRDP_NlaSecurity) ? 1U : 0U;
    }
    static Impl* owner(rdpContext* context) { return reinterpret_cast<Context*>(context)->owner; }
    static BOOL reject_redirect(freerdp*) noexcept { return FALSE; }
    static BOOL pre_connect(freerdp* instance) noexcept {
        // FreeRDP resets its abort event before this callback. Checking our
        // own stop flag here closes the cancellation race at Connect entry.
        owner(instance->context)->advance(3);
        return owner(instance->context)->stopping ? FALSE : TRUE;
    }
    static int verify(freerdp* instance, const BYTE* pem, size_t size,
        const char* hostname, UINT16 port, DWORD flags) noexcept {
        Impl* self = owner(instance->context);
        self->advance(4);
        ++self->certificate_calls;
        self->certificate_flags = flags;
        try {
            self->pin_verified = flags == VERIFY_CERT_FLAG_NONE && hostname &&
                self->config.server_address == hostname && port == self->config.server_port &&
                certificate_matches(pem, size, self->config.certificate_sha256);
            self->certificate_pin_ok = self->pin_verified;
            if (self->pin_verified) self->advance(5);
            if (!self->pin_verified) self->state(TransportState::CertificateRejected);
            // Never persist trust or accept an unknown/changed certificate interactively.
            return self->pin_verified ? 2 : 0;
        } catch (...) { self->state(TransportState::CertificateRejected); return 0; }
    }
    static BOOL begin_paint(rdpContext* context) noexcept {
        if (!context->gdi || !context->gdi->primary || !context->gdi->primary->hdc ||
            !context->gdi->primary->hdc->hwnd || !context->gdi->primary->hdc->hwnd->invalid) return FALSE;
        context->gdi->primary->hdc->hwnd->invalid->null = TRUE;
        return TRUE;
    }
    static BOOL end_paint(rdpContext* context) noexcept {
        try {
            rdpGdi* gdi = context->gdi;
            if (!gdi || !gdi->primary_buffer || gdi->width < 1 || gdi->width > 4096 ||
                gdi->height < 1 || gdi->height > 2160 || gdi->stride < static_cast<UINT32>(gdi->width) * 4)
                return FALSE;
            Impl* self = owner(context);
            {
                std::lock_guard lock(self->frame_mutex);
                self->frame_width = gdi->width;
                self->frame_height = gdi->height;
                self->frame.resize(static_cast<size_t>(gdi->width) * gdi->height * 4);
                for (int y = 0; y < gdi->height; ++y)
                    memcpy(self->frame.data() + static_cast<size_t>(y) * gdi->width * 4,
                        gdi->primary_buffer + static_cast<size_t>(y) * gdi->stride,
                        static_cast<size_t>(gdi->width) * 4);
            }
            ++self->frames;
            self->advance(9);
            if (self->window) PostMessageW(self->window, frame_message, 0, 0);
            return TRUE;
        } catch (...) { return FALSE; }
    }
    static BOOL resize(rdpContext* context) noexcept {
        const UINT32 width = freerdp_settings_get_uint32(context->settings, FreeRDP_DesktopWidth);
        const UINT32 height = freerdp_settings_get_uint32(context->settings, FreeRDP_DesktopHeight);
        if (width < 640 || width > 4096 || height < 480 || height > 2160) return FALSE;
        // Keep the input coordinate contract exact; unexpected server sizes fail closed.
        const SeatConfig& seat = owner(context)->config.seats[owner(context)->index];
        if (width != seat.desktop_width || height != seat.desktop_height) return FALSE;
        return gdi_resize(context->gdi, width, height);
    }
    static BOOL post_connect(freerdp* instance) noexcept {
        Impl* self = owner(instance->context);
        self->advance(6);
        if (!self->pin_verified) return FALSE;
        const SeatConfig& seat = self->config.seats[self->index];
        if (freerdp_settings_get_uint32(instance->context->settings, FreeRDP_DesktopWidth) != seat.desktop_width ||
            freerdp_settings_get_uint32(instance->context->settings, FreeRDP_DesktopHeight) != seat.desktop_height)
            return FALSE;
        if (!gdi_init(instance, PIXEL_FORMAT_BGRA32)) return FALSE;
        self->advance(7);
        instance->context->update->BeginPaint = begin_paint;
        instance->context->update->EndPaint = end_paint;
        instance->context->update->DesktopResize = resize;
        self->state(TransportState::Connected);
        return TRUE;
    }
    static void post_disconnect(freerdp* instance) noexcept { gdi_free(instance); }
    void configure(freerdp* instance) {
        rdpSettings* settings = instance->context->settings;
        const SeatConfig& seat = config.seats[index];
        const auto boolean = [settings](FreeRDP_Settings_Keys_Bool key, bool value) {
            if (!freerdp_settings_set_bool(settings, key, value ? TRUE : FALSE))
                throw std::runtime_error("RDP boolean setting rejected.");
        };
        const auto number = [settings](FreeRDP_Settings_Keys_UInt32 key, UINT32 value) {
            if (!freerdp_settings_set_uint32(settings, key, value))
                throw std::runtime_error("RDP numeric setting rejected.");
        };
        const auto string = [settings](FreeRDP_Settings_Keys_String key, const char* value) {
            if (!freerdp_settings_set_string(settings, key, value))
                throw std::runtime_error("RDP string setting rejected.");
        };
        string(FreeRDP_ServerHostname, config.server_address.c_str());
        string(FreeRDP_Username, utf8(seat.username).c_str());
        string(FreeRDP_Domain, utf8(seat.domain).c_str());
        string(FreeRDP_ClientHostname, "MSLABCLIENT");
        string(FreeRDP_ComputerName, "MSLABCLIENT");
        string(FreeRDP_ClientDir, "C:\\Windows\\System32");
        string(FreeRDP_ActionScript, nullptr);
        string(FreeRDP_TlsSecretsFile, nullptr);
        number(FreeRDP_ServerPort, config.server_port);
        number(FreeRDP_DesktopWidth, seat.desktop_width);
        number(FreeRDP_DesktopHeight, seat.desktop_height);
        number(FreeRDP_ColorDepth, 32);
        number(FreeRDP_TcpConnectTimeout, 5000);
        boolean(FreeRDP_ExternalCertificateManagement, true);
        boolean(FreeRDP_IgnoreCertificate, false);
        boolean(FreeRDP_AutoAcceptCertificate, false);
        boolean(FreeRDP_NlaSecurity, true);
        boolean(FreeRDP_TlsSecurity, false); // no TLS-without-NLA downgrade
        boolean(FreeRDP_RdpSecurity, false);
        boolean(FreeRDP_ExtSecurity, false);
        boolean(FreeRDP_RdstlsSecurity, false);
        boolean(FreeRDP_AadSecurity, false);
        boolean(FreeRDP_Authentication, true);
        boolean(FreeRDP_NegotiateSecurityLayer, true);
        boolean(FreeRDP_GatewayEnabled, false);
        if (!freerdp_settings_set_uint16(settings, FreeRDP_TLSMinVersion, 0x0303))
            throw std::runtime_error("TLS 1.2 minimum setting rejected.");
        boolean(FreeRDP_AutoReconnectionEnabled, false);
        boolean(FreeRDP_RedirectClipboard, false);
        boolean(FreeRDP_RedirectDrives, false);
        boolean(FreeRDP_RedirectPrinters, false);
        boolean(FreeRDP_RedirectSmartCards, false);
        boolean(FreeRDP_RedirectSerialPorts, false);
        boolean(FreeRDP_RedirectParallelPorts, false);
        boolean(FreeRDP_DeviceRedirection, false);
        boolean(FreeRDP_AudioPlayback, false);
        boolean(FreeRDP_AudioCapture, false);
        boolean(FreeRDP_SupportGraphicsPipeline, false);
        boolean(FreeRDP_RemoteFxCodec, false);
        boolean(FreeRDP_DumpRemoteFx, false);
        boolean(FreeRDP_PlayRemoteFx, false);
        boolean(FreeRDP_SoftwareGdi, true);
        // Plaintext exists only in process memory. It is never a CLI argument or log field.
        std::vector<BYTE> password = unprotect_password(seat.password_dpapi);
        const BOOL set = freerdp_settings_set_string(settings, FreeRDP_Password,
            reinterpret_cast<const char*>(password.data()));
        SecureZeroMemory(password.data(), password.size());
        if (!set) throw std::runtime_error("RDP credential setting rejected.");
    }
    bool send(rdpInput* input, const InputEvent& event) {
        // FreeRDP can return TRUE while skipping input before ACTIVE or while
        // suspended. Fail this transport rather than silently losing/replaying it.
        if (!freerdp_is_active_state(input->context) ||
            freerdp_settings_get_bool(input->context->settings, FreeRDP_SuspendInput)) return false;
        switch (event.kind) {
        case EventKind::Key: {
            UINT16 flags = static_cast<UINT16>(event.down ? (event.repeat ? KBD_FLAGS_DOWN : 0) : KBD_FLAGS_RELEASE);
            if (event.code & 0x100) flags |= KBD_FLAGS_EXTENDED;
            if (event.code & 0x200) flags |= KBD_FLAGS_EXTENDED1;
            if (!freerdp_input_send_keyboard_event(input, flags, static_cast<UINT8>(event.code & 0xFF))) return false;
            if (event.down && !event.repeat) {
                // Track only this seat's lock flags for protocol resynchronization;
                // never copy the shared host keyboard state into both sessions.
                unsigned mask = 0;
                if (event.code == 0x3A) mask = KBD_SYNC_CAPS_LOCK;
                else if (event.code == 0x45) mask = KBD_SYNC_NUM_LOCK;
                else if (event.code == 0x46) mask = KBD_SYNC_SCROLL_LOCK;
                if (mask) toggles.fetch_xor(mask);
            }
            return true;
        }
        case EventKind::Pause: return freerdp_input_send_keyboard_pause_event(input) != FALSE;
        case EventKind::Move:
            return freerdp_input_send_mouse_event(input, PTR_FLAGS_MOVE,
                static_cast<UINT16>(event.x), static_cast<UINT16>(event.y)) != FALSE;
        case EventKind::Button: {
            if (event.code >= 3) {
                UINT16 flags = event.code == 3 ? PTR_XFLAGS_BUTTON1 : PTR_XFLAGS_BUTTON2;
                if (event.down) flags |= PTR_XFLAGS_DOWN;
                return freerdp_input_send_extended_mouse_event(input, flags,
                    static_cast<UINT16>(event.x), static_cast<UINT16>(event.y)) != FALSE;
            }
            constexpr UINT16 flags_by_button[] = { PTR_FLAGS_BUTTON1, PTR_FLAGS_BUTTON2, PTR_FLAGS_BUTTON3 };
            UINT16 flags = flags_by_button[event.code];
            if (event.down) flags |= PTR_FLAGS_DOWN;
            return freerdp_input_send_mouse_event(input, flags,
                static_cast<UINT16>(event.x), static_cast<UINT16>(event.y)) != FALSE;
        }
        case EventKind::Wheel: {
            int remaining = static_cast<SHORT>(event.code & 0xFFFF);
            while (remaining != 0) {
                const int delta = std::clamp(remaining, -255, 255);
                UINT16 flags = event.horizontal ? PTR_FLAGS_HWHEEL : PTR_FLAGS_WHEEL;
                flags |= static_cast<UINT16>(delta & WheelRotationMask);
                if (!freerdp_input_send_mouse_event(input, flags,
                    static_cast<UINT16>(event.x), static_cast<UINT16>(event.y))) return false;
                remaining -= delta;
            }
            return true;
        }
        }
        return false;
    }
    void run() noexcept {
        current_ssl_signals = &ssl_signals;
        freerdp* instance = nullptr;
        bool connected = false;
        try {
            state(TransportState::Connecting);
            instance = freerdp_new();
            if (!instance) throw std::runtime_error("Cannot allocate RDP context.");
            instance->ContextSize = sizeof(Context);
            if (!freerdp_context_new(instance)) throw std::runtime_error("Cannot initialize RDP context.");
            { std::lock_guard lock(context_mutex); active_context = instance->context; }
            reinterpret_cast<Context*>(instance->context)->owner = this;
            advance(1);
            instance->VerifyX509Certificate = verify;
            instance->PreConnect = pre_connect;
            instance->Redirect = reject_redirect;
            instance->PostConnect = post_connect;
            instance->PostDisconnect = post_disconnect;
            configure(instance);
            snapshot(instance->context->settings);
            advance(2);
            if (stopping) throw std::runtime_error("Connection cancelled.");
            connected = freerdp_connect(instance) != FALSE;
            snapshot(instance->context->settings);
            if (!connected) throw std::runtime_error("RDP connection failed.");
            advance(8);
            while (!freerdp_shall_disconnect_context(instance->context)) {
                HANDLE handles[MAXIMUM_WAIT_OBJECTS]{};
                DWORD count = freerdp_get_event_handles(instance->context, handles, MAXIMUM_WAIT_OBJECTS - 1);
                if (count == 0 || count >= MAXIMUM_WAIT_OBJECTS) break;
                handles[count++] = input_event;
                if (WaitForMultipleObjects(count, handles, FALSE, 50) == WAIT_FAILED) break;
                // Process activation/deactivation before deciding whether queued
                // input is deliverable. Frame reception alone does not prove it.
                if (!stopping && !freerdp_check_event_handles(instance->context)) break;
                const bool active = freerdp_is_active_state(instance->context) &&
                    !freerdp_settings_get_bool(instance->context->settings, FreeRDP_SuspendInput);
                if (active && freerdp_focus_required(instance)) {
                    const auto flags = static_cast<UINT16>(toggles.load());
                    // Match the upstream Windows client's requested focus handshake.
                    if (!freerdp_input_send_focus_in_event(instance->context->input, flags) ||
                        !freerdp_input_send_focus_in_event(instance->context->input, flags))
                        throw std::runtime_error("RDP focus synchronization failed.");
                    ++focus_syncs;
                }
                if (ready_for_input.exchange(active) != active && window)
                    PostMessageW(window, state_message, 0, 0);
                std::deque<InputEvent> events;
                { std::lock_guard lock(queue_mutex); events.swap(queue); }
                for (const InputEvent& event : events) if (!send(instance->context->input, event))
                    throw std::runtime_error("RDP input send failed.");
                // Deliver queued release events before a locally requested disconnect.
                if (stopping) break;
            }
            if (!stopping) last_error = freerdp_get_last_error(instance->context);
            state(TransportState::Closed);
        } catch (...) {
            if (instance && instance->context) {
                last_error = freerdp_get_last_error(instance->context);
                snapshot(instance->context->settings);
            }
            if (status != TransportState::CertificateRejected) state(TransportState::Failed);
        }
        if (instance) {
            if (instance->context) {
                if (connected && !freerdp_disconnect(instance)) last_error = freerdp_get_last_error(instance->context);
                // Erase the owned settings password before the settings allocation is freed.
                const char* password = freerdp_settings_get_string(instance->context->settings, FreeRDP_Password);
                if (password) SecureZeroMemory(const_cast<char*>(password), strlen(password));
                { std::lock_guard lock(context_mutex); active_context = nullptr; }
                freerdp_context_free(instance);
            }
            freerdp_free(instance);
        }
        std::lock_guard lock(queue_mutex);
        queue.clear();
        current_ssl_signals = nullptr;
    }
};
Transport::Transport(const Config& config, unsigned seat_index, HWND window, unsigned toggle_states)
    : impl_(std::make_unique<Impl>(config, seat_index, window, toggle_states)) {
    if (seat_index >= config.seats.size()) throw std::runtime_error("Invalid seat index.");
}
Transport::~Transport() { stop(); }
void Transport::start() {
    if (impl_->worker.joinable() || impl_->status != TransportState::Idle)
        throw std::runtime_error("A transport cannot be started twice.");
    impl_->worker = std::thread([self = impl_.get()] { self->run(); });
}
void Transport::stop() {
    impl_->ready_for_input = false;
    impl_->stopping = true;
    SetEvent(impl_->input_event);
    {
        std::lock_guard lock(impl_->context_mutex);
        if (impl_->active_context && impl_->status == TransportState::Connecting)
            (void)freerdp_abort_connect_context(impl_->active_context);
    }
    if (impl_->worker.joinable()) impl_->worker.join();
}
bool Transport::enqueue(const InputEvent& event) {
    if (impl_->status != TransportState::Connected || !impl_->ready_for_input || impl_->stopping) return false;
    { std::lock_guard lock(impl_->queue_mutex);
        if (impl_->queue.size() >= 4096) { impl_->stopping = true; SetEvent(impl_->input_event); return false; }
        impl_->queue.push_back(event);
    }
    return SetEvent(impl_->input_event) != FALSE;
}
TransportState Transport::state() const { return impl_->status.load(); }
unsigned Transport::error_code() const { return impl_->last_error.load(); }
unsigned Transport::frame_count() const { return impl_->frames.load(); }
bool Transport::input_ready() const { return impl_->ready_for_input.load(); }
unsigned Transport::toggle_states() const { return impl_->toggles.load(); }
std::string Transport::diagnostics() const {
    return "stage=" + std::to_string(impl_->stage.load()) +
        " cert_calls=" + std::to_string(impl_->certificate_calls.load()) +
        " pin_ok=" + std::to_string(impl_->certificate_pin_ok.load() ? 1 : 0) +
        " cert_flags=" + std::to_string(impl_->certificate_flags.load()) +
        " protocol=" + std::to_string(impl_->selected_protocol.load()) +
        " tls_min=" + std::to_string(impl_->tls_min.load()) +
        " tls_max=" + std::to_string(impl_->tls_max.load()) +
        " nla=" + std::to_string(impl_->nla_enabled.load()) +
        " ssl_errors=" + std::to_string(impl_->ssl_signals.count.load()) +
        " ssl_code=" + std::to_string(impl_->ssl_signals.code.load()) +
        " ssl_alert=" + std::to_string(impl_->ssl_signals.alert.load()) +
        " input_ready=" + std::to_string(impl_->ready_for_input.load() ? 1 : 0) +
        " focus_syncs=" + std::to_string(impl_->focus_syncs.load());
}
bool Transport::enable_safe_ssl_diagnostics() {
    wLog* root = WLog_GetRoot();
    if (!WLog_SetLogLevel(root, WLOG_OFF) || !WLog_SetLogAppenderType(root, WLOG_APPENDER_CALLBACK)) return false;
    wLogAppender* appender = WLog_GetLogAppender(root);
    if (!appender) return false;
    wLogCallbacks callbacks{};
    callbacks.message = safe_ssl_log;
    callbacks.data = discard_log; callbacks.image = discard_log; callbacks.package = discard_log;
    return WLog_ConfigureAppender(appender, "callbacks", &callbacks) &&
        WLog_OpenAppender(root) && WLog_SetLogLevel(root, WLOG_ERROR);
}
bool Transport::draw(HDC dc, const RECT& target) {
    std::lock_guard lock(impl_->frame_mutex);
    if (impl_->frame.empty()) return false;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = impl_->frame_width;
    info.bmiHeader.biHeight = -impl_->frame_height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    SetStretchBltMode(dc, COLORONCOLOR);
    return StretchDIBits(dc, target.left, target.top, target.right - target.left, target.bottom - target.top,
        0, 0, impl_->frame_width, impl_->frame_height, impl_->frame.data(), &info, DIB_RGB_COLORS, SRCCOPY) != GDI_ERROR;
}
bool Transport::library_smoke() {
    if (!WLog_SetLogLevel(WLog_GetRoot(), WLOG_OFF)) return false;
    std::array<freerdp*, 2> instances{};
    bool result = true;
    for (freerdp*& instance : instances) {
        instance = freerdp_new();
        if (!instance || !freerdp_context_new(instance)) { result = false; break; }
    }
    if (result) result = instances[0]->context != instances[1]->context &&
        instances[0]->context->input != instances[1]->context->input;
    for (freerdp* instance : instances) if (instance) {
        if (instance->context) freerdp_context_free(instance);
        freerdp_free(instance);
    }
    return result;
}
}
