#pragma once

#include "hid_parser/report_descriptor.hpp"
#include "source_arbiter.hpp"

#include "esp_err.h"
#include "esp_hidh.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_gap.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

class BleHogpClient {
public:
    explicit BleHogpClient(SourceArbiter& arbiter) : arbiter_(arbiter) {}

    esp_err_t start();
    void suspend_forwarding();
    void resume_or_scan();
    void clear_bond();
    [[nodiscard]] uint16_t negotiated_interval_units() const {
        return negotiated_interval_units_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] uint32_t connections() const {
        return connections_.load(std::memory_order_relaxed);
    }

private:
    static constexpr size_t kMaxMaps = 4;
    static constexpr size_t kWorkerStackWords = 6144 / sizeof(StackType_t);

    static void host_task_entry(void* arg);
    static void worker_task_entry(void* arg);
    static void sync_callback();
    static int scan_callback(ble_gap_event* event, void* arg);
    static int gap_listener_callback(ble_gap_event* event, void* arg);
    static void hidh_callback(void* handler_arg, esp_event_base_t base,
                              int32_t event_id, void* event_data);

    void worker_task();
    void on_sync();
    void on_hidh_event(esp_hidh_event_t event,
                       const esp_hidh_event_data_t& data);
    bool compile_maps(esp_hidh_dev_t* device);
    void request_low_latency_interval(esp_hidh_dev_t* device);
    void decode_input(const esp_hidh_event_data_t& data);

    static BleHogpClient* instance_;
    SourceArbiter& arbiter_;
    TaskHandle_t worker_handle_{};
    StaticTask_t worker_control_{};
    std::array<StackType_t, kWorkerStackWords> worker_stack_{};
    std::array<hid_parser::MouseLayout, kMaxMaps> layouts_{};
    std::array<bool, kMaxMaps> layout_valid_{};
    std::array<uint8_t, 6> candidate_address_{};
    uint8_t candidate_address_type_{};
    std::atomic<esp_hidh_dev_t*> device_{};
    std::atomic<bool> forwarding_{true};
    std::atomic<bool> synced_{};
    std::atomic<bool> connecting_{};
    std::atomic<uint16_t> negotiated_interval_units_{};
    std::atomic<uint32_t> connections_{};
    ble_gap_event_listener gap_listener_{};
};
