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

## Driver Command Logging

The driver emits a best-effort telemetry datagram when a processed pose datagram
changes what the command asks for. It sends one datagram to a local IPv4
multicast group, so the companion UI and multiple independent application
processes can receive the same report:

```text
UDP
multicast group: 239.255.39.71
port: 39571
interface: 127.0.0.1 (loopback only)
UTF-8 JSON
logging version: 1
maximum logging datagram size: 65507 bytes
```

The same group also carries haptic events (see [Haptic Events](#haptic-events)).
A reader must dispatch on the `event` field rather than assume every datagram is
a command report.

The logging group is independent of the command receiver. Configure it in
the `driver_anyadance` section of `steamvr.vrsettings` and restart SteamVR to
apply a change:

```json
"driver_anyadance": {
    "command_log_enabled": true,
    "command_log_multicast_group": "239.255.39.71",
    "command_log_port": 39571,
    "haptic_log_enabled": true
}
```

`command_log_multicast_group` must be an IPv4 multicast address. Every local
subscriber joins that group and port independently; changing the group lets a
separate set of listeners consume the reports. `command_log_enabled` controls
report generation. These values default to the entries shipped in
`resources/settings/default.vrsettings`. The driver always selects the loopback
interface and multicast TTL 0, so reports do not leave the machine.

On Windows, each subscriber must enable `SO_REUSEADDR` before binding, bind UDP
port `39571` on `0.0.0.0`, and join `239.255.39.71` on the `127.0.0.1`
interface. Multiple processes following those steps each receive a copy of a
report, subject to normal UDP loss, duplication, and reordering.

The included PowerShell listener is a working reference implementation:

```powershell
# Monitor reports until Ctrl+C.
.\scripts\listen_driver_log.ps1

# Launch three independent listener processes and require every one to receive
# the same locally generated multicast probe.
.\scripts\listen_driver_log.ps1 -Validate -ListenerCount 3
```

Each packet uses this shape:

```json
{
  "version": 1,
  "event": "command_processed",
  "sequence": 42,
  "suppressed": 613,
  "source": {
    "host": "127.0.0.1",
    "port": 54321
  },
  "command": {
    "protocol": "pose_frame",
    "bytes": 347,
    "accepted": true,
    "devices": ["hmd", "left_controller"],
    "y_clamped": ["hmd"],
    "payload": "{\"version\":1,...}"
  },
  "detail": "accepted 2 device entries; clamped Y for 1"
}
```

| Field | Meaning |
| --- | --- |
| `version` | Driver logging protocol version. Version 1 is current. |
| `event` | Event type. `command_processed` for this shape; see [Haptic Events](#haptic-events) for `haptic_vibration`. Dispatch on this field. |
| `sequence` | Monotonic report number for the current driver receiver lifetime. It restarts at driver startup. Reports are numbered, not commands, so gaps do not appear when repeats are suppressed. |
| `suppressed` | Identical commands the driver absorbed between the previous report and this one. `0` means the previous command was not repeated. Optional: a sender that reports every command may omit it, and a reader that omits it treats it as `0`. |
| `source.host`, `source.port` | Endpoint that sent the original pose datagram to port `39570`. |
| `command.protocol` | Command protocol name; version 1 uses `pose_frame`. |
| `command.bytes` | Byte length of the original command datagram. |
| `command.accepted` | `true` when at least one recognized device entry was accepted and stored. |
| `command.devices` | Recognized device entries accepted from this datagram. |
| `command.y_clamped` | Accepted device entries whose Y value was clamped. |
| `command.payload` | Original command datagram as a JSON string, retained for inspection and resend. |
| `detail` | Compact English processing summary. |

The report socket is non-blocking and telemetry delivery is intentionally
lossy. A full socket buffer, invalid multicast configuration, or local delivery
failure drops the report while command processing continues. Serialization and
sending run on the UDP receiver thread, outside SteamVR's `RunFrame` path. The
driver sends only once per reported command regardless of subscriber count.

### Held Commands

A sender streaming a held pose repeats the identical command at its stream rate,
so reporting every datagram would emit `kStreamRateHz` reports per second while
nothing changes. The driver reports a command only when it differs from the last
one it reported. A command is a repeat when the acceptance outcome, the set of
recognized device entries, the set of clamped entries, and every present device's
pose and controller inputs all match. Arrival time is not part of the comparison,
and the comparison is exact — a held pose is re-serialized from unchanged state
and arrives identical, while slow deliberate motion still reports every frame.
Repeated datagrams that fail validation are compared by their bytes instead,
because a rejected datagram leaves no trustworthy parsed state.

Suppression is a change filter, not a rate limit: distinct commands are always
reported, so a listener recording a moving sequence receives every frame of it.
The count of absorbed repeats rides along on the next report's `suppressed`
field, which keeps a held pose distinguishable from a stalled sender.

The UI's **Monitor driver commands** switch joins or leaves the default
multicast group immediately. While the listener is joined, driver reports are the source of truth
for successful command rows and the UI suppresses its own successful-send rows.
Local socket failures remain visible because the driver cannot report a command
it did not receive. Rapid accepted reports from the same sender are coalesced in
100 ms display windows; rejected reports remain individual rows. Coalescing is
limited to the UI display and applies on top of the driver's held-command
suppression, so a moving pose still produces one row per 100 ms while a held pose
produces none. A row whose report carried a non-zero `suppressed` count says how
many identical commands were held before it.

## Haptic Events

The two virtual controllers expose an `/output/haptic` component, the way a real
Index controller does, so SteamVR routes haptic requests to them. The driver has
no motor and plays nothing back; it only reports the request so external tools
can react to it — driving indicators, or forwarding to physical hardware.

Reports go to the same multicast group and port as command reports, so a listener
joins one group to observe both. `haptic_log_enabled` in the `driver_anyadance`
section gates them and defaults to `true`; the `/output/haptic` component is
always created, so turning reporting off does not change what SteamVR or a game
sees. Reporting is best-effort and non-blocking on SteamVR's `RunFrame` thread:
a full socket buffer or an absent listener drops the report rather than delaying
a frame. No acknowledgement is expected or read.

```json
{
  "version": 1,
  "event": "haptic_vibration",
  "sequence": 7,
  "device": "right_controller",
  "haptic": {
    "duration_seconds": 0.125,
    "frequency_hz": 160.5,
    "amplitude": 0.75
  }
}
```

| Field | Meaning |
| --- | --- |
| `version` | Driver logging protocol version. Version 1 is current. |
| `event` | Always `haptic_vibration` for this shape. |
| `sequence` | Monotonic report number, counted separately from `command_processed` reports because the two streams are produced independently. It restarts at driver startup. |
| `device` | Controller asked to vibrate: `left_controller` or `right_controller`. |
| `haptic.duration_seconds` | Requested pulse length in seconds. `0` is a legitimate stop request. |
| `haptic.frequency_hz` | Requested vibration frequency in hertz. |
| `haptic.amplitude` | Requested strength, `0.0` through `1.0`. |

Values are passed through as SteamVR supplied them; the driver applies no policy
of its own beyond rejecting non-finite numbers. Because the two event types share
a group and number their sequences independently, a reader must dispatch on
`event` before reading any other field.

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
| `pose.position` | Three-number array | Required. Metres in driver pose space; every component must be finite and within `-30.0` to `30.0`. Y is clamped to `0.0`–`25.0` for `hmd` and to `-30.0`–`25.0` for every other device. |
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
| `system_click` | Boolean | Default `false` | Preserved in version-1 packets. |
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
- absolute position component above `30.0 m`
- quaternion squared length outside the accepted `0.5` to `1.5` range

Accepted quaternions are normalized before use.

## Position Limits

All position components must be finite and within `±30 m`. Device Y is further
capped at `25 m` by both the companion UI and the driver.

The Y floor is per-device. Only `hmd` is held at or above `0 m`: it is the
play-space head, and placing it below the ground plane puts the view
underground. The other five devices legitimately go below it — a foot passing
under the floor plane, a hip in a floor move, or a solved motion whose origin
sits above the ground — so their Y is bounded only by the shared `±30 m`
position range.

| Device | Y range |
| --- | --- |
| `hmd` | `0 m` to `25 m` |
| `left_controller`, `right_controller`, `hip`, `left_foot`, `right_foot` | `-30 m` to `25 m` |

## Pose Liveness

All six virtual devices start connected and valid at their neutral poses before
the first packet. When packets arrive,
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

`grip_click` drives `/input/grip/touch`, and `grip_value` drives
`/input/grip/force`. Skeleton components derive their transforms from
`finger_bends`.
