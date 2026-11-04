#include "ble_hogp_client.hpp"

#include "mouse_bridge/cadence.hpp"

#include "hid_parser/mouse_decoder.hpp"

#include "esp_log.h"
#include "esp_timer.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"

#include <algorithm>
#include <cstring>
#include <span>

extern "C" void ble_store_config_init(void);

namespace {
constexpr char kTag[] = "ble_mouse";
constexpr uint16_t kHidService = 0x1812;
constexpr uint16_t kPreferredIntervalUnits = 6;  // 7.5 ms
}

BleHogpClient* BleHogpClient::instance_ = nullptr;

esp_err_t BleHogpClient::start() {
    instance_ = this;
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES ||
        result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        result = nvs_flash_init();
    }
    if (result != ESP_OK) return result;

    result = nimble_port_init();
    if (result != ESP_OK) return result;
    ble_hs_cfg.sync_cb = sync_callback;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_store_config_init();
    if (ble_gap_event_listener_register(&gap_listener_, gap_listener_callback,
                                        this) != 0) {
        return ESP_FAIL;
    }

    esp_hidh_config_t config{};
    config.callback = hidh_callback;
    config.event_stack_size = 4096;
    config.callback_arg = this;
    result = esp_hidh_init(&config);
    if (result != ESP_OK) return result;

    worker_handle_ = xTaskCreateStatic(worker_task_entry, "ble_mouse",
                                       worker_stack_.size(), this, 3,
                                       worker_stack_.data(), &worker_control_);
    if (worker_handle_ == nullptr) return ESP_ERR_NO_MEM;
    nimble_port_freertos_init(host_task_entry);
    return ESP_OK;
}

void BleHogpClient::host_task_entry(void*) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void BleHogpClient::worker_task_entry(void* arg) {
    static_cast<BleHogpClient*>(arg)->worker_task();
}

void BleHogpClient::worker_task() {
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (connecting_.exchange(true)) continue;
        device_.store(esp_hidh_dev_open(candidate_address_.data(),
                                        ESP_HID_TRANSPORT_BLE,
                                        candidate_address_type_),
                      std::memory_order_release);
        if (device_.load(std::memory_order_acquire) == nullptr) {
            connecting_.store(false);
            resume_or_scan();
        }
    }
}

void BleHogpClient::sync_callback() {
    if (instance_ != nullptr) instance_->on_sync();
}

void BleHogpClient::on_sync() {
    synced_.store(true);
    resume_or_scan();
}

int BleHogpClient::scan_callback(ble_gap_event* event, void* arg) {
    auto* self = static_cast<BleHogpClient*>(arg);
    if (event->type == BLE_GAP_EVENT_DISC) {
        ble_hs_adv_fields fields{};
        if (ble_hs_adv_parse_fields(&fields, event->disc.data,
                                    event->disc.length_data) != 0) {
            return 0;
        }
        bool hid = false;
        for (int i = 0; i < fields.num_uuids16; ++i) {
            if (ble_uuid_u16(&fields.uuids16[i].u) == kHidService) {
                hid = true;
                break;
            }
        }
        if (!hid || self->connecting_.load() ||
            self->device_.load(std::memory_order_acquire) != nullptr) {
            return 0;
        }
        std::copy_n(event->disc.addr.val, self->candidate_address_.size(),
                    self->candidate_address_.begin());
        self->candidate_address_type_ = event->disc.addr.type;
        (void)ble_gap_disc_cancel();
        xTaskNotifyGive(self->worker_handle_);
    } else if (event->type == BLE_GAP_EVENT_DISC_COMPLETE &&
               self->device_.load(std::memory_order_acquire) == nullptr &&
               !self->connecting_.load()) {
        self->resume_or_scan();
    }
    return 0;
}

int BleHogpClient::gap_listener_callback(ble_gap_event* event, void* arg) {
    auto* self = static_cast<BleHogpClient*>(arg);
    if (self == nullptr || event->type != BLE_GAP_EVENT_CONN_UPDATE ||
        event->conn_update.status != 0) {
        return 0;
    }
    ble_gap_conn_desc connection{};
    if (ble_gap_conn_find(event->conn_update.conn_handle, &connection) == 0) {
        self->negotiated_interval_units_.store(connection.conn_itvl,
                                               std::memory_order_relaxed);
    }
    return 0;
}

void BleHogpClient::resume_or_scan() {
    forwarding_.store(true);
    if (!synced_.load() || device_.load(std::memory_order_acquire) != nullptr ||
        connecting_.load() ||
        ble_gap_disc_active()) {
        return;
    }
    uint8_t own_address_type = 0;
    if (ble_hs_id_infer_auto(0, &own_address_type) != 0) return;
    ble_gap_disc_params params{};
    params.filter_duplicates = 1;
    params.passive = 0;
    params.itvl = 0x30;
    params.window = 0x30;
    (void)ble_gap_disc(own_address_type, BLE_HS_FOREVER, &params,
                       scan_callback, this);
}

void BleHogpClient::suspend_forwarding() {
    forwarding_.store(false);
}

void BleHogpClient::clear_bond() {
    forwarding_.store(false);
    (void)ble_store_clear();
    if (auto* device = device_.load(std::memory_order_acquire)) {
        (void)esp_hidh_dev_close(device);
    }
}

