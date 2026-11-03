#include "hid_parser/mouse_decoder.hpp"
#include "fixtures/boot_mouse_descriptor.hpp"
#include "fixtures/composite_report_id_mouse_descriptor.hpp"

#include <array>
#include <cassert>

using namespace hid_parser;

int main() {
    const auto boot = parse_mouse_descriptor(kBootMouseDescriptor);
    assert(boot.status == ParseStatus::Ok);
    assert(!boot.layout.has_report_id);
    assert(boot.layout.button_count == 3);

    const std::array<uint8_t, 4> boot_report{0x05, 0xFB, 0x07, 0xFF};
    mouse_bridge::MouseReport decoded{};
    assert(decode_mouse_report(boot.layout, boot_report, decoded) ==
           DecodeStatus::Ok);
    assert(decoded.buttons == 0x0005);
    assert(decoded.x == -5 && decoded.y == 7 && decoded.wheel == -1);

    const auto composite = parse_mouse_descriptor(kCompositeMouseDescriptor);
    assert(composite.status == ParseStatus::Ok);
    assert(composite.layout.has_report_id);
    assert(composite.layout.report_id == 5);
    assert(composite.layout.button_count == 16);

    const std::array<uint8_t, 7> report_id_packet{
        0x05, 0x01, 0x80, 0xFF, 0xFF, 0x7F, 0xFE};
    decoded = {};
    assert(decode_mouse_report(composite.layout, report_id_packet, decoded) ==
           DecodeStatus::Ok);
    assert(decoded.buttons == 0x8001);
    assert(decoded.x == -1 && decoded.y == 2047 && decoded.wheel == -2);

    const std::array<uint8_t, 6> truncated{0x05, 0, 0, 0, 0, 0};
    assert(decode_mouse_report(composite.layout, truncated, decoded) ==
           DecodeStatus::Truncated);
    auto wrong_id = report_id_packet;
    wrong_id[0] = 4;
    assert(decode_mouse_report(composite.layout, wrong_id, decoded) ==
           DecodeStatus::WrongReportId);

    auto absolute = kBootMouseDescriptor;
    absolute[49] = 0x02;  // X/Y/Wheel Input: Data,Var,Abs.
    assert(parse_mouse_descriptor(absolute).status ==
           ParseStatus::UnsupportedAbsolutePointer);

    const std::array<uint8_t, 3> malformed{0xFE, 0x01, 0x00};
    assert(parse_mouse_descriptor(malformed).status == ParseStatus::LongItem);
}

