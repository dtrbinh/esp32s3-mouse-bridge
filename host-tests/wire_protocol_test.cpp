#include "mouse_bridge/wire_protocol.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <limits>

using namespace mouse_bridge;

int main() {
    static_assert(kWirePacketSize == 34);
    static_assert(kWireVersion == 2);

    MouseReport input{};
    input.timestamp_us = 0x0102030405060708ULL;
    input.x = std::numeric_limits<int16_t>::min();
    input.y = std::numeric_limits<int16_t>::max();
    input.physical_x = -1234;
    input.physical_y = 5678;
    input.wheel = -7;
    input.pan = 9;
    input.buttons = 0xA55A;
    input.source = InputSource::Usb;
    input.flags = ReportFlags::Connected | ReportFlags::GeneratedMotion;
    input.poll_interval_us = 4'000;

    const auto encoded = encode_packet(input, 0x89ABCDEFU);
    assert(encoded.size() == kWirePacketSize);
    assert(encoded[0] == 0x42 && encoded[1] == 0x4D);
    assert(encoded[4] == 0xEF && encoded[7] == 0x89);

    const auto decoded = decode_packet(encoded);
    assert(decoded.status == DecodeStatus::Ok);
    assert(decoded.sequence == 0x89ABCDEFU);
    assert(decoded.report == input);

    auto corrupt = encoded;
    corrupt[18] ^= 0x80;
    assert(decode_packet(corrupt).status == DecodeStatus::BadCrc);

    auto wrong_version = encoded;
    wrong_version[2] = 1;
    const auto crc = crc16_ccitt_false(
        std::span<const uint8_t>(wrong_version.data(), kWirePacketSize - 2));
    wrong_version[32] = static_cast<uint8_t>(crc & 0xFF);
    wrong_version[33] = static_cast<uint8_t>(crc >> 8);
    assert(decode_packet(wrong_version).status == DecodeStatus::UnsupportedVersion);

    std::array<uint8_t, 3> short_packet{};
    assert(decode_packet(short_packet).status == DecodeStatus::WrongSize);

    assert(sequence_is_newer(11, 10));
    assert(!sequence_is_newer(10, 10));
    assert(sequence_is_newer(0, std::numeric_limits<uint32_t>::max()));
    assert(!sequence_is_newer(std::numeric_limits<uint32_t>::max(), 0));
}

