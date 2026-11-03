#pragma once

#include "mouse_bridge/mouse_report.hpp"
#include "spi_link/latest_report_mailbox.hpp"
#include "spi_link/link_config.hpp"

#include "driver/spi_master.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <array>
#include <atomic>
#include <cstdint>

class SpiPacketProducer {
public:
    esp_err_t start();
    void submit(const mouse_bridge::MouseReport& report);
    [[nodiscard]] spi_link::CounterSnapshot counters() const;

private:
    static constexpr size_t kTaskStackWords = 4096 / sizeof(StackType_t);

    static void task_entry(void* arg);
    void task();
    bool transmit(const mouse_bridge::MouseReport& report);

    spi_device_handle_t device_{};
    TaskHandle_t task_handle_{};
    StaticTask_t task_control_{};
    std::array<StackType_t, kTaskStackWords> task_stack_{};
    portMUX_TYPE mailbox_lock_ = portMUX_INITIALIZER_UNLOCKED;
    spi_link::LatestReportMailbox mailbox_{};
    uint32_t next_sequence_{};
    std::atomic<uint32_t> packets_{};
    std::atomic<uint32_t> queue_overruns_{};
    std::atomic<uint32_t> transfer_errors_{};
    std::atomic<uint32_t> max_link_latency_us_{};
};
