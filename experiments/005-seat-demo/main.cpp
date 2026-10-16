#include <windows.h>
#include "../common/raw-input.h"
#include <array>
#include <iostream>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace {
// This routes events only to our own counters, never to another application.
struct Router {
    std::array<HANDLE, 2> keyboards{};
    std::array<HANDLE, 2> mice{};
    std::array<unsigned int, 2> keys{};
    std::array<unsigned int, 2> clicks{};
    bool ready() const { return keyboards[1] && mice[1]; }
    void event(HANDLE device, bool keyboard) {
        if (!device) return;
        auto& bindings = keyboard ? keyboards : mice;
        if (!ready()) {
            if (device == bindings[0] || device == bindings[1]) return;
            for (auto& binding : bindings) {
                if (!binding) { binding = device; return; }
            }
            return;
        }
        auto& counters = keyboard ? keys : clicks;
        for (size_t i = 0; i < bindings.size(); ++i) {
            if (bindings[i] == device) { ++counters[i]; return; }
        }
    }
    bool removed(HANDLE device) {
        if (!device) return false;
        for (size_t i = 0; i < keyboards.size(); ++i) {
            if (keyboards[i] == device || mice[i] == device) {
                *this = Router{};
                return true;
            }
        }
        return false;
    }
} router;
std::array<HWND, 2> panels{};
std::array<HWND, 2> labels{};
std::set<HANDLE> held;
std::vector<RECT> monitors;

bool self_test() {
    Router test;
    const auto k1 = reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(1));
    const auto k2 = reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(2));
    const auto m1 = reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(3));
    const auto m2 = reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(4));
    const auto other = reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(5));
    test.event(nullptr, true);
    test.event(k1, true); test.event(k1, true); test.event(k2, true);
    test.event(m1, false); test.event(m2, false);
    if (!test.ready() || test.keys[0] || test.clicks[1]) return false;
    for (unsigned int i = 0; i < 100; ++i) {
        test.event(k1, true); test.event(m2, false);
    }
    if (test.keys != std::array<unsigned int, 2>{100, 0} ||
        test.clicks != std::array<unsigned int, 2>{0, 100}) return false;
    for (unsigned int i = 0; i < 100; ++i) {
        test.event(k2, true); test.event(m1, false);
    }
    test.event(other, true); test.event(other, false);
    test.event(nullptr, false);
    if (test.keys != std::array<unsigned int, 2>{100, 100} ||
        test.clicks != std::array<unsigned int, 2>{100, 100}) return false;
    if (test.removed(other) || !test.ready()) return false;
    return test.removed(k1) && !test.ready() && !test.keys[0] && !test.clicks[1];
}

void refresh() {
    for (size_t i = 0; i < panels.size(); ++i) {
        const std::wstring text = L"PAINEL " + std::to_wstring(i + 1) +
            L"\r\n\r\nTeclado: " + (router.keyboards[i] ? L"associado" : L"aguardando F6") +
            L"\r\nMouse: " + (router.mice[i] ? L"associado" : L"aguardando clique direito") +
            L"\r\n\r\nF6: " + std::to_wstring(router.keys[i]) +
            L"\r\nCliques direitos: " + std::to_wstring(router.clicks[i]) +
            L"\r\n\r\n" + (router.ready() ? L"PRONTO: teste os dois pares sem trocar o foco."
                : L"Associacao: F6 no teclado 1, depois no 2.\r\nClique direito com mouse 1, depois com mouse 2.") +
            L"\r\n\r\nMantenha um destes paineis em primeiro plano.\r\n"
            L"F8 reinicia. Fechar qualquer painel encerra.\r\n"
            L"So F6 e clique direito alimentam os contadores.\r\n"
            L"Mesmo processo/sessao; aplicativos externos nao sao isolados.";
        SetWindowTextW(labels[i], text.c_str());
    }
}

