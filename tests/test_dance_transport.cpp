#include "test_framework.h"
#include "tests.h"

#include "core/dance_transport.h"

namespace anyadance::tests {

void TestDanceTransport() {
    {
        const DanceTransportRange range = BuildDanceTransportRange(10.0, false, 0.0, 0.0);
        EXPECT_NEAR(range.start, 0.0, 0.0001);
        EXPECT_NEAR(range.end, 10.0, 0.0001);
        EXPECT_NEAR(NormalizeDanceTransportTime(11.0, range, false), 10.0, 0.0001);
        EXPECT_NEAR(NormalizeDanceTransportTime(11.0, range, true), 1.0, 0.0001);
    }

    // A negative offset starts the BGM before frame zero. Motion stays frozen on
    // its first frame until transport time reaches zero.
    {
        const DanceTransportRange range = BuildDanceTransportRange(10.0, true, 14.0, -2.0);
        EXPECT_NEAR(range.start, -2.0, 0.0001);
        EXPECT_NEAR(range.end, 12.0, 0.0001);
        EXPECT_NEAR(MotionTimeForDanceTransport(-1.25, 10.0), 0.0, 0.0001);
        const DanceAudioPosition audio = AudioPositionForDanceTransport(-1.25, 14.0, -2.0);
        EXPECT_TRUE(audio.active);
        EXPECT_NEAR(audio.seconds, 0.75, 0.0001);
    }

    // The BGM outro extends the combined transport past the motion. The final
    // motion frame remains selected while audio continues.
    {
        const DanceTransportRange range = BuildDanceTransportRange(8.0, true, 12.0, 1.0);
        EXPECT_NEAR(range.start, 0.0, 0.0001);
        EXPECT_NEAR(range.end, 13.0, 0.0001);
        EXPECT_NEAR(MotionTimeForDanceTransport(11.0, 8.0), 8.0, 0.0001);
        const DanceAudioPosition audio = AudioPositionForDanceTransport(11.0, 12.0, 1.0);
        EXPECT_TRUE(audio.active);
        EXPECT_NEAR(audio.seconds, 10.0, 0.0001);
    }

    // Looping repeats the entire combined interval, including the intro freeze,
    // rather than looping the motion independently under a still-playing track.
    {
        const DanceTransportRange range = BuildDanceTransportRange(10.0, true, 14.0, -2.0);
        EXPECT_NEAR(NormalizeDanceTransportTime(12.25, range, true), -1.75, 0.0001);
        EXPECT_NEAR(MotionTimeForDanceTransport(-1.75, 10.0), 0.0, 0.0001);
        const DanceAudioPosition audio = AudioPositionForDanceTransport(-1.75, 14.0, -2.0);
        EXPECT_TRUE(audio.active);
        EXPECT_NEAR(audio.seconds, 0.25, 0.0001);
    }

    // A positive offset allows motion to begin before the BGM; audio remains
    // silent until its configured start time.
    {
        const DanceAudioPosition before = AudioPositionForDanceTransport(1.0, 5.0, 2.0);
        EXPECT_FALSE(before.active);
        EXPECT_NEAR(before.seconds, 0.0, 0.0001);
        const DanceAudioPosition after = AudioPositionForDanceTransport(2.5, 5.0, 2.0);
        EXPECT_TRUE(after.active);
        EXPECT_NEAR(after.seconds, 0.5, 0.0001);
    }
}

} // namespace anyadance::tests
