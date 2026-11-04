#pragma once

#include "ble_device_catalog.hpp"
#include "ble_device_history.hpp"
#include "ble_history_persistence.hpp"
#include "ble_selection_delegate.hpp"
#include "ble_setup_state.hpp"
#include "portal_codec.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <string_view>

enum class BleManagerDecisionKind : uint8_t {
    StartPortal,
    StopPortal,
    Connect,
    Disconnect,
    Forget,
};

struct BleManagerDecision {
    BleManagerDecisionKind kind{};
    std::optional<BlePeerAddress> peer{};
};

class BleDeviceManager final : public BleSelectionDelegate {
public:
    explicit BleDeviceManager(BleHistoryPersistence& persistence)
        : persistence_(persistence) {}

    void start(uint32_t now_ms);
    void request_connect(BlePeerAddress peer);
    void request_disconnect();
    void request_forget(BlePeerAddress identity);
    void request_portal();
    void request_close();
    void set_portal_active(bool active);
    std::optional<BleManagerDecision> service(uint32_t now_ms);
    [[nodiscard]] PortalView snapshot(uint32_t now_ms) const;

    void on_advertisement(
        const BleAdvertisement& advertisement,
        std::optional<BlePeerAddress> resolved_identity,
        uint32_t now_ms) override;
    void on_validated_connection(BlePeerAddress observed,
                                 BlePeerAddress stable_identity,
                                 std::string_view name,
                                 uint32_t now_ms) override;
    void on_attempt_failed(BlePeerAddress observed,
                           BleSelectionFailure failure,
                           uint32_t now_ms) override;
    void on_disconnected(uint32_t now_ms) override;

private:
    class Guard {
    public:
        explicit Guard(std::atomic_flag& lock) : lock_(lock) {
            while (lock_.test_and_set(std::memory_order_acquire)) {}
        }
        ~Guard() { lock_.clear(std::memory_order_release); }

    private:
        std::atomic_flag& lock_;
    };

    void push_extra(BleManagerDecision decision);
    std::optional<BleManagerDecision> pop_extra();
    static BleManagerDecision translate(BleSetupAction action);

    BleHistoryPersistence& persistence_;
    mutable std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
    BleDeviceCatalog catalog_{};
    BleDeviceHistory history_{};
    BleSetupState setup_{};
    bool history_dirty_{};
    bool persistence_failed_{};
    std::array<BleManagerDecision, 4> extra_{};
    size_t extra_read_{};
    size_t extra_write_{};
    size_t extra_count_{};
};
