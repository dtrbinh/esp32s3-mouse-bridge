#include "mouse_bridge/wire_protocol.hpp"

#include <type_traits>

namespace mouse_bridge {
namespace {

template <typename T>
void put_le(WirePacket& out, size_t offset, T value) {
    using U = std::make_unsigned_t<T>;
    const U bits = static_cast<U>(value);
    for (size_t i = 0; i < sizeof(T); ++i) {
        out[offset + i] = static_cast<uint8_t>(bits >> (i * 8U));
    }
}

template <typename T>
T get_le(std::span<const uint8_t> bytes, size_t offset) {
    using U = std::make_unsigned_t<T>;
    U bits = 0;
    for (size_t i = 0; i < sizeof(T); ++i) {
        bits |= static_cast<U>(bytes[offset + i]) << (i * 8U);
    }
    return static_cast<T>(bits);
}

}  // namespace

uint16_t crc16_ccitt_false(std::span<const uint8_t> bytes) {
    uint16_t crc = 0xFFFFU;
    for (const uint8_t byte : bytes) {
        crc ^= static_cast<uint16_t>(byte) << 8U;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000U) != 0
                      ? static_cast<uint16_t>((crc << 1U) ^ 0x1021U)
                      : static_cast<uint16_t>(crc << 1U);
        }
    }
    return crc;
}

WirePacket encode_packet(const MouseReport& report, uint32_t sequence) {
    WirePacket out{};
    put_le(out, 0, kWireMagic);
    out[2] = kWireVersion;
    out[3] = 1;  // Mouse report.
    put_le(out, 4, sequence);
    put_le(out, 8, report.timestamp_us);
    put_le(out, 16, report.x);
    put_le(out, 18, report.y);
    out[20] = static_cast<uint8_t>(report.wheel);
    out[21] = static_cast<uint8_t>(report.pan);
    put_le(out, 22, report.buttons);
    out[24] = static_cast<uint8_t>(report.source);
    out[25] = static_cast<uint8_t>(report.flags);
    put_le(out, 26, report.physical_x);
    put_le(out, 28, report.physical_y);
    put_le(out, 30, report.poll_interval_us);
    const uint16_t crc = crc16_ccitt_false(
        std::span<const uint8_t>(out.data(), kWirePacketSize - 2));
    put_le(out, 32, crc);
    return out;
}

DecodeResult decode_packet(std::span<const uint8_t> bytes) {
    DecodeResult result{};
    if (bytes.size() != kWirePacketSize) return result;
    if (get_le<uint16_t>(bytes, 0) != kWireMagic) {
        result.status = DecodeStatus::BadMagic;
        return result;
    }
    const uint16_t expected = crc16_ccitt_false(bytes.first(kWirePacketSize - 2));
    if (get_le<uint16_t>(bytes, 32) != expected) {
        result.status = DecodeStatus::BadCrc;
        return result;
    }
    if (bytes[2] != kWireVersion) {
        result.status = DecodeStatus::UnsupportedVersion;
        return result;
    }
    if (bytes[3] != 1) {
        result.status = DecodeStatus::UnsupportedType;
        return result;
    }
    if (bytes[24] > static_cast<uint8_t>(InputSource::Ble)) {
        result.status = DecodeStatus::InvalidSource;
        return result;
    }

    result.sequence = get_le<uint32_t>(bytes, 4);
    result.report.timestamp_us = get_le<uint64_t>(bytes, 8);
    result.report.x = get_le<int16_t>(bytes, 16);
    result.report.y = get_le<int16_t>(bytes, 18);
    result.report.wheel = static_cast<int8_t>(bytes[20]);
    result.report.pan = static_cast<int8_t>(bytes[21]);
    result.report.buttons = get_le<uint16_t>(bytes, 22);
    result.report.source = static_cast<InputSource>(bytes[24]);
    result.report.flags = static_cast<ReportFlags>(bytes[25]);
    result.report.physical_x = get_le<int16_t>(bytes, 26);
    result.report.physical_y = get_le<int16_t>(bytes, 28);
    result.report.poll_interval_us = get_le<uint16_t>(bytes, 30);
    result.status = DecodeStatus::Ok;
    return result;
}

bool sequence_is_newer(uint32_t candidate, uint32_t current) {
    return candidate != current &&
           static_cast<int32_t>(candidate - current) > 0;
}

}  // namespace mouse_bridge
