#include "spi_link/latest_report_mailbox.hpp"

#include <cassert>
#include <cstdint>

using mouse_bridge::InputSource;
using mouse_bridge::MouseReport;
using mouse_bridge::ReportFlags;
using spi_link::LatestReportMailbox;

int main() {
    LatestReportMailbox mailbox;
    MouseReport first{};
    first.x = 30'000;
    first.y = -30'000;
    first.physical_x = 30'000;
    first.physical_y = -30'000;
    first.wheel = 120;
    first.pan = -120;
    first.buttons = 0x0001;
    first.timestamp_us = 100;
    first.source = InputSource::Usb;

    assert(!mailbox.push(first));

    MouseReport newest{};
    newest.x = 30'000;
    newest.y = -30'000;
    newest.physical_x = 30'000;
    newest.physical_y = -30'000;
    newest.wheel = 120;
    newest.pan = -120;
    newest.buttons = 0x8002;
    newest.timestamp_us = 200;
    newest.source = InputSource::Ble;
    newest.flags = ReportFlags::Boundary;
    assert(mailbox.push(newest));

    MouseReport merged{};
    assert(mailbox.take(merged));
    assert(merged.x == INT16_MAX);
    assert(merged.y == INT16_MIN);
    assert(merged.physical_x == INT16_MAX);
    assert(merged.physical_y == INT16_MIN);
    assert(merged.wheel == INT8_MAX);
    assert(merged.pan == INT8_MIN);
    assert(merged.buttons == newest.buttons);
    assert(merged.timestamp_us == newest.timestamp_us);
    assert(merged.source == newest.source);
    assert(merged.flags == newest.flags);
    assert(!mailbox.take(merged));

    MouseReport boundary{};
    boundary.x = 20;
    boundary.buttons = 0xFFFF;
    mailbox.push(boundary);
    MouseReport release{};
    release.flags = ReportFlags::Boundary;
    mailbox.push(release);
    assert(mailbox.take(merged));
    assert(merged.x == 20);
    assert(merged.buttons == 0);
    assert(merged.flags == ReportFlags::Boundary);
}
