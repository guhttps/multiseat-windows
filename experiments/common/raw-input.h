#pragma once

#include <windows.h>
#include <cstddef>

namespace multiseat {
// Only fixed-size keyboard/mouse packets are registered by these experiments.
// Validate both lengths before accessing the tagged union; zero-filled storage
// must never turn a truncated packet into an apparently valid input event.
inline bool valid_raw_input(const RAWINPUT& input, UINT copied_bytes) {
    if (copied_bytes < sizeof(RAWINPUTHEADER) || copied_bytes > sizeof(RAWINPUT) ||
        input.header.dwSize != copied_bytes) return false;
    if (input.header.dwType == RIM_TYPEKEYBOARD)
        return copied_bytes >= offsetof(RAWINPUT, data) + sizeof(RAWKEYBOARD);
    if (input.header.dwType == RIM_TYPEMOUSE)
        return copied_bytes >= offsetof(RAWINPUT, data) + sizeof(RAWMOUSE);
    return false;
}
}
