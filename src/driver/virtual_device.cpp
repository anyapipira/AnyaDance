#include "virtual_device.h"
#include "core/constants.h"
#include "log.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <utility>

using namespace vr;
using namespace anyadance;

namespace {

constexpr const char* kDriverSettingsSection = anyadance::kDriverSettingsSection;
constexpr int32_t kDefaultWindowX = 0;
constexpr int32_t kDefaultWindowY = 0;
// Defaults are 1080p to keep the compositor's render cost modest; advanced users
// can raise them (for example to 4K, 3840x2160) through the headset_render_width
// / headset_render_height settings without rebuilding the driver.
constexpr uint32_t kDefaultWindowWidth = 1920;
constexpr uint32_t kDefaultWindowHeight = 1080;
constexpr uint32_t kDefaultRenderWidth = 1920;
constexpr uint32_t kDefaultRenderHeight = 1080;

enum class HeadsetWindowEyeMode {
    Left,
    Right,
    Both,
};

struct DisplaySettings {
    int32_t windowX = kDefaultWindowX;
    int32_t windowY = kDefaultWindowY;
    uint32_t windowWidth = kDefaultWindowWidth;
    uint32_t windowHeight = kDefaultWindowHeight;
    uint32_t renderWidth = kDefaultRenderWidth;
    uint32_t renderHeight = kDefaultRenderHeight;
    HeadsetWindowEyeMode eyeMode = HeadsetWindowEyeMode::Left;
    bool preserveAspect = true;
};

int32_t GetInt32Setting(const char* key, int32_t defaultValue) {
    EVRSettingsError error = VRSettingsError_None;
    const int32_t value = VRSettings()->GetInt32(kDriverSettingsSection, key, &error);
    return error == VRSettingsError_None ? value : defaultValue;
}

bool GetBoolSetting(const char* key, bool defaultValue) {
    EVRSettingsError error = VRSettingsError_None;
    const bool value = VRSettings()->GetBool(kDriverSettingsSection, key, &error);
    return error == VRSettingsError_None ? value : defaultValue;
}

HeadsetWindowEyeMode GetEyeModeSetting() {
    char value[32] = {};
    EVRSettingsError error = VRSettingsError_None;
    VRSettings()->GetString(kDriverSettingsSection, "headset_window_eye_mode", value, sizeof(value), &error);
    if (error != VRSettingsError_None) {
        return HeadsetWindowEyeMode::Left;
    }
    if (std::strcmp(value, "right") == 0) {
        return HeadsetWindowEyeMode::Right;
    }
    if (std::strcmp(value, "both") == 0) {
        return HeadsetWindowEyeMode::Both;
    }
    return HeadsetWindowEyeMode::Left;
}

DisplaySettings LoadDisplaySettings() {
    DisplaySettings settings;
    settings.windowX = GetInt32Setting("headset_window_x", kDefaultWindowX);
    settings.windowY = GetInt32Setting("headset_window_y", kDefaultWindowY);
    settings.windowWidth = static_cast<uint32_t>(
        std::max(1, GetInt32Setting("headset_window_width", static_cast<int32_t>(kDefaultWindowWidth))));
    settings.windowHeight = static_cast<uint32_t>(
        std::max(1, GetInt32Setting("headset_window_height", static_cast<int32_t>(kDefaultWindowHeight))));
    settings.renderWidth = static_cast<uint32_t>(
        std::max(1, GetInt32Setting("headset_render_width", static_cast<int32_t>(kDefaultRenderWidth))));
    settings.renderHeight = static_cast<uint32_t>(
        std::max(1, GetInt32Setting("headset_render_height", static_cast<int32_t>(kDefaultRenderHeight))));
    settings.eyeMode = GetEyeModeSetting();
    settings.preserveAspect = GetBoolSetting("headset_window_preserve_aspect", true);
    return settings;
}

void CenterAspectViewport(
    uint32_t regionX,
    uint32_t regionY,
    uint32_t regionWidth,
    uint32_t regionHeight,
    bool preserveAspect,
    float eyeAspect,
    uint32_t* pnX,
    uint32_t* pnY,
    uint32_t* pnWidth,
    uint32_t* pnHeight) {
    uint32_t width = regionWidth;
    uint32_t height = regionHeight;
    if (preserveAspect && regionWidth > 0 && regionHeight > 0 && eyeAspect > 0.0f) {
        const float regionAspect = static_cast<float>(regionWidth) / static_cast<float>(regionHeight);
        if (regionAspect > eyeAspect) {
            height = regionHeight;
            width = static_cast<uint32_t>(std::round(static_cast<float>(height) * eyeAspect));
        } else {
            width = regionWidth;
            height = static_cast<uint32_t>(std::round(static_cast<float>(width) / eyeAspect));
        }
        width = std::max<uint32_t>(1, std::min(width, regionWidth));
        height = std::max<uint32_t>(1, std::min(height, regionHeight));
    }
    *pnX = regionX + ((regionWidth - width) / 2);
    *pnY = regionY + ((regionHeight - height) / 2);
    *pnWidth = width;
    *pnHeight = height;
}

// ---------------------------------------------------------------------------
// Hand skeleton synthesis
//
// Bone order follows EVRSkeletalBoneIndex from openvr.h (31 bones total):
//   0 Root, 1 Wrist,
//   2-5 Thumb(CMC,MCP,IP,Tip), 6-10 Index, 11-15 Middle, 16-20 Ring, 21-25 Pinky,
//   26-30 Aux(Thumb,Index,Middle,Ring,Pinky)
//
// All transforms are in parent-bone local space (each bone relative to its parent).
// Bone 0 (Root) is relative to the controller device space.
//
// QUATERNION CONVENTION — two separate conventions exist in this driver:
//   • The driver wire protocol uses XYZW: rotation_xyzw = {x, y, z, w}.
//     Do not use the helpers below for pose quaternions.
//   • OpenVR skeleton bones (VRBoneTransform_t.orientation) use WXYZ.
//     vr::HmdQuaternionf_t is declared as {float w, x, y, z}.
//     All helpers below use WXYZ. Struct aggregate {a,b,c,d} → w=a, x=b, y=c, z=d.
//
// SteamVR's Knuckles skeleton convention uses local X as the distal axis for
// finger bones. Using local Z here makes neutral/open fingers appear twisted or collapsed in clients that consume skeletal input.
// The math below follows Valve's handskeletonsimulation driver sample, with
// incoming bend values feeding that model.
// ---------------------------------------------------------------------------

constexpr uint32_t kSteamVRBoneCount = 31;

// One bone transform (position + orientation, both in parent-local space).
using BoneTransform = vr::VRBoneTransform_t;

enum HandSkeletonBone : int {
    eBone_Root = 0,
    eBone_Wrist,
    eBone_Thumb0,
    eBone_Thumb1,
    eBone_Thumb2,
    eBone_Thumb3,
    eBone_IndexFinger0,
    eBone_IndexFinger1,
    eBone_IndexFinger2,
    eBone_IndexFinger3,
    eBone_IndexFinger4,
    eBone_MiddleFinger0,
    eBone_MiddleFinger1,
    eBone_MiddleFinger2,
    eBone_MiddleFinger3,
    eBone_MiddleFinger4,
    eBone_RingFinger0,
    eBone_RingFinger1,
    eBone_RingFinger2,
    eBone_RingFinger3,
    eBone_RingFinger4,
    eBone_PinkyFinger0,
    eBone_PinkyFinger1,
    eBone_PinkyFinger2,
    eBone_PinkyFinger3,
    eBone_PinkyFinger4,
    eBone_Aux_Thumb,
    eBone_Aux_IndexFinger,
    eBone_Aux_MiddleFinger,
    eBone_Aux_RingFinger,
    eBone_Aux_PinkyFinger,
};

struct HandSimSplayableJoint {
    vr::HmdVector2_t swing = {0.0f, 0.0f};
    float twist = 0.0f;
};

struct HandSimJoint {
    float rotation = 0.0f;
};

struct HandSimThumb {
    HandSimSplayableJoint metacarpal;
    HandSimSplayableJoint proximal;
    HandSimJoint distal;
};

struct HandSimFinger {
    HandSimSplayableJoint metacarpal;
    HandSimSplayableJoint proximal;
    HandSimJoint intermediate;
    HandSimJoint distal;
};

struct HandSimHand {
    vr::ETrackedControllerRole role = vr::TrackedControllerRole_Invalid;
    HandSimThumb thumb;
    HandSimFinger fingers[4];
};

struct FingerSplays {
    float thumb = 0.0f;
    float index = 0.0f;
    float middle = 0.0f;
    float ring = 0.0f;
    float pinky = 0.0f;
};

static constexpr float DegToRad(float degrees) {
    return degrees * 3.14159265358979323846f / 180.0f;
}

static constexpr float kFingerJointLengths[5][5] = {
    {0.05f, 0.05f, 0.035f, 0.025f, 0.0f},   // thumb
    {0.03f, 0.073f, 0.045f, 0.025f, 0.02f}, // index
    {0.01f, 0.091f, 0.049f, 0.030f, 0.02f}, // middle
    {0.02f, 0.073f, 0.045f, 0.030f, 0.03f}, // ring
    {0.03f, 0.067f, 0.030f, 0.025f, 0.02f}, // pinky
};

// WXYZ identity bone transform (position at parent origin, no rotation).
static BoneTransform BoneIdentity() {
    BoneTransform t{};
    t.position = {0.0f, 0.0f, 0.0f, 1.0f};
    t.orientation = {1.0f, 0.0f, 0.0f, 0.0f};  // WXYZ: w=1, x=y=z=0
    return t;
}

static vr::HmdQuaternion_t QuatIdentityD() {
    return {1.0, 0.0, 0.0, 0.0};
}

static vr::HmdQuaternion_t QuatMulD(vr::HmdQuaternion_t lhs, vr::HmdQuaternion_t rhs) {
    return {
        lhs.w * rhs.w - lhs.x * rhs.x - lhs.y * rhs.y - lhs.z * rhs.z,
        lhs.w * rhs.x + lhs.x * rhs.w + lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.w * rhs.y - lhs.x * rhs.z + lhs.y * rhs.w + lhs.z * rhs.x,
        lhs.w * rhs.z + lhs.x * rhs.y - lhs.y * rhs.x + lhs.z * rhs.w,
    };
}

static vr::HmdQuaternion_t QuatConjugateD(vr::HmdQuaternion_t q) {
    return {q.w, -q.x, -q.y, -q.z};
}

static vr::HmdVector3_t RotateVector(vr::HmdVector3_t vec, vr::HmdQuaternion_t q) {
    const vr::HmdQuaternion_t qvec = {0.0, vec.v[0], vec.v[1], vec.v[2]};
    const vr::HmdQuaternion_t rotated = QuatMulD(QuatMulD(q, qvec), QuatConjugateD(q));
    return {
        static_cast<float>(rotated.x),
        static_cast<float>(rotated.y),
        static_cast<float>(rotated.z),
    };
}

static vr::HmdQuaternion_t QuatFromSwingTwist(const vr::HmdVector2_t& swing, float twist) {
    vr::HmdQuaternion_t result{};
    const float swing_squared = swing.v[0] * swing.v[0] + swing.v[1] * swing.v[1];
    if (swing_squared > 0.0f) {
        const float theta_swing = std::sqrt(swing_squared);
        const float cos_half_theta_swing = std::cos(theta_swing * 0.5f);
        const float cos_half_theta_twist = std::cos(twist * 0.5f);
        const float sin_half_theta_twist = std::sin(twist * 0.5f);
        const float sin_half_theta_swing_over_theta = std::sin(theta_swing * 0.5f) / theta_swing;
        result.w = cos_half_theta_swing * cos_half_theta_twist;
        result.x = cos_half_theta_swing * sin_half_theta_twist;
        result.y = (swing.v[1] * cos_half_theta_twist * sin_half_theta_swing_over_theta) -
                   (swing.v[0] * sin_half_theta_twist * sin_half_theta_swing_over_theta);
        result.z = (swing.v[0] * cos_half_theta_twist * sin_half_theta_swing_over_theta) +
                   (swing.v[1] * sin_half_theta_twist * sin_half_theta_swing_over_theta);
    } else {
        const float half_twist = twist * 0.5f;
        const float cos_half_twist = std::cos(half_twist);
        const float sin_half_twist = std::sin(half_twist);
        result.w = cos_half_twist;
        result.x = sin_half_twist;
        result.y = swing.v[1] * cos_half_twist * 0.5f - swing.v[0] * sin_half_twist * 0.5f;
        result.z = swing.v[0] * cos_half_twist * 0.5f + swing.v[1] * sin_half_twist * 0.5f;
    }
    return result;
}

static vr::HmdQuaternion_t QuatFromEulerAngles(double roll, double pitch, double yaw) {
    const double cr = std::cos(roll * 0.5);
    const double sr = std::sin(roll * 0.5);
    const double cp = std::cos(pitch * 0.5);
    const double sp = std::sin(pitch * 0.5);
    const double cy = std::cos(yaw * 0.5);
    const double sy = std::sin(yaw * 0.5);
    return {
        cr * cp * cy + sr * sp * sy,
        cr * sp * cy + sr * cp * sy,
        cr * cp * sy - sr * sp * cy,
        sr * cp * cy - cr * sp * sy,
    };
}

static void CopyQuat(vr::HmdQuaternion_t in_quat, vr::HmdQuaternionf_t& out_quat) {
    out_quat.w = static_cast<float>(in_quat.w);
    out_quat.x = static_cast<float>(in_quat.x);
    out_quat.y = static_cast<float>(in_quat.y);
    out_quat.z = static_cast<float>(in_quat.z);
}

static void CopyVec(vr::HmdVector3_t in_vec, vr::HmdVector4_t& out_vec) {
    out_vec.v[0] = in_vec.v[0];
    out_vec.v[1] = in_vec.v[1];
    out_vec.v[2] = in_vec.v[2];
    out_vec.v[3] = 1.0f;
}

static void InitHand(HandSimHand& hand) {
    for (auto& finger : hand.fingers) {
        finger.metacarpal.swing.v[1] = 0.0f;
        finger.metacarpal.twist = 0.0f;
        finger.proximal.swing.v[1] = DegToRad(10.0f);
        finger.intermediate.rotation = DegToRad(5.0f);
        finger.distal.rotation = DegToRad(5.0f);
    }

    hand.thumb.metacarpal.swing.v[0] = DegToRad(10.0f);
    hand.thumb.metacarpal.swing.v[1] = DegToRad(40.0f);
    hand.thumb.metacarpal.twist = DegToRad(70.0f);
    hand.thumb.proximal.swing.v[0] = 0.0f;
    hand.thumb.proximal.swing.v[1] = 0.0f;
    hand.thumb.proximal.twist = 0.0f;
    hand.thumb.distal.rotation = 0.0f;

    hand.fingers[0].metacarpal.swing.v[1] = DegToRad(13.0f);
    hand.fingers[1].metacarpal.swing.v[1] = DegToRad(0.0f);
    hand.fingers[2].metacarpal.swing.v[1] = DegToRad(-15.0f);
    hand.fingers[3].metacarpal.swing.v[1] = DegToRad(-27.0f);

    hand.fingers[0].proximal.swing.v[1] = DegToRad(3.0f);
    hand.fingers[1].proximal.swing.v[1] = DegToRad(0.0f);
    hand.fingers[2].proximal.swing.v[1] = DegToRad(-1.0f);
    hand.fingers[3].proximal.swing.v[1] = DegToRad(-2.0f);
}

static void ApplyGenericFingerTransform(float curl, float splay, HandSimFinger& finger) {
    finger.metacarpal.swing.v[0] += DegToRad(curl * 5.0f);
    finger.proximal.swing.v[0] += DegToRad(curl * 90.0f);
    finger.proximal.swing.v[1] += DegToRad(splay * 15.0f);
    finger.intermediate.rotation += DegToRad(curl * 80.0f);
    finger.distal.rotation += DegToRad(curl * 80.0f);
}

static void ComputeBoneTransform(
    vr::ETrackedControllerRole role,
    vr::HmdQuaternion_t orientation,
    vr::HmdVector3_t position,
    BoneTransform& out_transform)
{
    CopyQuat(orientation, out_transform.orientation);
    CopyVec(position, out_transform.position);
    if (role == vr::TrackedControllerRole_RightHand) {
        out_transform.position.v[0] *= -1.0f;
    }
}

static void ComputeBoneTransform(
    vr::ETrackedControllerRole role,
    vr::HmdQuaternion_t orientation,
    float joint_length,
    BoneTransform& out_transform)
{
    ComputeBoneTransform(role, orientation, {joint_length, 0.0f, 0.0f}, out_transform);
}

static void ComputeMetacarpalBoneTransform(
    vr::ETrackedControllerRole role,
    vr::HmdQuaternion_t orientation,
    float joint_length,
    BoneTransform& out_transform)
{
    const vr::HmdVector3_t offset = {joint_length, 0.0f, 0.0f};
    const vr::HmdQuaternion_t magic = {0.5, 0.5, -0.5, 0.5};
    vr::HmdQuaternion_t bone_orientation = QuatMulD(magic, orientation);
    vr::HmdVector3_t bone_position = RotateVector(offset, bone_orientation);

    if (role == vr::TrackedControllerRole_RightHand) {
        std::swap(bone_orientation.w, bone_orientation.x);
        std::swap(bone_orientation.y, bone_orientation.z);
        bone_orientation.x *= -1.0;
        bone_orientation.z *= -1.0;
    }

    ComputeBoneTransform(role, bone_orientation, bone_position, out_transform);
}

static int FingerBoneIndex(int finger, int bone_in_finger) {
    return eBone_IndexFinger0 + finger * 5 + bone_in_finger;
}

static void ComputeSkeletalTransforms(const HandSimHand& hand, std::array<BoneTransform, kSteamVRBoneCount>& out) {
    for (auto& bone : out) {
        bone = BoneIdentity();
    }

    out[eBone_Root] = BoneIdentity();
    out[eBone_Wrist] = {
        {-0.034038f, 0.036503f, 0.164722f, 1.000000f},
        {-0.055147f, -0.078608f, -0.920279f, 0.379296f},
    };
    if (hand.role == vr::TrackedControllerRole_RightHand) {
        out[eBone_Wrist].position.v[0] *= -1.0f;
        out[eBone_Wrist].orientation.y *= -1.0f;
        out[eBone_Wrist].orientation.z *= -1.0f;
    }

    ComputeMetacarpalBoneTransform(
        hand.role,
        QuatFromSwingTwist(hand.thumb.metacarpal.swing, hand.thumb.metacarpal.twist),
        kFingerJointLengths[0][0],
        out[eBone_Thumb0]);
    ComputeBoneTransform(
        hand.role,
        QuatFromSwingTwist(hand.thumb.proximal.swing, hand.thumb.metacarpal.twist),
        kFingerJointLengths[0][1],
        out[eBone_Thumb1]);
    ComputeBoneTransform(
        hand.role,
        QuatFromEulerAngles(hand.thumb.distal.rotation, 0.0, 0.0),
        kFingerJointLengths[0][2],
        out[eBone_Thumb2]);
    ComputeBoneTransform(hand.role, QuatIdentityD(), kFingerJointLengths[0][3], out[eBone_Thumb3]);

    for (int finger = 0; finger < 4; ++finger) {
        ComputeMetacarpalBoneTransform(
            hand.role,
            QuatFromSwingTwist(hand.fingers[finger].metacarpal.swing, hand.fingers[finger].metacarpal.twist),
            kFingerJointLengths[finger + 1][0],
            out[FingerBoneIndex(finger, 0)]);
        ComputeBoneTransform(
            hand.role,
            QuatFromSwingTwist(hand.fingers[finger].proximal.swing, hand.fingers[finger].proximal.twist),
            kFingerJointLengths[finger + 1][1],
            out[FingerBoneIndex(finger, 1)]);
        ComputeBoneTransform(
            hand.role,
            QuatFromEulerAngles(hand.fingers[finger].intermediate.rotation, 0.0, 0.0),
            kFingerJointLengths[finger + 1][2],
            out[FingerBoneIndex(finger, 2)]);
        ComputeBoneTransform(
            hand.role,
            QuatFromEulerAngles(hand.fingers[finger].distal.rotation, 0.0, 0.0),
            kFingerJointLengths[finger + 1][3],
            out[FingerBoneIndex(finger, 3)]);
        ComputeBoneTransform(hand.role, QuatIdentityD(), kFingerJointLengths[finger + 1][4], out[FingerBoneIndex(finger, 4)]);
    }
}

// Fill in the hand skeleton from bend values using the OpenVR sample model.
static void BuildHandPose(
    bool is_left,
    const FingerBends& bends,
    std::array<BoneTransform, kSteamVRBoneCount>& out)
{
    HandSimHand hand{};
    hand.role = is_left ? vr::TrackedControllerRole_LeftHand : vr::TrackedControllerRole_RightHand;
    InitHand(hand);

    const FingerSplays splays{};
    hand.thumb.metacarpal.swing.v[0] += DegToRad(bends.thumb * 5.0f);
    hand.thumb.metacarpal.swing.v[1] += DegToRad(splays.thumb * 5.0f);
    hand.thumb.metacarpal.twist = 0.0f;
    hand.thumb.proximal.swing.v[0] += DegToRad(bends.thumb * 90.0f);
    hand.thumb.proximal.swing.v[1] += DegToRad(splays.thumb * 20.0f);
    hand.thumb.proximal.twist = 0.0f;
    hand.thumb.distal.rotation += DegToRad(bends.thumb * 90.0f);

    ApplyGenericFingerTransform(bends.index, splays.index, hand.fingers[0]);
    ApplyGenericFingerTransform(bends.middle, splays.middle, hand.fingers[1]);
    ApplyGenericFingerTransform(bends.ring, splays.ring, hand.fingers[2]);
    ApplyGenericFingerTransform(bends.pinky, splays.pinky, hand.fingers[3]);
    ComputeSkeletalTransforms(hand, out);
}

const char* ModelNameFor(VirtualDeviceKind kind) {
    switch (kind) {
    case VirtualDeviceKind::Hmd:
        return "AnyaDance Virtual HMD";
    case VirtualDeviceKind::Controller:
        return "AnyaDance Virtual Controller";
    case VirtualDeviceKind::Tracker:
        return "AnyaDance Virtual Tracker";
    }
    return "AnyaDance Virtual Device";
}

} // namespace

