#include "spi_packet_producer.hpp"

#include "mouse_bridge/wire_protocol.hpp"

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"

#include <algorithm>

namespace {
DMA_ATTR mouse_bridge::WirePacket tx_packet;
}

esp_err_t SpiPacketProducer::start() {
    gpio_config_t ready{};
    ready.pin_bit_mask = 1ULL << spi_link::kReadyGpio;
    ready.mode = GPIO_MODE_OUTPUT;
    esp_err_t result = gpio_config(&ready);
    if (result != ESP_OK) return result;
    gpio_set_level(static_cast<gpio_num_t>(spi_link::kReadyGpio), 0);

    spi_bus_config_t bus{};
    bus.mosi_io_num = spi_link::kMosiGpio;
    bus.miso_io_num = spi_link::kMisoGpio;
    bus.sclk_io_num = spi_link::kSclkGpio;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = mouse_bridge::kWirePacketSize;
    result = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (result != ESP_OK) return result;

    spi_device_interface_config_t device{};
    device.clock_speed_hz = spi_link::kClockHz;
    device.mode = 0;
    device.spics_io_num = spi_link::kChipSelectGpio;
    device.queue_size = spi_link::kDmaQueueDepth;
    result = spi_bus_add_device(SPI2_HOST, &device, &device_);
    if (result != ESP_OK) return result;

    task_handle_ = xTaskCreateStatic(task_entry, "spi_producer",
                                     task_stack_.size(), this, 7,
                                     task_stack_.data(), &task_control_);
    return task_handle_ != nullptr ? ESP_OK : ESP_ERR_NO_MEM;
}

void SpiPacketProducer::submit(const mouse_bridge::MouseReport& report) {
    portENTER_CRITICAL(&mailbox_lock_);
    const bool replaced = mailbox_.push(report);
    portEXIT_CRITICAL(&mailbox_lock_);
    if (replaced) queue_overruns_.fetch_add(1, std::memory_order_relaxed);
    if (task_handle_ != nullptr) xTaskNotifyGive(task_handle_);
}

void SpiPacketProducer::task_entry(void* arg) {
    static_cast<SpiPacketProducer*>(arg)->task();
}

void SpiPacketProducer::task() {
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        while (true) {
            mouse_bridge::MouseReport report{};
            portENTER_CRITICAL(&mailbox_lock_);
            const bool available = mailbox_.take(report);
            portEXIT_CRITICAL(&mailbox_lock_);
            if (!available) break;

            tx_packet = mouse_bridge::encode_packet(report, next_sequence_++);
            spi_transaction_t transaction{};
            transaction.length = mouse_bridge::kWirePacketSize * 8;
            transaction.tx_buffer = tx_packet.data();
            if (spi_device_transmit(device_, &transaction) != ESP_OK) {
                transfer_errors_.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            packets_.fetch_add(1, std::memory_order_relaxed);
            const uint64_t now_us = static_cast<uint64_t>(esp_timer_get_time());
            const uint32_t latency = now_us >= report.timestamp_us
                                         ? static_cast<uint32_t>(std::min<uint64_t>(
                                               now_us - report.timestamp_us,
                                               UINT32_MAX))
                                         : 0;
            uint32_t previous = max_link_latency_us_.load();
            while (latency > previous &&
                   !max_link_latency_us_.compare_exchange_weak(previous,
                                                               latency)) {}

            gpio_set_level(static_cast<gpio_num_t>(spi_link::kReadyGpio), 1);
            esp_rom_delay_us(1);
            gpio_set_level(static_cast<gpio_num_t>(spi_link::kReadyGpio), 0);
        }
    }
}

spi_link::CounterSnapshot SpiPacketProducer::counters() const {
    return {.packets = packets_.load(),
            .queue_overruns = queue_overruns_.load(),
            .transfer_errors = transfer_errors_.load(),
            .max_link_latency_us = max_link_latency_us_.load()};
}
