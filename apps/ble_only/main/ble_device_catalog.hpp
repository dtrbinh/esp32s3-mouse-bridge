#pragma once

#include "ble_peer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

struct BleNearbyDevice {
    BlePeerAddress address{};
    std::array<char, 33> name{};
    int8_t rssi{};
    std::optional<uint16_t> appearance{};
    bool advertises_hid{};
    uint32_t last_seen_ms{};
};

struct BleNearbySnapshot {
    std::array<BleNearbyDevice, 32> devices{};
    size_t count{};
};

class BleDeviceCatalog {
public:
    static constexpr size_t kCapacity = 32;
    static constexpr uint32_t kStaleAfterMs = 15'000;

    void observe(const BleAdvertisement& advertisement, uint32_t now_ms);
    [[nodiscard]] BleNearbySnapshot snapshot(uint32_t now_ms) const;
    [[nodiscard]] size_t active_count(uint32_t now_ms) const;
    void clear();

private:
    std::array<BleNearbyDevice, kCapacity> devices_{};
    size_t count_{};
};
