#include "usb_mouse_device.hpp"

#include "tusb_descriptors.h"

#include "class/hid/hid_device.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "tinyusb.h"
#include "tinyusb_cdc_acm.h"
#include "tinyusb_console.h"
#include "tinyusb_default_config.h"

#include <algorithm>
#include <cstdint>

namespace {
constexpr gpio_num_t kBypassPin = GPIO_NUM_0;
constexpr char kTag[] = "usb_mouse";
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

struct __attribute__((packed)) BootKeyboardReport {
    uint8_t modifiers;
    uint8_t reserved;
    uint8_t keys[6];
};
static_assert(sizeof(BootKeyboardReport) == 8);
}

esp_err_t UsbMouseDevice::start(bool enable_bypass_button) {
    device_instance = this;
    bypass_button_enabled_ = enable_bypass_button;
    keyboard_queue_ = xQueueCreateStatic(
        kKeyboardQueueCapacity, sizeof(mouse_bridge::KeyboardReport),
        keyboard_queue_storage_.data(), &keyboard_queue_control_);
    if (keyboard_queue_ == nullptr) return ESP_ERR_NO_MEM;
    esp_err_t result = ESP_OK;
    if (bypass_button_enabled_) {
        gpio_config_t button{};
        button.pin_bit_mask = 1ULL << kBypassPin;
        button.mode = GPIO_MODE_INPUT;
        button.pull_up_en = GPIO_PULLUP_ENABLE;
        result = gpio_config(&button);
        if (result != ESP_OK) return result;
    }

    tinyusb_config_t config = TINYUSB_DEFAULT_CONFIG();
    config.task.xCoreID = 1;
    config.task.priority = 5;
    config.descriptor.device = nullptr;
    config.descriptor.full_speed_config = mouse_hid_configuration_descriptor;
    config.descriptor.string = mouse_hid_string_descriptor;
    config.descriptor.string_count = mouse_hid_string_descriptor_count;
    result = tinyusb_driver_install(&config);
    if (result != ESP_OK) return result;

#if CONFIG_TINYUSB_CDC_ENABLED
    const tinyusb_config_cdcacm_t cdc_config{
        .cdc_port = TINYUSB_CDC_ACM_0,
        .callback_rx = nullptr,
        .callback_rx_wanted_char = nullptr,
        .callback_line_state_changed = nullptr,
        .callback_line_coding_changed = nullptr,
    };
    result = tinyusb_cdcacm_init(&cdc_config);
    if (result != ESP_OK) return result;
    result = tinyusb_console_init(TINYUSB_CDC_ACM_0);
    if (result != ESP_OK) return result;
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    ESP_LOGI(kTag, "USB composite HID+CDC diagnostics ready");
#endif

    task_handle_ = xTaskCreateStaticPinnedToCore(
        task_entry, "usb_mouse", task_stack_.size(), this, 8,
        task_stack_.data(), &task_control_, 1);
    return task_handle_ != nullptr ? ESP_OK : ESP_ERR_NO_MEM;
}

void UsbMouseDevice::on_packet(const mouse_bridge::MouseReport& report,
                               uint32_t sequence, uint64_t rx_us) {
    active_source_.store(static_cast<uint8_t>(report.source),
                         std::memory_order_relaxed);
    portENTER_CRITICAL(&state_lock_);
    state_.on_packet(report, sequence, rx_us);
    scheduler_.on_packet(report, rx_us);
    if (pending_stage_rx_us_ == 0) pending_stage_rx_us_ = rx_us;
    portEXIT_CRITICAL(&state_lock_);
    notify_packet_available();
}

void UsbMouseDevice::on_keyboard_report(
    const mouse_bridge::KeyboardReport& report) {
    portENTER_CRITICAL(&keyboard_lock_);
    if (report == last_keyboard_) {
        portEXIT_CRITICAL(&keyboard_lock_);
        return;
    }
    last_keyboard_ = report;
    portEXIT_CRITICAL(&keyboard_lock_);

    if (xQueueSend(keyboard_queue_, &report, 0) != pdTRUE) {
        mouse_bridge::KeyboardReport discarded{};
        (void)xQueueReceive(keyboard_queue_, &discarded, 0);
        (void)xQueueSend(keyboard_queue_, &report, 0);
    }
    notify_packet_available();
}

void UsbMouseDevice::on_suspend() {
    portENTER_CRITICAL(&state_lock_);
    suspended_.store(true, std::memory_order_relaxed);
    state_.on_usb_suspend();
    scheduler_.reset();
    portEXIT_CRITICAL(&state_lock_);
    notify_packet_available();
}

