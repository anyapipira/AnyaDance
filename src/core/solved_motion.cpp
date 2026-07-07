#include "core/solved_motion.h"

#include "core/json.h"

#include <array>
#include <cmath>

namespace anyadance {
namespace {

using anyadance::json::Value;

// JSON joint keys, indexed by SolvedJointId. Must match the names written by
// scripts/blender_export_mmd.py (JOINT_ALIASES order).
constexpr std::array<const char*, kSolvedJointCount> kJointKeys = {
    "pelvis",
    "head",
    "left_shoulder",
    "right_shoulder",
    "left_elbow",
    "right_elbow",
    "left_wrist",
    "right_wrist",
    "left_ankle",
    "right_ankle",
    "left_toe",
    "right_toe",
};

bool ReadNumber(const Value* value, float& out) {
    if (value == nullptr || value->type != json::Type::Number || !std::isfinite(value->number)) {
        return false;
    }
    out = static_cast<float>(value->number);
    return true;
}

bool ReadVec3(const Value* value, Vec3& out) {
    if (value == nullptr || !value->IsArray() || value->array.size() != 3) {
        return false;
    }
    return ReadNumber(&value->array[0], out.x) &&
           ReadNumber(&value->array[1], out.y) &&
           ReadNumber(&value->array[2], out.z);
}

bool ReadQuat(const Value* value, Quat& out) {
    if (value == nullptr || !value->IsArray() || value->array.size() != 4) {
        return false;
    }
    return ReadNumber(&value->array[0], out.x) &&
           ReadNumber(&value->array[1], out.y) &&
           ReadNumber(&value->array[2], out.z) &&
           ReadNumber(&value->array[3], out.w);
}

bool ReadJoint(const Value* value, SolvedJoint& out) {
    if (value == nullptr || !value->IsObject()) {
        return false;
    }
    return ReadVec3(value->Find("p"), out.position) && ReadQuat(value->Find("q"), out.rotation);
}

bool ReadJointSet(const Value* set, std::array<SolvedJoint, kSolvedJointCount>& out) {
    if (set == nullptr || !set->IsObject()) {
        return false;
    }
    for (std::size_t i = 0; i < kSolvedJointCount; ++i) {
        if (!ReadJoint(set->Find(kJointKeys[i]), out[i])) {
            return false;
        }
    }
    return true;
}

bool ReadFingers(const Value* value, std::array<float, 5>& out) {
    if (value == nullptr || !value->IsArray() || value->array.size() != 5) {
        return false;
    }
    for (std::size_t i = 0; i < 5; ++i) {
        if (!ReadNumber(&value->array[i], out[i])) {
            return false;
        }
        out[i] = ClampFloat(out[i], 0.0f, 1.0f);
    }
    return true;
}

bool ReadHandAxes(const Value* value, SolvedHandAxes& out) {
    if (value == nullptr || !value->IsObject() ||
        !ReadVec3(value->Find("finger"), out.finger) ||
        !ReadVec3(value->Find("palm"), out.palm)) {
        return false;
    }
    const auto lengthSquared = [](Vec3 value) {
        return value.x * value.x + value.y * value.y + value.z * value.z;
    };
    if (lengthSquared(out.finger) < 1e-8f || lengthSquared(out.palm) < 1e-8f) {
        return false;
    }
    out.valid = true;
    return true;
}

} // namespace

bool ParseSolvedMotion(const std::string& jsonText, SolvedMotion& out, std::string& error) {
    auto parsed = json::Parse(jsonText);
    if (!parsed) {
        error = "solved motion is not valid JSON";
        return false;
    }
    const Value& root = *parsed;
    if (!root.IsObject()) {
        error = "solved motion root is not an object";
        return false;
    }

    SolvedMotion motion;
    if (const Value* fps = root.Find("fps"); fps != nullptr && fps->type == json::Type::Number && fps->number > 0.0) {
        motion.fps = static_cast<float>(fps->number);
    }
    if (const Value* model = root.Find("model"); model != nullptr && model->type == json::Type::String) {
        motion.model = model->string;
    }

    if (const Value* rest = root.Find("rest"); rest != nullptr && rest->IsObject()) {
        if (!ReadJointSet(rest, motion.rest)) {
            error = "solved motion rest pose is incomplete";
            return false;
        }
        motion.hasRest = true;
    }

    // New solves include a model-derived rest hand frame. Keep this optional so
    // solved JSON produced by older AnyaDance builds remains loadable. A malformed
    // per-hand hint is ignored and the retargeter falls back to arm anatomy.
    if (const Value* axes = root.Find("rest_hand_axes"); axes != nullptr && axes->IsObject()) {
        ReadHandAxes(axes->Find("left_controller"), motion.restHandAxes[0]);
        ReadHandAxes(axes->Find("right_controller"), motion.restHandAxes[1]);
    }

    const Value* frames = root.Find("frames");
    if (frames == nullptr || !frames->IsArray() || frames->array.empty()) {
        error = "solved motion has no frames";
        return false;
    }

    motion.frames.reserve(frames->array.size());
    bool anyFingers = false;
    for (const Value& frameValue : frames->array) {
        if (!frameValue.IsObject()) {
            error = "solved motion frame is not an object";
            return false;
        }
        SolvedFrame frame;
        if (!ReadNumber(frameValue.Find("t"), frame.t)) {
            error = "solved motion frame is missing a timestamp";
            return false;
        }
        if (!ReadJointSet(frameValue.Find("j"), frame.joints)) {
            error = "solved motion frame is missing joints";
            return false;
        }
        if (ReadFingers(frameValue.Find("fl"), frame.leftFingers) &&
            ReadFingers(frameValue.Find("fr"), frame.rightFingers)) {
            frame.hasFingers = true;
            anyFingers = true;
        }
        motion.frames.push_back(frame);
    }
    motion.hasFingers = anyFingers;
    out = std::move(motion);
    return true;
}

} // namespace anyadance
