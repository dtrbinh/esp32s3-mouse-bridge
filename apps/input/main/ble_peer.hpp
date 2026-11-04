#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

struct BlePeerAddress {
    std::array<uint8_t, 6> bytes{};
    uint8_t type{};

    bool operator==(const BlePeerAddress&) const = default;
};

struct BleAdvertisement {
    BlePeerAddress address{};
    std::string_view name{};
    int8_t rssi{};
    std::optional<uint16_t> appearance{};
    bool advertises_hid{};
};

bool format_ble_address(const BlePeerAddress& address,
                        std::span<char> destination);
