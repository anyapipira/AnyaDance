# MMD Dance

**English** | [简体中文](mmd-dance.zh-CN.md) | [日本語](mmd-dance.ja.md)

The companion UI can play an MMD dance (a `.vmd` motion) on the six virtual
devices live in memory. Blender + MMD Tools does the accurate FK/IK solve against
a real model; the UI does a small remapping onto the hardcoded rig and streams
it over UDP at 60 Hz like any other pose.

It is reached from the **Dance (MMD)** button in the right column of the UI's
main controls, which opens a small dialog: pick a VMD and a model, **Analyze**,
then **Play**.

## What you need

- **Blender** (auto-detected from `C:\Program Files\Blender Foundation\Blender *`,
  Microsoft Store's `blender-launcher.exe` app execution alias, the
  `ANYADANCE_BLENDER` environment variable, or `PATH`).
- **MMD Tools** ([MMD-Blender/blender_mmd_tools](https://github.com/MMD-Blender/blender_mmd_tools))
  installed as a Blender add-on / extension (auto-detected from the Blender
  extension tree under `%APPDATA%`).
- A **VMD** motion file.
- A **PMX/PMD model** the motion targets. MMD models are third-party works with
  their own licenses, so you supply your own. Picking the model the dance was
  made for gives the best result.
- Optional **BGM audio** in a Windows Media Foundation-supported format. The UI
  file picker includes WAV, MP3, M4A, AAC, and WMA.

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
  finger/palm axes set the absolute palm roll, while shoulder/elbow/wrist anatomy
  provides the joint-based rest alignment.
- **Fingers.** When the model has finger bones, per-hand curls drive the
  controllers' skeletal hand pose.

Anchoring: when you press Play the dance is pinned to the HMD's current X/Z
position, so the avatar dances in place.

## Dialog

| Field                  | Meaning |
|------------------------|---------|
| VMD motion             | The `.vmd` dance file. |
| Model                  | The `.pmx`/`.pmd` model to solve against. |
| BGM audio              | Optional audio decoded by Windows Media Foundation. |
| Audio start offset (s) | Audio start relative to motion frame zero. A negative value plays an intro first; a positive value delays the audio. |
| Audio output           | The Windows playback endpoint used for BGM. The selection is saved in UI preferences. |
| Loop                   | Repeat the combined motion/audio transport. |

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

Motion and BGM share one transport. Its start is the earlier of motion frame
zero and the configured audio start; its end is the later of the last motion
frame and the end of the audio. During an audio intro the avatar holds the first
motion frame. During an audio outro it holds the last frame. **Loop** repeats
this entire combined interval, so the intro/outro freezes are repeated on every
cycle and the motion never loops independently underneath unfinished audio.

## Saving and loading clips (.nya)

Once a dance is analyzed, **Save .nya** writes the retargeted result to a `.nya`
clip file. **Load .nya** reads one back and enables Play immediately — loading a
clip skips both the Blender solve and the remap, so a saved dance plays instantly
on later runs.

A `.nya` file stores device-level frames (the six device poses plus per-hand
finger bends), so it is the same format the main window uses for **Save Pose** /
**Load Pose**: a pose is just a one-frame clip. On load, device Y is clamped to
the 0–25 m limit for the HMD and the -30–25 m limit for the other five devices,
and finger bends to `[0, 1]`. See `src/core/nya_format.*`.

## Runtime behavior

- The first solve of a long dance can take Blender several seconds to a minute;
  the UI keeps rendering and streaming a T-pose while it runs.
- Motion/model proportion matching determines retarget quality.
- This is offline-solve + live-play: the solved motion is used by the UI for
  playback. Standard VRChat anti-cheat caveats in the project
  [disclaimer](../DISCLAIMER.md) apply.
