#include "portal_scan_policy.hpp"

#include <cassert>

int main() {
    assert(portal_scan_policy(false, 0) ==
           PortalScanPolicy::BackgroundReconnect);
    assert(portal_scan_policy(true, 0) ==
           PortalScanPolicy::PausedForPortalBeacon);
    assert(portal_scan_policy(true, 1) ==
           PortalScanPolicy::PortalDiscovery);
    assert(portal_scan_policy(true, 4) ==
           PortalScanPolicy::PortalDiscovery);
}
