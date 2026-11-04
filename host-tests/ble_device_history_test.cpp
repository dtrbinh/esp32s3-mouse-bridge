#include "ble_device_history.hpp"

#include <array>
#include <cassert>
#include <cstring>
#include <span>
#include <string>

namespace {

BlePeerAddress address(uint8_t tail, uint8_t type = 0) {
    return {{{1, 2, 3, 4, 5, tail}}, type};
}

std::string name_of(const BleSavedDevice& device) {
    return device.name.data();
}

}  // namespace

int main() {
    BleDeviceHistory history;
    assert(history.count() == 0);
    assert(!history.selected().has_value());

    history.record_success(address(1), "Mouse A");
    history.record_success(address(2), "Mouse B");
    assert(history.count() == 2);
    assert(history.selected() == address(2));
    auto saved = history.snapshot(address(1));
    assert(saved.count == 2);
    assert(saved.devices[0].identity == address(1));
    assert(saved.devices[0].connected);
    assert(saved.devices[1].identity == address(2));

    history.record_success(address(1), "Mouse A renamed");
    assert(history.selected() == address(1));
    saved = history.snapshot(std::nullopt);
    assert(saved.devices[0].identity == address(1));
    assert(name_of(saved.devices[0]) == "Mouse A renamed");

    history.disconnect_without_forget();
    assert(!history.selected().has_value());
    assert(history.count() == 2);
    history.select(address(2));
    assert(history.selected() == address(2));
    assert(history.forget(address(2)));
    assert(!history.selected().has_value());
    assert(history.count() == 1);
    assert(!history.forget(address(99)));

    BleDeviceHistory capacity;
    for (uint8_t i = 0; i < BleDeviceHistory::kCapacity; ++i) {
        capacity.record_success(address(i), "saved");
    }
    capacity.record_success(address(99), "new");
    assert(capacity.count() == BleDeviceHistory::kCapacity);
    saved = capacity.snapshot(std::nullopt);
    bool found_oldest = false;
    bool found_new = false;
    for (size_t i = 0; i < saved.count; ++i) {
        found_oldest |= saved.devices[i].identity == address(0);
        found_new |= saved.devices[i].identity == address(99);
    }
    assert(!found_oldest);
    assert(found_new);

    std::array<uint8_t, 512> encoded{};
    const size_t encoded_size = encode_ble_history(capacity, encoded);
    assert(encoded_size > 0 && encoded_size < encoded.size());
    const auto decoded = decode_ble_history(
        std::span<const uint8_t>(encoded.data(), encoded_size));
    assert(decoded.has_value());
    assert(decoded->count() == capacity.count());
    assert(decoded->selected() == capacity.selected());
    auto decoded_saved = decoded->snapshot(std::nullopt);
    for (size_t i = 0; i < saved.count; ++i) {
        assert(decoded_saved.devices[i].identity == saved.devices[i].identity);
        assert(name_of(decoded_saved.devices[i]) == name_of(saved.devices[i]));
    }

    std::array<uint8_t, 8> tiny{};
    assert(encode_ble_history(capacity, tiny) == 0);
    assert(!decode_ble_history({}).has_value());
    assert(!decode_ble_history(
                std::span<const uint8_t>(encoded.data(), encoded_size - 1))
                .has_value());

    auto corrupted = encoded;
    corrupted[0] ^= 0x01;  // magic
    assert(!decode_ble_history(
                std::span<const uint8_t>(corrupted.data(), encoded_size))
                .has_value());
    corrupted = encoded;
    corrupted[4] = 2;  // version
    assert(!decode_ble_history(
                std::span<const uint8_t>(corrupted.data(), encoded_size))
                .has_value());
    corrupted = encoded;
    corrupted[5] = 9;  // count
    assert(!decode_ble_history(
                std::span<const uint8_t>(corrupted.data(), encoded_size))
                .has_value());
    corrupted = encoded;
    corrupted[encoded_size - 1] ^= 0x80;  // CRC
    assert(!decode_ble_history(
                std::span<const uint8_t>(corrupted.data(), encoded_size))
                .has_value());
}
