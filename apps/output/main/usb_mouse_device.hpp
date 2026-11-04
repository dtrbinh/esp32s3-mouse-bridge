#pragma once

#include "mouse_bridge/mouse_report.hpp"
#include "output_state.hpp"
#include "report_scheduler.hpp"

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <array>
#include <atomic>
#include <cstdint>

class UsbMouseDevice {
public:
    esp_err_t start();
    void on_packet(const mouse_bridge::MouseReport& report, uint32_t sequence,
                   uint64_t rx_us);
    void on_suspend();
    void on_resume();
    [[nodiscard]] uint32_t timeout_count();
    [[nodiscard]] uint16_t output_interval_us();
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

    static void task_entry(void* arg);
    void task();
    void send(const OutputFrame& frame);

    OutputState state_{};
    ReportScheduler scheduler_{};
    portMUX_TYPE state_lock_ = portMUX_INITIALIZER_UNLOCKED;
    StaticTask_t task_control_{};
    std::array<StackType_t, kTaskStackWords> task_stack_{};
    bool bypass_{};
    std::atomic<bool> suspended_{};
    std::atomic<uint32_t> resume_count_{};
    std::atomic<uint8_t> active_source_{};
};
