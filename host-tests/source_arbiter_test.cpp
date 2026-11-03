#include "source_arbiter.hpp"

#include <cassert>

using mouse_bridge::InputSource;
using mouse_bridge::MouseReport;

namespace {
MouseReport report(InputSource source, uint16_t buttons, int16_t x = 0) {
    MouseReport value{};
    value.source = source;
    value.buttons = buttons;
    value.x = x;
    return value;
}
}

int main() {
    SourceArbiter arbiter;
    arbiter.on_connected(InputSource::Ble, 0);
    arbiter.on_report(InputSource::Ble, report(InputSource::Ble, 1, 3), 0);
    auto out = arbiter.poll(0);
    assert(out && out->source == InputSource::Ble && out->x == 3);

    arbiter.on_connected(InputSource::Usb, 1'000);
    arbiter.on_report(InputSource::Usb, report(InputSource::Usb, 3, 7), 1'000);
    out = arbiter.poll(1'000);
    assert(out && out->buttons == 0 &&
           mouse_bridge::has_flag(out->flags, mouse_bridge::ReportFlags::Boundary));
    out = arbiter.poll(1'000);
    assert(out && out->source == InputSource::Usb && out->x == 7);

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

