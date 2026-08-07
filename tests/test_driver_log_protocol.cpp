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
    source.envelope.sequence = 42;
    source.envelope.suppressed = 613;
    source.envelope.timestampMs = 1700000000123ULL;
    source.senderHost = "127.0.0.1";
    source.senderPort = 54321;
    source.accepted = true;
    source.devices[DeviceSlot(DeviceIndex::Hmd)] = true;
    source.devices[DeviceSlot(DeviceIndex::LeftController)] = true;
    source.yClamped[DeviceSlot(DeviceIndex::Hmd)] = true;
    source.payload = "{\"version\":1,\"note\":\"line\\nquote \\\"\"}";
    source.receivedBytes = static_cast<int>(source.payload.size());
    source.envelope.detail = "accepted 2 device entries";

    const std::string encoded = SerializeDriverCommandLog(source);
    DriverCommandLogPacket parsed;
    EXPECT_TRUE(ParseDriverCommandLog(encoded, parsed));
    EXPECT_TRUE(parsed.envelope.sequence == 42);
    EXPECT_TRUE(parsed.envelope.suppressed == 613);
    // Milliseconds past 2^32 must survive intact: the field is 64-bit, and epoch
    // milliseconds exceeded 32 bits decades ago.
    EXPECT_TRUE(parsed.envelope.timestampMs == 1700000000123ULL);
    EXPECT_TRUE(parsed.senderHost == "127.0.0.1");
    EXPECT_TRUE(parsed.senderPort == 54321);
    EXPECT_TRUE(parsed.accepted);
    EXPECT_TRUE(parsed.devices[DeviceSlot(DeviceIndex::Hmd)]);
    EXPECT_TRUE(parsed.devices[DeviceSlot(DeviceIndex::LeftController)]);
    EXPECT_TRUE(parsed.yClamped[DeviceSlot(DeviceIndex::Hmd)]);
    EXPECT_TRUE(parsed.payload == source.payload);
    EXPECT_TRUE(parsed.envelope.detail == source.envelope.detail);

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
    EXPECT_TRUE(parsed.envelope.suppressed == 0);

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

    // "timestamp_ms" is optional for the same reason: a sender predating the
    // field is still a valid version 1 sender, and every other field stays
    // usable without it. Absent reads as "no driver time supplied".
    std::string withoutTimestamp = encoded;
    withoutTimestamp.replace(
        withoutTimestamp.find(",\"timestamp_ms\":1700000000123"),
        std::string(",\"timestamp_ms\":1700000000123").size(),
        "");
    EXPECT_TRUE(ParseDriverCommandLog(withoutTimestamp, parsed));
    EXPECT_TRUE(parsed.envelope.timestampMs == 0);

    // A present value must still be a non-negative integer, so a garbled clock
    // reading is rejected rather than silently dating a row to 1970.
    std::string negativeTimestamp = encoded;
    negativeTimestamp.replace(
        negativeTimestamp.find("\"timestamp_ms\":1700000000123"),
        std::string("\"timestamp_ms\":1700000000123").size(),
        "\"timestamp_ms\":-1");
    EXPECT_FALSE(ParseDriverCommandLog(negativeTimestamp, parsed));

    std::string nonNumericTimestamp = encoded;
    nonNumericTimestamp.replace(
        nonNumericTimestamp.find("\"timestamp_ms\":1700000000123"),
        std::string("\"timestamp_ms\":1700000000123").size(),
        "\"timestamp_ms\":\"2023-11-14T22:13:20Z\"");
    EXPECT_FALSE(ParseDriverCommandLog(nonNumericTimestamp, parsed));

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
    EXPECT_TRUE(event.command.envelope.sequence == 42);

    TestDriverHapticLog();
}

namespace {

void TestDriverHapticLog() {
    DriverHapticLogPacket source;
    source.envelope.sequence = 7;
    source.envelope.timestampMs = 1700000000456ULL;
    source.device = DeviceIndex::RightController;
    source.durationSeconds = 0.125f;
    source.frequencyHz = 160.5f;
    source.amplitude = 0.75f;

    source.envelope.detail = "0.125 s at 160.5 Hz, amplitude 0.75";

    const std::string encoded = SerializeDriverHapticLog(source);
    DriverHapticLogPacket parsed;
    EXPECT_TRUE(ParseDriverHapticLog(encoded, parsed));
    EXPECT_TRUE(parsed.envelope.sequence == 7);
    // The envelope is shared, so the time rides on every event type, not only
    // on command reports.
    EXPECT_TRUE(parsed.envelope.timestampMs == 1700000000456ULL);
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

    // The envelope arrives on every event, so a reader can render a haptic event
    // without knowing its shape.
    EXPECT_TRUE(event.envelope.sequence == 7);
    EXPECT_TRUE(event.name == "haptic_vibration");

    // Forward compatibility: an event type this build does not know is still a
    // well-formed event, not a corrupt stream. It parses, reports its name and
    // envelope, and a reader can filter it out on `event` without guessing.
    const std::string futureEvent =
        "{\"version\":1,\"event\":\"future_event\",\"sequence\":9,"
        "\"suppressed\":0,\"detail\":\"something new\",\"future\":{\"x\":1}}";
    EXPECT_TRUE(ParseDriverLogBytes(
        futureEvent.data(), static_cast<int>(futureEvent.size()), event));
    EXPECT_TRUE(event.type == DriverLogEventType::Unknown);
    EXPECT_TRUE(event.name == "future_event");
    EXPECT_TRUE(event.envelope.sequence == 9);
    EXPECT_TRUE(event.envelope.detail == "something new");

    // A malformed envelope is still rejected, whatever the event name claims.
    const std::string futureWithoutEnvelope =
        "{\"version\":1,\"event\":\"future_event\",\"sequence\":9}";
    EXPECT_FALSE(ParseDriverLogBytes(
        futureWithoutEnvelope.data(), static_cast<int>(futureWithoutEnvelope.size()), event));

    const std::string futureWrongVersion =
        "{\"version\":2,\"event\":\"future_event\",\"sequence\":9,\"detail\":\"\"}";
    EXPECT_FALSE(ParseDriverLogBytes(
        futureWrongVersion.data(), static_cast<int>(futureWrongVersion.size()), event));

    // Every known event type round-trips its wire name.
    EXPECT_TRUE(std::string(DriverLogEventName(DriverLogEventType::CommandProcessed)) ==
        "command_processed");
    EXPECT_TRUE(std::string(DriverLogEventName(DriverLogEventType::HapticVibration)) ==
        "haptic_vibration");

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
