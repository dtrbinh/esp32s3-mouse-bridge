#pragma once

#include "ble_device_catalog.hpp"
#include "ble_device_history.hpp"
#include "ble_setup_state.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

struct PortalView {
    BleSetupSnapshot setup{};
    BleSavedSnapshot saved{};
    BleNearbySnapshot nearby{};
};

std::optional<size_t> write_portal_json(const PortalView& view,
                                        std::span<char> output);
std::optional<BlePeerAddress> parse_peer_form(std::string_view body);
