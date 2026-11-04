#pragma once

#include <cstdint>

namespace mouse_bridge {

inline constexpr uint16_t kMinimumPollIntervalUs = 1'000;
inline constexpr uint16_t kMaximumPollIntervalUs = 8'000;

uint16_t sanitize_poll_interval_us(uint32_t interval_us);

class CadenceEstimator {
public:
    uint16_t observe(uint64_t timestamp_us);
    void reset();
    [[nodiscard]] uint16_t current() const { return current_us_; }

private:
    uint64_t previous_timestamp_us_{};
    uint16_t current_us_{};
    uint16_t slower_candidate_us_{};
    uint8_t slower_candidate_count_{};
};

}  // namespace mouse_bridge
