#include "ble_bond_reset.hpp"

#include <cassert>

int main() {
    assert(plan_ble_bond_reset(true) ==
           BleBondResetDisposition::CloseConnectedDevice);
    assert(plan_ble_bond_reset(false) ==
           BleBondResetDisposition::RestartScanning);
}
