#pragma once

#include <cstddef>
#include <cstdint>

enum class PortalScanPolicy : uint8_t {
    BackgroundReconnect,
    PausedForPortalBeacon,
    PortalDiscovery,
};

constexpr PortalScanPolicy portal_scan_policy(bool portal_active,
                                              size_t station_count) {
    if (!portal_active) return PortalScanPolicy::BackgroundReconnect;
    return station_count == 0 ? PortalScanPolicy::PausedForPortalBeacon
                              : PortalScanPolicy::PortalDiscovery;
}
