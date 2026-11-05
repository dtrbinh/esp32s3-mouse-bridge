#pragma once

#include "hid_parser/mouse_decoder.hpp"
#include "hid_parser/report_descriptor.hpp"
#include "mouse_bridge/mouse_report.hpp"

#include <cstdint>
#include <span>

namespace hid_parser {

DecodeStatus decode_keyboard_report(const KeyboardLayout& layout,
                                    std::span<const uint8_t> report,
                                    mouse_bridge::KeyboardReport& output);

}  // namespace hid_parser
