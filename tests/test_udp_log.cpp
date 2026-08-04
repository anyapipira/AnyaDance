#include "test_framework.h"
#include "tests.h"

#include "core/udp_log.h"

#include <cstdint>
#include <vector>

namespace anyadance::tests {
namespace {

// UDP may deliver driver events late, twice, or not at all. Everything that does
// arrive must still read in the order the driver produced it.
void TestLogOrdering() {
    const auto sequences = [](const UdpLog& log) {
        std::vector<std::uint64_t> out;
        for (const UdpLogEntry& entry : log.Entries()) {
            out.push_back(entry.sequence);
        }
        return out;
    };

    // A reordered delivery is placed back where it belongs. Distinct reasons
    // keep coalescing out of the way.
    UdpLog log;
    log.AddDriverEvent("Haptic vibration", "left_controller", {}, "a", 10);
    log.AddDriverEvent("Haptic vibration", "left_controller", {}, "c", 12);
    log.AddDriverEvent("Haptic vibration", "left_controller", {}, "b", 11);
    EXPECT_TRUE((sequences(log) == std::vector<std::uint64_t>{10, 11, 12}));
    EXPECT_TRUE(log.Entries()[1].detail == "b");

    // An event arriving several places late still lands correctly.
    log.AddDriverEvent("Haptic vibration", "left_controller", {}, "f", 15);
    log.AddDriverEvent("Haptic vibration", "left_controller", {}, "e", 14);
    log.AddDriverEvent("Haptic vibration", "left_controller", {}, "d", 13);
    EXPECT_TRUE((sequences(log) == std::vector<std::uint64_t>{10, 11, 12, 13, 14, 15}));

    // A duplicated datagram is dropped rather than logged twice.
    const std::size_t beforeDuplicate = log.Entries().size();
    log.AddDriverEvent("Haptic vibration", "left_controller", {}, "b again", 11);
    EXPECT_TRUE(log.Entries().size() == beforeDuplicate);

    // Loss leaves a gap in the numbering; what did arrive is still ordered, so a
    // receiver can see 16 is missing without the log misrepresenting the rest.
    log.AddDriverEvent("Haptic vibration", "left_controller", {}, "h", 17);
    EXPECT_TRUE(log.Entries().back().sequence == 17);

    // Command and haptic events interleave in one order, since they share the
    // counter, and a late command is placed among the haptic rows.
    log.AddDriverCommand(
        "Pose frame processed", "Processed", "127.0.0.1:1", "p", "late", true, 16);
    EXPECT_TRUE((sequences(log) ==
        std::vector<std::uint64_t>{10, 11, 12, 13, 14, 15, 16, 17}));

    // A row this process logged itself has no sequence and stops the walk, so a
    // late driver event never jumps ahead of local history.
    UdpLog barrier;
    barrier.AddDriverEvent("Haptic vibration", "left_controller", {}, "first", 100);
    barrier.Add("Socket error", "Failed", {}, "local");
    barrier.AddDriverEvent("Haptic vibration", "left_controller", {}, "late", 99);
    const std::vector<std::uint64_t> barrierOrder = sequences(barrier);
    EXPECT_TRUE((barrierOrder == std::vector<std::uint64_t>{100, 0, 99}));

    // A late event beyond the reorder window is appended rather than searched
    // for indefinitely, so one stray datagram cannot rewrite old history.
    UdpLog windowed;
    for (std::uint64_t i = 0; i < 200; ++i) {
        windowed.AddDriverEvent("Haptic vibration", "left_controller", {}, "x", 1000 + i);
    }
    windowed.AddDriverEvent("Haptic vibration", "left_controller", {}, "ancient", 1);
    EXPECT_TRUE(windowed.Entries().back().sequence == 1000 + 199);
    EXPECT_TRUE(windowed.Entries()[windowed.Entries().size() - 65].sequence == 1);
}

} // namespace

void TestLog() {
    UdpLog log;
    log.Add("Reset to T-Pose", "Sent", "payload1");
    EXPECT_TRUE(log.Entries().size() == 1);
    EXPECT_TRUE(log.Entries().back().payload == "payload1");
    EXPECT_TRUE(!log.Entries().back().timeText.empty());

    // Rapid manipulations with the same reason coalesce into the last entry
    // instead of flooding the log, so two quick updates add only one row.
    log.AddManipulation("Sent", "payload2");
    log.AddManipulation("Sent", "payload3");
    EXPECT_TRUE(log.Entries().size() == 2);
    EXPECT_TRUE(log.Entries().back().payload == "payload3");

    log.Add("Socket error", "Failed", "payload4", "err");
    EXPECT_TRUE(log.Entries().back().detail == "err");

    // Rapid accepted reports from one command source coalesce for display,
    // retaining the latest payload and endpoint. Rejections remain distinct.
    const std::size_t beforeDriverReports = log.Entries().size();
    log.AddDriverCommand(
        "Pose frame processed", "Processed", "127.0.0.1:50000",
        "driver-payload-1", "report #1", true, 1);
    log.AddDriverCommand(
        "Pose frame processed", "Processed", "127.0.0.1:50000",
        "driver-payload-2", "report #2", true, 2);
    EXPECT_TRUE(log.Entries().size() == beforeDriverReports + 1);
    EXPECT_TRUE(log.Entries().back().payload == "driver-payload-2");
    EXPECT_TRUE(log.Entries().back().endpoint == "127.0.0.1:50000");
    log.AddDriverCommand(
        "Pose frame rejected", "Rejected", "127.0.0.1:50000",
        "bad-payload", "report #3", false, 3);
    EXPECT_TRUE(log.Entries().size() == beforeDriverReports + 2);

    TestLogOrdering();

    // The ring buffer caps at 1000 entries, dropping the oldest.
    for (int i = 0; i < 1100; ++i) {
        log.Add("Keyboard/button state", "Sent", "x");
    }
    EXPECT_TRUE(log.Entries().size() == 1000);

    // Clear empties the buffer.
    log.Clear();
    EXPECT_TRUE(log.Entries().empty());
}

} // namespace anyadance::tests