struct VirtualDevice::DisplayComponent final : public IVRDisplayComponent {
    DisplayComponent()
        : m_settings(LoadDisplaySettings()) {}

    bool IsDisplayOnDesktop() override {
        return true;
    }

    bool IsDisplayRealDisplay() override {
        return false;
    }

    void GetRecommendedRenderTargetSize(uint32_t* pnWidth, uint32_t* pnHeight) override {
        *pnWidth = m_settings.renderWidth;
        *pnHeight = m_settings.renderHeight;
    }

    void GetEyeOutputViewport(EVREye eEye, uint32_t* pnX, uint32_t* pnY, uint32_t* pnWidth, uint32_t* pnHeight) override {
        const bool showLeft = m_settings.eyeMode == HeadsetWindowEyeMode::Left ||
            m_settings.eyeMode == HeadsetWindowEyeMode::Both;
        const bool showRight = m_settings.eyeMode == HeadsetWindowEyeMode::Right ||
            m_settings.eyeMode == HeadsetWindowEyeMode::Both;
        if ((eEye == Eye_Left && !showLeft) || (eEye == Eye_Right && !showRight)) {
            *pnX = m_settings.windowWidth + 100;
            *pnY = m_settings.windowHeight + 100;
            *pnWidth = 1;
            *pnHeight = 1;
            return;
        }

        uint32_t regionX = 0;
        uint32_t regionY = 0;
        uint32_t regionWidth = m_settings.windowWidth;
        uint32_t regionHeight = m_settings.windowHeight;
        if (m_settings.eyeMode == HeadsetWindowEyeMode::Both) {
            const uint32_t leftWidth = m_settings.windowWidth / 2;
            const uint32_t rightWidth = m_settings.windowWidth - leftWidth;
            regionX = eEye == Eye_Left ? 0 : leftWidth;
            regionWidth = eEye == Eye_Left ? leftWidth : rightWidth;
        }

        CenterAspectViewport(
            regionX,
            regionY,
            std::max<uint32_t>(1, regionWidth),
            std::max<uint32_t>(1, regionHeight),
            m_settings.preserveAspect,
            static_cast<float>(m_settings.renderWidth) / static_cast<float>(m_settings.renderHeight),
            pnX,
            pnY,
            pnWidth,
            pnHeight);
    }

