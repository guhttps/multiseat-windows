#include <windows.h>
#include <iostream>
#include <string>
#include <vector>

namespace {
std::wstring object_name(HANDLE object) {
    DWORD bytes = 0;
    if (!object) return {};
    GetUserObjectInformationW(object, UOI_NAME, nullptr, 0, &bytes);
    if (!bytes) return {};
    std::vector<wchar_t> buffer((bytes + sizeof(wchar_t) - 1) / sizeof(wchar_t));
    if (!GetUserObjectInformationW(object, UOI_NAME, buffer.data(), bytes, &bytes)) return {};
    return buffer.data();
}

std::wstring input_name() {
    const HDESK desktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    const auto name = object_name(desktop);
    if (desktop) CloseDesktop(desktop);
    return name;
}

int error(const char* operation) {
    std::cerr << operation << " failed; Windows error=" << GetLastError() << '\n';
    return 1;
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc == 3 && std::wstring(argv[1]) == L"--child") {
        const auto desktop = object_name(GetThreadDesktop(GetCurrentThreadId()));
        const auto active = input_name();
        std::wcout << L"Child desktop: " << desktop << L"\nChild input desktop: " << active << std::endl;
        return !desktop.empty() && !active.empty() && desktop == argv[2] && desktop != active ? 0 : 1;
    }
    if (argc != 1) {
        std::cerr << "Run ms-desktop-probe without arguments.\n";
        return 1;
    }
    const auto station = object_name(GetProcessWindowStation());
    const auto parent = object_name(GetThreadDesktop(GetCurrentThreadId()));
    const auto before = input_name();
    if (station.empty() || parent.empty() || before.empty()) return error("Read parent context");
    if (station != L"WinSta0") {
        std::cerr << "Run this experiment in an interactive Windows session.\n";
        return 1;
    }
    const auto name = L"MultiseatProbe_" + std::to_wstring(GetCurrentProcessId());
    const HDESK desktop = CreateDesktopW(name.c_str(), nullptr, nullptr, 0,
        DESKTOP_CREATEWINDOW | DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS, nullptr);
    if (!desktop) return error("CreateDesktop");
    std::vector<wchar_t> executable(32768);
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (!length || length >= executable.size()) {
        const int result = error("GetModuleFileName");
        CloseDesktop(desktop);
        return result;
    }
    auto command = L"\"" + std::wstring(executable.data()) + L"\" --child " + name;
    auto target = station + L"\\" + name;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.lpDesktop = target.data();
    PROCESS_INFORMATION process{};
    std::wcout << L"Parent desktop: " << parent << L"\nInput desktop before: " << before
               << L"\nTemporary desktop: " << name << std::endl;
    if (!CreateProcessW(executable.data(), command.data(), nullptr, nullptr, FALSE, 0,
                        nullptr, nullptr, &startup, &process)) {
        const int result = error("CreateProcess");
        CloseDesktop(desktop);
        return result;
    }
    CloseHandle(process.hThread);
    const DWORD wait = WaitForSingleObject(process.hProcess, 10000);
    DWORD child_exit = 1;
    if (wait != WAIT_OBJECT_0) {
        std::cerr << "Child did not finish within the observation window; stopping this test child.\n";
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 5000);
    } else if (!GetExitCodeProcess(process.hProcess, &child_exit)) {
        error("GetExitCodeProcess");
    }
    CloseHandle(process.hProcess);
    const bool closed = CloseDesktop(desktop) != FALSE;
    const auto after = input_name();
    const bool passed = wait == WAIT_OBJECT_0 && child_exit == 0 && closed && !after.empty() && before == after;
    std::wcout << L"Child exit code: " << child_exit << L"\nInput desktop after: " << after
               << L"\nDesktop handle closed: " << (closed ? L"yes" : L"no") << L'\n'
               << (passed ? L"PASS" : L"FAIL") << L": separate desktop context; no simultaneous stations demonstrated.\n";
    return passed ? 0 : 1;
}
