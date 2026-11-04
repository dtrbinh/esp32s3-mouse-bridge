#include "ble_device_history.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace {
constexpr std::array<uint8_t, 4> kMagic{'M', 'B', 'H', '1'};
constexpr uint8_t kVersion = 1;
constexpr size_t kHeaderSize = 12;
constexpr size_t kEntrySize = 44;
constexpr size_t kCrcSize = 4;
constexpr uint8_t kNoSelection = 0xFF;

void write_u32(std::span<uint8_t> output, size_t offset, uint32_t value) {
    for (size_t i = 0; i < 4; ++i) {
        output[offset + i] = static_cast<uint8_t>(value >> (8 * i));
    }
}

uint32_t read_u32(std::span<const uint8_t> input, size_t offset) {
    uint32_t value = 0;
    for (size_t i = 0; i < 4; ++i) {
        value |= static_cast<uint32_t>(input[offset + i]) << (8 * i);
    }
    return value;
}

uint32_t crc32(std::span<const uint8_t> input) {
    uint32_t crc = 0xFFFFFFFFu;
    for (const uint8_t byte : input) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

void copy_name(std::string_view source, std::array<char, 33>& destination) {
    destination.fill('\0');
    const size_t length = std::min(source.size(), destination.size() - 1);
    for (size_t i = 0; i < length; ++i) {
        const auto byte = static_cast<unsigned char>(source[i]);
        destination[i] = byte < 0x20 || byte == 0x7F ? '?' : source[i];
    }
}

size_t name_length(const std::array<char, 33>& name) {
    size_t length = 0;
    while (length < name.size() - 1 && name[length] != '\0') ++length;
    return length;
}
}  // namespace

void BleDeviceHistory::renormalize_orders() {
    std::array<size_t, kCapacity> indices{};
    for (size_t i = 0; i < count_; ++i) indices[i] = i;
    for (size_t i = 1; i < count_; ++i) {
        const size_t key = indices[i];
        size_t position = i;
        while (position > 0 &&
               devices_[key].last_success_order <
                   devices_[indices[position - 1]].last_success_order) {
            indices[position] = indices[position - 1];
            --position;
        }
        indices[position] = key;
    }
    for (size_t i = 0; i < count_; ++i) {
        devices_[indices[i]].last_success_order = static_cast<uint32_t>(i + 1);
    }
    next_order_ = static_cast<uint32_t>(count_ + 1);
}

void BleDeviceHistory::record_success(BlePeerAddress identity,
                                      std::string_view name) {
    if (next_order_ == std::numeric_limits<uint32_t>::max()) {
        renormalize_orders();
    }
    size_t index = count_;
    for (size_t i = 0; i < count_; ++i) {
        if (devices_[i].identity == identity) {
            index = i;
            break;
        }
    }
    if (index == count_) {
        if (count_ < devices_.size()) {
            ++count_;
        } else {
            index = 0;
            for (size_t i = 1; i < count_; ++i) {
                if (devices_[i].last_success_order <
                    devices_[index].last_success_order) {
                    index = i;
                }
            }
        }
        devices_[index] = {};
        devices_[index].identity = identity;
    }
    if (!name.empty()) copy_name(name, devices_[index].name);
    devices_[index].last_success_order = next_order_++;
    selected_ = identity;
}

bool BleDeviceHistory::forget(BlePeerAddress identity) {
    size_t index = count_;
    for (size_t i = 0; i < count_; ++i) {
        if (devices_[i].identity == identity) {
            index = i;
            break;
        }
    }
    if (index == count_) return false;
    for (size_t i = index + 1; i < count_; ++i) {
        devices_[i - 1] = devices_[i];
    }
    devices_[--count_] = {};
    if (selected_ == identity) selected_.reset();
    return true;
}

void BleDeviceHistory::disconnect_without_forget() {
    selected_.reset();
}

void BleDeviceHistory::select(BlePeerAddress identity) {
    selected_ = identity;
}