    void GetProjectionRaw(EVREye /*eEye*/, float* pfLeft, float* pfRight, float* pfTop, float* pfBottom) override {
        // Bounds are tangents of the half-FOV angles. The horizontal extent is
        // widened by the render target's aspect ratio so pixels stay square at any
        // configured resolution; a square (+/-1) frustum into a non-square target
        // would stretch the image horizontally.
        const float aspect = static_cast<float>(m_settings.renderWidth) / static_cast<float>(m_settings.renderHeight);
        *pfLeft = -aspect;
        *pfRight = aspect;
        *pfTop = -1.0f;
        *pfBottom = 1.0f;
    }

    DistortionCoordinates_t ComputeDistortion(EVREye /*eEye*/, float fU, float fV) override {
        DistortionCoordinates_t coordinates{};
        coordinates.rfRed[0] = fU;
        coordinates.rfRed[1] = fV;
        coordinates.rfGreen[0] = fU;
        coordinates.rfGreen[1] = fV;
        coordinates.rfBlue[0] = fU;
        coordinates.rfBlue[1] = fV;
        return coordinates;
    }

    bool ComputeInverseDistortion(HmdVector2_t* pResult, EVREye /*eEye*/, uint32_t /*unChannel*/, float fU, float fV) override {
        // The virtual display uses identity distortion; inverse is identity too.
        pResult->v[0] = fU;
        pResult->v[1] = fV;
        return true;
    }

