#include "captive_dns_codec.hpp"

#include <array>
#include <cassert>
#include <span>

namespace {
std::array<uint8_t, 25> query() {
    return {0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x03, 'f',  'o',  'o',  0x03, 'b',
            'a',  'r',  0x00, 0x00, 0x01, 0x00, 0x01};
}
}  // namespace

int main() {
    const auto request = query();
    std::array<uint8_t, 512> response{};
    const auto size = build_captive_dns_response(
        request, response, std::array<uint8_t, 4>{192, 168, 4, 1});
    assert(size.has_value());
    assert(response[0] == 0x12 && response[1] == 0x34);
    assert((response[2] & 0x80) != 0);
    assert(response[6] == 0x00 && response[7] == 0x01);
    assert(response[*size - 4] == 192 && response[*size - 3] == 168 &&
           response[*size - 2] == 4 && response[*size - 1] == 1);

    assert(!build_captive_dns_response(
                std::span<const uint8_t>(request.data(), request.size() - 1),
                response, {192, 168, 4, 1})
                .has_value());
    auto invalid = request;
    invalid[2] |= 0x80;  // Already a response.
    assert(!build_captive_dns_response(invalid, response, {192, 168, 4, 1})
                .has_value());
    invalid = request;
    invalid[5] = 2;  // Two questions.
    assert(!build_captive_dns_response(invalid, response, {192, 168, 4, 1})
                .has_value());
    invalid = request;
    invalid[12] = 0xC0;  // Compressed question name is deliberately rejected.
    assert(!build_captive_dns_response(invalid, response, {192, 168, 4, 1})
                .has_value());
    invalid = request;
    invalid[21] = 0x00;
    invalid[22] = 0x1C;  // AAAA.
    assert(!build_captive_dns_response(invalid, response, {192, 168, 4, 1})
                .has_value());
    invalid = request;
    invalid[12] = 64;  // Oversized label.
    assert(!build_captive_dns_response(invalid, response, {192, 168, 4, 1})
                .has_value());
    std::array<uint8_t, 32> tiny{};
    assert(!build_captive_dns_response(request, tiny, {192, 168, 4, 1})
                .has_value());
}
