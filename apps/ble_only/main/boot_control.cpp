#include "boot_control.hpp"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace {
constexpr gpio_num_t kBootPin = GPIO_NUM_0;
constexpr TickType_t kSamplePeriod = pdMS_TO_TICKS(20);
constexpr char kTag[] = "ble_only_boot";
}

esp_err_t BootControl::start(TaskHandle_t pipeline_handle) {
    pipeline_handle_ = pipeline_handle;
    gpio_config_t config{};
    config.pin_bit_mask = 1ULL << kBootPin;
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en = GPIO_PULLUP_ENABLE;
    esp_err_t result = gpio_config(&config);
    if (result != ESP_OK) return result;
    return xTaskCreateStaticPinnedToCore(
               task_entry, "ble_only_boot", task_stack_.size(), this, 2,
               task_stack_.data(), &task_control_, 0) != nullptr
               ? ESP_OK
               : ESP_ERR_NO_MEM;
}

void BootControl::task_entry(void* arg) {
    static_cast<BootControl*>(arg)->task();
}

void BootControl::task() {
    ESP_LOGI(kTag, "core=%d", xPortGetCoreID());
    while (true) {
        const bool pressed = gpio_get_level(kBootPin) == 0;
        const uint32_t now_ms =
            static_cast<uint32_t>(esp_timer_get_time() / 1'000);
        const BootAction action = classifier_.update(pressed, now_ms);
        if (action == BootAction::OpenPortal) {
            ESP_LOGI(kTag, "hold=5s action=open_portal");
            manager_.request_portal();
        } else if (action == BootAction::NextProfile ||
                   action == BootAction::ToggleMods) {
            if (mode_.enqueue(action)) {
                ESP_LOGI(kTag, "action=%s queued",
                         action == BootAction::NextProfile
                             ? "next_profile"
                             : "toggle_mods");
                if (pipeline_handle_ != nullptr) {
                    xTaskNotifyGive(pipeline_handle_);
                }
            } else {
                ESP_LOGE(kTag, "action queue full");
            }
        }
        vTaskDelay(kSamplePeriod);
    }
}
