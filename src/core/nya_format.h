#pragma once

#include "core/frame_state.h"
#include "core/mmd_retarget.h"

#include <string>

namespace anyadance {

inline constexpr int kNyaFormatVersion = 1;
inline constexpr const char* kNyaExtension = ".nya";

// A saved AnyaDance clip. The payload is a device-level DanceMotion: the same
// six-device frame representation playback already consumes, so one format serves
// both poses and animations. A pose is simply a one-frame clip with loop = true
// (a loop of a single frame holds that pose), which is why MakePoseClip produces
// exactly one frame.
struct NyaClip {
    int version = kNyaFormatVersion;
    bool loop = true;     // poses always loop one frame; animations may or may not
    float fps = 60.0f;    // capture/playback rate hint
    std::string model;    // optional source label (e.g. the MMD model name)
    DanceMotion motion;   // per-frame times + device-level FrameStates

    bool IsPose() const { return motion.frames.size() == 1; }
};

// Serialize a clip to a .nya JSON document.
std::string SerializeNya(const NyaClip& clip);

// Parse a .nya JSON document. Returns false and sets error on malformed input or
// a wrong format tag. Device Y is clamped to the supported range and finger bends to
// [0, 1] on load, so a hand-edited file can never exceed the safe ranges.
bool ParseNya(const std::string& text, NyaClip& out, std::string& error);

// Build a one-frame looping clip from a single device-level frame (a saved pose).
NyaClip MakePoseClip(const FrameState& frame);

// Build an animation clip from a remapped dance motion.
NyaClip MakeAnimationClip(const DanceMotion& motion, float fps, const std::string& model);

} // namespace anyadance
