#include "esp_hidh.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

extern "C" void ble_store_config_init(void);

namespace {
constexpr char kTag[] = "X7_DEBUG";
constexpr std::string_view kTargetName = "X7";
constexpr uint16_t kHidService = 0x1812;

struct Candidate {
    std::array<uint8_t, 6> address{};
    uint8_t address_type{};
};

QueueHandle_t candidate_queue;
std::atomic<bool> connecting{};
std::atomic<esp_hidh_dev_t*> device{};

void print_address(const uint8_t* address, char* output, size_t size) {
    std::snprintf(output, size, "%02X:%02X:%02X:%02X:%02X:%02X",
                  address[5], address[4], address[3], address[2], address[1],
                  address[0]);
}

void start_scan();

int scan_callback(ble_gap_event* event, void*) {
    if (event->type == BLE_GAP_EVENT_DISC) {
        ble_hs_adv_fields fields{};
        if (ble_hs_adv_parse_fields(&fields, event->disc.data,
                                    event->disc.length_data) != 0) {
            return 0;
        }

        bool advertises_hid = false;
        for (int i = 0; i < fields.num_uuids16; ++i) {
            if (ble_uuid_u16(&fields.uuids16[i].u) == kHidService) {
                advertises_hid = true;
                break;
            }
        }

        const std::string_view name =
            fields.name != nullptr
                ? std::string_view(reinterpret_cast<const char*>(fields.name),
                                   fields.name_len)
                : std::string_view{};
        if (!name.empty() || advertises_hid) {
            char address[18]{};
            print_address(event->disc.addr.val, address, sizeof(address));
            ESP_LOGI(kTag, "SCAN addr=%s type=%u rssi=%d hid=%s name='%.*s'",
                     address, static_cast<unsigned>(event->disc.addr.type),
                     static_cast<int>(event->disc.rssi),
                     advertises_hid ? "yes" : "no",
                     static_cast<int>(name.size()), name.data());
        }

        if (name.find(kTargetName) == std::string_view::npos ||
            connecting.exchange(true) || device.load() != nullptr) {
            return 0;
        }

        Candidate candidate{};
        std::copy_n(event->disc.addr.val, candidate.address.size(),
                    candidate.address.begin());
        candidate.address_type = event->disc.addr.type;
        char address[18]{};
        print_address(candidate.address.data(), address, sizeof(address));
        ESP_LOGI(kTag, "MATCH name='%.*s' addr=%s; connecting",
                 static_cast<int>(name.size()), name.data(), address);
        (void)ble_gap_disc_cancel();
        if (xQueueSend(candidate_queue, &candidate, 0) != pdTRUE) {
            connecting.store(false);
            ESP_LOGE(kTag, "candidate queue full");
        }
    } else if (event->type == BLE_GAP_EVENT_DISC_COMPLETE &&
               !connecting.load() && device.load() == nullptr) {
        start_scan();
    }
    return 0;
}

void start_scan() {
    if (connecting.load() || device.load() != nullptr ||
        ble_gap_disc_active()) {
        return;
    }
    uint8_t own_address_type = 0;
    const int address_result =
        ble_hs_id_infer_auto(0, &own_address_type);
    if (address_result != 0) {
        ESP_LOGE(kTag, "cannot infer own address type rc=%d", address_result);
        return;
    }

    ble_gap_disc_params params{};
    params.passive = 0;
    params.filter_duplicates = 0;
    params.itvl = 0x50;
    params.window = 0x50;
    const int result = ble_gap_disc(own_address_type, BLE_HS_FOREVER, &params,
                                    scan_callback, nullptr);
    ESP_LOGI(kTag, "SCAN_START target='%.*s' rc=%d",
             static_cast<int>(kTargetName.size()), kTargetName.data(), result);
}

void host_task(void*) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void on_sync() { start_scan(); }

void hidh_callback(void*, esp_event_base_t, int32_t event_id,
                   void* event_data) {
    if (event_data == nullptr) return;
    const auto event = static_cast<esp_hidh_event_t>(event_id);
    auto& data = *static_cast<esp_hidh_event_data_t*>(event_data);

    switch (event) {
    case ESP_HIDH_OPEN_EVENT: {
        connecting.store(false);
        if (data.open.status != ESP_OK || data.open.dev == nullptr) {
            ESP_LOGE(kTag, "OPEN_FAILED status=%s",
                     esp_err_to_name(data.open.status));
            device.store(nullptr);
            start_scan();
            break;
        }
        device.store(data.open.dev);
        const uint8_t* address = esp_hidh_dev_bda_get(data.open.dev);
        char address_text[18] = "unknown";
        if (address != nullptr) {
            print_address(address, address_text, sizeof(address_text));
        }
        const char* name = esp_hidh_dev_name_get(data.open.dev);
        ESP_LOGI(kTag, "CONNECTED addr=%s name='%s'", address_text,
                 name != nullptr ? name : "");
        esp_hidh_dev_dump(data.open.dev, stdout);
        break;
    }
    case ESP_HIDH_INPUT_EVENT: {
        ESP_LOGI(kTag,
                 "INPUT t_us=%lld usage=%s map=%u id=%u len=%u",
                 static_cast<long long>(esp_timer_get_time()),
                 esp_hid_usage_str(data.input.usage),
                 static_cast<unsigned>(data.input.map_index),
                 static_cast<unsigned>(data.input.report_id),
                 static_cast<unsigned>(data.input.length));
        if (data.input.data != nullptr && data.input.length != 0) {
            ESP_LOG_BUFFER_HEX_LEVEL(kTag, data.input.data, data.input.length,
                                     ESP_LOG_INFO);
        }
        break;
    }
    case ESP_HIDH_BATTERY_EVENT:
        ESP_LOGI(kTag, "BATTERY level=%u%% status=%s",
                 static_cast<unsigned>(data.battery.level),
                 esp_err_to_name(data.battery.status));
        break;
    case ESP_HIDH_CLOSE_EVENT:
        ESP_LOGW(kTag, "DISCONNECTED reason=%d", data.close.reason);
        if (data.close.dev != nullptr) {
            (void)esp_hidh_dev_free(data.close.dev);
        }
        device.store(nullptr);
        connecting.store(false);
        start_scan();
        break;
    default:
        ESP_LOGI(kTag, "HID_EVENT id=%ld", static_cast<long>(event_id));
        break;
    }
}

void connect_task(void*) {
    Candidate candidate{};
    while (true) {
        if (xQueueReceive(candidate_queue, &candidate, portMAX_DELAY) !=
            pdTRUE) {
            continue;
        }
        auto* opened = esp_hidh_dev_open(candidate.address.data(),
                                         ESP_HID_TRANSPORT_BLE,
                                         candidate.address_type);
        device.store(opened);
        if (opened == nullptr) {
            ESP_LOGE(kTag, "esp_hidh_dev_open returned null");
            connecting.store(false);
            start_scan();
        }
    }
}
}  // namespace

extern "C" void app_main() {
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES ||
        result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(result);

    candidate_queue = xQueueCreate(1, sizeof(Candidate));
    ESP_ERROR_CHECK(candidate_queue != nullptr ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(connect_task, "x7_connect", 4096, nullptr, 3,
                                nullptr) == pdPASS
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);

    ESP_ERROR_CHECK(nimble_port_init());
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_our_key_dist =
        BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist =
        BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_store_config_init();

    const esp_hidh_config_t hidh_config{
        .callback = hidh_callback,
        .event_stack_size = 4096,
        .callback_arg = nullptr,
    };
    ESP_ERROR_CHECK(esp_hidh_init(&hidh_config));
    nimble_port_freertos_init(host_task);
    ESP_LOGI(kTag, "READY target='%.*s'", static_cast<int>(kTargetName.size()),
             kTargetName.data());
}
