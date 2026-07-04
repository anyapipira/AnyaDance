# UDP Protocol

**English** | [简体中文](protocol.zh-CN.md)

## Transport

```text
UDP
127.0.0.1:39570
UTF-8 JSON
version: 1
fire-and-forget datagrams
accepted datagram size: less than 8192 bytes
```

The driver binds to loopback only. Senders treat `sendto` success as local socket success.

## Coordinate Expectations

Positions are in metres in the driver pose coordinate space expected by SteamVR for this driver. Quaternions use XYZW order:

```json
"rotation_xyzw": [x, y, z, w]
```

## Packet Shape

The AnyaDance companion emits a complete snapshot on every datagram: all six
device entries and input entries for both controllers. `finger_bends` is emitted
only when finger data is present. A minimal third-party sender may include fewer
devices, provided at least one recognized device entry is valid.

```json
{
  "version": 1,
  "devices": {
    "hmd": {
      "valid": true,
      "connected": true,
      "pose": {
        "position": [0.0, 1.5, 0.0],
        "rotation_xyzw": [0.0, 0.0, 0.0, 1.0]
      }
    },
    "left_controller": {
      "valid": true,
      "connected": true,
      "pose": {
        "position": [-0.26, 1.10, -0.54],
        "rotation_xyzw": [0.77, 0.10, -0.16, 0.61]
      }
    },
    "right_controller": {
      "valid": true,
      "connected": true,
      "pose": {
        "position": [0.27, 1.57, -0.54],
        "rotation_xyzw": [0.77, -0.10, 0.16, 0.61]
      }
    },
    "hip": {
      "valid": true,
      "connected": true,
      "pose": {
        "position": [0.0, 1.07, -0.05],
        "rotation_xyzw": [0.0, 0.0, 0.0, 1.0]
      }
    },
    "left_foot": {
      "valid": true,
      "connected": true,
      "pose": {
        "position": [-0.09, 0.26, 0.10],
        "rotation_xyzw": [0.0, 0.0, 0.0, 1.0]
      }
    },
    "right_foot": {
      "valid": true,
      "connected": true,
      "pose": {
        "position": [0.09, 0.26, 0.10],
        "rotation_xyzw": [0.0, 0.0, 0.0, 1.0]
      }
    }
  },
  "inputs": {
    "left_controller": {
      "trigger_click": false,
      "trigger_value": 0.0,
      "menu_click": false,
      "system_click": false,
      "a_click": false,
      "b_click": false,
      "grip_click": false,
      "grip_value": 0.0,
      "joystick_x": 0.0,
      "joystick_y": 0.0,
      "trackpad_x": 0.0,
      "trackpad_y": 0.0,
      "finger_bends": {
        "thumb": 0.0,
        "index": 0.0,
        "middle": 0.0,
        "ring": 0.0,
        "pinky": 0.0
      }
    },
    "right_controller": {
      "trigger_click": false,
      "trigger_value": 0.0,
      "menu_click": false,
      "system_click": false,
      "a_click": false,
      "b_click": false,
      "grip_click": false,
      "grip_value": 0.0,
      "joystick_x": 0.0,
      "joystick_y": 0.0,
      "trackpad_x": 0.0,
      "trackpad_y": 0.0
    }
  }
}
```

Recognized device IDs are exactly:

```text
hmd
left_controller
right_controller
hip
left_foot
right_foot
```

The parser uses recognized device IDs and fields. Malformed recognized device entries are skipped; other valid recognized entries in the same packet can still be used.

## Required Device Fields

Each device entry requires:

```json
{
  "valid": true,
  "connected": true,
  "pose": {
    "position": [0.0, 0.0, 0.0],
    "rotation_xyzw": [0.0, 0.0, 0.0, 1.0]
  }
}
```

| Field | Type | Requirement and behavior |
| --- | --- | --- |
| `valid` | Boolean | Required for the entry to parse. Version 1 keeps every virtual device valid after startup, so the driver currently reports `true` to SteamVR regardless of this value. |
| `connected` | Boolean | Required for the entry to parse. Version 1 keeps every virtual device connected after startup, so the driver currently reports `true` to SteamVR regardless of this value. |
| `pose.position` | Three-number array | Required. Metres in driver pose space; every component must be finite and within `-10.0` to `10.0`. Y is additionally capped at `2.0`. |
| `pose.rotation_xyzw` | Four-number array | Required. Quaternion in XYZW order; values must be finite and its squared length must be from `0.5` through `1.5`. Accepted values are normalized. |

