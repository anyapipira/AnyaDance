# MMD Dance

**English** | [简体中文](mmd-dance.zh-CN.md)

The companion UI can play an MMD dance (a `.vmd` motion) on the six virtual
devices live in memory. Blender + MMD Tools does the accurate FK/IK solve against
a real model; the UI does a small remapping onto the hardcoded rig and streams
it over UDP at 60 Hz like any other pose.

It is reached from the **Dance (MMD)** button in the right column of the UI's
main controls, which opens a small dialog: pick a VMD and a model, **Analyze**,
then **Play**.

## What you need

- **Blender** (auto-detected from `C:\Program Files\Blender Foundation\Blender *`,
  the `ANYADANCE_BLENDER` environment variable, or `PATH`).
- **MMD Tools** ([MMD-Blender/blender_mmd_tools](https://github.com/MMD-Blender/blender_mmd_tools))
  installed as a Blender add-on / extension (auto-detected from the Blender
  extension tree under `%APPDATA%`).
- A **VMD** motion file.
- A **PMX/PMD model** the motion targets. MMD models are third-party works with
  their own licenses, so you supply your own. Picking the model the dance was
  made for gives the best result.

## How it works

```text
.vmd + .pmx
  -> Blender + MMD Tools import + evaluate FK/IK/constraints   (scripts/blender_export_mmd.py)
  -> one solved-motion JSON of world-space joint poses          (src/core/solved_motion.*)
  -> simplified remap onto the six hardcoded devices            (src/core/mmd_retarget.*)
  -> 60 Hz UDP stream to the driver                             (the UI's normal path)
```

`scripts/blender_export_mmd.py` runs headless inside Blender. It imports the
model and motion, lets MMD Tools evaluate the pose, and
writes **one** JSON file (reused/overwritten each export, under
`%TEMP%\AnyaDance\mmd_solved.json`) holding, per frame, the world-space
pose of head, shoulders, elbows, wrists, pelvis, ankles and toes, plus per-hand
finger curls. When the model has the required finger bones, it also records each
rest hand's finger direction and palm normal for controller-frame calibration.
Output is in the same convention the driver uses: right-handed, `+Y` up, `-Z`
forward, metres, quaternions `xyzw`, the avatar's left on `-X`.

## The simplified remap

Because the rig is hardcoded and turning is done with the joystick, the remap is
correspondingly small (`src/core/mmd_retarget.cpp`):

- **Turn → joystick.** The remap keeps world steering as controller input. You
  steer the avatar in-world with the same `Q`/`E` controls used by normal pose
  streaming.
- **Scale to height.** The body is scaled so its standing height matches the
  target height (`1.5 m`, the rig's HMD height `kResetHmdY`).
- **Direct device placement.** Each device follows its driving joint: HMD ← head
  (nudged up to the crown), hip ← pelvis, feet ← ankles, hands ← wrists. The palm
  offset is calibrated in wrist-local rest space, so it follows live wrist
  flexion, and is stretched a little about the shoulder (**Hand reach**) so
  VRChat IK gets an extended-arm target.
- **HMD/hip/feet rotation as deltas.** Orientation is the joint's rotation
  *relative to the model's rest pose* applied to the clean upright/forward device
  rest, keeping those devices in a stable rest frame.
- **Controller orientation from the solved wrist.** The model-specific wrist bone
  frame is calibrated to the OpenVR controller frame at rest, then follows the
  solved wrist rigidly so flexion, deviation, and roll all survive. Exported rest
  finger/palm axes set the absolute palm roll; models without them fall back to
  an arm-anatomy rest frame.
- **Fingers.** When the model has finger bones, per-hand curls drive the
  controllers' skeletal hand pose.

Anchoring: when you press Play the dance is pinned to the HMD's current X/Z
position, so the avatar dances in place.

## Dialog

| Field       | Meaning                                                              |
|-------------|---------------------------------------------------------------------|
| VMD motion  | The `.vmd` dance file.                                               |
| Model       | The `.pmx`/`.pmd` model to solve against.                           |
| Loop        | Repeat the dance when it ends (otherwise it holds the final pose).  |

Everything else (target height `1.5 m`, playback speed, hand reach, solve frame
rate) uses fixed defaults.

**Advanced** (collapsible) holds the **Blender path** and **MMD Tools path**.
They are auto-filled with the detected locations when the dialog opens, so most
users keep the detected values. Manual paths are saved in the UI preferences
and reused on the next launch.

**Analyze** runs the Blender solve and reports duration / frame count / scale.
**Play** stays disabled until a solve succeeds (and while one is running), then
plays the solved dance. **Stop** returns to the T-pose. **Reset to T-Pose** also
stops playback.

## Saving and loading clips (.nya)

Once a dance is analyzed, **Save .nya** writes the retargeted result to a `.nya`
clip file. **Load .nya** reads one back and enables Play immediately — loading a
clip skips both the Blender solve and the remap, so a saved dance plays instantly
on later runs (and on machines without Blender installed).

A `.nya` file stores device-level frames (the six device poses plus per-hand
finger bends), so it is the same format the main window uses for **Save Pose** /
**Load Pose**: a pose is just a one-frame clip. Device Y is clamped to the 2 m
limit and finger bends to `[0, 1]` on load. See `src/core/nya_format.*`.

## Notes and limits

- The first solve of a long dance can take Blender several seconds to a minute;
  the UI keeps rendering and streaming a T-pose while it runs.
- Quality depends on matching the motion to its intended model. A wildly
  different model (very different proportions) will still play but may look off.
- This is offline-solve + live-play: the solved motion is used by the UI for
  playback. Standard VRChat anti-cheat caveats in the project
  [disclaimer](../DISCLAIMER.md) apply.
