#include "test_framework.h"
#include "tests.h"

#include "core/constants.h"
#include "core/math3d.h"

#include <cmath>
#include <limits>

namespace anyadance::tests {

void TestMath3d() {
    // LengthSquared sums the four components squared; the identity quaternion has
    // unit length.
    EXPECT_NEAR(LengthSquared(Quat{}), 1.0f, 0.0001f);
    EXPECT_NEAR(LengthSquared(Quat{0.0f, 0.0f, 0.0f, 2.0f}), 4.0f, 0.0001f);

    // NormalizeIfAcceptable scales near-unit quaternions to unit length and
    // accepts the same 0.5..1.5 squared-length window the protocol validates.
    Quat acceptable{0.0f, 0.0f, 0.0f, 1.2f};
    EXPECT_TRUE(NormalizeIfAcceptable(acceptable));
    EXPECT_NEAR(LengthSquared(acceptable), 1.0f, 0.0001f);
    Quat tooShort{0.0f, 0.0f, 0.0f, 0.1f};  // lenSq 0.01 < 0.5
    EXPECT_FALSE(NormalizeIfAcceptable(tooShort));
    Quat tooLong{0.0f, 0.0f, 0.0f, 2.0f};  // lenSq 4.0 > 1.5
    EXPECT_FALSE(NormalizeIfAcceptable(tooLong));
    Quat nonFinite{0.0f, 0.0f, 0.0f, std::numeric_limits<float>::infinity()};
    EXPECT_FALSE(NormalizeIfAcceptable(nonFinite));

    // Normalized accepts any positive length and falls back to identity for a
    // degenerate (zero / non-finite) quaternion.
    EXPECT_NEAR(LengthSquared(Normalized(Quat{0.0f, 0.0f, 0.0f, 5.0f})), 1.0f, 0.0001f);
    EXPECT_SAME_ROTATION(Normalized(Quat{0.0f, 0.0f, 0.0f, 0.0f}), Quat{});

    // A quaternion times its conjugate is identity; conjugate flips the vector
    // part only.
    const Quat r = FromAxisAngle({0.0f, 1.0f, 0.0f}, DegToRad(37.0f));
    EXPECT_SAME_ROTATION(Multiply(r, Conjugate(r)), Quat{});
    EXPECT_SAME_ROTATION(Multiply(Conjugate(r), r), Quat{});

    // Multiply by identity is a no-op on either side.
    EXPECT_SAME_ROTATION(Multiply(r, Quat{}), r);
    EXPECT_SAME_ROTATION(Multiply(Quat{}, r), r);

    // FromAxisAngle normalizes its axis and a degenerate axis yields identity.
    EXPECT_SAME_ROTATION(FromAxisAngle({0.0f, 5.0f, 0.0f}, DegToRad(90.0f)),
                         FromAxisAngle({0.0f, 1.0f, 0.0f}, DegToRad(90.0f)));
    EXPECT_SAME_ROTATION(FromAxisAngle({0.0f, 0.0f, 0.0f}, DegToRad(90.0f)), Quat{});

    // Rotate applies the quaternion: a +90 deg yaw turns forward (+Z) toward +X
    // in this right-handed, +Y-up convention.
    const Vec3 forward = Rotate(FromYaw(DegToRad(90.0f)), {0.0f, 0.0f, 1.0f});
    EXPECT_VEC3_NEAR(forward, (Vec3{1.0f, 0.0f, 0.0f}));
    // Rotation about an axis leaves a vector on that axis unchanged.
    EXPECT_VEC3_NEAR(Rotate(FromYaw(DegToRad(123.0f)), {0.0f, 1.0f, 0.0f}), (Vec3{0.0f, 1.0f, 0.0f}));

    // FromYaw and YawFromQuaternion round-trip across the signed range.
    for (float deg : {-170.0f, -90.0f, -10.0f, 0.0f, 45.0f, 90.0f, 160.0f}) {
        EXPECT_NEAR(YawFromQuaternion(FromYaw(DegToRad(deg))), DegToRad(deg), 0.0001f);
    }
    // A pure pitch has zero yaw.
    EXPECT_NEAR(YawFromQuaternion(FromAxisAngle({1.0f, 0.0f, 0.0f}, DegToRad(30.0f))), 0.0f, 0.0001f);

    // Add / Scale are componentwise.
    EXPECT_VEC3_NEAR(Add(Vec3{1.0f, -2.0f, 3.0f}, Vec3{0.5f, 0.5f, -1.0f}), (Vec3{1.5f, -1.5f, 2.0f}));
    EXPECT_VEC3_NEAR(Scale(Vec3{1.0f, -2.0f, 3.0f}, 2.0f), (Vec3{2.0f, -4.0f, 6.0f}));

    // ClampFloat saturates to the bounds and passes interior values through.
    EXPECT_NEAR(ClampFloat(5.0f, 0.0f, 1.0f), 1.0f, 0.0001f);
    EXPECT_NEAR(ClampFloat(-5.0f, 0.0f, 1.0f), 0.0f, 0.0001f);
    EXPECT_NEAR(ClampFloat(0.3f, 0.0f, 1.0f), 0.3f, 0.0001f);

    // The ceiling is shared, but only the HMD has a floor at the ground plane.
    EXPECT_NEAR(ClampDeviceY(DeviceIndex::Hmd, 30.0f), kMaxDeviceY, 0.0001f);
    EXPECT_NEAR(ClampDeviceY(DeviceIndex::LeftFoot, 30.0f), kMaxDeviceY, 0.0001f);
    EXPECT_NEAR(ClampDeviceY(DeviceIndex::Hmd, -1.0f), kMinHmdY, 0.0001f);
    EXPECT_NEAR(ClampDeviceY(DeviceIndex::Hmd, 0.5f), 0.5f, 0.0001f);

    // Every non-HMD device passes negative Y through down to the shared range.
    for (const DeviceInfo& device : kDevices) {
        if (device.index == DeviceIndex::Hmd) {
            continue;
        }
        EXPECT_NEAR(ClampDeviceY(device.index, -1.0f), -1.0f, 0.0001f);
        EXPECT_NEAR(ClampDeviceY(device.index, -40.0f), kMinTrackerY, 0.0001f);
    }


    // DegToRad / RadToDeg are inverses.
    EXPECT_NEAR(RadToDeg(DegToRad(72.0f)), 72.0f, 0.0001f);
}

} // namespace anyadance::tests
