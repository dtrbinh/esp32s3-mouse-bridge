#include "hid_parser/report_descriptor.hpp"

#include <array>
#include <cstddef>

namespace hid_parser {
namespace {

struct Globals {
    uint16_t usage_page{};
    int32_t logical_min{};
    int32_t logical_max{};
    uint8_t report_size{};
    uint8_t report_count{};
    uint8_t report_id{};
};

struct Locals {
    std::array<uint32_t, 32> usages{};
    size_t usage_count{};
    uint32_t usage_min{};
    uint32_t usage_max{};
    bool has_range{};

    void clear() { *this = {}; }
    uint32_t usage_at(size_t index) const {
        if (index < usage_count) return usages[index];
        if (has_range && usage_min + index <= usage_max) return usage_min + index;
        return 0;
    }
};

uint32_t read_unsigned(std::span<const uint8_t> bytes) {
    uint32_t value = 0;
    for (size_t i = 0; i < bytes.size(); ++i) value |= uint32_t(bytes[i]) << (8U * i);
    return value;
}

int32_t read_signed(std::span<const uint8_t> bytes) {
    const uint32_t raw = read_unsigned(bytes);
    if (bytes.empty()) return 0;
    const unsigned bits = static_cast<unsigned>(bytes.size() * 8U);
    if (bits == 32) return static_cast<int32_t>(raw);
    const uint32_t sign = 1U << (bits - 1U);
    return static_cast<int32_t>((raw ^ sign) - sign);
}

}  // namespace

ParseResult parse_mouse_descriptor(std::span<const uint8_t> descriptor) {
    ParseResult result{};
    Globals global{};
    std::array<Globals, 4> stack{};
    size_t stack_size = 0;
    Locals local{};
    std::array<uint16_t, 256> offsets{};
    int collection_depth = 0;
    int mouse_depth = -1;
    size_t cursor = 0;

    while (cursor < descriptor.size()) {
        const uint8_t prefix = descriptor[cursor++];
        if (prefix == 0xFE) {
            result.status = ParseStatus::LongItem;
            return result;
        }
        const uint8_t size_code = prefix & 0x03U;
        const size_t size = size_code == 3 ? 4 : size_code;
        if (cursor + size > descriptor.size()) {
            result.status = ParseStatus::Truncated;
            return result;
        }
        const auto data = descriptor.subspan(cursor, size);
        cursor += size;
        const uint8_t type = (prefix >> 2U) & 0x03U;
        const uint8_t tag = (prefix >> 4U) & 0x0FU;
        const uint32_t uvalue = read_unsigned(data);

        if (type == 1) {  // Global.
            switch (tag) {
                case 0: global.usage_page = static_cast<uint16_t>(uvalue); break;
                case 1: global.logical_min = read_signed(data); break;
                case 2: global.logical_max = read_signed(data); break;
                case 7: global.report_size = static_cast<uint8_t>(uvalue); break;
                case 8:
                    global.report_id = static_cast<uint8_t>(uvalue);
                    result.layout.has_report_id = true;
                    break;
                case 9: global.report_count = static_cast<uint8_t>(uvalue); break;
                case 10:
                    if (stack_size == stack.size()) {
                        result.status = ParseStatus::StackOverflow;
                        return result;
                    }
                    stack[stack_size++] = global;
                    break;
                case 11:
                    if (stack_size == 0) {
                        result.status = ParseStatus::StackUnderflow;
                        return result;
                    }
                    global = stack[--stack_size];
                    break;
                default: break;
            }
            continue;
        }

        if (type == 2) {  // Local.
            if (tag == 0 && local.usage_count < local.usages.size()) {
                local.usages[local.usage_count++] = uvalue;
            } else if (tag == 1) {
                local.usage_min = uvalue;
                local.has_range = true;
            } else if (tag == 2) {
                local.usage_max = uvalue;
                local.has_range = true;
            }
            continue;
        }

        if (type != 0) continue;
        if (tag == 10) {  // Collection.
            ++collection_depth;
            if (global.usage_page == 0x01 && local.usage_at(0) == 0x02 &&
                mouse_depth < 0) {
                mouse_depth = collection_depth;
            }
            local.clear();
            continue;
        }
        if (tag == 12) {  // End Collection.
            if (mouse_depth == collection_depth) mouse_depth = -1;
            --collection_depth;
            local.clear();
            continue;
        }
        if (tag != 8) {  // Only Input advances input report offsets here.
            local.clear();
            continue;
        }

        const uint16_t base = offsets[global.report_id];
        const uint16_t width = static_cast<uint16_t>(global.report_size) * global.report_count;
        const bool constant = (uvalue & 0x01U) != 0;
        const bool relative = (uvalue & 0x04U) != 0;
        if (mouse_depth >= 0 && !constant) {
            for (size_t i = 0; i < global.report_count; ++i) {
                const uint32_t usage = local.usage_at(i);
                const uint16_t bit_offset = base + static_cast<uint16_t>(i * global.report_size);
                if (global.usage_page == 0x09 && usage >= 1 && usage <= 16) {
                    if (result.layout.button_count == 0) {
                        result.layout.button_bit_offset = bit_offset;
                    }
                    ++result.layout.button_count;
                } else if (global.usage_page == 0x01 &&
                           (usage == 0x30 || usage == 0x31 || usage == 0x38)) {
                    if (!relative) {
                        result.status = ParseStatus::UnsupportedAbsolutePointer;
                        return result;
                    }
                    ScalarField field{bit_offset, global.report_size, true};
                    if (usage == 0x30) result.layout.x = field;
                    if (usage == 0x31) result.layout.y = field;
                    if (usage == 0x38) result.layout.wheel = field;
                } else if (global.usage_page == 0x0C && usage == 0x238) {
                    if (!relative) {
                        result.status = ParseStatus::UnsupportedAbsolutePointer;
                        return result;
                    }
                    result.layout.pan = {bit_offset, global.report_size, true};
                }
            }
            result.layout.report_id = global.report_id;
        }
        offsets[global.report_id] = static_cast<uint16_t>(base + width);
        local.clear();
    }

    if (!result.layout.x.valid || !result.layout.y.valid) {
        result.status = ParseStatus::MissingRelativeAxes;
        return result;
    }
    result.layout.report_bits = offsets[result.layout.report_id];
    result.status = ParseStatus::Ok;
    return result;
}

}  // namespace hid_parser