    void GetWindowBounds(int32_t* pnX, int32_t* pnY, uint32_t* pnWidth, uint32_t* pnHeight) override {
        *pnX = m_settings.windowX;
        *pnY = m_settings.windowY;
        *pnWidth = m_settings.windowWidth;
        *pnHeight = m_settings.windowHeight;
    }

private:
    DisplaySettings m_settings;
};

VirtualDevice::VirtualDevice(VirtualDeviceDefinition definition)
    : m_definition(std::move(definition)),
      m_objectId(k_unTrackedDeviceIndexInvalid),
      m_triggerClick(k_ulInvalidInputComponentHandle),
      m_triggerValue(k_ulInvalidInputComponentHandle),
      m_menuClick(k_ulInvalidInputComponentHandle),
      m_aClick(k_ulInvalidInputComponentHandle),
      m_bClick(k_ulInvalidInputComponentHandle),
      m_gripClick(k_ulInvalidInputComponentHandle),
      m_gripValue(k_ulInvalidInputComponentHandle),
      m_gripForce(k_ulInvalidInputComponentHandle),
      m_gripTouch(k_ulInvalidInputComponentHandle),
      m_joystickX(k_ulInvalidInputComponentHandle),
      m_joystickY(k_ulInvalidInputComponentHandle),
      m_trackpadX(k_ulInvalidInputComponentHandle),
      m_trackpadY(k_ulInvalidInputComponentHandle),
      m_skeletonHandle(k_ulInvalidInputComponentHandle) {
    std::memset(&m_pose, 0, sizeof(m_pose));
    m_pose.qWorldFromDriverRotation.w = 1.0f;
    m_pose.qDriverFromHeadRotation.w = 1.0f;
    m_pose.qRotation.w = 1.0f;
    if (m_definition.kind == VirtualDeviceKind::Hmd) {
        m_display = std::make_unique<DisplayComponent>();
    }
    ApplyNeutralPose();
}

