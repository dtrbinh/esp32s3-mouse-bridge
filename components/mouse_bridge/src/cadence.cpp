#include "mouse_bridge/cadence.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

namespace mouse_bridge {
namespace {

uint16_t snap_standard(uint32_t interval_us) {
    constexpr std::array<uint16_t, 4> standards{1'000, 2'000, 4'000, 8'000};
    for (const uint16_t standard : standards) {
        const uint32_t difference = interval_us > standard
                                        ? interval_us - standard
                                        : standard - interval_us;
        if (difference * 5U <= static_cast<uint32_t>(standard)) {
            return standard;
        }
    }
    return sanitize_poll_interval_us(interval_us);
}

bool approximately_same(uint16_t lhs, uint16_t rhs) {
    const uint32_t difference = lhs > rhs ? lhs - rhs : rhs - lhs;
    return difference * 5U <= static_cast<uint32_t>(rhs);
}

}  // namespace

uint16_t sanitize_poll_interval_us(uint32_t interval_us) {
    if (interval_us == 0) return kMinimumPollIntervalUs;
    return static_cast<uint16_t>(std::clamp<uint32_t>(
        interval_us, kMinimumPollIntervalUs, kMaximumPollIntervalUs));
}

uint16_t CadenceEstimator::observe(uint64_t timestamp_us) {
    if (previous_timestamp_us_ == 0 || timestamp_us <= previous_timestamp_us_) {
        previous_timestamp_us_ = timestamp_us;
        return current_us_;
    }

    const uint64_t elapsed = timestamp_us - previous_timestamp_us_;
    previous_timestamp_us_ = timestamp_us;
    const uint16_t sample = snap_standard(static_cast<uint32_t>(
        std::min<uint64_t>(elapsed, UINT32_MAX)));

    if (current_us_ == 0) {
        current_us_ = sample;
        return current_us_;
    }

    if (static_cast<uint32_t>(sample) * 2U < current_us_) {
        current_us_ = sample;
        slower_candidate_us_ = 0;
        slower_candidate_count_ = 0;
        return current_us_;
    }

    if (sample > static_cast<uint32_t>(current_us_) * 3U / 2U) {
        if (slower_candidate_us_ != 0 &&
            approximately_same(sample, slower_candidate_us_)) {
            ++slower_candidate_count_;
        } else {
            slower_candidate_us_ = sample;
            slower_candidate_count_ = 1;
        }
        if (slower_candidate_count_ >= 2) {
            current_us_ = slower_candidate_us_;
            slower_candidate_us_ = 0;
            slower_candidate_count_ = 0;
        }
        return current_us_;
    }

    slower_candidate_us_ = 0;
    slower_candidate_count_ = 0;
    current_us_ = snap_standard(
        (static_cast<uint32_t>(current_us_) * 3U + sample) / 4U);
    return current_us_;
}

void CadenceEstimator::reset() {
    previous_timestamp_us_ = 0;
    current_us_ = 0;
    slower_candidate_us_ = 0;
    slower_candidate_count_ = 0;
}

}  // namespace mouse_bridge
