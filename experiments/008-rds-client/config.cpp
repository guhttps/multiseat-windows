#include "config.h"
#include <windows.data.json.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <roapi.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <limits>
#include <stdexcept>

namespace rds_lab {
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Wrappers::HString;
using Microsoft::WRL::Wrappers::HStringReference;
using ABI::Windows::Data::Json::IJsonObject;
using ABI::Windows::Data::Json::IJsonObjectStatics;
using ABI::Windows::Data::Json::IJsonArray;
using ABI::Windows::Data::Json::IJsonValue;
namespace {
struct ProtectedCleartext {
    DATA_BLOB value{};
    ~ProtectedCleartext() {
        if (value.pbData) { SecureZeroMemory(value.pbData, value.cbData); LocalFree(value.pbData); }
    }
};
void check(HRESULT result) {
    if (FAILED(result)) throw std::runtime_error("Invalid lab configuration JSON.");
}
std::wstring named_string(IJsonObject* object, const wchar_t* name) {
    HString value;
    check(object->GetNamedString(HStringReference(name).Get(), value.GetAddressOf()));
    UINT32 length = 0;
    const wchar_t* data = value.GetRawBuffer(&length);
    if (length > 32768 || (length && std::find(data, data + length, L'\0') != data + length))
        throw std::runtime_error("Invalid configuration string.");
    return std::wstring(data, length);
}
unsigned named_uint(IJsonObject* object, const wchar_t* name, unsigned minimum, unsigned maximum) {
    double value = 0;
    check(object->GetNamedNumber(HStringReference(name).Get(), &value));
    if (!std::isfinite(value) || value < minimum || value > maximum || std::floor(value) != value)
        throw std::runtime_error("Configuration integer out of range.");
    return static_cast<unsigned>(value);
}
void exact_property_count(IJsonObject* object, unsigned expected) {
    ComPtr<ABI::Windows::Foundation::Collections::IMap<HSTRING, IJsonValue*>> properties;
    check(object->QueryInterface(IID_PPV_ARGS(&properties)));
    UINT32 size = 0;
    check(properties->get_Size(&size));
    if (size != expected) throw std::runtime_error("Unknown or missing configuration properties.");
}
int nibble(wchar_t c) {
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    if (c >= L'A' && c <= L'F') return c - L'A' + 10;
    return -1;
}
bool private_ipv4(const std::wstring& value) {
    std::array<unsigned, 4> bytes{};
    size_t position = 0;
    for (size_t i = 0; i < bytes.size(); ++i) {
        size_t start = position;
        while (position < value.size() && value[position] >= L'0' && value[position] <= L'9') {
            bytes[i] = bytes[i] * 10 + static_cast<unsigned>(value[position++] - L'0');
            if (bytes[i] > 255 || position - start > 3) return false;
        }
        if (position == start || (position - start > 1 && value[start] == L'0')) return false;
        if (i < 3 && (position >= value.size() || value[position++] != L'.')) return false;
    }
    return position == value.size() &&
        (bytes[0] == 10 || (bytes[0] == 172 && bytes[1] >= 16 && bytes[1] <= 31) ||
         (bytes[0] == 192 && bytes[1] == 168));
}
bool simple_name(const std::wstring& value) {
    return !value.empty() && value.size() <= 64 &&
        std::all_of(value.begin(), value.end(), [](wchar_t c) {
            return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
                (c >= L'0' && c <= L'9') || c == L'-' || c == L'_';
        });
}
}
std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    if (value.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
        throw std::runtime_error("String too long.");
    int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) throw std::runtime_error("Invalid Unicode string.");
    std::string result(static_cast<size_t>(count), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        result.data(), count, nullptr, nullptr) != count) throw std::runtime_error("Unicode conversion failed.");
    return result;
}
Config parse_config(const std::wstring& json) {
    if (json.empty() || json.size() > 65536) throw std::runtime_error("Configuration size invalid.");
    ComPtr<IJsonObjectStatics> factory;
    check(RoGetActivationFactory(HStringReference(RuntimeClass_Windows_Data_Json_JsonObject).Get(),
        IID_PPV_ARGS(&factory)));
    HString text;
    check(text.Set(json.data(), static_cast<UINT32>(json.size())));
    ComPtr<IJsonObject> root;
    check(factory->Parse(text.Get(), &root));
    exact_property_count(root.Get(), 5);
    if (named_uint(root.Get(), L"schema_version", 1, 1) != 1)
        throw std::runtime_error("Unsupported schema.");
    Config result;
    const std::wstring address = named_string(root.Get(), L"server_address");
    if (!private_ipv4(address)) throw std::runtime_error("A literal RFC1918 lab IPv4 address is required.");
    result.server_address = utf8(address);
    result.server_port = named_uint(root.Get(), L"server_port", 1, 65535);
    const std::wstring pin = named_string(root.Get(), L"certificate_sha256");
    if (pin.size() != 64) throw std::runtime_error("A SHA-256 leaf certificate pin is required.");
    for (size_t i = 0; i < result.certificate_sha256.size(); ++i) {
        const int high = nibble(pin[i * 2]), low = nibble(pin[i * 2 + 1]);
        if (high < 0 || low < 0) throw std::runtime_error("Certificate pin must be hexadecimal.");
        result.certificate_sha256[i] = static_cast<BYTE>((high << 4) | low);
    }
    if (std::all_of(result.certificate_sha256.begin(), result.certificate_sha256.end(),
        [](BYTE b) { return b == 0; })) throw std::runtime_error("An empty certificate pin is forbidden.");
    ComPtr<IJsonArray> seats;
    check(root->GetNamedArray(HStringReference(L"seats").Get(), &seats));
    ComPtr<ABI::Windows::Foundation::Collections::IVector<IJsonValue*>> values;
    check(seats.As(&values));
    UINT32 count = 0;
    check(values->get_Size(&count));
    if (count != 2) throw std::runtime_error("Exactly two lab seats are required.");
    for (UINT32 i = 0; i < 2; ++i) {
        ComPtr<IJsonObject> seat;
        check(seats->GetObjectAt(i, &seat));
        exact_property_count(seat.Get(), 7);
        SeatConfig& out = result.seats[i];
        out.seat_id = named_uint(seat.Get(), L"seat_id", i + 1, i + 1);
        out.username = named_string(seat.Get(), L"username");
        out.domain = named_string(seat.Get(), L"domain");
        if (!simple_name(out.username) || !simple_name(out.domain))
            throw std::runtime_error("Lab account names are invalid.");
        if (_wcsicmp(out.username.c_str(), i == 0 ? L"LabSeatA" : L"LabSeatB") != 0 ||
            _wcsicmp(out.domain.c_str(), L"MSRDSLAB") != 0)
            throw std::runtime_error("Only the designated non-administrative lab accounts are supported.");
        out.password_dpapi = named_string(seat.Get(), L"password_dpapi");
        if (out.password_dpapi.empty() || out.password_dpapi.size() > 16384)
            throw std::runtime_error("DPAPI password is missing or oversized.");
        out.monitor_index = named_uint(seat.Get(), L"monitor_index", 0, 15);
        out.desktop_width = named_uint(seat.Get(), L"desktop_width", 640, 4096);
        out.desktop_height = named_uint(seat.Get(), L"desktop_height", 480, 2160);
    }
    if (_wcsicmp(result.seats[0].username.c_str(), result.seats[1].username.c_str()) == 0 ||
        result.seats[0].monitor_index == result.seats[1].monitor_index)
        throw std::runtime_error("Seats need distinct accounts and monitors.");
    if (_wcsicmp(result.seats[0].domain.c_str(), result.seats[1].domain.c_str()) != 0)
        throw std::runtime_error("Both accounts must belong to the same lab guest.");
    return result;
}
Config load_config(const std::wstring& path) {
    std::ifstream stream(std::filesystem::path(path), std::ios::binary | std::ios::ate);
    if (!stream) throw std::runtime_error("Cannot open local lab configuration.");
    const std::streamoff size = stream.tellg();
    if (size <= 0 || size > 262144) throw std::runtime_error("Configuration file size invalid.");
    std::string data(static_cast<size_t>(size), '\0');
    stream.seekg(0);
    if (!stream.read(data.data(), static_cast<std::streamsize>(data.size())))
        throw std::runtime_error("Cannot read lab configuration.");
    size_t offset = data.size() >= 3 && static_cast<BYTE>(data[0]) == 0xEF &&
        static_cast<BYTE>(data[1]) == 0xBB && static_cast<BYTE>(data[2]) == 0xBF ? 3 : 0;
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data.data() + offset,
        static_cast<int>(data.size() - offset), nullptr, 0);
    if (count <= 0) throw std::runtime_error("Configuration must be UTF-8 JSON.");
    std::wstring wide(static_cast<size_t>(count), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data.data() + offset,
        static_cast<int>(data.size() - offset), wide.data(), count) != count)
        throw std::runtime_error("Configuration encoding failed.");
    return parse_config(wide);
}
std::wstring default_config_path() {
    PWSTR path = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path)))
        throw std::runtime_error("Local application data is unavailable.");
    std::wstring result(path);
    CoTaskMemFree(path);
    return result + L"\\MultiseatPrototype\\Lab\\rds-client.json";
}
std::vector<BYTE> unprotect_password(const std::wstring& protected_base64) {
    DWORD size = 0;
    if (!CryptStringToBinaryW(protected_base64.c_str(), static_cast<DWORD>(protected_base64.size()),
        CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT, nullptr, &size, nullptr, nullptr) || size == 0 || size > 16384)
        throw std::runtime_error("DPAPI password encoding is invalid.");
    std::vector<BYTE> encrypted(size);
    if (!CryptStringToBinaryW(protected_base64.c_str(), static_cast<DWORD>(protected_base64.size()),
        CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT, encrypted.data(), &size, nullptr, nullptr))
        throw std::runtime_error("DPAPI password decoding failed.");
    DATA_BLOB input{ size, encrypted.data() };
    ProtectedCleartext clear;
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &clear.value))
        throw std::runtime_error("DPAPI password cannot be decrypted by this Windows user.");
    std::vector<BYTE> result;
    const DATA_BLOB& bytes = clear.value;
    bool valid = bytes.cbData > 0 && bytes.cbData <= 1024 &&
        std::find(bytes.pbData, bytes.pbData + bytes.cbData, BYTE{0}) == bytes.pbData + bytes.cbData &&
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, reinterpret_cast<const char*>(bytes.pbData),
            static_cast<int>(bytes.cbData), nullptr, 0) > 0;
    if (valid) {
        // Allocate the terminator together with the password so adding NUL
        // cannot leave a freed intermediate plaintext allocation behind.
        result.resize(static_cast<size_t>(bytes.cbData) + 1, 0);
        memcpy(result.data(), bytes.pbData, bytes.cbData);
    }
    if (!valid) throw std::runtime_error("DPAPI password must contain bounded UTF-8 bytes without NUL.");
    return result;
}
bool certificate_matches(const BYTE* pem, size_t length, const std::array<BYTE, 32>& expected) {
    if (!pem || length == 0 || length > 1048576) return false;
    const std::string text(reinterpret_cast<const char*>(pem), length);
    constexpr char begin_marker[] = "-----BEGIN CERTIFICATE-----";
    constexpr char end_marker[] = "-----END CERTIFICATE-----";
    const size_t begin = text.find(begin_marker);
    const size_t end = text.find(end_marker, begin == std::string::npos ? 0 : begin);
    if (begin != 0 || end == std::string::npos) return false;
    const size_t leaf_size = end + sizeof(end_marker) - 1;
    DWORD der_size = 0;
    if (!CryptStringToBinaryA(text.data(), static_cast<DWORD>(leaf_size), CRYPT_STRING_BASE64HEADER,
        nullptr, &der_size, nullptr, nullptr) || der_size == 0 || der_size > 65536) return false;
    std::vector<BYTE> der(der_size);
    if (!CryptStringToBinaryA(text.data(), static_cast<DWORD>(leaf_size), CRYPT_STRING_BASE64HEADER,
        der.data(), &der_size, nullptr, nullptr)) return false;
    PCCERT_CONTEXT certificate = CertCreateCertificateContext(X509_ASN_ENCODING, der.data(), der_size);
    if (!certificate) return false;
    const bool current = CertVerifyTimeValidity(nullptr, certificate->pCertInfo) == 0;
    CertFreeCertificateContext(certificate);
    std::array<BYTE, 32> actual{};
    DWORD actual_size = static_cast<DWORD>(actual.size());
    if (!current || !CryptHashCertificate2(BCRYPT_SHA256_ALGORITHM, 0, nullptr, der.data(), der_size,
        actual.data(), &actual_size) || actual_size != actual.size()) return false;
    BYTE difference = 0;
    for (size_t i = 0; i < actual.size(); ++i) difference |= actual[i] ^ expected[i];
    return difference == 0;
}
}
