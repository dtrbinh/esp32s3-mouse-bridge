#include "ble_only_mode.hpp"

BleOnlyMode::BleOnlyMode(size_t default_profile_index, bool mods_enabled) {
    const size_t selected = default_profile_index % kProfileCount;
    selected_.store(selected, std::memory_order_relaxed);
    mods_enabled_.store(mods_enabled, std::memory_order_relaxed);
    (void)profiles_.select(selected);
    profiles_.set_enabled(mods_enabled);
}

bool BleOnlyMode::enqueue(BootAction action) {
    if (action != BootAction::NextProfile &&
        action != BootAction::ToggleMods) {
        return false;
    }
    const uint32_t write = write_index_.load(std::memory_order_relaxed);
    const uint32_t read = read_index_.load(std::memory_order_acquire);
    if (write - read >= kCommandCapacity) return false;
    commands_[write % kCommandCapacity] = action;
    write_index_.store(write + 1, std::memory_order_release);
    return true;
}

std::optional<BootAction> BleOnlyMode::apply_next() {
    const uint32_t read = read_index_.load(std::memory_order_relaxed);
    const uint32_t write = write_index_.load(std::memory_order_acquire);
    if (read == write) return std::nullopt;
    const BootAction action = commands_[read % kCommandCapacity];
    read_index_.store(read + 1, std::memory_order_release);

    if (action == BootAction::NextProfile) {
        const size_t selected =
            (selected_.load(std::memory_order_relaxed) + 1) % kProfileCount;
        (void)profiles_.select(selected);
        selected_.store(selected, std::memory_order_release);
    } else if (action == BootAction::ToggleMods) {
        const bool enabled = !mods_enabled_.load(std::memory_order_relaxed);
        profiles_.set_enabled(enabled);
        profiles_.reset();
        mods_enabled_.store(enabled, std::memory_order_release);
    }
    return action;
}

mouse_bridge::MouseReport BleOnlyMode::process(
    const mouse_bridge::MouseReport& report, uint64_t now_us) {
    return profiles_.process(report, now_us);
}

void BleOnlyMode::reset() { profiles_.reset(); }
