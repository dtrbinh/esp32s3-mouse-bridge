#pragma once

#include "mouse_bridge/mouse_report.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace mouse_bridge {

inline constexpr size_t kWirePacketSize = 34;
inline constexpr uint16_t kWireMagic = 0x4D42;
inline constexpr uint8_t kWireVersion = 2;

using WirePacket = std::array<uint8_t, kWirePacketSize>;

enum class DecodeStatus : uint8_t {
    Ok,
    WrongSize,
    BadMagic,
    UnsupportedVersion,
    UnsupportedType,
    InvalidSource,
    BadCrc,
};

struct DecodeResult {
    DecodeStatus status{DecodeStatus::WrongSize};
    MouseReport report{};
    uint32_t sequence{};
};

uint16_t crc16_ccitt_false(std::span<const uint8_t> bytes);
WirePacket encode_packet(const MouseReport& report, uint32_t sequence);
DecodeResult decode_packet(std::span<const uint8_t> bytes);
bool sequence_is_newer(uint32_t candidate, uint32_t current);

}  // namespace mouse_bridge

