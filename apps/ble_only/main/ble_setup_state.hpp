#pragma once

#include "ble_peer.hpp"

#include <array>
#include <cstdint>
#include <optional>

enum class BleSetupPhase : uint8_t {
    Idle,
    ScanningSelected,
    Portal,
    SwitchDisconnect,
    Connecting,
    Connected,
    SuccessDelay,
    Failed,
};

enum class BleAttemptError : uint8_t {
    None,
    ConnectionFailed,
    NotMouse,
    PersistenceFailed,
};

enum class BleSetupActionKind : uint8_t {
    StartPortal,
    StopPortal,
    Connect,
    Disconnect,
};

struct BleSetupAction {
    BleSetupActionKind kind{};
    std::optional<BlePeerAddress> peer{};
};

struct BleSetupSnapshot {
    BleSetupPhase phase{BleSetupPhase::Idle};
    BleAttemptError error{BleAttemptError::None};
    bool portal_desired{};
    bool portal_active{};
    std::optional<BlePeerAddress> selected{};
    std::optional<BlePeerAddress> connected{};
    std::optional<BlePeerAddress> attempt{};
    std::optional<uint32_t> wifi_shutdown_due_ms{};
};

class BleSetupState {
public:
    void start(std::optional<BlePeerAddress> selected, uint32_t now_ms);
    void tick(uint32_t now_ms);
    void request_portal();
    void request_close();
    void request_connect(BlePeerAddress peer);
    void request_disconnect();
    void forget(BlePeerAddress peer);
    void on_connected(BlePeerAddress peer, uint32_t now_ms);
    void on_disconnected(uint32_t now_ms);
    void on_failed(BlePeerAddress peer, BleAttemptError error);
    void on_reconnect_failed(BlePeerAddress attempted_peer,
                             BlePeerAddress saved_peer,
                             BleAttemptError error, uint32_t now_ms);
    void on_portal_active(bool active);
    void on_portal_start_failed();

    [[nodiscard]] BleSetupSnapshot snapshot() const { return snapshot_; }
    std::optional<BleSetupAction> take_action();

private:
    static constexpr uint32_t kPortalFallbackMs = 10'000;
    static constexpr uint32_t kSuccessDelayMs = 3'000;
    static constexpr size_t kActionCapacity = 4;

    void push(BleSetupActionKind kind,
              std::optional<BlePeerAddress> peer = std::nullopt);
    void ensure_portal();

    BleSetupSnapshot snapshot_{};
    uint32_t reconnect_started_ms_{};
    uint32_t last_now_ms_{};
    std::array<BleSetupAction, kActionCapacity> actions_{};
    size_t action_read_{};
    size_t action_write_{};
    size_t action_count_{};
};
