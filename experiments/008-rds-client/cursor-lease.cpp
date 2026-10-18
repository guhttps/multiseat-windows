#include "cursor-lease.h"
#include <cstdint>

namespace rds_lab {
bool CursorLease::acquire(const RECT& client, const RECT& desktop) {
    if (client.right <= client.left || client.bottom <= client.top ||
        client.left < desktop.left || client.top < desktop.top ||
        client.right > desktop.right || client.bottom > desktop.bottom) return false;
    RECT current{};
    if (!api_.get_clip(&current)) return false;
    if (owned_) {
        if (!EqualRect(&current, &anchor_)) return false;
        POINT anchor{anchor_.left, anchor_.top};
        if (PtInRect(&client, anchor)) { api_.set_shape(nullptr); return true; }
        if (!release() || !api_.get_clip(&current)) return false;
    }
    // Do not replace another application's existing cursor confinement.
    if (!EqualRect(&current, &desktop)) return false;
    POINT position{};
    if (!api_.get_position(&position)) return false;
    if (!PtInRect(&client, position)) {
        position.x = static_cast<LONG>(static_cast<std::int64_t>(client.left) +
            (static_cast<std::int64_t>(client.right) - client.left) / 2);
        position.y = static_cast<LONG>(static_cast<std::int64_t>(client.top) +
            (static_cast<std::int64_t>(client.bottom) - client.top) / 2);
    }
    anchor_ = {position.x, position.y, position.x + 1, position.y + 1};
    if (!api_.clip(&anchor_)) return false;
    owned_ = true;
    previous_ = api_.set_shape(nullptr);
    return true;
}
bool CursorLease::release() noexcept {
    if (!owned_) return true;
    RECT current{};
    const bool queried = api_.get_clip(&current) != FALSE;
    // If another owner changed the rectangle, preserve its newer state.
    if (!queried || (EqualRect(&current, &anchor_) && !api_.clip(nullptr))) return false;
    if (previous_ && !api_.get_shape()) api_.set_shape(previous_);
    previous_ = nullptr;
    owned_ = false;
    return true;
}
}
