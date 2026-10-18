#pragma once
#include <windows.h>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace rds_lab {
struct SeatConfig {
    unsigned seat_id{};
    std::wstring username;
    std::wstring domain;
    std::wstring password_dpapi;
    unsigned monitor_index{};
    unsigned desktop_width{};
    unsigned desktop_height{};
};
struct Config {
    std::string server_address;
    unsigned server_port{};
    std::array<BYTE, 32> certificate_sha256{};
    std::array<SeatConfig, 2> seats;
};
// JSON uses the inbox Windows.Data.Json parser. No script or shell is evaluated.
Config parse_config(const std::wstring& json);
Config load_config(const std::wstring& path);
std::wstring default_config_path();
std::string utf8(const std::wstring& value);
// DPAPI CurrentUser, no optional entropy; the protected bytes must be UTF-8.
std::vector<BYTE> unprotect_password(const std::wstring& protected_base64);
bool certificate_matches(const BYTE* pem, size_t length,
                         const std::array<BYTE, 32>& expected);
}
