#include "ble_hogp_client.hpp"

#include "hid_parser/mouse_decoder.hpp"
#include "mouse_bridge/cadence.hpp"

#include "esp_log.h"
#include "esp_timer.h"
#include "host/ble_att.h"
#include "host/ble_hs.h"
#include "host/ble_sm.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "os/os_mbuf.h"

#include <algorithm>
#include <cstring>

namespace {
constexpr char kTag[] = "ble_mouse";
constexpr uint16_t kHidService = 0x1812;
constexpr uint16_t kReportMapCharacteristic = 0x2A4B;
constexpr uint16_t kReportCharacteristic = 0x2A4D;
constexpr uint16_t kReportReferenceDescriptor = 0x2908;
constexpr uint16_t kPreferredIntervalUnits = 6;  // 7.5 ms
const ble_uuid16_t kHidServiceUuid = BLE_UUID16_INIT(kHidService);

bool auth_required(int status) {
    return status == BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_AUTHEN) ||
           status == BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_AUTHOR) ||
           status == BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_ENC);
}
}  // namespace

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
    if (selection_delegate_ != nullptr) {
        selection_delegate_->on_client_start(
            static_cast<uint32_t>(esp_timer_get_time() / 1'000));
    }

    result = nimble_port_init();
    if (result != ESP_OK) return result;
    ble_hs_cfg.sync_cb = sync_callback;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 0;
    ble_hs_cfg.sm_sc = 0;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_our_key_dist = 0;
    ble_hs_cfg.sm_their_key_dist = 0;

    command_queue_ = xQueueCreateStatic(
        kCommandCapacity, sizeof(BleClientCommand),
        command_queue_storage_.data(), &command_queue_control_);
    if (command_queue_ == nullptr) return ESP_ERR_NO_MEM;
    worker_handle_ = xTaskCreateStaticPinnedToCore(
        worker_task_entry, "ble_mouse", worker_stack_.size(), this, 3,
        worker_stack_.data(), &worker_control_, 0);
    if (worker_handle_ == nullptr) return ESP_ERR_NO_MEM;
    nimble_port_freertos_init(host_task_entry);
    ESP_LOGI(kTag, "transport=direct_gatt bonding=off encryption=off");
    return ESP_OK;
}

void BleHogpClient::host_task_entry(void*) {
    ESP_LOGI(kTag, "nimble_host core=%d", xPortGetCoreID());
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void BleHogpClient::worker_task_entry(void* arg) {
    static_cast<BleHogpClient*>(arg)->worker_task();
}

void BleHogpClient::worker_task() {
    ESP_LOGI(kTag, "control core=%d", xPortGetCoreID());
    while (true) {
        BleClientCommand command{};
        if (xQueueReceive(command_queue_, &command, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        switch (command.kind) {
        case BleClientCommandKind::StartScan:
            portal_scanning_.store(true, std::memory_order_release);
            resume_or_scan();
            break;
        case BleClientCommandKind::StopScan:
            portal_scanning_.store(false, std::memory_order_release);
            (void)ble_gap_disc_cancel();
            break;
        case BleClientCommandKind::Connect:
            begin_connect(command.peer);
            break;
        case BleClientCommandKind::Disconnect:
            forwarding_.store(false, std::memory_order_release);
            (void)ble_gap_disc_cancel();
            if (connection_handle_ != BLE_HS_CONN_HANDLE_NONE) {
                (void)ble_gap_terminate(connection_handle_,
                                        BLE_ERR_REM_USER_CONN_TERM);
            } else {
                connecting_.store(false, std::memory_order_release);
                if (selection_delegate_ != nullptr) {
                    selection_delegate_->on_disconnected(
                        static_cast<uint32_t>(esp_timer_get_time() / 1'000));
                }
                resume_or_scan();
            }
            break;
        case BleClientCommandKind::Forget: {
            const BlePeerAddress peer = normalize_ble_address(command.peer);
            const auto current = connected_identity();
            if (current.has_value() && *current == peer) {
                pending_forget_ = peer;
                pending_forget_valid_.store(true, std::memory_order_release);
                (void)ble_gap_terminate(connection_handle_,
                                        BLE_ERR_REM_USER_CONN_TERM);
            } else {
                forget_peer(peer);
            }
            break;
        }
        }
    }
}

bool BleHogpClient::enqueue_control(BleClientCommand command) {
    return command_queue_ != nullptr &&
           xQueueSend(command_queue_, &command, 0) == pdTRUE;
}

void BleHogpClient::sync_callback() {
    if (instance_ != nullptr) instance_->on_sync();
}

void BleHogpClient::on_sync() {
    synced_.store(true, std::memory_order_release);
    resume_or_scan();
}

int BleHogpClient::gap_callback(ble_gap_event* event, void* arg) {
    if (arg == nullptr || event == nullptr) return 0;
    return static_cast<BleHogpClient*>(arg)->on_gap_event(*event);
}

int BleHogpClient::on_gap_event(ble_gap_event& event) {
    switch (event.type) {
    case BLE_GAP_EVENT_DISC: {
        ble_hs_adv_fields fields{};
        if (ble_hs_adv_parse_fields(&fields, event.disc.data,
                                    event.disc.length_data) != 0) {
            return 0;
        }
        bool advertises_hid = false;
        for (int i = 0; i < fields.num_uuids16; ++i) {
            if (ble_uuid_u16(&fields.uuids16[i].u) == kHidService) {
                advertises_hid = true;
                break;
            }
        }
        BleAdvertisement advertisement{};
        std::copy_n(event.disc.addr.val, advertisement.address.bytes.size(),
                    advertisement.address.bytes.begin());
        advertisement.address.type =
            normalize_ble_address_type(event.disc.addr.type);
        if (fields.name != nullptr && fields.name_len != 0) {
            advertisement.name = std::string_view(
                reinterpret_cast<const char*>(fields.name), fields.name_len);
            remember_name(advertisement.address, advertisement.name);
        }
        advertisement.rssi = event.disc.rssi;
        if (fields.appearance_is_present) {
            advertisement.appearance = fields.appearance;
        }
        advertisement.advertises_hid = advertises_hid;

        if (selection_delegate_ != nullptr) {
            selection_delegate_->on_advertisement(
                advertisement, std::nullopt,
                static_cast<uint32_t>(esp_timer_get_time() / 1'000));
        } else if (advertises_hid && !connecting_.load() &&
                   connection_handle_ == BLE_HS_CONN_HANDLE_NONE) {
            (void)enqueue_control(
                {BleClientCommandKind::Connect, advertisement.address});
        }
        return 0;
    }
    case BLE_GAP_EVENT_DISC_COMPLETE:
        resume_or_scan();
        return 0;
    case BLE_GAP_EVENT_CONNECT:
        if (event.connect.status != 0) {
            connecting_.store(false, std::memory_order_release);
            if (selection_delegate_ != nullptr) {
                selection_delegate_->on_attempt_failed(
                    candidate_, BleSelectionFailure::ConnectionFailed,
                    static_cast<uint32_t>(esp_timer_get_time() / 1'000));
            }
            resume_or_scan();
            return 0;
        }
        reset_discovery();
        connection_handle_ = event.connect.conn_handle;
        ESP_LOGI(kTag, "link_connected handle=%u",
                 static_cast<unsigned>(connection_handle_));
        {
            const int rc = ble_gattc_disc_svc_by_uuid(
                connection_handle_, &kHidServiceUuid.u, service_callback,
                this);
            if (rc != 0) {
                fail_attempt(BleSelectionFailure::ConnectionFailed,
                             "service_start", rc);
            }
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        handle_disconnect(event.disconnect.reason);
        return 0;
    case BLE_GAP_EVENT_NOTIFY_RX:
        decode_input(event.notify_rx.attr_handle, event.notify_rx.om);
        return 0;
    case BLE_GAP_EVENT_CONN_UPDATE:
        if (event.conn_update.status == 0) {
            ble_gap_conn_desc connection{};
            if (ble_gap_conn_find(event.conn_update.conn_handle,
                                  &connection) == 0) {
                negotiated_interval_units_.store(
                    connection.conn_itvl, std::memory_order_relaxed);
            }
        }
        return 0;
    default:
        return 0;
    }
}

void BleHogpClient::begin_connect(BlePeerAddress peer) {
    if (connecting_.load(std::memory_order_acquire) ||
        connection_handle_ != BLE_HS_CONN_HANDLE_NONE) {
        if (selection_delegate_ != nullptr) {
            selection_delegate_->on_attempt_failed(
                peer, BleSelectionFailure::ConnectionFailed,
                static_cast<uint32_t>(esp_timer_get_time() / 1'000));
        }
        return;
    }
    connecting_.store(true, std::memory_order_release);
    (void)ble_gap_disc_cancel();
    candidate_ = normalize_ble_address(peer);
    candidate_name_.fill('\0');
    const auto name = name_for(candidate_);
    const size_t name_length =
        std::min(name.size(), candidate_name_.size() - 1);
    std::copy_n(name.begin(), name_length, candidate_name_.begin());
    reset_discovery();
    attempt_failed_.store(false, std::memory_order_release);
    uint8_t own_address_type = 0;
    int rc = ble_hs_id_infer_auto(0, &own_address_type);
    ble_addr_t address{};
    address.type = candidate_.type;
    std::copy(candidate_.bytes.begin(), candidate_.bytes.end(), address.val);
    if (rc == 0) {
        rc = ble_gap_connect(own_address_type, &address, 30'000, nullptr,
                             gap_callback, this);
    }
    ESP_LOGI(kTag, "connect_start rc=%d name='%s'", rc,
             candidate_name_.data());
    if (rc != 0) {
        connecting_.store(false, std::memory_order_release);
        if (selection_delegate_ != nullptr) {
            selection_delegate_->on_attempt_failed(
                candidate_, BleSelectionFailure::ConnectionFailed,
                static_cast<uint32_t>(esp_timer_get_time() / 1'000));
        }
        resume_or_scan();
    }
}

int BleHogpClient::service_callback(uint16_t, const ble_gatt_error* error,
                                    const ble_gatt_svc* service, void* arg) {
    auto& self = *static_cast<BleHogpClient*>(arg);
    if (error->status == 0 && service != nullptr) {
        self.service_start_ = service->start_handle;
        self.service_end_ = service->end_handle;
        return 0;
    }
    if (error->status != BLE_HS_EDONE) {
        self.fail_attempt(BleSelectionFailure::ConnectionFailed,
                          "service_discovery", error->status);
        return 0;
    }
    if (self.service_start_ == 0) {
        self.fail_attempt(BleSelectionFailure::NotMouse,
                          "hid_service_missing", BLE_HS_ENOENT);
        return 0;
    }
    self.begin_characteristic_discovery();
    return 0;
}

void BleHogpClient::begin_characteristic_discovery() {
    const int rc = ble_gattc_disc_all_chrs(
        connection_handle_, service_start_, service_end_,
        characteristic_callback, this);
    if (rc != 0) {
        fail_attempt(BleSelectionFailure::ConnectionFailed,
                     "characteristic_start", rc);
    }
}

int BleHogpClient::characteristic_callback(
    uint16_t connection_handle, const ble_gatt_error* error,
    const ble_gatt_chr* characteristic, void* arg) {
    auto& self = *static_cast<BleHogpClient*>(arg);
    if (error->status == 0 && characteristic != nullptr) {
        if (self.characteristic_count_ >= self.characteristics_.size()) {
            self.fail_attempt(BleSelectionFailure::NotMouse,
                              "too_many_characteristics", BLE_HS_ENOMEM);
            return 0;
        }
        auto& saved = self.characteristics_[self.characteristic_count_++];
        saved.definition_handle = characteristic->def_handle;
        saved.value_handle = characteristic->val_handle;
        saved.uuid16 = ble_uuid_u16(&characteristic->uuid.u);
        saved.properties = characteristic->properties;
        if (saved.uuid16 == kReportMapCharacteristic) {
            self.report_map_handle_ = saved.value_handle;
        }
        return 0;
    }
    if (error->status != BLE_HS_EDONE) {
        self.fail_attempt(BleSelectionFailure::ConnectionFailed,
                          "characteristic_discovery", error->status);
        return 0;
    }
    if (self.report_map_handle_ == 0) {
        self.fail_attempt(BleSelectionFailure::NotMouse,
                          "report_map_missing", BLE_HS_ENOENT);
        return 0;
    }
    const int rc = ble_gattc_read_long(
        connection_handle, self.report_map_handle_, 0, report_map_callback,
        &self);
    if (rc != 0) {
        self.fail_attempt(BleSelectionFailure::ConnectionFailed,
                          "report_map_start", rc);
    }
    return 0;
}

int BleHogpClient::report_map_callback(uint16_t,
                                       const ble_gatt_error* error,
                                       ble_gatt_attr* attribute, void* arg) {
    auto& self = *static_cast<BleHogpClient*>(arg);
    if (error->status == 0 && attribute != nullptr &&
        attribute->om != nullptr) {
        const size_t length = OS_MBUF_PKTLEN(attribute->om);
        if (self.report_map_length_ + length > self.report_map_.size()) {
            self.fail_attempt(BleSelectionFailure::NotMouse,
                              "report_map_too_large", BLE_HS_EMSGSIZE);
            return 0;
        }
        os_mbuf_copydata(attribute->om, 0, length,
                         self.report_map_.data() + self.report_map_length_);
        self.report_map_length_ += length;
        return 0;
    }
    if (error->status != BLE_HS_EDONE) {
        self.fail_attempt(auth_required(error->status)
                              ? BleSelectionFailure::ConnectionFailed
                              : BleSelectionFailure::NotMouse,
                          "report_map_read", error->status);
        return 0;
    }
    self.finish_report_map();
    return 0;
}

void BleHogpClient::finish_report_map() {
    const auto parsed = hid_parser::parse_mouse_descriptor(
        std::span<const uint8_t>(report_map_.data(), report_map_length_));
    if (parsed.status != hid_parser::ParseStatus::Ok) {
        fail_attempt(BleSelectionFailure::NotMouse, "report_map_parse",
                     static_cast<int>(parsed.status));
        return;
    }
    mouse_layout_ = parsed.layout;
    mouse_layout_valid_ = true;
    subscribe_cursor_ = 0;
    start_next_subscription();
}

uint16_t BleHogpClient::characteristic_end(size_t index) const {
    if (index + 1 < characteristic_count_) {
        return static_cast<uint16_t>(
            characteristics_[index + 1].definition_handle - 1);
    }
    return service_end_;
}

void BleHogpClient::start_next_subscription() {
    while (subscribe_cursor_ < characteristic_count_) {
        auto& characteristic = characteristics_[subscribe_cursor_];
        if (characteristic.uuid16 != kReportCharacteristic ||
            (characteristic.properties & BLE_GATT_CHR_PROP_NOTIFY) == 0) {
            ++subscribe_cursor_;
            continue;
        }
        active_characteristic_ = subscribe_cursor_;
        characteristic.cccd_handle = 0;
        characteristic.report_reference_handle = 0;
        const int rc = ble_gattc_disc_all_dscs(
            connection_handle_, characteristic.value_handle,
            characteristic_end(subscribe_cursor_), descriptor_callback,
            this);
        if (rc == 0) return;
        ESP_LOGW(kTag, "descriptor_start handle=%u rc=%d",
                 static_cast<unsigned>(characteristic.value_handle), rc);
        ++subscribe_cursor_;
    }
    finish_validation();
}

int BleHogpClient::descriptor_callback(
    uint16_t, const ble_gatt_error* error, uint16_t,
    const ble_gatt_dsc* descriptor, void* arg) {
    auto& self = *static_cast<BleHogpClient*>(arg);
    auto& characteristic =
        self.characteristics_[self.active_characteristic_];
    if (error->status == 0 && descriptor != nullptr) {
        const uint16_t uuid = ble_uuid_u16(&descriptor->uuid.u);
        if (uuid == BLE_GATT_DSC_CLT_CFG_UUID16) {
            characteristic.cccd_handle = descriptor->handle;
        } else if (uuid == kReportReferenceDescriptor) {
            characteristic.report_reference_handle = descriptor->handle;
        }
        return 0;
    }
    if (error->status != BLE_HS_EDONE) {
        self.fail_attempt(BleSelectionFailure::ConnectionFailed,
                          "descriptor_discovery", error->status);
        return 0;
    }
    if (characteristic.cccd_handle == 0) {
        ++self.subscribe_cursor_;
        self.start_next_subscription();
        return 0;
    }
    if (characteristic.report_reference_handle != 0) {
        const int rc = ble_gattc_read(
            self.connection_handle_, characteristic.report_reference_handle,
            report_reference_callback, &self);
        if (rc == 0) return 0;
        ESP_LOGW(kTag, "report_reference_start rc=%d", rc);
    }
    characteristic.report_id = self.mouse_layout_.report_id;
    self.write_active_cccd();
    return 0;
}

int BleHogpClient::report_reference_callback(
    uint16_t, const ble_gatt_error* error, ble_gatt_attr* attribute,
    void* arg) {
    auto& self = *static_cast<BleHogpClient*>(arg);
    auto& characteristic =
        self.characteristics_[self.active_characteristic_];
    if (error->status == 0 && attribute != nullptr &&
        attribute->om != nullptr && OS_MBUF_PKTLEN(attribute->om) >= 2) {
        uint8_t reference[2]{};
        os_mbuf_copydata(attribute->om, 0, sizeof(reference), reference);
        characteristic.report_id = reference[0];
    } else {
        characteristic.report_id = self.mouse_layout_.report_id;
        ESP_LOGW(kTag, "report_reference_read status=%d", error->status);
    }
    self.write_active_cccd();
    return 0;
}

void BleHogpClient::write_active_cccd() {
    auto& characteristic = characteristics_[active_characteristic_];
    const uint8_t enable[2] = {1, 0};
    const int rc = ble_gattc_write_flat(
        connection_handle_, characteristic.cccd_handle, enable,
        sizeof(enable), subscription_callback, this);
    if (rc != 0) {
        fail_attempt(BleSelectionFailure::ConnectionFailed,
                     "subscribe_start", rc);
    }
}

int BleHogpClient::subscription_callback(uint16_t,
                                         const ble_gatt_error* error,
                                         ble_gatt_attr*, void* arg) {
    auto& self = *static_cast<BleHogpClient*>(arg);
    if (error->status != 0) {
        self.fail_attempt(BleSelectionFailure::ConnectionFailed,
                          "subscribe_write", error->status);
        return 0;
    }
    self.characteristics_[self.active_characteristic_].subscribed = true;
    ++self.subscribe_cursor_;
    self.start_next_subscription();
    return 0;
}

void BleHogpClient::finish_validation() {
    const bool subscribed = std::any_of(
        characteristics_.begin(),
        characteristics_.begin() + characteristic_count_,
        [](const Characteristic& characteristic) {
            return characteristic.subscribed;
        });
    if (!mouse_layout_valid_ || !subscribed) {
        fail_attempt(BleSelectionFailure::NotMouse, "no_mouse_input",
                     BLE_HS_ENOENT);
        return;
    }
    connecting_.store(false, std::memory_order_release);
    connected_.store(true, std::memory_order_release);
    forwarding_.store(true, std::memory_order_release);
    connections_.fetch_add(1, std::memory_order_relaxed);
    request_low_latency_interval();
    const uint64_t now_us = static_cast<uint64_t>(esp_timer_get_time());
    arbiter_.on_connected(mouse_bridge::InputSource::Ble, now_us);
    if (selection_delegate_ != nullptr) {
        selection_delegate_->on_validated_connection(
            candidate_, connected_identity().value_or(candidate_),
            candidate_name_.data(), static_cast<uint32_t>(now_us / 1'000));
    }
    ESP_LOGI(kTag, "validated name='%s' reports_ready=yes",
             candidate_name_.data());
    resume_or_scan();
}

void BleHogpClient::fail_attempt(BleSelectionFailure failure,
                                 const char* step, int status) {
    if (attempt_failed_.exchange(true, std::memory_order_acq_rel)) return;
    ESP_LOGE(kTag, "%s failed status=%d%s", step, status,
             auth_required(status) ? " auth_required" : "");
    connecting_.store(false, std::memory_order_release);
    connected_.store(false, std::memory_order_release);
    if (selection_delegate_ != nullptr) {
        selection_delegate_->on_attempt_failed(
            candidate_, failure,
            static_cast<uint32_t>(esp_timer_get_time() / 1'000));
    }
    if (connection_handle_ != BLE_HS_CONN_HANDLE_NONE) {
        (void)ble_gap_terminate(connection_handle_,
                                BLE_ERR_REM_USER_CONN_TERM);
    } else {
        resume_or_scan();
    }
}

void BleHogpClient::handle_disconnect(int reason) {
    const bool was_connected =
        connected_.exchange(false, std::memory_order_acq_rel);
    const bool failed = attempt_failed_.load(std::memory_order_acquire);
    const uint64_t now_us = static_cast<uint64_t>(esp_timer_get_time());
    ESP_LOGW(kTag, "disconnected reason=%d validated=%s", reason,
             was_connected ? "yes" : "no");
    if (was_connected) {
        arbiter_.on_disconnected(mouse_bridge::InputSource::Ble, now_us);
        if (selection_delegate_ != nullptr) {
            selection_delegate_->on_disconnected(
                static_cast<uint32_t>(now_us / 1'000));
        }
    } else if (!failed && connecting_.load() &&
               selection_delegate_ != nullptr) {
        selection_delegate_->on_attempt_failed(
            candidate_, BleSelectionFailure::ConnectionFailed,
            static_cast<uint32_t>(now_us / 1'000));
    }
    connection_handle_ = BLE_HS_CONN_HANDLE_NONE;
    connecting_.store(false, std::memory_order_release);
    negotiated_interval_units_.store(0, std::memory_order_relaxed);
    reset_discovery();
    if (pending_forget_valid_.exchange(false,
                                       std::memory_order_acq_rel)) {
        forget_peer(pending_forget_);
    }
    resume_or_scan();
}

void BleHogpClient::request_low_latency_interval() {
    ble_gap_conn_desc connection{};
    if (ble_gap_conn_find(connection_handle_, &connection) != 0) return;
    negotiated_interval_units_.store(connection.conn_itvl,
                                     std::memory_order_relaxed);
    ble_gap_upd_params params{};
    params.itvl_min = kPreferredIntervalUnits;
    params.itvl_max = kPreferredIntervalUnits;
    params.latency = 0;
    params.supervision_timeout = 200;
    const int rc = ble_gap_update_params(connection_handle_, &params);
    if (rc != 0) {
        ESP_LOGW(kTag, "interval_update current=%u rc=%d",
                 static_cast<unsigned>(connection.conn_itvl), rc);
    }
}

void BleHogpClient::decode_input(uint16_t value_handle,
                                 const os_mbuf* data) {
    if (!forwarding_.load(std::memory_order_acquire) ||
        !mouse_layout_valid_ || data == nullptr) {
        return;
    }
    const auto iterator = std::find_if(
        characteristics_.begin(),
        characteristics_.begin() + characteristic_count_,
        [value_handle](const Characteristic& characteristic) {
            return characteristic.subscribed &&
                   characteristic.value_handle == value_handle;
        });
    if (iterator == characteristics_.begin() + characteristic_count_) return;
    if (mouse_layout_.has_report_id &&
        iterator->report_id != mouse_layout_.report_id) {
        return;
    }

    std::array<uint8_t, 65> report_bytes{};
    const size_t payload_length = OS_MBUF_PKTLEN(data);
    const size_t prefix = mouse_layout_.has_report_id ? 1 : 0;
    if (payload_length + prefix > report_bytes.size()) return;
    if (prefix != 0) report_bytes[0] = iterator->report_id;
    os_mbuf_copydata(data, 0, payload_length,
                     report_bytes.data() + prefix);
    mouse_bridge::MouseReport report{};
    if (hid_parser::decode_mouse_report(
            mouse_layout_,
            std::span<const uint8_t>(report_bytes.data(),
                                     payload_length + prefix),
            report) != hid_parser::DecodeStatus::Ok) {
        return;
    }
    const uint16_t units =
        negotiated_interval_units_.load(std::memory_order_relaxed);
    report.poll_interval_us =
        units == 0 ? 0 : mouse_bridge::sanitize_poll_interval_us(
                             static_cast<uint32_t>(units) * 1'250U);
    arbiter_.on_report(mouse_bridge::InputSource::Ble, report,
                       static_cast<uint64_t>(esp_timer_get_time()));
}

void BleHogpClient::resume_or_scan() {
    forwarding_.store(true, std::memory_order_release);
    if (!synced_.load(std::memory_order_acquire) ||
        connecting_.load(std::memory_order_acquire) ||
        ble_gap_disc_active()) {
        return;
    }
    if (connection_handle_ != BLE_HS_CONN_HANDLE_NONE &&
        !portal_scanning_.load(std::memory_order_acquire)) {
        return;
    }
    uint8_t own_address_type = 0;
    if (ble_hs_id_infer_auto(0, &own_address_type) != 0) return;
    ble_gap_disc_params params{};
    params.filter_duplicates = 0;
    params.passive = 0;
    params.itvl = 0x60;
    params.window = 0x30;
    const int rc = ble_gap_disc(own_address_type, BLE_HS_FOREVER, &params,
                                gap_callback, this);
    if (rc != 0) ESP_LOGW(kTag, "scan_start rc=%d", rc);
}

void BleHogpClient::suspend_forwarding() {
    forwarding_.store(false, std::memory_order_release);
}

void BleHogpClient::clear_bond() {
    forwarding_.store(false, std::memory_order_release);
    (void)ble_gap_disc_cancel();
    if (connection_handle_ != BLE_HS_CONN_HANDLE_NONE) {
        (void)ble_gap_terminate(connection_handle_,
                                BLE_ERR_REM_USER_CONN_TERM);
    } else {
        resume_or_scan();
    }
}

void BleHogpClient::forget_peer(BlePeerAddress) {
    // Direct-GATT mode intentionally does not create NimBLE bonds. Device
    // history is owned and removed by BleDeviceManager/NVS.
}

std::optional<BlePeerAddress> BleHogpClient::connected_identity() const {
    if (connection_handle_ == BLE_HS_CONN_HANDLE_NONE) return std::nullopt;
    ble_gap_conn_desc connection{};
    if (ble_gap_conn_find(connection_handle_, &connection) != 0) {
        return candidate_;
    }
    BlePeerAddress identity{};
    identity.type =
        normalize_ble_address_type(connection.peer_id_addr.type);
    std::copy_n(connection.peer_id_addr.val, identity.bytes.size(),
                identity.bytes.begin());
    return normalize_ble_address(identity);
}

void BleHogpClient::remember_name(BlePeerAddress peer,
                                  std::string_view name) {
    peer = normalize_ble_address(peer);
    while (!name.empty() &&
           (name.back() == ' ' || name.back() == '\0')) {
        name.remove_suffix(1);
    }
    if (name.empty()) return;
    size_t index = named_peers_.size();
    for (size_t i = 0; i < named_peers_.size(); ++i) {
        if (named_peers_[i].used && named_peers_[i].address == peer) {
            index = i;
            break;
        }
    }
    if (index == named_peers_.size()) {
        index = named_peer_cursor_++ % named_peers_.size();
    }
    auto& saved = named_peers_[index];
    saved = {};
    saved.used = true;
    saved.address = peer;
    const size_t length = std::min(name.size(), saved.name.size() - 1);
    for (size_t i = 0; i < length; ++i) {
        const auto byte = static_cast<unsigned char>(name[i]);
        saved.name[i] = byte < 0x20 || byte == 0x7F ? '?' : name[i];
    }
}

std::string_view BleHogpClient::name_for(BlePeerAddress peer) const {
    peer = normalize_ble_address(peer);
    for (const auto& saved : named_peers_) {
        if (saved.used && saved.address == peer) {
            return saved.name.data();
        }
    }
    return {};
}

void BleHogpClient::reset_discovery() {
    service_start_ = 0;
    service_end_ = 0;
    report_map_handle_ = 0;
    characteristics_ = {};
    characteristic_count_ = 0;
    subscribe_cursor_ = 0;
    active_characteristic_ = kMaxCharacteristics;
    report_map_.fill(0);
    report_map_length_ = 0;
    mouse_layout_ = {};
    mouse_layout_valid_ = false;
}
