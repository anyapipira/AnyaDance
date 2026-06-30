#include "test_framework.h"
#include "tests.h"

#include "core/constants.h"
#include "core/frame_state.h"
#include "core/math3d.h"
#include "core/tpose.h"

namespace anyadance::tests {

void TestTPose() {
    FrameState frame = MakeNeutralFrame();
    frame.devices[DeviceSlot(DeviceIndex::Hmd)].position = {2.0f, 1.8f, -1.0f};
    frame.devices[DeviceSlot(DeviceIndex::Hmd)].rotation = FromYaw(DegToRad(90.0f));
    FrameState reset = BuildResetTPose(frame);
    const DeviceState& hmd = reset.devices[DeviceSlot(DeviceIndex::Hmd)];
    // Reset anchors the rig at tracking-space origin, ignoring the input HMD X/Z.
    EXPECT_NEAR(hmd.position.x, 0.0f, 0.0001f);
    EXPECT_NEAR(hmd.position.y, 1.50f, 0.0001f);
    EXPECT_NEAR(hmd.position.z, 0.0f, 0.0001f);
    // Reset zeroes the HMD yaw so the rig faces the canonical forward, regardless
    // of the input heading (the in-game yaw is driven by locomotion).
    EXPECT_SAME_ROTATION(hmd.rotation, FromYaw(0.0f));

    // Reset uprights the head and zeroes its yaw: a tilted/rolled/yawed HMD resets
    // to identity orientation, and the body follows.
    FrameState tilted = MakeNeutralFrame();
    const Quat tiltedRotation = Multiply(
        Multiply(FromYaw(DegToRad(40.0f)), FromAxisAngle({1.0f, 0.0f, 0.0f}, DegToRad(25.0f))),
        FromAxisAngle({0.0f, 0.0f, 1.0f}, DegToRad(30.0f)));
    tilted.devices[DeviceSlot(DeviceIndex::Hmd)].rotation = tiltedRotation;
    FrameState tiltedReset = BuildResetTPose(tilted);
    const Quat expectedYaw = FromYaw(0.0f);
    EXPECT_SAME_ROTATION(tiltedReset.devices[DeviceSlot(DeviceIndex::Hmd)].rotation, expectedYaw);
    EXPECT_SAME_ROTATION(tiltedReset.devices[DeviceSlot(DeviceIndex::Hip)].rotation, expectedYaw);

    const DeviceState& left = reset.devices[DeviceSlot(DeviceIndex::LeftController)];
    const DeviceState& right = reset.devices[DeviceSlot(DeviceIndex::RightController)];
    const DeviceState& hip = reset.devices[DeviceSlot(DeviceIndex::Hip)];
    const DeviceState& leftFoot = reset.devices[DeviceSlot(DeviceIndex::LeftFoot)];
    const DeviceState& rightFoot = reset.devices[DeviceSlot(DeviceIndex::RightFoot)];
    // With the heading zeroed and the rig anchored at origin, offsets land in body
    // space directly (HMD at x=0, z=0).
    EXPECT_VEC3_NEAR(left.position, (Vec3{-0.62f, 1.33f, -0.10f}));
    EXPECT_VEC3_NEAR(right.position, (Vec3{0.62f, 1.33f, -0.10f}));
    EXPECT_VEC3_NEAR(hip.position, (Vec3{0.0f, 1.07f, -0.05f}));
    EXPECT_VEC3_NEAR(leftFoot.position, (Vec3{-0.09f, 0.26f, 0.10f}));
    EXPECT_VEC3_NEAR(rightFoot.position, (Vec3{0.09f, 0.26f, 0.10f}));
    EXPECT_NEAR(LengthSquared(left.rotation), 1.0f, 0.0001f);
    EXPECT_NEAR(LengthSquared(right.rotation), 1.0f, 0.0001f);
    EXPECT_SAME_ROTATION(left.rotation, kLeftControllerCanonicalRotation);

    FrameState neutralReset = BuildResetTPose(MakeNeutralFrame());
    EXPECT_VEC3_NEAR(neutralReset.devices[DeviceSlot(DeviceIndex::LeftController)].position, (Vec3{-0.62f, 1.33f, -0.10f}));
    EXPECT_VEC3_NEAR(neutralReset.devices[DeviceSlot(DeviceIndex::RightController)].position, (Vec3{0.62f, 1.33f, -0.10f}));
    EXPECT_VEC3_NEAR(neutralReset.devices[DeviceSlot(DeviceIndex::Hip)].position, (Vec3{0.0f, 1.07f, -0.05f}));
    EXPECT_VEC3_NEAR(neutralReset.devices[DeviceSlot(DeviceIndex::LeftFoot)].position, (Vec3{-0.09f, 0.26f, 0.10f}));
    EXPECT_VEC3_NEAR(neutralReset.devices[DeviceSlot(DeviceIndex::RightFoot)].position, (Vec3{0.09f, 0.26f, 0.10f}));
    EXPECT_FALSE(reset.controllers[0].trigger_click);
    EXPECT_FALSE(reset.controllers[1].a_click);
}

} // namespace anyadance::tests
