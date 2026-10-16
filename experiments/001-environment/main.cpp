#include <windows.h>
#include <iostream>
#include <string>
#include <vector>

bool print_object_name(const wchar_t* label, HANDLE object) {
    if (!object) {
        std::wcerr << label << L": handle unavailable; error=" << GetLastError() << L'\n';
        return false;
    }
    DWORD bytes = 0;
    GetUserObjectInformationW(object, UOI_NAME, nullptr, 0, &bytes);
    if (bytes == 0) {
        std::wcerr << label << L": size query failed; error=" << GetLastError() << L'\n';
        return false;
    }
    std::vector<wchar_t> name((bytes + sizeof(wchar_t) - 1) / sizeof(wchar_t), L'\0');
    if (!GetUserObjectInformationW(object, UOI_NAME, name.data(), bytes, &bytes)) {
        std::wcerr << label << L": name query failed; error=" << GetLastError() << L'\n';
        return false;
    }
    std::wcout << label << L": " << name.data() << L'\n';
    return true;
}

int main() {
    DWORD session = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &session)) {
        std::wcerr << L"ProcessIdToSessionId failed; error=" << GetLastError() << L'\n';
        return 1;
    }
    std::wcout << L"Multiseat Windows - environment diagnostic\n"
               << L"Process session: " << session << L'\n';
    const DWORD console = WTSGetActiveConsoleSessionId();
    if (console == 0xFFFFFFFF) {
        std::wcout << L"Console session: no session attached\n";
    } else {
        std::wcout << L"Console session: " << console << L'\n';
    }
    // These are borrowed handles owned by Windows; do not close them.
    const bool station = print_object_name(L"Window station", GetProcessWindowStation());
    const bool desktop = print_object_name(L"Thread desktop", GetThreadDesktop(GetCurrentThreadId()));
    std::wcout << L"This diagnostic does not create independent seats.\n";
    return (station && desktop) ? 0 : 1;
}
