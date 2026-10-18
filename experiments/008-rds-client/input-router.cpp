#include "input-router.h"
#include <algorithm>
#include <utility>
#include <stdexcept>

namespace rds_lab {
InputRouter::InputRouter(const std::array<std::pair<unsigned, unsigned>, 2>& sizes, Sink sink)
    : sink_(std::move(sink)) {
    if (!sink_) throw std::runtime_error("Input router requires an event sink.");
    for (size_t i = 0; i < seats_.size(); ++i) {
        if (sizes[i].first < 1 || sizes[i].first > 4096 || sizes[i].second < 1 || sizes[i].second > 2160)
            throw std::runtime_error("Input router desktop dimensions are invalid.");
        seats_[i].width = static_cast<int>(sizes[i].first);
        seats_[i].height = static_cast<int>(sizes[i].second);
        seats_[i].x = seats_[i].width / 2;
        seats_[i].y = seats_[i].height / 2;
    }
}
bool InputRouter::ready() const {
    return std::all_of(seats_.begin(), seats_.end(), [](const SeatInput& seat) {
        return seat.keyboard && seat.mouse;
    });
}
bool InputRouter::routable(unsigned index) const {
    const SeatInput& seat = seats_.at(index);
    return initially_paired_ && seat.enabled && seat.keyboard && seat.mouse;
}
bool InputRouter::any_routable() const {
    for (unsigned i = 0; i < seats_.size(); ++i) if (routable(i)) return true;
    return false;
}
void InputRouter::keyboard(HANDLE device, const RAWKEYBOARD& key, bool enabled) {
    if (!enabled || !device || key.VKey == 255 || key.MakeCode == 0 || key.MakeCode >= 255) return;
    const bool down = (key.Flags & RI_KEY_BREAK) == 0;
    if (down && key.VKey == VK_F6) {
        for (const SeatInput& seat : seats_) if (seat.keyboard == device) return;
        for (SeatInput& seat : seats_) if (!seat.keyboard) {
            seat.keyboard = device;
            initially_paired_ = initially_paired_ || ready();
            return;
        }
        return;
    }
    for (unsigned i = 0; i < seats_.size(); ++i) {
        SeatInput& seat = seats_[i];
        if (seat.keyboard != device) continue;
        if (!routable(i)) return;
        if (key.VKey == VK_F6 || key.VKey == VK_F7 || key.VKey == VK_F8 || key.VKey == VK_F9 || key.VKey == VK_F12 ||
            key.VKey == VK_LWIN || key.VKey == VK_RWIN) return;
        if (key.VKey == VK_PAUSE && (key.Flags & RI_KEY_E1)) {
            if (down) sink_(i, InputEvent{ EventKind::Pause });
            return;
        }
        const unsigned code = key.MakeCode | ((key.Flags & RI_KEY_E0) ? 0x100U : 0) |
            ((key.Flags & RI_KEY_E1) ? 0x200U : 0);
        const bool held = seat.held_keys.contains(code);
        if (!down && !held) return;
        if (down) seat.held_keys.insert(code);
        else seat.held_keys.erase(code);
        sink_(i, InputEvent{ EventKind::Key, code, down, held && down });
        return;
    }
}
void InputRouter::button(unsigned index, unsigned button_index, bool down) {
    SeatInput& seat = seats_[index];
    const unsigned mask = 1U << button_index;
    if (down == ((seat.held_buttons & mask) != 0)) return;
    if (down) seat.held_buttons |= mask;
    else seat.held_buttons &= ~mask;
    sink_(index, InputEvent{ EventKind::Button, button_index, down, false, false, seat.x, seat.y });
}
void InputRouter::mouse(HANDLE device, const RAWMOUSE& mouse_input, bool enabled) {
    if (!enabled || !device) return;
    const bool assigned = std::any_of(seats_.begin(), seats_.end(), [device](const SeatInput& seat) {
        return seat.mouse == device;
    });
    if (!assigned) {
        if (!(mouse_input.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN)) return;
        for (SeatInput& seat : seats_) if (!seat.mouse) {
            seat.mouse = device;
            initially_paired_ = initially_paired_ || ready();
            return;
        }
        return;
    }
    for (unsigned i = 0; i < seats_.size(); ++i) {
        SeatInput& seat = seats_[i];
        if (seat.mouse != device) continue;
        if (!routable(i)) return;
        const int previous_x = seat.x, previous_y = seat.y;
        if (mouse_input.usFlags & MOUSE_MOVE_ABSOLUTE) {
            seat.x = static_cast<int>(std::clamp<std::int64_t>(mouse_input.lLastX, 0, 65535) *
                (seat.width - 1) / 65535);
            seat.y = static_cast<int>(std::clamp<std::int64_t>(mouse_input.lLastY, 0, 65535) *
                (seat.height - 1) / 65535);
        } else {
            seat.x = static_cast<int>(std::clamp<std::int64_t>(static_cast<std::int64_t>(seat.x) +
                mouse_input.lLastX, 0, seat.width - 1));
            seat.y = static_cast<int>(std::clamp<std::int64_t>(static_cast<std::int64_t>(seat.y) +
                mouse_input.lLastY, 0, seat.height - 1));
        }
        if (previous_x != seat.x || previous_y != seat.y)
            sink_(i, InputEvent{ EventKind::Move, 0, false, false, false, seat.x, seat.y });
        constexpr std::array<USHORT, 5> downs{ RI_MOUSE_LEFT_BUTTON_DOWN, RI_MOUSE_RIGHT_BUTTON_DOWN,
            RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_BUTTON_4_DOWN, RI_MOUSE_BUTTON_5_DOWN };
        constexpr std::array<USHORT, 5> ups{ RI_MOUSE_LEFT_BUTTON_UP, RI_MOUSE_RIGHT_BUTTON_UP,
            RI_MOUSE_MIDDLE_BUTTON_UP, RI_MOUSE_BUTTON_4_UP, RI_MOUSE_BUTTON_5_UP };
        for (unsigned b = 0; b < downs.size(); ++b) {
            if (mouse_input.usButtonFlags & downs[b]) button(i, b, true);
            if (mouse_input.usButtonFlags & ups[b]) button(i, b, false);
        }
        if (mouse_input.usButtonFlags & (RI_MOUSE_WHEEL | RI_MOUSE_HWHEEL)) {
            const auto delta = static_cast<SHORT>(mouse_input.usButtonData);
            sink_(i, InputEvent{ EventKind::Wheel, static_cast<unsigned>(static_cast<int>(delta)),
                false, false, (mouse_input.usButtonFlags & RI_MOUSE_HWHEEL) != 0, seat.x, seat.y });
        }
        return;
    }
}
void InputRouter::release(unsigned index) {
    SeatInput& seat = seats_.at(index);
    for (unsigned code : seat.held_keys) sink_(index, InputEvent{ EventKind::Key, code, false });
    seat.held_keys.clear();
    for (unsigned b = 0; b < 5; ++b) if (seat.held_buttons & (1U << b)) button(index, b, false);
}
void InputRouter::set_enabled(unsigned index, bool enabled) {
    SeatInput& seat = seats_.at(index);
    if (seat.enabled && !enabled) release(index);
    seat.enabled = enabled;
}
void InputRouter::release_all() {
    for (unsigned i = 0; i < seats_.size(); ++i) release(i);
}
void InputRouter::reset() {
    release_all();
    for (SeatInput& seat : seats_) { seat.keyboard = nullptr; seat.mouse = nullptr; }
    initially_paired_ = false;
}
void InputRouter::device_removed(HANDLE device) {
    if (!device) return;
    for (unsigned i = 0; i < seats_.size(); ++i) {
        SeatInput& seat = seats_[i];
        if (seat.keyboard != device && seat.mouse != device) continue;
        // Release all held input for this incomplete pair, including its
        // remaining device. The peer keeps its bindings and held state.
        release(i);
        if (seat.keyboard == device) seat.keyboard = nullptr;
        if (seat.mouse == device) seat.mouse = nullptr;
    }
}
}
