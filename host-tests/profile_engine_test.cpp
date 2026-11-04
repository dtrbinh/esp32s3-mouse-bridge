#include "profile_engine/profile_engine.hpp"

#include <cassert>
#include <cstdint>
#include <limits>
#include <optional>

using mouse_bridge::MouseReport;
using profile_engine::ProfileEngine;

namespace {
MouseReport buttons(uint16_t value) {
    MouseReport report{};
    report.buttons = value;
    return report;
}
}

int main() {
    ProfileEngine engine;
    assert(engine.select(0));
    assert(engine.select(6));
    assert(!engine.select(7));
    assert(!engine.select(14));
    assert(engine.select(0));
    engine.set_enabled(true);

    auto out = engine.process(buttons(0x0001), 0);
    assert(out.x == 0 && out.y == 0);  // Primary alone does not activate.

    out = engine.process(buttons(0x0003), 1'000);
    assert(out.y == 1);
    assert(mouse_bridge::has_flag(out.flags,
                                  mouse_bridge::ReportFlags::GeneratedMotion));

    out = engine.process(buttons(0x0003), 25'000);
    assert(out.y == 1);

    // Profile motion continues on its own schedule even when a BLE mouse
    // sends no more input reports while its activation buttons stay held.
    auto tick = engine.tick(buttons(0x0003), 48'000);
    assert(tick.has_value());
    assert(tick->y == 1);
    assert(tick->buttons == 0x0003);
    assert(mouse_bridge::has_flag(tick->flags,
                                  mouse_bridge::ReportFlags::GeneratedMotion));
    assert(!engine.tick(buttons(0x0003), 49'000).has_value());

    out = engine.process(buttons(0), 26'000);
    assert(out.y == 0);
    assert(!engine.active());

    engine.set_activation_mask(0x0002);
    out = engine.process(buttons(0x0002), 27'000);
    assert(out.y == 1);  // Configurable aim-only activation.
    assert(engine.active());
    out = engine.process(buttons(0x0001), 28'000);
    assert(out.y == 0);  // Primary alone does not satisfy aim-only activation.
    assert(!engine.active());

    engine.set_activation_mask(0x0003);

    engine.reset();
    engine.set_crouched(true);
    out = engine.process(buttons(0x0003), 100'000);
    assert(out.y == 1);  // round(1 * 0.5) is symmetric half-away from zero.

    engine.reset();
    engine.set_crouched(false);
    assert(engine.select(5));
    out = engine.process(buttons(0x0003), 200'000);
    assert(out.y == 6);
    assert(out.x == 0);  // mod6 uses mod6_X, not mod1_X.

    // A large scheduler stall emits at most one small step instead of
    // accumulating every missed step into a cursor jump.
    out = engine.process(buttons(0x0003), 8'500'000);
    assert(out.x >= -5 && out.x <= 5);
    assert(out.y >= 0 && out.y <= 7);
    assert(engine.catchup_overflows() <= 1);

    engine.set_bypass(true);
    const auto physical = MouseReport{.x = 12, .y = -8, .buttons = 3};
    out = engine.process(physical, 8'501'000);
    assert(out.x == 12 && out.y == -8);
    assert(out.physical_x == 12 && out.physical_y == -8);
    assert(!mouse_bridge::has_flag(out.flags,
                                  mouse_bridge::ReportFlags::GeneratedMotion));

    engine.set_bypass(false);
    engine.set_enabled(false);
    out = engine.process(buttons(3), 8'502'000);
    assert(out.x == 0 && out.y == 0);

    assert(profile_engine::saturating_add(
               std::numeric_limits<int16_t>::max(), 1) ==
           std::numeric_limits<int16_t>::max());
    assert(profile_engine::saturating_add(
               std::numeric_limits<int16_t>::min(), -1) ==
           std::numeric_limits<int16_t>::min());
}

