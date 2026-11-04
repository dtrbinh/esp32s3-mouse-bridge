#include "portal_lifecycle.hpp"

#include <array>

namespace {
constexpr std::array<PortalResource, 5> kAcquisitionOrder{
    PortalResource::Netif,
    PortalResource::Wifi,
    PortalResource::WifiStarted,
    PortalResource::Http,
    PortalResource::Dns,
};
}

bool PortalLifecycle::begin() {
    if (starting_ || stopping_ || acquired_ != 0) return false;
    starting_ = true;
    return true;
}

std::optional<PortalResource> PortalLifecycle::next_acquire() const {
    if (!starting_ || acquired_ >= kAcquisitionOrder.size()) {
        return std::nullopt;
    }
    return kAcquisitionOrder[acquired_];
}

bool PortalLifecycle::mark_acquired(PortalResource resource) {
    if (next_acquire() != resource) return false;
    ++acquired_;
    if (acquired_ == kResourceCount) starting_ = false;
    return true;
}

void PortalLifecycle::fail() {
    starting_ = false;
    stopping_ = acquired_ != 0;
}

void PortalLifecycle::stop() {
    if (stopping_) return;
    starting_ = false;
    stopping_ = acquired_ != 0;
}

std::optional<PortalResource> PortalLifecycle::next_release() const {
    if (!stopping_ || acquired_ == 0) {
        return std::nullopt;
    }
    return kAcquisitionOrder[acquired_ - 1];
}

bool PortalLifecycle::mark_released(PortalResource resource) {
    if (next_release() != resource) return false;
    --acquired_;
    if (acquired_ == 0) stopping_ = false;
    return true;
}

bool PortalLifecycle::active() const {
    return acquired_ == kResourceCount && !stopping_;
}
