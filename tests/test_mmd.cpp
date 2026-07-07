#include "test_framework.h"
#include "tests.h"

#include "core/constants.h"
#include "core/frame_state.h"
#include "core/math3d.h"
#include "core/mmd_retarget.h"
#include "core/solved_motion.h"

#include <string>

namespace anyadance::tests {
namespace {

// Build a simple forward-facing standing skeleton in OpenVR convention so the
// retarget has a known rest to scale against.
SolvedJoint MakeJoint(Vec3 position) {
    return SolvedJoint{position, Quat{}};
}

SolvedMotion MakeStandingMotion() {
    SolvedMotion motion;
    motion.fps = 30.0f;
    motion.hasRest = true;
    auto& r = motion.rest;
    r[JointSlot(SolvedJointId::Pelvis)] = MakeJoint({0.0f, 0.90f, 0.0f});
    r[JointSlot(SolvedJointId::Head)] = MakeJoint({0.0f, 1.50f, 0.0f});
    r[JointSlot(SolvedJointId::LeftShoulder)] = MakeJoint({-0.18f, 1.35f, 0.0f});
    r[JointSlot(SolvedJointId::RightShoulder)] = MakeJoint({0.18f, 1.35f, 0.0f});
    r[JointSlot(SolvedJointId::LeftElbow)] = MakeJoint({-0.40f, 1.35f, 0.0f});
    r[JointSlot(SolvedJointId::RightElbow)] = MakeJoint({0.40f, 1.35f, 0.0f});
    r[JointSlot(SolvedJointId::LeftWrist)] = MakeJoint({-0.60f, 1.35f, 0.0f});
    r[JointSlot(SolvedJointId::RightWrist)] = MakeJoint({0.60f, 1.35f, 0.0f});
    r[JointSlot(SolvedJointId::LeftAnkle)] = MakeJoint({-0.10f, 0.10f, 0.0f});
    r[JointSlot(SolvedJointId::RightAnkle)] = MakeJoint({0.10f, 0.10f, 0.0f});
    r[JointSlot(SolvedJointId::LeftToe)] = MakeJoint({-0.10f, 0.00f, 0.10f});
    r[JointSlot(SolvedJointId::RightToe)] = MakeJoint({0.10f, 0.00f, 0.10f});

    SolvedFrame frame0;
    frame0.t = 0.0f;
    frame0.joints = motion.rest;
    SolvedFrame frame1 = frame0;
    frame1.t = 1.0f;
    frame1.joints[JointSlot(SolvedJointId::Head)] = MakeJoint({0.0f, 1.60f, 0.0f});
    motion.frames = {frame0, frame1};
    return motion;
}

} // namespace

void TestMmdParse() {
    const std::string json =
        "{ \"format\":\"anyadance_mmd_solved\", \"version\":1, \"fps\":60, \"model\":\"Miku\", "
        "\"has_fingers\":true, "
        "\"rest_hand_axes\":{\"left_controller\":{\"finger\":[-1,0,0],\"palm\":[0,0,-1]},"
        "\"right_controller\":{\"finger\":[1,0,0],\"palm\":[0,0,-1]}}, "
        "\"rest\": { "
        "\"pelvis\":{\"p\":[0,0.9,0],\"q\":[0,0,0,1]}, \"head\":{\"p\":[0,1.5,0],\"q\":[0,0,0,1]}, "
        "\"left_shoulder\":{\"p\":[-0.18,1.35,0],\"q\":[0,0,0,1]}, \"right_shoulder\":{\"p\":[0.18,1.35,0],\"q\":[0,0,0,1]}, "
        "\"left_elbow\":{\"p\":[-0.4,1.35,0],\"q\":[0,0,0,1]}, \"right_elbow\":{\"p\":[0.4,1.35,0],\"q\":[0,0,0,1]}, "
        "\"left_wrist\":{\"p\":[-0.6,1.35,0],\"q\":[0,0,0,1]}, \"right_wrist\":{\"p\":[0.6,1.35,0],\"q\":[0,0,0,1]}, "
        "\"left_ankle\":{\"p\":[-0.1,0.1,0],\"q\":[0,0,0,1]}, \"right_ankle\":{\"p\":[0.1,0.1,0],\"q\":[0,0,0,1]}, "
        "\"left_toe\":{\"p\":[-0.1,0,0.1],\"q\":[0,0,0,1]}, \"right_toe\":{\"p\":[0.1,0,0.1],\"q\":[0,0,0,1]} }, "
        "\"frames\": [ { \"t\":0, \"j\": { "
        "\"pelvis\":{\"p\":[0,0.9,0],\"q\":[0,0,0,1]}, \"head\":{\"p\":[0,1.5,0],\"q\":[0,0,0,1]}, "
        "\"left_shoulder\":{\"p\":[-0.18,1.35,0],\"q\":[0,0,0,1]}, \"right_shoulder\":{\"p\":[0.18,1.35,0],\"q\":[0,0,0,1]}, "
        "\"left_elbow\":{\"p\":[-0.4,1.35,0],\"q\":[0,0,0,1]}, \"right_elbow\":{\"p\":[0.4,1.35,0],\"q\":[0,0,0,1]}, "
        "\"left_wrist\":{\"p\":[-0.6,1.35,0],\"q\":[0,0,0,1]}, \"right_wrist\":{\"p\":[0.6,1.35,0],\"q\":[0,0,0,1]}, "
        "\"left_ankle\":{\"p\":[-0.1,0.1,0],\"q\":[0,0,0,1]}, \"right_ankle\":{\"p\":[0.1,0.1,0],\"q\":[0,0,0,1]}, "
        "\"left_toe\":{\"p\":[-0.1,0,0.1],\"q\":[0,0,0,1]}, \"right_toe\":{\"p\":[0.1,0,0.1],\"q\":[0,0,0,1]} }, "
        "\"fl\":[0.1,0.2,0.3,0.4,0.5], \"fr\":[0.5,0.4,0.3,0.2,0.1] } ] }";
    SolvedMotion motion;
    std::string error;
    EXPECT_TRUE(ParseSolvedMotion(json, motion, error));
    EXPECT_NEAR(motion.fps, 60.0f, 0.001f);
    EXPECT_TRUE(motion.hasRest);
    EXPECT_TRUE(motion.hasFingers);
    EXPECT_TRUE(motion.frames.size() == 1);
    EXPECT_TRUE(motion.frames[0].hasFingers);
    EXPECT_NEAR(motion.frames[0].leftFingers[2], 0.3f, 0.001f);
    EXPECT_NEAR(motion.rest[JointSlot(SolvedJointId::Head)].position.y, 1.5f, 0.001f);
    EXPECT_TRUE(motion.restHandAxes[0].valid);
    EXPECT_VEC3_NEAR(motion.restHandAxes[0].finger, (Vec3{-1.0f, 0.0f, 0.0f}));
    EXPECT_VEC3_NEAR(motion.restHandAxes[1].palm, (Vec3{0.0f, 0.0f, -1.0f}));

    SolvedMotion bad;
    EXPECT_FALSE(ParseSolvedMotion("{ \"frames\": [] }", bad, error));
    // A non-JSON document is rejected with an error message.
    SolvedMotion garbage;
    std::string garbageError;
    EXPECT_FALSE(ParseSolvedMotion("not json at all", garbage, garbageError));
    EXPECT_TRUE(!garbageError.empty());
}

void TestMmdRetarget() {
    SolvedMotion motion = MakeStandingMotion();
    MmdRetargetParams params;
    params.targetHeightM = 1.62f;  // matches the source height so scale == 1
    params.handReachScale = 1.0f;
    params.headMountUpM = 0.10f;
    params.floorOffsetM = 0.0f;

    DanceMotion dance = BuildDanceMotion(motion, params);
    EXPECT_TRUE(dance.valid);
    EXPECT_TRUE(dance.hasFingers == false);  // synthetic frames carry no fingers
    EXPECT_NEAR(dance.scale, 1.0f, 0.01f);
    EXPECT_NEAR(dance.sourceHeightM, 1.62f, 0.01f);
    EXPECT_TRUE(dance.frames.size() == 2);

    // At the rest pose, devices sit at their scaled joints (feet on the floor, HMD
    // lifted by the head-mount offset) and every rotation is upright.
    const FrameState& rest = dance.frames[0];
    EXPECT_VEC3_NEAR(rest.devices[DeviceSlot(DeviceIndex::Hmd)].position, (Vec3{0.0f, 1.60f, 0.0f}));
    EXPECT_VEC3_NEAR(rest.devices[DeviceSlot(DeviceIndex::Hip)].position, (Vec3{0.0f, 0.90f, 0.0f}));
    EXPECT_VEC3_NEAR(rest.devices[DeviceSlot(DeviceIndex::LeftFoot)].position, (Vec3{-0.10f, 0.10f, 0.0f}));
    EXPECT_VEC3_NEAR(rest.devices[DeviceSlot(DeviceIndex::RightFoot)].position, (Vec3{0.10f, 0.10f, 0.0f}));
    // Hands target the palm (wrist extended along the forearm by 0.21).
    EXPECT_NEAR(rest.devices[DeviceSlot(DeviceIndex::RightController)].position.x, 0.642f, 0.001f);
    EXPECT_SAME_ROTATION(rest.devices[DeviceSlot(DeviceIndex::Hmd)].rotation, Quat{});
    EXPECT_SAME_ROTATION(rest.devices[DeviceSlot(DeviceIndex::LeftFoot)].rotation, Quat{});

    // Optional model-derived hand axes fix the absolute palm roll while keeping
    // the neutral index finger aligned with the model's hand direction.
    SolvedMotion handAxesMotion = MakeStandingMotion();
    handAxesMotion.restHandAxes[0] = {
        {-1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, -1.0f},
        true,
    };
    const DanceMotion handAxesDance = BuildDanceMotion(handAxesMotion, params);
    const FrameState& handAxesRest = handAxesDance.frames[0];
    const Quat handAxesRotation =
        handAxesRest.devices[DeviceSlot(DeviceIndex::LeftController)].rotation;
    const Vec3 neutralIndexLeft{0.11569004f, -0.51338429f, -0.85032487f};
    const Vec3 neutralPalmLeft{0.9783916f, -0.0887937f, 0.1867233f};
    EXPECT_VEC3_NEAR(Rotate(handAxesRotation, neutralIndexLeft), (Vec3{-1.0f, 0.0f, 0.0f}));
    EXPECT_VEC3_NEAR(Rotate(handAxesRotation, neutralPalmLeft), (Vec3{0.0f, 0.0f, -1.0f}));

    // The controller basis and palm offset follow every live wrist degree of
    // freedom. A 90-degree wrist flex turns both the pointing direction and the
    // palm anchor down; the former forearm-only reconstruction lost this motion.
    SolvedMotion flexedMotion = MakeStandingMotion();
    flexedMotion.frames[1].joints[JointSlot(SolvedJointId::LeftWrist)].rotation =
        FromAxisAngle({0.0f, 0.0f, 1.0f}, DegToRad(90.0f));
    const DanceMotion flexedDance = BuildDanceMotion(flexedMotion, params);
    const DeviceState& flexedHand =
        flexedDance.frames[1].devices[DeviceSlot(DeviceIndex::LeftController)];
    EXPECT_VEC3_NEAR(flexedHand.position, (Vec3{-0.60f, 1.308f, 0.0f}));
    EXPECT_VEC3_NEAR(Rotate(flexedHand.rotation, neutralIndexLeft),
                     (Vec3{0.0f, -1.0f, 0.0f}));

    // The floor offset lifts every device by a constant, e.g. to lift a dance off
    // the ground; it does not change the source-height scale.
    MmdRetargetParams lifted = params;
    lifted.floorOffsetM = 0.20f;
    DanceMotion liftedDance = BuildDanceMotion(motion, lifted);
    EXPECT_TRUE(liftedDance.valid);
    EXPECT_NEAR(liftedDance.frames[0].devices[DeviceSlot(DeviceIndex::LeftFoot)].position.y,
                rest.devices[DeviceSlot(DeviceIndex::LeftFoot)].position.y + 0.20f, 0.001f);
    EXPECT_NEAR(liftedDance.frames[0].devices[DeviceSlot(DeviceIndex::Hmd)].position.y,
                rest.devices[DeviceSlot(DeviceIndex::Hmd)].position.y + 0.20f, 0.001f);

    // Steering is controller input, so pelvis orientation keeps controller
    // positions left/right in solve coordinates.
    SolvedMotion orientedMotion = MakeStandingMotion();
    orientedMotion.rest[JointSlot(SolvedJointId::Pelvis)].rotation = FromYaw(DegToRad(90.0f));
    orientedMotion.frames[0].joints[JointSlot(SolvedJointId::Pelvis)].rotation = FromYaw(DegToRad(90.0f));
    orientedMotion.frames[1].joints[JointSlot(SolvedJointId::Pelvis)].rotation = FromYaw(DegToRad(90.0f));
    DanceMotion orientedDance = BuildDanceMotion(orientedMotion, params);
    EXPECT_TRUE(orientedDance.valid);
    EXPECT_TRUE(orientedDance.frames[0].devices[DeviceSlot(DeviceIndex::RightController)].position.x > 0.5f);
    EXPECT_TRUE(orientedDance.frames[0].devices[DeviceSlot(DeviceIndex::LeftController)].position.x < -0.5f);

    // Sampling the head bob halfway lands between the two frames.
    FrameState mid = SampleDanceMotion(dance, 0.5f, false);
    EXPECT_NEAR(mid.devices[DeviceSlot(DeviceIndex::Hmd)].position.y, 1.65f, 0.01f);

    // Anchoring shifts X/Z without touching height.
    AnchorDanceFrame(mid, 2.0f, -3.0f);
    EXPECT_NEAR(mid.devices[DeviceSlot(DeviceIndex::Hip)].position.x, 2.0f, 0.001f);
    EXPECT_NEAR(mid.devices[DeviceSlot(DeviceIndex::Hip)].position.z, -3.0f, 0.001f);
    EXPECT_NEAR(mid.devices[DeviceSlot(DeviceIndex::Hmd)].position.y, 1.65f, 0.01f);

    motion = MakeStandingMotion();
    motion.hasFingers = true;
    motion.frames[0].hasFingers = true;
    motion.frames[0].leftFingers = {0.0f, 0.2f, 0.4f, 0.6f, 0.8f};
    motion.frames[0].rightFingers = {0.8f, 0.6f, 0.4f, 0.2f, 0.0f};
    motion.frames[1].hasFingers = true;
    motion.frames[1].leftFingers = {1.0f, 0.8f, 0.6f, 0.4f, 0.2f};
    motion.frames[1].rightFingers = {0.2f, 0.4f, 0.6f, 0.8f, 1.0f};
    dance = BuildDanceMotion(motion, params);
    EXPECT_TRUE(dance.valid);
    EXPECT_TRUE(dance.hasFingers);
    mid = SampleDanceMotion(dance, 0.5f, false);
    EXPECT_TRUE(mid.controllers[0].has_finger_bends);
    EXPECT_TRUE(mid.controllers[1].has_finger_bends);
    EXPECT_NEAR(mid.controllers[0].finger_bends.thumb, 0.5f, 0.001f);
    EXPECT_NEAR(mid.controllers[0].finger_bends.index, 0.5f, 0.001f);
    EXPECT_NEAR(mid.controllers[1].finger_bends.thumb, 0.5f, 0.001f);
    EXPECT_NEAR(mid.controllers[1].finger_bends.pinky, 0.5f, 0.001f);

    // With no rest pose the first frame stands in for it, so a frame-only motion
    // still retargets.
    SolvedMotion noRest = MakeStandingMotion();
    noRest.hasRest = false;
    EXPECT_TRUE(BuildDanceMotion(noRest, params).valid);

    // A motion with no frames at all cannot be retargeted.
    SolvedMotion empty;
    EXPECT_FALSE(BuildDanceMotion(empty, params).valid);
}

} // namespace anyadance::tests
