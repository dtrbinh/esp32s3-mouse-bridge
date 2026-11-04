#include "spi_packet_consumer.hpp"

#include "mouse_bridge/wire_protocol.hpp"

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <span>

namespace {
DMA_ATTR uint8_t rx_buffers[spi_link::kDmaQueueDepth]
                           [mouse_bridge::kWirePacketSize]{};
constexpr char kTag[] = "spi_consumer";
}

esp_err_t SpiPacketConsumer::start() {
    spi_bus_config_t bus{};
    bus.mosi_io_num = spi_link::kMosiGpio;
    bus.miso_io_num = spi_link::kMisoGpio;
    bus.sclk_io_num = spi_link::kSclkGpio;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = mouse_bridge::kWirePacketSize;

    spi_slave_interface_config_t slave{};
    slave.spics_io_num = spi_link::kChipSelectGpio;
    slave.mode = 0;
    slave.queue_size = spi_link::kDmaQueueDepth;
    esp_err_t result =
        spi_slave_initialize(SPI2_HOST, &bus, &slave, SPI_DMA_CH_AUTO);
    if (result != ESP_OK) return result;

    for (size_t i = 0; i < transactions_.size(); ++i) {
        transactions_[i].length = mouse_bridge::kWirePacketSize * 8;
        transactions_[i].rx_buffer = rx_buffers[i];
        result = spi_slave_queue_trans(SPI2_HOST, &transactions_[i], 0);
        if (result != ESP_OK) return result;
    }

    task_handle_ = xTaskCreateStaticPinnedToCore(
        task_entry, "spi_consumer", task_stack_.size(), this, 8,
        task_stack_.data(), &task_control_, 0);
    if (task_handle_ == nullptr) return ESP_ERR_NO_MEM;

    gpio_config_t ready{};
    ready.pin_bit_mask = 1ULL << spi_link::kReadyGpio;
    ready.mode = GPIO_MODE_INPUT;
    ready.pull_down_en = GPIO_PULLDOWN_ENABLE;
    ready.intr_type = GPIO_INTR_POSEDGE;
    result = gpio_config(&ready);
    if (result != ESP_OK) return result;
    result = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) return result;
    return gpio_isr_handler_add(static_cast<gpio_num_t>(spi_link::kReadyGpio),
                                ready_isr, this);
}

void IRAM_ATTR SpiPacketConsumer::ready_isr(void* arg) {
    auto* self = static_cast<SpiPacketConsumer*>(arg);
    BaseType_t wake = pdFALSE;
    vTaskNotifyGiveFromISR(self->task_handle_, &wake);
    portYIELD_FROM_ISR(wake);
}

void SpiPacketConsumer::task_entry(void* arg) {
    static_cast<SpiPacketConsumer*>(arg)->task();
}

void SpiPacketConsumer::task() {
    ESP_LOGI(kTag, "core=%d", xPortGetCoreID());
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        spi_slave_transaction_t* completed = nullptr;
        while (spi_slave_get_trans_result(SPI2_HOST, &completed, 0) == ESP_OK) {
            process(*completed, static_cast<uint64_t>(esp_timer_get_time()));
            completed->trans_len = 0;
            if (spi_slave_queue_trans(SPI2_HOST, completed, 0) != ESP_OK) {
                queue_overruns_.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
}

void SpiPacketConsumer::process(spi_slave_transaction_t& transaction,
                                uint64_t rx_us) {
    if (transaction.trans_len != mouse_bridge::kWirePacketSize * 8 ||
        transaction.rx_buffer == nullptr) {
        crc_failures_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const auto* bytes = static_cast<const uint8_t*>(transaction.rx_buffer);
    const auto decoded = mouse_bridge::decode_packet(
        std::span<const uint8_t>(bytes, mouse_bridge::kWirePacketSize));
    if (decoded.status != mouse_bridge::DecodeStatus::Ok) {
        if (decoded.status == mouse_bridge::DecodeStatus::BadCrc) {
            crc_failures_.fetch_add(1, std::memory_order_relaxed);
        }
        return;
    }
    if (have_sequence_) {
        if (!mouse_bridge::sequence_is_newer(decoded.sequence,
                                             last_sequence_)) {
            return;
        }
        sequence_gaps_.fetch_add(decoded.sequence - last_sequence_ - 1,
                                 std::memory_order_relaxed);
    }
    have_sequence_ = true;
    last_sequence_ = decoded.sequence;
    packets_.fetch_add(1, std::memory_order_relaxed);
    if (callback_ != nullptr) {
        callback_(context_, decoded.report, decoded.sequence, rx_us);
    }
}

spi_link::CounterSnapshot SpiPacketConsumer::counters() const {
    return {.packets = packets_.load(),
            .sequence_gaps = sequence_gaps_.load(),
            .crc_failures = crc_failures_.load(),
            .queue_overruns = queue_overruns_.load()};
}
