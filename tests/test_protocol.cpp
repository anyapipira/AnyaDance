#include "test_framework.h"
#include "tests.h"

#include "core/constants.h"
#include "core/frame_state.h"
#include "core/json.h"
#include "core/protocol.h"
#include "core/tpose.h"

#include <string>

namespace anyadance::tests {
namespace {

std::string ValidPacket() {
    FrameState frame = BuildResetTPose(MakeNeutralFrame());
    frame.controllers[0].trigger_click = true;
    frame.controllers[0].trigger_value = 3.0f;
    frame.controllers[0].joystick_x = 2.0f;
    frame.controllers[0].joystick_y = -2.0f;
    return SerializeFrame(frame);
}

} // namespace

void TestProtocol() {
    ParsedFrame parsed;
    const std::string valid = ValidPacket();
    EXPECT_TRUE(ParsePoseFrame(valid, parsed));
    EXPECT_TRUE(parsed.present[DeviceSlot(DeviceIndex::Hmd)]);
    EXPECT_TRUE(parsed.present[DeviceSlot(DeviceIndex::LeftController)]);

    std::string wrongVersion = valid;
    wrongVersion.replace(wrongVersion.find("\"version\":1"), 11, "\"version\":2");
    EXPECT_FALSE(ParsePoseFrame(wrongVersion, parsed));
    EXPECT_FALSE(ParsePoseFrame("{not json", parsed));

    std::string oversized(static_cast<std::size_t>(kMaxPacketBytes), ' ');
    EXPECT_FALSE(ParsePoseFrameBytes(oversized.data(), static_cast<int>(oversized.size()), parsed));

    const std::string missingHmd = "{\"version\":1,\"devices\":{\"hmd\":{\"valid\":true},\"left_controller\":{\"valid\":true,\"connected\":true,\"pose\":{\"position\":[0,1,0],\"rotation_xyzw\":[0,0,0,1]}}}}";
    EXPECT_TRUE(ParsePoseFrame(missingHmd, parsed));
    EXPECT_FALSE(parsed.present[DeviceSlot(DeviceIndex::Hmd)]);
    EXPECT_TRUE(parsed.present[DeviceSlot(DeviceIndex::LeftController)]);

    const std::string unknownOnly = "{\"version\":1,\"devices\":{\"unknown\":{\"valid\":true,\"connected\":true,\"pose\":{\"position\":[0,1,0],\"rotation_xyzw\":[0,0,0,1]}}}}";
    EXPECT_FALSE(ParsePoseFrame(unknownOnly, parsed));

    const std::string nonFinite = "{\"version\":1,\"devices\":{\"hmd\":{\"valid\":true,\"connected\":true,\"pose\":{\"position\":[0,1e999,0],\"rotation_xyzw\":[0,0,0,1]}}}}";
    EXPECT_FALSE(ParsePoseFrame(nonFinite, parsed));

    const std::string normalized = "{\"version\":1,\"devices\":{\"hmd\":{\"valid\":true,\"connected\":true,\"pose\":{\"position\":[0,1,0],\"rotation_xyzw\":[0,0,0,0.8]}}}}";
    EXPECT_TRUE(ParsePoseFrame(normalized, parsed));
    EXPECT_NEAR(parsed.samples[DeviceSlot(DeviceIndex::Hmd)].rotation_xyzw[3], 1.0f, 0.0001f);

    const std::string invalidQuat = "{\"version\":1,\"devices\":{\"hmd\":{\"valid\":true,\"connected\":true,\"pose\":{\"position\":[0,1,0],\"rotation_xyzw\":[0,0,0,0.1]}}}}";
    EXPECT_FALSE(ParsePoseFrame(invalidQuat, parsed));

    const std::string clampedInput = "{\"version\":1,\"devices\":{\"left_controller\":{\"valid\":true,\"connected\":true,\"pose\":{\"position\":[0,1,0],\"rotation_xyzw\":[0,0,0,1]}}},\"inputs\":{\"left_controller\":{\"trigger_click\":true,\"trigger_value\":3,\"joystick_x\":2,\"joystick_y\":-2}}}";
    EXPECT_TRUE(ParsePoseFrame(clampedInput, parsed));
    const PoseSample& left = parsed.samples[DeviceSlot(DeviceIndex::LeftController)];
    EXPECT_NEAR(left.trigger_value, 1.0f, 0.0001f);
    EXPECT_NEAR(left.joystick_x, 1.0f, 0.0001f);
    EXPECT_NEAR(left.joystick_y, -1.0f, 0.0001f);
    EXPECT_NEAR(left.trackpad_x, left.joystick_x, 0.0001f);
    EXPECT_NEAR(left.trackpad_y, left.joystick_y, 0.0001f);

    FrameState serialized = BuildResetTPose(MakeNeutralFrame());
    serialized.devices[DeviceSlot(DeviceIndex::Hmd)].position.y = 26.0f;
    serialized.controllers[0].grip_click = true;
    serialized.controllers[0].grip_value = 2.0f;
    serialized.controllers[0].has_finger_bends = true;
    serialized.controllers[0].finger_bends = {-0.5f, 0.25f, 0.5f, 0.75f, 1.5f};
    EXPECT_TRUE(ParsePoseFrame(SerializeFrame(serialized), parsed));
    const PoseSample& serializedHmd = parsed.samples[DeviceSlot(DeviceIndex::Hmd)];
    EXPECT_NEAR(serializedHmd.position[1], kMaxDeviceY, 0.0001f);
    const PoseSample& serializedLeft = parsed.samples[DeviceSlot(DeviceIndex::LeftController)];
    EXPECT_TRUE(serializedLeft.grip_click);
    EXPECT_NEAR(serializedLeft.grip_value, 1.0f, 0.0001f);
    EXPECT_TRUE(serializedLeft.has_finger_bends);
    EXPECT_NEAR(serializedLeft.finger_bends.thumb, 0.0f, 0.0001f);
    EXPECT_NEAR(serializedLeft.finger_bends.index, 0.25f, 0.0001f);
    EXPECT_NEAR(serializedLeft.finger_bends.pinky, 1.0f, 0.0001f);

    const std::string overHeight = "{\"version\":1,\"devices\":{\"hmd\":{\"valid\":true,\"connected\":true,\"pose\":{\"position\":[0,26,0],\"rotation_xyzw\":[0,0,0,1]}}}}";
    EXPECT_TRUE(ParsePoseFrame(overHeight, parsed));
    EXPECT_NEAR(parsed.samples[DeviceSlot(DeviceIndex::Hmd)].position[1], kMaxDeviceY, 0.0001f);
    EXPECT_TRUE(parsed.y_clamped[DeviceSlot(DeviceIndex::Hmd)]);

    // Position components beyond the +/-30 m safety range are rejected.
    const std::string outOfRange = "{\"version\":1,\"devices\":{\"hmd\":{\"valid\":true,\"connected\":true,\"pose\":{\"position\":[31,1,0],\"rotation_xyzw\":[0,0,0,1]}}}}";
    EXPECT_FALSE(ParsePoseFrame(outOfRange, parsed));

    // trackpad falls back to the joystick only when omitted; an explicit trackpad
    // value is kept as-is.
    const std::string explicitTrackpad = "{\"version\":1,\"devices\":{\"left_controller\":{\"valid\":true,\"connected\":true,\"pose\":{\"position\":[0,1,0],\"rotation_xyzw\":[0,0,0,1]}}},\"inputs\":{\"left_controller\":{\"joystick_x\":0.5,\"trackpad_x\":-0.5}}}";
    EXPECT_TRUE(ParsePoseFrame(explicitTrackpad, parsed));
    const PoseSample& tp = parsed.samples[DeviceSlot(DeviceIndex::LeftController)];
    EXPECT_NEAR(tp.joystick_x, 0.5f, 0.0001f);
    EXPECT_NEAR(tp.trackpad_x, -0.5f, 0.0001f);

    const std::string pretty = PrettyPrintJson("{\"text\":\"brace } and quote \\\" stays string\",\"items\":[1,2]}");
    EXPECT_TRUE(pretty.find("brace } and quote \\\" stays string") != std::string::npos);
    EXPECT_TRUE(pretty.find("\n  \"items\"") != std::string::npos);
}

} // namespace anyadance::tests
