#pragma once

#include "ble_peer.hpp"
#include "ble_selection_delegate.hpp"
#include "hid_parser/report_descriptor.hpp"
#include "source_arbiter.hpp"

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

struct os_mbuf;

class BleHogpClient {
public:
    explicit BleHogpClient(SourceArbiter& arbiter) : arbiter_(arbiter) {}

    esp_err_t start();
    void set_selection_delegate(BleSelectionDelegate* delegate) {
        selection_delegate_ = delegate;
    }
    bool enqueue_control(BleClientCommand command);
    void suspend_forwarding();
    void resume_or_scan();
    void clear_bond();
    [[nodiscard]] uint16_t negotiated_interval_units() const {
        return negotiated_interval_units_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] uint32_t connections() const {
        return connections_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] bool connected() const {
        return connected_.load(std::memory_order_acquire);
    }

private:
    static constexpr size_t kCommandCapacity = 8;
    static constexpr size_t kWorkerStackWords = 6144 / sizeof(StackType_t);
    static constexpr size_t kMaxCharacteristics = 32;
    static constexpr size_t kMaxNamedPeers = 32;
    static constexpr size_t kMaxReportMapBytes = 512;

    struct Characteristic {
        uint16_t definition_handle{};
        uint16_t value_handle{};
        uint16_t uuid16{};
        uint8_t properties{};
        uint16_t cccd_handle{};
        uint16_t report_reference_handle{};
        uint8_t report_id{};
        bool subscribed{};
    };

    struct NamedPeer {
        BlePeerAddress address{};
        std::array<char, 33> name{};
        bool used{};
    };

    static void host_task_entry(void* arg);
    static void worker_task_entry(void* arg);
    static void sync_callback();
    static int gap_callback(ble_gap_event* event, void* arg);
    static int service_callback(uint16_t connection_handle,
                                const ble_gatt_error* error,
                                const ble_gatt_svc* service, void* arg);
    static int characteristic_callback(uint16_t connection_handle,
                                       const ble_gatt_error* error,
                                       const ble_gatt_chr* characteristic,
                                       void* arg);
    static int report_map_callback(uint16_t connection_handle,
                                   const ble_gatt_error* error,
                                   ble_gatt_attr* attribute, void* arg);
    static int descriptor_callback(uint16_t connection_handle,
                                   const ble_gatt_error* error,
                                   uint16_t characteristic_value_handle,
                                   const ble_gatt_dsc* descriptor, void* arg);
    static int report_reference_callback(uint16_t connection_handle,
                                         const ble_gatt_error* error,
                                         ble_gatt_attr* attribute, void* arg);
    static int subscription_callback(uint16_t connection_handle,
                                     const ble_gatt_error* error,
                                     ble_gatt_attr* attribute, void* arg);

    void worker_task();
    void on_sync();
    int on_gap_event(ble_gap_event& event);
    void begin_connect(BlePeerAddress peer);
    void begin_service_discovery(const char* security_result);
    void begin_characteristic_discovery();
    void finish_report_map();
    void start_next_subscription();
    void write_active_cccd();
    void finish_validation();
    void fail_attempt(BleSelectionFailure failure, const char* step,
                      int status);
    void handle_disconnect(int reason);
    void request_low_latency_interval();
    void decode_input(uint16_t value_handle, const os_mbuf* data);
    void forget_peer(BlePeerAddress peer);
    [[nodiscard]] std::optional<BlePeerAddress> connected_identity() const;
    void remember_name(BlePeerAddress peer, std::string_view name);
    [[nodiscard]] std::string_view name_for(BlePeerAddress peer) const;
    [[nodiscard]] uint16_t characteristic_end(size_t index) const;
    void reset_discovery();

    static BleHogpClient* instance_;
    SourceArbiter& arbiter_;
    BleSelectionDelegate* selection_delegate_{};
    TaskHandle_t worker_handle_{};
    StaticTask_t worker_control_{};
    std::array<StackType_t, kWorkerStackWords> worker_stack_{};
    QueueHandle_t command_queue_{};
    StaticQueue_t command_queue_control_{};
    std::array<uint8_t, kCommandCapacity * sizeof(BleClientCommand)>
        command_queue_storage_{};
    std::array<Characteristic, kMaxCharacteristics> characteristics_{};
    size_t characteristic_count_{};
    size_t subscribe_cursor_{};
    size_t active_characteristic_{kMaxCharacteristics};
    std::array<uint8_t, kMaxReportMapBytes> report_map_{};
    size_t report_map_length_{};
    hid_parser::MouseLayout mouse_layout_{};
    bool mouse_layout_valid_{};
    uint16_t service_start_{};
    uint16_t service_end_{};
    uint16_t report_map_handle_{};
    uint16_t connection_handle_{BLE_HS_CONN_HANDLE_NONE};
    bool discovery_started_{};
    BlePeerAddress candidate_{};
    std::array<char, 33> candidate_name_{};
    std::array<NamedPeer, kMaxNamedPeers> named_peers_{};
    size_t named_peer_cursor_{};
    BlePeerAddress pending_forget_{};
    std::atomic<bool> pending_forget_valid_{};
    std::atomic<bool> forwarding_{true};
    std::atomic<bool> synced_{};
    std::atomic<bool> connecting_{};
    std::atomic<bool> connected_{};
    std::atomic<bool> portal_scanning_{};
    std::atomic<bool> attempt_failed_{};
    std::atomic<uint16_t> negotiated_interval_units_{};
    std::atomic<uint32_t> connections_{};
};
