#include "usb_mouse_host.hpp"

#include "hid_parser/mouse_decoder.hpp"

#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "usb/usb_host.h"

#include <span>

namespace {
constexpr char kTag[] = "usb_mouse";
}

esp_err_t UsbMouseHost::start() {
    queue_ = xQueueCreateStatic(kQueueDepth, sizeof(Event), queue_storage_.data(),
                                &queue_control_);
    if (queue_ == nullptr) return ESP_ERR_NO_MEM;

    usb_host_config_t host_config{};
    host_config.skip_phy_setup = false;
    host_config.intr_flags = ESP_INTR_FLAG_LOWMED;
    esp_err_t result = usb_host_install(&host_config);
    if (result != ESP_OK) return result;

    if (xTaskCreateStatic(usb_task_entry, "usb_events", usb_task_stack_.size(),
                          this, 4, usb_task_stack_.data(),
                          &usb_task_control_) == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreateStatic(service_task_entry, "mouse_input",
                          service_task_stack_.size(), this, 6,
                          service_task_stack_.data(),
                          &service_task_control_) == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    hid_host_driver_config_t hid_config{};
    hid_config.create_background_task = true;
    hid_config.task_priority = 5;
    hid_config.stack_size = 4096;
    hid_config.core_id = tskNO_AFFINITY;
    hid_config.callback = driver_event_callback;
    hid_config.callback_arg = this;
    return hid_host_install(&hid_config);
}

void UsbMouseHost::driver_event_callback(hid_host_device_handle_t handle,
                                         hid_host_driver_event_t event,
                                         void* arg) {
    if (event != HID_HOST_DRIVER_EVENT_CONNECTED || arg == nullptr) return;
    auto* self = static_cast<UsbMouseHost*>(arg);
    Event queued{.type = EventType::Connected, .handle = handle};
    self->enqueue(queued, true);
}

void UsbMouseHost::interface_event_callback(hid_host_device_handle_t handle,
                                            hid_host_interface_event_t event,
                                            void* arg) {
    if (arg == nullptr) return;
    auto* self = static_cast<UsbMouseHost*>(arg);
    Event queued{.handle = handle,
                 .timestamp_us = static_cast<uint64_t>(esp_timer_get_time())};

    switch (event) {
    case HID_HOST_INTERFACE_EVENT_INPUT_REPORT: {
        queued.type = EventType::InputReport;
        size_t length = 0;
        if (hid_host_device_get_raw_input_report_data(
                handle, queued.data.data(), queued.data.size(), &length) != ESP_OK ||
            length > queued.data.size()) {
            self->dropped_reports_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        queued.length = static_cast<uint8_t>(length);
        self->enqueue(queued, false);
        return;
    }
    case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
        queued.type = EventType::Disconnected;
        self->enqueue(queued, true);
        return;
    case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
        queued.type = EventType::TransferError;
        self->enqueue(queued, false);
        return;
    default:
        return;
    }
}

bool UsbMouseHost::enqueue(const Event& event, bool critical) {
    if (xQueueSend(queue_, &event, 0) == pdTRUE) return true;
    if (!critical) {
        dropped_reports_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    Event discarded{};
    (void)xQueueReceive(queue_, &discarded, 0);
    if (xQueueSendToFront(queue_, &event, 0) == pdTRUE) {
        dropped_reports_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    return false;
}

void UsbMouseHost::usb_task_entry(void* arg) {
    static_cast<UsbMouseHost*>(arg)->usb_task();
}

void UsbMouseHost::service_task_entry(void* arg) {
    static_cast<UsbMouseHost*>(arg)->service_task();
}

void UsbMouseHost::usb_task() {
    while (true) {
        uint32_t event_flags = 0;
        (void)usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
    }
}

void UsbMouseHost::service_task() {
    Event event{};
    while (xQueueReceive(queue_, &event, portMAX_DELAY) == pdTRUE) {
        switch (event.type) {
        case EventType::Connected:
            handle_connected(event.handle);
            break;
        case EventType::InputReport:
            handle_input(event);
            break;
        case EventType::Disconnected:
            handle_disconnected(event.handle, event.timestamp_us);
            break;
        case EventType::TransferError:
            transfer_errors_.fetch_add(1, std::memory_order_relaxed);
            break;
        }
    }
}

void UsbMouseHost::handle_connected(hid_host_device_handle_t handle) {
    hid_host_device_config_t device_config{};
    device_config.callback = interface_event_callback;
    device_config.callback_arg = this;
    if (hid_host_device_open(handle, &device_config) != ESP_OK) return;

    size_t descriptor_length = 0;
    uint8_t* descriptor =
        hid_host_get_report_descriptor(handle, &descriptor_length);
    if (descriptor == nullptr || descriptor_length == 0) {
        rejected_interfaces_.fetch_add(1, std::memory_order_relaxed);
        (void)hid_host_device_close(handle);
        return;
    }

    const auto parsed = hid_parser::parse_mouse_descriptor(
        std::span<const uint8_t>(descriptor, descriptor_length));
    if (parsed.status != hid_parser::ParseStatus::Ok ||
        active_handle_ != nullptr) {
        rejected_interfaces_.fetch_add(1, std::memory_order_relaxed);
        ESP_LOGW(kTag, "Reject HID interface: parser status=%u",
                 static_cast<unsigned>(parsed.status));
        (void)hid_host_device_close(handle);
        return;
    }

    active_handle_ = handle;
    active_layout_ = parsed.layout;
    if (hid_host_device_start(handle) != ESP_OK) {
        active_handle_ = nullptr;
        rejected_interfaces_.fetch_add(1, std::memory_order_relaxed);
        (void)hid_host_device_close(handle);
    }
}

void UsbMouseHost::handle_disconnected(hid_host_device_handle_t handle,
                                       uint64_t now_us) {
    if (handle == active_handle_) {
        active_handle_ = nullptr;
        active_layout_ = {};
        arbiter_.on_disconnected(mouse_bridge::InputSource::Usb, now_us);
    }
    (void)hid_host_device_close(handle);
}

void UsbMouseHost::handle_input(const Event& event) {
    if (event.handle != active_handle_) return;

    mouse_bridge::MouseReport report{};
    const auto status = hid_parser::decode_mouse_report(
        active_layout_,
        std::span<const uint8_t>(event.data.data(), event.length), report);
    if (status != hid_parser::DecodeStatus::Ok) {
        dropped_reports_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    arbiter_.on_report(mouse_bridge::InputSource::Usb, report,
                       event.timestamp_us);
}
