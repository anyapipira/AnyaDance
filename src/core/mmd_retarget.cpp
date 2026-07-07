#include "core/mmd_retarget.h"

#include <algorithm>
#include <cmath>

namespace anyadance {
namespace {

// Palm sits ~this fraction of the forearm beyond the wrist; this gives VRChat IK
// an extended-arm target.
constexpr float kPalmForearmRatio = 0.21f;
constexpr float kHeadToCrownM = 0.12f;
constexpr float kMinSourceHeightM = 0.3f;
constexpr float kMaxSourceHeightM = 5.0f;

Vec3 Sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 Lerp(Vec3 a, Vec3 b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

// Slerp with sign alignment, falling back to nlerp for nearly-parallel inputs.
Quat Slerp(Quat a, Quat b, float t) {
    a = Normalized(a);
    b = Normalized(b);
    float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (dot < 0.0f) {
        b = {-b.x, -b.y, -b.z, -b.w};
        dot = -dot;
    }
    if (dot > 0.9995f) {
        return Normalized(Quat{
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t,
            a.w + (b.w - a.w) * t,
        });
    }
    const float theta0 = std::acos(ClampFloat(dot, -1.0f, 1.0f));
    const float theta = theta0 * t;
    const float sinTheta = std::sin(theta);
    const float sinTheta0 = std::sin(theta0);
    const float sa = std::cos(theta) - dot * sinTheta / sinTheta0;
    const float sb = sinTheta / sinTheta0;
    return Normalized(Quat{
        a.x * sa + b.x * sb,
        a.y * sa + b.y * sb,
        a.z * sa + b.z * sb,
        a.w * sa + b.w * sb,
    });
}

float Percentile(std::vector<float> values, float q) {
    if (values.empty()) {
        return 0.0f;
    }
    std::sort(values.begin(), values.end());
    if (values.size() == 1) {
        return values.front();
    }
    const float pos = q * static_cast<float>(values.size() - 1);
    const std::size_t lo = static_cast<std::size_t>(std::floor(pos));
    const std::size_t hi = static_cast<std::size_t>(std::ceil(pos));
    if (lo == hi) {
        return values[lo];
    }
    const float frac = pos - static_cast<float>(lo);
    return values[lo] * (1.0f - frac) + values[hi] * frac;
}

// Dance-local transform: drop to the floor and recenter on the root X/Z. Avatar
// world turning is represented by controller input.
struct DanceLocal {
    float floorY = 0.0f;
    float rootX = 0.0f;
    float rootZ = 0.0f;

    SolvedJoint Apply(const SolvedJoint& joint) const {
        const Vec3 floored{joint.position.x - rootX, joint.position.y - floorY, joint.position.z - rootZ};
        return {floored, Normalized(joint.rotation)};
    }
};

Vec3 LocalPos(const DanceLocal& dl, const SolvedJoint& joint) { return dl.Apply(joint).position; }

// Stretch a palm point about the shoulder so VRChat IK fully extends the arm.
Vec3 HandAnchor(Vec3 palm, Vec3 shoulder, float reach) {
    return Add(shoulder, Scale(Sub(palm, shoulder), reach));
}

// --- Controller orientation ---
// The solved wrist matrix contains the hand's full motion, but its local axes are
// model-specific. Calibrate the OpenVR controller basis in wrist-local rest space,
// then rigidly follow the live wrist so flexion, deviation, and twist survive.

const Vec3 kBodyForward{0.0f, 0.0f, -1.0f};
const Vec3 kBodyUp{0.0f, 1.0f, 0.0f};
const Vec3 kBodyRight{1.0f, 0.0f, 0.0f};
const Vec3 kControllerLocalForward{0.0f, 0.0f, -1.0f};
// Neutral index-finger direction in the native OpenVR hand skeleton (controller
// local space); the Valve hand sample points it between local -Y and -Z. Same
// hand skeleton as the driver, so the same axis applies here.
const Vec3 kNeutralIndexLeft{0.11569004f, -0.51338429f, -0.85032487f};
const Vec3 kNeutralIndexRight{-0.11569004f, -0.51338429f, -0.85032487f};
// Open-palm normal in the same native controller skeleton, derived from the
// direction the neutral index finger curls toward.
const Vec3 kNeutralPalmLeft{0.9783916f, -0.0887937f, 0.1867233f};
const Vec3 kNeutralPalmRight{-0.9783916f, -0.0887937f, 0.1867233f};

float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 Cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
float Length(Vec3 v) { return std::sqrt(Dot(v, v)); }
Vec3 NormalizeVec(Vec3 v, Vec3 fallback) {
    const float len = Length(v);
    return len > 0.0f ? Vec3{v.x / len, v.y / len, v.z / len} : fallback;
}
float Component(Vec3 v, int i) { return i == 0 ? v.x : (i == 1 ? v.y : v.z); }

// Component of reference perpendicular to normal, normalized. Returns false when
// the perpendicular part is degenerate (reference ~parallel to normal).
bool ProjectPerp(Vec3 reference, Vec3 normal, float minLenSq, Vec3& out) {
    const Vec3 projected = Sub(reference, Scale(normal, Dot(reference, normal)));
    if (Dot(projected, projected) < minLenSq) {
        return false;
    }
    out = NormalizeVec(projected, {0.0f, 0.0f, -1.0f});
    return true;
}

Vec3 BestPerpendicular(const Vec3* candidates, int count, Vec3 normal) {
    Vec3 best{};
    float bestLenSq = -1.0f;
    for (int i = 0; i < count; ++i) {
        const Vec3 projected = Sub(candidates[i], Scale(normal, Dot(candidates[i], normal)));
        const float lenSq = Dot(projected, projected);
        if (lenSq > bestLenSq) {
            best = projected;
            bestLenSq = lenSq;
        }
    }
    if (bestLenSq < 1e-6f) {
        return {0.0f, 0.0f, -1.0f};
    }
    return NormalizeVec(best, {0.0f, 0.0f, -1.0f});
}

void BasisFromPrimarySecondary(Vec3 primary, Vec3 secondary, Vec3& c0, Vec3& c1, Vec3& c2) {
    c1 = NormalizeVec(primary, {0.0f, -1.0f, 0.0f});
    if (!ProjectPerp(secondary, c1, 1e-8f, c2)) {
        c2 = {0.0f, 0.0f, -1.0f};
    }
    c0 = NormalizeVec(Cross(c1, c2), {1.0f, 0.0f, 0.0f});
    c2 = NormalizeVec(Cross(c0, c1), c2);
}

// Convert a (proper, orthonormal) 3x3 rotation to a quaternion (xyzw).
Quat RotationMatrixToQuat(const float m[3][3]) {
    const float trace = m[0][0] + m[1][1] + m[2][2];
    Quat q{};
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        q = {(m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s, 0.25f * s};
    } else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
        const float s = std::sqrt(1.0f + m[0][0] - m[1][1] - m[2][2]) * 2.0f;
        q = {0.25f * s, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s, (m[2][1] - m[1][2]) / s};
    } else if (m[1][1] > m[2][2]) {
        const float s = std::sqrt(1.0f + m[1][1] - m[0][0] - m[2][2]) * 2.0f;
        q = {(m[0][1] + m[1][0]) / s, 0.25f * s, (m[1][2] + m[2][1]) / s, (m[0][2] - m[2][0]) / s};
    } else {
        const float s = std::sqrt(1.0f + m[2][2] - m[0][0] - m[1][1]) * 2.0f;
        q = {(m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, 0.25f * s, (m[1][0] - m[0][1]) / s};
    }
    return Normalized(q);
}

// Rotation mapping the local (primary, secondary) basis onto the world one.
Quat BasisMappingToQuat(Vec3 localPrimary, Vec3 localSecondary, Vec3 worldPrimary, Vec3 worldSecondary) {
    Vec3 l0, l1, l2, w0, w1, w2;
    BasisFromPrimarySecondary(localPrimary, localSecondary, l0, l1, l2);
    BasisFromPrimarySecondary(worldPrimary, worldSecondary, w0, w1, w2);
    float m[3][3];
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            m[row][col] = Component(w0, row) * Component(l0, col) +
                          Component(w1, row) * Component(l1, col) +
                          Component(w2, row) * Component(l2, col);
        }
    }
    return RotationMatrixToQuat(m);
}

// Rest-pose roll reference for a controller: the upper-arm direction projected
// perpendicular to the forearm, falling back to a body axis when degenerate.
Vec3 ControllerReferenceTwistAxis(Vec3 shoulder, Vec3 elbow, Vec3 wrist) {
    const Vec3 fingerAxis = NormalizeVec(Sub(wrist, elbow), {0.0f, -1.0f, 0.0f});
    const Vec3 upperToShoulder = NormalizeVec(Sub(shoulder, elbow), {0.0f, 1.0f, 0.0f});
    Vec3 armPlane;
    if (ProjectPerp(upperToShoulder, fingerAxis, 1e-4f, armPlane)) {
        return armPlane;
    }
    const Vec3 candidates[] = {kBodyForward, kBodyUp, kBodyRight, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}};
    return BestPerpendicular(candidates, 5, fingerAxis);
}

