#include "spi_link/keepalive.hpp"

namespace spi_link {

mouse_bridge::MouseReport make_keepalive(
    const mouse_bridge::MouseReport& state, uint64_t timestamp_us) {
    auto keepalive = state;
    keepalive.timestamp_us = timestamp_us;
    keepalive.x = 0;
    keepalive.y = 0;
    keepalive.physical_x = 0;
    keepalive.physical_y = 0;
    keepalive.wheel = 0;
    keepalive.pan = 0;
    constexpr uint8_t persistent_flags =
        static_cast<uint8_t>(mouse_bridge::ReportFlags::Connected) |
        static_cast<uint8_t>(mouse_bridge::ReportFlags::Bypass);
    keepalive.flags = static_cast<mouse_bridge::ReportFlags>(
        static_cast<uint8_t>(state.flags) & persistent_flags);
    return keepalive;
}

}  // namespace spi_link
