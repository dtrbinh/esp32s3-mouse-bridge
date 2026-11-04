#include "boot_press.hpp"

BootAction BootPressClassifier::update(bool pressed, uint32_t elapsed_ms) {
    if (pressed) {
        if (!pressed_) {
            pressed_ = true;
            terminal_action_fired_ = false;
            pressed_at_ms_ = elapsed_ms;
            return BootAction::None;
        }
        if (!terminal_action_fired_ &&
            elapsed_ms - pressed_at_ms_ >= kOpenPortalThresholdMs) {
            terminal_action_fired_ = true;
            return BootAction::OpenPortal;
        }
        return BootAction::None;
    }

    if (!pressed_) return BootAction::None;
    pressed_ = false;
    const uint32_t duration_ms = elapsed_ms - pressed_at_ms_;
    if (terminal_action_fired_) {
        terminal_action_fired_ = false;
        return BootAction::None;
    }
    if (duration_ms < kShortPressLimitMs) return BootAction::NextProfile;
    if (duration_ms >= kToggleModsThresholdMs &&
        duration_ms < kOpenPortalThresholdMs) {
        return BootAction::ToggleMods;
    }
    return BootAction::None;
}
