#include "core/nya_format.h"

#include "core/constants.h"
#include "core/json.h"
#include "core/math3d.h"

#include <array>
#include <cmath>
#include <utility>

namespace anyadance {
namespace {

using anyadance::json::Value;

constexpr char kNyaFormatTag[] = "anyadance_nya";

Value Vec3Array(const Vec3& v) {
    Value a = Value::Array();
    a.array.push_back(Value::Number(v.x));
    a.array.push_back(Value::Number(v.y));
    a.array.push_back(Value::Number(v.z));
    return a;
}

Value QuatArray(const Quat& q) {
    Value a = Value::Array();
    a.array.push_back(Value::Number(q.x));
    a.array.push_back(Value::Number(q.y));
    a.array.push_back(Value::Number(q.z));
    a.array.push_back(Value::Number(q.w));
    return a;
}

Value FingerArray(const FingerBends& f) {
    Value a = Value::Array();
    a.array.push_back(Value::Number(f.thumb));
    a.array.push_back(Value::Number(f.index));
    a.array.push_back(Value::Number(f.middle));
    a.array.push_back(Value::Number(f.ring));
    a.array.push_back(Value::Number(f.pinky));
    return a;
}

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

bool ReadFingers(const Value* value, FingerBends& out) {
    if (value == nullptr || !value->IsArray() || value->array.size() != 5) {
        return false;
    }
    float v[5];
    for (std::size_t i = 0; i < 5; ++i) {
        if (!ReadNumber(&value->array[i], v[i])) {
            return false;
        }
        v[i] = ClampFloat(v[i], 0.0f, 1.0f);
    }
    out = {v[0], v[1], v[2], v[3], v[4]};
    return true;
}

} // namespace

std::string SerializeNya(const NyaClip& clip) {
    Value root = Value::Object();
    root.Set("format", Value::String(kNyaFormatTag));
    root.Set("version", Value::Number(kNyaFormatVersion));
    root.Set("loop", Value::Bool(clip.loop));
    root.Set("fps", Value::Number(clip.fps));
    root.Set("model", Value::String(clip.model));

    const DanceMotion& motion = clip.motion;
    Value frames = Value::Array();
    frames.array.reserve(motion.frames.size());
    for (std::size_t fi = 0; fi < motion.frames.size(); ++fi) {
        const FrameState& fs = motion.frames[fi];
        const float t = fi < motion.times.size() ? motion.times[fi] : 0.0f;

        Value frame = Value::Object();
        frame.Set("t", Value::Number(t));

        Value devices = Value::Object();
        for (std::size_t d = 0; d < fs.devices.size(); ++d) {
            Value dev = Value::Object();
            dev.Set("p", Vec3Array(fs.devices[d].position));
            dev.Set("q", QuatArray(fs.devices[d].rotation));
            devices.Set(kDevices[d].id, std::move(dev));
        }
        frame.Set("devices", std::move(devices));

        if (fs.controllers[0].has_finger_bends || fs.controllers[1].has_finger_bends) {
            Value fingers = Value::Object();
            fingers.Set("left", FingerArray(fs.controllers[0].finger_bends));
            fingers.Set("right", FingerArray(fs.controllers[1].finger_bends));
            frame.Set("fingers", std::move(fingers));
        }

        frames.array.push_back(std::move(frame));
    }
    root.Set("frames", std::move(frames));
    return json::Serialize(root);
}

bool ParseNya(const std::string& text, NyaClip& out, std::string& error) {
    auto parsed = json::Parse(text);
    if (!parsed) {
        error = "not valid JSON";
        return false;
    }
    const Value& root = *parsed;
    if (!root.IsObject()) {
        error = "root is not an object";
        return false;
    }
    if (const Value* tag = root.Find("format");
        tag == nullptr || tag->type != json::Type::String || tag->string != kNyaFormatTag) {
        error = "not an AnyaDance .nya file";
        return false;
    }

    NyaClip clip;
    if (const Value* v = root.Find("version"); v != nullptr && v->type == json::Type::Number) {
        clip.version = static_cast<int>(v->number);
    }
    if (const Value* v = root.Find("loop"); v != nullptr && v->type == json::Type::Bool) {
        clip.loop = v->boolean;
    }
    if (const Value* v = root.Find("fps"); v != nullptr && v->type == json::Type::Number && v->number > 0.0) {
        clip.fps = static_cast<float>(v->number);
    }
    if (const Value* v = root.Find("model"); v != nullptr && v->type == json::Type::String) {
        clip.model = v->string;
    }

    const Value* frames = root.Find("frames");
    if (frames == nullptr || !frames->IsArray() || frames->array.empty()) {
        error = "clip has no frames";
        return false;
    }

    DanceMotion motion;
    motion.frames.reserve(frames->array.size());
    motion.times.reserve(frames->array.size());
    bool anyFingers = false;
    for (const Value& frameValue : frames->array) {
        if (!frameValue.IsObject()) {
            error = "frame is not an object";
            return false;
        }
        float t = 0.0f;
        ReadNumber(frameValue.Find("t"), t);

        const Value* devices = frameValue.Find("devices");
        if (devices == nullptr || !devices->IsObject()) {
            error = "frame is missing devices";
            return false;
        }

        FrameState fs;
        for (std::size_t d = 0; d < fs.devices.size(); ++d) {
            const Value* dev = devices->Find(kDevices[d].id);
            Vec3 position;
            Quat rotation;
            if (dev == nullptr || !dev->IsObject() ||
                !ReadVec3(dev->Find("p"), position) || !ReadQuat(dev->Find("q"), rotation)) {
                error = std::string("frame has a bad pose for device ") + kDevices[d].id;
                return false;
            }
            position.y = ClampDeviceY(d, position.y);
            fs.devices[d].position = position;
            fs.devices[d].rotation = rotation;
            fs.devices[d].valid = true;
            fs.devices[d].connected = true;
        }

        if (const Value* fingers = frameValue.Find("fingers"); fingers != nullptr && fingers->IsObject()) {
            FingerBends left;
            FingerBends right;
            if (ReadFingers(fingers->Find("left"), left) && ReadFingers(fingers->Find("right"), right)) {
                fs.controllers[0].has_finger_bends = true;
                fs.controllers[0].finger_bends = left;
                fs.controllers[1].has_finger_bends = true;
                fs.controllers[1].finger_bends = right;
                anyFingers = true;
            }
        }

        motion.frames.push_back(fs);
        motion.times.push_back(t);
    }

    motion.duration = motion.times.back();
    motion.hasFingers = anyFingers;
    motion.scale = 1.0f;
    motion.valid = true;
    clip.motion = std::move(motion);
    out = std::move(clip);
    return true;
}

NyaClip MakePoseClip(const FrameState& frame) {
    NyaClip clip;
    clip.loop = true;
    clip.motion.frames.push_back(frame);
    clip.motion.times.push_back(0.0f);
    clip.motion.duration = 0.0f;
    clip.motion.scale = 1.0f;
    clip.motion.valid = true;
    clip.motion.hasFingers =
        frame.controllers[0].has_finger_bends || frame.controllers[1].has_finger_bends;
    return clip;
}

NyaClip MakeAnimationClip(const DanceMotion& motion, float fps, const std::string& model) {
    NyaClip clip;
    clip.loop = true;
    clip.fps = fps > 0.0f ? fps : 60.0f;
    clip.model = model;
    clip.motion = motion;
    return clip;
}

} // namespace anyadance
