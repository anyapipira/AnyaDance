#pragma once

#include "core/math3d.h"

#include <array>
#include <string>
#include <vector>

namespace anyadance {

// Canonical anatomical joints exported by the Blender MMD solve and consumed by
// the device remapping. Order is fixed; do not reorder without updating parsing.
enum class SolvedJointId : std::size_t {
    Pelvis = 0,
    Head,
    LeftShoulder,
    RightShoulder,
    LeftElbow,
    RightElbow,
    LeftWrist,
    RightWrist,
    LeftAnkle,
    RightAnkle,
    LeftToe,
    RightToe,
    Count,
};

inline constexpr std::size_t kSolvedJointCount = static_cast<std::size_t>(SolvedJointId::Count);

inline constexpr std::size_t JointSlot(SolvedJointId id) {
    return static_cast<std::size_t>(id);
}

struct SolvedJoint {
    Vec3 position{};
    Quat rotation{};
};

// Optional rest-pose hand frame measured from the model's finger bones. The
// finger axis points from the wrist toward the fingers; palm is the direction
// the open palm faces. Both use the solved motion's world-space convention.
struct SolvedHandAxes {
    Vec3 finger{};
    Vec3 palm{};
    bool valid = false;
};

// One solved frame: world-space joint poses (OpenVR standing convention) plus
// optional per-hand finger curls ordered thumb, index, middle, ring, pinky.
struct SolvedFrame {
    float t = 0.0f;
    std::array<SolvedJoint, kSolvedJointCount> joints{};
    bool hasFingers = false;
    std::array<float, 5> leftFingers{};
    std::array<float, 5> rightFingers{};
};

struct SolvedMotion {
    float fps = 60.0f;
    std::string model;
    bool hasFingers = false;
    bool hasRest = false;
    std::array<SolvedJoint, kSolvedJointCount> rest{};
    std::array<SolvedHandAxes, 2> restHandAxes{}; // left, right
    std::vector<SolvedFrame> frames;

    float DurationSec() const { return frames.empty() ? 0.0f : frames.back().t; }
};

// Parse a solved-motion JSON document produced by scripts/blender_export_mmd.py.
// Returns false and sets error on malformed input or missing required fields.
bool ParseSolvedMotion(const std::string& jsonText, SolvedMotion& out, std::string& error);

} // namespace anyadance
