#include "spi_link/keepalive.hpp"

#include <cassert>

int main() {
    mouse_bridge::MouseReport state{};
    state.timestamp_us = 100;
    state.x = 20;
    state.y = -10;
    state.physical_x = 15;
    state.physical_y = -8;
    state.wheel = 2;
    state.pan = -1;
    state.buttons = 0x8003;
    state.source = mouse_bridge::InputSource::Ble;
    state.poll_interval_us = 7'500;
    state.flags = mouse_bridge::ReportFlags::Connected |
                  mouse_bridge::ReportFlags::GeneratedMotion |
                  mouse_bridge::ReportFlags::Overflow;

    const auto keepalive = spi_link::make_keepalive(state, 2'000);
    assert(keepalive.timestamp_us == 2'000);
    assert(keepalive.x == 0 && keepalive.y == 0);
    assert(keepalive.physical_x == 0 && keepalive.physical_y == 0);
    assert(keepalive.wheel == 0 && keepalive.pan == 0);
    assert(keepalive.buttons == state.buttons);
    assert(keepalive.source == mouse_bridge::InputSource::Ble);
    assert(keepalive.poll_interval_us == 7'500);
    assert(mouse_bridge::has_flag(keepalive.flags,
                                  mouse_bridge::ReportFlags::Connected));
    assert(!mouse_bridge::has_flag(keepalive.flags,
                                   mouse_bridge::ReportFlags::GeneratedMotion));
    assert(!mouse_bridge::has_flag(keepalive.flags,
                                   mouse_bridge::ReportFlags::Overflow));
    assert(mouse_bridge::has_flag(keepalive.flags,
                                  mouse_bridge::ReportFlags::Keepalive));
}
