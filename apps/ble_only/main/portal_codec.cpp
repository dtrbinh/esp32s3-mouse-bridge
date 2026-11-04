#include "portal_codec.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <string>

namespace {
class Writer {
public:
    explicit Writer(std::span<char> output) : output_(output) {}

    void append(std::string_view text) {
        if (!valid_ || position_ + text.size() > output_.size()) {
            valid_ = false;
            return;
        }
        std::copy(text.begin(), text.end(), output_.begin() + position_);
        position_ += text.size();
    }

    void number(int value) {
        std::array<char, 16> buffer{};
        const auto result =
            std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
        if (result.ec != std::errc{}) {
            valid_ = false;
            return;
        }
        append(std::string_view(buffer.data(), result.ptr));
    }

    void boolean(bool value) { append(value ? "true" : "false"); }

    void json_string(std::string_view text) {
        append("\"");
        size_t i = 0;
        while (i < text.size()) {
            const auto byte = static_cast<unsigned char>(text[i]);
            if (byte == '"') {
                append("\\\"");
                ++i;
            } else if (byte == '\\') {
                append("\\\\");
                ++i;
            } else if (byte < 0x20) {
                constexpr char hex[] = "0123456789ABCDEF";
                const std::array<char, 6> escaped{
                    '\\', 'u', '0', '0', hex[byte >> 4], hex[byte & 0x0F]};
                append(std::string_view(escaped.data(), escaped.size()));
                ++i;
            } else if (byte < 0x80) {
                append(text.substr(i, 1));
                ++i;
            } else {
                size_t length = 0;
                if (byte >= 0xC2 && byte <= 0xDF) length = 2;
                if (byte >= 0xE0 && byte <= 0xEF) length = 3;
                if (byte >= 0xF0 && byte <= 0xF4) length = 4;
                bool valid = length != 0 && i + length <= text.size();
                for (size_t j = 1; valid && j < length; ++j) {
                    valid = (static_cast<unsigned char>(text[i + j]) & 0xC0) ==
                            0x80;
                }
                if (valid && length == 3) {
                    const auto second =
                        static_cast<unsigned char>(text[i + 1]);
                    valid = !((byte == 0xE0 && second < 0xA0) ||
                              (byte == 0xED && second >= 0xA0));
                }
                if (valid && length == 4) {
                    const auto second =
                        static_cast<unsigned char>(text[i + 1]);
                    valid = !((byte == 0xF0 && second < 0x90) ||
                              (byte == 0xF4 && second >= 0x90));
                }
                if (valid) {
                    append(text.substr(i, length));
                    i += length;
                } else {
                    append("\\uFFFD");
                    ++i;
                }
            }
        }
        append("\"");
    }

    [[nodiscard]] bool valid() const { return valid_; }
    [[nodiscard]] size_t size() const { return position_; }

private:
    std::span<char> output_;
    size_t position_{};
    bool valid_{true};
};

std::string_view bounded_name(const std::array<char, 33>& name) {
    size_t length = 0;
    while (length < name.size() && name[length] != '\0') ++length;
    return {name.data(), length};
}

std::string_view phase_name(BleSetupPhase phase) {
    switch (phase) {
    case BleSetupPhase::Idle: return "idle";
    case BleSetupPhase::ScanningSelected: return "scanning";
    case BleSetupPhase::Portal: return "portal";
    case BleSetupPhase::SwitchDisconnect: return "switching";
    case BleSetupPhase::Connecting: return "connecting";
    case BleSetupPhase::Connected: return "connected";
    case BleSetupPhase::SuccessDelay: return "success";
    case BleSetupPhase::Failed: return "failed";
    }
    return "idle";
}

std::string_view error_name(BleAttemptError error) {
    switch (error) {
    case BleAttemptError::None: return "none";
    case BleAttemptError::ConnectionFailed: return "connection_failed";
    case BleAttemptError::NotMouse: return "not_mouse";
    case BleAttemptError::PersistenceFailed: return "persistence_failed";
    }
    return "none";
}

void write_address(Writer& writer, const BlePeerAddress& address) {
    std::array<char, 18> formatted{};
    if (format_ble_address(address, formatted)) {
        writer.json_string(formatted.data());
    } else {
        writer.json_string("");
    }
}

bool is_saved(const PortalView& view, BlePeerAddress address) {
    for (size_t i = 0; i < view.saved.count; ++i) {
        if (view.saved.devices[i].identity == address) return true;
    }
    return false;
}

int hex_value(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

std::optional<std::string> url_decode(std::string_view input) {
    std::string output;
    output.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '%') {
            if (i + 2 >= input.size()) return std::nullopt;
            const int high = hex_value(input[i + 1]);
            const int low = hex_value(input[i + 2]);
            if (high < 0 || low < 0) return std::nullopt;
            output.push_back(static_cast<char>((high << 4) | low));
            i += 2;
        } else if (input[i] == '+') {
            output.push_back(' ');
        } else {
            output.push_back(input[i]);
        }
    }
    return output;
}
}  // namespace

