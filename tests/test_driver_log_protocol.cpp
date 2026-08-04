#include "test_framework.h"
#include "tests.h"

#include "core/driver_log_protocol.h"

#include <string>

namespace anyadance::tests {

void TestDriverLogProtocol() {
    DriverCommandLogPacket source;
    source.sequence = 42;
    source.suppressed = 613;
    source.senderHost = "127.0.0.1";
    source.senderPort = 54321;
    source.accepted = true;
    source.devices[DeviceSlot(DeviceIndex::Hmd)] = true;
    source.devices[DeviceSlot(DeviceIndex::LeftController)] = true;
    source.yClamped[DeviceSlot(DeviceIndex::Hmd)] = true;
    source.payload = "{\"version\":1,\"note\":\"line\\nquote \\\"\"}";
    source.receivedBytes = static_cast<int>(source.payload.size());
    source.detail = "accepted 2 device entries";

    const std::string encoded = SerializeDriverCommandLog(source);
    DriverCommandLogPacket parsed;
    EXPECT_TRUE(ParseDriverCommandLog(encoded, parsed));
    EXPECT_TRUE(parsed.sequence == 42);
    EXPECT_TRUE(parsed.suppressed == 613);
    EXPECT_TRUE(parsed.senderHost == "127.0.0.1");
    EXPECT_TRUE(parsed.senderPort == 54321);
    EXPECT_TRUE(parsed.accepted);
    EXPECT_TRUE(parsed.devices[DeviceSlot(DeviceIndex::Hmd)]);
    EXPECT_TRUE(parsed.devices[DeviceSlot(DeviceIndex::LeftController)]);
    EXPECT_TRUE(parsed.yClamped[DeviceSlot(DeviceIndex::Hmd)]);
    EXPECT_TRUE(parsed.payload == source.payload);
    EXPECT_TRUE(parsed.detail == source.detail);

    std::string wrongVersion = encoded;
    wrongVersion.replace(wrongVersion.find("\"version\":1"), 11, "\"version\":2");
    EXPECT_FALSE(ParseDriverCommandLog(wrongVersion, parsed));

    std::string wrongEvent = encoded;
    wrongEvent.replace(
        wrongEvent.find("command_processed"),
        std::string("command_processed").size(),
        "unknown_event");
    EXPECT_FALSE(ParseDriverCommandLog(wrongEvent, parsed));

    // "suppressed" is optional so a sender that reports every command still
    // parses, but a present value must be a non-negative integer.
    std::string withoutSuppressed = encoded;
    withoutSuppressed.replace(
        withoutSuppressed.find(",\"suppressed\":613"),
        std::string(",\"suppressed\":613").size(),
        "");
    EXPECT_TRUE(ParseDriverCommandLog(withoutSuppressed, parsed));
    EXPECT_TRUE(parsed.suppressed == 0);

    std::string negativeSuppressed = encoded;
    negativeSuppressed.replace(
        negativeSuppressed.find("\"suppressed\":613"),
        std::string("\"suppressed\":613").size(),
        "\"suppressed\":-1");
    EXPECT_FALSE(ParseDriverCommandLog(negativeSuppressed, parsed));

    std::string nonNumericSuppressed = encoded;
    nonNumericSuppressed.replace(
        nonNumericSuppressed.find("\"suppressed\":613"),
        std::string("\"suppressed\":613").size(),
        "\"suppressed\":\"3\"");
    EXPECT_FALSE(ParseDriverCommandLog(nonNumericSuppressed, parsed));

    const std::string malformed =
        "{\"version\":1,\"event\":\"command_processed\"}";
    EXPECT_FALSE(ParseDriverCommandLog(malformed, parsed));
    EXPECT_FALSE(ParseDriverCommandLogBytes(nullptr, 0, parsed));

    DriverCommandLogPacket worstCase = source;
    worstCase.payload.assign(
        static_cast<std::size_t>(kMaxPacketBytes) - 1,
        '\x01');
    worstCase.receivedBytes = static_cast<int>(worstCase.payload.size());
    const std::string worstEncoded = SerializeDriverCommandLog(worstCase);
    EXPECT_TRUE(worstEncoded.size() <=
        static_cast<std::size_t>(kMaxDriverLogPacketBytes));
    EXPECT_TRUE(ParseDriverCommandLog(worstEncoded, parsed));
    EXPECT_TRUE(parsed.payload == worstCase.payload);
}

} // namespace anyadance::tests
