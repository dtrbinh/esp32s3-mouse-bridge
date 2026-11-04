#pragma once

#include "status_led_model.hpp"

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"

#include <array>

class BleHogpClient;
class BleOnlyMode;

class StatusLed {
public:
    StatusLed(const BleHogpClient& ble, const BleOnlyMode& mode)
        : ble_(ble), mode_(mode) {}

    esp_err_t start();

private:
    static constexpr size_t kStackWords = 3072 / sizeof(StackType_t);
    static void task_entry(void* arg);
    void task();

    const BleHogpClient& ble_;
    const BleOnlyMode& mode_;
    StatusLedModel model_{};
    led_strip_handle_t strip_{};
    StaticTask_t task_control_{};
    std::array<StackType_t, kStackWords> task_stack_{};
};
