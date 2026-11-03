#pragma once

#include <cstdint>
#include <span>

namespace hid_parser {

enum class ParseStatus : uint8_t {
    Ok,
    Truncated,
    LongItem,
    StackOverflow,
    StackUnderflow,
    TooManyFields,
    UnsupportedAbsolutePointer,
    MissingRelativeAxes,
};

struct ScalarField {
    uint16_t bit_offset{};
    uint8_t bit_size{};
    bool valid{};
};

struct MouseLayout {
    bool has_report_id{};
    uint8_t report_id{};
    uint16_t report_bits{};
    uint16_t button_bit_offset{};
    uint8_t button_count{};
    ScalarField x{};
    ScalarField y{};
    ScalarField wheel{};
    ScalarField pan{};
};

struct ParseResult {
    ParseStatus status{ParseStatus::Truncated};
    MouseLayout layout{};
};

ParseResult parse_mouse_descriptor(std::span<const uint8_t> descriptor);

}  // namespace hid_parser