void UsbMouseDevice::on_resume() {
    portENTER_CRITICAL(&state_lock_);
    suspended_.store(false, std::memory_order_relaxed);
    state_.on_usb_resume(static_cast<uint64_t>(esp_timer_get_time()));
    scheduler_.reset();
    portEXIT_CRITICAL(&state_lock_);
    resume_count_.fetch_add(1, std::memory_order_relaxed);
    mouse_bridge::KeyboardReport keyboard{};
    portENTER_CRITICAL(&keyboard_lock_);
    keyboard = last_keyboard_;
    portEXIT_CRITICAL(&keyboard_lock_);
    if (keyboard_queue_ != nullptr) {
        (void)xQueueSend(keyboard_queue_, &keyboard, 0);
    }
    notify_packet_available();
}

void UsbMouseDevice::notify_packet_available() {
    if (task_handle_ != nullptr) xTaskNotifyGive(task_handle_);
}

uint32_t UsbMouseDevice::timeout_count() {
    portENTER_CRITICAL(&state_lock_);
    const uint32_t value = state_.timeout_count();
    portEXIT_CRITICAL(&state_lock_);
    return value;
}

uint16_t UsbMouseDevice::output_interval_us() {
    portENTER_CRITICAL(&state_lock_);
    const uint16_t value = scheduler_.interval_us();
    portEXIT_CRITICAL(&state_lock_);
    return value;
}

void UsbMouseDevice::task_entry(void* arg) {
    static_cast<UsbMouseDevice*>(arg)->task();
}

void UsbMouseDevice::task() {
    ESP_LOGI(kTag, "core=%d", xPortGetCoreID());
    if (esp_task_wdt_status(nullptr) == ESP_ERR_NOT_FOUND) {
        ESP_ERROR_CHECK(esp_task_wdt_add(nullptr));
    }
    unsigned low_samples = 0;
    unsigned high_samples = 0;
    uint64_t next_button_sample_us = 0;
    while (true) {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1));
        const uint64_t now_us = static_cast<uint64_t>(esp_timer_get_time());
        if (bypass_button_enabled_ && now_us >= next_button_sample_us) {
            next_button_sample_us = now_us + 1'000;
            const bool pressed = gpio_get_level(kBypassPin) == 0;
            low_samples = pressed
                              ? std::min(low_samples + 1,
                                         kBypassDebounceSamples)
                              : 0;
            high_samples = pressed
                               ? 0
                               : std::min(high_samples + 1,
                                          kBypassDebounceSamples);
            if (low_samples == kBypassDebounceSamples) bypass_ = true;
            if (high_samples == kBypassDebounceSamples) bypass_ = false;
        }

        const bool usb_ready = !suspended_.load(std::memory_order_relaxed) &&
                               tud_hid_ready();
        if (!suspended_.load(std::memory_order_relaxed) &&
            tud_hid_n_ready(1)) {
            mouse_bridge::KeyboardReport keyboard{};
            if (xQueueReceive(keyboard_queue_, &keyboard, 0) == pdTRUE) {
                send_keyboard(keyboard);
            }
        }
        bool report_due = false;
        OutputFrame frame{};
        portENTER_CRITICAL(&state_lock_);
        if (pending_stage_rx_us_ != 0) {
            const uint32_t latency =
                now_us >= pending_stage_rx_us_
                    ? static_cast<uint32_t>(std::min<uint64_t>(
                          now_us - pending_stage_rx_us_, UINT32_MAX))
                    : 0;
            uint32_t previous = max_stage_latency_us_.load();
            while (latency > previous &&
                   !max_stage_latency_us_.compare_exchange_weak(previous,
                                                                latency)) {}
            pending_stage_rx_us_ = 0;
        }
        if (state_.update_timeouts(now_us)) scheduler_.request_immediate();
        if (usb_ready && scheduler_.due(now_us)) {
            frame = state_.tick(now_us, bypass_);
            scheduler_.on_sent(now_us);
            report_due = true;
        }
        portEXIT_CRITICAL(&state_lock_);
        if (report_due) {
            send(frame);
        }
        ESP_ERROR_CHECK(esp_task_wdt_reset());
    }
}

void UsbMouseDevice::send_keyboard(
    const mouse_bridge::KeyboardReport& keyboard) {
    BootKeyboardReport report{.modifiers = keyboard.modifiers,
                              .reserved = 0,
                              .keys = {}};
    std::copy(keyboard.keys.begin(), keyboard.keys.end(), report.keys);
    (void)tud_hid_n_report(1, 0, &report, sizeof(report));
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