VirtualDevice::~VirtualDevice() = default;

EVRInitError VirtualDevice::Activate(uint32_t unObjectId) {
    m_objectId = unObjectId;
    auto* properties = VRProperties();
    const auto container = properties->TrackedDeviceToPropertyContainer(m_objectId);

    ActivateCommon(container);
    switch (m_definition.kind) {
    case VirtualDeviceKind::Hmd:
        ActivateHmd(container);
        break;
    case VirtualDeviceKind::Controller:
        ActivateController(container);
        break;
    case VirtualDeviceKind::Tracker:
        ActivateTracker(container);
        break;
    }
    return VRInitError_None;
}

void VirtualDevice::Deactivate() {
    m_objectId = k_unTrackedDeviceIndexInvalid;
    m_triggerClick = k_ulInvalidInputComponentHandle;
    m_triggerValue = k_ulInvalidInputComponentHandle;
    m_menuClick = k_ulInvalidInputComponentHandle;
    m_aClick = k_ulInvalidInputComponentHandle;
    m_bClick = k_ulInvalidInputComponentHandle;
    m_gripClick = k_ulInvalidInputComponentHandle;
    m_gripValue = k_ulInvalidInputComponentHandle;
    m_gripForce = k_ulInvalidInputComponentHandle;
    m_gripTouch = k_ulInvalidInputComponentHandle;
    m_joystickX = k_ulInvalidInputComponentHandle;
    m_joystickY = k_ulInvalidInputComponentHandle;
    m_trackpadX = k_ulInvalidInputComponentHandle;
    m_trackpadY = k_ulInvalidInputComponentHandle;
    m_skeletonHandle = k_ulInvalidInputComponentHandle;
    m_hasFingerBends = false;
}

