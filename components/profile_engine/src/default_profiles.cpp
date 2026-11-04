#include "profile_engine/profile.hpp"

#include <array>

namespace profile_engine {
namespace {

constexpr std::array<uint16_t, 15> kBaseDelay{
    24, 23, 34, 20, 0, 11, 15, 20, 20, 20, 20, 20, 20, 20, 20};
constexpr std::array<int8_t, 15> kZeroX{};
constexpr std::array<uint32_t, 5> kDefaultEnds{500, 1500, 3500, 6500, 7500};

constexpr Profile make_vertical(int8_t strength,
                                std::array<uint32_t, 5> ends = kDefaultEnds) {
    Profile profile{};
    profile.x = kZeroX;
    profile.y.fill(strength);
    profile.y[4] = 0;
    profile.delay_ms = kBaseDelay;
    profile.phase_end_ms = ends;
    profile.crouch_gain_x = 0.5F;
    profile.crouch_gain_y = 0.5F;
    return profile;
}

constexpr auto make_profiles() {
    std::array<Profile, 7> profiles{};
    profiles[0] = make_vertical(1, {300, 550, 1550, 6500, 7500});
    profiles[1] = make_vertical(2);
    profiles[2] = make_vertical(3);
    profiles[3] = make_vertical(4, {500, 830, 3500, 6500, 7500});
    profiles[4] = make_vertical(5);
    profiles[5] = make_vertical(6);  // Correct mod6 gain ownership.
    profiles[6] = make_vertical(7);
    profiles[6].y[4] = 2;
    return profiles;
}

constexpr auto kProfiles = make_profiles();

}  // namespace

std::span<const Profile> default_profiles() { return kProfiles; }

}  // namespace profile_engine

