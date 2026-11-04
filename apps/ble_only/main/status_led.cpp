#include "status_led.hpp"

#include "ble_hogp_client.hpp"
#include "ble_only_mode.hpp"
#include "ble_device_manager.hpp"

#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

namespace {
constexpr char kTag[] = "status_led";
constexpr TickType_t kSamplePeriod = pdMS_TO_TICKS(50);
constexpr BaseType_t kLedCore = 0;
}

esp_err_t StatusLed::start() {
    led_strip_config_t strip_config{};
    strip_config.strip_gpio_num = CONFIG_BLE_ONLY_LED_GPIO;
    strip_config.max_leds = 1;
    led_strip_rmt_config_t rmt_config{};
    rmt_config.resolution_hz = 10'000'000;
    rmt_config.flags.with_dma = false;
    esp_err_t result =
        led_strip_new_rmt_device(&strip_config, &rmt_config, &strip_);
    if (result != ESP_OK) return result;
    result = led_strip_clear(strip_);
    if (result != ESP_OK) {
        (void)led_strip_del(strip_);
        strip_ = nullptr;
        return result;
    }

    const TaskHandle_t handle = xTaskCreateStaticPinnedToCore(
        task_entry, "status_led", task_stack_.size(), this, 1,
        task_stack_.data(), &task_control_, kLedCore);
    if (handle == nullptr) {
        (void)led_strip_del(strip_);
        strip_ = nullptr;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void StatusLed::task_entry(void* arg) {
    static_cast<StatusLed*>(arg)->task();
}

void StatusLed::task() {
    ESP_LOGI(kTag, "core=%d gpio=%d rgb_budget=76", xPortGetCoreID(),
             CONFIG_BLE_ONLY_LED_GPIO);
    LedRgb previous{};
    bool previous_valid = false;
    while (true) {
        const uint32_t now_ms =
            static_cast<uint32_t>(esp_timer_get_time() / 1'000);
        const auto setup = manager_.status(now_ms).setup;
        const LedRgb color = model_.update(
            ble_.connected(), setup.portal_active, mode_.mods_enabled(),
            mode_.selected_profile(), now_ms);
        if (!previous_valid || color != previous) {
            esp_err_t result = led_strip_set_pixel(
                strip_, 0, color.red, color.green, color.blue);
            if (result == ESP_OK) result = led_strip_refresh(strip_);
            if (result != ESP_OK) {
                ESP_LOGE(kTag, "LED update failed: %s",
                         esp_err_to_name(result));
                (void)led_strip_clear(strip_);
                (void)led_strip_del(strip_);
                strip_ = nullptr;
                vTaskDelete(nullptr);
                return;
            }
            previous = color;
            previous_valid = true;
        }
        vTaskDelay(kSamplePeriod);
    }
}
