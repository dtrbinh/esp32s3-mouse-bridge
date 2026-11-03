#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "source_arbiter.hpp"
#include "usb_mouse_host.hpp"

extern "C" void app_main() {
    static SourceArbiter arbiter;
    static UsbMouseHost usb_mouse(arbiter);

    ESP_ERROR_CHECK(usb_mouse.start());
    ESP_LOGI("bridge_input", "USB mouse host started");

    while (true) {
        // Task 7 consumes these reports and publishes them over SPI.
        (void)arbiter.poll(static_cast<uint64_t>(esp_timer_get_time()));
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

