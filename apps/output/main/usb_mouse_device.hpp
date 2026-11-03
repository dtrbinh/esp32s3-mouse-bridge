#pragma once

#include "mouse_bridge/mouse_report.hpp"
#include "output_state.hpp"

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

private:
    static constexpr size_t kTaskStackWords = 4096 / sizeof(StackType_t);
    static constexpr unsigned kBypassDebounceSamples = 5;

    static void task_entry(void* arg);
    void task();
    void send(const OutputFrame& frame);

    OutputState state_{};
    portMUX_TYPE state_lock_ = portMUX_INITIALIZER_UNLOCKED;
    StaticTask_t task_control_{};
    std::array<StackType_t, kTaskStackWords> task_stack_{};
    bool bypass_{};
    std::atomic<bool> suspended_{};
};
