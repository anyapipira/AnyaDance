"""Solve an MMD VMD dance against a PMX/PMD model and export joint world poses.

AnyaDance runs this through Blender (not the UI process):

    blender --background --python scripts/blender_export_mmd.py -- \
      --model model.pmx --vmd dance.vmd --output solved.json [--mmd-tools-path DIR] [--fps 60]

MMD Tools (https://github.com/MMD-Blender/blender_mmd_tools) must be installed as
a Blender add-on, or pass --mmd-tools-path. The script imports the model and VMD,
lets Blender/MMD Tools evaluate FK/IK/constraints, then writes the evaluated
world-space poses of the body joints AnyaDance needs into a small JSON file.
the UI reads that JSON and does the (simple) device remapping itself.

Output JSON is in OpenVR standing convention: right-handed, +Y up, -Z forward,
metres, quaternions (x, y, z, w). The avatar's left side is on -X.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

# Canonical joints AnyaDance consumes, each with source-bone aliases in
# MMD priority order. Non-IK anatomical bones win over IK control bones. MMD Tools
# renames the Japanese L/R bones to a "<base>.L"/"<base>.R" suffix form, so both
# the raw VMD prefix names (左肩) and the imported suffix names (肩.L) are listed.
JOINT_ALIASES: dict[str, tuple[str, ...]] = {
    "pelvis": ("下半身", "センター", "グルーブ", "腰", "lower body", "center", "pelvis", "hips"),
    "head": ("頭", "head"),
    "left_shoulder": ("左肩", "肩.L", "left shoulder", "shoulder_L"),
    "right_shoulder": ("右肩", "肩.R", "right shoulder", "shoulder_R"),
    "left_elbow": ("左ひじ", "左肘", "ひじ.L", "肘.L", "left elbow", "elbow_L"),
    "right_elbow": ("右ひじ", "右肘", "ひじ.R", "肘.R", "right elbow", "elbow_R"),
    "left_wrist": ("左手首", "手首.L", "left wrist", "wrist_L"),
    "right_wrist": ("右手首", "手首.R", "right wrist", "wrist_R"),
    "left_ankle": ("左足首", "足首.L", "left ankle", "ankle_L", "左足ＩＫ", "足ＩＫ.L"),
    "right_ankle": ("右足首", "足首.R", "right ankle", "ankle_R", "右足ＩＫ", "足ＩＫ.R"),
    "left_toe": ("左つま先", "左足先", "つま先.L", "足先EX.L", "left toe", "toe_L"),
    "right_toe": ("右つま先", "右足先", "つま先.R", "足先EX.R", "right toe", "toe_R"),
}

# Joints that may borrow another joint when the model lacks them, so a model
# without explicit toe bones still produces a usable floor/height reference.
JOINT_FALLBACK: dict[str, str] = {
    "left_toe": "left_ankle",
    "right_toe": "right_ankle",
}

# Finger phalanx bone *base* names per finger, base->tip. The actual bone is the
# base with a side marker: MMD Tools uses the "<base>.L"/"<base>.R" suffix; raw
# VMD names use a "左"/"右" prefix. _finger_bone_names builds both forms.
_FINGER_ORDER = ("thumb", "index", "middle", "ring", "pinky")
FINGER_BASES: dict[str, tuple[str, ...]] = {
    "thumb": ("親指０", "親指１", "親指２", "親指0", "親指1", "親指2"),
    "index": ("人指１", "人指２", "人指３", "人差指１", "人差指２", "人差指３",
              "人指1", "人指2", "人指3"),
    "middle": ("中指１", "中指２", "中指３", "中指1", "中指2", "中指3"),
    "ring": ("薬指１", "薬指２", "薬指３", "薬指1", "薬指2", "薬指3"),
    "pinky": ("小指１", "小指２", "小指３", "小指1", "小指2", "小指3"),
}


def _finger_bone_names(side: str, finger: str) -> tuple[str, ...]:
    suffix = ".L" if side == "left" else ".R"
    prefix = "左" if side == "left" else "右"
    names: list[str] = []
    for base in FINGER_BASES[finger]:
        names.append(base + suffix)  # MMD Tools imported form, e.g. 人指１.L
        names.append(prefix + base)  # raw VMD form, e.g. 左人指１
    return tuple(names)
# Total curl across a finger's phalanges that maps to a fully closed hand (1.0).
_FULL_FINGER_CURL_RAD = math.radians(150.0)
_IK_TOKENS = ("ik", "ｉｋ", "ＩＫ")


def main() -> int:
    args = _parse_args()
    try:
        import bpy  # type: ignore[import-not-found]
        import mathutils  # type: ignore[import-not-found]
    except ImportError as exc:
        print(f"This script must run inside Blender: {exc}", file=sys.stderr)
        return 2

    try:
        _enable_mmd_tools(bpy, args.mmd_tools_path)
        _clear_scene(bpy)
        _import_model(bpy, args.model)
        armature = _find_armature(bpy)
        _import_vmd(bpy, args.vmd, armature)

        scene = bpy.context.scene
        source_fps = float(scene.render.fps) / max(1.0, float(scene.render.fps_base))
        output_fps = float(args.fps) if args.fps and args.fps > 0 else source_fps
        frame_start, frame_end = _frame_range(scene, armature)

        bone_names = [bone.name for bone in armature.pose.bones]
        selected = _map_joints(bone_names)
        finger_selected = _map_fingers(set(bone_names))

        rest = _read_pose(bpy, mathutils, armature, selected, frame_start, rest=True)
        rest_hand_axes = _read_rest_hand_axes(
            bpy, mathutils, armature, rest, finger_selected, frame_start
        )
        frames = []
        for index, blender_frame in enumerate(
            _sample_frames(frame_start, frame_end, source_fps, output_fps)
        ):
            joints = _read_pose(bpy, mathutils, armature, selected, blender_frame, rest=False)
            fingers = _read_fingers(bpy, armature, finger_selected, blender_frame)
            frame_entry = {"t": index / output_fps, "j": joints}
            if fingers is not None:
                frame_entry["fl"] = fingers["left"]
                frame_entry["fr"] = fingers["right"]
            frames.append(frame_entry)

        document = {
            "format": "anyadance_mmd_solved",
            "version": 1,
            "fps": output_fps,
            "model": args.model.stem,
            "joints": list(JOINT_ALIASES.keys()),
            "has_fingers": finger_selected is not None,
            "rest": rest,
            "frames": frames,
        }
        if rest_hand_axes:
            document["rest_hand_axes"] = rest_hand_axes
        args.output.write_text(json.dumps(document), encoding="utf-8")
        print(f"Wrote solved MMD motion: {args.output} ({len(frames)} frames @ {output_fps:g} fps)")
        return 0
    except Exception as exc:  # noqa: BLE001 - boundary script: report and fail.
        print(f"AnyaDance MMD export failed: {exc}", file=sys.stderr)
        return 1


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--vmd", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--fps", type=float, default=60.0)
    parser.add_argument("--mmd-tools-path", type=Path, default=None)
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    return parser.parse_args(argv)


def _enable_mmd_tools(bpy, mmd_tools_path) -> None:  # type: ignore[no-untyped-def]
    if mmd_tools_path is not None:
        mmd_tools_path = Path(mmd_tools_path).resolve()
        module_root = (
            mmd_tools_path.parent
            if (mmd_tools_path / "__init__.py").exists()
            else mmd_tools_path
        )
        sys.path.insert(0, str(module_root))
    # Newer Blender ships MMD Tools as an extension (bl_ext.*); older installs use
    # the legacy add-on module name. Try the common ids in turn.
    for module in (
        "bl_ext.blender_org.mmd_tools",
        "bl_ext.user_default.mmd_tools",
        "mmd_tools",
        "blender_mmd_tools",
    ):
        try:
            bpy.ops.preferences.addon_enable(module=module)
            return
        except Exception:
            pass
    # If the operator is already registered the import path is irrelevant.
    if hasattr(bpy.ops, "mmd_tools"):
        return
    raise RuntimeError(
        "MMD Tools is not available in Blender. Install MMD-Blender/blender_mmd_tools "
        "as a Blender add-on or pass --mmd-tools-path."
    )


def _clear_scene(bpy) -> None:  # type: ignore[no-untyped-def]
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete()


def _import_model(bpy, model_path: Path) -> None:  # type: ignore[no-untyped-def]
    if not model_path.exists():
        raise RuntimeError(f"Model path does not exist: {model_path}")
    _try_ops(
        (
            lambda: bpy.ops.mmd_tools.import_model(filepath=str(model_path)),
            lambda: bpy.ops.import_scene.mmd(filepath=str(model_path)),
        ),
        f"Could not import PMD/PMX model with MMD Tools: {model_path}",
    )


def _import_vmd(bpy, vmd_path: Path, armature) -> None:  # type: ignore[no-untyped-def]
    if not vmd_path.exists():
        raise RuntimeError(f"VMD path does not exist: {vmd_path}")
    bpy.context.view_layer.objects.active = armature
    armature.select_set(True)
    _try_ops(
        (
            lambda: bpy.ops.mmd_tools.import_vmd(filepath=str(vmd_path)),
            lambda: bpy.ops.mmd_tools.import_vmd(
                filepath=str(vmd_path),
                files=[{"name": vmd_path.name}],
                directory=str(vmd_path.parent),
            ),
        ),
        f"Could not import/apply VMD motion with MMD Tools: {vmd_path}",
    )


def _try_ops(calls, error: str) -> None:  # type: ignore[no-untyped-def]
    failures = []
    for call in calls:
        try:
            result = call()
            if "FINISHED" in set(result):
                return
        except Exception as exc:  # noqa: BLE001
            failures.append(str(exc))
    raise RuntimeError(error + "; attempts: " + " | ".join(failures))


def _find_armature(bpy):  # type: ignore[no-untyped-def]
    armatures = [obj for obj in bpy.context.scene.objects if obj.type == "ARMATURE"]
    if not armatures:
        raise RuntimeError("No armature was created by the MMD model import.")
    return armatures[0]


def _frame_range(scene, armature) -> tuple[float, float]:  # type: ignore[no-untyped-def]
    start = float(scene.frame_start)
    end = float(scene.frame_end)
    if armature.animation_data and armature.animation_data.action:
        action_start, action_end = armature.animation_data.action.frame_range
        start = min(start, float(action_start))
        end = max(end, float(action_end))
    return start, max(start, end)


def _sample_frames(
    frame_start: float, frame_end: float, source_fps: float, output_fps: float
) -> list[float]:
    duration = max(0.0, (frame_end - frame_start) / max(1e-6, source_fps))
    count = max(1, int(duration * output_fps) + 1)
    samples = [frame_start + i * source_fps / output_fps for i in range(count)]
    if samples[-1] < frame_end:
        samples.append(frame_end)
    return [min(frame_end, sample) for sample in samples]


def _normalize(name: str) -> str:
    return name.strip().lower().replace("_", " ").replace("-", " ")


def _is_ik(name: str) -> bool:
    lowered = name.lower()
    return any(token in lowered for token in _IK_TOKENS)


def _map_joints(bone_names: list[str]) -> dict[str, str]:
    by_norm: dict[str, str] = {}
    for name in bone_names:
        by_norm.setdefault(_normalize(name), name)
    selected: dict[str, str] = {}
    for joint, aliases in JOINT_ALIASES.items():
        candidates = []
        for alias in aliases:
            found = by_norm.get(_normalize(alias))
            if found is not None and found not in candidates:
                candidates.append(found)
        anatomical = [name for name in candidates if not _is_ik(name)]
        if anatomical:
            selected[joint] = anatomical[0]
        elif candidates:
            selected[joint] = candidates[0]

    for joint, fallback in JOINT_FALLBACK.items():
        if joint not in selected and fallback in selected:
            selected[joint] = selected[fallback]

    missing = [joint for joint in JOINT_ALIASES if joint not in selected]
    if missing:
        raise RuntimeError(
            "Model is missing required bones for: " + ", ".join(missing)
            + ". Found bones: " + ", ".join(bone_names)
        )
    return selected


def _map_fingers(bone_names: set[str]):  # type: ignore[no-untyped-def]
    selected: dict[str, dict[str, tuple[str, ...]]] = {}
    found_any = False
    for side in ("left", "right"):
        selected[side] = {}
        for finger in _FINGER_ORDER:
            present = tuple(
                name for name in _finger_bone_names(side, finger) if name in bone_names
            )
            selected[side][finger] = present
            found_any = found_any or bool(present)
    return selected if found_any else None


# Blender->OpenVR change of basis. MMD Tools imports the model +Z up, facing -Y;
# AnyaDance wants +Y up, facing -Z, left side on -X. C is symmetric and a
# proper rotation (det +1), so a rotation maps as R' = C @ R @ C.
def _basis(mathutils):  # type: ignore[no-untyped-def]
    return mathutils.Matrix(((-1.0, 0.0, 0.0), (0.0, 0.0, 1.0), (0.0, 1.0, 0.0)))


def _pose_from_matrix(mathutils, world) -> dict:  # type: ignore[no-untyped-def]
    c = _basis(mathutils)
    position = c @ world.translation
    rotation = (c @ world.to_3x3().normalized() @ c).to_quaternion()
    return {
        "p": [position.x, position.y, position.z],
        # mathutils Quaternion is (w, x, y, z); emit xyzw.
        "q": [rotation.x, rotation.y, rotation.z, rotation.w],
    }


def _read_pose(bpy, mathutils, armature, selected, frame, *, rest):  # type: ignore[no-untyped-def]
    data = armature.data
    previous = data.pose_position
    if rest:
        data.pose_position = "REST"
    try:
        scene = bpy.context.scene
        scene.frame_set(int(math.floor(frame)), subframe=frame - math.floor(frame))
        depsgraph = bpy.context.evaluated_depsgraph_get()
        depsgraph.update()
        evaluated = armature.evaluated_get(depsgraph)
        out: dict[str, dict] = {}
        for joint, bone_name in selected.items():
            pose_bone = evaluated.pose.bones.get(bone_name)
            if pose_bone is None:
                raise RuntimeError(f"Bone disappeared during evaluation: {bone_name}")
            world = evaluated.matrix_world @ pose_bone.matrix
            out[joint] = _pose_from_matrix(mathutils, world)
        return out
    finally:
        data.pose_position = previous


def _read_rest_hand_axes(
    bpy, mathutils, armature, rest, finger_selected, frame
):  # type: ignore[no-untyped-def]
    """Measure rest finger directions and palm normals from finger-base bones.

    The wrist matrix carries all live wrist motion, but its bone-local axes are
    model-specific. These two anatomical directions let the retargeter calibrate
    the model wrist frame to the OpenVR controller frame without guessing palm
    roll. Models without the three required finger bases use the arm fallback.
    """
    if finger_selected is None:
        return {}

    data = armature.data
    previous = data.pose_position
    data.pose_position = "REST"
    try:
        scene = bpy.context.scene
        scene.frame_set(int(math.floor(frame)), subframe=frame - math.floor(frame))
        depsgraph = bpy.context.evaluated_depsgraph_get()
        depsgraph.update()
        evaluated = armature.evaluated_get(depsgraph)
        basis = _basis(mathutils)

        def first_position(names):  # type: ignore[no-untyped-def]
            for name in names:
                pose_bone = evaluated.pose.bones.get(name)
                if pose_bone is not None:
                    world = evaluated.matrix_world @ pose_bone.matrix
                    position = basis @ world.translation
                    return mathutils.Vector((position.x, position.y, position.z))
            return None

        axes = {}
        for side, device in (
            ("left", "left_controller"),
            ("right", "right_controller"),
        ):
            index = first_position(finger_selected[side]["index"])
            middle = first_position(finger_selected[side]["middle"])
            pinky = first_position(finger_selected[side]["pinky"])
            if index is None or middle is None or pinky is None:
                continue
            wrist_values = rest[side + "_wrist"]["p"]
            wrist = mathutils.Vector(wrist_values)
            finger = middle - wrist
            palm = (index - wrist).cross(pinky - wrist)
            # Mirrored hand topology reverses the cross-product direction.
            if side == "left":
                palm.negate()
            if finger.length < 1e-9 or palm.length < 1e-9:
                continue
            finger.normalize()
            palm.normalize()
            axes[device] = {
                "finger": list(finger),
                "palm": list(palm),
            }
        return axes
    finally:
        data.pose_position = previous


def _read_fingers(bpy, armature, finger_selected, frame):  # type: ignore[no-untyped-def]
    if finger_selected is None:
        return None
    scene = bpy.context.scene
    scene.frame_set(int(math.floor(frame)), subframe=frame - math.floor(frame))
    out: dict[str, list[float]] = {}
    for side in ("left", "right"):
        curls = []
        for finger in _FINGER_ORDER:
            total = 0.0
            for bone_name in finger_selected[side].get(finger, ()):
                pose_bone = armature.pose.bones.get(bone_name)
                if pose_bone is None:
                    continue
                # matrix_basis is the bone's local channel rotation; its angle is
                # the per-bone curl regardless of the rest orientation.
                total += abs(pose_bone.matrix_basis.to_quaternion().angle)
            curls.append(max(0.0, min(1.0, total / _FULL_FINGER_CURL_RAD)))
        out[side] = curls
    return out


if __name__ == "__main__":
    raise SystemExit(main())
