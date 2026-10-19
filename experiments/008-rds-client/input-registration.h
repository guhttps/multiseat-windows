#pragma once
#include <windows.h>

namespace rds_lab {
enum class RegistrationMode { None, Notifications, Foreground };
// Register device-change notifications without background input sinks.
// The injectable boundary keeps offline tests away from real host input.
class InputRegistration {
public:
    using Register = decltype(&::RegisterRawInputDevices);
    explicit InputRegistration(Register function = ::RegisterRawInputDevices) : register_(function) {}
    InputRegistration(const InputRegistration&) = delete;
    InputRegistration& operator=(const InputRegistration&) = delete;
    ~InputRegistration() { release(); }
    RegistrationMode mode() const noexcept { return mode_; }
    bool set(HWND target, bool foreground) {
        if (!register_ || !target || (mode_ != RegistrationMode::None && target != target_)) return false;
        const RegistrationMode requested = foreground ? RegistrationMode::Foreground : RegistrationMode::Notifications;
        if (mode_ == requested) return true;
        const DWORD flags = RIDEV_DEVNOTIFY | (foreground ? RIDEV_NOLEGACY : 0U);
        RAWINPUTDEVICE devices[] = {{1, 6, flags, target}, {1, 2, flags, target}};
        if (!register_(devices, 2, sizeof(RAWINPUTDEVICE))) return false;
        target_ = target;
        mode_ = requested;
        return true;
    }
    bool release() noexcept {
        if (mode_ == RegistrationMode::None) return true;
        RAWINPUTDEVICE devices[] = {{1, 6, RIDEV_REMOVE, nullptr}, {1, 2, RIDEV_REMOVE, nullptr}};
        if (!register_(devices, 2, sizeof(RAWINPUTDEVICE))) return false;
        target_ = nullptr;
        mode_ = RegistrationMode::None;
        return true;
    }
private:
    Register register_;
    HWND target_{};
    RegistrationMode mode_ = RegistrationMode::None;
};
}
