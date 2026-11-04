#include "report_scheduler.hpp"

#include <cassert>

namespace {
mouse_bridge::MouseReport packet(uint16_t interval_us, bool keepalive) {
    mouse_bridge::MouseReport report{};
    report.poll_interval_us = interval_us;
    if (keepalive) report.flags = mouse_bridge::ReportFlags::Keepalive;
    return report;
}
}

int main() {
    ReportScheduler scheduler;
    assert(scheduler.interval_us() == 1'000);
    assert(scheduler.due(0));
    scheduler.on_sent(0);
    assert(!scheduler.due(999));
    assert(scheduler.due(1'000));

    // Keepalive changes cadence but is not an immediate input event.
    scheduler.on_sent(1'000);
    scheduler.on_packet(packet(2'000, true), 1'100);
    assert(scheduler.interval_us() == 2'000);
    assert(!scheduler.due(2'999));
    assert(scheduler.due(3'000));

    // A real report bypasses the cadence wait.
    scheduler.on_sent(3'000);
    scheduler.on_packet(packet(8'000, false), 3'500);
    assert(scheduler.due(3'500));
    scheduler.on_sent(4'000);
    assert(!scheduler.due(11'999));
    assert(scheduler.due(12'000));

    // A 7.5 ms BLE cadence alternates 8 ms and 7 ms USB-frame gaps.
    scheduler.reset();
    scheduler.on_packet(packet(7'500, false), 0);
    assert(scheduler.due(0));
    scheduler.on_sent(0);
    assert(!scheduler.due(7'000));
    assert(scheduler.due(8'000));
    scheduler.on_sent(8'000);
    assert(scheduler.due(15'000));
    scheduler.on_sent(15'000);
    assert(!scheduler.due(22'000));
    assert(scheduler.due(23'000));

    // Unknown and invalid values safely fall back to the fastest cadence.
    scheduler.reset();
    scheduler.on_packet(packet(0, true), 0);
    assert(scheduler.interval_us() == 1'000);
}
