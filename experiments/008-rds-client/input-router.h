#pragma once
#include <windows.h>
#include <array>
#include <cstdint>
#include <functional>
#include <set>

namespace rds_lab {
enum class EventKind { Key, Pause, Move, Button, Wheel };
struct InputEvent {
    EventKind kind{};
    unsigned code{}; // scan code with E0/E1 bits, button index, or signed wheel delta
    bool down{};
    bool repeat{};
    bool horizontal{};
    int x{};
    int y{};
};
struct SeatInput {
    HANDLE keyboard{};
    HANDLE mouse{};
    int width{}, height{}, x{}, y{};
    std::set<unsigned> held_keys;
    unsigned held_buttons{};
    bool enabled = true;
};
class InputRouter {
public:
    using Sink = std::function<void(unsigned, const InputEvent&)>;
    InputRouter(const std::array<std::pair<unsigned, unsigned>, 2>& sizes, Sink sink);
    bool ready() const;
    void keyboard(HANDLE device, const RAWKEYBOARD& key, bool enabled);
    void mouse(HANDLE device, const RAWMOUSE& mouse, bool enabled);
    void set_enabled(unsigned seat, bool enabled);
    void release_all();
    void reset();
    void device_removed(HANDLE device);
    const SeatInput& seat(size_t index) const { return seats_.at(index); }
private:
    std::array<SeatInput, 2> seats_;
    Sink sink_;
    void button(unsigned seat, unsigned button, bool down);
    void release(unsigned seat);
};
}
