#pragma once

#include <cstdint>

enum class BootAction : uint8_t {
    None,
    NextProfile,
    ToggleMods,
    OpenPortal,
    ClearBond = OpenPortal,
};

class BootPressClassifier {
public:
    BootAction update(bool pressed, uint32_t elapsed_ms);

private:
    static constexpr uint32_t kShortPressLimitMs = 700;
    static constexpr uint32_t kToggleModsThresholdMs = 1'500;
    static constexpr uint32_t kClearBondThresholdMs = 5'000;

    uint32_t pressed_at_ms_{};
    bool pressed_{};
    bool terminal_action_fired_{};
};
