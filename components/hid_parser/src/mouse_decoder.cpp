#include "hid_parser/mouse_decoder.hpp"

#include <algorithm>
#include <cstddef>

namespace hid_parser {
namespace {

uint32_t extract_unsigned(std::span<const uint8_t> bytes, uint16_t offset,
                          uint8_t size) {
    uint32_t value = 0;
    for (uint8_t bit = 0; bit < size; ++bit) {
        const uint16_t source = offset + bit;
        const uint8_t set = (bytes[source / 8U] >> (source % 8U)) & 1U;
        value |= static_cast<uint32_t>(set) << bit;
    }
    return value;
}

int32_t extract_signed(std::span<const uint8_t> bytes, const ScalarField& field) {
    const uint32_t raw = extract_unsigned(bytes, field.bit_offset, field.bit_size);
    if (field.bit_size == 32) return static_cast<int32_t>(raw);
    const uint32_t sign = 1U << (field.bit_size - 1U);
    return static_cast<int32_t>((raw ^ sign) - sign);
}

int16_t clamp_i16(int32_t value) {
    return static_cast<int16_t>(std::clamp(value, -32768, 32767));
}

int8_t clamp_i8(int32_t value) {
    return static_cast<int8_t>(std::clamp(value, -128, 127));
}

}  // namespace

DecodeStatus decode_mouse_report(const MouseLayout& layout,
                                 std::span<const uint8_t> report,
                                 mouse_bridge::MouseReport& output) {
    if (!layout.x.valid || !layout.y.valid || layout.x.bit_size == 0 ||
        layout.y.bit_size == 0 || layout.x.bit_size > 16 ||
        layout.y.bit_size > 16) {
        return DecodeStatus::InvalidLayout;
    }
    if (layout.has_report_id) {
        if (report.empty()) return DecodeStatus::Truncated;
        if (report[0] != layout.report_id) return DecodeStatus::WrongReportId;
        report = report.subspan(1);
    }
    const size_t required = (layout.report_bits + 7U) / 8U;
    if (report.size() < required) return DecodeStatus::Truncated;

    output = {};
    if (layout.button_count > 0) {
        output.buttons = static_cast<uint16_t>(extract_unsigned(
            report, layout.button_bit_offset, std::min<uint8_t>(16, layout.button_count)));
    }
    output.x = clamp_i16(extract_signed(report, layout.x));
    output.y = clamp_i16(extract_signed(report, layout.y));
    if (layout.wheel.valid) output.wheel = clamp_i8(extract_signed(report, layout.wheel));
    if (layout.pan.valid) output.pan = clamp_i8(extract_signed(report, layout.pan));
    return DecodeStatus::Ok;
}

}  // namespace hid_parser
