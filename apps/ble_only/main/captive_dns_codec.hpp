#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

std::optional<size_t> build_captive_dns_response(
    std::span<const uint8_t> request, std::span<uint8_t> response,
    std::array<uint8_t, 4> ipv4);
