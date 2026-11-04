#include "portal_codec.hpp"
#include "portal_page.hpp"

#include <array>
#include <cassert>
#include <cstring>
#include <string>

namespace {
BlePeerAddress address(uint8_t tail, uint8_t type = 0) {
    return {{{1, 2, 3, 4, 5, tail}}, type};
}

void set_name(std::array<char, 33>& output, const char* input) {
    std::strncpy(output.data(), input, output.size() - 1);
}
}  // namespace

int main() {
    PortalView view{};
    view.setup.phase = BleSetupPhase::Connecting;
    view.setup.portal_active = true;
    view.setup.portal_desired = true;
    view.setup.attempt = address(9, 1);
    view.saved.count = 2;
    view.saved.devices[0].identity = address(1);
    view.saved.devices[0].connected = true;
    set_name(view.saved.devices[0].name, "Mouse \"A\"");
    view.saved.devices[1].identity = address(2);
    view.nearby.count = 2;
    view.nearby.devices[0].address = address(9, 1);
    view.nearby.devices[0].rssi = -37;
    view.nearby.devices[0].advertises_hid = true;
    set_name(view.nearby.devices[0].name, "Freewolf\\X7");
    view.nearby.devices[1].address = address(10, 1);
    view.nearby.devices[1].rssi = -20;

    std::array<char, 8192> output{};
    const auto size = write_portal_json(view, output);
    assert(size.has_value());
    const std::string json(output.data(), *size);
    assert(json.find("\"portalActive\":true") != std::string::npos);
    assert(json.find("\"phase\":\"connecting\"") != std::string::npos);
    assert(json.find("Mouse \\\"A\\\"") != std::string::npos);
    assert(json.find("Freewolf\\\\X7") != std::string::npos);
    assert(json.find("\"connected\":true") != std::string::npos);
    assert(json.find("\"rssi\":-37") != std::string::npos);
    assert(json.find("\"address\":\"09:05:04:03:02:01\"") !=
           std::string::npos);
    assert(json.find("02:05:04:03:02:01") == std::string::npos);
    assert(json.find("0A:05:04:03:02:01") == std::string::npos);

    std::array<char, 32> tiny{};
    assert(!write_portal_json(view, tiny).has_value());

    const auto parsed =
        parse_peer_form("address=A6%3A05%3A04%3A03%3A02%3A01&type=1");
    assert(parsed == address(0xA6, 1));
    assert(!parse_peer_form("address=bad&type=1").has_value());
    assert(!parse_peer_form(
                "address=A6%3A05%3A04%3A03%3A02%3A01&type=3")
                .has_value());
    assert(!parse_peer_form(
                "address=A6%3A05%3A04%3A03%3A02%3A01&type=1&type=0")
                .has_value());
    assert(!parse_peer_form(
                "address=A6%3A05%3A04%3A03%3A02%3A01&type=1&extra=x")
                .has_value());
    assert(!parse_peer_form(std::string(97, 'x')).has_value());

    assert(kPortalPage.find("Đã kết nối trước đây") != std::string_view::npos);
    assert(kPortalPage.find("Thiết bị ở gần") != std::string_view::npos);
    assert(kPortalPage.find("/api/state") != std::string_view::npos);
    assert(kPortalPage.find("Đóng cấu hình") != std::string_view::npos);
    assert(kPortalPage.find("https://") == std::string_view::npos);
}
