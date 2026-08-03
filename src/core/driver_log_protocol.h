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
    std::string senderHost;
    unsigned short senderPort = 0;
    int receivedBytes = 0;
    bool accepted = false;
    std::array<bool, kDevices.size()> devices{};
    std::array<bool, kDevices.size()> yClamped{};
    std::string payload;
    std::string detail;
};

std::string SerializeDriverCommandLog(const DriverCommandLogPacket& packet);
bool ParseDriverCommandLog(std::string_view json, DriverCommandLogPacket& packet);
bool ParseDriverCommandLogBytes(const char* data, int size, DriverCommandLogPacket& packet);

} // namespace anyadance
