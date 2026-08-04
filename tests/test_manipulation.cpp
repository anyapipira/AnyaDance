#include "test_framework.h"
#include "tests.h"

#include "core/constants.h"
#include "core/frame_state.h"
#include "core/manipulation.h"
#include "core/math3d.h"
#include "core/tpose.h"

#include <algorithm>
#include <cmath>

namespace anyadance::tests {
namespace {

using ::anyadance::testing::Canonical;

Quat ToHmdYawLocal(Quat rotation, float hmdYaw) {
    return Canonical(Multiply(Conjugate(FromYaw(hmdYaw)), rotation));
}

void ExpectMirroredInBasis(const FrameState& frame, float yawBasis, DeviceIndex activeDevice, DeviceIndex mirroredDevice, const char* file, int line) {
    const Quat activeLocal = ToHmdYawLocal(frame.devices[DeviceSlot(activeDevice)].rotation, yawBasis);
    const Quat mirroredLocal = ToHmdYawLocal(frame.devices[DeviceSlot(mirroredDevice)].rotation, yawBasis);
    const Quat expected = Canonical({activeLocal.x, -activeLocal.y, -activeLocal.z, activeLocal.w});
    ::anyadance::testing::ExpectSameRotation(mirroredLocal, expected, "mirrored hand rotation", file, line);
}

void ExpectMirroredInYawBasis(const FrameState& frame, const DragSnapshot& drag, DeviceIndex activeDevice, DeviceIndex mirroredDevice, const char* file, int line) {
    ExpectMirroredInBasis(frame, drag.hmdYawBasis, activeDevice, mirroredDevice, file, line);
}

} // namespace

#define EXPECT_MIRRORED_IN_YAW_BASIS(frame, drag, active, mirrored) ExpectMirroredInYawBasis((frame), (drag), (active), (mirrored), __FILE__, __LINE__)
#define EXPECT_MIRRORED_IN_BASIS(frame, yawBasis, active, mirrored) ExpectMirroredInBasis((frame), (yawBasis), (active), (mirrored), __FILE__, __LINE__)

void TestManipulation() {
    FrameState frame = BuildResetTPose(MakeNeutralFrame());
    const Vec3 hmdStart = frame.devices[DeviceSlot(DeviceIndex::Hmd)].position;
    DragSnapshot hmdDrag = BeginDrag(frame, DeviceIndex::Hmd);
    ApplyDragDelta(hmdDrag, frame, 100.0f, -50.0f, ManipulationModifier_None);
    EXPECT_NEAR(frame.devices[DeviceSlot(DeviceIndex::Hmd)].position.x, hmdStart.x, 0.0001f);
    EXPECT_NEAR(frame.devices[DeviceSlot(DeviceIndex::Hmd)].position.y, hmdStart.y, 0.0001f);
    EXPECT_TRUE(frame.devices[DeviceSlot(DeviceIndex::Hmd)].rotation.y < 0.0f);

    hmdDrag = BeginDrag(frame, DeviceIndex::Hmd);
    const Quat beforeRoll = frame.devices[DeviceSlot(DeviceIndex::Hmd)].rotation;
    ApplyDragDelta(hmdDrag, frame, 100.0f, 0.0f, ManipulationModifier_Ctrl | ManipulationModifier_Shift);
    EXPECT_TRUE(std::fabs(frame.devices[DeviceSlot(DeviceIndex::Hmd)].rotation.z - beforeRoll.z) > 0.0f);

    // The head allows vertical (Y) translation with the shift gesture; X/Z stay
    // locked and the rotation is left untouched.
    frame = BuildResetTPose(MakeNeutralFrame());
    const Vec3 hmdYStart = frame.devices[DeviceSlot(DeviceIndex::Hmd)].position;
    const Quat hmdRotStart = frame.devices[DeviceSlot(DeviceIndex::Hmd)].rotation;
    hmdDrag = BeginDrag(frame, DeviceIndex::Hmd);
    ApplyDragDelta(hmdDrag, frame, 25.0f, -50.0f, ManipulationModifier_Shift);
    EXPECT_NEAR(frame.devices[DeviceSlot(DeviceIndex::Hmd)].position.x, hmdYStart.x, 0.0001f);
    EXPECT_NEAR(frame.devices[DeviceSlot(DeviceIndex::Hmd)].position.z, hmdYStart.z, 0.0001f);
    EXPECT_TRUE(frame.devices[DeviceSlot(DeviceIndex::Hmd)].position.y > hmdYStart.y);
    EXPECT_SAME_ROTATION(frame.devices[DeviceSlot(DeviceIndex::Hmd)].rotation, hmdRotStart);

    // Head Y is capped at the shared maximum.
    hmdDrag = BeginDrag(frame, DeviceIndex::Hmd);
    ApplyDragDelta(hmdDrag, frame, 0.0f, -100000.0f, ManipulationModifier_Shift);
    EXPECT_NEAR(frame.devices[DeviceSlot(DeviceIndex::Hmd)].position.y, kMaxDeviceY, 0.0001f);
    EXPECT_TRUE(frame.devices[DeviceSlot(DeviceIndex::Hmd)].y_clamped);

    // Reversing at the cap descends immediately.
    ApplyDragDelta(hmdDrag, frame, 0.0f, 20.0f, ManipulationModifier_Shift);
    EXPECT_NEAR(frame.devices[DeviceSlot(DeviceIndex::Hmd)].position.y,
                kMaxDeviceY - 20.0f * kTranslationMetersPerCount, 0.0001f);
    EXPECT_FALSE(frame.devices[DeviceSlot(DeviceIndex::Hmd)].y_clamped);

    frame = BuildResetTPose(MakeNeutralFrame());
    DragSnapshot leftDrag = BeginDrag(frame, DeviceIndex::LeftController);
    const Vec3 leftStart = frame.devices[DeviceSlot(DeviceIndex::LeftController)].position;
    ApplyDragDelta(leftDrag, frame, 10.0f, -10.0f, ManipulationModifier_None);
    EXPECT_TRUE(frame.devices[DeviceSlot(DeviceIndex::LeftController)].position.x > leftStart.x);
    EXPECT_TRUE(frame.devices[DeviceSlot(DeviceIndex::LeftController)].position.y > leftStart.y);

    leftDrag = BeginDrag(frame, DeviceIndex::LeftController);
    const float zStart = frame.devices[DeviceSlot(DeviceIndex::LeftController)].position.z;
    ApplyDragDelta(leftDrag, frame, 0.0f, -10.0f, ManipulationModifier_Shift);
    EXPECT_TRUE(frame.devices[DeviceSlot(DeviceIndex::LeftController)].position.z < zStart);

    leftDrag = BeginDrag(frame, DeviceIndex::LeftController);
    const Quat rotStart = frame.devices[DeviceSlot(DeviceIndex::LeftController)].rotation;
    ApplyDragDelta(leftDrag, frame, 10.0f, -10.0f, ManipulationModifier_Ctrl);
    EXPECT_TRUE(std::fabs(frame.devices[DeviceSlot(DeviceIndex::LeftController)].rotation.y - rotStart.y) > 0.0f);

    // Rotation respects the head yaw basis like translation: a pure pitch produces
    // the same head-local delta whether or not the body is yawed. (Driving the
    // delta about world axes would make this differ once the head is turned.)
    auto headLocalPitchDelta = [](float hmdYawDeg) {
        FrameState f = BuildResetTPose(MakeNeutralFrame());
        f.devices[DeviceSlot(DeviceIndex::Hmd)].rotation = FromYaw(DegToRad(hmdYawDeg));
        DragSnapshot d = BeginDrag(f, DeviceIndex::LeftController);
        const Quat start = f.devices[DeviceSlot(DeviceIndex::LeftController)].rotation;
        ApplyDragDelta(d, f, 0.0f, -40.0f, ManipulationModifier_Ctrl);
        const Quat basis = FromYaw(d.hmdYawBasis);
        const Quat worldDelta = Normalized(Multiply(
            f.devices[DeviceSlot(DeviceIndex::LeftController)].rotation, Conjugate(start)));
        return Canonical(Normalized(Multiply(Multiply(Conjugate(basis), worldDelta), basis)));
    };
    EXPECT_SAME_ROTATION(headLocalPitchDelta(0.0f), headLocalPitchDelta(90.0f));

    // The Global frame ignores HMD heading: the world-space rotation delta is
    // identical regardless of yaw (unlike the Hmd frame above).
    auto worldPitchDelta = [](float hmdYawDeg) {
        FrameState f = BuildResetTPose(MakeNeutralFrame());
        f.devices[DeviceSlot(DeviceIndex::Hmd)].rotation = FromYaw(DegToRad(hmdYawDeg));
        DragSnapshot d = BeginDrag(f, DeviceIndex::LeftController);
        const Quat start = f.devices[DeviceSlot(DeviceIndex::LeftController)].rotation;
        ApplyDragDelta(d, f, 0.0f, -40.0f, ManipulationModifier_Ctrl, ManipulationFrame::Global);
        return Canonical(Normalized(Multiply(
            f.devices[DeviceSlot(DeviceIndex::LeftController)].rotation, Conjugate(start))));
    };
    EXPECT_SAME_ROTATION(worldPitchDelta(0.0f), worldPitchDelta(90.0f));

    // The frame governs translation too: a horizontal drag in Global mode moves
    // along world X regardless of HMD yaw, whereas the Hmd frame follows the head.
    auto horizontalMove = [](float hmdYawDeg, ManipulationFrame mf) {
        FrameState f = BuildResetTPose(MakeNeutralFrame());
        f.devices[DeviceSlot(DeviceIndex::Hmd)].rotation = FromYaw(DegToRad(hmdYawDeg));
        DragSnapshot d = BeginDrag(f, DeviceIndex::LeftController);
        const Vec3 s = f.devices[DeviceSlot(DeviceIndex::LeftController)].position;
        ApplyDragDelta(d, f, 30.0f, 0.0f, ManipulationModifier_None, mf);
        const Vec3 e = f.devices[DeviceSlot(DeviceIndex::LeftController)].position;
        return Vec3{e.x - s.x, e.y - s.y, e.z - s.z};
    };
    const Vec3 globalMove0 = horizontalMove(0.0f, ManipulationFrame::Global);
    const Vec3 globalMove90 = horizontalMove(90.0f, ManipulationFrame::Global);
    EXPECT_NEAR(globalMove90.x, globalMove0.x, 0.0001f);
    EXPECT_NEAR(globalMove90.z, globalMove0.z, 0.0001f);
    EXPECT_TRUE(std::fabs(globalMove0.x) > 0.0f);
    // The Hmd frame instead rotates the drag axis with the head, so a 90 deg yaw
    // turns the same X drag into world-Z motion.
    const Vec3 hmdMove90 = horizontalMove(90.0f, ManipulationFrame::Hmd);
    EXPECT_TRUE(std::fabs(hmdMove90.z) > std::fabs(hmdMove90.x));

    frame.devices[DeviceSlot(DeviceIndex::LeftController)].position.y = 1.99f;
    leftDrag = BeginDrag(frame, DeviceIndex::LeftController);
    ApplyDragDelta(leftDrag, frame, 50.0f, -100000.0f, ManipulationModifier_None);
    EXPECT_NEAR(frame.devices[DeviceSlot(DeviceIndex::LeftController)].position.y, kMaxDeviceY, 0.0001f);

    frame = BuildResetTPose(MakeNeutralFrame());
    leftDrag = BeginDrag(frame, DeviceIndex::LeftController);
    ApplyDragDelta(leftDrag, frame, 20.0f, -10.0f, ManipulationModifier_None);
    ApplySymmetricMirror(leftDrag, frame, DeviceIndex::RightController);
    const Vec3 leftLocal = frame.devices[DeviceSlot(DeviceIndex::LeftController)].position;
    const Vec3 rightLocal = frame.devices[DeviceSlot(DeviceIndex::RightController)].position;
    EXPECT_NEAR(rightLocal.x, -leftLocal.x, 0.0001f);
    EXPECT_NEAR(rightLocal.y, leftLocal.y, 0.0001f);
    EXPECT_NEAR(rightLocal.z, leftLocal.z, 0.0001f);

    frame = BuildResetTPose(MakeNeutralFrame());
    frame.devices[DeviceSlot(DeviceIndex::Hmd)].rotation = FromYaw(DegToRad(90.0f));
    frame = BuildResetTPose(frame);
    leftDrag = BeginDrag(frame, DeviceIndex::LeftController);
    ApplyDragDelta(leftDrag, frame, 20.0f, -10.0f, ManipulationModifier_None);
    ApplySymmetricMirror(leftDrag, frame, DeviceIndex::RightController);
    const Quat yawBasis = FromYaw(leftDrag.hmdYawBasis);
    const Vec3 hmdOrigin = frame.devices[DeviceSlot(DeviceIndex::Hmd)].position;
    const Vec3 yawLeftLocal = Rotate(Conjugate(yawBasis), {
        frame.devices[DeviceSlot(DeviceIndex::LeftController)].position.x - hmdOrigin.x,
        frame.devices[DeviceSlot(DeviceIndex::LeftController)].position.y - hmdOrigin.y,
        frame.devices[DeviceSlot(DeviceIndex::LeftController)].position.z - hmdOrigin.z,
    });
    const Vec3 yawRightLocal = Rotate(Conjugate(yawBasis), {
        frame.devices[DeviceSlot(DeviceIndex::RightController)].position.x - hmdOrigin.x,
        frame.devices[DeviceSlot(DeviceIndex::RightController)].position.y - hmdOrigin.y,
        frame.devices[DeviceSlot(DeviceIndex::RightController)].position.z - hmdOrigin.z,
    });
    EXPECT_NEAR(yawRightLocal.x, -yawLeftLocal.x, 0.0001f);
    EXPECT_NEAR(yawRightLocal.y, yawLeftLocal.y, 0.0001f);
    EXPECT_NEAR(yawRightLocal.z, yawLeftLocal.z, 0.0001f);

    frame = MakeNeutralFrame();
    frame.devices[DeviceSlot(DeviceIndex::Hmd)].position = {2.0f, 1.5f, -1.0f};
    frame.devices[DeviceSlot(DeviceIndex::Hmd)].rotation = FromYaw(DegToRad(90.0f));
    frame.devices[DeviceSlot(DeviceIndex::LeftController)].position =
        Add(frame.devices[DeviceSlot(DeviceIndex::Hmd)].position, Rotate(FromYaw(DegToRad(90.0f)), {0.30f, 0.10f, 0.40f}));
    frame.devices[DeviceSlot(DeviceIndex::LeftController)].rotation =
        Normalized(Multiply(FromYaw(DegToRad(90.0f)), FromAxisAngle({1.0f, 0.0f, 0.0f}, DegToRad(20.0f))));
    leftDrag = BeginDrag(frame, DeviceIndex::LeftController);
    ApplySymmetricMirror(leftDrag, frame, DeviceIndex::RightController, ManipulationFrame::Global);
    const Vec3 globalOrigin = frame.devices[DeviceSlot(DeviceIndex::Hmd)].position;
    const Vec3 globalLeft = frame.devices[DeviceSlot(DeviceIndex::LeftController)].position;
    const Vec3 globalRight = frame.devices[DeviceSlot(DeviceIndex::RightController)].position;
    EXPECT_NEAR(globalRight.x - globalOrigin.x, -(globalLeft.x - globalOrigin.x), 0.0001f);
    EXPECT_NEAR(globalRight.y - globalOrigin.y, globalLeft.y - globalOrigin.y, 0.0001f);
    EXPECT_NEAR(globalRight.z - globalOrigin.z, globalLeft.z - globalOrigin.z, 0.0001f);
    EXPECT_MIRRORED_IN_BASIS(frame, 0.0f, DeviceIndex::LeftController, DeviceIndex::RightController);

    frame = BuildResetTPose(MakeNeutralFrame());
    leftDrag = BeginDrag(frame, DeviceIndex::LeftFoot);
    ApplyDragDelta(leftDrag, frame, 0.0f, -20.0f, ManipulationModifier_Shift);
    ApplySymmetricMirror(leftDrag, frame, DeviceIndex::RightFoot);
    EXPECT_NEAR(frame.devices[DeviceSlot(DeviceIndex::RightFoot)].position.x, -frame.devices[DeviceSlot(DeviceIndex::LeftFoot)].position.x, 0.0001f);
    EXPECT_NEAR(frame.devices[DeviceSlot(DeviceIndex::RightFoot)].position.z, frame.devices[DeviceSlot(DeviceIndex::LeftFoot)].position.z, 0.0001f);

    frame = BuildResetTPose(MakeNeutralFrame());
    frame.devices[DeviceSlot(DeviceIndex::Hmd)].rotation = FromYaw(DegToRad(90.0f));
    frame = BuildResetTPose(frame);
    const Quat rightStartRotation = frame.devices[DeviceSlot(DeviceIndex::RightController)].rotation;
    leftDrag = BeginDrag(frame, DeviceIndex::LeftController);
    ApplySymmetricMirror(leftDrag, frame, DeviceIndex::RightController);
    EXPECT_SAME_ROTATION(frame.devices[DeviceSlot(DeviceIndex::RightController)].rotation, rightStartRotation);

    leftDrag = BeginDrag(frame, DeviceIndex::LeftController);
    ApplyDragDelta(leftDrag, frame, 0.0f, -30.0f, ManipulationModifier_Ctrl);
    ApplySymmetricMirror(leftDrag, frame, DeviceIndex::RightController);
    EXPECT_MIRRORED_IN_YAW_BASIS(frame, leftDrag, DeviceIndex::LeftController, DeviceIndex::RightController);

    leftDrag = BeginDrag(frame, DeviceIndex::LeftController);
    ApplyDragDelta(leftDrag, frame, 30.0f, 0.0f, ManipulationModifier_Ctrl);
    ApplySymmetricMirror(leftDrag, frame, DeviceIndex::RightController);
    EXPECT_MIRRORED_IN_YAW_BASIS(frame, leftDrag, DeviceIndex::LeftController, DeviceIndex::RightController);

    leftDrag = BeginDrag(frame, DeviceIndex::LeftController);
    ApplyDragDelta(leftDrag, frame, 30.0f, 0.0f, ManipulationModifier_Ctrl | ManipulationModifier_Shift);
    ApplySymmetricMirror(leftDrag, frame, DeviceIndex::RightController);
    EXPECT_MIRRORED_IN_YAW_BASIS(frame, leftDrag, DeviceIndex::LeftController, DeviceIndex::RightController);

    // Whole-rig manipulation (empty-area drags). Ctrl (MMB) yaws every device
    // about the HMD's start position: offsets keep their length and height, the
    // rig actually turns, and orientations follow.
    frame = BuildResetTPose(MakeNeutralFrame());
    const FrameState rigStart = frame;
    const Vec3 rigPivot = rigStart.devices[DeviceSlot(DeviceIndex::Hmd)].position;
    DragSnapshot rigDrag = BeginDrag(frame, DeviceIndex::Hmd);
    ApplyRigDragDelta(rigDrag, frame, 300.0f, 0.0f, ManipulationModifier_Ctrl);
    const auto offsetFrom = [](const Vec3& p, const Vec3& o) { return Vec3{p.x - o.x, p.y - o.y, p.z - o.z}; };
    const auto lengthOf = [](const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); };
    for (std::size_t slot = 0; slot < frame.devices.size(); ++slot) {
        const Vec3 before = offsetFrom(rigStart.devices[slot].position, rigPivot);
        const Vec3 after = offsetFrom(frame.devices[slot].position, rigPivot);
        EXPECT_NEAR(lengthOf(after), lengthOf(before), 0.0001f);
        EXPECT_NEAR(after.y, before.y, 0.0001f);
    }
    EXPECT_TRUE(std::fabs(frame.devices[DeviceSlot(DeviceIndex::LeftController)].position.z -
                          rigStart.devices[DeviceSlot(DeviceIndex::LeftController)].position.z) > 0.001f);
    EXPECT_TRUE(std::fabs(frame.devices[DeviceSlot(DeviceIndex::Hmd)].rotation.y -
                          rigStart.devices[DeviceSlot(DeviceIndex::Hmd)].rotation.y) > 0.0f);

