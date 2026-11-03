#pragma once

#include <cstddef>
#include <cstdint>

namespace spi_link {

inline constexpr int kSclkGpio = 4;
inline constexpr int kMosiGpio = 5;
inline constexpr int kMisoGpio = 6;
inline constexpr int kChipSelectGpio = 7;
inline constexpr int kReadyGpio = 8;
inline constexpr int kClockHz = 10'000'000;
inline constexpr size_t kDmaQueueDepth = 4;

struct CounterSnapshot {
    uint32_t packets{};
    uint32_t sequence_gaps{};
    uint32_t crc_failures{};
    uint32_t queue_overruns{};
    uint32_t transfer_errors{};
    uint32_t max_link_latency_us{};
};

}  // namespace spi_link
