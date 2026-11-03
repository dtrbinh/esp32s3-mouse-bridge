#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "source_arbiter.hpp"
#include "usb_mouse_host.hpp"
#include "ble_hogp_client.hpp"
#include "bond_control.hpp"
#include "spi_packet_producer.hpp"
#include "profile_engine/profile_engine.hpp"

#include <array>

namespace {
constexpr uint16_t kProfile1Button = 1U << 3;
constexpr uint16_t kProfile2Button = 1U << 4;

struct DiagnosticsContext {
    SourceArbiter* arbiter;
    UsbMouseHost* usb;
    BleHogpClient* ble;
    SpiPacketProducer* spi;
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

void diagnostics_task(void* arg) {
    auto& context = *static_cast<DiagnosticsContext*>(arg);
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        const auto link = context.spi->counters();
        ESP_LOGI(
            "MB_DIAG",
            "board=input source=%s usb_interval_us=%lu ble_interval_units=%u "
            "spi_packets=%lu sequence_gaps=0 crc_failures=0 overruns=%lu "
            "timeouts=0 max_link_latency_us=%lu usb_drops=%lu "
            "usb_connections=%lu usb_disconnects=%lu ble_connections=%lu "
            "arbiter_drops=%lu",
            source_name(context.arbiter->active_source()),
            static_cast<unsigned long>(context.usb->observed_interval_us()),
            static_cast<unsigned>(context.ble->negotiated_interval_units()),
            static_cast<unsigned long>(link.packets),
            static_cast<unsigned long>(link.queue_overruns),
            static_cast<unsigned long>(link.max_link_latency_us),
            static_cast<unsigned long>(context.usb->dropped_reports()),
            static_cast<unsigned long>(context.usb->connections()),
            static_cast<unsigned long>(context.usb->disconnects()),
            static_cast<unsigned long>(context.ble->connections()),
            static_cast<unsigned long>(context.arbiter->dropped_reports()));
    }
}
}  // namespace

extern "C" void app_main() {
    static SourceArbiter arbiter;
    static UsbMouseHost usb_mouse(arbiter);
    static BleHogpClient ble_mouse(arbiter);
    static BondControl bond_control(ble_mouse);
    static SpiPacketProducer spi_producer;
    static profile_engine::ProfileEngine profiles;
    static DiagnosticsContext diagnostics{&arbiter, &usb_mouse, &ble_mouse,
                                          &spi_producer};

    ESP_ERROR_CHECK(profiles.select(CONFIG_MOUSE_BRIDGE_DEFAULT_PROFILE - 1)
                        ? ESP_OK
                        : ESP_ERR_INVALID_ARG);
    profiles.set_enabled(CONFIG_MOUSE_BRIDGE_PROFILE_ENABLED);

    usb_mouse.set_priority_callbacks(
        [](void* context) {
            static_cast<BleHogpClient*>(context)->suspend_forwarding();
        },
        [](void* context) {
            static_cast<BleHogpClient*>(context)->resume_or_scan();
        },
        &ble_mouse);

    ESP_ERROR_CHECK(ble_mouse.start());
    ESP_ERROR_CHECK(bond_control.start());
    ESP_ERROR_CHECK(usb_mouse.start());
    ESP_ERROR_CHECK(spi_producer.start());
    if (esp_task_wdt_status(nullptr) == ESP_ERR_NOT_FOUND) {
        ESP_ERROR_CHECK(esp_task_wdt_add(nullptr));
    }
    ESP_ERROR_CHECK(xTaskCreateStatic(
                        diagnostics_task, "diagnostics",
                        diagnostics_stack.size(), &diagnostics, 1,
                        diagnostics_stack.data(), &diagnostics_control) != nullptr
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);
    ESP_LOGI("bridge_input", "USB and BLE mouse inputs started");

    TickType_t wake = xTaskGetTickCount();
    uint16_t previous_buttons = 0;
    while (true) {
        uint64_t now_us = static_cast<uint64_t>(esp_timer_get_time());
        while (const auto report = arbiter.poll(now_us)) {
            if (mouse_bridge::has_flag(report->flags,
                                       mouse_bridge::ReportFlags::Boundary)) {
                profiles.reset();
                previous_buttons = 0;
            }
            const uint16_t pressed = report->buttons & ~previous_buttons;
            if ((pressed & kProfile1Button) != 0) {
                (void)profiles.select(0);
            } else if ((pressed & kProfile2Button) != 0) {
                (void)profiles.select(1);
            }
            previous_buttons = report->buttons;
            spi_producer.submit(profiles.process(*report, now_us));
            now_us = static_cast<uint64_t>(esp_timer_get_time());
        }
        ESP_ERROR_CHECK(esp_task_wdt_reset());
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(1));
    }
}