    // Ctrl+Shift (MMB+RMB) rolls the rig: the HMD rolls in place and a laterally
    // offset device (the left controller in T-pose) swings vertically.
    frame = BuildResetTPose(MakeNeutralFrame());
    rigDrag = BeginDrag(frame, DeviceIndex::Hmd);
    ApplyRigDragDelta(rigDrag, frame, 300.0f, 0.0f, ManipulationModifier_Ctrl | ManipulationModifier_Shift);
    EXPECT_TRUE(std::fabs(frame.devices[DeviceSlot(DeviceIndex::Hmd)].rotation.z -
                          rigStart.devices[DeviceSlot(DeviceIndex::Hmd)].rotation.z) > 0.0f);
    EXPECT_TRUE(std::fabs(frame.devices[DeviceSlot(DeviceIndex::LeftController)].position.y -
                          rigStart.devices[DeviceSlot(DeviceIndex::LeftController)].position.y) > 0.001f);

    // Shift (RMB) moves the whole rig vertically, preserving shape and rotations.
    frame = BuildResetTPose(MakeNeutralFrame());
    rigDrag = BeginDrag(frame, DeviceIndex::Hmd);
    ApplyRigDragDelta(rigDrag, frame, 0.0f, -100.0f, ManipulationModifier_Shift);
    const float rigRise = 100.0f * kTranslationMetersPerCount;
    for (std::size_t slot = 0; slot < frame.devices.size(); ++slot) {
        EXPECT_NEAR(frame.devices[slot].position.x, rigStart.devices[slot].position.x, 0.0001f);
        EXPECT_NEAR(frame.devices[slot].position.y, rigStart.devices[slot].position.y + rigRise, 0.0001f);
        EXPECT_NEAR(frame.devices[slot].position.z, rigStart.devices[slot].position.z, 0.0001f);
        EXPECT_SAME_ROTATION(frame.devices[slot].rotation, rigStart.devices[slot].rotation);
    }

