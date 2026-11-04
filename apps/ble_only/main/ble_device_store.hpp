#pragma once

#include "ble_device_history.hpp"

#include "esp_err.h"

class BleDeviceStore {
public:
    [[nodiscard]] BleDeviceHistory load() const;
    esp_err_t save(const BleDeviceHistory& history) const;
};
