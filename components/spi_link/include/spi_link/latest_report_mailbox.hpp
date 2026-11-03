#pragma once

#include "mouse_bridge/mouse_report.hpp"

namespace spi_link {

class LatestReportMailbox {
public:
    // Returns true when an older, unsent report was merged.
    bool push(const mouse_bridge::MouseReport& report);
    bool take(mouse_bridge::MouseReport& report);
    [[nodiscard]] bool pending() const { return pending_; }

private:
    mouse_bridge::MouseReport report_{};
    bool pending_{};
};

}  // namespace spi_link
