#include "core/driver_log_protocol.h"

#include "core/json.h"

#include <cmath>
#include <cstdio>

namespace anyadance {
namespace {

void AppendEscaped(std::string& out, std::string_view value) {
    static constexpr char kHex[] = "0123456789abcdef";
    out.push_back('"');
    for (const unsigned char ch : value) {
        switch (ch) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (ch < 0x20) {
                out += "\\u00";
                out.push_back(kHex[(ch >> 4) & 0x0f]);
                out.push_back(kHex[ch & 0x0f]);
            } else {
                out.push_back(static_cast<char>(ch));
            }
            break;
        }
    }
    out.push_back('"');
}

void AppendDeviceArray(
    std::string& out,
    const std::array<bool, kDevices.size()>& present) {
    out.push_back('[');
    bool first = true;
    for (std::size_t i = 0; i < present.size(); ++i) {
        if (!present[i]) {
            continue;
        }
        if (!first) {
            out.push_back(',');
        }
        AppendEscaped(out, kDevices[i].id);
        first = false;
    }
    out.push_back(']');
}

const json::Value* Required(
    const json::Value& object,
    const char* key,
    json::Type type) {
    const json::Value* value = object.Find(key);
    return value && value->type == type ? value : nullptr;
}

bool ParseNonNegativeInteger(const json::Value& value, std::uint64_t& result) {
    if (value.type != json::Type::Number || !std::isfinite(value.number) ||
        value.number < 0.0 || std::floor(value.number) != value.number ||
        value.number > 9007199254740991.0) {
        return false;
    }
    result = static_cast<std::uint64_t>(value.number);
    return true;
}

bool ParseBoundedInt(const json::Value& value, int minValue, int maxValue, int& result) {
    if (value.type != json::Type::Number || !std::isfinite(value.number) ||
        std::floor(value.number) != value.number || value.number < minValue ||
        value.number > maxValue) {
        return false;
    }
    result = static_cast<int>(value.number);
    return true;
}

// Compact round-trippable float. %.9g preserves a float exactly and drops the
// trailing zeros a fixed format would emit.
void AppendFloat(std::string& out, float value) {
    char buffer[32];
    const int written = std::snprintf(buffer, sizeof(buffer), "%.9g", static_cast<double>(value));
    if (written <= 0 || written >= static_cast<int>(sizeof(buffer))) {
        out += '0';
        return;
    }
    out.append(buffer, static_cast<std::size_t>(written));
}

bool ParseFiniteFloat(const json::Value& value, float low, float high, float& result) {
    if (value.type != json::Type::Number || !std::isfinite(value.number) ||
        value.number < low || value.number > high) {
        return false;
    }
    result = static_cast<float>(value.number);
    return true;
}

bool DeviceSlotForId(const std::string& id, std::size_t& slot) {
    for (std::size_t i = 0; i < kDevices.size(); ++i) {
        if (id == kDevices[i].id) {
            slot = i;
            return true;
        }
    }
    return false;
}

bool ParseDeviceArray(
    const json::Value& value,
    std::array<bool, kDevices.size()>& present) {
    if (value.type != json::Type::Array) {
        return false;
    }
    present = {};
    for (const json::Value& item : value.array) {
        if (item.type != json::Type::String) {
            return false;
        }
        std::size_t slot = 0;
        if (!DeviceSlotForId(item.string, slot)) {
            return false;
        }
        present[slot] = true;
    }
    return true;
}

} // namespace

std::string SerializeDriverCommandLog(const DriverCommandLogPacket& packet) {
    std::string out;
    out.reserve(packet.payload.size() + 384);
    out += "{\"version\":";
    out += std::to_string(kDriverLogProtocolVersion);
    out += ",\"event\":\"command_processed\",\"sequence\":";
    out += std::to_string(packet.sequence);
    out += ",\"suppressed\":";
    out += std::to_string(packet.suppressed);
    out += ",\"source\":{\"host\":";
    AppendEscaped(out, packet.senderHost);
    out += ",\"port\":";
    out += std::to_string(packet.senderPort);
    out += "},\"command\":{\"protocol\":\"pose_frame\",\"bytes\":";
    out += std::to_string(packet.receivedBytes);
    out += ",\"accepted\":";
    out += packet.accepted ? "true" : "false";
    out += ",\"devices\":";
    AppendDeviceArray(out, packet.devices);
    out += ",\"y_clamped\":";
    AppendDeviceArray(out, packet.yClamped);
    out += ",\"payload\":";
    AppendEscaped(out, packet.payload);
    out += "},\"detail\":";
    AppendEscaped(out, packet.detail);
    out.push_back('}');
    return out;
}

