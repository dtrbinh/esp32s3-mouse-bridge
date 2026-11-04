#pragma once

#include <cstdint>

enum class BleBondResetDisposition : uint8_t {
    CloseConnectedDevice,
    RestartScanning,
};

constexpr BleBondResetDisposition plan_ble_bond_reset(bool device_connected) {
    return device_connected
               ? BleBondResetDisposition::CloseConnectedDevice
               : BleBondResetDisposition::RestartScanning;
}
