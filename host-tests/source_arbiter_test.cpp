#include "source_arbiter.hpp"

#include <cassert>

using mouse_bridge::InputSource;
using mouse_bridge::MouseReport;

namespace {
MouseReport report(InputSource source, uint16_t buttons, int16_t x = 0,
                   uint16_t poll_interval_us = 0) {
    MouseReport value{};
    value.source = source;
    value.buttons = buttons;
    value.x = x;
    value.poll_interval_us = poll_interval_us;
    return value;
}

void count_wake(void* context) {
    ++*static_cast<unsigned*>(context);
}

void wake_callback_fires_for_deliverable_reports_and_boundaries() {
    SourceArbiter arbiter;
    unsigned wakes = 0;
    arbiter.set_wake_callback(count_wake, &wakes);

    arbiter.on_report(InputSource::Ble,
                      report(InputSource::Ble, 1, 3, 7'500), 1'000);
    assert(wakes == 1);
    assert(arbiter.poll(1'000)->x == 3);

    arbiter.on_report(InputSource::Usb,
                      report(InputSource::Usb, 2, 7, 1'000), 2'000);
    assert(wakes == 2);  // One wake covers the boundary and USB report.
    auto out = arbiter.poll(2'000);
    assert(out && mouse_bridge::has_flag(
                      out->flags, mouse_bridge::ReportFlags::Boundary));
    out = arbiter.poll(2'000);
    assert(out && out->source == InputSource::Usb && out->x == 7);

    arbiter.on_disconnected(InputSource::Usb, 3'000);
    assert(wakes == 3);
    out = arbiter.poll(3'000);
    assert(out && mouse_bridge::has_flag(
                      out->flags, mouse_bridge::ReportFlags::Boundary));
}

void suppressed_ble_does_not_wake_pipeline() {
    SourceArbiter arbiter;
    unsigned wakes = 0;
    arbiter.set_wake_callback(count_wake, &wakes);
    arbiter.on_report(InputSource::Usb,
                      report(InputSource::Usb, 0, 1, 1'000), 1'000);
    assert(wakes == 1);
    (void)arbiter.poll(1'000);

    arbiter.on_report(InputSource::Ble,
                      report(InputSource::Ble, 0, 99, 7'500), 2'000);
    assert(wakes == 1);
    assert(!arbiter.poll(2'000));
}

void coalesced_wakes_do_not_change_queue_contents() {
    SourceArbiter arbiter;
    unsigned wakes = 0;
    arbiter.set_wake_callback(count_wake, &wakes);
    arbiter.on_report(InputSource::Usb,
                      report(InputSource::Usb, 1, 4, 1'000), 1'000);
    arbiter.on_report(InputSource::Usb,
                      report(InputSource::Usb, 2, 5, 1'000), 2'000);
    assert(wakes == 2);

    auto first = arbiter.poll(2'000);
    auto second = arbiter.poll(2'000);
    assert(first && first->x == 4 && first->buttons == 1);
    assert(second && second->x == 5 && second->buttons == 2);
    assert(!arbiter.poll(2'000));
}
}

int main() {
    wake_callback_fires_for_deliverable_reports_and_boundaries();
    suppressed_ble_does_not_wake_pipeline();
    coalesced_wakes_do_not_change_queue_contents();

    SourceArbiter arbiter;
    arbiter.on_connected(InputSource::Ble, 0);
    arbiter.on_report(InputSource::Ble,
                      report(InputSource::Ble, 1, 3, 7'500), 0);
    auto out = arbiter.poll(0);
    assert(out && out->source == InputSource::Ble && out->x == 3 &&
           out->poll_interval_us == 7'500);

    arbiter.on_connected(InputSource::Usb, 1'000);
    arbiter.on_report(InputSource::Usb,
                      report(InputSource::Usb, 3, 7, 1'000), 1'000);
    out = arbiter.poll(1'000);
    assert(out && out->buttons == 0 &&
           mouse_bridge::has_flag(out->flags, mouse_bridge::ReportFlags::Boundary));
    assert(out->poll_interval_us == 1'000);
    out = arbiter.poll(1'000);
    assert(out && out->source == InputSource::Usb && out->x == 7 &&
           out->poll_interval_us == 1'000);

    arbiter.on_report(InputSource::Ble, report(InputSource::Ble, 1, 99), 2'000);
    assert(!arbiter.poll(2'000));  // BLE is suppressed while USB is valid.

    arbiter.on_disconnected(InputSource::Usb, 3'000);
    out = arbiter.poll(3'000);
    assert(out && out->buttons == 0 &&
           mouse_bridge::has_flag(out->flags, mouse_bridge::ReportFlags::Boundary));
    assert(!arbiter.poll(40'000));
    out = arbiter.poll(60'000);
    assert(out && out->source == InputSource::Ble && out->x == 99);
    assert(!arbiter.poll(60'000));  // Exactly one release boundary.

    arbiter.on_disconnected(InputSource::Ble, 70'000);
    out = arbiter.poll(70'000);
    assert(out && out->buttons == 0);

    // Rapid USB reconnect cancels pending BLE fallback.
    arbiter.on_connected(InputSource::Ble, 80'000);
    arbiter.on_report(InputSource::Ble, report(InputSource::Ble, 0, 4), 80'000);
    arbiter.on_connected(InputSource::Usb, 81'000);
    arbiter.on_report(InputSource::Usb, report(InputSource::Usb, 0, 8), 81'000);
    while (arbiter.poll(81'000)) {}
    assert(arbiter.active_source() == InputSource::Usb);
}

