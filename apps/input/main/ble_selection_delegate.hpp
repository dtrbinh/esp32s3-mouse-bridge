#pragma once

#include "ble_peer.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

enum class BleSelectionFailure : uint8_t {
    ConnectionFailed,
    NotMouse,
};

enum class BleClientCommandKind : uint8_t {
    Connect,
    Disconnect,
    Forget,
    StartScan,
    StopScan,
};

struct BleClientCommand {
    BleClientCommandKind kind{};
    BlePeerAddress peer{};
};

class BleSelectionDelegate {
public:
    virtual ~BleSelectionDelegate() = default;
    virtual void on_client_start(uint32_t) {}
    virtual void on_advertisement(
        const BleAdvertisement& advertisement,
        std::optional<BlePeerAddress> resolved_identity,
        uint32_t now_ms) = 0;
    virtual void on_validated_connection(BlePeerAddress observed,
                                         BlePeerAddress stable_identity,
                                         std::string_view name,
                                         uint32_t now_ms) = 0;
    virtual void on_attempt_failed(BlePeerAddress observed,
                                   BleSelectionFailure failure,
                                   uint32_t now_ms) = 0;
    virtual void on_disconnected(uint32_t now_ms) = 0;
};
