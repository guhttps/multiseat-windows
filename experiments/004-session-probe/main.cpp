#include <windows.h>
#include <wtsapi32.h>
#include <iostream>
#include <cstring>

namespace {
const char* state_name(WTS_CONNECTSTATE_CLASS state) {
    switch (state) {
    case WTSActive: return "Active";
    case WTSConnected: return "Connected";
    case WTSConnectQuery: return "ConnectQuery";
    case WTSShadow: return "Shadow";
    case WTSDisconnected: return "Disconnected";
    case WTSIdle: return "Idle";
    case WTSListen: return "Listen";
    case WTSReset: return "Reset";
    case WTSDown: return "Down";
    case WTSInit: return "Init";
    default: return "Unknown";
    }
}

struct DisplaySummary {
    unsigned int count = 0;
    bool failed = false;
};

BOOL CALLBACK monitor_info(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
    auto& summary = *reinterpret_cast<DisplaySummary*>(data);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) {
        std::cerr << "GetMonitorInfo failed: " << GetLastError() << '\n';
        summary.failed = true;
        return FALSE;
    }
    ++summary.count;
    std::cout << "Monitor " << summary.count << ": desktop rectangle ["
              << info.rcMonitor.left << ", " << info.rcMonitor.top << ", "
              << info.rcMonitor.right << ", " << info.rcMonitor.bottom << "]"
              << ((info.dwFlags & MONITORINFOF_PRIMARY) ? " primary" : "") << '\n';
    return TRUE;
}
}

int main() {
    // DPI awareness affects coordinate reporting, not the display configuration.
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        std::cout << "DPI awareness unavailable; coordinates may be virtualized; error="
                  << GetLastError() << '\n';
    }
    DWORD current = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &current)) {
        std::cerr << "ProcessIdToSessionId failed: " << GetLastError() << '\n';
        return 1;
    }
    const DWORD console = WTSGetActiveConsoleSessionId();
    std::cout << "Current process session: " << current << '\n';
    if (console == 0xFFFFFFFF) std::cout << "Physical console session: none attached\n";
    else std::cout << "Physical console session: " << console << '\n';

    PWTS_SESSION_INFOW sessions = nullptr;
    DWORD count = 0;
    if (!WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &sessions, &count)) {
        std::cerr << "WTSEnumerateSessions failed: " << GetLastError() << '\n';
        return 1;
    }
    DWORD active = 0;
    for (DWORD i = 0; i < count; ++i) {
        const auto& session = sessions[i];
        if (session.State == WTSActive) ++active;
        std::cout << "Session " << session.SessionId << ": " << state_name(session.State);
        if (session.SessionId == current) std::cout << " [this process]";
        if (console != 0xFFFFFFFF && session.SessionId == console) std::cout << " [physical console]";
        LPWSTR buffer = nullptr;
        DWORD bytes = 0;
        if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, session.SessionId,
                                       WTSClientProtocolType, &buffer, &bytes)) {
            if (buffer && bytes >= sizeof(USHORT)) {
                USHORT protocol = 0;
                std::memcpy(&protocol, buffer, sizeof(protocol));
                std::cout << " protocol=" << protocol
                          << (protocol == 0 ? " (console protocol)" : protocol == 2 ? " (RDP)" : " (other)");
            } else std::cout << " protocol=unavailable (short response)";
            WTSFreeMemory(buffer);
        } else {
            const DWORD query_error = GetLastError();
            if (buffer) WTSFreeMemory(buffer);
            std::cout << " protocol=unavailable error=" << query_error;
        }
        std::cout << '\n';
    }
    WTSFreeMemory(sessions);
    std::cout << "Enumerated sessions: " << count << " | state Active: " << active
              << " (not a count of independent physical stations)\n";
    DisplaySummary displays;
    if (!EnumDisplayMonitors(nullptr, nullptr, monitor_info, reinterpret_cast<LPARAM>(&displays)) || displays.failed) {
        std::cerr << "Display enumeration incomplete.\n";
        return 1;
    }
    std::cout << "Monitors enumerated in this desktop: " << displays.count << '\n'
              << "Session and monitor lists are separate; no per-monitor session assignment is demonstrated.\n"
              << "Read-only snapshot; no accounts, session state or display layout changed.\n";
    return 0;
}
