#include "output_state.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace {

template <typename T>
T clamp_to(int32_t value) {
    return static_cast<T>(std::clamp(
        value, static_cast<int32_t>(std::numeric_limits<T>::min()),
        static_cast<int32_t>(std::numeric_limits<T>::max())));
}

int32_t saturating_accumulate(int32_t current, int32_t delta,
                              int32_t minimum, int32_t maximum) {
    return std::clamp<int64_t>(static_cast<int64_t>(current) + delta,
                               minimum, maximum);
}

}  // namespace

void OutputState::on_packet(const mouse_bridge::MouseReport& report,
                            uint32_t, uint64_t rx_us) {
    x_ = saturating_accumulate(x_, report.x, INT16_MIN, INT16_MAX);
    y_ = saturating_accumulate(y_, report.y, INT16_MIN, INT16_MAX);
    const bool generated = mouse_bridge::has_flag(
        report.flags, mouse_bridge::ReportFlags::GeneratedMotion);
    bypass_x_ = saturating_accumulate(
        bypass_x_, generated ? report.physical_x : report.x, INT16_MIN,
        INT16_MAX);
    bypass_y_ = saturating_accumulate(
        bypass_y_, generated ? report.physical_y : report.y, INT16_MIN,
        INT16_MAX);
    wheel_ = saturating_accumulate(wheel_, report.wheel, INT8_MIN, INT8_MAX);
    pan_ = saturating_accumulate(pan_, report.pan, INT8_MIN, INT8_MAX);
    buttons_ = report.buttons;
    last_packet_us_ = rx_us;
    received_ = true;
    timeout_latched_ = false;
}

OutputFrame OutputState::tick(uint64_t now_us, bool bypass) {
    const uint64_t age = received_ && now_us >= last_packet_us_
                             ? now_us - last_packet_us_
                             : UINT64_MAX;
    if (age >= kMotionTimeoutUs) {
        x_ = y_ = bypass_x_ = bypass_y_ = wheel_ = pan_ = 0;
    }
    if (age >= kReleaseTimeoutUs) {
        buttons_ = 0;
        if (received_ && !timeout_latched_) {
            ++timeout_count_;
            timeout_latched_ = true;
        }
    }

    OutputFrame frame{};
    frame.link_active = age < kReleaseTimeoutUs;
    if (!resume_release_pending_) {
        frame.x = clamp_to<int16_t>(bypass ? bypass_x_ : x_);
        frame.y = clamp_to<int16_t>(bypass ? bypass_y_ : y_);
        frame.wheel = clamp_to<int8_t>(wheel_);
        frame.pan = clamp_to<int8_t>(pan_);
        frame.buttons = buttons_;
    }
    resume_release_pending_ = false;
    x_ = y_ = bypass_x_ = bypass_y_ = wheel_ = pan_ = 0;
    return frame;
}

void OutputState::on_usb_suspend() {
    x_ = y_ = bypass_x_ = bypass_y_ = wheel_ = pan_ = 0;
}

void OutputState::on_usb_resume(uint64_t) {
    on_usb_suspend();
    buttons_ = 0;
    received_ = false;
    timeout_latched_ = false;
    resume_release_pending_ = true;
}
