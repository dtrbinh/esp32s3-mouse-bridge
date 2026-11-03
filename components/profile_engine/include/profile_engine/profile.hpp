#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace profile_engine {

struct Profile {
    std::array<int8_t, 15> x{};
    std::array<int8_t, 15> y{};
    std::array<uint16_t, 15> delay_ms{};
    std::array<uint32_t, 5> phase_end_ms{};
    float gain_x{1.0F};
    float gain_y{1.0F};
    float crouch_gain_x{1.0F};
    float crouch_gain_y{1.0F};
};

std::span<const Profile> default_profiles();

}  // namespace profile_engine

