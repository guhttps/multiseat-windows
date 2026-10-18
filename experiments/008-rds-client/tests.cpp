#include "network.h"
#include "config.h"
#include "input-router.h"
#include "cursor-lease.h"
#include "ssl-diagnostics.h"
#include <wincrypt.h>
#include <bcrypt.h>
#include <ncrypt.h>
#include <array>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace rds_lab {
namespace {
void require(bool condition) { if (!condition) throw std::runtime_error("Offline lab assertion failed."); }
void rejects(const std::function<void()>& operation) {
    try { operation(); } catch (const std::runtime_error&) { return; }
    throw std::runtime_error("Unsafe fixture was accepted.");
}
std::wstring fixture() {
    return LR"({"schema_version":1,"server_address":"10.73.12.2","server_port":3389,
"certificate_sha256":"00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff",
"seats":[{"seat_id":1,"username":"LabSeatA","domain":"MSRDSLAB","password_dpapi":"fixture",
"monitor_index":0,"desktop_width":1280,"desktop_height":720},
{"seat_id":2,"username":"LabSeatB","domain":"MSRDSLAB","password_dpapi":"fixture",
"monitor_index":1,"desktop_width":1280,"desktop_height":720}]})";
}
std::wstring replace_once(std::wstring text, const std::wstring& before, const std::wstring& after) {
    const size_t position = text.find(before);
    require(position != std::wstring::npos);
    text.replace(position, before.size(), after);
    return text;
}
void configuration_tests() {
    const Config config = parse_config(fixture());
    require(config.server_address == "10.73.12.2" && config.server_port == 3389 &&
        config.seats[0].seat_id == 1 && config.seats[1].username == L"LabSeatB" &&
        config.seats[1].monitor_index == 1 && config.certificate_sha256[1] == 0x11);
    for (const std::wstring& address : { L"127.0.0.1", L"169.254.1.2", L"8.8.8.8", L"localhost",
        L"10.01.2.3", L"10.256.0.1", L"10.1.2.3.extra", L"10.1.2.3:3389", L"10.1.2.3\\u0000bad" })
        rejects([&] { parse_config(replace_once(fixture(), L"10.73.12.2", address)); });
    for (const auto& change : std::vector<std::pair<std::wstring, std::wstring>>{
        {L"\"schema_version\":1", L"\"schema_version\":2"},
        {L"\"server_port\":3389", L"\"server_port\":0"},
        {L"\"server_port\":3389", L"\"server_port\":3389.5"},
        {L"\"monitor_index\":1", L"\"monitor_index\":0"},
        {L"LabSeatB", L"labseata"}, {L"LabSeatA", L"OtherUser"},
        {L"MSRDSLAB", L"AnotherGuest"}, {L"\"seat_id\":2", L"\"seat_id\":1"},
        {L"\"schema_version\":1", L"\"schema_version\":1,\"password\":\"fixture\""},
        {L"\"desktop_width\":1280", L"\"desktop_width\":8192"},
        {L"\"desktop_height\":720", L"\"desktop_height\":-1"},
        {L"\"password_dpapi\":\"fixture\"", L"\"password_dpapi\":\"\""},
        {L"00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff", L"00"},
        {L"00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff",
         L"0000000000000000000000000000000000000000000000000000000000000000"},
        {L"LabSeatA", L"LabSeatA\\\\other"}, {L"LabSeatA", L"LabSeatA\\u0000other"} })
        rejects([&] { parse_config(replace_once(fixture(), change.first, change.second)); });
    rejects([] { parse_config(L"{"); });
    rejects([] { parse_config(std::wstring(65537, L' ')); });
    require(utf8(L"Fixture çã") == "Fixture \xC3\xA7\xC3\xA3");
    rejects([] { utf8(std::wstring(1, static_cast<wchar_t>(0xD800))); });
}
std::wstring protect_fixture(const std::vector<BYTE>& bytes) {
    DATA_BLOB input{ static_cast<DWORD>(bytes.size()), const_cast<BYTE*>(bytes.data()) }, encrypted{};
    require(CryptProtectData(&input, L"RdsLab offline fixture", nullptr, nullptr, nullptr,
        CRYPTPROTECT_UI_FORBIDDEN, &encrypted) != FALSE);
    DWORD count = 0;
    bool okay = CryptBinaryToStringW(encrypted.pbData, encrypted.cbData,
        CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &count) != FALSE;
    std::wstring result(okay ? count : 0, L'\0');
    if (okay) okay = CryptBinaryToStringW(encrypted.pbData, encrypted.cbData,
        CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, result.data(), &count) != FALSE;
    SecureZeroMemory(encrypted.pbData, encrypted.cbData); LocalFree(encrypted.pbData);
    require(okay);
    result.resize(wcslen(result.c_str()));
    return result;
}
void dpapi_tests() {
    const std::string clear = utf8(L"offline-fixture-çã");
    const std::vector<BYTE> bytes(clear.begin(), clear.end());
    auto decrypted = unprotect_password(protect_fixture(bytes));
    require(decrypted.size() == bytes.size() + 1 && decrypted.back() == 0 &&
        std::equal(bytes.begin(), bytes.end(), decrypted.begin()));
    SecureZeroMemory(decrypted.data(), decrypted.size());
    rejects([] { unprotect_password(L"not valid base64!"); });
    rejects([] { unprotect_password(L"AAAA"); });
    rejects([] { unprotect_password(protect_fixture({ 'a', 0, 'b' })); });
    rejects([] { unprotect_password(protect_fixture({ 0xC3, 0x28 })); });
}
void ssl_diagnostic_tests() {
    SafeSslError error;
    require(parse_handshake_error("[BIO_do_handshake] 0000ABCD:error:0A000438:SSL routines:"
        "ssl3_read_bytes:tlsv1 alert internal error:ssl/record/record.c:100:SSL alert number 80\n [100]", error));
    require(error.code == 0x0A000438 && error.has_alert && error.alert == 80);
    require(!parse_handshake_error("arbitrary message containing :error:0A000438: data", error));
    require(!parse_handshake_error("[BIO_do_handshake] BAD-PREFIX:error:0A000438:text", error));
    require(!parse_handshake_error("[BIO_do_handshake] ABCD:error:0A0004381:text", error));
    require(!parse_handshake_error("[BIO_do_handshake] ABCD:error:0A00043Z:text", error));
    require(!parse_handshake_error(std::string(4097, 'x'), error));
    require(parse_handshake_error("[BIO_do_handshake] ABCD:error:0A000438:SSL alert number 9999", error));
    require(error.code == 0x0A000438 && !error.has_alert && error.alert == 0);
}
struct EphemeralCertificate {
    NCRYPT_PROV_HANDLE provider = 0;
    NCRYPT_KEY_HANDLE key = 0;
    PCCERT_CONTEXT certificate = nullptr;
    EphemeralCertificate() {
        try {
            require(NCryptOpenStorageProvider(&provider, MS_KEY_STORAGE_PROVIDER, 0) == ERROR_SUCCESS);
            // A null key name creates an ephemeral in-memory key, never a persisted container.
            require(NCryptCreatePersistedKey(provider, &key, NCRYPT_RSA_ALGORITHM, nullptr, 0, 0) == ERROR_SUCCESS);
            require(NCryptFinalizeKey(key, 0) == ERROR_SUCCESS);
            DWORD size = 0;
            require(CertStrToNameW(X509_ASN_ENCODING, L"CN=RdsLabFixture.invalid", CERT_X500_NAME_STR,
                nullptr, nullptr, &size, nullptr) != FALSE);
            std::vector<BYTE> subject(size);
            require(CertStrToNameW(X509_ASN_ENCODING, L"CN=RdsLabFixture.invalid", CERT_X500_NAME_STR,
                nullptr, subject.data(), &size, nullptr) != FALSE);
            CERT_NAME_BLOB name{size, subject.data()};
            CRYPT_ALGORITHM_IDENTIFIER algorithm{ const_cast<char*>(szOID_RSA_SHA256RSA), {} };
            SYSTEMTIME start{}, end{};
            FILETIME now{}; GetSystemTimeAsFileTime(&now);
            ULARGE_INTEGER time{}; time.LowPart = now.dwLowDateTime; time.HighPart = now.dwHighDateTime;
            constexpr ULONGLONG day = 24ULL * 60 * 60 * 10000000;
            time.QuadPart -= day;
            FILETIME earlier{time.LowPart, time.HighPart};
            require(FileTimeToSystemTime(&earlier, &start) != FALSE);
            time.QuadPart += 2 * day;
            FILETIME later{time.LowPart, time.HighPart};
            require(FileTimeToSystemTime(&later, &end) != FALSE);
            certificate = CertCreateSelfSignCertificate(key, &name, CERT_CREATE_SELFSIGN_NO_KEY_INFO, nullptr,
                &algorithm, &start, &end, nullptr);
            if (!certificate) throw std::runtime_error("Ephemeral certificate generation failed; code=" +
                std::to_string(GetLastError()));
        } catch (...) { cleanup(); throw; }
    }
    void cleanup() {
        if (certificate) { CertFreeCertificateContext(certificate); certificate = nullptr; }
        if (key) { NCryptFreeObject(key); key = 0; }
        if (provider) { NCryptFreeObject(provider); provider = 0; }
    }
    ~EphemeralCertificate() { cleanup(); }
};
void certificate_tests() {
    // Keys/certificates are ephemeral memory objects: no store, file, trust change or network.
    EphemeralCertificate fixture_certificate;
    const PCCERT_CONTEXT certificate = fixture_certificate.certificate;
    DWORD size = 0;
    require(CryptBinaryToStringA(certificate->pbCertEncoded, certificate->cbCertEncoded,
        CRYPT_STRING_BASE64HEADER, nullptr, &size) != FALSE);
    std::vector<char> pem(size);
    require(CryptBinaryToStringA(certificate->pbCertEncoded, certificate->cbCertEncoded,
        CRYPT_STRING_BASE64HEADER, pem.data(), &size) != FALSE);
    std::array<BYTE, 32> pin{};
    DWORD pin_size = static_cast<DWORD>(pin.size());
    require(CryptHashCertificate2(BCRYPT_SHA256_ALGORITHM, 0, nullptr, certificate->pbCertEncoded,
        certificate->cbCertEncoded, pin.data(), &pin_size) != FALSE);
    if (!certificate_matches(reinterpret_cast<const BYTE*>(pem.data()), strlen(pem.data()), pin))
        throw std::runtime_error("Matching ephemeral certificate pin was rejected.");
    pin[0] ^= 1;
    require(!certificate_matches(reinterpret_cast<const BYTE*>(pem.data()), strlen(pem.data()), pin));
    require(!certificate_matches(nullptr, 1, pin));
    constexpr BYTE malformed[] = { 'b', 'a', 'd' };
    require(!certificate_matches(malformed, sizeof(malformed), pin));
}
RAWKEYBOARD key(USHORT virtual_key, USHORT scan, USHORT flags = 0) {
    RAWKEYBOARD result{}; result.VKey = virtual_key; result.MakeCode = scan; result.Flags = flags; return result;
}
struct CursorFixture {
    static inline RECT desktop{-1200, 0, 1920, 1080}, clipped{};
    static inline POINT position{};
    static inline HCURSOR shape{};
    static inline unsigned writes{};
    static inline bool fail_clip{}, fail_release{};
    static void reset() {
        clipped = desktop; position = {200, 200};
        shape = reinterpret_cast<HCURSOR>(1); writes = 0;
        fail_clip = fail_release = false;
    }
    static BOOL WINAPI get_clip(LPRECT value) { *value = clipped; return TRUE; }
    static BOOL WINAPI clip(const RECT* value) {
        if ((value && fail_clip) || (!value && fail_release)) return FALSE;
        clipped = value ? *value : desktop; ++writes; return TRUE;
    }
    static BOOL WINAPI get_position(LPPOINT value) { *value = position; return TRUE; }
    static HCURSOR WINAPI set_shape(HCURSOR value) { HCURSOR old = shape; shape = value; return old; }
    static HCURSOR WINAPI get_shape() { return shape; }
    static CursorApi api() { return {get_clip, clip, get_position, set_shape, get_shape}; }
};
void cursor_tests() {
    const RECT client{0, 50, 800, 650};
    CursorFixture::reset();
    {
        CursorLease lease(CursorFixture::api());
        require(lease.release() && CursorFixture::writes == 0);
        require(lease.acquire(client, CursorFixture::desktop) && lease.active());
        require(CursorFixture::clipped.right - CursorFixture::clipped.left == 1 && !CursorFixture::shape);
        require(lease.acquire(client, CursorFixture::desktop) && CursorFixture::writes == 1);
        require(lease.release() && !lease.active());
        require(EqualRect(&CursorFixture::clipped, &CursorFixture::desktop) && CursorFixture::shape);
    }
    CursorFixture::reset();
    CursorFixture::clipped = {10, 10, 100, 100};
    {
        CursorLease lease(CursorFixture::api());
        require(!lease.acquire(client, CursorFixture::desktop) && CursorFixture::writes == 0 && CursorFixture::shape);
    }
    CursorFixture::reset();
    CursorFixture::fail_clip = true;
    {
        CursorLease lease(CursorFixture::api());
        require(!lease.acquire(client, CursorFixture::desktop) && !lease.active() && CursorFixture::shape);
    }
    CursorFixture::reset();
    bool exception_reached = false;
    try {
        CursorLease lease(CursorFixture::api());
        require(lease.acquire(client, CursorFixture::desktop));
        exception_reached = true;
        throw std::runtime_error("Inert cleanup fixture.");
    } catch (const std::runtime_error&) { }
    require(exception_reached && EqualRect(&CursorFixture::clipped, &CursorFixture::desktop) && CursorFixture::shape);
    CursorFixture::reset();
    {
        CursorLease lease(CursorFixture::api());
        require(lease.acquire(client, CursorFixture::desktop));
        const RECT newer{25, 25, 75, 75};
        CursorFixture::clipped = newer;
        CursorFixture::shape = reinterpret_cast<HCURSOR>(2);
        require(lease.release() && EqualRect(&CursorFixture::clipped, &newer) &&
            CursorFixture::shape == reinterpret_cast<HCURSOR>(2));
    }
    CursorFixture::reset();
    {
        CursorLease lease(CursorFixture::api());
        const RECT negative{-1000, 50, -200, 650};
        require(lease.acquire(negative, CursorFixture::desktop));
        require(CursorFixture::clipped.left == -600 && CursorFixture::clipped.top == 350);
        CursorFixture::fail_release = true;
        require(!lease.release() && lease.active());
        CursorFixture::fail_release = false;
    }
    require(EqualRect(&CursorFixture::clipped, &CursorFixture::desktop) && CursorFixture::shape);
}
void router_tests() {
    rejects([] { InputRouter invalid({ std::pair{0U, 720U}, std::pair{1280U, 720U} },
        [](unsigned, const InputEvent&) {}); });
    std::vector<std::pair<unsigned, InputEvent>> events;
    InputRouter router({ std::pair{1280U, 720U}, std::pair{1280U, 720U} },
        [&](unsigned seat, const InputEvent& event) { events.emplace_back(seat, event); });
    const HANDLE ka = reinterpret_cast<HANDLE>(1), kb = reinterpret_cast<HANDLE>(2);
    const HANDLE ma = reinterpret_cast<HANDLE>(3), mb = reinterpret_cast<HANDLE>(4);
    router.keyboard(ka, key(VK_F6, 0x40), false);
    require(router.seat(0).keyboard == nullptr);
    router.keyboard(ka, key(VK_F6, 0x40), true);
    router.keyboard(ka, key(VK_F6, 0x40), true);
    require(router.seat(0).keyboard == ka && router.seat(1).keyboard == nullptr);
    router.keyboard(kb, key(VK_F6, 0x40), true);
    RAWMOUSE click{}; click.usButtonFlags = RI_MOUSE_RIGHT_BUTTON_DOWN;
    router.mouse(ma, click, true); router.mouse(ma, click, true);
    require(router.seat(1).mouse == nullptr);
    router.mouse(mb, click, true);
    require(router.ready() && events.empty());
    router.keyboard(ka, key('A', 0x1E), true);
    router.keyboard(kb, key('B', 0x30), true);
    require(events.size() == 2 && events[0].first == 0 && events[1].first == 1);
    router.keyboard(ka, key('A', 0x1E), true);
    require(events.back().second.repeat);
    router.keyboard(ka, key('A', 0x1E, RI_KEY_BREAK), false);
    require(router.seat(0).held_keys.contains(0x1E));
    router.release_all();
    require(router.seat(0).held_keys.empty() && router.seat(1).held_keys.empty());
    require(events.size() == 5 && !events[3].second.down && !events[4].second.down);
    const size_t released = events.size();
    router.release_all(); require(events.size() == released);
    router.keyboard(ka, key(VK_LWIN, 0x5B, RI_KEY_E0), true);
    router.keyboard(ka, key('A', 0), true);
    router.keyboard(nullptr, key('A', 0x1E), true);
    router.keyboard(reinterpret_cast<HANDLE>(99), key('A', 0x1E), true);
    require(events.size() == released);
    router.keyboard(kb, key(VK_RIGHT, 0x4D, RI_KEY_E0), true);
    require(events.back().first == 1 && events.back().second.code == 0x14D);
    RAWMOUSE movement{}; movement.lLastX = LONG_MAX; movement.lLastY = LONG_MIN;
    router.mouse(ma, movement, true);
    require(router.seat(0).x == 1279 && router.seat(0).y == 0 && router.seat(1).x == 640);
    RAWMOUSE absolute{}; absolute.usFlags = MOUSE_MOVE_ABSOLUTE;
    absolute.lLastX = 0; absolute.lLastY = 65535;
    router.mouse(mb, absolute, true);
    require(router.seat(1).x == 0 && router.seat(1).y == 719);
    RAWMOUSE left{}; left.usButtonFlags = RI_MOUSE_LEFT_BUTTON_DOWN;
    router.mouse(ma, left, true);
    require(events.back().first == 0 && events.back().second.kind == EventKind::Button && events.back().second.down);
    const size_t button_count = events.size();
    router.mouse(ma, left, true); require(events.size() == button_count);
    router.device_removed(reinterpret_cast<HANDLE>(99)); require(router.ready());
    router.device_removed(mb);
    require(!router.ready() && router.seat(0).held_buttons == 0 && router.seat(1).held_keys.empty());
    require(events.back().second.kind == EventKind::Key || events.back().second.kind == EventKind::Button);
    for (const SeatInput* seat : { &router.seat(0), &router.seat(1) })
        require(!seat->keyboard && !seat->mouse);
}
}
bool run_self_tests() {
    try {
        SocketRuntime sockets;
        require(numeric_address_smoke());
        std::cout << "PASS: Winsock 2.2 and numeric IPv4 getaddrinfo; no DNS or socket connection.\n";
        configuration_tests(); std::cout << "PASS: strict local configuration and UTF-8 policy.\n";
        dpapi_tests(); std::cout << "PASS: current-user DPAPI round trip and malformed/invalid payload rejection.\n";
        ssl_diagnostic_tests(); std::cout << "PASS: bounded TLS diagnostic parser returns numeric codes only.\n";
        certificate_tests(); std::cout << "PASS: ephemeral certificate SHA-256 leaf pin and mismatch rejection.\n";
        router_tests(); std::cout << "PASS: per-device routing, clamping, repeats and release/reset/removal.\n";
        cursor_tests(); std::cout << "PASS: inert cursor capture, cleanup, failure and foreign-owner preservation.\n";
        std::cout << "Offline tests only; no RDP connection, input registration, trust-store or host configuration changes.\n";
        return true;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return false; }
    catch (...) { std::cerr << "FAIL: offline lab test threw.\n"; return false; }
}
}
