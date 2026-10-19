#pragma once
#include "input-router.h"
#include "options.h"
#include <array>
#include <iterator>
#include <stdexcept>

namespace rds_lab {
// Explicit test fixture only. Fabricated handles are never registered with
// Windows. The production router and its existing sink deliver only the fixed
// laboratory mouse/key fixtures to the two owned RDP contexts.
class RouterHotplugFixture {
public:
    RouterHotplugFixture(InputRouter& router, HotplugKind kind, unsigned target)
        : router_(router), kind_(kind), target_(target) {
        if ((kind != HotplugKind::Keyboard && kind != HotplugKind::Mouse) || target > 1)
            throw std::runtime_error("Invalid router hotplug fixture selection.");
        for (unsigned i = 0; i < 2; ++i) {
            keyboards_[i] = handle(0x100 + i);
            mice_[i] = handle(0x200 + i);
        }
        retired_ = kind == HotplugKind::Keyboard ? keyboards_[target] : mice_[target];
    }
    void pair() {
        if (paired_ || router_.ready() || router_.seat(0).keyboard || router_.seat(1).keyboard ||
            router_.seat(0).mouse || router_.seat(1).mouse)
            throw std::runtime_error("Router hotplug requires an empty router.");
        for (unsigned i = 0; i < 2; ++i) key(keyboards_[i], 0x40, VK_F6, true);
        for (unsigned i = 0; i < 2; ++i) {
            mouse(mice_[i], RI_MOUSE_RIGHT_BUTTON_DOWN);
            mouse(mice_[i], RI_MOUSE_RIGHT_BUTTON_UP);
        }
        paired_ = true;
        assert_bindings(false);
    }
    void hold_and_remove() {
        if (!paired_ || removed_) throw std::runtime_error("Unexpected router removal phase.");
        assert_bindings(false);
        if (kind_ == HotplugKind::Keyboard) key(keyboards_[target_], 0x2A, VK_SHIFT, true);
        else { position(target_); mouse(mice_[target_], RI_MOUSE_LEFT_BUTTON_DOWN); }
        router_.device_changed(GIDC_REMOVAL, retired_);
        removed_ = true;
        assert_bindings(true);
        probe_retired();
        // Ordinary input from the incomplete pair must neither route nor pair.
        key(keyboards_[target_], 0x13, 'R', true);
        key(keyboards_[target_], 0x13, 'R', false);
        mouse(mice_[target_], RI_MOUSE_LEFT_BUTTON_DOWN | RI_MOUSE_LEFT_BUTTON_UP);
        assert_bindings(true);
    }
    void reassociate() {
        if (!removed_ || restored_) throw std::runtime_error("Unexpected router reassociation phase.");
        assert_bindings(true);
        if (kind_ == HotplugKind::Keyboard) {
            keyboards_[target_] = handle(0x300 + target_);
            key(keyboards_[target_], 0x40, VK_F6, true);
        } else {
            mice_[target_] = handle(0x400 + target_);
            mouse(mice_[target_], RI_MOUSE_RIGHT_BUTTON_DOWN);
            mouse(mice_[target_], RI_MOUSE_RIGHT_BUTTON_UP);
        }
        restored_ = true;
        assert_bindings(false);
        probe_retired();
    }
    void click_editor(unsigned index) {
        require_routable(index);
        position(index);
        mouse(mice_[index], RI_MOUSE_LEFT_BUTTON_DOWN | RI_MOUSE_LEFT_BUTTON_UP);
    }
    void type_fixture(unsigned index) {
        require_routable(index);
        constexpr unsigned codes[] = {0x13, 0x20, 0x19, 0x0C, 0x1F, 0x12, 0x1E, 0x14, 0x0C};
        constexpr unsigned vkeys[] = {'R', 'D', 'P', VK_OEM_MINUS, 'S', 'E', 'A', 'T', VK_OEM_MINUS};
        for (unsigned i = 0; i < std::size(codes); ++i) {
            key(keyboards_[index], codes[i], vkeys[i], true);
            key(keyboards_[index], codes[i], vkeys[i], false);
        }
        const unsigned code = index == 0 ? 0x1E : 0x30;
        const unsigned vkey = index == 0 ? 'A' : 'B';
        key(keyboards_[index], code, vkey, true);
        key(keyboards_[index], code, vkey, false);
    }
    void probe_retired() {
        if (!removed_) throw std::runtime_error("No retired fixture device.");
        if (kind_ == HotplugKind::Keyboard) {
            key(retired_, 0x13, 'R', true); key(retired_, 0x13, 'R', false);
        } else {
            RAWMOUSE input{}; input.lLastX = 1;
            input.usButtonFlags = RI_MOUSE_LEFT_BUTTON_DOWN | RI_MOUSE_LEFT_BUTTON_UP;
            router_.mouse(retired_, input, true);
        }
        assert_bindings(!restored_);
    }
private:
    InputRouter& router_;
    HotplugKind kind_;
    unsigned target_;
    std::array<HANDLE, 2> keyboards_{}, mice_{};
    HANDLE retired_{};
    bool paired_{}, removed_{}, restored_{};
    static HANDLE handle(unsigned value) { return reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(value)); }
    void key(HANDLE device, unsigned code, unsigned vkey, bool down) {
        RAWKEYBOARD input{};
        input.MakeCode = static_cast<USHORT>(code); input.VKey = static_cast<USHORT>(vkey);
        input.Flags = static_cast<USHORT>(down ? 0 : RI_KEY_BREAK);
        router_.keyboard(device, input, true);
    }
    void mouse(HANDLE device, USHORT flags) {
        RAWMOUSE input{}; input.usButtonFlags = flags; router_.mouse(device, input, true);
    }
    void require_routable(unsigned index) const {
        if (!paired_ || index > 1 || !router_.routable(index))
            throw std::runtime_error("Router fixture seat is not routable.");
    }
    void position(unsigned index) {
        require_routable(index);
        const auto& seat = router_.seat(index);
        if (seat.width <= 250 || seat.height <= 180) throw std::runtime_error("Fixture editor point is outside its surface.");
        RAWMOUSE input{}; input.lLastX = 250 - seat.x; input.lLastY = 180 - seat.y;
        router_.mouse(mice_[index], input, true);
    }
    void assert_bindings(bool incomplete) const {
        for (unsigned i = 0; i < 2; ++i) {
            const bool missing = incomplete && i == target_;
            const auto& seat = router_.seat(i);
            if (seat.keyboard != (missing && kind_ == HotplugKind::Keyboard ? nullptr : keyboards_[i]) ||
                seat.mouse != (missing && kind_ == HotplugKind::Mouse ? nullptr : mice_[i]) ||
                router_.routable(i) == missing || !seat.held_keys.empty() || seat.held_buttons)
                throw std::runtime_error("Router hotplug fixture lost a binding or held input invariant.");
        }
    }
};
}
