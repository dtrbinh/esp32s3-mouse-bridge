#include "spi_link/latest_report_mailbox.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace spi_link {
namespace {

template <typename T>
T saturating_add(T lhs, T rhs) {
    using Wide = std::conditional_t<(sizeof(T) < sizeof(int32_t)), int32_t,
                                    int64_t>;
    const Wide sum = static_cast<Wide>(lhs) + static_cast<Wide>(rhs);
    return static_cast<T>(std::clamp(
        sum, static_cast<Wide>(std::numeric_limits<T>::min()),
        static_cast<Wide>(std::numeric_limits<T>::max())));
}

}  // namespace

bool LatestReportMailbox::push(const mouse_bridge::MouseReport& report) {
    if (!pending_) {
        report_ = report;
        pending_ = true;
        return false;
    }

    report_.x = saturating_add(report_.x, report.x);
    report_.y = saturating_add(report_.y, report.y);
    report_.physical_x = saturating_add(report_.physical_x, report.physical_x);
    report_.physical_y = saturating_add(report_.physical_y, report.physical_y);
    report_.wheel = saturating_add(report_.wheel, report.wheel);
    report_.pan = saturating_add(report_.pan, report.pan);
    report_.buttons = report.buttons;
    report_.timestamp_us = report.timestamp_us;
    report_.source = report.source;
    report_.flags = report.flags;
    return true;
}

bool LatestReportMailbox::take(mouse_bridge::MouseReport& report) {
    if (!pending_) return false;
    report = report_;
    pending_ = false;
    return true;
}

}  // namespace spi_link
