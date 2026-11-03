#pragma once

#include <cstdint>

namespace mouse_bridge {

enum class InputSource : uint8_t {
    None = 0,
    Usb = 1,
    Ble = 2,
};

enum class ReportFlags : uint8_t {
    None = 0,
    Connected = 1U << 0,
    Overflow = 1U << 1,
    Bypass = 1U << 2,
    GeneratedMotion = 1U << 3,
    Boundary = 1U << 4,
};

constexpr ReportFlags operator|(ReportFlags lhs, ReportFlags rhs) {
    return static_cast<ReportFlags>(static_cast<uint8_t>(lhs) |
                                    static_cast<uint8_t>(rhs));
}

constexpr ReportFlags operator&(ReportFlags lhs, ReportFlags rhs) {
    return static_cast<ReportFlags>(static_cast<uint8_t>(lhs) &
                                    static_cast<uint8_t>(rhs));
}

constexpr bool has_flag(ReportFlags value, ReportFlags flag) {
    return static_cast<uint8_t>(value & flag) != 0;
}

struct MouseReport {
    uint64_t timestamp_us{};
    int16_t x{};
    int16_t y{};
    int8_t wheel{};
    int8_t pan{};
    uint16_t buttons{};
    InputSource source{InputSource::None};
    ReportFlags flags{ReportFlags::None};

    constexpr bool operator==(const MouseReport&) const = default;
};

}  // namespace mouse_bridge