    // The rig stops at the shared maximum while retaining its shape.
    const auto highestY = [](const FrameState& f) {
        float highest = f.devices[0].position.y;
        for (const DeviceState& device : f.devices) {
            highest = std::max(highest, device.position.y);
        }
        return highest;
    };
    const auto lowestY = [](const FrameState& f) {
        float lowest = f.devices[0].position.y;
        for (const DeviceState& device : f.devices) {
            lowest = std::min(lowest, device.position.y);
        }
        return lowest;
    };
    const float hmdToHipStart = frame.devices[DeviceSlot(DeviceIndex::Hmd)].position.y -
                                frame.devices[DeviceSlot(DeviceIndex::Hip)].position.y;
    rigDrag = BeginDrag(frame, DeviceIndex::Hmd);
    ApplyRigDragDelta(rigDrag, frame, 0.0f, -100000.0f, ManipulationModifier_Shift);
    EXPECT_NEAR(highestY(frame), kMaxDeviceY, 0.0001f);
    EXPECT_NEAR(frame.devices[DeviceSlot(DeviceIndex::Hmd)].position.y -
                    frame.devices[DeviceSlot(DeviceIndex::Hip)].position.y,
                hmdToHipStart, 0.0001f);
    ApplyRigDragDelta(rigDrag, frame, 0.0f, 20.0f, ManipulationModifier_Shift);
    EXPECT_NEAR(highestY(frame), kMaxDeviceY - 20.0f * kTranslationMetersPerCount, 0.0001f);

