#include "usb_mouse_device.hpp"

#include "tusb_descriptors.h"

#include "class/hid/hid_device.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"

#include <algorithm>
#include <cstdint>

namespace {
constexpr gpio_num_t kBypassPin = GPIO_NUM_0;
UsbMouseDevice* device_instance;

struct __attribute__((packed)) ExtendedMouseReport {
    uint16_t buttons;
    int16_t x;
    int16_t y;
    int8_t wheel;
    int8_t pan;
};
static_assert(sizeof(ExtendedMouseReport) == 8);

struct __attribute__((packed)) BootMouseReport {
    uint8_t buttons;
    int8_t x;
    int8_t y;
};
static_assert(sizeof(BootMouseReport) == 3);
}

esp_err_t UsbMouseDevice::start() {
    device_instance = this;
    gpio_config_t button{};
    button.pin_bit_mask = 1ULL << kBypassPin;
    button.mode = GPIO_MODE_INPUT;
    button.pull_up_en = GPIO_PULLUP_ENABLE;
    esp_err_t result = gpio_config(&button);
    if (result != ESP_OK) return result;

    tinyusb_config_t config = TINYUSB_DEFAULT_CONFIG();
    config.descriptor.device = nullptr;
    config.descriptor.full_speed_config = mouse_hid_configuration_descriptor;
    config.descriptor.string = mouse_hid_string_descriptor;
    config.descriptor.string_count = mouse_hid_string_descriptor_count;
    result = tinyusb_driver_install(&config);
    if (result != ESP_OK) return result;

    return xTaskCreateStatic(task_entry, "usb_mouse", task_stack_.size(), this,
                             8, task_stack_.data(), &task_control_) != nullptr
               ? ESP_OK
               : ESP_ERR_NO_MEM;
}

void UsbMouseDevice::on_packet(const mouse_bridge::MouseReport& report,
                               uint32_t sequence, uint64_t rx_us) {
    active_source_.store(static_cast<uint8_t>(report.source),
                         std::memory_order_relaxed);
    portENTER_CRITICAL(&state_lock_);
    state_.on_packet(report, sequence, rx_us);
    portEXIT_CRITICAL(&state_lock_);
}

void UsbMouseDevice::on_suspend() {
    portENTER_CRITICAL(&state_lock_);
    suspended_.store(true, std::memory_order_relaxed);
    state_.on_usb_suspend();
    portEXIT_CRITICAL(&state_lock_);
}

void UsbMouseDevice::on_resume() {
    portENTER_CRITICAL(&state_lock_);
    suspended_.store(false, std::memory_order_relaxed);
    state_.on_usb_resume(static_cast<uint64_t>(esp_timer_get_time()));
    portEXIT_CRITICAL(&state_lock_);
    resume_count_.fetch_add(1, std::memory_order_relaxed);
}

uint32_t UsbMouseDevice::timeout_count() {
    portENTER_CRITICAL(&state_lock_);
    const uint32_t value = state_.timeout_count();
    portEXIT_CRITICAL(&state_lock_);
    return value;
}

void UsbMouseDevice::task_entry(void* arg) {
    static_cast<UsbMouseDevice*>(arg)->task();
}

void UsbMouseDevice::task() {
    if (esp_task_wdt_status(nullptr) == ESP_ERR_NOT_FOUND) {
        ESP_ERROR_CHECK(esp_task_wdt_add(nullptr));
    }
    TickType_t wake = xTaskGetTickCount();
    unsigned low_samples = 0;
    unsigned high_samples = 0;
    while (true) {
        const bool pressed = gpio_get_level(kBypassPin) == 0;
        low_samples = pressed ? std::min(low_samples + 1,
                                         kBypassDebounceSamples)
                              : 0;
        high_samples = pressed ? 0
                               : std::min(high_samples + 1,
                                          kBypassDebounceSamples);
        if (low_samples == kBypassDebounceSamples) bypass_ = true;
        if (high_samples == kBypassDebounceSamples) bypass_ = false;

        if (!suspended_.load(std::memory_order_relaxed) && tud_hid_ready()) {
            portENTER_CRITICAL(&state_lock_);
            const OutputFrame frame = state_.tick(
                static_cast<uint64_t>(esp_timer_get_time()), bypass_);
            portEXIT_CRITICAL(&state_lock_);
            send(frame);
        }
        ESP_ERROR_CHECK(esp_task_wdt_reset());
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(1));
    }
}

void UsbMouseDevice::send(const OutputFrame& frame) {
    if (mouse_hid_boot_protocol()) {
        const BootMouseReport report{
            .buttons = static_cast<uint8_t>(frame.buttons & 0x07),
            .x = static_cast<int8_t>(std::clamp<int>(frame.x, INT8_MIN,
                                                     INT8_MAX)),
            .y = static_cast<int8_t>(std::clamp<int>(frame.y, INT8_MIN,
                                                     INT8_MAX)),
        };
        (void)tud_hid_report(0, &report, sizeof(report));
        return;
    }
    const ExtendedMouseReport report{.buttons = frame.buttons,
                                     .x = frame.x,
                                     .y = frame.y,
                                     .wheel = frame.wheel,
                                     .pan = frame.pan};
    (void)tud_hid_report(0, &report, sizeof(report));
}

extern "C" void tud_suspend_cb(bool) {
    if (device_instance != nullptr) device_instance->on_suspend();
}

extern "C" void tud_resume_cb(void) {
    if (device_instance != nullptr) device_instance->on_resume();
}
