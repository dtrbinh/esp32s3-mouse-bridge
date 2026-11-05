#include "hid_parser/mouse_decoder.hpp"
#include "hid_parser/keyboard_decoder.hpp"
#include "fixtures/boot_mouse_descriptor.hpp"
#include "fixtures/composite_mouse_keyboard_descriptor.hpp"
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

    const auto keyboard =
        parse_keyboard_descriptor(kCompositeMouseKeyboardDescriptor);
    assert(keyboard.status == ParseStatus::Ok);
    assert(keyboard.layout.has_report_id);
    assert(keyboard.layout.report_id == 6);
    assert(keyboard.layout.key_array_valid);

    // Left Shift + Q, held state, release, then E. The decoder preserves the
    // complete state needed by the USB side to represent press/hold/release.
    const std::array<uint8_t, 9> q_pressed{
        0x06, 0x02, 0x00, 0x14, 0, 0, 0, 0, 0};
    mouse_bridge::KeyboardReport keys{};
    assert(decode_keyboard_report(keyboard.layout, q_pressed, keys) ==
           DecodeStatus::Ok);
    assert(keys.modifiers == 0x02 && keys.keys[0] == 0x14);
    mouse_bridge::KeyboardReport held{};
    assert(decode_keyboard_report(keyboard.layout, q_pressed, held) ==
           DecodeStatus::Ok);
    assert(held == keys);

    const std::array<uint8_t, 9> released{0x06, 0, 0, 0, 0, 0, 0, 0, 0};
    assert(decode_keyboard_report(keyboard.layout, released, keys) ==
           DecodeStatus::Ok);
    assert(keys == mouse_bridge::KeyboardReport{});
    const std::array<uint8_t, 9> e_pressed{
        0x06, 0, 0, 0x08, 0, 0, 0, 0, 0};
    assert(decode_keyboard_report(keyboard.layout, e_pressed, keys) ==
           DecodeStatus::Ok);
    assert(keys.keys[0] == 0x08);
}

