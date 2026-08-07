#pragma once

#include "core/constants.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace anyadance {

// Fields every event on the driver log group carries, whatever its type. They
// are serialized and validated in one place, so a new event type cannot drift
// from the others, and a reader can present any event without knowing its shape.
struct DriverLogEnvelope {
    // Monotonic across every event the driver emits, not per event type, so the
    // reports of all types share one order. Restarts at driver startup.
    std::uint64_t sequence = 0;
    // When the driver emitted the event: milliseconds since the Unix epoch, UTC.
    // Taken from the wall clock alongside the sequence number, so it says when
    // the event happened rather than when a datagram arrived.
    //
    // This answers "when", never "in what order" — the wall clock can step
    // backwards across an NTP correction or a manual change, and two events can
    // share a millisecond. Order, deduplicate, and detect loss with `sequence`.
    //
    // Zero means the sender did not supply one, which is how a sender predating
    // this field parses.
    std::uint64_t timestampMs = 0;
    // Identical events the driver absorbed between the previous report of this
    // type and this one. Non-zero means a repeat was held rather than the stream
    // stalling. Event types that never suppress always report 0.
    std::uint64_t suppressed = 0;
    // Compact English summary, always present so a reader can render any event
    // without special-casing its type.
    std::string detail;
};

// One best-effort telemetry event emitted after the driver parses a UDP
// command. The original command remains available for inspection and resend.
struct DriverCommandLogPacket {
    DriverLogEnvelope envelope;
    std::string senderHost;
    unsigned short senderPort = 0;
    int receivedBytes = 0;
    bool accepted = false;
    std::array<bool, kDevices.size()> devices{};
    std::array<bool, kDevices.size()> yClamped{};
    std::string payload;
};

// One haptic pulse SteamVR asked a virtual controller to play. The driver only
// observes and reports it; nothing is played back, and no acknowledgement is
// expected. Values are passed through as SteamVR supplied them.
struct DriverHapticLogPacket {
    DriverLogEnvelope envelope;
    DeviceIndex device = DeviceIndex::LeftController;
    float durationSeconds = 0.0f;
    float frequencyHz = 0.0f;
    float amplitude = 0.0f;
};

// Which event a datagram on the driver log group carries. The wire form is the
// `event` string; this is its parsed form. Readers filter and dispatch on it.
//
// Unknown means the datagram is a well-formed event of a type this build does
// not know. That is not an error: the group is designed to grow new event types,
// and a reader must skip what it does not handle rather than treat the stream as
// corrupt. The envelope and `name` are still populated, so an unknown event can
// be logged or displayed generically.
enum class DriverLogEventType {
    Unknown,
    CommandProcessed,
    HapticVibration,
};

// Wire name for an event type. Empty for Unknown, whose name lives in the parsed
// event itself.
const char* DriverLogEventName(DriverLogEventType type);

struct DriverLogEvent {
    DriverLogEventType type = DriverLogEventType::Unknown;
    std::string name;             // the `event` string exactly as received
    DriverLogEnvelope envelope;   // always populated for a well-formed event
    DriverCommandLogPacket command;  // valid when type is CommandProcessed
    DriverHapticLogPacket haptic;    // valid when type is HapticVibration
};

std::string SerializeDriverCommandLog(const DriverCommandLogPacket& packet);
bool ParseDriverCommandLog(std::string_view json, DriverCommandLogPacket& packet);
bool ParseDriverCommandLogBytes(const char* data, int size, DriverCommandLogPacket& packet);

std::string SerializeDriverHapticLog(const DriverHapticLogPacket& packet);
bool ParseDriverHapticLog(std::string_view json, DriverHapticLogPacket& packet);

// Parses any event on the driver log group. Unknown event names are rejected so
// a reader never mistakes one event's shape for another's.
bool ParseDriverLogBytes(const char* data, int size, DriverLogEvent& event);

} // namespace anyadance
