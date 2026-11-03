#pragma once

#include "mouse_bridge/mouse_report.hpp"

#include <cstdint>

namespace spi_link {

mouse_bridge::MouseReport make_keepalive(
    const mouse_bridge::MouseReport& state, uint64_t timestamp_us);

}  // namespace spi_link
