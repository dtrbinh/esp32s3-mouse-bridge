#pragma once

#include "mouse_bridge/mouse_report.hpp"

#include <cstdint>

struct OutputFrame {
    int16_t x{};
    int16_t y{};
    int8_t wheel{};
    int8_t pan{};
    uint16_t buttons{};
    bool link_active{};
};

class OutputState {
public:
    void on_packet(const mouse_bridge::MouseReport& report, uint32_t sequence,
                   uint64_t rx_us);
    OutputFrame tick(uint64_t now_us, bool bypass);
    void on_usb_suspend();
    void on_usb_resume(uint64_t now_us);

private:
    static constexpr uint64_t kMotionTimeoutUs = 5'000;
    static constexpr uint64_t kReleaseTimeoutUs = 20'000;

    int32_t x_{};
    int32_t y_{};
    int32_t bypass_x_{};
    int32_t bypass_y_{};
    int32_t wheel_{};
    int32_t pan_{};
    uint16_t buttons_{};
    uint64_t last_packet_us_{};
    bool received_{};
    bool resume_release_pending_{true};
};