    // Direct HMD manipulation stops at the ground plane and re-anchors at the
    // bound, so a reversed drag ascends immediately.
    frame = BuildResetTPose(MakeNeutralFrame());
    hmdDrag = BeginDrag(frame, DeviceIndex::Hmd);
    ApplyDragDelta(hmdDrag, frame, 0.0f, 100000.0f, ManipulationModifier_Shift);
    EXPECT_NEAR(frame.devices[DeviceSlot(DeviceIndex::Hmd)].position.y, kMinHmdY, 0.0001f);
    ApplyDragDelta(hmdDrag, frame, 0.0f, -20.0f, ManipulationModifier_Shift);
    EXPECT_NEAR(frame.devices[DeviceSlot(DeviceIndex::Hmd)].position.y,
                kMinHmdY + 20.0f * kTranslationMetersPerCount, 0.0001f);

    // A foot has no floor at the ground plane, so the same drag passes straight
    // through zero and only stops at the shared position range.
    frame = BuildResetTPose(MakeNeutralFrame());
    leftDrag = BeginDrag(frame, DeviceIndex::LeftFoot);
    ApplyDragDelta(leftDrag, frame, 0.0f, 1000.0f, ManipulationModifier_None);
    EXPECT_TRUE(frame.devices[DeviceSlot(DeviceIndex::LeftFoot)].position.y < 0.0f);
    ApplyDragDelta(leftDrag, frame, 0.0f, 1000000.0f, ManipulationModifier_None);
    EXPECT_NEAR(frame.devices[DeviceSlot(DeviceIndex::LeftFoot)].position.y, kMinTrackerY, 0.0001f);
    ApplyDragDelta(leftDrag, frame, 0.0f, -20.0f, ManipulationModifier_None);
    EXPECT_NEAR(frame.devices[DeviceSlot(DeviceIndex::LeftFoot)].position.y,
                kMinTrackerY + 20.0f * kTranslationMetersPerCount, 0.0001f);