BOOL CALLBACK collect_monitor(HMONITOR monitor, HDC, LPRECT, LPARAM) {
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) return FALSE;
    monitors.push_back(info.rcWork);
    return TRUE;
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_INPUT) {
        RAWINPUT input{};
        UINT bytes = sizeof(input);
        const UINT read = GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam), RID_INPUT,
            &input, &bytes, sizeof(RAWINPUTHEADER));
        if (read == static_cast<UINT>(-1)) {
            std::cerr << "GetRawInputData failed: " << GetLastError() << '\n';
        } else if (!multiseat::valid_raw_input(input, read)) {
            std::cerr << "Ignoring incomplete or unsupported raw input packet.\n";
        } else if (input.header.dwType == RIM_TYPEKEYBOARD) {
            const auto& key = input.data.keyboard;
            if (key.VKey == VK_F8 && (key.Flags & RI_KEY_BREAK) == 0) {
                router = Router{}; held.clear(); refresh();
            }
            if (key.VKey == VK_F6 && input.header.hDevice) {
                if ((key.Flags & RI_KEY_BREAK) != 0) held.erase(input.header.hDevice);
                else if (held.insert(input.header.hDevice).second) {
                    router.event(input.header.hDevice, true); refresh();
                }
            }
        } else if (input.header.dwType == RIM_TYPEMOUSE &&
                   (input.data.mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN) != 0) {
            router.event(input.header.hDevice, false); refresh();
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }
    if (message == WM_INPUT_DEVICE_CHANGE && wparam == GIDC_REMOVAL) {
        if (router.removed(reinterpret_cast<HANDLE>(lparam))) {
            held.clear(); refresh();
        }
        return 0;
    }
    if (message == WM_ACTIVATEAPP && !wparam) held.clear();
    if (message == WM_SIZE) {
        for (size_t i = 0; i < panels.size(); ++i) {
            if (panels[i] == window && labels[i])
                MoveWindow(labels[i], 20, 20, LOWORD(lparam) > 40 ? LOWORD(lparam) - 40 : 0,
                    HIWORD(lparam) > 40 ? HIWORD(lparam) - 40 : 0, TRUE);
        }
    }
    if (message == WM_CLOSE) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, wparam, lparam);
}
}

int main(int argc, char** argv) {
    const bool smoke = argc == 2 && std::string_view(argv[1]) == "--smoke";
    const bool test = argc == 2 && std::string_view(argv[1]) == "--self-test";
    if (argc > 1 && !smoke && !test) {
        std::cerr << "Usage: ms-seat-demo [--smoke|--self-test]\n"; return 1;
    }
    if (test) {
        const bool passed = self_test();
        std::cout << (passed ? "PASS" : "FAIL") << ": routing, unknown devices and removal; synthetic events only.\n";
        return passed ? 0 : 1;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (!EnumDisplayMonitors(nullptr, nullptr, collect_monitor, 0) || monitors.empty()) return 1;
    if (!smoke && monitors.size() < 2) {
        std::cerr << "Connect two monitors in extended desktop mode.\n"; return 1;
    }
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW type{};
    type.lpfnWndProc = window_proc;
    type.hInstance = instance;
    type.lpszClassName = L"MultiseatSeatDemo";
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassW(&type)) return 1;
    for (size_t i = 0; i < panels.size(); ++i) {
        const RECT area = monitors[i % monitors.size()];
        panels[i] = CreateWindowExW(0, type.lpszClassName,
            (L"Multiseat - painel " + std::to_wstring(i + 1)).c_str(), WS_OVERLAPPEDWINDOW,
            area.left + 30, area.top + 30, 650, 470, nullptr, nullptr, instance, nullptr);
        if (!panels[i]) return 1;
        labels[i] = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
            20, 20, 600, 390, panels[i], nullptr, instance, nullptr);
        if (!labels[i]) return 1;
    }
    // One receiver per device class; foreground only, no global suppression.
    RAWINPUTDEVICE devices[]{{1, 6, RIDEV_DEVNOTIFY, panels[0]},
                             {1, 2, RIDEV_DEVNOTIFY, panels[0]}};
    if (!RegisterRawInputDevices(devices, 2, sizeof(RAWINPUTDEVICE))) return 1;
    refresh();
    if (smoke) {
        for (HWND panel : panels) DestroyWindow(panel);
        std::cout << "PASS: two hidden panels and raw-input registration; physical routing not tested.\n";
        return 0;
    }
    for (HWND panel : panels) ShowWindow(panel, SW_SHOW);
    std::cout << "Associate keyboard 1/2 with F6, then mouse 1/2 with right clicks.\n"
        << "Foreground-only own-panel routing. F8 resets; close either panel to stop.\n";
    MSG message{};
    BOOL status;
    while ((status = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        TranslateMessage(&message); DispatchMessageW(&message);
    }
    for (HWND panel : panels) DestroyWindow(panel);
    return status == -1 ? 1 : 0;
}
