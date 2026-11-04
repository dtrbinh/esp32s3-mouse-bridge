#include "ble_hogp_client.hpp"
#include "ble_device_manager.hpp"
#include "ble_device_store.hpp"
#include "ble_only_mode.hpp"
#include "boot_control.hpp"
#include "setup_portal.hpp"
#include "source_arbiter.hpp"
#include "status_led.hpp"
#include "usb_mouse_device.hpp"

#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include <array>
#include <cstdint>
#include <optional>

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
    BleDeviceManager* manager;
};

struct SetupContext {
    BleDeviceManager* manager;
    SetupPortal* portal;
    BleHogpClient* ble;
};

StaticTask_t pipeline_control;
std::array<StackType_t, 6144 / sizeof(StackType_t)> pipeline_stack;
TaskHandle_t pipeline_handle;
StaticTask_t diagnostics_control;
std::array<StackType_t, 3072 / sizeof(StackType_t)> diagnostics_stack;
StaticTask_t setup_control;
std::array<StackType_t, 4096 / sizeof(StackType_t)> setup_stack;

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

const char* setup_phase_name(BleSetupPhase phase) {
    switch (phase) {
    case BleSetupPhase::Idle: return "idle";
    case BleSetupPhase::ScanningSelected: return "scanning";
    case BleSetupPhase::Portal: return "portal";
    case BleSetupPhase::SwitchDisconnect: return "switching";
    case BleSetupPhase::Connecting: return "connecting";
    case BleSetupPhase::Connected: return "connected";
    case BleSetupPhase::SuccessDelay: return "success_delay";
    case BleSetupPhase::Failed: return "failed";
    }
    return "unknown";
}

esp_err_t initialize_nvs() {
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES ||
        result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        result = nvs_flash_erase();
        if (result == ESP_OK) result = nvs_flash_init();
    }
    return result;
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
        const uint32_t now_ms =
            static_cast<uint32_t>(esp_timer_get_time() / 1'000);
        const auto portal = context.manager->status(now_ms);
        ESP_LOGI(
            "MB_DIAG",
            "board=ble_only source=%s ble_connections=%lu "
            "ble_interval_units=%u profile=%u mods=%s usb_interval_us=%u "
            "timeouts=%lu usb_resumes=%lu portal=%s saved=%u nearby=%u "
            "attempt=%s",
            source_name(context.arbiter->active_source()),
            static_cast<unsigned long>(context.ble->connections()),
            static_cast<unsigned>(context.ble->negotiated_interval_units()),
            static_cast<unsigned>(context.mode->selected_profile()),
            context.mode->mods_enabled() ? "on" : "off",
            static_cast<unsigned>(context.usb->output_interval_us()),
            static_cast<unsigned long>(context.usb->timeout_count()),
            static_cast<unsigned long>(context.usb->resume_count()),
            portal.setup.portal_active ? "on" : "off",
            static_cast<unsigned>(portal.saved_count),
            static_cast<unsigned>(portal.nearby_count),
            setup_phase_name(portal.setup.phase));
    }
}

void setup_task(void* arg) {
    auto& context = *static_cast<SetupContext*>(arg);
    ESP_LOGI("ble_only_setup", "core=%d", xPortGetCoreID());
    std::optional<BleManagerDecision> pending;
    while (true) {
        const uint32_t now_ms =
            static_cast<uint32_t>(esp_timer_get_time() / 1'000);
        if (!pending.has_value()) pending = context.manager->service(now_ms);
        if (pending.has_value()) {
            bool complete = true;
            switch (pending->kind) {
            case BleManagerDecisionKind::StartPortal: {
                const esp_err_t result = context.portal->start();
                if (result == ESP_OK) {
                    complete = context.ble->enqueue_control(
                        {BleClientCommandKind::StartScan, {}});
                    if (complete) context.manager->set_portal_active(true);
                } else {
                    complete = false;
                    ESP_LOGE("ble_only_setup", "Portal unavailable: %s",
                             esp_err_to_name(result));
                }
                break;
            }
            case BleManagerDecisionKind::StopPortal: {
                const esp_err_t result = context.portal->stop();
                complete = result == ESP_OK;
                if (complete) {
                    complete = context.ble->enqueue_control(
                        {BleClientCommandKind::StopScan, {}});
                }
                if (complete) {
                    context.manager->set_portal_active(false);
                } else if (result != ESP_OK) {
                    ESP_LOGE("ble_only_setup", "Portal shutdown failed: %s",
                             esp_err_to_name(result));
                }
                break;
            }
            case BleManagerDecisionKind::Connect:
                complete = pending->peer.has_value() &&
                           context.ble->enqueue_control(
                               {BleClientCommandKind::Connect,
                                *pending->peer});
                break;
            case BleManagerDecisionKind::Disconnect:
                complete = context.ble->enqueue_control(
                    {BleClientCommandKind::Disconnect,
                     pending->peer.value_or(BlePeerAddress{})});
                break;
            case BleManagerDecisionKind::Forget:
                complete = pending->peer.has_value() &&
                           context.ble->enqueue_control(
                               {BleClientCommandKind::Forget,
                                *pending->peer});
                break;
            }
            if (complete) pending.reset();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
}  // namespace

extern "C" void app_main() {
    static BleDeviceStore device_store;
    static BleDeviceManager device_manager(device_store);
    static SetupPortal setup_portal(device_manager);
    static SourceArbiter arbiter;
    static BleOnlyMode mode(CONFIG_BLE_ONLY_DEFAULT_PROFILE - 1, true);
    static UsbMouseDevice usb_mouse;
    static BleHogpClient ble_mouse(arbiter);
    static BootControl boot_control(device_manager, mode);
    static StatusLed status_led(ble_mouse, mode, device_manager);
    static PipelineContext pipeline{&arbiter, &mode, &usb_mouse};
    static DiagnosticsContext diagnostics{&arbiter, &ble_mouse, &mode,
                                          &usb_mouse, &device_manager};
    static SetupContext setup{&device_manager, &setup_portal, &ble_mouse};

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

    // Follow Espressif's coexistence startup order: Wi-Fi before NimBLE.
    // This lets the setup AP establish its RF time slice before BLE scanning.
    ESP_ERROR_CHECK(initialize_nvs());
    const uint32_t bootstrap_ms =
        static_cast<uint32_t>(esp_timer_get_time() / 1'000);
    device_manager.start(bootstrap_ms);
    bool portal_bootstrapped = false;
    if (const auto initial = device_manager.service(bootstrap_ms)) {
        ESP_ERROR_CHECK(initial->kind == BleManagerDecisionKind::StartPortal
                            ? ESP_OK
                            : ESP_ERR_INVALID_STATE);
        ESP_ERROR_CHECK(setup_portal.start());
        device_manager.set_portal_active(true);
        portal_bootstrapped = true;
    }

    ble_mouse.set_selection_delegate(&device_manager);
    ESP_ERROR_CHECK(ble_mouse.start());
    if (portal_bootstrapped) {
        ESP_ERROR_CHECK(ble_mouse.enqueue_control(
                            {BleClientCommandKind::StartScan, {}})
                            ? ESP_OK
                            : ESP_ERR_NO_MEM);
    }
    ESP_ERROR_CHECK(boot_control.start(pipeline_handle));
    ESP_ERROR_CHECK(xTaskCreateStaticPinnedToCore(
                        setup_task, "ble_only_setup", setup_stack.size(),
                        &setup, 2, setup_stack.data(), &setup_control,
                        kRadioCore) != nullptr
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);
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
