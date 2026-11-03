#pragma once

#include "mouse_bridge/mouse_report.hpp"
#include "spi_link/link_config.hpp"

#include "driver/spi_slave.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <array>
#include <atomic>
#include <cstdint>

class SpiPacketConsumer {
public:
    using PacketCallback = void (*)(void* context,
                                    const mouse_bridge::MouseReport& report,
                                    uint32_t sequence, uint64_t rx_us);

    SpiPacketConsumer(PacketCallback callback, void* context)
        : callback_(callback), context_(context) {}

    esp_err_t start();
    [[nodiscard]] spi_link::CounterSnapshot counters() const;

private:
    static constexpr size_t kTaskStackWords = 4096 / sizeof(StackType_t);

    static void IRAM_ATTR ready_isr(void* arg);
    static void task_entry(void* arg);
    void task();
    void process(spi_slave_transaction_t& transaction, uint64_t rx_us);

    PacketCallback callback_{};
    void* context_{};
    TaskHandle_t task_handle_{};
    StaticTask_t task_control_{};
    std::array<StackType_t, kTaskStackWords> task_stack_{};
    std::array<spi_slave_transaction_t, spi_link::kDmaQueueDepth> transactions_{};
    bool have_sequence_{};
    uint32_t last_sequence_{};
    std::atomic<uint32_t> packets_{};
    std::atomic<uint32_t> sequence_gaps_{};
    std::atomic<uint32_t> crc_failures_{};
    std::atomic<uint32_t> queue_overruns_{};
};
