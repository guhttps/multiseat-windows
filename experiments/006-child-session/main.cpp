#include <windows.h>
#include <wtsapi32.h>
#include <oleauto.h>
#include <iostream>
#include <limits>

// Import the installed control's actual ABI, rather than guessing its vtable.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4192)
#import "mstscax.dll" raw_interfaces_only named_guids
#pragma warning(pop)
#else
#error This capability experiment requires MSVC type-library import.
#endif

int main() {
    DWORD parent = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &parent)) return 1;
    std::cout << "Current session: " << parent << '\n';
    BOOL enabled = FALSE;
    if (!WTSIsChildSessionsEnabled(&enabled)) {
        std::cerr << "WTSIsChildSessionsEnabled failed: " << GetLastError() << '\n';
        return 1;
    }
    std::cout << "Child sessions enabled: " << (enabled ? "yes" : "no") << '\n';
    constexpr ULONG no_child = std::numeric_limits<ULONG>::max();
    ULONG child = no_child;
    if (!WTSGetChildSessionId(&child)) {
        const DWORD error = GetLastError();
        std::cout << "Child session ID unavailable: error=" << error << '\n';
        // On this host, the disabled feature reports ERROR_NOT_FOUND.
        // Preserve that result, without claiming a successful enumeration.
        if (error != ERROR_NOT_FOUND) return 1;
    } else if (child == no_child) std::cout << "Child session: none\n";
    else std::cout << "Child session: " << child << '\n';
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) {
        std::cerr << "CoInitializeEx failed: " << std::hex << initialized << '\n';
        return 1;
    }
    MSTSCLib::IMsRdpExtendedSettings* settings = nullptr;
    const HRESULT created = CoCreateInstance(MSTSCLib::CLSID_MsRdpClient10NotSafeForScripting,
        nullptr, CLSCTX_INPROC_SERVER, __uuidof(MSTSCLib::IMsRdpExtendedSettings),
        reinterpret_cast<void**>(&settings));
    bool passed = false;
    if (FAILED(created)) {
        std::cerr << "RDP extended settings unavailable: " << std::hex << created << '\n';
    } else {
        BSTR name = SysAllocString(L"ConnectToChildSession");
        VARIANT value;
        VariantInit(&value);
        if (name) {
            const HRESULT read = settings->get_Property(name, &value);
            if (SUCCEEDED(read) && value.vt == VT_BOOL) {
                std::cout << "ConnectToChildSession property readable: yes\n";
                // Only configure this unconnected COM object; no system setting changes.
                VariantClear(&value);
                value.vt = VT_BOOL;
                value.boolVal = VARIANT_TRUE;
                const HRESULT configured = settings->put_Property(name, &value);
                VariantClear(&value);
                const HRESULT verified = SUCCEEDED(configured)
                    ? settings->get_Property(name, &value) : configured;
                passed = SUCCEEDED(verified) && value.vt == VT_BOOL && value.boolVal == VARIANT_TRUE;
                if (!passed) std::cerr << "Child property configuration failed: " << std::hex << verified << '\n';
            } else std::cerr << "Child property read failed or unexpected type: " << std::hex << read << '\n';
            VariantClear(&value);
            SysFreeString(name);
        }
        settings->Release();
    }
    CoUninitialize();
    std::cout << (passed ? "PASS" : "FAIL") << ": API/control capability only; no child session created.\n"
        << "No WTSEnableChildSessions, Connect, accounts, security or driver changes.\n";
    return passed ? 0 : 1;
}
