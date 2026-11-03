#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "source_arbiter.hpp"
#include "usb_mouse_host.hpp"
#include "ble_hogp_client.hpp"
#include "bond_control.hpp"

extern "C" void app_main() {
    static SourceArbiter arbiter;
    static UsbMouseHost usb_mouse(arbiter);
    static BleHogpClient ble_mouse(arbiter);
    static BondControl bond_control(ble_mouse);

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
    ESP_LOGI("bridge_input", "USB and BLE mouse inputs started");

    while (true) {
        // Task 7 consumes these reports and publishes them over SPI.
        (void)arbiter.poll(static_cast<uint64_t>(esp_timer_get_time()));
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