void BleHogpClient::hidh_callback(void* handler_arg, esp_event_base_t,
                                  int32_t event_id, void* event_data) {
    if (handler_arg == nullptr || event_data == nullptr) return;
    static_cast<BleHogpClient*>(handler_arg)->on_hidh_event(
        static_cast<esp_hidh_event_t>(event_id),
        *static_cast<esp_hidh_event_data_t*>(event_data));
}

void BleHogpClient::on_hidh_event(esp_hidh_event_t event,
                                  const esp_hidh_event_data_t& data) {
    const uint64_t now_us = static_cast<uint64_t>(esp_timer_get_time());
    switch (event) {
    case ESP_HIDH_OPEN_EVENT:
        connecting_.store(false);
        if (data.open.status == ESP_OK && compile_maps(data.open.dev)) {
            device_.store(data.open.dev, std::memory_order_release);
            connections_.fetch_add(1, std::memory_order_relaxed);
            request_low_latency_interval(data.open.dev);
            arbiter_.on_connected(mouse_bridge::InputSource::Ble, now_us);
        } else {
            device_.store(nullptr, std::memory_order_release);
            if (data.open.dev != nullptr) (void)esp_hidh_dev_close(data.open.dev);
            resume_or_scan();
        }
        break;
    case ESP_HIDH_INPUT_EVENT:
        decode_input(data);
        break;
    case ESP_HIDH_CLOSE_EVENT:
        if (data.close.dev == device_.load(std::memory_order_acquire)) {
            device_.store(nullptr, std::memory_order_release);
            layout_valid_.fill(false);
            arbiter_.on_disconnected(mouse_bridge::InputSource::Ble, now_us);
        }
        (void)esp_hidh_dev_free(data.close.dev);
        connecting_.store(false);
        resume_or_scan();
        break;
    default:
        break;
    }
}

void BleHogpClient::request_low_latency_interval(esp_hidh_dev_t* device) {
    const uint8_t* bda = esp_hidh_dev_bda_get(device);
    if (bda == nullptr) return;

    ble_addr_t peer{};
    peer.type = candidate_address_type_;
    std::copy_n(bda, sizeof(peer.val), peer.val);
    ble_gap_conn_desc connection{};
    if (ble_gap_conn_find_by_addr(&peer, &connection) != 0) return;

    negotiated_interval_units_.store(connection.conn_itvl,
                                     std::memory_order_relaxed);
    ble_gap_upd_params params{};
    params.itvl_min = kPreferredIntervalUnits;
    params.itvl_max = kPreferredIntervalUnits;
    params.latency = 0;
    params.supervision_timeout = 200;  // 2 seconds
    const int result = ble_gap_update_params(connection.conn_handle, &params);
    if (result != 0) {
        ESP_LOGW(kTag, "Mouse kept negotiated BLE interval (%u units), rc=%d",
                 static_cast<unsigned>(connection.conn_itvl), result);
    }
}

bool BleHogpClient::compile_maps(esp_hidh_dev_t* device) {
    size_t map_count = 0;
    esp_hid_raw_report_map_t* maps = nullptr;
    if (esp_hidh_dev_report_maps_get(device, &map_count, &maps) != ESP_OK ||
        maps == nullptr) {
        return false;
    }
    layout_valid_.fill(false);
    bool found = false;
    for (size_t i = 0; i < std::min(map_count, kMaxMaps); ++i) {
        const auto parsed = hid_parser::parse_mouse_descriptor(
            std::span<const uint8_t>(maps[i].data, maps[i].len));
        if (parsed.status == hid_parser::ParseStatus::Ok) {
            layouts_[i] = parsed.layout;
            layout_valid_[i] = true;
            found = true;
        }
    }
    return found;
}

void BleHogpClient::decode_input(const esp_hidh_event_data_t& data) {
    if (!forwarding_.load() || data.input.map_index >= kMaxMaps ||
        !layout_valid_[data.input.map_index] || data.input.data == nullptr) {
        return;
    }
    const auto& layout = layouts_[data.input.map_index];
    std::array<uint8_t, 65> report_bytes{};
    size_t length = data.input.length;
    if (layout.has_report_id) {
        if (length >= report_bytes.size()) return;
        report_bytes[0] = static_cast<uint8_t>(data.input.report_id);
        std::copy_n(data.input.data, length, report_bytes.begin() + 1);
        ++length;
    } else {
        if (length > report_bytes.size()) return;
        std::copy_n(data.input.data, length, report_bytes.begin());
    }

    mouse_bridge::MouseReport report{};
    if (hid_parser::decode_mouse_report(
            layout, std::span<const uint8_t>(report_bytes.data(), length),
            report) == hid_parser::DecodeStatus::Ok) {
        const uint16_t units = negotiated_interval_units_.load(
            std::memory_order_relaxed);
        report.poll_interval_us = units == 0
                                      ? 0
                                      : mouse_bridge::sanitize_poll_interval_us(
                                            static_cast<uint32_t>(units) * 1'250U);
        arbiter_.on_report(mouse_bridge::InputSource::Ble, report,
                           static_cast<uint64_t>(esp_timer_get_time()));
    }
}