    // The rig's vertical move preserves its shape and re-anchors at the floor so
    // a reversed drag rises immediately. The HMD is the only device with a floor
    // at the ground plane, so it is what stops the rig's descent.
    frame = BuildResetTPose(MakeNeutralFrame());
    const float rigHipOffset = frame.devices[DeviceSlot(DeviceIndex::Hip)].position.y -
                               frame.devices[DeviceSlot(DeviceIndex::Hmd)].position.y;
    rigDrag = BeginDrag(frame, DeviceIndex::Hmd);
    ApplyRigDragDelta(rigDrag, frame, 0.0f, 100000.0f, ManipulationModifier_Shift);
    EXPECT_NEAR(frame.devices[DeviceSlot(DeviceIndex::Hmd)].position.y, kMinHmdY, 0.0001f);
    EXPECT_TRUE(lowestY(frame) < 0.0f);
    EXPECT_NEAR(frame.devices[DeviceSlot(DeviceIndex::Hip)].position.y -
                    frame.devices[DeviceSlot(DeviceIndex::Hmd)].position.y,
                rigHipOffset, 0.0001f);
    ApplyRigDragDelta(rigDrag, frame, 0.0f, -20.0f, ManipulationModifier_Shift);
    EXPECT_NEAR(frame.devices[DeviceSlot(DeviceIndex::Hmd)].position.y,
                kMinHmdY + 20.0f * kTranslationMetersPerCount, 0.0001f);

    DeviceIndex mirroredDevice = DeviceIndex::Hmd;
    EXPECT_TRUE(MirroredDeviceFor(DeviceIndex::LeftController, mirroredDevice));
    EXPECT_TRUE(mirroredDevice == DeviceIndex::RightController);
    EXPECT_FALSE(MirroredDeviceFor(DeviceIndex::Hip, mirroredDevice));
    // Mirroring is symmetric across the controller pair and across the feet.
    EXPECT_TRUE(MirroredDeviceFor(DeviceIndex::RightController, mirroredDevice));
    EXPECT_TRUE(mirroredDevice == DeviceIndex::LeftController);
    EXPECT_TRUE(MirroredDeviceFor(DeviceIndex::RightFoot, mirroredDevice));
    EXPECT_TRUE(mirroredDevice == DeviceIndex::LeftFoot);
    EXPECT_FALSE(MirroredDeviceFor(DeviceIndex::Hmd, mirroredDevice));
}

} // namespace anyadance::tests
