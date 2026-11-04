#pragma once

#include "ble_device_history.hpp"

class BleHistoryPersistence {
public:
    virtual ~BleHistoryPersistence() = default;
    virtual BleDeviceHistory load_history() = 0;
    virtual bool save_history(const BleDeviceHistory& history) = 0;
};
