#include "ble_only_mode.hpp"

#include "mouse_bridge/mouse_report.hpp"

#include <cassert>

using mouse_bridge::MouseReport;
using mouse_bridge::ReportFlags;

int main() {
    BleOnlyMode queued(13, true);
    assert(queued.selected_profile() == 14);
    assert(queued.mods_enabled());
    assert(queued.enqueue(BootAction::NextProfile));
    assert(queued.enqueue(BootAction::NextProfile));
    assert(queued.enqueue(BootAction::ToggleMods));

    assert(queued.apply_next() == BootAction::NextProfile);
    assert(queued.selected_profile() == 1);
    assert(queued.apply_next() == BootAction::NextProfile);
    assert(queued.selected_profile() == 2);
    assert(queued.apply_next() == BootAction::ToggleMods);
    assert(!queued.mods_enabled());
    assert(!queued.apply_next().has_value());

    assert(!queued.enqueue(BootAction::None));
    assert(!queued.enqueue(BootAction::OpenPortal));

    BleOnlyMode mode(0, true);
    MouseReport activating{};
    activating.x = 4;
    activating.y = -2;
    activating.wheel = 1;
    activating.pan = -1;
    activating.buttons = 0x02;
    auto generated = mode.process(activating, 1'000);
    assert(mouse_bridge::has_flag(generated.flags,
                                  ReportFlags::GeneratedMotion));
    assert(mode.profile_active());

    MouseReport left_only{};
    left_only.buttons = 0x01;
    auto unmodified = mode.process(left_only, 1'500);
    assert(!mouse_bridge::has_flag(unmodified.flags,
                                   ReportFlags::GeneratedMotion));
    assert(!mode.profile_active());

    assert(mode.enqueue(BootAction::ToggleMods));
    assert(mode.apply_next() == BootAction::ToggleMods);
    assert(!mode.mods_enabled());
    assert(!mode.profile_active());

    auto raw = mode.process(activating, 2'000);
    assert(raw.x == activating.x && raw.y == activating.y);
    assert(raw.wheel == activating.wheel && raw.pan == activating.pan);
    assert(raw.buttons == activating.buttons);
    assert(!mouse_bridge::has_flag(raw.flags,
                                   ReportFlags::GeneratedMotion));

    BleOnlyMode full(0, true);
    for (size_t i = 0; i < BleOnlyMode::kCommandCapacity; ++i) {
        assert(full.enqueue(BootAction::NextProfile));
    }
    assert(!full.enqueue(BootAction::NextProfile));
    for (size_t i = 0; i < BleOnlyMode::kCommandCapacity; ++i) {
        assert(full.apply_next() == BootAction::NextProfile);
    }
    assert(!full.apply_next().has_value());
}