// Build the rest OpenVR controller orientation from arm anatomy. This fallback
// recovers the finger direction when an older solve has no exported hand frame;
// the palm roll remains heuristic in that case.
Quat ControllerRestRotationFromForearm(Vec3 shoulder, Vec3 elbow, Vec3 wrist, bool isLeft) {
    const Vec3 fingerAxis = NormalizeVec(Sub(wrist, elbow), {0.0f, -1.0f, 0.0f});
    const Vec3 handForward = ControllerReferenceTwistAxis(shoulder, elbow, wrist);
    const Vec3 neutralIndex = isLeft ? kNeutralIndexLeft : kNeutralIndexRight;
    return BasisMappingToQuat(neutralIndex, kControllerLocalForward, fingerAxis, handForward);
}

float SourceHeight(const std::array<SolvedJoint, kSolvedJointCount>& restLocal) {
    const float headY = restLocal[JointSlot(SolvedJointId::Head)].position.y;
    const float footY = std::min({
        restLocal[JointSlot(SolvedJointId::LeftToe)].position.y,
        restLocal[JointSlot(SolvedJointId::RightToe)].position.y,
        restLocal[JointSlot(SolvedJointId::LeftAnkle)].position.y,
        restLocal[JointSlot(SolvedJointId::RightAnkle)].position.y,
    });
    const float height = headY - footY + kHeadToCrownM;
    return ClampFloat(height, kMinSourceHeightM, kMaxSourceHeightM);
}

} // namespace

