#pragma once

#include "hid_parser/report_descriptor.hpp"
#include "source_arbiter.hpp"

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "usb/hid_host.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

class UsbMouseHost {
public:
    using PriorityCallback = void (*)(void* context);

    explicit UsbMouseHost(SourceArbiter& arbiter) : arbiter_(arbiter) {}

    esp_err_t start();
    void set_priority_callbacks(PriorityCallback on_active,
                                PriorityCallback on_inactive,
                                void* context) {
        on_active_ = on_active;
        on_inactive_ = on_inactive;
        priority_context_ = context;
    }

    [[nodiscard]] uint32_t dropped_reports() const {
        return dropped_reports_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] uint32_t rejected_interfaces() const {
        return rejected_interfaces_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] uint32_t transfer_errors() const {
        return transfer_errors_.load(std::memory_order_relaxed);
    }

private:
    enum class EventType : uint8_t {
        Connected,
        InputReport,
        Disconnected,
        TransferError,
    };

    static constexpr size_t kMaxReportBytes = 64;
    static constexpr size_t kQueueDepth = 16;
    static constexpr size_t kUsbTaskStackWords = 4096 / sizeof(StackType_t);
    static constexpr size_t kServiceTaskStackWords = 6144 / sizeof(StackType_t);

    struct Event {
        EventType type{};
        hid_host_device_handle_t handle{};
        uint64_t timestamp_us{};
        uint8_t length{};
        std::array<uint8_t, kMaxReportBytes> data{};
    };

    static void driver_event_callback(hid_host_device_handle_t handle,
                                      hid_host_driver_event_t event, void* arg);
    static void interface_event_callback(hid_host_device_handle_t handle,
                                         hid_host_interface_event_t event,
                                         void* arg);
    static void usb_task_entry(void* arg);
    static void service_task_entry(void* arg);

    void usb_task();
    void service_task();
    void handle_connected(hid_host_device_handle_t handle);
    void handle_disconnected(hid_host_device_handle_t handle, uint64_t now_us);
    void handle_input(const Event& event);
    bool enqueue(const Event& event, bool critical);

    SourceArbiter& arbiter_;
    QueueHandle_t queue_{};
    StaticQueue_t queue_control_{};
    std::array<uint8_t, kQueueDepth * sizeof(Event)> queue_storage_{};
    StaticTask_t usb_task_control_{};
    std::array<StackType_t, kUsbTaskStackWords> usb_task_stack_{};
    StaticTask_t service_task_control_{};
    std::array<StackType_t, kServiceTaskStackWords> service_task_stack_{};
    hid_host_device_handle_t active_handle_{};
    hid_parser::MouseLayout active_layout_{};
    std::atomic<uint32_t> dropped_reports_{};
    std::atomic<uint32_t> rejected_interfaces_{};
    std::atomic<uint32_t> transfer_errors_{};
    PriorityCallback on_active_{};
    PriorityCallback on_inactive_{};
    void* priority_context_{};
    bool priority_active_{};
};
