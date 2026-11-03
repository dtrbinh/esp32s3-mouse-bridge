#pragma once

#include "ble_hogp_client.hpp"

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <array>

class BondControl {
public:
    explicit BondControl(BleHogpClient& ble) : ble_(ble) {}
    esp_err_t start();

private:
    static constexpr size_t kStackWords = 2048 / sizeof(StackType_t);
    static void task_entry(void* arg);
    void task();

    BleHogpClient& ble_;
    StaticTask_t task_control_{};
    std::array<StackType_t, kStackWords> task_stack_{};
};
