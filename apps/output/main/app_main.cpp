#include "esp_log.h"

#include "spi_packet_consumer.hpp"
#include "usb_mouse_device.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <array>

#if CONFIG_FREERTOS_UNICORE
#error "The real-time mouse pipeline requires both ESP32-S3 cores"
#endif

namespace {
struct DiagnosticsContext {
    SpiPacketConsumer* spi;
    UsbMouseDevice* usb;
};

StaticTask_t diagnostics_control;
std::array<StackType_t, 3072 / sizeof(StackType_t)> diagnostics_stack;

const char* source_name(mouse_bridge::InputSource source) {
    switch (source) {
    case mouse_bridge::InputSource::Usb:
        return "usb";
    case mouse_bridge::InputSource::Ble:
        return "ble";
    default:
        return "none";
    }
}

void on_packet(void* context, const mouse_bridge::MouseReport& report,
               uint32_t sequence, uint64_t rx_us) {
    static_cast<UsbMouseDevice*>(context)->on_packet(report, sequence, rx_us);
}

void diagnostics_task(void* arg) {
    auto& context = *static_cast<DiagnosticsContext*>(arg);
    ESP_LOGI("task_affinity", "diagnostics core=%d", xPortGetCoreID());
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        const auto link = context.spi->counters();
        ESP_LOGI(
            "MB_DIAG",
            "board=output source=%s usb_interval_us=%u ble_interval_units=0 "
            "spi_packets=%lu sequence_gaps=%lu crc_failures=%lu overruns=%lu "
            "timeouts=%lu max_link_latency_us=%lu usb_resumes=%lu",
            source_name(context.usb->active_source()),
            static_cast<unsigned>(context.usb->output_interval_us()),
            static_cast<unsigned long>(link.packets),
            static_cast<unsigned long>(link.sequence_gaps),
            static_cast<unsigned long>(link.crc_failures),
            static_cast<unsigned long>(link.queue_overruns),
            static_cast<unsigned long>(context.usb->timeout_count()),
            static_cast<unsigned long>(context.usb->max_stage_latency_us()),
            static_cast<unsigned long>(context.usb->resume_count()));
    }
}
}

extern "C" void app_main() {
    static UsbMouseDevice usb_mouse;
    static SpiPacketConsumer spi_consumer(on_packet, &usb_mouse);
    static DiagnosticsContext diagnostics{&spi_consumer, &usb_mouse};
    ESP_ERROR_CHECK(usb_mouse.start());
    ESP_ERROR_CHECK(spi_consumer.start());
    ESP_ERROR_CHECK(xTaskCreateStaticPinnedToCore(
                        diagnostics_task, "diagnostics",
                        diagnostics_stack.size(), &diagnostics, 1,
                        diagnostics_stack.data(), &diagnostics_control, 0) !=
                        nullptr
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);
    ESP_LOGI("bridge_output", "Adaptive 125-1000 Hz USB HID mouse bridge started");
}

