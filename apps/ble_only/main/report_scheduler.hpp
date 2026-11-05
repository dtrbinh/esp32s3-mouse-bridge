#pragma once

#include "mouse_bridge/mouse_report.hpp"

#include <cstdint>

class ReportScheduler {
public:
    void on_packet(const mouse_bridge::MouseReport& report, uint64_t rx_us);
    [[nodiscard]] bool due(uint64_t now_us) const;
    void request_immediate() { event_pending_ = true; }
    void on_sent(uint64_t now_us);
    void reset();
    [[nodiscard]] uint16_t interval_us() const { return interval_us_; }

private:
    uint16_t interval_us_{1'000};
    uint64_t last_sent_us_{};
    uint64_t next_due_us_{};
    bool sent_{};
    bool event_pending_{};
};
