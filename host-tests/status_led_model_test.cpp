#include "status_led_model.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>

namespace {

unsigned channel_sum(LedRgb color) {
    return static_cast<unsigned>(color.red) + color.green + color.blue;
}

}  // namespace

int main() {
    const LedRgb off{};
    const LedRgb yellow = limit_led_rgb({255, 192, 0});
    const LedRgb green = limit_led_rgb({0, 255, 0});

    StatusLedModel scan;
    assert(scan.update(false, false, true, 1, 10'000) == yellow);
    assert(scan.update(false, false, true, 1, 10'149) == yellow);
    assert(scan.update(false, false, true, 1, 10'150) == off);
    assert(scan.update(false, false, true, 1, 10'299) == off);
    assert(scan.update(false, false, true, 1, 10'300) == yellow);

    StatusLedModel portal;
    assert(portal.update(true, true, true, 7, 20'000) == yellow);
    assert(portal.update(true, true, true, 7, 20'149) == yellow);
    assert(portal.update(true, true, true, 7, 20'150) == off);

    StatusLedModel connected;
    assert(connected.update(true, false, false, 1, 42'000) == green);
    assert(connected.update(true, false, false, 1, 42'999) == green);
    assert(connected.update(true, false, false, 1, 43'000) == off);
    assert(connected.update(true, false, false, 1, 43'999) == off);
    assert(connected.update(true, false, false, 1, 44'000) == green);

    StatusLedModel transition;
    assert(transition.update(false, false, true, 1, 50'000) == yellow);
    assert(transition.update(false, false, true, 1, 50'150) == off);
    assert(transition.update(true, false, false, 1, 50'151) == green);

    std::array<LedRgb, 7> profiles{};
    for (size_t i = 0; i < profiles.size(); ++i) {
        StatusLedModel model;
        profiles[i] = model.update(true, false, true, i + 1, 1'000);
        assert(profiles[i] != off);
        assert(channel_sum(profiles[i]) <= 76);
        assert(model.update(true, false, true, i + 1, 99'000) == profiles[i]);
        for (size_t earlier = 0; earlier < i; ++earlier) {
            assert(profiles[i] != profiles[earlier]);
        }
    }

    StatusLedModel low_clamp;
    StatusLedModel high_clamp;
    assert(low_clamp.update(true, false, true, 0, 0) == profiles[0]);
    assert(high_clamp.update(true, false, true, 8, 0) == profiles[6]);

    assert(limit_led_rgb({0, 0, 0}) == off);
    const LedRgb mixed = limit_led_rgb({255, 128, 64});
    assert(channel_sum(mixed) <= 76);
    assert(mixed.red > mixed.green && mixed.green > mixed.blue);
    assert(std::abs(static_cast<int>(mixed.red) * 128 -
                    static_cast<int>(mixed.green) * 255) <= 255);
    assert(std::abs(static_cast<int>(mixed.green) * 64 -
                    static_cast<int>(mixed.blue) * 128) <= 128);
    assert(channel_sum(yellow) <= 76);
    assert(channel_sum(green) <= 76);
}
