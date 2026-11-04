#include "esp_log.h"
#include "esp_timer.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_sm.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "os/os_mbuf.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <string_view>

namespace {
constexpr char kTag[] = "X7_GATT";
constexpr std::string_view kTargetName = "X7";
constexpr uint16_t kHidService = 0x1812;
constexpr uint16_t kReportMap = 0x2A4B;
constexpr uint16_t kReport = 0x2A4D;
constexpr size_t kMaxCharacteristics = 32;
const ble_uuid16_t kHidServiceUuid = BLE_UUID16_INIT(kHidService);

struct Characteristic {
    uint16_t definition_handle{};
    uint16_t value_handle{};
    uint16_t uuid16{};
    uint8_t properties{};
};

struct State {
    bool connecting{};
    uint16_t connection_handle{BLE_HS_CONN_HANDLE_NONE};
    uint16_t service_start{};
    uint16_t service_end{};
    uint16_t report_map_handle{};
    std::array<Characteristic, kMaxCharacteristics> characteristics{};
    size_t characteristic_count{};
    size_t subscribe_cursor{};
    size_t active_report{kMaxCharacteristics};
    uint16_t active_cccd{};
    size_t report_map_bytes{};
} state;

void start_scan();
void start_next_subscription();
int gap_callback(ble_gap_event* event, void*);

void print_address(const uint8_t* address, char* output, size_t size) {
    std::snprintf(output, size, "%02X:%02X:%02X:%02X:%02X:%02X",
                  address[5], address[4], address[3], address[2], address[1],
                  address[0]);
}

void log_mbuf(const char* label, const os_mbuf* mbuf) {
    const uint16_t total = OS_MBUF_PKTLEN(mbuf);
    std::array<uint8_t, 256> bytes{};
    const uint16_t shown = std::min<uint16_t>(total, bytes.size());
    if (shown != 0) os_mbuf_copydata(mbuf, 0, shown, bytes.data());
    ESP_LOGI(kTag, "%s len=%u%s", label, static_cast<unsigned>(total),
             total > shown ? " (first 256 bytes)" : "");
    if (shown != 0) {
        ESP_LOG_BUFFER_HEX_LEVEL(kTag, bytes.data(), shown, ESP_LOG_INFO);
    }
}

void reset_link() {
    state.connection_handle = BLE_HS_CONN_HANDLE_NONE;
    state.service_start = 0;
    state.service_end = 0;
    state.report_map_handle = 0;
    state.characteristic_count = 0;
    state.subscribe_cursor = 0;
    state.active_report = kMaxCharacteristics;
    state.active_cccd = 0;
    state.report_map_bytes = 0;
}

void log_failure(const char* step, int status) {
    const bool auth_required =
        status == BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_AUTHEN) ||
        status == BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_AUTHOR) ||
        status == BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_ENC);
    ESP_LOGE(kTag, "%s_FAILED status=%d%s", step, status,
             auth_required ? " AUTH_REQUIRED" : "");
}

int subscription_write_callback(uint16_t, const ble_gatt_error* error,
                                ble_gatt_attr*, void*) {
    if (error->status != 0) {
        log_failure("SUBSCRIBE_WRITE", error->status);
    } else {
        const auto& chr = state.characteristics[state.active_report];
        ESP_LOGI(kTag, "SUBSCRIBED value_handle=%u cccd_handle=%u",
                 static_cast<unsigned>(chr.value_handle),
                 static_cast<unsigned>(state.active_cccd));
    }
    ++state.subscribe_cursor;
    start_next_subscription();
    return 0;
}

int descriptor_callback(uint16_t connection_handle,
                        const ble_gatt_error* error, uint16_t,
                        const ble_gatt_dsc* descriptor, void*) {
    if (error->status == 0 && descriptor != nullptr) {
        char uuid[BLE_UUID_STR_LEN]{};
        ble_uuid_to_str(&descriptor->uuid.u, uuid);
        ESP_LOGI(kTag, "DSC handle=%u uuid=%s",
                 static_cast<unsigned>(descriptor->handle), uuid);
        if (ble_uuid_u16(&descriptor->uuid.u) ==
            BLE_GATT_DSC_CLT_CFG_UUID16) {
            state.active_cccd = descriptor->handle;
        }
        return 0;
    }
    if (error->status != BLE_HS_EDONE) {
        log_failure("DESCRIPTOR_DISCOVERY", error->status);
        ++state.subscribe_cursor;
        start_next_subscription();
        return 0;
    }
    if (state.active_cccd == 0) {
        ESP_LOGW(kTag, "NO_CCCD value_handle=%u",
                 static_cast<unsigned>(
                     state.characteristics[state.active_report].value_handle));
        ++state.subscribe_cursor;
        start_next_subscription();
        return 0;
    }
    const uint8_t enable[2] = {1, 0};
    const int rc = ble_gattc_write_flat(
        connection_handle, state.active_cccd, enable, sizeof(enable),
        subscription_write_callback, nullptr);
    ESP_LOGI(kTag, "SUBSCRIBE_START cccd_handle=%u rc=%d",
             static_cast<unsigned>(state.active_cccd), rc);
    if (rc != 0) {
        ++state.subscribe_cursor;
        start_next_subscription();
    }
    return 0;
}

