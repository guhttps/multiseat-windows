#pragma once
#include <windows.h>

namespace rds_lab {
// Injectable Win32 boundary: offline tests must never confine the real cursor.
struct CursorApi {
    decltype(&::GetClipCursor) get_clip = ::GetClipCursor;
    decltype(&::ClipCursor) clip = ::ClipCursor;
    decltype(&::GetCursorPos) get_position = ::GetCursorPos;
    decltype(&::SetCursor) set_shape = ::SetCursor;
    decltype(&::GetCursor) get_shape = ::GetCursor;
};
class CursorLease {
    CursorApi api_;
    RECT anchor_{};
    HCURSOR previous_{};
    bool owned_{};
public:
    explicit CursorLease(CursorApi api = {}) : api_(api) {}
    CursorLease(const CursorLease&) = delete;
    CursorLease& operator=(const CursorLease&) = delete;
    ~CursorLease() { release(); }
    bool acquire(const RECT& client_screen, const RECT& virtual_screen);
    bool release() noexcept;
    bool active() const noexcept { return owned_; }
};
}