bool ParseDriverCommandLog(std::string_view text, DriverCommandLogPacket& packet) {
    packet = {};
    const auto root = json::Parse(std::string(text));
    if (!root || root->type != json::Type::Object) {
        return false;
    }

    const json::Value* version = Required(*root, "version", json::Type::Number);
    const json::Value* event = Required(*root, "event", json::Type::String);
    const json::Value* sequence = Required(*root, "sequence", json::Type::Number);
    const json::Value* source = Required(*root, "source", json::Type::Object);
    const json::Value* command = Required(*root, "command", json::Type::Object);
    const json::Value* detail = Required(*root, "detail", json::Type::String);
    if (!version || version->number != kDriverLogProtocolVersion || !event ||
        event->string != "command_processed" || !sequence || !source ||
        !command || !detail || !ParseNonNegativeInteger(*sequence, packet.sequence)) {
        packet = {};
        return false;
    }

    // Optional so a version 1 sender that predates hold suppression still
    // parses; absent means the sender reported every command it processed.
    const json::Value* suppressed = root->Find("suppressed");
    if (suppressed && !ParseNonNegativeInteger(*suppressed, packet.suppressed)) {
        packet = {};
        return false;
    }

    const json::Value* host = Required(*source, "host", json::Type::String);
    const json::Value* port = Required(*source, "port", json::Type::Number);
    const json::Value* protocol = Required(*command, "protocol", json::Type::String);
    const json::Value* bytes = Required(*command, "bytes", json::Type::Number);
    const json::Value* accepted = Required(*command, "accepted", json::Type::Bool);
    const json::Value* devices = Required(*command, "devices", json::Type::Array);
    const json::Value* yClamped = Required(*command, "y_clamped", json::Type::Array);
    const json::Value* payload = Required(*command, "payload", json::Type::String);
    int parsedPort = 0;
    if (!host || host->string.empty() || !port ||
        !ParseBoundedInt(*port, 0, 65535, parsedPort) || !protocol ||
        protocol->string != "pose_frame" || !bytes ||
        !ParseBoundedInt(*bytes, 0, kMaxPacketBytes, packet.receivedBytes) ||
        !accepted || !devices || !ParseDeviceArray(*devices, packet.devices) ||
        !yClamped || !ParseDeviceArray(*yClamped, packet.yClamped) || !payload) {
        packet = {};
        return false;
    }

    packet.senderHost = host->string;
    packet.senderPort = static_cast<unsigned short>(parsedPort);
    packet.accepted = accepted->boolean;
    packet.payload = payload->string;
    packet.detail = detail->string;
    return static_cast<int>(packet.payload.size()) == packet.receivedBytes;
}

bool ParseDriverCommandLogBytes(const char* data, int size, DriverCommandLogPacket& packet) {
    if (!data || size <= 0 || size > kMaxDriverLogPacketBytes) {
        packet = {};
        return false;
    }
    return ParseDriverCommandLog(
        std::string_view(data, static_cast<std::size_t>(size)), packet);
}

std::string SerializeDriverHapticLog(const DriverHapticLogPacket& packet) {
    std::string out;
    out.reserve(192);
    out += "{\"version\":";
    out += std::to_string(kDriverLogProtocolVersion);
    out += ",\"event\":\"haptic_vibration\",\"sequence\":";
    out += std::to_string(packet.sequence);
    out += ",\"device\":";
    AppendEscaped(out, kDevices[DeviceSlot(packet.device)].id);
    out += ",\"haptic\":{\"duration_seconds\":";
    AppendFloat(out, packet.durationSeconds);
    out += ",\"frequency_hz\":";
    AppendFloat(out, packet.frequencyHz);
    out += ",\"amplitude\":";
    AppendFloat(out, packet.amplitude);
    out += "}}";
    return out;
}

bool ParseDriverHapticLog(std::string_view text, DriverHapticLogPacket& packet) {
    packet = {};
    const auto root = json::Parse(std::string(text));
    if (!root || root->type != json::Type::Object) {
        return false;
    }

    const json::Value* version = Required(*root, "version", json::Type::Number);
    const json::Value* event = Required(*root, "event", json::Type::String);
    const json::Value* sequence = Required(*root, "sequence", json::Type::Number);
    const json::Value* device = Required(*root, "device", json::Type::String);
    const json::Value* haptic = Required(*root, "haptic", json::Type::Object);
    if (!version || version->number != kDriverLogProtocolVersion || !event ||
        event->string != "haptic_vibration" || !sequence || !device || !haptic ||
        !ParseNonNegativeInteger(*sequence, packet.sequence)) {
        packet = {};
        return false;
    }

    std::size_t slot = 0;
    if (!DeviceSlotForId(device->string, slot)) {
        packet = {};
        return false;
    }
    packet.device = static_cast<DeviceIndex>(slot);

    const json::Value* duration = Required(*haptic, "duration_seconds", json::Type::Number);
    const json::Value* frequency = Required(*haptic, "frequency_hz", json::Type::Number);
    const json::Value* amplitude = Required(*haptic, "amplitude", json::Type::Number);
    // Bounds are generous on purpose: the driver forwards what SteamVR supplied
    // rather than asserting a policy on it. Only nonsense is rejected.
    if (!duration || !ParseFiniteFloat(*duration, 0.0f, kMaxHapticDurationSeconds, packet.durationSeconds) ||
        !frequency || !ParseFiniteFloat(*frequency, 0.0f, kMaxHapticFrequencyHz, packet.frequencyHz) ||
        !amplitude || !ParseFiniteFloat(*amplitude, 0.0f, 1.0f, packet.amplitude)) {
        packet = {};
        return false;
    }
    return true;
}

bool ParseDriverLogBytes(const char* data, int size, DriverLogEvent& event) {
    event = {};
    if (!data || size <= 0 || size > kMaxDriverLogPacketBytes) {
        return false;
    }
    const std::string_view text(data, static_cast<std::size_t>(size));

    // Dispatch on the event name so one group can carry several event shapes
    // without a reader guessing which fields to expect.
    const auto root = json::Parse(std::string(text));
    if (!root || root->type != json::Type::Object) {
        return false;
    }
    const json::Value* name = Required(*root, "event", json::Type::String);
    if (!name) {
        return false;
    }
    if (name->string == "command_processed") {
        event.type = DriverLogEventType::CommandProcessed;
        return ParseDriverCommandLog(text, event.command);
    }
    if (name->string == "haptic_vibration") {
        event.type = DriverLogEventType::HapticVibration;
        return ParseDriverHapticLog(text, event.haptic);
    }
    return false;
}

} // namespace anyadance
