#include "output_state.hpp"

#include <cassert>
#include <cstdint>
#include <limits>

using mouse_bridge::MouseReport;
using mouse_bridge::ReportFlags;

int main() {
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

    // Twenty milliseconds releases all held buttons.
    frame = state.tick(30'000, false);
    assert(frame.buttons == 0);
    assert(!frame.link_active);
    assert(state.timeout_count() == 1);
    (void)state.tick(31'000, false);
    assert(state.timeout_count() == 1);

    // USB resume always begins with a release-all report.
    state.on_packet(first, 4, 31'000);
    state.on_usb_resume(31'500);
    frame = state.tick(32'000, false);
    assert(frame.x == 0 && frame.y == 0 && frame.buttons == 0);

    MouseReport large{};
    large.x = std::numeric_limits<int16_t>::max();
    large.y = std::numeric_limits<int16_t>::min();
    large.wheel = std::numeric_limits<int8_t>::max();
    large.pan = std::numeric_limits<int8_t>::min();
    state.on_packet(large, 5, 40'000);
    state.on_packet(large, 6, 40'500);
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
    state.on_packet(skipped, 7, 50'000);
    state.update_timeouts(51'000);
    frame = state.tick(51'000, false);
    assert(frame.x == 12 && frame.y == -9);
}
