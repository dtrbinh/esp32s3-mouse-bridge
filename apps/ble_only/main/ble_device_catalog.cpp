#include "ble_device_catalog.hpp"

#include <algorithm>
#include <cstdio>

namespace {

bool is_stale(uint32_t now_ms, uint32_t last_seen_ms) {
    return now_ms - last_seen_ms > BleDeviceCatalog::kStaleAfterMs;
}

size_t utf8_sequence_length(unsigned char lead) {
    if (lead < 0x80) return 1;
    if (lead >= 0xC2 && lead <= 0xDF) return 2;
    if (lead >= 0xE0 && lead <= 0xEF) return 3;
    if (lead >= 0xF0 && lead <= 0xF4) return 4;
    return 0;
}

bool valid_utf8_sequence(std::string_view input, size_t offset, size_t length) {
    if (offset + length > input.size()) return false;
    const auto lead = static_cast<unsigned char>(input[offset]);
    for (size_t i = 1; i < length; ++i) {
        const auto byte = static_cast<unsigned char>(input[offset + i]);
        if ((byte & 0xC0) != 0x80) return false;
    }
    if (length == 3) {
        const auto second = static_cast<unsigned char>(input[offset + 1]);
        if ((lead == 0xE0 && second < 0xA0) ||
            (lead == 0xED && second >= 0xA0)) {
            return false;
        }
    }
    if (length == 4) {
        const auto second = static_cast<unsigned char>(input[offset + 1]);
        if ((lead == 0xF0 && second < 0x90) ||
            (lead == 0xF4 && second >= 0x90)) {
            return false;
        }
    }
    return true;
}

void copy_display_name(std::string_view input, std::array<char, 33>& output) {
    output.fill('\0');
    size_t source = 0;
    size_t destination = 0;
    while (source < input.size() && destination < output.size() - 1) {
        const auto lead = static_cast<unsigned char>(input[source]);
        if (lead < 0x20 || lead == 0x7F) {
            output[destination++] = '?';
            ++source;
            continue;
        }
        const size_t length = utf8_sequence_length(lead);
        if (length == 0 || !valid_utf8_sequence(input, source, length)) {
            output[destination++] = '?';
            ++source;
            continue;
        }
        if (destination + length > output.size() - 1) break;
        for (size_t i = 0; i < length; ++i) {
            output[destination++] = input[source++];
        }
    }
}

bool address_before(const BlePeerAddress& left, const BlePeerAddress& right) {
    if (left.type != right.type) return left.type < right.type;
    return left.bytes < right.bytes;
}

}  // namespace

bool format_ble_address(const BlePeerAddress& address,
                        std::span<char> destination) {
    if (destination.size() < 18) return false;
    const int written = std::snprintf(
        destination.data(), destination.size(), "%02X:%02X:%02X:%02X:%02X:%02X",
        address.bytes[5], address.bytes[4], address.bytes[3], address.bytes[2],
        address.bytes[1], address.bytes[0]);
    return written == 17;
}

void BleDeviceCatalog::observe(const BleAdvertisement& advertisement,
                               uint32_t now_ms) {
    size_t index = count_;
    for (size_t i = 0; i < count_; ++i) {
        if (devices_[i].address == advertisement.address) {
            index = i;
            break;
        }
    }

    if (index == count_) {
        if (count_ < devices_.size()) {
            ++count_;
        } else {
            index = 0;
            bool chose_stale = is_stale(now_ms, devices_[0].last_seen_ms);
            for (size_t i = 1; i < count_; ++i) {
                const bool candidate_stale =
                    is_stale(now_ms, devices_[i].last_seen_ms);
                if (candidate_stale != chose_stale) {
                    if (candidate_stale) {
                        index = i;
                        chose_stale = true;
                    }
                    continue;
                }
                if (chose_stale) {
                    if (now_ms - devices_[i].last_seen_ms >
                        now_ms - devices_[index].last_seen_ms) {
                        index = i;
                    }
                } else if (devices_[i].rssi < devices_[index].rssi ||
                           (devices_[i].rssi == devices_[index].rssi &&
                            devices_[i].last_seen_ms <
                                devices_[index].last_seen_ms)) {
                    index = i;
                }
            }
            devices_[index] = {};
        }
        devices_[index].address = advertisement.address;
    }

    auto& device = devices_[index];
    if (!advertisement.name.empty()) {
        copy_display_name(advertisement.name, device.name);
    }
    device.rssi = advertisement.rssi;
    if (advertisement.appearance.has_value()) {
        device.appearance = advertisement.appearance;
    }
    device.advertises_hid |= advertisement.advertises_hid;
    device.last_seen_ms = now_ms;
}

BleNearbySnapshot BleDeviceCatalog::snapshot(uint32_t now_ms) const {
    BleNearbySnapshot result{};
    for (size_t i = 0; i < count_; ++i) {
        if (!is_stale(now_ms, devices_[i].last_seen_ms)) {
            result.devices[result.count++] = devices_[i];
        }
    }
    std::sort(result.devices.begin(), result.devices.begin() + result.count,
              [](const BleNearbyDevice& left, const BleNearbyDevice& right) {
                  if (left.rssi != right.rssi) return left.rssi > right.rssi;
                  return address_before(left.address, right.address);
              });
    return result;
}

void BleDeviceCatalog::clear() {
    devices_ = {};
    count_ = 0;
}