DanceMotion BuildDanceMotion(const SolvedMotion& motion, const MmdRetargetParams& params) {
    DanceMotion dance;
    if (motion.frames.empty()) {
        return dance;
    }

    const std::array<SolvedJoint, kSolvedJointCount>& rest =
        motion.hasRest ? motion.rest : motion.frames.front().joints;

    DanceLocal dl;
    const SolvedJoint& restPelvis = rest[JointSlot(SolvedJointId::Pelvis)];
    dl.rootX = restPelvis.position.x;
    dl.rootZ = restPelvis.position.z;

    // Floor: 5th percentile of foot-contact heights across the dance plus the rest
    // pose, robust to a single penetrating frame.
    std::vector<float> contacts;
    contacts.reserve(motion.frames.size() * 2 + 2);
    const auto footYAt = [](const SolvedJoint& a, const SolvedJoint& b, const SolvedJoint& c, const SolvedJoint& d) {
        return std::min({a.position.y, b.position.y, c.position.y, d.position.y});
    };
    for (const SolvedFrame& frame : motion.frames) {
        contacts.push_back(footYAt(
            frame.joints[JointSlot(SolvedJointId::LeftAnkle)],
            frame.joints[JointSlot(SolvedJointId::RightAnkle)],
            frame.joints[JointSlot(SolvedJointId::LeftToe)],
            frame.joints[JointSlot(SolvedJointId::RightToe)]));
    }
    contacts.push_back(rest[JointSlot(SolvedJointId::LeftToe)].position.y);
    contacts.push_back(rest[JointSlot(SolvedJointId::RightToe)].position.y);
    dl.floorY = Percentile(contacts, 0.05f);

    std::array<SolvedJoint, kSolvedJointCount> restLocal{};
    for (std::size_t i = 0; i < kSolvedJointCount; ++i) {
        restLocal[i] = dl.Apply(rest[i]);
    }
    dance.sourceHeightM = SourceHeight(restLocal);
    dance.scale = params.targetHeightM / std::max(1e-3f, dance.sourceHeightM);

    // Rotations are applied as a delta from the source rest onto a clean (upright,
    // forward) device rest, so the model's own bone frame never leaks through.
    // Each device borrows the inverse rest rotation of the joint that drives it.
    struct DeviceMap {
        DeviceIndex device;
        SolvedJointId joint;
    };
    static constexpr DeviceMap kPositionalJoints[] = {
        {DeviceIndex::Hmd, SolvedJointId::Head},
        {DeviceIndex::Hip, SolvedJointId::Pelvis},
        {DeviceIndex::LeftFoot, SolvedJointId::LeftAnkle},
        {DeviceIndex::RightFoot, SolvedJointId::RightAnkle},
        {DeviceIndex::LeftController, SolvedJointId::LeftWrist},
        {DeviceIndex::RightController, SolvedJointId::RightWrist},
    };
    std::array<Quat, 6> invSourceRestRot{};
    for (const DeviceMap& map : kPositionalJoints) {
        invSourceRestRot[DeviceSlot(map.device)] =
            Conjugate(restLocal[JointSlot(map.joint)].rotation);
    }

    // Express each desired OpenVR controller rest basis and straight-hand palm
    // offset in the source wrist bone's local frame. The live wrist can then move
    // both rigidly, preserving wrist flexion instead of pinning them to the
    // elbow->wrist line on every frame.
    std::array<Quat, 2> controllerBasisInWrist{};
    std::array<Vec3, 2> palmOffsetInWrist{};
    const auto calibrateHand = [&](std::size_t hand, SolvedJointId shoulder,
                                   SolvedJointId elbow, SolvedJointId wrist) {
        const SolvedJoint& wristRest = restLocal[JointSlot(wrist)];
        const Quat invWristRest = Conjugate(wristRest.rotation);
        const bool isLeft = hand == 0;
        const SolvedHandAxes& exportedAxes = motion.restHandAxes[hand];
        Quat restBasis;
        if (exportedAxes.valid) {
            const Vec3 neutralIndex = isLeft ? kNeutralIndexLeft : kNeutralIndexRight;
            const Vec3 neutralPalm = isLeft ? kNeutralPalmLeft : kNeutralPalmRight;
            restBasis = BasisMappingToQuat(
                neutralIndex, neutralPalm, exportedAxes.finger, exportedAxes.palm);
        } else {
            restBasis = ControllerRestRotationFromForearm(
                restLocal[JointSlot(shoulder)].position,
                restLocal[JointSlot(elbow)].position,
                wristRest.position,
                isLeft);
        }
        controllerBasisInWrist[hand] = Normalized(Multiply(invWristRest, restBasis));
        palmOffsetInWrist[hand] = Rotate(
            invWristRest,
            Scale(Sub(wristRest.position, restLocal[JointSlot(elbow)].position),
                  kPalmForearmRatio));
    };
    calibrateHand(0,
        SolvedJointId::LeftShoulder, SolvedJointId::LeftElbow, SolvedJointId::LeftWrist);
    calibrateHand(1,
        SolvedJointId::RightShoulder, SolvedJointId::RightElbow, SolvedJointId::RightWrist);

    dance.times.reserve(motion.frames.size());
    dance.frames.reserve(motion.frames.size());
    const float scale = dance.scale;
    const Vec3 headUp{0.0f, params.headMountUpM, 0.0f};
    const Vec3 floorLift{0.0f, params.floorOffsetM, 0.0f};

    for (const SolvedFrame& src : motion.frames) {
        FrameState frame;
        for (DeviceState& device : frame.devices) {
            device.connected = true;
            device.valid = true;
        }

        // Body joints in dance-local space.
        const SolvedJoint head = dl.Apply(src.joints[JointSlot(SolvedJointId::Head)]);
        const SolvedJoint pelvis = dl.Apply(src.joints[JointSlot(SolvedJointId::Pelvis)]);
        const SolvedJoint lAnkle = dl.Apply(src.joints[JointSlot(SolvedJointId::LeftAnkle)]);
        const SolvedJoint rAnkle = dl.Apply(src.joints[JointSlot(SolvedJointId::RightAnkle)]);
        const Vec3 lShoulder = LocalPos(dl, src.joints[JointSlot(SolvedJointId::LeftShoulder)]);
        const Vec3 rShoulder = LocalPos(dl, src.joints[JointSlot(SolvedJointId::RightShoulder)]);
        const SolvedJoint lWristJoint = dl.Apply(src.joints[JointSlot(SolvedJointId::LeftWrist)]);
        const SolvedJoint rWristJoint = dl.Apply(src.joints[JointSlot(SolvedJointId::RightWrist)]);

        const Vec3 lPalm = Add(
            lWristJoint.position,
            Rotate(lWristJoint.rotation, palmOffsetInWrist[0]));
        const Vec3 rPalm = Add(
            rWristJoint.position,
            Rotate(rWristJoint.rotation, palmOffsetInWrist[1]));
        const Vec3 lHand = HandAnchor(lPalm, lShoulder, params.handReachScale);
        const Vec3 rHand = HandAnchor(rPalm, rShoulder, params.handReachScale);

        // Positions: scaled dance-local joint (palm anchor for hands), plus the
        // small head mount lift and the global floor offset.
        frame.devices[DeviceSlot(DeviceIndex::Hmd)].position =
            Add(Add(Scale(head.position, scale), headUp), floorLift);
        frame.devices[DeviceSlot(DeviceIndex::Hip)].position =
            Add(Scale(pelvis.position, scale), floorLift);
        frame.devices[DeviceSlot(DeviceIndex::LeftFoot)].position =
            Add(Scale(lAnkle.position, scale), floorLift);
        frame.devices[DeviceSlot(DeviceIndex::RightFoot)].position =
            Add(Scale(rAnkle.position, scale), floorLift);
        frame.devices[DeviceSlot(DeviceIndex::LeftController)].position =
            Add(Scale(lHand, scale), floorLift);
        frame.devices[DeviceSlot(DeviceIndex::RightController)].position =
            Add(Scale(rHand, scale), floorLift);

        // Rotations: delta from the source rest applied onto a clean (identity)
        // device rest, so at the rest pose every device is upright/forward.
        const auto deltaRot = [&](DeviceIndex device, const SolvedJoint& current) {
            const Quat delta = Multiply(current.rotation, invSourceRestRot[DeviceSlot(device)]);
            return Normalized(delta);
        };
        frame.devices[DeviceSlot(DeviceIndex::Hmd)].rotation = deltaRot(DeviceIndex::Hmd, head);
        frame.devices[DeviceSlot(DeviceIndex::Hip)].rotation = deltaRot(DeviceIndex::Hip, pelvis);
        frame.devices[DeviceSlot(DeviceIndex::LeftFoot)].rotation = deltaRot(DeviceIndex::LeftFoot, lAnkle);
        frame.devices[DeviceSlot(DeviceIndex::RightFoot)].rotation = deltaRot(DeviceIndex::RightFoot, rAnkle);
        // Controllers rigidly follow the solved wrist through the wrist-local
        // basis calibrated above, preserving flexion, deviation, and roll.
        frame.devices[DeviceSlot(DeviceIndex::LeftController)].rotation =
            Normalized(Multiply(lWristJoint.rotation, controllerBasisInWrist[0]));
        frame.devices[DeviceSlot(DeviceIndex::RightController)].rotation =
            Normalized(Multiply(rWristJoint.rotation, controllerBasisInWrist[1]));

        if (src.hasFingers) {
            ControllerState& left = frame.controllers[0];
            ControllerState& right = frame.controllers[1];
            left.has_finger_bends = true;
            right.has_finger_bends = true;
            left.finger_bends = {src.leftFingers[0], src.leftFingers[1], src.leftFingers[2], src.leftFingers[3], src.leftFingers[4]};
            right.finger_bends = {src.rightFingers[0], src.rightFingers[1], src.rightFingers[2], src.rightFingers[3], src.rightFingers[4]};
        }

        dance.times.push_back(src.t);
        dance.frames.push_back(frame);
    }

    dance.duration = dance.times.back();
    dance.hasFingers = motion.hasFingers;
    dance.valid = true;
    return dance;
}

