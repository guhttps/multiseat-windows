#include "network.h"
#include "config.h"
#include "input-router.h"
#include "input-registration.h"
#include "cursor-lease.h"
#include "transport.h"
#include "../common/raw-input.h"
#include <roapi.h>
#include <array>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace rds_lab {
bool run_self_tests();
namespace {
constexpr wchar_t window_class[] = L"MultiseatRdsLabView";
class Application;
class PaintBuffer {
    HDC memory_{};
    HBITMAP bitmap_{};
    HGDIOBJ previous_{};
    int width_{}, height_{};
    void clear() noexcept {
        if (memory_ && previous_) SelectObject(memory_, previous_);
        if (bitmap_) DeleteObject(bitmap_);
        if (memory_) DeleteDC(memory_);
        memory_ = nullptr; bitmap_ = nullptr; previous_ = nullptr;
        width_ = height_ = 0;
    }
public:
    PaintBuffer() = default;
    PaintBuffer(const PaintBuffer&) = delete;
    PaintBuffer& operator=(const PaintBuffer&) = delete;
    ~PaintBuffer() { clear(); }
    HDC prepare(HDC target, int width, int height) {
        if (memory_ && width == width_ && height == height_) return memory_;
        clear();
        memory_ = CreateCompatibleDC(target);
        if (memory_) bitmap_ = CreateCompatibleBitmap(target, width, height);
        if (bitmap_) previous_ = SelectObject(memory_, bitmap_);
        if (!memory_ || !bitmap_ || !previous_ || previous_ == HGDI_ERROR) {
            if (previous_ == HGDI_ERROR) previous_ = nullptr;
            clear();
            throw std::runtime_error("Cannot create the local view paint buffer.");
        }
        width_ = width; height_ = height;
        return memory_;
    }
};
struct View { Application* app{}; unsigned index{}; HWND window{}; PaintBuffer buffer; };
bool input_desktop_is_default() {
    HDESK desktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (!desktop) return false;
    wchar_t name[128]{};
    DWORD required = 0;
    const bool result = GetUserObjectInformationW(desktop, UOI_NAME, name, sizeof(name), &required) &&
        required <= sizeof(name) && wcscmp(name, L"Default") == 0;
    CloseDesktop(desktop);
    return result;
}
std::array<std::pair<unsigned, unsigned>, 2> sizes(const Config& config) {
    return { std::pair{ config.seats[0].desktop_width, config.seats[0].desktop_height },
        std::pair{ config.seats[1].desktop_width, config.seats[1].desktop_height } };
}
BOOL CALLBACK collect_monitor(HMONITOR monitor, HDC, LPRECT, LPARAM parameter) {
    auto* rectangles = reinterpret_cast<std::vector<RECT>*>(parameter);
    MONITORINFO info{ sizeof(info) };
    if (!GetMonitorInfoW(monitor, &info)) return FALSE;
    try { rectangles->push_back(info.rcWork); return TRUE; }
    catch (...) { return FALSE; }
}
const wchar_t* state_text(TransportState state) {
    switch (state) {
    case TransportState::Idle: return L"Sem conexão";
    case TransportState::Connecting: return L"Conectando ao laboratório";
    case TransportState::Connected: return L"RDP conectado; observação externa ainda necessária";
    case TransportState::Closed: return L"RDP desconectado";
    case TransportState::CertificateRejected: return L"Certificado recusado: confira a VM por PowerShell Direct";
    default: return L"Falha no transporte RDP; consulte o código numérico no terminal";
    }
}
bool terminal_state(TransportState state) {
    return state == TransportState::Closed || state == TransportState::Failed ||
        state == TransportState::CertificateRejected;
}
class Application {
public:
    enum class RecoveryPhase { Initial, PeerOffline, Reconnecting, PreparingEditor, Finished };
    Config config;
    std::array<View, 2> views{};
    std::array<std::unique_ptr<Transport>, 2> transports;
    InputRouter router;
    CursorLease cursor;
    InputRegistration registration;
    std::array<ULONGLONG, 2> routed_keys{}, routed_clicks{};
    ULONGLONG unassigned_keys{}, full_resets{};
    std::array<bool, 2> send_failed{}, timed_out{}, failure_reported{};
    std::array<ULONGLONG, 2> attempt_started{};
    bool smoke = false, synthetic = false, unattended = false, closing = false, capture = false;
    bool recovery = false;
    RecoveryPhase recovery_phase = RecoveryPhase::Initial;
    ULONGLONG recovery_phase_since = 0;
    bool sent_synthetic = false, waiting_foreground_reported = false;
    ULONGLONG started = 0, connected_since = 0, synthetic_sent_at = 0;
    std::array<std::string, 2> last_progress;
    int result = 0;
    Application(Config source, bool smoke_mode, bool synthetic_mode, bool unattended_mode = false, bool recovery_mode = false)
        : config(std::move(source)), router(sizes(config), [this](unsigned seat, const InputEvent& event) {
            if (transports[seat]) {
                if (transports[seat]->enqueue(event)) {
                    if (event.kind == EventKind::Key && event.down) ++routed_keys[seat];
                    if (event.kind == EventKind::Button && event.code == 0 && event.down) ++routed_clicks[seat];
                } else if (transports[seat]->state() == TransportState::Connected) send_failed[seat] = true;
            }
        }), smoke(smoke_mode), synthetic(synthetic_mode), unattended(unattended_mode), recovery(recovery_mode) {}
    ~Application() {
        cursor.release();
        router.release_all();
        unregister_input();
        for (auto& transport : transports) if (transport) transport->stop();
        for (View& view : views) if (view.window && IsWindow(view.window)) DestroyWindow(view.window);
    }
    bool foreground() const {
        const HWND active = GetForegroundWindow();
        return IsWindowVisible(views[0].window) && IsWindowVisible(views[1].window) &&
            !IsIconic(views[0].window) && !IsIconic(views[1].window) &&
            (active == views[0].window || active == views[1].window) && input_desktop_is_default();
    }
    bool connected(unsigned index) const {
        return transports[index] && transports[index]->state() == TransportState::Connected &&
            transports[index]->frame_count() > 0 && transports[index]->input_ready();
    }
    bool both_connected() const { return connected(0) && connected(1); }
    bool any_connected() const { return connected(0) || connected(1); }
    void report_progress(bool force) {
        if (smoke) return;
        for (unsigned i = 0; i < transports.size(); ++i) {
            if (!transports[i]) continue;
            const std::string progress = "state=" + std::to_string(static_cast<int>(transports[i]->state())) +
                " code=" + std::to_string(transports[i]->error_code()) + " " + transports[i]->diagnostics();
            if (force || progress != last_progress[i]) {
                // Flush only fixed labels and numeric fields, including during
                // a blocked handshake. Never print library/configuration text.
                std::cout << "RDP seat " << i + 1 << " " << progress << std::endl;
                last_progress[i] = progress;
            }
        }
    }
    void unregister_input() {
        if (!registration.release()) result = 1;
    }
    void report_input_counts() const {
        if (smoke || synthetic) return;
        for (unsigned i = 0; i < views.size(); ++i)
            std::cout << "INPUT seat=" << i + 1 << " key_downs=" << routed_keys[i] <<
                " left_clicks=" << routed_clicks[i] << std::endl;
        std::cout << "INPUT unassigned_key_downs=" << unassigned_keys << std::endl;
        std::cout << "INPUT full_resets=" << full_resets << std::endl;
    }
    void update_gate() {
        if (smoke || synthetic || closing) return;
        if (!views[0].window || !views[1].window) return;
        const bool enabled = foreground() && any_connected();
        if (!enabled && capture) { router.release_all(); report_input_counts(); }
        for (unsigned i = 0; i < transports.size(); ++i) router.set_enabled(i, enabled && connected(i));
        capture = enabled;
        // Keep only device-change notifications while routing is paused.
        // Neither mode enables INPUTSINK/EXINPUTSINK: host background typing
        // is never requested, and raw() still checks the live foreground gate.
        if (!registration.set(views[0].window, enabled))
            throw std::runtime_error("Raw Input registration failed.");
        if (enabled && router.any_routable()) {
            RECT client{};
            const HWND active = GetForegroundWindow();
            if (active != views[0].window && active != views[1].window) {
                capture = false;
                router.release_all();
                if (!registration.set(views[0].window, false))
                    throw std::runtime_error("Cannot pause Raw Input registration.");
                if (!cursor.release()) throw std::runtime_error("Cannot release the owned host cursor capture.");
                return;
            }
            if (!GetClientRect(active, &client)) throw std::runtime_error("Cannot locate cursor capture area.");
            POINT origin{client.left, client.top}, end{client.right, client.bottom};
            if (!ClientToScreen(active, &origin) || !ClientToScreen(active, &end))
                throw std::runtime_error("Cannot map cursor capture area.");
            client = {origin.x, origin.y, end.x, end.y};
            const int x = GetSystemMetrics(SM_XVIRTUALSCREEN), y = GetSystemMetrics(SM_YVIRTUALSCREEN);
            const RECT desktop{x, y, x + GetSystemMetrics(SM_CXVIRTUALSCREEN), y + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
            if (!cursor.acquire(client, desktop)) throw std::runtime_error("Cannot safely capture the shared host cursor.");
        } else if (!cursor.release()) throw std::runtime_error("Cannot release the owned host cursor capture.");
    }
    void invalidate() { for (const View& view : views) InvalidateRect(view.window, nullptr, FALSE); }
    void raw(HRAWINPUT handle) {
        if (!capture || !foreground() || !any_connected()) { update_gate(); return; }
        RAWINPUT input{};
        UINT size = sizeof(input);
        const UINT copied = GetRawInputData(handle, RID_INPUT, &input, &size, sizeof(RAWINPUTHEADER));
        if (copied == static_cast<UINT>(-1) || !multiseat::valid_raw_input(input, copied)) return;
        if (input.header.dwType == RIM_TYPEKEYBOARD) {
            const RAWKEYBOARD& key = input.data.keyboard;
            if (!(key.Flags & RI_KEY_BREAK) && key.VKey == VK_F12) { close(0); return; }
            if (!(key.Flags & RI_KEY_BREAK) && key.VKey == VK_F8) {
                ++full_resets;
                router.reset(); report_input_counts(); update_gate(); invalidate(); return;
            }
            if (!(key.Flags & RI_KEY_BREAK) && key.VKey == VK_F7) { retry_unavailable(); return; }
            if (!(key.Flags & RI_KEY_BREAK) && key.VKey == VK_F9) { disconnect_device(input.header.hDevice); return; }
            if (router.any_routable() && !(key.Flags & RI_KEY_BREAK) && key.VKey != VK_F6 &&
                (!input.header.hDevice || key.MakeCode == 0 || key.MakeCode >= 255 || key.VKey == 255 ||
                (router.seat(0).keyboard != input.header.hDevice && router.seat(1).keyboard != input.header.hDevice)))
                ++unassigned_keys;
            router.keyboard(input.header.hDevice, key, true);
        } else router.mouse(input.header.hDevice, input.data.mouse, true);
        update_gate();
        invalidate();
    }
    RECT surface(const RECT& client, unsigned index) const {
        RECT area{ 0, 86, client.right, client.bottom - 30 };
        if (area.bottom <= area.top) area.bottom = area.top + 1;
        const SeatConfig& seat = config.seats[index];
        const int available_width = area.right - area.left, available_height = area.bottom - area.top;
        int width = available_width;
        int height = static_cast<int>(static_cast<long long>(width) * seat.desktop_height / seat.desktop_width);
        if (height > available_height) {
            height = available_height;
            width = static_cast<int>(static_cast<long long>(height) * seat.desktop_width / seat.desktop_height);
        }
        area.left += (available_width - width) / 2;
        area.top += (available_height - height) / 2;
        area.right = area.left + width;
        area.bottom = area.top + height;
        return area;
    }
    void paint(View& view, HDC dc) {
        RECT client{};
        GetClientRect(view.window, &client);
        FillRect(dc, &client, GetSysColorBrush(COLOR_WINDOW));
        SetBkMode(dc, TRANSPARENT);
        HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        const HGDIOBJ previous = SelectObject(dc, font);
        std::wstring status = L"LABORATÓRIO WINDOWS SERVER — POSTO " + std::to_wstring(view.index + 1) +
            L"\n" + (smoke ? L"Smoke local: nenhuma conexão ou captura" :
                timed_out[view.index] ? L"Prazo de conexão/frame excedido" : state_text(transports[view.index]->state()));
        if (!smoke && !synthetic && (timed_out[view.index] || terminal_state(transports[view.index]->state())))
            status += L"; F7 reconecta este posto";
        if (synthetic) {
            status += L"\nTeste sintético: não demonstra isolamento dos periféricos físicos.";
            if (unattended)
                status += L"\nAutoteste: fixtures fixas na VM; o foco do host não é exigido.";
            else if (waiting_foreground_reported && !sent_synthetic)
                status += L"\nClique em um destes painéis para permitir o envio do teste.";
            if (recovery) status += L"\nRecuperação sintética: desconecta e reconecta somente o posto 1.";
        }
        else if (!router.ready()) {
            const SeatInput& seat = router.seat(view.index);
            status += L"\nTeclado: " + std::wstring(seat.keyboard ? L"associado" : L"aguardando F6") +
                L" | Mouse: " + std::wstring(seat.mouse ? L"associado" : L"aguardando clique direito");
            if (capture && router.any_routable())
                status += router.routable(view.index) ? L"\nEste posto continua ativo. Reassocie o dispositivo ausente no outro posto." :
                    L"\nReassocie o dispositivo ausente: F6 no teclado ou clique direito no mouse.";
            else status += capture ? L"\nComplete os dois pares: F6 em cada teclado; clique direito em cada mouse." :
                    L"\nAtive um painel para associar. São necessários dois teclados e dois mouses.";
        }
        else status += capture ? L"\nF7 reconecta; F8 libera; F9 desconecta seu posto; F12 encerra os dois." :
            L"\nEntrada pausada. Ative uma destas janelas para continuar; F12 encerra.";
        if (!smoke && !synthetic)
            status += L"\nTeclas encaminhadas: " + std::to_wstring(routed_keys[view.index]) +
                L" | Cliques: " + std::to_wstring(routed_clicks[view.index]) +
                L" | Teclas sem rota: " + std::to_wstring(unassigned_keys) +
                L" | Resets F8: " + std::to_wstring(full_resets);
        RECT text{ 10, 8, client.right - 10, 82 };
        DrawTextW(dc, status.c_str(), -1, &text, DT_LEFT | DT_NOPREFIX);
        RECT image = surface(client, view.index);
        FillRect(dc, &image, GetSysColorBrush(COLOR_APPWORKSPACE));
        if (connected(view.index)) transports[view.index]->draw(dc, image);
        else if (!smoke) {
            const wchar_t* unavailable = timed_out[view.index] ? L"Prazo de conexão excedido; F7 reconecta." :
                state_text(transports[view.index]->state());
            DrawTextW(dc, unavailable, -1, &image, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
        if (capture && router.routable(view.index)) {
            const SeatInput& seat = router.seat(view.index);
            const int x = image.left + static_cast<int>(static_cast<long long>(seat.x) *
                (image.right - image.left) / seat.width);
            const int y = image.top + static_cast<int>(static_cast<long long>(seat.y) *
                (image.bottom - image.top) / seat.height);
            const HGDIOBJ pen = SelectObject(dc, GetStockObject(DC_PEN));
            SetDCPenColor(dc, RGB(230, 45, 45));
            MoveToEx(dc, x - 7, y, nullptr); LineTo(dc, x + 8, y);
            MoveToEx(dc, x, y - 7, nullptr); LineTo(dc, x, y + 8);
            SelectObject(dc, pen);
        }
        std::wstring footer = L"VM auxiliar por RDP; áudio/desempenho/jogos/multiseat nativo não validados.";
        text = { 10, client.bottom - 24, client.right - 10, client.bottom };
        DrawTextW(dc, footer.c_str(), -1, &text, DT_LEFT | DT_NOPREFIX);
        SelectObject(dc, previous);
    }
    void paint_buffered(View& view, HDC target) {
        RECT client{};
        if (!GetClientRect(view.window, &client)) throw std::runtime_error("Cannot read local view dimensions.");
        if (client.right <= 0 || client.bottom <= 0) return;
        HDC memory = view.buffer.prepare(target, client.right, client.bottom);
        paint(view, memory);
        if (!BitBlt(target, 0, 0, client.right, client.bottom, memory, 0, 0, SRCCOPY))
            throw std::runtime_error("Cannot present the local view paint buffer.");
    }
    void click_fixture_editor(unsigned index) {
        auto send = [this, index](InputEvent event) {
            if (!transports[index]->enqueue(event)) throw std::runtime_error("Synthetic RDP send failed.");
        };
        send({EventKind::Move, 0, false, false, false, 250, 180});
        send({EventKind::Button, 0, true, false, false, 250, 180});
        send({EventKind::Button, 0, false, false, false, 250, 180});
    }
    void send_fixture_keys(unsigned index) {
        auto send = [this, index](InputEvent event) {
            if (!transports[index]->enqueue(event)) throw std::runtime_error("Synthetic RDP send failed.");
        };
        constexpr unsigned keys[] = { 0x13, 0x20, 0x19, 0x0C, 0x1F, 0x12, 0x1E, 0x14, 0x0C };
        for (unsigned code : keys) { send({EventKind::Key, code, true}); send({EventKind::Key, code, false}); }
        const unsigned ending = index == 0 ? 0x1E : 0x30;
        send({EventKind::Key, ending, true}); send({EventKind::Key, ending, false});
    }
    void send_fixture(unsigned index) {
        click_fixture_editor(index);
        send_fixture_keys(index);
    }
    void restart(unsigned index) {
        router.set_enabled(index, false);
        transports[index]->stop();
        const unsigned toggles = transports[index]->toggle_states();
        transports[index] = std::make_unique<Transport>(config, index, views[index].window, toggles);
        send_failed[index] = timed_out[index] = failure_reported[index] = false;
        last_progress[index].clear();
        attempt_started[index] = GetTickCount64();
        transports[index]->start();
        std::cout << "RETRY seat=" << index + 1 << std::endl;
    }
    void disconnect(unsigned index) {
        router.set_enabled(index, false);
        transports[index]->stop();
        send_failed[index] = false;
        std::cout << "DISCONNECT seat=" << index + 1 << std::endl;
    }
    void disconnect_device(HANDLE device) {
        if (smoke || synthetic || closing || !foreground() || !device) return;
        for (unsigned i = 0; i < transports.size(); ++i)
            if (router.routable(i) && router.seat(i).keyboard == device && connected(i)) { disconnect(i); break; }
        update_gate();
        invalidate();
    }
    void retry_unavailable() {
        if (smoke || synthetic || closing || !foreground()) return;
        for (unsigned i = 0; i < transports.size(); ++i)
            if (timed_out[i] || terminal_state(transports[i]->state())) restart(i);
        update_gate();
        invalidate();
    }
    void tick_recovery(ULONGLONG now) {
        switch (recovery_phase) {
        case RecoveryPhase::Initial:
            if (now - started >= 120000) throw std::runtime_error("Recovery preparation timed out.");
            if (both_connected() && !connected_since) connected_since = now;
            if (connected_since && now - connected_since >= 30000) {
                // The external controller must validate both empty owned
                // editors during this delay. This mode never registers Raw Input.
                recovery_phase = RecoveryPhase::PeerOffline;
                recovery_phase_since = now;
                disconnect(0);
                if (!connected(1)) throw std::runtime_error("Recovery peer was lost.");
                send_fixture(1);
                recovery_phase_since = GetTickCount64();
                std::cout << "RECOVERY: seat1 disconnected; seat2 fixture sent; guest observation required." << std::endl;
            }
            break;
        case RecoveryPhase::PeerOffline:
            if (now - recovery_phase_since >= 20000) {
                restart(0);
                recovery_phase = RecoveryPhase::Reconnecting;
                recovery_phase_since = GetTickCount64();
                std::cout << "RECOVERY: reconnecting seat1; seat2 retained." << std::endl;
            }
            break;
        case RecoveryPhase::Reconnecting:
            if (now - recovery_phase_since >= 60000) throw std::runtime_error("Recovery reconnection timed out.");
            if (both_connected()) {
                click_fixture_editor(0);
                recovery_phase = RecoveryPhase::PreparingEditor;
                recovery_phase_since = now;
                std::cout << "RECOVERY: seat1 reconnected; editor preparation; guest preflight required." << std::endl;
            }
            break;
        case RecoveryPhase::PreparingEditor:
            // Let the GUI process activation independently of the RDP handshake.
            // The external controller must revalidate the empty owned editor
            // during this window and abort if its focus or identity is wrong.
            if (now - recovery_phase_since >= 10000) {
                if (!both_connected()) throw std::runtime_error("Recovery input readiness was lost.");
                send_fixture_keys(0);
                sent_synthetic = true;
                synthetic_sent_at = now;
                recovery_phase = RecoveryPhase::Finished;
                std::cout << "RECOVERY: seat1 fixture sent after reconnect; guest observation required." << std::endl;
            }
            break;
        case RecoveryPhase::Finished:
            if (now - synthetic_sent_at >= 45000) close(0);
            break;
        }
    }
    void tick() {
        if (closing) return;
        const ULONGLONG now = GetTickCount64();
        if (smoke) { if (now - started >= 200) close(0); return; }
        report_progress(false);
        for (unsigned i = 0; i < transports.size(); ++i) {
            // Each connection owns its deadline and input state. A failed peer
            // cannot release the healthy peer's held keys or stop its worker.
            if (!synthetic && (send_failed[i] || (!connected(i) && !terminal_state(transports[i]->state()) &&
                now - attempt_started[i] >= 60000))) {
                router.set_enabled(i, false);
                timed_out[i] = !send_failed[i];
                transports[i]->stop();
                send_failed[i] = false;
            }
            const TransportState state = transports[i]->state();
            if (terminal_state(state)) {
                if (!failure_reported[i]) {
                    failure_reported[i] = true;
                    std::cout << "RDP seat " << i + 1 << " unavailable; state=" << static_cast<int>(state)
                        << " code=" << transports[i]->error_code() << " " << transports[i]->diagnostics() << std::endl;
                }
                const bool expected_disconnect = recovery && i == 0 && recovery_phase == RecoveryPhase::PeerOffline;
                if (synthetic && !expected_disconnect) { close(1); return; }
            }
        }
        update_gate();
        if (recovery) { tick_recovery(now); invalidate(); return; }
        if (synthetic) {
            if (!sent_synthetic && now - started >= 120000)
                throw std::runtime_error("Synthetic transport check timed out.");
            if (both_connected() && !connected_since) connected_since = now;
            const ULONGLONG fixture_delay = unattended ? 30000 : 10000;
            if (!sent_synthetic && connected_since && now - connected_since >= fixture_delay) {
                if (!unattended && !foreground()) {
                    if (!waiting_foreground_reported) {
                        std::cout << "WaitingForeground: activate one lab panel; no fixture sent." << std::endl;
                        waiting_foreground_reported = true;
                    }
                } else {
                    send_fixture(0); send_fixture(1);
                    sent_synthetic = true; synthetic_sent_at = now;
                    std::cout << "FRAMES: seat1=" << transports[0]->frame_count() << " seat2=" <<
                        transports[1]->frame_count() << std::endl;
                    std::cout << "SENT: two per-context mouse/key fixtures; guest observation is required." << std::endl;
                }
            }
            if (sent_synthetic && now - synthetic_sent_at >= 45000) { close(0); return; }
        }
        invalidate();
    }
    void close(int exit_code) {
        if (closing) return;
        closing = true;
        result = exit_code;
        capture = false;
        report_input_counts();
        if (!cursor.release()) result = 1;
        router.release_all();
        unregister_input();
        report_progress(true);
        PostQuitMessage(result);
    }
    int run() {
        if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) &&
            GetAwarenessFromDpiAwarenessContext(GetThreadDpiAwarenessContext()) != DPI_AWARENESS_PER_MONITOR_AWARE)
            throw std::runtime_error("Cannot establish per-monitor DPI coordinates.");
        WNDCLASSEXW type{};
        type.cbSize = sizeof(type); type.hInstance = GetModuleHandleW(nullptr);
        type.lpszClassName = window_class; type.lpfnWndProc = procedure;
        type.hCursor = nullptr;
        if (!RegisterClassExW(&type)) throw std::runtime_error("Cannot register lab view class.");
        std::vector<RECT> monitors;
        if (!smoke && !EnumDisplayMonitors(nullptr, nullptr, collect_monitor, reinterpret_cast<LPARAM>(&monitors)))
            throw std::runtime_error("Cannot enumerate monitors.");
        if (!smoke) for (const SeatConfig& seat : config.seats)
            if (seat.monitor_index >= monitors.size()) throw std::runtime_error("A configured monitor is unavailable.");
        for (unsigned i = 0; i < views.size(); ++i) {
            views[i].app = this;
            views[i].index = i;
            const RECT rectangle = smoke ? RECT{ 0, 0, 800, 640 } : monitors[config.seats[i].monitor_index];
            const std::wstring title = L"Multiseat — laboratório RDP — posto " + std::to_wstring(i + 1);
            views[i].window = CreateWindowExW(smoke ? WS_EX_NOACTIVATE : 0, window_class, title.c_str(),
                WS_OVERLAPPEDWINDOW, rectangle.left, rectangle.top, rectangle.right - rectangle.left,
                rectangle.bottom - rectangle.top, nullptr, nullptr, GetModuleHandleW(nullptr), &views[i]);
            if (!views[i].window) throw std::runtime_error("Cannot create RDP lab view.");
            transports[i] = std::make_unique<Transport>(config, i, views[i].window);
        }
        if (smoke) for (View& view : views) {
            // Exercise actual buffered painting without showing/activating a
            // window, connecting a transport, or registering Raw Input.
            HDC dc = GetDC(view.window);
            if (!dc) throw std::runtime_error("Cannot open the hidden smoke view DC.");
            try { paint_buffered(view, dc); }
            catch (...) { ReleaseDC(view.window, dc); throw; }
            ReleaseDC(view.window, dc);
        }
        started = GetTickCount64();
        if (!SetTimer(views[0].window, 1, 100, nullptr)) throw std::runtime_error("Cannot create lab gate timer.");
        if (!smoke) {
            ShowWindow(views[1].window, SW_SHOWNOACTIVATE);
            ShowWindow(views[0].window, SW_SHOW);
            SetForegroundWindow(views[0].window);
            for (unsigned i = 0; i < transports.size(); ++i) {
                attempt_started[i] = GetTickCount64();
                transports[i]->start();
            }
        }
        MSG message{};
        int status = 0;
        while ((status = static_cast<int>(GetMessageW(&message, nullptr, 0, 0))) > 0) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        if (status == -1) result = 1;
        if (!cursor.release()) result = 1;
        router.release_all();
        unregister_input();
        for (auto& transport : transports) transport->stop();
        report_progress(true);
        return result;
    }
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
        View* view = reinterpret_cast<View*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            auto* creation = reinterpret_cast<CREATESTRUCTW*>(lparam);
            view = static_cast<View*>(creation->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(view));
        }
        if (!view) return DefWindowProcW(window, message, wparam, lparam);
        try {
            Application& app = *view->app;
            switch (message) {
            case WM_INPUT:
                app.raw(reinterpret_cast<HRAWINPUT>(lparam));
                return DefWindowProcW(window, message, wparam, lparam);
            case WM_INPUT_DEVICE_CHANGE:
                if (wparam == GIDC_REMOVAL || wparam == GIDC_ARRIVAL) {
                    app.router.device_changed(wparam, reinterpret_cast<HANDLE>(lparam));
                    app.update_gate(); app.invalidate();
                }
                return 0;
            case WM_TIMER: app.tick(); return 0;
            case WM_ACTIVATEAPP: app.update_gate(); app.invalidate(); return 0;
            case WM_KEYDOWN:
                if (wparam == VK_F12) app.close(0);
                else if (wparam == VK_F7) app.retry_unavailable();
                return 0;
            case WM_CLOSE: app.close(0); return 0;
            case WM_SETCURSOR:
                if (LOWORD(lparam) == HTCLIENT) {
                    SetCursor(app.cursor.active() && app.capture && app.foreground() ? nullptr : LoadCursorW(nullptr, IDC_ARROW));
                    return TRUE;
                }
                break;
            case WM_ERASEBKGND: return 1;
            case WM_SIZE: InvalidateRect(window, nullptr, FALSE); return 0;
            case frame_message: case state_message: app.update_gate(); app.invalidate(); return 0;
            case WM_PRINTCLIENT: app.paint_buffered(*view, reinterpret_cast<HDC>(wparam)); return 0;
            case WM_PAINT: {
                PAINTSTRUCT paint{}; HDC dc = BeginPaint(window, &paint);
                try { app.paint_buffered(*view, dc); }
                catch (...) { EndPaint(window, &paint); throw; }
                EndPaint(window, &paint); return 0;
            }
            }
        } catch (...) {
            std::cerr << "FAIL: local frontend operation failed; no private configuration was printed.\n";
            view->app->close(1);
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }
};
Config smoke_config() {
    Config result;
    for (unsigned i = 0; i < result.seats.size(); ++i) {
        result.seats[i].seat_id = i + 1;
        result.seats[i].desktop_width = 640; result.seats[i].desktop_height = 480;
    }
    return result;
}
}
}
int wmain(int argc, wchar_t** argv) {
    using namespace rds_lab;
    const HRESULT initialized = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(initialized)) { std::cerr << "FAIL: Windows Runtime initialization.\n"; return 1; }
    int result = 1;
    try {
        // This scope outlives every Application/Transport created below, so
        // worker threads and RDP contexts are gone before WSACleanup runs.
        SocketRuntime sockets;
        bool smoke = false, self_test = false, synthetic = false, check_config = false, unattended = false, recovery = false;
        std::wstring path;
        for (int i = 1; i < argc; ++i) {
            const std::wstring argument(argv[i]);
            if (argument == L"--smoke") smoke = true;
            else if (argument == L"--self-test") self_test = true;
            else if (argument == L"--transport-check") synthetic = true;
            else if (argument == L"--unattended-fixtures") unattended = true;
            else if (argument == L"--recovery-cycle") recovery = true;
            else if (argument == L"--check-config") check_config = true;
            else if (argument == L"--config" && i + 1 < argc) path = argv[++i];
            else throw std::runtime_error("Usage: ms-rds-lab-client [--config local.json] [--check-config|--transport-check [--unattended-fixtures [--recovery-cycle]]|--smoke|--self-test]");
        }
        if (static_cast<int>(smoke) + self_test + synthetic + check_config > 1 ||
            ((smoke || self_test) && !path.empty()) || (unattended && !synthetic) || (recovery && (!synthetic || !unattended)))
            throw std::runtime_error("Conflicting command options.");
        if (self_test) result = run_self_tests() ? 0 : 1;
        else {
            if (!Transport::library_smoke()) throw std::runtime_error("FreeRDP context smoke failed.");
            if (smoke) {
                Application application(smoke_config(), true, false);
                result = application.run();
                if (result == 0) std::cout << "PASS: two local views and FreeRDP contexts; no connection or Raw Input registration.\n";
            } else {
                Config config = load_config(path.empty() ? default_config_path() : path);
                if (check_config) {
                    for (const SeatConfig& seat : config.seats) {
                        auto password = unprotect_password(seat.password_dpapi);
                        SecureZeroMemory(password.data(), password.size());
                    }
                    std::cout << "PASS: local JSON/pin/DPAPI validated; no connection or input capture.\n";
                    result = 0;
                } else {
                    if (!Transport::enable_safe_ssl_diagnostics())
                        throw std::runtime_error("Cannot enable bounded numeric TLS diagnostics.");
                    std::cout << "Windows Server lab only. Two RDP contexts; no host session, service or driver changes.\n";
                    std::cout << "F6 pairs keyboards, right clicks pair mice; F7 retries unavailable seats, F8 resets, F9 disconnects the paired seat, F12 closes both. Host focus loss pauses input.\n";
                    if (unattended)
                        std::cout << "UNATTENDED: fixed VM fixtures after 30 seconds with both frames; host focus is not required. Verify the owned guest Notepads before sending." << std::endl;
                    Application application(std::move(config), false, synthetic, unattended, recovery);
                    result = application.run();
                }
            }
        }
    } catch (const std::exception& error) {
        // Our exception messages are fixed categories, never OS/FreeRDP text or JSON content.
        std::cerr << "FAIL: " << error.what() << '\n';
        result = 1;
    } catch (...) { std::cerr << "FAIL: unexpected local lab failure.\n"; result = 1; }
    RoUninitialize();
    return result;
}
