#include "profile_engine/profile_engine.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace profile_engine {

int16_t saturating_add(int16_t base, int32_t delta) {
    const int32_t sum = static_cast<int32_t>(base) + delta;
    return static_cast<int16_t>(std::clamp(
        sum, static_cast<int32_t>(std::numeric_limits<int16_t>::min()),
        static_cast<int32_t>(std::numeric_limits<int16_t>::max())));
}

bool ProfileEngine::select(size_t profile_index) {
    if (profile_index >= default_profiles().size()) return false;
    selected_ = profile_index;
    deactivate();
    return true;
}

void ProfileEngine::set_enabled(bool enabled) {
    enabled_ = enabled;
    if (!enabled_) deactivate();
}

void ProfileEngine::set_bypass(bool bypass) {
    bypass_ = bypass;
    if (bypass_) deactivate();
}

void ProfileEngine::set_crouched(bool crouched) { crouched_ = crouched; }

void ProfileEngine::set_activation_mask(uint16_t mask) {
    activation_mask_ = mask;
    deactivate();
}

void ProfileEngine::reset() {
    deactivate();
    catchup_overflows_ = 0;
}

void ProfileEngine::deactivate() {
    active_ = false;
    profile_complete_ = false;
    initial_step_ = 0;
    pair_step_ = 0;
    started_us_ = 0;
    next_due_us_ = 0;
}

size_t ProfileEngine::step_for_elapsed(uint64_t event_us) {
    if (initial_step_ < 5) return initial_step_++;
    const auto& profile = default_profiles()[selected_];
    const uint64_t elapsed_ms = (event_us - started_us_) / 1000U;
    size_t phase = 0;
    while (phase < profile.phase_end_ms.size() &&
           elapsed_ms >= profile.phase_end_ms[phase]) {
        ++phase;
    }
    if (phase >= profile.phase_end_ms.size()) {
        profile_complete_ = true;
        return 14;
    }
    const size_t step = 5 + phase * 2 + pair_step_;
    pair_step_ ^= 1U;
    return step;
}

mouse_bridge::MouseReport ProfileEngine::process(
    const mouse_bridge::MouseReport& physical, uint64_t now_us) {
    auto output = physical;
    output.physical_x = physical.x;
    output.physical_y = physical.y;
    const bool activation = activation_mask_ != 0 &&
                            (physical.buttons & activation_mask_) ==
                                activation_mask_;
    if (!enabled_ || bypass_ || !activation) {
        deactivate();
        return output;
    }

    if (!active_) {
        active_ = true;
        started_us_ = now_us;
        next_due_us_ = now_us;
    }

    (void)append_due_motion(output, now_us);
    return output;
}

std::optional<mouse_bridge::MouseReport> ProfileEngine::tick(
    const mouse_bridge::MouseReport& held_state, uint64_t now_us) {
    if (!active_ || !enabled_ || bypass_) return std::nullopt;

    mouse_bridge::MouseReport output{};
    output.timestamp_us = now_us;
    output.buttons = held_state.buttons;
    output.source = held_state.source;
    output.poll_interval_us = held_state.poll_interval_us;
    if (!append_due_motion(output, now_us)) return std::nullopt;
    return output;
}

bool ProfileEngine::append_due_motion(mouse_bridge::MouseReport& output,
                                      uint64_t now_us) {
    if (profile_complete_ || now_us < next_due_us_) return false;

    const auto& profile = default_profiles()[selected_];
    if (now_us > next_due_us_ + 1'000U) {
        ++catchup_overflows_;
    }

    const size_t step = step_for_elapsed(now_us);
    if (profile_complete_) return false;
    const float gain_x =
        profile.gain_x * (crouched_ ? profile.crouch_gain_x : 1.0F);
    const float gain_y =
        profile.gain_y * (crouched_ ? profile.crouch_gain_y : 1.0F);
    const int32_t generated_x =
        static_cast<int32_t>(std::lround(profile.x[step] * gain_x));
    const int32_t generated_y =
        static_cast<int32_t>(std::lround(profile.y[step] * gain_y));
    const uint64_t delay_us =
        static_cast<uint64_t>(std::max<uint16_t>(1, profile.delay_ms[step])) *
        1000U;
    // Reschedule from the actual emission time. Missed periods are dropped,
    // never accumulated into a single large HID movement.
    next_due_us_ = now_us + delay_us;
    if (generated_x != 0 || generated_y != 0) {
        output.x = saturating_add(output.x, generated_x);
        output.y = saturating_add(output.y, generated_y);
        output.flags = output.flags | mouse_bridge::ReportFlags::GeneratedMotion;
    }
    return true;
}

}  // namespace profile_engine