FrameState SampleDanceMotion(const DanceMotion& dance, float t, bool loop) {
    if (dance.frames.empty()) {
        return MakeNeutralFrame();
    }
    if (dance.frames.size() == 1) {
        return dance.frames.front();
    }
    const float duration = dance.duration;
    if (duration <= 0.0f) {
        return dance.frames.front();
    }
    if (loop) {
        t = std::fmod(t, duration);
        if (t < 0.0f) {
            t += duration;
        }
    } else {
        t = ClampFloat(t, 0.0f, duration);
    }

    // Binary search for the bracketing frames.
    const std::vector<float>& times = dance.times;
    std::size_t lo = 0;
    std::size_t hi = times.size() - 1;
    if (t <= times.front()) {
        return dance.frames.front();
    }
    if (t >= times.back()) {
        return dance.frames.back();
    }
    while (lo + 1 < hi) {
        const std::size_t mid = (lo + hi) / 2;
        if (times[mid] <= t) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    const float span = times[hi] - times[lo];
    const float alpha = span <= 0.0f ? 0.0f : (t - times[lo]) / span;

    const FrameState& a = dance.frames[lo];
    const FrameState& b = dance.frames[hi];
    FrameState out = a;
    for (std::size_t i = 0; i < out.devices.size(); ++i) {
        out.devices[i].position = Lerp(a.devices[i].position, b.devices[i].position, alpha);
        out.devices[i].rotation = Slerp(a.devices[i].rotation, b.devices[i].rotation, alpha);
    }
    for (std::size_t i = 0; i < out.controllers.size(); ++i) {
        if (a.controllers[i].has_finger_bends && b.controllers[i].has_finger_bends) {
            FingerBends& bends = out.controllers[i].finger_bends;
            const FingerBends& fa = a.controllers[i].finger_bends;
            const FingerBends& fb = b.controllers[i].finger_bends;
            bends.thumb = fa.thumb + (fb.thumb - fa.thumb) * alpha;
            bends.index = fa.index + (fb.index - fa.index) * alpha;
            bends.middle = fa.middle + (fb.middle - fa.middle) * alpha;
            bends.ring = fa.ring + (fb.ring - fa.ring) * alpha;
            bends.pinky = fa.pinky + (fb.pinky - fa.pinky) * alpha;
        }
    }
    return out;
}

void AnchorDanceFrame(FrameState& frame, float rootX, float rootZ) {
    for (DeviceState& device : frame.devices) {
        device.position.x += rootX;
        device.position.z += rootZ;
    }
}

} // namespace anyadance
