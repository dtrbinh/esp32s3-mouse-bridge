#pragma once

#include "mouse_bridge/mouse_report.hpp"
#include "profile_engine/profile.hpp"

#include <cstddef>
#include <cstdint>

namespace profile_engine {

int16_t saturating_add(int16_t base, int32_t delta);

class ProfileEngine {
public:
    bool select(size_t profile_index);
    void set_enabled(bool enabled);
    void set_bypass(bool bypass);
    void set_crouched(bool crouched);
    void reset();

    [[nodiscard]] bool active() const { return active_; }
    [[nodiscard]] uint32_t catchup_overflows() const { return catchup_overflows_; }
    mouse_bridge::MouseReport process(const mouse_bridge::MouseReport& physical,
                                      uint64_t now_us);

private:
    static constexpr uint16_t kPrimaryMask = 1U << 0;
    static constexpr uint16_t kAimMask = 1U << 1;
    static constexpr size_t kMaxCatchupSteps = 512;

    size_t step_for_elapsed(uint64_t event_us);
    void deactivate();

    size_t selected_{};
    size_t initial_step_{};
    size_t pair_step_{};
    bool enabled_{};
    bool bypass_{};
    bool crouched_{};
    bool active_{};
    bool profile_complete_{};
    uint64_t started_us_{};
    uint64_t next_due_us_{};
    uint32_t catchup_overflows_{};
};

}  // namespace profile_engine

