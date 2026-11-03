#pragma once

#include "hid_parser/report_descriptor.hpp"
#include "mouse_bridge/mouse_report.hpp"

#include <cstdint>
#include <span>

namespace hid_parser {

enum class DecodeStatus : uint8_t {
    Ok,
    Truncated,
    WrongReportId,
    InvalidLayout,
};

DecodeStatus decode_mouse_report(const MouseLayout& layout,
                                 std::span<const uint8_t> report,
                                 mouse_bridge::MouseReport& output);

}  // namespace hid_parser