void VirtualDevice::EnterStandby() {
    DriverLog("[anyadance] EnterStandby: %s\n", m_definition.serial.c_str());
}

void* VirtualDevice::GetComponent(const char* pchComponentNameAndVersion) {
    if (m_definition.kind == VirtualDeviceKind::Hmd &&
        std::strcmp(pchComponentNameAndVersion, IVRDisplayComponent_Version) == 0) {
        return m_display.get();
    }
    return nullptr;
}

void VirtualDevice::DebugRequest(const char* /*pchRequest*/, char* pchResponseBuffer, uint32_t unResponseBufferSize) {
    if (unResponseBufferSize > 0) {
        pchResponseBuffer[0] = '\0';
    }
}

DriverPose_t VirtualDevice::GetPose() {
    return m_pose;
}

void VirtualDevice::ApplyPoseSample(const PoseSample& sample) {
    PoseSample safeSample = sample;
    if (safeSample.position[1] < kMinDeviceY || safeSample.position[1] > kMaxDeviceY) {
        safeSample.position[1] = ClampDeviceY(safeSample.position[1]);
        safeSample.y_clamped = true;
    }
    if (safeSample.y_clamped) {
        static auto lastClampWarning = std::chrono::steady_clock::time_point{};
        const auto now = std::chrono::steady_clock::now();
        if (now - lastClampWarning > std::chrono::seconds(1)) {
            DriverLog("[anyadance] Clamped device Y to [%.2f, %.2f] m; device=%s\n",
                      kMinDeviceY, kMaxDeviceY, m_definition.serial.c_str());
            lastClampWarning = now;
        }
    }

    m_pose.deviceIsConnected = true;
    m_pose.poseIsValid = true;
    m_pose.result = TrackingResult_Running_OK;
    m_pose.vecPosition[0] = safeSample.position[0];
    m_pose.vecPosition[1] = safeSample.position[1];
    m_pose.vecPosition[2] = safeSample.position[2];
    m_pose.qRotation.x = safeSample.rotation_xyzw[0];
    m_pose.qRotation.y = safeSample.rotation_xyzw[1];
    m_pose.qRotation.z = safeSample.rotation_xyzw[2];
    m_pose.qRotation.w = safeSample.rotation_xyzw[3];

    if (m_definition.kind == VirtualDeviceKind::Controller) {
        m_pose.poseTimeOffset = 0.0;
        m_currentTriggerClick = safeSample.trigger_click;
        m_currentTriggerValue = std::clamp(safeSample.trigger_value, 0.0f, 1.0f);
        m_currentMenuClick = safeSample.menu_click;
        m_currentAClick = safeSample.a_click;
        m_currentBClick = safeSample.b_click;
        m_currentGripClick = safeSample.grip_click;
        m_currentGripValue = std::clamp(safeSample.grip_value, 0.0f, 1.0f);
        m_currentJoystickX = std::clamp(safeSample.joystick_x, -1.0f, 1.0f);
        m_currentJoystickY = std::clamp(safeSample.joystick_y, -1.0f, 1.0f);
        m_currentTrackpadX = std::clamp(safeSample.trackpad_x, -1.0f, 1.0f);
        m_currentTrackpadY = std::clamp(safeSample.trackpad_y, -1.0f, 1.0f);
        if (safeSample.has_finger_bends) {
            m_currentFingerBends = safeSample.finger_bends;
            m_hasFingerBends = true;
        }
    }
}

void VirtualDevice::ApplyNeutralPose() {
    m_pose.deviceIsConnected = true;
    m_pose.poseIsValid = true;
    m_pose.result = TrackingResult_Running_OK;
    m_pose.qRotation.x = 0.0f;
    m_pose.qRotation.y = 0.0f;
    m_pose.qRotation.z = 0.0f;
    m_pose.qRotation.w = 1.0f;
    m_pose.vecPosition[0] = m_definition.neutralPosition[0];
    m_pose.vecPosition[1] = m_definition.neutralPosition[1];
    m_pose.vecPosition[2] = m_definition.neutralPosition[2];
}

void VirtualDevice::ApplyInvalidPose() {
    m_pose.deviceIsConnected = false;
    m_pose.poseIsValid = false;
    m_pose.result = TrackingResult_Uninitialized;
    m_currentTriggerClick = false;
    m_currentTriggerValue = 0.0f;
    m_currentMenuClick = false;
    m_currentAClick = false;
    m_currentBClick = false;
    m_currentGripClick = false;
    m_currentGripValue = 0.0f;
    m_currentJoystickX = 0.0f;
    m_currentJoystickY = 0.0f;
    m_currentTrackpadX = 0.0f;
    m_currentTrackpadY = 0.0f;
}

