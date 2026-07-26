#include "test_framework.h"
#include "tests.h"

#include "core/constants.h"
#include "core/frame_state.h"
#include "core/mmd_retarget.h"
#include "core/nya_format.h"
#include "core/tpose.h"

#include <string>

namespace anyadance::tests {

void TestNya() {
    // A saved pose is a one-frame looping clip; round-tripping it restores the
    // device poses and finger bends exactly.
    FrameState frame = BuildResetTPose(MakeNeutralFrame());
    frame.devices[DeviceSlot(DeviceIndex::LeftFoot)].position = {0.3f, 0.1f, -0.2f};
    frame.devices[DeviceSlot(DeviceIndex::Hmd)].position.y = 1.7f;
    frame.controllers[0].has_finger_bends = true;
    frame.controllers[0].finger_bends = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f};
    frame.controllers[1].has_finger_bends = true;
    frame.controllers[1].finger_bends = {0.5f, 0.4f, 0.3f, 0.2f, 0.1f};

    NyaClip pose = MakePoseClip(frame);
    EXPECT_TRUE(pose.IsPose());
    EXPECT_TRUE(pose.loop);

    NyaClip roundTrip;
    std::string error;
    EXPECT_TRUE(ParseNya(SerializeNya(pose), roundTrip, error));
    EXPECT_TRUE(roundTrip.IsPose());
    EXPECT_TRUE(roundTrip.motion.valid);
    const FrameState& got = roundTrip.motion.frames[0];
    EXPECT_VEC3_NEAR(got.devices[DeviceSlot(DeviceIndex::LeftFoot)].position, (Vec3{0.3f, 0.1f, -0.2f}));
    EXPECT_NEAR(got.devices[DeviceSlot(DeviceIndex::Hmd)].position.y, 1.7f, 0.0001f);
    EXPECT_TRUE(got.controllers[0].has_finger_bends);
    EXPECT_NEAR(got.controllers[0].finger_bends.thumb, 0.1f, 0.0001f);
    EXPECT_NEAR(got.controllers[1].finger_bends.thumb, 0.5f, 0.0001f);

    // A multi-frame animation round-trips its times and frame count, and Y is
    // clamped to the supported Y range on load even if the source exceeds it.
    DanceMotion motion;
    for (int i = 0; i < 3; ++i) {
        FrameState f = BuildResetTPose(MakeNeutralFrame());
        f.devices[DeviceSlot(DeviceIndex::Hmd)].position.y = 26.0f;
        motion.frames.push_back(f);
        motion.times.push_back(static_cast<float>(i) * 0.5f);
    }
    motion.valid = true;
    NyaClip anim = MakeAnimationClip(motion, 60.0f, "test_model");
    NyaClip animBack;
    EXPECT_TRUE(ParseNya(SerializeNya(anim), animBack, error));
    EXPECT_FALSE(animBack.IsPose());
    EXPECT_TRUE(animBack.motion.frames.size() == 3);
    EXPECT_NEAR(animBack.motion.times[2], 1.0f, 0.0001f);
    EXPECT_TRUE(animBack.model == "test_model");
    EXPECT_NEAR(animBack.motion.frames[0].devices[DeviceSlot(DeviceIndex::Hmd)].position.y, kMaxDeviceY, 0.0001f);

    // A document without the format tag is rejected.
    NyaClip rejected;
    EXPECT_FALSE(ParseNya("{\"frames\":[]}", rejected, error));
}

} // namespace anyadance::tests
