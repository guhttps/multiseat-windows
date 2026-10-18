#include "network.h"
#include "config.h"
#include "input-router.h"
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
class Application {
public:
    Config config;
    std::array<View, 2> views{};
    std::array<std::unique_ptr<Transport>, 2> transports;
    InputRouter router;
    bool smoke = false, synthetic = false, unattended = false, closing = false, registered = false, capture = false;
    bool sent_synthetic = false, send_failed = false, waiting_foreground_reported = false;
    ULONGLONG started = 0, connected_since = 0, synthetic_sent_at = 0;
    std::array<std::string, 2> last_progress;
    bool initial_frames_ready = false;
    int result = 0;
    Application(Config source, bool smoke_mode, bool synthetic_mode, bool unattended_mode = false)
        : config(std::move(source)), router(sizes(config), [this](unsigned seat, const InputEvent& event) {
            if (transports[seat] && !transports[seat]->enqueue(event) &&
                transports[seat]->state() == TransportState::Connected) send_failed = true;
        }), smoke(smoke_mode), synthetic(synthetic_mode), unattended(unattended_mode) {}
    ~Application() {
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
    bool both_connected() const {
        return transports[0] && transports[1] && transports[0]->state() == TransportState::Connected &&
            transports[1]->state() == TransportState::Connected &&
            transports[0]->frame_count() > 0 && transports[1]->frame_count() > 0;
    }
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
        if (!registered) return;
        RAWINPUTDEVICE devices[] = { {1, 6, RIDEV_REMOVE, nullptr}, {1, 2, RIDEV_REMOVE, nullptr} };
        if (!RegisterRawInputDevices(devices, 2, sizeof(RAWINPUTDEVICE))) result = 1;
        registered = false;
    }
    void update_gate() {
        if (smoke || synthetic || closing) return;
        const bool enabled = foreground() && both_connected();
        if (!enabled && capture) router.release_all();
        capture = enabled;
        if (enabled && !registered) {
            RAWINPUTDEVICE devices[] = {
                {1, 6, RIDEV_NOLEGACY | RIDEV_DEVNOTIFY, views[0].window},
                {1, 2, RIDEV_NOLEGACY | RIDEV_DEVNOTIFY, views[0].window}
            };
            if (!RegisterRawInputDevices(devices, 2, sizeof(RAWINPUTDEVICE)))
                throw std::runtime_error("Raw Input registration failed.");
            registered = true;
        } else if (!enabled) unregister_input();
    }
    void invalidate() { for (const View& view : views) InvalidateRect(view.window, nullptr, FALSE); }
    void raw(HRAWINPUT handle) {
        if (!capture || !foreground() || !both_connected()) { update_gate(); return; }
        RAWINPUT input{};
        UINT size = sizeof(input);
        const UINT copied = GetRawInputData(handle, RID_INPUT, &input, &size, sizeof(RAWINPUTHEADER));
        if (copied == static_cast<UINT>(-1) || !multiseat::valid_raw_input(input, copied)) return;
        if (input.header.dwType == RIM_TYPEKEYBOARD) {
            const RAWKEYBOARD& key = input.data.keyboard;
            if (!(key.Flags & RI_KEY_BREAK) && key.VKey == VK_F12) { close(0); return; }
            if (!(key.Flags & RI_KEY_BREAK) && key.VKey == VK_F8) { router.reset(); invalidate(); return; }
            router.keyboard(input.header.hDevice, key, true);
        } else router.mouse(input.header.hDevice, input.data.mouse, true);
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
            L"\n" + (smoke ? L"Smoke local: nenhuma conexão ou captura" : state_text(transports[view.index]->state()));
        if (synthetic) {
            status += L"\nTeste sintético: não demonstra isolamento dos periféricos físicos.";
            if (unattended)
                status += L"\nAutoteste: fixtures fixas na VM; o foco do host não é exigido.";
            else if (waiting_foreground_reported && !sent_synthetic)
                status += L"\nClique em um destes painéis para permitir o envio do teste.";
        }
        else if (!router.ready()) {
            const SeatInput& seat = router.seat(view.index);
            status += L"\nTeclado: " + std::wstring(seat.keyboard ? L"associado" : L"aguardando F6") +
                L" | Mouse: " + std::wstring(seat.mouse ? L"associado" : L"aguardando clique direito");
            status += capture ? L"\nComplete os dois pares: F6 em cada teclado; clique direito em cada mouse." :
                L"\nAtive um painel para associar. São necessários dois teclados e dois mouses.";
        }
        else status += capture ? L"\nEntrada ativa por dispositivo. F8 reassocia; F12 encerra." :
            L"\nEntrada pausada. Ative uma destas janelas para continuar; F12 encerra.";
        RECT text{ 10, 8, client.right - 10, 82 };
        DrawTextW(dc, status.c_str(), -1, &text, DT_LEFT | DT_NOPREFIX);
        RECT image = surface(client, view.index);
        FillRect(dc, &image, GetSysColorBrush(COLOR_APPWORKSPACE));
        if (transports[view.index]) transports[view.index]->draw(dc, image);
        if (router.ready() && capture) {
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
    void send_fixture(unsigned index) {
        auto send = [this, index](InputEvent event) {
            if (!transports[index]->enqueue(event)) throw std::runtime_error("Synthetic RDP send failed.");
        };
        send({EventKind::Move, 0, false, false, false, 250, 180});
        send({EventKind::Button, 0, true, false, false, 250, 180});
        send({EventKind::Button, 0, false, false, false, 250, 180});
        constexpr unsigned keys[] = { 0x13, 0x20, 0x19, 0x0C, 0x1F, 0x12, 0x1E, 0x14, 0x0C };
        for (unsigned code : keys) { send({EventKind::Key, code, true}); send({EventKind::Key, code, false}); }
        const unsigned ending = index == 0 ? 0x1E : 0x30;
        send({EventKind::Key, ending, true}); send({EventKind::Key, ending, false});
    }
    void tick() {
        if (closing) return;
        const ULONGLONG now = GetTickCount64();
        if (smoke) { if (now - started >= 200) close(0); return; }
        report_progress(false);
        if (both_connected()) initial_frames_ready = true;
        // One fixed deadline for the initial pair of connections and first
        // frames. A partly connected peer cannot restart or extend it.
        if (!synthetic && !initial_frames_ready && now - started >= 60000) {
            std::cerr << "FAIL: initial RDP connection/frame deadline (60 seconds)." << std::endl;
            close(1); return;
        }
        update_gate();
        if (send_failed) throw std::runtime_error("RDP input queue failed.");
        for (unsigned i = 0; i < transports.size(); ++i) {
            const TransportState state = transports[i]->state();
            if (state == TransportState::Failed || state == TransportState::CertificateRejected ||
                state == TransportState::Closed) {
                std::cerr << "RDP seat " << i + 1 << " stopped; state=" << static_cast<int>(state)
                    << " code=" << transports[i]->error_code() << " " << transports[i]->diagnostics() << std::endl;
                close(1); return;
            }
        }
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
        type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
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
            for (auto& transport : transports) transport->start();
        }
        MSG message{};
        int status = 0;
        while ((status = static_cast<int>(GetMessageW(&message, nullptr, 0, 0))) > 0) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        if (status == -1) result = 1;
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
                if (wparam == GIDC_REMOVAL) { app.router.device_removed(reinterpret_cast<HANDLE>(lparam)); app.invalidate(); }
                return 0;
            case WM_TIMER: app.tick(); return 0;
            case WM_ACTIVATEAPP: app.update_gate(); app.invalidate(); return 0;
            case WM_KEYDOWN: if (wparam == VK_F12) app.close(0); return 0;
            case WM_CLOSE: app.close(0); return 0;
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
        bool smoke = false, self_test = false, synthetic = false, check_config = false, unattended = false;
        std::wstring path;
        for (int i = 1; i < argc; ++i) {
            const std::wstring argument(argv[i]);
            if (argument == L"--smoke") smoke = true;
            else if (argument == L"--self-test") self_test = true;
            else if (argument == L"--transport-check") synthetic = true;
            else if (argument == L"--unattended-fixtures") unattended = true;
            else if (argument == L"--check-config") check_config = true;
            else if (argument == L"--config" && i + 1 < argc) path = argv[++i];
            else throw std::runtime_error("Usage: ms-rds-lab-client [--config local.json] [--check-config|--transport-check [--unattended-fixtures]|--smoke|--self-test]");
        }
        if (static_cast<int>(smoke) + self_test + synthetic + check_config > 1 ||
            ((smoke || self_test) && !path.empty()) || (unattended && !synthetic))
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
                    std::cout << "F6 pairs keyboards, right clicks pair mice; F8 resets, F12 closes. Host focus loss pauses input.\n";
                    if (unattended)
                        std::cout << "UNATTENDED: fixed VM fixtures after 30 seconds with both frames; host focus is not required. Verify the owned guest Notepads before sending." << std::endl;
                    Application application(std::move(config), false, synthetic, unattended);
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