void VirtualDevice::UpdateInputs() {
    if (m_definition.kind != VirtualDeviceKind::Controller) {
        return;
    }
    if (m_triggerClick != k_ulInvalidInputComponentHandle) {
        VRDriverInput()->UpdateBooleanComponent(m_triggerClick, m_currentTriggerClick, 0.0);
    }
    if (m_triggerValue != k_ulInvalidInputComponentHandle) {
        VRDriverInput()->UpdateScalarComponent(m_triggerValue, m_currentTriggerValue, 0.0);
    }
    if (m_menuClick != k_ulInvalidInputComponentHandle) {
        VRDriverInput()->UpdateBooleanComponent(m_menuClick, m_currentMenuClick, 0.0);
    }
    if (m_aClick != k_ulInvalidInputComponentHandle) {
        VRDriverInput()->UpdateBooleanComponent(m_aClick, m_currentAClick, 0.0);
    }
    if (m_bClick != k_ulInvalidInputComponentHandle) {
        VRDriverInput()->UpdateBooleanComponent(m_bClick, m_currentBClick, 0.0);
    }
    if (m_gripClick != k_ulInvalidInputComponentHandle) {
        VRDriverInput()->UpdateBooleanComponent(m_gripClick, m_currentGripClick, 0.0);
    }
    if (m_gripValue != k_ulInvalidInputComponentHandle) {
        VRDriverInput()->UpdateScalarComponent(m_gripValue, m_currentGripValue, 0.0);
    }
    // VRChat's default Index "Grab" binding reads grip force, so drive force
    // from the same grip value and report touch while gripping.
    if (m_gripForce != k_ulInvalidInputComponentHandle) {
        VRDriverInput()->UpdateScalarComponent(m_gripForce, m_currentGripValue, 0.0);
    }
    if (m_gripTouch != k_ulInvalidInputComponentHandle) {
        VRDriverInput()->UpdateBooleanComponent(m_gripTouch, m_currentGripClick, 0.0);
    }
    if (m_joystickX != k_ulInvalidInputComponentHandle) {
        VRDriverInput()->UpdateScalarComponent(m_joystickX, m_currentJoystickX, 0.0);
    }
    if (m_joystickY != k_ulInvalidInputComponentHandle) {
        VRDriverInput()->UpdateScalarComponent(m_joystickY, m_currentJoystickY, 0.0);
    }
    if (m_trackpadX != k_ulInvalidInputComponentHandle) {
        VRDriverInput()->UpdateScalarComponent(m_trackpadX, m_currentTrackpadX, 0.0);
    }
    if (m_trackpadY != k_ulInvalidInputComponentHandle) {
        VRDriverInput()->UpdateScalarComponent(m_trackpadY, m_currentTrackpadY, 0.0);
    }
    if (m_skeletonHandle != k_ulInvalidInputComponentHandle) {
        // Build the hand pose from current bends (or neutral/open if no data).
        const bool isLeft = m_definition.controllerRole == TrackedControllerRole_LeftHand;
        const FingerBends& bends = m_hasFingerBends ? m_currentFingerBends : FingerBends{};
        std::array<BoneTransform, kSteamVRBoneCount> bonePose{};
        BuildHandPose(isLeft, bends, bonePose);
        // WithoutController: free expressive hands (not constrained to hold shape).
        VRDriverInput()->UpdateSkeletonComponent(
            m_skeletonHandle,
            VRSkeletalMotionRange_WithoutController,
            bonePose.data(),
            kSteamVRBoneCount);
        // WithController: same pose — driver uses the same open pose for this range.
        VRDriverInput()->UpdateSkeletonComponent(
            m_skeletonHandle,
            VRSkeletalMotionRange_WithController,
            bonePose.data(),
            kSteamVRBoneCount);
    }
}

void VirtualDevice::UpdatePose() {
    if (m_objectId == k_unTrackedDeviceIndexInvalid) {
        return;
    }

    VRServerDriverHost()->TrackedDevicePoseUpdated(m_objectId, m_pose, sizeof(DriverPose_t));
}

void VirtualDevice::ActivateCommon(PropertyContainerHandle_t container) {
    const std::string registeredDeviceType = "anyadance/" + m_definition.serial;
    VRProperties()->SetStringProperty(container, Prop_ModelNumber_String, ModelNameFor(m_definition.kind));
    VRProperties()->SetStringProperty(container, Prop_SerialNumber_String, m_definition.serial.c_str());
    VRProperties()->SetStringProperty(container, Prop_ManufacturerName_String, "AnyaDance");
    VRProperties()->SetStringProperty(container, Prop_TrackingSystemName_String, "anyadance");
    VRProperties()->SetStringProperty(container, Prop_RegisteredDeviceType_String, registeredDeviceType.c_str());
    VRProperties()->SetBoolProperty(container, Prop_WillDriftInYaw_Bool, false);
    VRProperties()->SetBoolProperty(container, Prop_NeverTracked_Bool, false);
    // Virtual devices hold a constant pose while the user is still, so SteamVR
    // would otherwise idle them into standby/powersave after a few seconds. Tell
    // it not to treat lack of motion as a reason to enter standby.
    VRProperties()->SetBoolProperty(container, Prop_IgnoreMotionForStandby_Bool, true);
}

void VirtualDevice::ActivateHmd(PropertyContainerHandle_t container) {
    VRProperties()->SetBoolProperty(container, Prop_DeviceCanPowerOff_Bool, false);
    VRProperties()->SetBoolProperty(container, Prop_ContainsProximitySensor_Bool, false);
    VRProperties()->SetFloatProperty(container, Prop_UserIpdMeters_Float, 0.063f);
    VRProperties()->SetFloatProperty(container, Prop_DisplayFrequency_Float, 90.0f);
    VRProperties()->SetFloatProperty(container, Prop_UserHeadToEyeDepthMeters_Float, 0.0f);
    VRProperties()->SetFloatProperty(container, Prop_SecondsFromVsyncToPhotons_Float, 0.011f);
    VRProperties()->SetBoolProperty(container, Prop_IsOnDesktop_Bool, true);
    VRProperties()->SetBoolProperty(container, Prop_DisplayDebugMode_Bool, true);
    VRProperties()->SetInt32Property(container, Prop_HmdTrackingStyle_Int32, HmdTrackingStyle_Lighthouse);
    // HMDs still need an input profile even though this one exposes no inputs.
    VRProperties()->SetStringProperty(container, Prop_InputProfilePath_String, "{anyadance}/input/anyadance_hmd_profile.json");
}