BleSavedSnapshot BleDeviceHistory::snapshot(
    std::optional<BlePeerAddress> connected_identity) const {
    BleSavedSnapshot result{};
    result.count = count_;
    std::copy_n(devices_.begin(), count_, result.devices.begin());
    for (size_t i = 0; i < result.count; ++i) {
        result.devices[i].connected =
            connected_identity == result.devices[i].identity;
    }
    const auto comes_before = [](const BleSavedDevice& left,
                                 const BleSavedDevice& right) {
        if (left.connected != right.connected) return left.connected;
        if (left.last_success_order != right.last_success_order) {
            return left.last_success_order > right.last_success_order;
        }
        if (left.identity.type != right.identity.type) {
            return left.identity.type < right.identity.type;
        }
        return left.identity.bytes < right.identity.bytes;
    };
    for (size_t i = 1; i < result.count; ++i) {
        const BleSavedDevice key = result.devices[i];
        size_t position = i;
        while (position > 0 &&
               comes_before(key, result.devices[position - 1])) {
            result.devices[position] = result.devices[position - 1];
            --position;
        }
        result.devices[position] = key;
    }
    return result;
}

size_t encode_ble_history(const BleDeviceHistory& history,
                          std::span<uint8_t> output) {
    const size_t size =
        kHeaderSize + history.count_ * kEntrySize + kCrcSize;
    if (output.size() < size) return 0;
    std::fill(output.begin(), output.begin() + size, 0);
    std::copy(kMagic.begin(), kMagic.end(), output.begin());
    output[4] = kVersion;
    output[5] = static_cast<uint8_t>(history.count_);
    output[6] = kNoSelection;
    for (size_t i = 0; i < history.count_; ++i) {
        if (history.selected_ == history.devices_[i].identity) {
            output[6] = static_cast<uint8_t>(i);
        }
    }
    write_u32(output, 8, history.next_order_);
    for (size_t i = 0; i < history.count_; ++i) {
        const size_t offset = kHeaderSize + i * kEntrySize;
        output[offset] = history.devices_[i].identity.type;
        std::copy(history.devices_[i].identity.bytes.begin(),
                  history.devices_[i].identity.bytes.end(),
                  output.begin() + offset + 1);
        const size_t length = name_length(history.devices_[i].name);
        output[offset + 7] = static_cast<uint8_t>(length);
        std::copy_n(reinterpret_cast<const uint8_t*>(
                        history.devices_[i].name.data()),
                    length, output.begin() + offset + 8);
        write_u32(output, offset + 40,
                  history.devices_[i].last_success_order);
    }
    write_u32(output, size - kCrcSize,
              crc32(output.first(size - kCrcSize)));
    return size;
}

std::optional<BleDeviceHistory> decode_ble_history(
    std::span<const uint8_t> input) {
    if (input.size() < kHeaderSize + kCrcSize ||
        !std::equal(kMagic.begin(), kMagic.end(), input.begin()) ||
        input[4] != kVersion || input[5] > BleDeviceHistory::kCapacity) {
        return std::nullopt;
    }
    const size_t count = input[5];
    const size_t expected_size = kHeaderSize + count * kEntrySize + kCrcSize;
    if (input.size() != expected_size ||
        read_u32(input, expected_size - kCrcSize) !=
            crc32(input.first(expected_size - kCrcSize))) {
        return std::nullopt;
    }
    const uint8_t selected_index = input[6];
    if (selected_index != kNoSelection && selected_index >= count) {
        return std::nullopt;
    }

    BleDeviceHistory history;
    history.count_ = count;
    history.next_order_ = read_u32(input, 8);
    if (history.next_order_ == 0) return std::nullopt;
    for (size_t i = 0; i < count; ++i) {
        const size_t offset = kHeaderSize + i * kEntrySize;
        auto& device = history.devices_[i];
        device.identity.type = input[offset];
        std::copy_n(input.begin() + offset + 1, 6,
                    device.identity.bytes.begin());
        const size_t length = input[offset + 7];
        if (length > device.name.size() - 1) return std::nullopt;
        std::copy_n(reinterpret_cast<const char*>(input.data() + offset + 8),
                    length, device.name.begin());
        device.name[length] = '\0';
        device.last_success_order = read_u32(input, offset + 40);
        if (device.last_success_order == 0) return std::nullopt;
    }
    if (selected_index != kNoSelection) {
        history.selected_ = history.devices_[selected_index].identity;
    }
    return history;
}
