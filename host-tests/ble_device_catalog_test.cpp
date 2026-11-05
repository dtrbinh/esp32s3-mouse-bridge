#include "ble_device_catalog.hpp"

#include <cassert>
#include <cstring>
#include <string>

namespace {

BlePeerAddress address(uint8_t tail, uint8_t type = 0) {
    return {{{1, 2, 3, 4, 5, tail}}, type};
}

BleAdvertisement advertisement(uint8_t tail, std::string_view name,
                               int8_t rssi, uint8_t type = 0) {
    return {address(tail, type), name, rssi, std::nullopt, false};
}

std::string name_of(const BleNearbyDevice& device) {
    return device.name.data();
}

}  // namespace

int main() {
    BleDeviceCatalog catalog;
    auto first = advertisement(9, {}, -70);
    first.appearance = 0x03c2;
    catalog.observe(first, 1'000);
    auto response = advertisement(9, "Freewolf X7   ", -45);
    response.advertises_hid = true;
    catalog.observe(response, 1'010);

    auto snapshot = catalog.snapshot(1'010);
    assert(snapshot.count == 1);
    assert(name_of(snapshot.devices[0]) == "Freewolf X7");
    assert(snapshot.devices[0].appearance == 0x03c2);
    assert(snapshot.devices[0].advertises_hid);
    assert(snapshot.devices[0].rssi == -45);
    assert(snapshot.devices[0].last_seen_ms == 1'010);

    const auto stable_identity = address(42);
    catalog.observe(response, stable_identity, 1'011);
    snapshot = catalog.snapshot(1'011);
    assert(snapshot.devices[0].resolved_identity == stable_identity);

    // Anonymous advertisements are retained for scan-response merging but
    // never exposed to the portal.
    catalog.observe(advertisement(12, {}, -10), 1'015);
    snapshot = catalog.snapshot(1'015);
    assert(snapshot.count == 1);
    assert(catalog.active_count(1'015) == 1);

    catalog.observe(advertisement(10, "Weak", -80), 1'020);
    catalog.observe(advertisement(11, "Strong", -20), 1'030);
    snapshot = catalog.snapshot(1'030);
    assert(snapshot.count == 3);
    assert(name_of(snapshot.devices[0]) == "Strong");
    assert(name_of(snapshot.devices[1]) == "Freewolf X7");
    assert(name_of(snapshot.devices[2]) == "Weak");

    // Address type is part of identity, even when bytes match.
    catalog.observe(advertisement(11, "Random address", -10, 1), 1'040);
    snapshot = catalog.snapshot(1'040);
    assert(snapshot.count == 4);
    assert(snapshot.devices[0].address.type == 1);

    // Stale is strictly older than 15 seconds; the boundary remains visible.
    assert(catalog.snapshot(16'010).count == 4);
    assert(catalog.snapshot(16'041).count == 0);

    catalog.clear();
    std::string hostile = "A\x01\xFF" "B";
    catalog.observe(advertisement(1, hostile, -30), 100);
    std::string long_name(40, 'x');
    catalog.observe(advertisement(2, long_name, -31), 101);
    snapshot = catalog.snapshot(101);
    assert(name_of(snapshot.devices[0]) == "A??B");
    assert(name_of(snapshot.devices[1]).size() == 32);

    char formatted[18]{};
    assert(format_ble_address(address(0xA6), formatted));
    assert(std::strcmp(formatted, "A6:05:04:03:02:01") == 0);
    char too_small[17]{};
    assert(!format_ble_address(address(0xA6), too_small));

    catalog.clear();
    for (uint8_t i = 0; i < BleDeviceCatalog::kCapacity; ++i) {
        catalog.observe(advertisement(i, "device", static_cast<int8_t>(-90 + i)),
                        1'000 + i);
    }
    assert(catalog.snapshot(1'100).count == BleDeviceCatalog::kCapacity);
    catalog.observe(advertisement(200, "new", -40), 1'200);
    snapshot = catalog.snapshot(1'200);
    assert(snapshot.count == BleDeviceCatalog::kCapacity);
    bool found_weakest = false;
    bool found_new = false;
    for (size_t i = 0; i < snapshot.count; ++i) {
        found_weakest |= snapshot.devices[i].address == address(0);
        found_new |= snapshot.devices[i].address == address(200);
    }
    assert(!found_weakest);
    assert(found_new);

    // A stale slot is preferred over evicting a current weak device.
    catalog.observe(advertisement(201, "after expiry", -127), 20'000);
    snapshot = catalog.snapshot(20'000);
    assert(snapshot.count == 1);
    assert(snapshot.devices[0].address == address(201));
}
