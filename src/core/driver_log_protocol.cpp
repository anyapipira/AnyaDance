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

// Writes the fields common to every event, up to and including the opening of
// the event-specific part. Every serializer starts here so the envelope cannot
// diverge between event types.
void AppendEnvelope(
    std::string& out,
    const char* eventName,
    const DriverLogEnvelope& envelope) {
    out += "{\"version\":";
    out += std::to_string(kDriverLogProtocolVersion);
    out += ",\"event\":";
    AppendEscaped(out, eventName);
    out += ",\"sequence\":";
    out += std::to_string(envelope.sequence);
    out += ",\"timestamp_ms\":";
    out += std::to_string(envelope.timestampMs);
    out += ",\"suppressed\":";
    out += std::to_string(envelope.suppressed);
    out += ",\"detail\":";
    AppendEscaped(out, envelope.detail);
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

// Validates the fields common to every event. Each event type calls this before
// reading its own, so all of them agree on the envelope and reject the same
// malformed input.
bool ParseEnvelope(
    const json::Value& root,
    const char* expectedEvent,
    DriverLogEnvelope& envelope) {
    envelope = {};
    const json::Value* version = Required(root, "version", json::Type::Number);
    const json::Value* event = Required(root, "event", json::Type::String);
    const json::Value* sequence = Required(root, "sequence", json::Type::Number);
    const json::Value* detail = Required(root, "detail", json::Type::String);
    if (!version || version->number != kDriverLogProtocolVersion || !event ||
        event->string != expectedEvent || !sequence || !detail ||
        !ParseNonNegativeInteger(*sequence, envelope.sequence)) {
        envelope = {};
        return false;
    }

    // Optional so a version 1 sender that predates hold suppression still
    // parses; absent means the sender reported every event it produced.
    const json::Value* suppressed = root.Find("suppressed");
    if (suppressed && !ParseNonNegativeInteger(*suppressed, envelope.suppressed)) {
        envelope = {};
        return false;
    }

    // Optional for the same reason: a sender predating the field is still a
    // valid version 1 sender, and every other field remains usable without it.
    // Absent leaves it zero, which a reader reads as "no driver time supplied".
    const json::Value* timestamp = root.Find("timestamp_ms");
    if (timestamp && !ParseNonNegativeInteger(*timestamp, envelope.timestampMs)) {
        envelope = {};
        return false;
    }
    envelope.detail = detail->string;
    return true;
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
    AppendEnvelope(out, "command_processed", packet.envelope);
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
    out += "}}";
    return out;
}

// Internal: the caller has already parsed the datagram and dispatched on its
// event name, so these read a known-good root rather than re-parsing text.
static bool ParseDriverCommandLogRoot(const json::Value& root, DriverCommandLogPacket& packet) {
    packet = {};
    if (!ParseEnvelope(root, "command_processed", packet.envelope)) {
        packet = {};
        return false;
    }

    const json::Value* source = Required(root, "source", json::Type::Object);
    const json::Value* command = Required(root, "command", json::Type::Object);
    if (!source || !command) {
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
    if (static_cast<int>(packet.payload.size()) != packet.receivedBytes) {
        packet = {};
        return false;
    }
    return true;
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
    out.reserve(256);
    AppendEnvelope(out, "haptic_vibration", packet.envelope);
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

static bool ParseDriverHapticLogRoot(const json::Value& root, DriverHapticLogPacket& packet) {
    packet = {};
    if (!ParseEnvelope(root, "haptic_vibration", packet.envelope)) {
        packet = {};
        return false;
    }

    const json::Value* device = Required(root, "device", json::Type::String);
    const json::Value* haptic = Required(root, "haptic", json::Type::Object);
    if (!device || !haptic) {
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

bool ParseDriverCommandLog(std::string_view text, DriverCommandLogPacket& packet) {
    packet = {};
    const auto root = json::Parse(std::string(text));
    if (!root || root->type != json::Type::Object) {
        return false;
    }
    return ParseDriverCommandLogRoot(*root, packet);
}

bool ParseDriverHapticLog(std::string_view text, DriverHapticLogPacket& packet) {
    packet = {};
    const auto root = json::Parse(std::string(text));
    if (!root || root->type != json::Type::Object) {
        return false;
    }
    return ParseDriverHapticLogRoot(*root, packet);
}

const char* DriverLogEventName(DriverLogEventType type) {
    switch (type) {
    case DriverLogEventType::CommandProcessed:
        return "command_processed";
    case DriverLogEventType::HapticVibration:
        return "haptic_vibration";
    case DriverLogEventType::Unknown:
        break;
    }
    return "";
}

bool ParseDriverLogBytes(const char* data, int size, DriverLogEvent& event) {
    event = {};
    if (!data || size <= 0 || size > kMaxDriverLogPacketBytes) {
        return false;
    }

    // Parse once and dispatch on the event name, so one group can carry several
    // event shapes without a reader guessing which fields to expect and without
    // re-parsing the same datagram per candidate type.
    const auto root = json::Parse(std::string(data, static_cast<std::size_t>(size)));
    if (!root || root->type != json::Type::Object) {
        return false;
    }
    const json::Value* name = Required(*root, "event", json::Type::String);
    if (!name) {
        return false;
    }
    event.name = name->string;

    if (name->string == DriverLogEventName(DriverLogEventType::CommandProcessed)) {
        event.type = DriverLogEventType::CommandProcessed;
        if (!ParseDriverCommandLogRoot(*root, event.command)) {
            event = {};
            return false;
        }
        event.envelope = event.command.envelope;
        return true;
    }
    if (name->string == DriverLogEventName(DriverLogEventType::HapticVibration)) {
        event.type = DriverLogEventType::HapticVibration;
        if (!ParseDriverHapticLogRoot(*root, event.haptic)) {
            event = {};
            return false;
        }
        event.envelope = event.haptic.envelope;
        return true;
    }

    // A well-formed event of a type this build does not know. The envelope is
    // common to every event, so it still parses and the reader can skip or
    // display the event generically instead of treating the stream as corrupt.
    event.type = DriverLogEventType::Unknown;
    if (!ParseEnvelope(*root, name->string.c_str(), event.envelope)) {
        event = {};
        return false;
    }
    return true;
}

} // namespace anyadance
