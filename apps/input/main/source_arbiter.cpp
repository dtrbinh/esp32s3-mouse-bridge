#include "source_arbiter.hpp"

using mouse_bridge::InputSource;
using mouse_bridge::MouseReport;
using mouse_bridge::ReportFlags;

namespace {
#ifdef ESP_PLATFORM
class ArbiterLock {
public:
    explicit ArbiterLock(portMUX_TYPE& lock) : lock_(lock) {
        portENTER_CRITICAL(&lock_);
    }
    ~ArbiterLock() { portEXIT_CRITICAL(&lock_); }

private:
    portMUX_TYPE& lock_;
};
#define ARBITER_GUARD() ArbiterLock guard(lock_)
#else
#define ARBITER_GUARD() ((void)0)
#endif
}  // namespace

void SourceArbiter::push(const MouseReport& report) {
    if (size_ == kQueueCapacity) {
        head_ = (head_ + 1) % kQueueCapacity;
        --size_;
        ++dropped_;
    }
    queue_[(head_ + size_) % kQueueCapacity] = report;
    ++size_;
}

void SourceArbiter::push_boundary(uint64_t now_us) {
    MouseReport boundary{};
    boundary.timestamp_us = now_us;
    boundary.source = InputSource::None;
    boundary.flags = ReportFlags::Boundary;
    push(boundary);
}

void SourceArbiter::on_connected(InputSource source, uint64_t) {
    ARBITER_GUARD();
    if (source == InputSource::Usb) usb_connected_ = true;
    if (source == InputSource::Ble) ble_connected_ = true;
}

void SourceArbiter::on_report(InputSource source, const MouseReport& report,
                              uint64_t now_us) {
    ARBITER_GUARD();
    MouseReport tagged = report;
    tagged.timestamp_us = now_us;
    tagged.source = source;
    tagged.flags = tagged.flags | ReportFlags::Connected;

    if (source == InputSource::Usb) {
        usb_connected_ = true;
        usb_valid_ = true;
        if (active_ != InputSource::Usb) {
            if (active_ != InputSource::None) push_boundary(now_us);
            active_ = InputSource::Usb;
        }
        push(tagged);
        return;
    }

    if (source != InputSource::Ble) return;
    ble_connected_ = true;
    pending_ble_ = tagged;
    pending_ble_valid_ = true;
    if (usb_valid_ || active_ == InputSource::Usb || now_us < ble_fallback_after_us_) {
        return;
    }
    active_ = InputSource::Ble;
    push(tagged);
    pending_ble_valid_ = false;
}

void SourceArbiter::on_disconnected(InputSource source, uint64_t now_us) {
    ARBITER_GUARD();
    if (source == InputSource::Usb) {
        usb_connected_ = false;
        usb_valid_ = false;
        ble_fallback_after_us_ = now_us + kBleFallbackDelayUs;
    } else if (source == InputSource::Ble) {
        ble_connected_ = false;
        pending_ble_valid_ = false;
    }
    if (active_ == source) {
        active_ = InputSource::None;
        push_boundary(now_us);
    }
}

std::optional<MouseReport> SourceArbiter::poll(uint64_t now_us) {
    ARBITER_GUARD();
    if (size_ > 0) {
        MouseReport value = queue_[head_];
        head_ = (head_ + 1) % kQueueCapacity;
        --size_;
        return value;
    }
    if (active_ == InputSource::None && !usb_valid_ && ble_connected_ &&
        pending_ble_valid_ && now_us >= ble_fallback_after_us_) {
        active_ = InputSource::Ble;
        pending_ble_valid_ = false;
        return pending_ble_;
    }
    return std::nullopt;
}

InputSource SourceArbiter::active_source() const {
    ARBITER_GUARD();
    return active_;
}

uint32_t SourceArbiter::dropped_reports() const {
    ARBITER_GUARD();
    return dropped_;
}
