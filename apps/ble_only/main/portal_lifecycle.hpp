#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

enum class PortalResource : uint8_t {
    Netif,
    Wifi,
    WifiStarted,
    Http,
    Dns,
};

class PortalLifecycle {
public:
    bool begin();
    [[nodiscard]] std::optional<PortalResource> next_acquire() const;
    bool mark_acquired(PortalResource resource);
    void fail();
    void stop();
    [[nodiscard]] std::optional<PortalResource> take_release();
    [[nodiscard]] bool active() const;

private:
    static constexpr size_t kResourceCount = 5;
    size_t acquired_{};
    bool starting_{};
    bool stopping_{};
};
