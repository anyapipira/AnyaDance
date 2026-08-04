#pragma once

#include "core/constants.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace anyadance {

// One best-effort telemetry event emitted after the driver parses a UDP
// command. The original command remains available for inspection and resend.
struct DriverCommandLogPacket {
    std::uint64_t sequence = 0;
    // Identical commands the driver absorbed between the previous report and
    // this one. Non-zero means a pose was held rather than the stream stalling.
    std::uint64_t suppressed = 0;
    std::string senderHost;
    unsigned short senderPort = 0;
    int receivedBytes = 0;
    bool accepted = false;
    std::array<bool, kDevices.size()> devices{};
    std::array<bool, kDevices.size()> yClamped{};
    std::string payload;
    std::string detail;
};

// One haptic pulse SteamVR asked a virtual controller to play. The driver only
// observes and reports it; nothing is played back, and no acknowledgement is
// expected. Values are passed through as SteamVR supplied them.
struct DriverHapticLogPacket {
    std::uint64_t sequence = 0;
    DeviceIndex device = DeviceIndex::LeftController;
    float durationSeconds = 0.0f;
    float frequencyHz = 0.0f;
    float amplitude = 0.0f;
};

// Which event a datagram on the driver log group carries. Readers dispatch on
// this so a stream carrying more than one event type stays parseable.
enum class DriverLogEventType {
    CommandProcessed,
    HapticVibration,
};

struct DriverLogEvent {
    DriverLogEventType type = DriverLogEventType::CommandProcessed;
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
