#include "report_scheduler.hpp"

#include "mouse_bridge/cadence.hpp"

void ReportScheduler::on_packet(const mouse_bridge::MouseReport& report,
                                uint64_t) {
    const uint16_t interval =
        mouse_bridge::sanitize_poll_interval_us(report.poll_interval_us);
    if (interval != interval_us_) {
        interval_us_ = interval;
        if (sent_) next_due_us_ = last_sent_us_ + interval_us_;
    }
    if (!mouse_bridge::has_flag(report.flags,
                                mouse_bridge::ReportFlags::Keepalive)) {
        event_pending_ = true;
    }
}

bool ReportScheduler::due(uint64_t now_us) const {
    return !sent_ || event_pending_ || now_us >= next_due_us_;
}

void ReportScheduler::on_sent(uint64_t now_us) {
    if (!sent_ || event_pending_) {
        sent_ = true;
        event_pending_ = false;
        last_sent_us_ = now_us;
        next_due_us_ = now_us + interval_us_;
        return;
    }

    last_sent_us_ = now_us;
    do {
        next_due_us_ += interval_us_;
    } while (next_due_us_ <= now_us);
}

void ReportScheduler::reset() {
    interval_us_ = 1'000;
    last_sent_us_ = 0;
    next_due_us_ = 0;
    sent_ = false;
    event_pending_ = false;
}
