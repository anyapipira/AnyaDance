#pragma once

namespace anyadance {

struct DanceTransportRange {
    double start = 0.0;
    double end = 0.0;
};

struct DanceAudioPosition {
    bool active = false;
    double seconds = 0.0;
};

DanceTransportRange BuildDanceTransportRange(
    double motionDuration,
    bool hasAudio,
    double audioDuration,
    double audioOffset);

double NormalizeDanceTransportTime(
    double time,
    const DanceTransportRange& range,
    bool loop);

double MotionTimeForDanceTransport(double transportTime, double motionDuration);

DanceAudioPosition AudioPositionForDanceTransport(
    double transportTime,
    double audioDuration,
    double audioOffset);

} // namespace anyadance