std::optional<size_t> write_portal_json(const PortalView& view,
                                        std::span<char> output) {
    Writer writer(output);
    writer.append("{\"portalActive\":");
    writer.boolean(view.setup.portal_active);
    writer.append(",\"portalDesired\":");
    writer.boolean(view.setup.portal_desired);
    writer.append(",\"phase\":");
    writer.json_string(phase_name(view.setup.phase));
    writer.append(",\"error\":");
    writer.json_string(error_name(view.setup.error));
    writer.append(",\"saved\":[");
    size_t saved_written = 0;
    for (size_t i = 0; i < view.saved.count; ++i) {
        const auto& device = view.saved.devices[i];
        if (device.name[0] == '\0') continue;
        if (saved_written++ != 0) writer.append(",");
        writer.append("{\"name\":");
        writer.json_string(bounded_name(device.name));
        writer.append(",\"address\":");
        write_address(writer, device.identity);
        writer.append(",\"type\":");
        writer.number(device.identity.type);
        writer.append(",\"connected\":");
        writer.boolean(device.connected);
        writer.append("}");
    }
    writer.append("],\"nearby\":[");
    size_t nearby_written = 0;
    for (size_t i = 0; i < view.nearby.count; ++i) {
        const auto& device = view.nearby.devices[i];
        if (device.name[0] == '\0') continue;
        if (nearby_written++ != 0) writer.append(",");
        writer.append("{\"name\":");
        writer.json_string(bounded_name(device.name));
        writer.append(",\"address\":");
        write_address(writer, device.address);
        writer.append(",\"type\":");
        writer.number(device.address.type);
        writer.append(",\"rssi\":");
        writer.number(device.rssi);
        writer.append(",\"hid\":");
        writer.boolean(device.advertises_hid);
        writer.append(",\"saved\":");
        writer.boolean(is_saved(view, device.address));
        writer.append(",\"busy\":");
        writer.boolean(view.setup.attempt == device.address);
        writer.append("}");
    }
    writer.append("]}");
    if (!writer.valid()) {
        if (!output.empty()) output[0] = '\0';
        return std::nullopt;
    }
    return writer.size();
}

std::optional<BlePeerAddress> parse_peer_form(std::string_view body) {
    if (body.empty() || body.size() > 96) return std::nullopt;
    std::optional<std::string> address_value;
    std::optional<std::string> type_value;
    size_t position = 0;
    while (position <= body.size()) {
        const size_t end = body.find('&', position);
        const std::string_view field = body.substr(
            position, end == std::string_view::npos ? body.size() - position
                                                    : end - position);
        const size_t equals = field.find('=');
        if (equals == std::string_view::npos) return std::nullopt;
        const auto decoded = url_decode(field.substr(equals + 1));
        if (!decoded.has_value()) return std::nullopt;
        const auto key = field.substr(0, equals);
        if (key == "address" && !address_value.has_value()) {
            address_value = decoded;
        } else if (key == "type" && !type_value.has_value()) {
            type_value = decoded;
        } else {
            return std::nullopt;
        }
        if (end == std::string_view::npos) break;
        position = end + 1;
    }
    if (!address_value.has_value() || !type_value.has_value() ||
        address_value->size() != 17 || type_value->size() != 1 ||
        ((*type_value)[0] != '0' && (*type_value)[0] != '1')) {
        return std::nullopt;
    }
    std::array<uint8_t, 6> displayed{};
    for (size_t i = 0; i < displayed.size(); ++i) {
        const size_t offset = i * 3;
        if (i != 5 && (*address_value)[offset + 2] != ':') {
            return std::nullopt;
        }
        const int high = hex_value((*address_value)[offset]);
        const int low = hex_value((*address_value)[offset + 1]);
        if (high < 0 || low < 0) return std::nullopt;
        displayed[i] = static_cast<uint8_t>((high << 4) | low);
    }
    BlePeerAddress result{};
    std::reverse_copy(displayed.begin(), displayed.end(), result.bytes.begin());
    result.type = static_cast<uint8_t>((*type_value)[0] - '0');
    return result;
}
