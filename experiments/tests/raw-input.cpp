#include "../common/raw-input.h"
#include <iostream>

int main() {
    RAWINPUT packet{};
    packet.header.dwType = RIM_TYPEKEYBOARD;
    packet.header.dwSize = sizeof(RAWINPUTHEADER) + sizeof(RAWKEYBOARD);
    const UINT keyboard_bytes = packet.header.dwSize;
    if (!multiseat::valid_raw_input(packet, keyboard_bytes)) return 1;
    if (multiseat::valid_raw_input(packet, sizeof(RAWINPUTHEADER) - 1) ||
        multiseat::valid_raw_input(packet, keyboard_bytes - 1) ||
        multiseat::valid_raw_input(packet, sizeof(RAWINPUT) + 1) ||
        multiseat::valid_raw_input(packet, static_cast<UINT>(-1))) return 1;
    packet.header.dwSize = keyboard_bytes - 1;
    if (multiseat::valid_raw_input(packet, keyboard_bytes - 1)) return 1;
    packet.header.dwType = RIM_TYPEMOUSE;
    packet.header.dwSize = sizeof(RAWINPUTHEADER) + sizeof(RAWMOUSE);
    const UINT mouse_bytes = packet.header.dwSize;
    if (!multiseat::valid_raw_input(packet, mouse_bytes)) return 1;
    packet.header.dwSize = mouse_bytes - 1;
    if (multiseat::valid_raw_input(packet, mouse_bytes - 1)) return 1;
    packet.header.dwType = RIM_TYPEHID;
    packet.header.dwSize = sizeof(RAWINPUT);
    if (multiseat::valid_raw_input(packet, sizeof(RAWINPUT))) return 1;
    packet.header.dwType = 99;
    if (multiseat::valid_raw_input(packet, sizeof(RAWINPUT))) return 1;
    std::cout << "PASS: complete keyboard/mouse packets accepted; truncated, oversized and unknown packets rejected.\n";
    return 0;
}
