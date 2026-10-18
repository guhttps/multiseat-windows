#pragma once
#include <string_view>

namespace rds_lab {
struct SafeSslError { unsigned code{}; unsigned alert{}; bool has_alert{}; };
// Parses only the canonical ERR_print_errors_cb line emitted by the audited
// bio_err_print callback. The caller must also verify the WLog source/function.
// No strings, names, paths, certificate data or arbitrary text are returned.
bool parse_handshake_error(std::string_view text, SafeSslError& result) noexcept;
}
