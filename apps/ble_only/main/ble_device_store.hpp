#pragma once

#include "ble_device_history.hpp"
#include "ble_history_persistence.hpp"

#include "esp_err.h"

class BleDeviceStore final : public BleHistoryPersistence {
public:
    [[nodiscard]] BleDeviceHistory load() const;
    esp_err_t save(const BleDeviceHistory& history) const;
    BleDeviceHistory load_history() override { return load(); }
    bool save_history(const BleDeviceHistory& history) override {
        return save(history) == ESP_OK;
    }
};
