#pragma once

#include "boot_press.hpp"
#include "mouse_bridge/mouse_report.hpp"
#include "profile_engine/profile_engine.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>

class BleOnlyMode {
public:
    static constexpr size_t kCommandCapacity = 8;
    static constexpr size_t kProfileCount = 14;

    explicit BleOnlyMode(size_t default_profile_index, bool mods_enabled);

    bool enqueue(BootAction action);
    std::optional<BootAction> apply_next();
    mouse_bridge::MouseReport process(
        const mouse_bridge::MouseReport& report, uint64_t now_us);
    void reset();

    [[nodiscard]] size_t selected_profile() const {
        return selected_.load(std::memory_order_acquire) + 1;
    }
    [[nodiscard]] bool mods_enabled() const {
        return mods_enabled_.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool profile_active() const { return profiles_.active(); }

private:
    std::array<BootAction, kCommandCapacity> commands_{};
    std::atomic<uint32_t> write_index_{};
    std::atomic<uint32_t> read_index_{};
    std::atomic<size_t> selected_{};
    std::atomic<bool> mods_enabled_{};
    profile_engine::ProfileEngine profiles_{};
};
