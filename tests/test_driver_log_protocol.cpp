#include "test_framework.h"
#include "tests.h"

#include "core/driver_log_protocol.h"

#include <string>

namespace anyadance::tests {
namespace {
void TestDriverHapticLog();
}

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

    // A command report still dispatches as one through the shared reader.
    DriverLogEvent event;
    EXPECT_TRUE(ParseDriverLogBytes(encoded.data(), static_cast<int>(encoded.size()), event));
    EXPECT_TRUE(event.type == DriverLogEventType::CommandProcessed);
    EXPECT_TRUE(event.command.sequence == 42);

    TestDriverHapticLog();
}

namespace {

void TestDriverHapticLog() {
    DriverHapticLogPacket source;
    source.sequence = 7;
    source.device = DeviceIndex::RightController;
    source.durationSeconds = 0.125f;
    source.frequencyHz = 160.5f;
    source.amplitude = 0.75f;

    const std::string encoded = SerializeDriverHapticLog(source);
    DriverHapticLogPacket parsed;
    EXPECT_TRUE(ParseDriverHapticLog(encoded, parsed));
    EXPECT_TRUE(parsed.sequence == 7);
    EXPECT_TRUE(parsed.device == DeviceIndex::RightController);
    EXPECT_NEAR(parsed.durationSeconds, 0.125f, 0.000001f);
    EXPECT_NEAR(parsed.frequencyHz, 160.5f, 0.0001f);
    EXPECT_NEAR(parsed.amplitude, 0.75f, 0.000001f);

    // Haptic events ride the same group as command reports, so the shared reader
    // must tell them apart rather than guess at a shape.
    DriverLogEvent event;
    EXPECT_TRUE(ParseDriverLogBytes(encoded.data(), static_cast<int>(encoded.size()), event));
    EXPECT_TRUE(event.type == DriverLogEventType::HapticVibration);
    EXPECT_TRUE(event.haptic.device == DeviceIndex::RightController);
    // A haptic event must not parse as a command report, and vice versa.
    DriverCommandLogPacket asCommand;
    EXPECT_FALSE(ParseDriverCommandLog(encoded, asCommand));

    const std::string unknownEvent =
        "{\"version\":1,\"event\":\"future_event\",\"sequence\":1}";
    EXPECT_FALSE(ParseDriverLogBytes(
        unknownEvent.data(), static_cast<int>(unknownEvent.size()), event));

    std::string unknownDevice = encoded;
    unknownDevice.replace(
        unknownDevice.find("right_controller"),
        std::string("right_controller").size(),
        "third_controller");
    EXPECT_FALSE(ParseDriverHapticLog(unknownDevice, parsed));

    // Nonsense values are rejected; the driver forwards what SteamVR gave it,
    // so the bounds only guard against garbage.
    std::string negativeAmplitude = encoded;
    negativeAmplitude.replace(
        negativeAmplitude.find("\"amplitude\":0.75"),
        std::string("\"amplitude\":0.75").size(),
        "\"amplitude\":-0.5");
    EXPECT_FALSE(ParseDriverHapticLog(negativeAmplitude, parsed));

    std::string tooLoud = encoded;
    tooLoud.replace(
        tooLoud.find("\"amplitude\":0.75"),
        std::string("\"amplitude\":0.75").size(),
        "\"amplitude\":2");
    EXPECT_FALSE(ParseDriverHapticLog(tooLoud, parsed));

    std::string missingHaptic =
        "{\"version\":1,\"event\":\"haptic_vibration\",\"sequence\":1,"
        "\"device\":\"left_controller\"}";
    EXPECT_FALSE(ParseDriverHapticLog(missingHaptic, parsed));

    // A zero-length pulse is a legitimate stop request, not garbage.
    DriverHapticLogPacket stopPulse;
    stopPulse.device = DeviceIndex::LeftController;
    const std::string stopEncoded = SerializeDriverHapticLog(stopPulse);
    EXPECT_TRUE(ParseDriverHapticLog(stopEncoded, parsed));
    EXPECT_NEAR(parsed.durationSeconds, 0.0f, 0.000001f);
    EXPECT_TRUE(parsed.device == DeviceIndex::LeftController);
}

} // namespace

} // namespace anyadance::tests