void start_next_subscription() {
    while (state.subscribe_cursor < state.characteristic_count) {
        const size_t index = state.subscribe_cursor;
        const auto& chr = state.characteristics[index];
        if (chr.uuid16 != kReport ||
            (chr.properties & BLE_GATT_CHR_PROP_NOTIFY) == 0) {
            ++state.subscribe_cursor;
            continue;
        }
        uint16_t end_handle = state.service_end;
        for (size_t i = index + 1; i < state.characteristic_count; ++i) {
            if (state.characteristics[i].definition_handle >
                chr.definition_handle) {
                end_handle =
                    state.characteristics[i].definition_handle - 1;
                break;
            }
        }
        state.active_report = index;
        state.active_cccd = 0;
        const int rc = ble_gattc_disc_all_dscs(
            state.connection_handle, chr.value_handle, end_handle,
            descriptor_callback, nullptr);
        ESP_LOGI(kTag,
                 "DESCRIPTOR_DISCOVERY_START value_handle=%u end=%u rc=%d",
                 static_cast<unsigned>(chr.value_handle),
                 static_cast<unsigned>(end_handle), rc);
        if (rc == 0) return;
        ++state.subscribe_cursor;
    }
    ESP_LOGI(kTag,
             "READY_FOR_INPUT subscribed_reports_complete; move the mouse");
}

int report_map_callback(uint16_t, const ble_gatt_error* error,
                        ble_gatt_attr* attr, void*) {
    if (error->status == 0 && attr != nullptr && attr->om != nullptr) {
        state.report_map_bytes += OS_MBUF_PKTLEN(attr->om);
        log_mbuf("REPORT_MAP_FRAGMENT", attr->om);
        return 0;
    }
    if (error->status != BLE_HS_EDONE) {
        log_failure("REPORT_MAP_READ", error->status);
    } else {
        ESP_LOGI(kTag, "REPORT_MAP_COMPLETE bytes=%u",
                 static_cast<unsigned>(state.report_map_bytes));
    }
    start_next_subscription();
    return 0;
}

int characteristic_callback(uint16_t connection_handle,
                            const ble_gatt_error* error,
                            const ble_gatt_chr* chr, void*) {
    if (error->status == 0 && chr != nullptr) {
        char uuid[BLE_UUID_STR_LEN]{};
        ble_uuid_to_str(&chr->uuid.u, uuid);
        ESP_LOGI(kTag, "CHR def=%u val=%u props=0x%02x uuid=%s",
                 static_cast<unsigned>(chr->def_handle),
                 static_cast<unsigned>(chr->val_handle),
                 static_cast<unsigned>(chr->properties), uuid);
        if (state.characteristic_count >= state.characteristics.size()) {
            ESP_LOGE(kTag, "too many HID characteristics");
            return 0;
        }
        auto& saved = state.characteristics[state.characteristic_count++];
        saved = {chr->def_handle, chr->val_handle,
                 ble_uuid_u16(&chr->uuid.u), chr->properties};
        if (saved.uuid16 == kReportMap) {
            state.report_map_handle = saved.value_handle;
        }
        return 0;
    }
    if (error->status != BLE_HS_EDONE) {
        log_failure("CHARACTERISTIC_DISCOVERY", error->status);
        return 0;
    }
    ESP_LOGI(kTag, "CHARACTERISTIC_DISCOVERY_COMPLETE count=%u map_handle=%u",
             static_cast<unsigned>(state.characteristic_count),
             static_cast<unsigned>(state.report_map_handle));
    if (state.report_map_handle == 0) {
        ESP_LOGW(kTag, "NO_REPORT_MAP");
        start_next_subscription();
        return 0;
    }
    const int rc = ble_gattc_read_long(connection_handle,
                                       state.report_map_handle, 0,
                                       report_map_callback, nullptr);
    ESP_LOGI(kTag, "REPORT_MAP_READ_START handle=%u rc=%d",
             static_cast<unsigned>(state.report_map_handle), rc);
    if (rc != 0) start_next_subscription();
    return 0;
}

int service_callback(uint16_t connection_handle,
                     const ble_gatt_error* error, const ble_gatt_svc* service,
                     void*) {
    if (error->status == 0 && service != nullptr) {
        state.service_start = service->start_handle;
        state.service_end = service->end_handle;
        ESP_LOGI(kTag, "HID_SERVICE start=%u end=%u",
                 static_cast<unsigned>(state.service_start),
                 static_cast<unsigned>(state.service_end));
        return 0;
    }
    if (error->status != BLE_HS_EDONE) {
        log_failure("SERVICE_DISCOVERY", error->status);
        return 0;
    }
    if (state.service_start == 0) {
        ESP_LOGE(kTag, "HID_SERVICE_NOT_FOUND");
        return 0;
    }
    const int rc = ble_gattc_disc_all_chrs(
        connection_handle, state.service_start, state.service_end,
        characteristic_callback, nullptr);
    ESP_LOGI(kTag, "CHARACTERISTIC_DISCOVERY_START rc=%d", rc);
    return 0;
}

