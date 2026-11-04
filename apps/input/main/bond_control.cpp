#include "bond_control.hpp"

#include "driver/gpio.h"
#include "esp_log.h"

namespace {
constexpr gpio_num_t kBootPin = GPIO_NUM_0;
constexpr TickType_t kSamplePeriod = pdMS_TO_TICKS(20);
constexpr unsigned kLongPressSamples = 100;
}

esp_err_t BondControl::start() {
    gpio_config_t config{};
    config.pin_bit_mask = 1ULL << kBootPin;
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en = GPIO_PULLUP_ENABLE;
    esp_err_t result = gpio_config(&config);
    if (result != ESP_OK) return result;
    return xTaskCreateStaticPinnedToCore(
               task_entry, "bond_button", task_stack_.size(), this, 2,
               task_stack_.data(), &task_control_, 0) !=
                   nullptr
               ? ESP_OK
               : ESP_ERR_NO_MEM;
}

void BondControl::task_entry(void* arg) {
    static_cast<BondControl*>(arg)->task();
}

void BondControl::task() {
    ESP_LOGI("bond_button", "core=%d", xPortGetCoreID());
    unsigned held_samples = 0;
    bool fired = false;
    while (true) {
        if (gpio_get_level(kBootPin) == 0) {
            if (held_samples < kLongPressSamples) ++held_samples;
            if (held_samples == kLongPressSamples && !fired) {
                fired = true;
                ble_.clear_bond();
            }
        } else {
            held_samples = 0;
            fired = false;
        }
        vTaskDelay(kSamplePeriod);
    }
}
