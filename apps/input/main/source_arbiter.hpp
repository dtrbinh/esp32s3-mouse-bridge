#pragma once

#include "mouse_bridge/mouse_report.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#endif

class SourceArbiter {
public:
    void on_connected(mouse_bridge::InputSource source, uint64_t now_us);
    void on_report(mouse_bridge::InputSource source,
                   const mouse_bridge::MouseReport& report, uint64_t now_us);
    void on_disconnected(mouse_bridge::InputSource source, uint64_t now_us);
    std::optional<mouse_bridge::MouseReport> poll(uint64_t now_us);

    [[nodiscard]] mouse_bridge::InputSource active_source() const;
    [[nodiscard]] uint32_t dropped_reports() const;

private:
    static constexpr uint64_t kBleFallbackDelayUs = 50'000;
    static constexpr size_t kQueueCapacity = 8;

    void push(const mouse_bridge::MouseReport& report);
    void push_boundary(uint64_t now_us);

    std::array<mouse_bridge::MouseReport, kQueueCapacity> queue_{};
    size_t head_{};
    size_t size_{};
    bool usb_connected_{};
    bool ble_connected_{};
    bool usb_valid_{};
    bool pending_ble_valid_{};
    mouse_bridge::MouseReport pending_ble_{};
    mouse_bridge::InputSource active_{mouse_bridge::InputSource::None};
    uint64_t ble_fallback_after_us_{};
    uint32_t dropped_{};
#ifdef ESP_PLATFORM
    mutable portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
#endif
};

