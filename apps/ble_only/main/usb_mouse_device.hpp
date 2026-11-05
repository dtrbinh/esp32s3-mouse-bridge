#pragma once

#include "mouse_bridge/mouse_report.hpp"
#include "output_state.hpp"
#include "report_scheduler.hpp"

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <array>
#include <atomic>
#include <cstdint>

class UsbMouseDevice {
public:
    esp_err_t start(bool enable_bypass_button = true);
    void on_packet(const mouse_bridge::MouseReport& report, uint32_t sequence,
                   uint64_t rx_us);
    void on_keyboard_report(const mouse_bridge::KeyboardReport& report);
    void on_suspend();
    void on_resume();
    [[nodiscard]] uint32_t timeout_count();
    [[nodiscard]] uint16_t output_interval_us();
    [[nodiscard]] uint32_t max_stage_latency_us() const {
        return max_stage_latency_us_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] uint32_t resume_count() const {
        return resume_count_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] mouse_bridge::InputSource active_source() const {
        return static_cast<mouse_bridge::InputSource>(
            active_source_.load(std::memory_order_relaxed));
    }

private:
    static constexpr size_t kTaskStackWords = 4096 / sizeof(StackType_t);
    static constexpr unsigned kBypassDebounceSamples = 5;
    static constexpr size_t kKeyboardQueueCapacity = 16;

    static void task_entry(void* arg);
    void task();
    void send(const OutputFrame& frame);
    void send_keyboard(const mouse_bridge::KeyboardReport& report);
    void notify_packet_available();

    OutputState state_{};
    ReportScheduler scheduler_{};
    portMUX_TYPE state_lock_ = portMUX_INITIALIZER_UNLOCKED;
    StaticTask_t task_control_{};
    std::array<StackType_t, kTaskStackWords> task_stack_{};
    TaskHandle_t task_handle_{};
    QueueHandle_t keyboard_queue_{};
    StaticQueue_t keyboard_queue_control_{};
    std::array<uint8_t, kKeyboardQueueCapacity *
                            sizeof(mouse_bridge::KeyboardReport)>
        keyboard_queue_storage_{};
    mouse_bridge::KeyboardReport last_keyboard_{};
    portMUX_TYPE keyboard_lock_ = portMUX_INITIALIZER_UNLOCKED;
    uint64_t pending_stage_rx_us_{};
    bool bypass_button_enabled_{true};
    bool bypass_{};
    std::atomic<bool> suspended_{};
    std::atomic<uint32_t> resume_count_{};
    std::atomic<uint8_t> active_source_{};
    std::atomic<uint32_t> max_stage_latency_us_{};
};
