#pragma once
#include <span>
#include <stdexcept>
#include <string>

namespace rds_lab {
enum class HotplugKind { None, Keyboard, Mouse };
struct Options {
    bool smoke{}, self_test{}, synthetic{}, check_config{}, unattended{}, recovery{}, close_view_recovery{};
    unsigned recovery_index{};
    HotplugKind router_hotplug = HotplugKind::None;
    std::wstring path;
};
inline Options parse_options(std::span<const std::wstring> arguments) {
    Options result;
    bool recovery_seat_given = false;
    for (size_t i = 0; i < arguments.size(); ++i) {
        const auto& argument = arguments[i];
        if (argument == L"--smoke") result.smoke = true;
        else if (argument == L"--self-test") result.self_test = true;
        else if (argument == L"--transport-check") result.synthetic = true;
        else if (argument == L"--unattended-fixtures") result.unattended = true;
        else if (argument == L"--recovery-cycle") result.recovery = true;
        else if (argument == L"--close-view-recovery") result.close_view_recovery = true;
        else if (argument == L"--check-config") result.check_config = true;
        else if (argument == L"--router-hotplug") {
            if (result.router_hotplug != HotplugKind::None || i + 1 >= arguments.size())
                throw std::runtime_error("A single router hotplug device kind is required.");
            const auto& kind = arguments[++i];
            if (kind != L"keyboard" && kind != L"mouse")
                throw std::runtime_error("Router hotplug kind must be keyboard or mouse.");
            result.router_hotplug = kind == L"keyboard" ? HotplugKind::Keyboard : HotplugKind::Mouse;
        }
        else if (argument == L"--recovery-seat") {
            if (recovery_seat_given || i + 1 >= arguments.size())
                throw std::runtime_error("A single recovery seat, 1 or 2, is required.");
            const auto& seat = arguments[++i];
            if (seat != L"1" && seat != L"2")
                throw std::runtime_error("Recovery seat must be 1 or 2.");
            result.recovery_index = seat == L"1" ? 0U : 1U;
            recovery_seat_given = true;
        }
        else if (argument == L"--config" && i + 1 < arguments.size()) result.path = arguments[++i];
        else throw std::runtime_error("Unknown or incomplete lab command option.");
    }
    if (static_cast<int>(result.smoke) + result.self_test + result.synthetic + result.check_config > 1 ||
        ((result.smoke || result.self_test) && !result.path.empty()) || (result.unattended && !result.synthetic) ||
        (result.recovery && (!result.synthetic || !result.unattended)) ||
        (recovery_seat_given && !result.recovery && result.router_hotplug == HotplugKind::None) ||
        (result.close_view_recovery && !result.recovery) ||
        (result.router_hotplug != HotplugKind::None &&
            (!result.synthetic || !result.unattended || result.recovery || result.close_view_recovery)))
        throw std::runtime_error("Conflicting command options.");
    return result;
}
}
