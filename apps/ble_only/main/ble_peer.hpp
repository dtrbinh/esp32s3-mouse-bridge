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

constexpr uint8_t normalize_ble_address_type(uint8_t type) {
    return type == 2 ? 0 : (type == 3 ? 1 : type);
}

constexpr BlePeerAddress normalize_ble_address(BlePeerAddress address) {
    address.type = normalize_ble_address_type(address.type);
    return address;
}

bool format_ble_address(const BlePeerAddress& address,
                        std::span<char> destination);
