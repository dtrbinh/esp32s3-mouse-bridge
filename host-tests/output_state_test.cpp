#include "output_state.hpp"

#include <cassert>
#include <cstdint>
#include <limits>

using mouse_bridge::MouseReport;
using mouse_bridge::ReportFlags;

int main() {
    // A busy HID endpoint must not lose reports that arrive before tick().
    OutputState busy_state;
    (void)busy_state.tick(0, false);
    MouseReport busy_first{};
    busy_first.x = 7;
    busy_first.y = -4;
    busy_first.wheel = 1;
    busy_first.buttons = 1;
    busy_state.on_packet(busy_first, 1, 1'000);
    MouseReport busy_second{};
    busy_second.x = 5;
    busy_second.y = 2;
    busy_second.wheel = 3;
    busy_second.buttons = 2;
    busy_state.on_packet(busy_second, 2, 1'500);
    auto busy_frame = busy_state.tick(2'000, false);
    assert(busy_frame.x == 12 && busy_frame.y == -2);
    assert(busy_frame.wheel == 4 && busy_frame.buttons == 2);

    OutputState state;
    auto frame = state.tick(0, false);
    assert(frame.x == 0 && frame.y == 0 && frame.buttons == 0);

    MouseReport first{};
    first.x = 100;
    first.y = -50;
    first.physical_x = 4;
    first.physical_y = -3;
    first.wheel = 2;
    first.pan = -1;
    first.buttons = 0x8001;
    first.flags = ReportFlags::Connected | ReportFlags::GeneratedMotion;
    state.on_packet(first, 1, 1'000);

    frame = state.tick(2'000, false);
    assert(frame.x == 100 && frame.y == -50);
    assert(frame.wheel == 2 && frame.pan == -1);
    assert(frame.buttons == 0x8001);

    // Motion is consumed once; button state is held.
    frame = state.tick(3'000, false);
    assert(frame.x == 0 && frame.y == 0);
    assert(frame.buttons == 0x8001);

    state.on_packet(first, 2, 4'000);
    frame = state.tick(4'500, true);
    assert(frame.x == 4 && frame.y == -3);
    assert(frame.buttons == 0x8001);

    // Five milliseconds without a packet discards pending motion.
    state.on_packet(first, 3, 10'000);
    frame = state.tick(15'000, false);
    assert(frame.x == 0 && frame.y == 0);
    assert(frame.buttons == 0x8001);

    // Event-driven BLE mice can remain silent for the entire button hold.
    // Idle time must never synthesize a release.
    assert(!state.update_timeouts(30'000));
    frame = state.tick(30'000, false);
    assert(frame.buttons == 0x8001);
    assert(frame.link_active);

    // A later movement report with the same held state must not create a
    // second click, and only the physical release clears the buttons.
    MouseReport held_movement{};
    held_movement.x = 3;
    held_movement.buttons = 0x8001;
    state.on_packet(held_movement, 4, 31'000);
    frame = state.tick(31'500, false);
    assert(frame.buttons == 0x8001 && frame.x == 3);
    MouseReport physical_release{};
    state.on_packet(physical_release, 5, 32'000);
    frame = state.tick(32'500, false);
    assert(frame.buttons == 0);
    state.on_packet(physical_release, 6, 33'000);
    frame = state.tick(33'500, false);
    assert(frame.buttons == 0);
    assert(state.timeout_count() == 0);

    // A disconnect boundary is the failsafe release for a lost link.
    state.on_packet(first, 7, 34'000);
    MouseReport disconnect_boundary{};
    disconnect_boundary.flags = ReportFlags::Boundary;
    state.on_packet(disconnect_boundary, 8, 34'500);
    frame = state.tick(35'000, false);
    assert(frame.buttons == 0 && !frame.link_active);
    assert(state.timeout_count() == 1);

    // USB resume always begins with a release-all report.
    state.on_packet(first, 9, 36'000);
    state.on_usb_resume(36'500);
    frame = state.tick(37'000, false);
    assert(frame.x == 0 && frame.y == 0 && frame.buttons == 0);

    MouseReport large{};
    large.x = std::numeric_limits<int16_t>::max();
    large.y = std::numeric_limits<int16_t>::min();
    large.wheel = std::numeric_limits<int8_t>::max();
    large.pan = std::numeric_limits<int8_t>::min();
    state.on_packet(large, 10, 40'000);
    state.on_packet(large, 11, 40'500);
    frame = state.tick(41'000, false);
    assert(frame.x == std::numeric_limits<int16_t>::max());
    assert(frame.y == std::numeric_limits<int16_t>::min());
    assert(frame.wheel == std::numeric_limits<int8_t>::max());
    assert(frame.pan == std::numeric_limits<int8_t>::min());

    // No callback for a rejected packet means no state change.
    frame = state.tick(42'000, false);
    assert(frame.x == 0 && frame.y == 0);

    // Checking failsafes on skipped USB frames must not consume motion.
    MouseReport skipped{};
    skipped.x = 12;
    skipped.y = -9;
    state.on_packet(skipped, 12, 50'000);
    state.update_timeouts(51'000);
    frame = state.tick(51'000, false);
    assert(frame.x == 12 && frame.y == -9);
}
