#include "hid_parser/keyboard_decoder.hpp"

#include <algorithm>

namespace hid_parser {
namespace {

uint32_t extract_unsigned(std::span<const uint8_t> bytes, uint16_t offset,
                          uint8_t size) {
    uint32_t value = 0;
    for (uint8_t bit = 0; bit < size; ++bit) {
        const uint16_t source = offset + bit;
        value |= static_cast<uint32_t>(
                     (bytes[source / 8U] >> (source % 8U)) & 1U)
                 << bit;
    }
    return value;
}

void append_key(mouse_bridge::KeyboardReport& output, uint8_t usage) {
    if (usage == 0 || usage >= 0xE0) return;
    if (std::find(output.keys.begin(), output.keys.end(), usage) !=
        output.keys.end()) {
        return;
    }
    const auto empty = std::find(output.keys.begin(), output.keys.end(), 0);
    if (empty != output.keys.end()) *empty = usage;
}

}  // namespace

DecodeStatus decode_keyboard_report(const KeyboardLayout& layout,
                                    std::span<const uint8_t> report,
                                    mouse_bridge::KeyboardReport& output) {
    if (!layout.key_array_valid && !layout.key_bitmap_valid) {
        return DecodeStatus::InvalidLayout;
    }
    if (layout.has_report_id) {
        if (report.empty()) return DecodeStatus::Truncated;
        if (report[0] != layout.report_id) return DecodeStatus::WrongReportId;
        report = report.subspan(1);
    }
    if (report.size() < (layout.report_bits + 7U) / 8U) {
        return DecodeStatus::Truncated;
    }

    output = {};
    for (size_t i = 0; i < layout.modifiers.size(); ++i) {
        const auto& field = layout.modifiers[i];
        if (field.valid && extract_unsigned(report, field.bit_offset,
                                            field.bit_size) != 0) {
            output.modifiers |= static_cast<uint8_t>(1U << i);
        }
    }
    if (layout.key_array_valid) {
        for (uint8_t i = 0; i < layout.key_array_count; ++i) {
            const uint16_t offset = static_cast<uint16_t>(
                layout.key_array_bit_offset + i * layout.key_array_bit_size);
            append_key(output, static_cast<uint8_t>(extract_unsigned(
                                   report, offset,
                                   layout.key_array_bit_size)));
        }
    }
    if (layout.key_bitmap_valid) {
        const uint16_t count = std::min<uint16_t>(
            layout.key_bitmap_count, 256 - layout.key_bitmap_usage_min);
        for (uint16_t i = 0; i < count; ++i) {
            if (extract_unsigned(
                    report,
                    static_cast<uint16_t>(layout.key_bitmap_bit_offset + i),
                    1) != 0) {
                append_key(output, static_cast<uint8_t>(
                                       layout.key_bitmap_usage_min + i));
            }
        }
    }
    return DecodeStatus::Ok;
}

}  // namespace hid_parser
