#include "test_framework.h"
#include "tests.h"

#include "core/constants.h"
#include "core/frame_state.h"

namespace anyadance::tests {

void TestFrameState() {
    // The neutral frame stands every device up: all six are valid, connected, and
    // unclamped, with the HMD at the rig's reset height and controllers neutral.
    FrameState frame = MakeNeutralFrame();
    for (const DeviceState& device : frame.devices) {
        EXPECT_TRUE(device.valid);
        EXPECT_TRUE(device.connected);
        EXPECT_FALSE(device.y_clamped);
    }
    EXPECT_NEAR(frame.devices[DeviceSlot(DeviceIndex::Hmd)].position.y, kResetHmdY, 0.0001f);
    for (const ControllerState& controller : frame.controllers) {
        EXPECT_FALSE(controller.trigger_click);
        EXPECT_FALSE(controller.grip_click);
        EXPECT_NEAR(controller.joystick_x, 0.0f, 0.0001f);
    }

    // NeutralizeControllerInputs clears every controller input but leaves the
    // device poses (the streamed body) untouched.
    frame.controllers[0].trigger_click = true;
    frame.controllers[0].trigger_value = 1.0f;
    frame.controllers[1].grip_click = true;
    frame.controllers[1].joystick_x = 0.8f;
    const Vec3 hmdBefore = frame.devices[DeviceSlot(DeviceIndex::Hmd)].position;
    NeutralizeControllerInputs(frame);
    EXPECT_FALSE(frame.controllers[0].trigger_click);
    EXPECT_NEAR(frame.controllers[0].trigger_value, 0.0f, 0.0001f);
    EXPECT_FALSE(frame.controllers[1].grip_click);
    EXPECT_NEAR(frame.controllers[1].joystick_x, 0.0f, 0.0001f);
    EXPECT_VEC3_NEAR(frame.devices[DeviceSlot(DeviceIndex::Hmd)].position, hmdBefore);
}

void TestSafety() {
    FrameState frame = MakeNeutralFrame();
    frame.devices[DeviceSlot(DeviceIndex::Hmd)].position = {1.0f, 30.0f, -3.0f};
    EXPECT_TRUE(ClampFrameY(frame));
    const DeviceState& hmd = frame.devices[DeviceSlot(DeviceIndex::Hmd)];
    EXPECT_NEAR(hmd.position.x, 1.0f, 0.0001f);
    EXPECT_NEAR(hmd.position.y, kMaxDeviceY, 0.0001f);
    EXPECT_NEAR(hmd.position.z, -3.0f, 0.0001f);
    EXPECT_TRUE(hmd.y_clamped);

    for (DeviceState& device : frame.devices) {
        device.position.y = 30.0f;
    }
    EXPECT_TRUE(ClampFrameY(frame));
    for (const DeviceState& device : frame.devices) {
        EXPECT_NEAR(device.position.y, kMaxDeviceY, 0.0001f);
    }

    // Frames already in range report no clamp and clear the flags.
    FrameState low = MakeNeutralFrame();
    EXPECT_FALSE(ClampFrameY(low));
    for (const DeviceState& device : low.devices) {
        EXPECT_FALSE(device.y_clamped);
    }
}

} // namespace anyadance::tests
