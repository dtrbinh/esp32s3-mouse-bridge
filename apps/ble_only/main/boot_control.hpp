#pragma once

#include "ble_device_manager.hpp"
#include "ble_only_mode.hpp"
#include "boot_press.hpp"

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <array>

class BootControl {
public:
    BootControl(BleDeviceManager& manager, BleOnlyMode& mode)
        : manager_(manager), mode_(mode) {}

    esp_err_t start(TaskHandle_t pipeline_handle);

private:
    static constexpr size_t kStackWords = 2048 / sizeof(StackType_t);
    static void task_entry(void* arg);
    void task();

    BleDeviceManager& manager_;
    BleOnlyMode& mode_;
    BootPressClassifier classifier_{};
    TaskHandle_t pipeline_handle_{};
    StaticTask_t task_control_{};
    std::array<StackType_t, kStackWords> task_stack_{};
};
