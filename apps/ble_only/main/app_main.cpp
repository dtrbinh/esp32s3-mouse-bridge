#include "ble_hogp_client.hpp"
#include "ble_only_mode.hpp"
#include "boot_control.hpp"
#include "source_arbiter.hpp"
#include "status_led.hpp"
#include "usb_mouse_device.hpp"

#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <array>
#include <cstdint>

#if CONFIG_FREERTOS_UNICORE
#error "The BLE-only mouse pipeline requires both ESP32-S3 cores"
#endif

namespace {
constexpr BaseType_t kRadioCore = 0;
constexpr BaseType_t kRealtimeCore = 1;

struct PipelineContext {
    SourceArbiter* arbiter;
    BleOnlyMode* mode;
    UsbMouseDevice* usb;
};

struct DiagnosticsContext {
    SourceArbiter* arbiter;
    BleHogpClient* ble;
    BleOnlyMode* mode;
    UsbMouseDevice* usb;
};

StaticTask_t pipeline_control;
std::array<StackType_t, 6144 / sizeof(StackType_t)> pipeline_stack;
TaskHandle_t pipeline_handle;
StaticTask_t diagnostics_control;
std::array<StackType_t, 3072 / sizeof(StackType_t)> diagnostics_stack;

const char* source_name(mouse_bridge::InputSource source) {
    switch (source) {
    case mouse_bridge::InputSource::Ble:
        return "ble";
    case mouse_bridge::InputSource::Usb:
        return "usb";
    default:
        return "none";
    }
}

void wake_pipeline(void* context) {
    const TaskHandle_t handle = *static_cast<TaskHandle_t*>(context);
    if (handle != nullptr) xTaskNotifyGive(handle);
}

void pipeline_task(void* arg) {
    auto& context = *static_cast<PipelineContext*>(arg);
    ESP_LOGI("ble_only_pipeline", "core=%d", xPortGetCoreID());
    if (esp_task_wdt_status(nullptr) == ESP_ERR_NOT_FOUND) {
        ESP_ERROR_CHECK(esp_task_wdt_add(nullptr));
    }

    uint32_t sequence = 0;
    while (true) {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1));
        while (const auto action = context.mode->apply_next()) {
            if (*action == BootAction::NextProfile) {
                ESP_LOGI("ble_only_mode", "profile=%u mods=%s",
                         static_cast<unsigned>(
                             context.mode->selected_profile()),
                         context.mode->mods_enabled() ? "on" : "off");
            } else if (*action == BootAction::ToggleMods) {
                ESP_LOGI("ble_only_mode", "profile=%u mods=%s",
                         static_cast<unsigned>(
                             context.mode->selected_profile()),
                         context.mode->mods_enabled() ? "on" : "off");
            }
        }

        uint64_t now_us = static_cast<uint64_t>(esp_timer_get_time());
        while (const auto report = context.arbiter->poll(now_us)) {
            if (mouse_bridge::has_flag(report->flags,
                                       mouse_bridge::ReportFlags::Boundary)) {
                context.mode->reset();
            }
            context.usb->on_packet(context.mode->process(*report, now_us),
                                   sequence++, now_us);
            now_us = static_cast<uint64_t>(esp_timer_get_time());
        }
        ESP_ERROR_CHECK(esp_task_wdt_reset());
    }
}

void diagnostics_task(void* arg) {
    auto& context = *static_cast<DiagnosticsContext*>(arg);
    ESP_LOGI("ble_only_diag", "core=%d", xPortGetCoreID());
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(
            "MB_DIAG",
            "board=ble_only source=%s ble_connections=%lu "
            "ble_interval_units=%u profile=%u mods=%s usb_interval_us=%u "
            "timeouts=%lu usb_resumes=%lu",
            source_name(context.arbiter->active_source()),
            static_cast<unsigned long>(context.ble->connections()),
            static_cast<unsigned>(context.ble->negotiated_interval_units()),
            static_cast<unsigned>(context.mode->selected_profile()),
            context.mode->mods_enabled() ? "on" : "off",
            static_cast<unsigned>(context.usb->output_interval_us()),
            static_cast<unsigned long>(context.usb->timeout_count()),
            static_cast<unsigned long>(context.usb->resume_count()));
    }
}
}  // namespace

extern "C" void app_main() {
    static SourceArbiter arbiter;
    static BleOnlyMode mode(CONFIG_BLE_ONLY_DEFAULT_PROFILE - 1, true);
    static UsbMouseDevice usb_mouse;
    static BleHogpClient ble_mouse(arbiter);
    static BootControl boot_control(ble_mouse, mode);
    static StatusLed status_led(ble_mouse, mode);
    static PipelineContext pipeline{&arbiter, &mode, &usb_mouse};
    static DiagnosticsContext diagnostics{&arbiter, &ble_mouse, &mode,
                                          &usb_mouse};

    ESP_ERROR_CHECK(usb_mouse.start(false));
    const esp_err_t led_result = status_led.start();
    if (led_result != ESP_OK) {
        ESP_LOGE("ble_only", "Status LED disabled: %s",
                 esp_err_to_name(led_result));
    }
    pipeline_handle = xTaskCreateStaticPinnedToCore(
        pipeline_task, "ble_only_pipeline", pipeline_stack.size(), &pipeline,
        7, pipeline_stack.data(), &pipeline_control, kRealtimeCore);
    ESP_ERROR_CHECK(pipeline_handle != nullptr ? ESP_OK : ESP_ERR_NO_MEM);
    arbiter.set_wake_callback(wake_pipeline, &pipeline_handle);

    ESP_ERROR_CHECK(ble_mouse.start());
    ESP_ERROR_CHECK(boot_control.start(pipeline_handle));
    ESP_ERROR_CHECK(xTaskCreateStaticPinnedToCore(
                        diagnostics_task, "ble_only_diag",
                        diagnostics_stack.size(), &diagnostics, 1,
                        diagnostics_stack.data(), &diagnostics_control,
                        kRadioCore) != nullptr
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);
    ESP_LOGI("ble_only", "BLE mouse to USB HID started; profile=%u mods=on",
             static_cast<unsigned>(mode.selected_profile()));
}
