#pragma once

#include <cstddef>
#include <cstdint>

struct LedRgb {
    uint8_t red{};
    uint8_t green{};
    uint8_t blue{};

    bool operator==(const LedRgb&) const = default;
};

LedRgb limit_led_rgb(LedRgb logical);

class StatusLedModel {
public:
    LedRgb update(bool connected, bool portal_active, bool mods_enabled,
                  size_t profile_number, uint32_t now_ms);

private:
    enum class State : uint8_t {
        Scanning,
        ConnectedPassThrough,
        Profile,
    };

    State state_{State::Scanning};
    size_t profile_number_{1};
    uint32_t phase_started_ms_{};
    bool initialized_{};
};
