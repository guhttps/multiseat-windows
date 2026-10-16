#include <windows.h>
#include "../common/raw-input.h"
#include <iostream>
#include <map>
#include <string>
#include <string_view>

namespace {
std::map<HANDLE, unsigned int> keyboards;
std::map<HANDLE, unsigned int> mice;
unsigned int presses = 0;
unsigned int clicks = 0;
unsigned int next_keyboard_id = 1;
unsigned int next_mouse_id = 1;

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_INPUT) {
        // Keyboard and mouse packets fit in RAWINPUT; variable-size HID is not registered.
        RAWINPUT input{};
        UINT bytes = sizeof(input);
        const UINT read = GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam), RID_INPUT,
                                          &input, &bytes, sizeof(RAWINPUTHEADER));
        if (read == static_cast<UINT>(-1)) {
            std::cerr << "GetRawInputData failed: " << GetLastError() << '\n';
        } else if (!multiseat::valid_raw_input(input, read)) {
            std::cerr << "Ignoring incomplete or unsupported raw input packet.\n";
        } else if (
                   input.header.dwType == RIM_TYPEKEYBOARD &&
                   input.data.keyboard.VKey == VK_F6 &&
                   (input.data.keyboard.Flags & RI_KEY_BREAK) == 0) {
            if (!input.header.hDevice) {
                std::cout << "F6: device unavailable; cannot identify its origin.\n" << std::flush;
            } else {
                const auto entry = keyboards.try_emplace(input.header.hDevice, next_keyboard_id);
                if (entry.second) ++next_keyboard_id;
                const auto id = entry.first->second;
                ++presses;
                const auto result = L"F6: Teclado " + std::to_wstring(id) +
                    L" | Eventos: " + std::to_wstring(presses);
                SetWindowTextW(window, result.c_str());
                std::cout << "F6: Keyboard " << id << " | event " << presses << '\n' << std::flush;
            }
        }
        if (multiseat::valid_raw_input(input, read) &&
            input.header.dwType == RIM_TYPEMOUSE &&
            (input.data.mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN) != 0) {
            if (!input.header.hDevice) {
                std::cout << "Right click: device unavailable; cannot identify its origin.\n" << std::flush;
            } else {
                const auto entry = mice.try_emplace(input.header.hDevice, next_mouse_id);
                if (entry.second) ++next_mouse_id;
                const auto id = entry.first->second;
                ++clicks;
                const auto result = L"Clique direito: Mouse " + std::to_wstring(id) +
                    L" | Eventos: " + std::to_wstring(clicks);
                SetWindowTextW(window, result.c_str());
                std::cout << "Right click: Mouse " << id << " | event " << clicks << '\n' << std::flush;
            }
        }
        // Windows requires default processing to clean up foreground raw input.
        return DefWindowProcW(window, message, wparam, lparam);
    }
    if (message == WM_INPUT_DEVICE_CHANGE && wparam == GIDC_REMOVAL) {
        const auto device = reinterpret_cast<HANDLE>(lparam);
        const auto removed_keyboards = keyboards.erase(device);
        const auto removed_mice = mice.erase(device);
        if (removed_keyboards || removed_mice)
            std::cout << "Device removed; its temporary ID was retired.\n" << std::flush;
        return 0;
    }
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
}

int main(int argc, char** argv) {
    const bool smoke = argc == 2 && std::string_view(argv[1]) == "--smoke";
    if (argc > 1 && !smoke) {
        std::cerr << "Usage: ms-keyboard-probe [--smoke]\n";
        return 1;
    }
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW type{};
    type.lpfnWndProc = window_proc;
    type.hInstance = instance;
    type.lpszClassName = L"MultiseatKeyboardProbe";
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassW(&type)) {
        std::cerr << "RegisterClass failed: " << GetLastError() << '\n';
        return 1;
    }
    const HWND window = CreateWindowExW(0, type.lpszClassName, L"Teste de teclados e mouses - clique aqui",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 700, 220,
        nullptr, nullptr, instance, nullptr);
    if (!window) {
        std::cerr << "CreateWindow failed: " << GetLastError() << '\n';
        return 1;
    }
    // Foreground only: no background capture, suppression or redirection.
    RAWINPUTDEVICE devices[]{{0x01, 0x06, RIDEV_DEVNOTIFY, window},
                             {0x01, 0x02, RIDEV_DEVNOTIFY, window}};
    if (!RegisterRawInputDevices(devices, 2, sizeof(RAWINPUTDEVICE))) {
        std::cerr << "RegisterRawInputDevices failed: " << GetLastError() << '\n';
        DestroyWindow(window);
        return 1;
    }
    if (smoke) {
        std::cout << "Window creation and keyboard/mouse registration OK; hardware input not tested.\n";
        DestroyWindow(window);
        return 0;
    }
    CreateWindowExW(0, L"STATIC",
        L"Clique nesta janela e pressione F6 no primeiro teclado.\n"
        L"Alterne F6 nos teclados ou clique direito com cada mouse nesta janela.\n"
        L"Veja o numero no titulo e no terminal. Feche esta janela para sair.\n"
        L"Apenas F6 e clique direito sao exibidos. Os IDs sao temporarios.",
        WS_CHILD | WS_VISIBLE, 20, 20, 640, 120, window, nullptr, instance, nullptr);
    ShowWindow(window, SW_SHOW);
    std::cout << "Click the test window; press F6 on each keyboard. Close the window to stop.\n"
              << "Right-click inside the test window with each mouse.\n"
              << "Only F6 and right clicks are displayed; IDs are temporary. No input isolation is provided.\n";
    MSG message{};
    BOOL status;
    while ((status = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (status == -1) {
        std::cerr << "GetMessage failed: " << GetLastError() << '\n';
        return 1;
    }
    return 0;
}
