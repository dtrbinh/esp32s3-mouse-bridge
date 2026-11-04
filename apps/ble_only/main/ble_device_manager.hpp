#pragma once

#include "ble_device_catalog.hpp"
#include "ble_device_history.hpp"
#include "ble_history_persistence.hpp"
#include "ble_selection_delegate.hpp"
#include "ble_setup_state.hpp"
#include "portal_codec.hpp"

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string_view>

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#endif

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

struct BleManagerStatus {
    BleSetupSnapshot setup{};
    size_t saved_count{};
    size_t nearby_count{};
};

class BleDeviceManager final : public BleSelectionDelegate {
public:
    explicit BleDeviceManager(BleHistoryPersistence& persistence)
        : persistence_(persistence) {}

    void start(uint32_t now_ms);
    bool request_connect(BlePeerAddress peer);
    void request_disconnect();
    void request_forget(BlePeerAddress identity);
    void request_portal();
    void request_close();
    void set_portal_active(bool active);
    void portal_start_failed();
    std::optional<BleManagerDecision> service(uint32_t now_ms);
    [[nodiscard]] PortalView snapshot(uint32_t now_ms) const;
    [[nodiscard]] BleManagerStatus status(uint32_t now_ms) const;

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
    void on_client_start(uint32_t now_ms) override { start(now_ms); }

private:
    class ManagerLock {
    public:
#ifdef ESP_PLATFORM
        ManagerLock() { handle_ = xSemaphoreCreateMutexStatic(&control_); }
        void lock() { (void)xSemaphoreTake(handle_, portMAX_DELAY); }
        void unlock() { (void)xSemaphoreGive(handle_); }

    private:
        StaticSemaphore_t control_{};
        SemaphoreHandle_t handle_{};
#else
        void lock() { mutex_.lock(); }
        void unlock() { mutex_.unlock(); }

    private:
        std::mutex mutex_{};
#endif
    };

    class Guard {
    public:
        explicit Guard(ManagerLock& lock) : lock_(lock) { lock_.lock(); }
        ~Guard() { lock_.unlock(); }

    private:
        ManagerLock& lock_;
    };

    void push_extra(BleManagerDecision decision);
    std::optional<BleManagerDecision> pop_extra();
    static BleManagerDecision translate(BleSetupAction action);

    BleHistoryPersistence& persistence_;
    mutable ManagerLock lock_{};
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
