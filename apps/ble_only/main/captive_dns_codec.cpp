#include "captive_dns_codec.hpp"

#include <algorithm>

namespace {
uint16_t read_u16(std::span<const uint8_t> input, size_t offset) {
    return static_cast<uint16_t>((input[offset] << 8) | input[offset + 1]);
}

void write_u16(std::span<uint8_t> output, size_t offset, uint16_t value) {
    output[offset] = static_cast<uint8_t>(value >> 8);
    output[offset + 1] = static_cast<uint8_t>(value);
}
}  // namespace

std::optional<size_t> build_captive_dns_response(
    std::span<const uint8_t> request, std::span<uint8_t> response,
    std::array<uint8_t, 4> ipv4) {
    constexpr size_t kHeaderSize = 12;
    constexpr size_t kAnswerSize = 16;
    if (request.size() < kHeaderSize || (read_u16(request, 2) & 0xF800) != 0 ||
        read_u16(request, 4) != 1) {
        return std::nullopt;
    }
    size_t position = kHeaderSize;
    while (true) {
        if (position >= request.size()) return std::nullopt;
        const uint8_t length = request[position++];
        if (length == 0) break;
        if ((length & 0xC0) != 0 || length > 63 ||
            position + length > request.size()) {
            return std::nullopt;
        }
        position += length;
    }
    if (position + 4 != request.size() || read_u16(request, position) != 1 ||
        read_u16(request, position + 2) != 1 ||
        response.size() < request.size() + kAnswerSize) {
        return std::nullopt;
    }
    const size_t size = request.size() + kAnswerSize;
    std::copy(request.begin(), request.end(), response.begin());
    const uint16_t request_flags = read_u16(request, 2);
    write_u16(response, 2,
              static_cast<uint16_t>(0x8080 | (request_flags & 0x0100)));
    write_u16(response, 4, 1);
    write_u16(response, 6, 1);
    write_u16(response, 8, 0);
    write_u16(response, 10, 0);
    size_t out = request.size();
    response[out++] = 0xC0;
    response[out++] = 0x0C;
    write_u16(response, out, 1);
    out += 2;
    write_u16(response, out, 1);
    out += 2;
    response[out++] = 0;
    response[out++] = 0;
    response[out++] = 0;
    response[out++] = 30;
    write_u16(response, out, 4);
    out += 2;
    std::copy(ipv4.begin(), ipv4.end(), response.begin() + out);
    out += ipv4.size();
    return out == size ? std::optional<size_t>(size) : std::nullopt;
}
