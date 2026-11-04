#pragma once

#include "ble_peer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

struct BleSavedDevice {
    BlePeerAddress identity{};
    std::array<char, 33> name{};
    uint32_t last_success_order{};
    bool connected{};
};

struct BleSavedSnapshot {
    std::array<BleSavedDevice, 8> devices{};
    size_t count{};
};

class BleDeviceHistory {
public:
    static constexpr size_t kCapacity = 8;

    void record_success(BlePeerAddress identity, std::string_view name);
    bool forget(BlePeerAddress identity);
    void disconnect_without_forget();
    void select(BlePeerAddress identity);

    [[nodiscard]] std::optional<BlePeerAddress> selected() const {
        return selected_;
    }
    [[nodiscard]] size_t count() const { return count_; }
    [[nodiscard]] BleSavedSnapshot snapshot(
        std::optional<BlePeerAddress> connected_identity) const;

private:
    friend size_t encode_ble_history(const BleDeviceHistory&,
                                     std::span<uint8_t>);
    friend std::optional<BleDeviceHistory> decode_ble_history(
        std::span<const uint8_t>);

    void renormalize_orders();

    std::array<BleSavedDevice, kCapacity> devices_{};
    size_t count_{};
    std::optional<BlePeerAddress> selected_{};
    uint32_t next_order_{1};
};

size_t encode_ble_history(const BleDeviceHistory& history,
                          std::span<uint8_t> output);
std::optional<BleDeviceHistory> decode_ble_history(
    std::span<const uint8_t> input);
