#include "ble_device_store.hpp"

#include "nvs.h"

#include <array>
#include <span>

namespace {
constexpr char kNamespace[] = "mb_ble";
constexpr char kHistoryKey[] = "history";
constexpr size_t kMaximumBlobSize = 512;
}

BleDeviceHistory BleDeviceStore::load() const {
    nvs_handle_t handle{};
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) return {};
    size_t size = 0;
    esp_err_t result = nvs_get_blob(handle, kHistoryKey, nullptr, &size);
    if (result != ESP_OK || size == 0 || size > kMaximumBlobSize) {
        nvs_close(handle);
        return {};
    }
    std::array<uint8_t, kMaximumBlobSize> bytes{};
    result = nvs_get_blob(handle, kHistoryKey, bytes.data(), &size);
    nvs_close(handle);
    if (result != ESP_OK) return {};
    const auto decoded =
        decode_ble_history(std::span<const uint8_t>(bytes.data(), size));
    return decoded.value_or(BleDeviceHistory{});
}

esp_err_t BleDeviceStore::save(const BleDeviceHistory& history) const {
    std::array<uint8_t, kMaximumBlobSize> bytes{};
    const size_t size = encode_ble_history(history, bytes);
    if (size == 0) return ESP_ERR_INVALID_SIZE;
    nvs_handle_t handle{};
    esp_err_t result = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (result != ESP_OK) return result;
    result = nvs_set_blob(handle, kHistoryKey, bytes.data(), size);
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
    return result;
}