The `inputs` object is optional. Only `left_controller` and `right_controller`
input entries affect OpenVR controller state. Each member inside a controller
input entry is optional; omitted members use the defaults or fallbacks below.
An input entry is applied only when the matching controller also has a valid
entry under `devices` in the same datagram. If that device entry is present but
its input entry is absent, ordinary buttons and axes reset to their defaults. If
the device entry itself is absent or malformed, its previous pose and inputs are
retained.

| Input field | Type | Range/default | Driver behavior |
| --- | --- | --- | --- |
| `trigger_click` | Boolean | Default `false` | Drives `/input/trigger/click`. |
| `trigger_value` | Number | Clamped to `0.0`–`1.0`; defaults to `1.0` when `trigger_click` is true, otherwise `0.0` | Drives `/input/trigger/value`. |
| `menu_click` | Boolean | Default `false` | Drives `/input/application_menu/click`. |
| `system_click` | Boolean | Default `false` | Accepted and emitted for version-1 compatibility, but the current driver exposes no `/input/system/click` component, so it has no OpenVR effect. |
| `a_click` | Boolean | Default `false` | Drives `/input/a/click`. |
| `b_click` | Boolean | Default `false` | Drives `/input/b/click`. |
| `grip_click` | Boolean | Default `false` | Drives `/input/grip/click` and `/input/grip/touch`. |
| `grip_value` | Number | Clamped to `0.0`–`1.0`; defaults to `1.0` when `grip_click` is true, otherwise `0.0` | Drives both `/input/grip/value` and `/input/grip/force`. |
| `joystick_x`, `joystick_y` | Number | Each clamped to `-1.0`–`1.0`; default `0.0` | Drive `/input/thumbstick/x` and `/input/thumbstick/y`. |
| `trackpad_x`, `trackpad_y` | Number | Each clamped to `-1.0`–`1.0`; each omitted axis falls back to its corresponding joystick axis | Drive `/input/trackpad/x` and `/input/trackpad/y`. |
| `finger_bends` | Object | Optional; omission retains the last applied bends (open until the first valid object) | Drives the hand skeleton. If present, all five members below must parse successfully or the entire object is ignored. |
| `finger_bends.thumb`, `.index`, `.middle`, `.ring`, `.pinky` | Number | Each clamped to `0.0` (open) through `1.0` (fully bent) | Applied to the corresponding finger in `/input/skeleton/left` or `/input/skeleton/right`. |

## Validation

The driver rejects packets with:

- missing or wrong `version`
- malformed JSON shape
- datagram size greater than or equal to 8192 bytes
- zero valid recognized devices

A device entry is ignored if it has:

- missing required fields
- non-finite position or quaternion values
- absolute position component above `10.0 m`
- quaternion squared length outside the accepted `0.5` to `1.5` range

Accepted quaternions are normalized before use.

## Y Clamp

All devices are clamped to a maximum Y of `2.0 m`:

```cpp
position.y = std::min(position.y, 2.0f);
```

The clamp applies to Y. The companion UI clamps before serialization. The native driver clamps again after packet validation and rate-limits repeated clamp warnings.

## Pose Liveness

The driver does not time out device poses. All six virtual devices start connected
and valid at their neutral poses before the first packet. When packets arrive,
the latest valid packet for each device updates its pose and controller inputs.
If packets stop, SteamVR continues to see the device connected and valid at its
last pose.

Accepted device samples are reported as connected, valid, and
`TrackingResult_Running_OK`.

## Controller Inputs

The driver exposes Valve Index-compatible controller components:

```text
/input/trigger/click
/input/trigger/value
/input/application_menu/click
/input/a/click
/input/b/click
/input/grip/click
/input/grip/touch
/input/grip/value
/input/grip/force
/input/thumbstick/x
/input/thumbstick/y
/input/trackpad/x
/input/trackpad/y
/input/skeleton/left
/input/skeleton/right
```

`/input/grip/touch` and `/input/grip/force` do not have separate wire fields;
they are derived from `grip_click` and `grip_value`, respectively. Skeleton
components likewise derive their transforms from `finger_bends`.
