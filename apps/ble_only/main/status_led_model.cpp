#include "status_led_model.hpp"

#include <algorithm>
#include <array>

namespace {
constexpr uint32_t kScanPeriodMs = 300;
constexpr uint32_t kScanOnMs = 150;
constexpr uint32_t kConnectedPeriodMs = 2'000;
constexpr uint32_t kConnectedOnMs = 1'000;
constexpr uint16_t kPreviousChannelBudget = 76;
constexpr uint16_t kBrightnessPercent = 30;
constexpr uint16_t kChannelBudget =
    (kPreviousChannelBudget * kBrightnessPercent + 50) / 100;

constexpr LedRgb kScanYellow{255, 192, 0};
constexpr LedRgb kConnectedGreen{0, 255, 0};
constexpr std::array<LedRgb, 7> kProfileColors{{
    {255, 0, 0},
    {255, 48, 0},
    {255, 112, 0},
    {255, 32, 96},
    {255, 64, 180},
    {176, 32, 255},
    {80, 32, 255},
}};

bool blink_on(uint32_t elapsed_ms, uint32_t period_ms, uint32_t on_ms) {
    return elapsed_ms % period_ms < on_ms;
}
}  // namespace

LedRgb limit_led_rgb(LedRgb logical) {
    const std::array<uint16_t, 3> input{logical.red, logical.green,
                                        logical.blue};
    const uint16_t sum = input[0] + input[1] + input[2];
    if (sum == 0 || sum <= kChannelBudget) return logical;

    std::array<uint8_t, 3> output{};
    std::array<uint16_t, 3> remainder{};
    uint16_t used = 0;
    for (size_t i = 0; i < input.size(); ++i) {
        const uint32_t scaled =
            static_cast<uint32_t>(input[i]) * kChannelBudget;
        output[i] = static_cast<uint8_t>(scaled / sum);
        remainder[i] = static_cast<uint16_t>(scaled % sum);
        used += output[i];
    }
    while (used < kChannelBudget) {
        size_t best = 0;
        for (size_t i = 1; i < remainder.size(); ++i) {
            if (remainder[i] > remainder[best]) best = i;
        }
        ++output[best];
        remainder[best] = 0;
        ++used;
    }
    return {output[0], output[1], output[2]};
}

LedRgb StatusLedModel::update(bool connected, bool portal_active,
                              bool mods_enabled, size_t profile_number,
                              uint32_t now_ms) {
    const size_t clamped_profile = std::clamp<size_t>(profile_number, 1, 7);
    const State next_state = (!connected || portal_active)
                                 ? State::Scanning
                                 : (mods_enabled
                                        ? State::Profile
                                        : State::ConnectedPassThrough);
    const bool changed = !initialized_ || next_state != state_ ||
                         (next_state == State::Profile &&
                          clamped_profile != profile_number_);
    if (changed) {
        state_ = next_state;
        profile_number_ = clamped_profile;
        phase_started_ms_ = now_ms;
        initialized_ = true;
    }

    const uint32_t elapsed_ms = now_ms - phase_started_ms_;
    switch (state_) {
    case State::Scanning:
        return blink_on(elapsed_ms, kScanPeriodMs, kScanOnMs)
                   ? limit_led_rgb(kScanYellow)
                   : LedRgb{};
    case State::ConnectedPassThrough:
        return blink_on(elapsed_ms, kConnectedPeriodMs, kConnectedOnMs)
                   ? limit_led_rgb(kConnectedGreen)
                   : LedRgb{};
    case State::Profile:
        return limit_led_rgb(kProfileColors[profile_number_ - 1]);
    }
    return {};
}