void VirtualDevice::ActivateController(PropertyContainerHandle_t container) {
    // Opt out of SteamVR power management so the controller is never put to
    // sleep and SteamVR never shows "move your controller to wake it up".
    VRProperties()->SetBoolProperty(container, Prop_DeviceCanPowerOff_Bool, false);
    VRProperties()->SetBoolProperty(container, Prop_DeviceProvidesBatteryStatus_Bool, false);
    VRProperties()->SetFloatProperty(container, Prop_DeviceBatteryPercentage_Float, 1.0f);
    DriverLog("[anyadance] Controller %s: power-off=false, battery-status=false, battery=100%%\n",
              m_definition.serial.c_str());

    // Advertise as Valve Index ("knuckles") controllers so VRChat's default
    // binding maps right A->Jump, left A->Toggle Mute, and the thumbstick to
    // locomotion without the user editing bindings.
    VRProperties()->SetStringProperty(container, Prop_ControllerType_String, "knuckles");
    VRProperties()->SetInt32Property(container, Prop_ControllerRoleHint_Int32, m_definition.controllerRole);
    VRProperties()->SetStringProperty(container, Prop_InputProfilePath_String, "{anyadance}/input/anyadance_controller_profile.json");
    // Point SteamVR/VRChat at the Valve Index "knuckles" render models so the
    // controllers appear as left/right VR hand controllers in the SteamVR
    // dashboard, bindings UI, and VRChat — not the generic dual-handed
    // gamepad placeholder that shows up when no render model is set.
    const bool isLeft = m_definition.controllerRole == TrackedControllerRole_LeftHand;
    VRProperties()->SetStringProperty(
        container,
        Prop_RenderModelName_String,
        isLeft
            ? "{indexcontroller}valve_controller_knu_1_0_left"
            : "{indexcontroller}valve_controller_knu_1_0_right");
    // Override the generic controller name set by ActivateCommon with a
    // per-side label so the bindings UI shows handed controller entries.
    VRProperties()->SetStringProperty(
        container,
        Prop_ModelNumber_String,
        isLeft ? "AnyaDance Index Left Controller" : "AnyaDance Index Right Controller");
    // VRChat menus and setup flows need normal controller inputs. Keep
    // application_menu exposed for bindings that use it; current VRChat Index
    // defaults open the Quick Menu from B.
    VRDriverInput()->CreateScalarComponent(
        container,
        "/input/trigger/value",
        &m_triggerValue,
        VRScalarType_Absolute,
        VRScalarUnits_NormalizedOneSided);
    VRDriverInput()->CreateBooleanComponent(container, "/input/trigger/click", &m_triggerClick);
    VRDriverInput()->CreateBooleanComponent(container, "/input/application_menu/click", &m_menuClick);
    // Extra face buttons. VRChat's default Index binding uses A for Jump/Mute
    // depending on hand; B is Quick Menu.
    VRDriverInput()->CreateBooleanComponent(container, "/input/a/click", &m_aClick);
    VRDriverInput()->CreateBooleanComponent(container, "/input/b/click", &m_bClick);
    // Grip (squeeze). Expose the Index grip surface
    // — value, force, touch, and click — and drive them together, because
    // VRChat's default Index "Grab" binding reads grip FORCE (not click); only
    // exposing click meant the grab never fired.
    VRDriverInput()->CreateScalarComponent(
        container,
        "/input/grip/value",
        &m_gripValue,
        VRScalarType_Absolute,
        VRScalarUnits_NormalizedOneSided);
    VRDriverInput()->CreateScalarComponent(
        container,
        "/input/grip/force",
        &m_gripForce,
        VRScalarType_Absolute,
        VRScalarUnits_NormalizedOneSided);
    VRDriverInput()->CreateBooleanComponent(container, "/input/grip/touch", &m_gripTouch);
    VRDriverInput()->CreateBooleanComponent(container, "/input/grip/click", &m_gripClick);
    // Knuckles locomotion lives on the thumbstick; the joystick_x/y wire values
    // drive it (the trackpad mirrors them below for bindings that read trackpad).
    VRDriverInput()->CreateScalarComponent(
        container,
        "/input/thumbstick/x",
        &m_joystickX,
        VRScalarType_Absolute,
        VRScalarUnits_NormalizedTwoSided);
    VRDriverInput()->CreateScalarComponent(
        container,
        "/input/thumbstick/y",
        &m_joystickY,
        VRScalarType_Absolute,
        VRScalarUnits_NormalizedTwoSided);
    VRDriverInput()->CreateScalarComponent(
        container,
        "/input/trackpad/x",
        &m_trackpadX,
        VRScalarType_Absolute,
        VRScalarUnits_NormalizedTwoSided);
    VRDriverInput()->CreateScalarComponent(
        container,
        "/input/trackpad/y",
        &m_trackpadY,
        VRScalarType_Absolute,
        VRScalarUnits_NormalizedTwoSided);

    // Hand skeleton (public driver).
    // Paths follow the OpenVR convention for Knuckles/Index controllers.
    // Component name: /input/skeleton/{left|right}
    // Skeleton path:  /skeleton/hand/{left|right}
    // Tracking level: Partial (full individual finger tracking, no per-phalange curl).
    // Grip limit transforms are only used by SteamVR to calculate curl/splay.
    // Passing nullptr lets SteamVR use its controller defaults; the live pose
    // still comes from UpdateSkeletonComponent below.
    const char* skeletonComponentName = isLeft ? "/input/skeleton/left" : "/input/skeleton/right";
    const char* skeletonPath = isLeft ? "/skeleton/hand/left" : "/skeleton/hand/right";
    VRDriverInput()->CreateSkeletonComponent(
        container,
        skeletonComponentName,
        skeletonPath,
        "/pose/raw",
        VRSkeletalTracking_Partial,
        nullptr,
        0,
        &m_skeletonHandle);
}

void VirtualDevice::ActivateTracker(PropertyContainerHandle_t container) {
    // Opt out of SteamVR power management — trackers must never sleep or the
    // avatar's limb tracking drops.
    VRProperties()->SetBoolProperty(container, Prop_DeviceCanPowerOff_Bool, false);
    VRProperties()->SetBoolProperty(container, Prop_DeviceProvidesBatteryStatus_Bool, false);
    VRProperties()->SetFloatProperty(container, Prop_DeviceBatteryPercentage_Float, 1.0f);
    DriverLog("[anyadance] Tracker %s: power-off=false, battery-status=false, battery=100%%\n",
              m_definition.serial.c_str());
}
