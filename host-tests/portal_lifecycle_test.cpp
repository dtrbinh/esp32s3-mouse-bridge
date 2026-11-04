#include "portal_lifecycle.hpp"

#include <array>
#include <cassert>
#include <optional>

namespace {
constexpr std::array<PortalResource, 5> kForward{
    PortalResource::Netif,
    PortalResource::Wifi,
    PortalResource::WifiStarted,
    PortalResource::Http,
    PortalResource::Dns,
};

void drain_and_check(PortalLifecycle& lifecycle, size_t acquired) {
    for (size_t i = acquired; i > 0; --i) {
        assert(lifecycle.take_release() == kForward[i - 1]);
    }
    assert(!lifecycle.take_release().has_value());
    assert(!lifecycle.active());
}
}  // namespace

int main() {
    for (size_t fail_at = 0; fail_at < kForward.size(); ++fail_at) {
        PortalLifecycle lifecycle;
        assert(lifecycle.begin());
        for (size_t i = 0; i < fail_at; ++i) {
            assert(lifecycle.next_acquire() == kForward[i]);
            assert(lifecycle.mark_acquired(kForward[i]));
        }
        assert(lifecycle.next_acquire() == kForward[fail_at]);
        lifecycle.fail();
        drain_and_check(lifecycle, fail_at);

        // A completely unwound partial start can be retried.
        assert(lifecycle.begin());
        assert(lifecycle.next_acquire() == PortalResource::Netif);
        lifecycle.fail();
        drain_and_check(lifecycle, 0);
    }

    PortalLifecycle lifecycle;
    assert(lifecycle.begin());
    for (const auto resource : kForward) {
        assert(lifecycle.next_acquire() == resource);
        assert(lifecycle.mark_acquired(resource));
    }
    assert(!lifecycle.next_acquire().has_value());
    assert(lifecycle.active());

    lifecycle.stop();
    lifecycle.stop();  // Idempotent: must not duplicate releases.
    drain_and_check(lifecycle, kForward.size());
    lifecycle.stop();
    assert(!lifecycle.take_release().has_value());
}
