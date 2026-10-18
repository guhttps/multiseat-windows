#include "ssl-diagnostics.h"

namespace rds_lab {
namespace {
int hex(char value) noexcept {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}
}
bool parse_handshake_error(std::string_view text, SafeSslError& result) noexcept {
    result = {};
    constexpr std::string_view prefix = "[BIO_do_handshake] ";
    if (text.size() > 4096 || !text.starts_with(prefix)) return false;
    text.remove_prefix(prefix.size());
    const size_t separator = text.find(":error:");
    if (separator == std::string_view::npos || separator < 1 || separator > 32) return false;
    for (char c : text.substr(0, separator)) if (hex(c) < 0) return false;
    text.remove_prefix(separator + 7);
    if (text.size() < 9 || text[8] != ':') return false;
    unsigned code = 0;
    for (char c : text.substr(0, 8)) {
        const int value = hex(c);
        if (value < 0) return false;
        code = (code << 4) | static_cast<unsigned>(value);
    }
    if (code == 0) return false;
    result.code = code;
    constexpr std::string_view alert_prefix = "SSL alert number ";
    const size_t alert_position = text.find(alert_prefix, 9);
    if (alert_position != std::string_view::npos) {
        const std::string_view remainder = text.substr(alert_position + alert_prefix.size());
        unsigned alert = 0;
        size_t count = 0;
        while (count < remainder.size() && remainder[count] >= '0' && remainder[count] <= '9') {
            alert = alert * 10 + static_cast<unsigned>(remainder[count++] - '0');
            if (count > 3 || alert > 255) return true;
        }
        if (count > 0 && (count == remainder.size() || remainder[count] == '\n' ||
            remainder[count] == '\r' || remainder[count] == ' ')) {
            result.alert = alert; result.has_alert = true;
        }
    }
    return true;
}
}
