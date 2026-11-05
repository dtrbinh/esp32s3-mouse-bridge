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
    MissingKeyboardInput,
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

struct KeyboardLayout {
    bool has_report_id{};
    uint8_t report_id{};
    uint16_t report_bits{};
    std::array<ScalarField, 8> modifiers{};
    uint16_t key_array_bit_offset{};
    uint8_t key_array_bit_size{};
    uint8_t key_array_count{};
    bool key_array_valid{};
    uint16_t key_bitmap_bit_offset{};
    uint16_t key_bitmap_usage_min{};
    uint16_t key_bitmap_count{};
    bool key_bitmap_valid{};
};

struct KeyboardParseResult {
    ParseStatus status{ParseStatus::Truncated};
    KeyboardLayout layout{};
};

ParseResult parse_mouse_descriptor(std::span<const uint8_t> descriptor);
KeyboardParseResult parse_keyboard_descriptor(
    std::span<const uint8_t> descriptor);

}  // namespace hid_parser