int gap_callback(ble_gap_event* event, void*) {
    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        ble_hs_adv_fields fields{};
        if (ble_hs_adv_parse_fields(&fields, event->disc.data,
                                    event->disc.length_data) != 0) return 0;
        const std::string_view name =
            fields.name != nullptr
                ? std::string_view(reinterpret_cast<const char*>(fields.name),
                                   fields.name_len)
                : std::string_view{};
        if (name.empty()) return 0;
        char address[18]{};
        print_address(event->disc.addr.val, address, sizeof(address));
        ESP_LOGI(kTag, "SCAN addr=%s rssi=%d name='%.*s'", address,
                 static_cast<int>(event->disc.rssi),
                 static_cast<int>(name.size()), name.data());
        if (name.find(kTargetName) == std::string_view::npos ||
            state.connecting ||
            state.connection_handle != BLE_HS_CONN_HANDLE_NONE) return 0;
        state.connecting = true;
        (void)ble_gap_disc_cancel();
        uint8_t own_address_type = 0;
        int rc = ble_hs_id_infer_auto(0, &own_address_type);
        if (rc == 0) {
            rc = ble_gap_connect(own_address_type, &event->disc.addr, 30000,
                                 nullptr, gap_callback, nullptr);
        }
        ESP_LOGI(kTag,
                 "MATCH name='%.*s' addr=%s; LINK_CONNECT_START rc=%d",
                 static_cast<int>(name.size()), name.data(), address, rc);
        if (rc != 0) {
            state.connecting = false;
            start_scan();
        }
        return 0;
    }
    case BLE_GAP_EVENT_CONNECT:
        state.connecting = false;
        if (event->connect.status != 0) {
            ESP_LOGE(kTag, "LINK_CONNECT_FAILED status=%d",
                     event->connect.status);
            start_scan();
            return 0;
        }
        reset_link();
        state.connection_handle = event->connect.conn_handle;
        ESP_LOGI(kTag,
                 "LINK_CONNECTED handle=%u security=disabled mtu_exchange=skipped",
                 static_cast<unsigned>(state.connection_handle));
        {
            const int rc = ble_gattc_disc_svc_by_uuid(
                state.connection_handle, &kHidServiceUuid.u,
                service_callback, nullptr);
            ESP_LOGI(kTag, "SERVICE_DISCOVERY_START uuid=0x1812 rc=%d", rc);
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGW(kTag, "LINK_DISCONNECTED reason=%d",
                 event->disconnect.reason);
        reset_link();
        state.connecting = false;
        start_scan();
        return 0;
    case BLE_GAP_EVENT_NOTIFY_RX:
        ESP_LOGI(kTag, "INPUT t_us=%lld handle=%u indication=%u",
                 static_cast<long long>(esp_timer_get_time()),
                 static_cast<unsigned>(event->notify_rx.attr_handle),
                 static_cast<unsigned>(event->notify_rx.indication));
        log_mbuf("INPUT_REPORT", event->notify_rx.om);
        return 0;
    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(kTag, "MTU_CHANGED handle=%u mtu=%u",
                 static_cast<unsigned>(event->mtu.conn_handle),
                 static_cast<unsigned>(event->mtu.value));
        return 0;
    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGW(kTag, "UNEXPECTED_ENCRYPTION_CHANGE status=%d",
                 event->enc_change.status);
        return 0;
    case BLE_GAP_EVENT_DISC_COMPLETE:
        if (!state.connecting &&
            state.connection_handle == BLE_HS_CONN_HANDLE_NONE) start_scan();
        return 0;
    default:
        return 0;
    }
}

void start_scan() {
    if (state.connecting ||
        state.connection_handle != BLE_HS_CONN_HANDLE_NONE ||
        ble_gap_disc_active()) return;
    uint8_t own_address_type = 0;
    const int address_rc = ble_hs_id_infer_auto(0, &own_address_type);
    if (address_rc != 0) {
        ESP_LOGE(kTag, "cannot infer own address type rc=%d", address_rc);
        return;
    }
    ble_gap_disc_params params{};
    params.passive = 0;
    params.filter_duplicates = 0;
    params.itvl = 0x50;
    params.window = 0x50;
    const int rc = ble_gap_disc(own_address_type, BLE_HS_FOREVER, &params,
                                gap_callback, nullptr);
    ESP_LOGI(kTag, "SCAN_START target='%.*s' rc=%d",
             static_cast<int>(kTargetName.size()), kTargetName.data(), rc);
}

void host_task(void*) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void on_sync() { start_scan(); }
}  // namespace

extern "C" void app_main() {
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES ||
        result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(result);
    ESP_ERROR_CHECK(nimble_port_init());
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 0;
    ble_hs_cfg.sm_sc = 0;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_our_key_dist = 0;
    ble_hs_cfg.sm_their_key_dist = 0;
    nimble_port_freertos_init(host_task);
    ESP_LOGI(kTag,
             "READY target='%.*s' direct_gatt=yes bonding=off encryption=off",
             static_cast<int>(kTargetName.size()), kTargetName.data());
}
