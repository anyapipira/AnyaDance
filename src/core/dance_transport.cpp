#include "core/dance_transport.h"

#include <algorithm>
#include <cmath>

namespace anyadance {
namespace {

double NonNegativeFinite(double value) {
    return std::isfinite(value) ? std::max(0.0, value) : 0.0;
}

double FiniteOrZero(double value) {
    return std::isfinite(value) ? value : 0.0;
}

} // namespace

DanceTransportRange BuildDanceTransportRange(
    double motionDuration,
    bool hasAudio,
    double audioDuration,
    double audioOffset) {
    motionDuration = NonNegativeFinite(motionDuration);
    if (!hasAudio) {
        return {0.0, motionDuration};
    }

    audioDuration = NonNegativeFinite(audioDuration);
    audioOffset = FiniteOrZero(audioOffset);
    const double start = std::min(0.0, audioOffset);
    const double end = std::max(motionDuration, audioOffset + audioDuration);
    return {start, std::max(start, end)};
}

double NormalizeDanceTransportTime(
    double time,
    const DanceTransportRange& range,
    bool loop) {
    time = FiniteOrZero(time);
    const double start = std::min(range.start, range.end);
    const double end = std::max(range.start, range.end);
    const double duration = end - start;
    if (!loop || duration <= 0.0) {
        return std::clamp(time, start, end);
    }

    double wrapped = std::fmod(time - start, duration);
    if (wrapped < 0.0) {
        wrapped += duration;
    }
    return start + wrapped;
}

double MotionTimeForDanceTransport(double transportTime, double motionDuration) {
    return std::clamp(FiniteOrZero(transportTime), 0.0, NonNegativeFinite(motionDuration));
}

DanceAudioPosition AudioPositionForDanceTransport(
    double transportTime,
    double audioDuration,
    double audioOffset) {
    transportTime = FiniteOrZero(transportTime);
    audioDuration = NonNegativeFinite(audioDuration);
    audioOffset = FiniteOrZero(audioOffset);
    const double position = transportTime - audioOffset;
    return {
        audioDuration > 0.0 && position >= 0.0 && position < audioDuration,
        std::clamp(position, 0.0, audioDuration),
    };
}

} // namespace anyadance
