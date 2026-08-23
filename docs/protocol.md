# UDP Protocol

**English** | [简体中文](protocol.zh-CN.md) | [日本語](protocol.ja.md)

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

The same group carries more than one kind of event. Every datagram shares a
common envelope and is identified by its `event` field, which is the field a
receiver filters on. See [Event Schema](#event-schema) for the envelope, the
event registry, and the compatibility rules that keep both stable.

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

The included PowerShell listener is a working reference implementation:

```powershell
# Monitor reports until Ctrl+C.
.\scripts\listen_driver_log.ps1

# Launch three independent listener processes and require every one to receive
# the same locally generated multicast probe.
.\scripts\listen_driver_log.ps1 -Validate -ListenerCount 3
```

## Writing A Receiver

Any local process can subscribe. Nothing needs to be registered with the driver,
and the companion UI does not need to be running — the driver multicasts whether
or not anyone is listening, and its **Monitor driver commands** switch only
controls whether the UI itself joins.

Four socket steps, in this order. Each one is a silent failure if skipped: the
socket still opens and binds, and no datagram ever arrives.

1. **Create a UDP socket** (`AF_INET`, `SOCK_DGRAM`).
2. **Set `SO_REUSEADDR` before binding.** Several processes share this port by
   design. Without it, whichever process starts second either fails to bind or
   receives nothing.
3. **Bind port `39571` on `0.0.0.0`**, not on the group address. Binding the
   multicast address itself works on some platforms and not on Windows.
4. **Join `239.255.39.71` on the `127.0.0.1` interface** (`IP_ADD_MEMBERSHIP`).
   The interface matters: the driver sends from loopback with TTL 0, so a
   membership on any other interface never sees the traffic.

Then read datagrams, decode UTF-8, parse JSON, and filter on `event`. This
receiver handles both current event types, skips one it does not know, and
reports loss from the sequence numbering:

```python
import datetime
import json
import socket
import struct

GROUP = "239.255.39.71"
PORT = 39571
INTERFACE = "127.0.0.1"

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
# Before bind: lets other receivers share the port.
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
# Bind the port on any address, not on the group address.
sock.bind(("", PORT))
# Join on the loopback interface: the driver sends with TTL 0 from 127.0.0.1.
sock.setsockopt(
    socket.IPPROTO_IP,
    socket.IP_ADD_MEMBERSHIP,
    struct.pack("=4s4s", socket.inet_aton(GROUP), socket.inet_aton(INTERFACE)),
)

expected = None
while True:
    datagram, _ = sock.recvfrom(65507)
    try:
        event = json.loads(datagram.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        continue

    if event.get("version") != 1:
        continue  # a version this receiver does not know

    sequence = event.get("sequence")
    if expected is not None and sequence > expected:
        print(f"  (lost {sequence - expected} event(s))")
    expected = sequence + 1

    # Driver wall clock. Absent from a sender predating the field.
    stamp = event.get("timestamp_ms")
    when = (
        datetime.datetime.fromtimestamp(stamp / 1000, datetime.timezone.utc)
        .astimezone()
        .strftime("%H:%M:%S.%f")[:-3]
        if stamp
        else "--:--:--.---"
    )

    name = event.get("event")
    if name == "haptic_vibration":
        haptic = event["haptic"]
        print(
            f"{when} #{sequence} haptic {event['device']}: "
            f"{haptic['duration_seconds']:.3f}s "
            f"{haptic['frequency_hz']:.1f}Hz "
            f"amplitude {haptic['amplitude']:.2f}"
        )
    elif name == "command_processed":
        command = event["command"]
        print(
            f"{when} #{sequence} command from {event['source']['host']}: "
            f"{'accepted' if command['accepted'] else 'rejected'} - {event['detail']}"
        )
    else:
        # An event type added after this receiver was written. The envelope is
        # common to every event, so it is still safe to read and skip.
        print(f"{when} #{sequence} {name} - {event['detail']}")
```

That example is deliberately minimal. Before relying on one in production, read
[Ordering And Delivery](#ordering-and-delivery) and [Schema
Stability](#schema-stability): a receiver should also place events by `sequence`
rather than arrival and drop duplicated ones, which the example does not do.

### No datagrams arriving

The socket opening cleanly proves nothing, so work down this list:

- **Is the driver running?** Reports only exist while SteamVR has the driver
  loaded. Check `driver_anyadance` in the SteamVR web console, or look for the
  `Command logging multicasts on loopback` line in the SteamVR driver log.
- **Did you join on `127.0.0.1`?** Joining on the default or a LAN interface is
  the most common cause of a silent, empty socket.
- **Did you bind `0.0.0.0` rather than the group address?**
- **Was `SO_REUSEADDR` set before the bind, not after?**
- **Is reporting switched off?** `command_log_enabled` and `haptic_log_enabled`
  in `steamvr.vrsettings` both default to `true`; a change needs a SteamVR
  restart.
- **Does the group and port match?** If `command_log_multicast_group` or
  `command_log_port` was changed, subscribers must follow.
- **Is the traffic there at all?** Run
  `.\scripts\listen_driver_log.ps1 -Validate -ListenerCount 3`. It generates its
  own multicast probe, so it passing means the group works on this machine and
  the problem is in your receiver; it failing points at the machine's multicast
  configuration instead.

## Event Schema

Every datagram on the group is one event. All events share this envelope,
whatever their type, so a receiver can identify, filter, order, and display any
event — including one it does not recognize — without knowing its shape:

```json
{
  "version": 1,
  "event": "<event name>",
  "sequence": 42,
  "timestamp_ms": 1700000000123,
  "suppressed": 0,
  "detail": "compact English summary"
}
```

| Envelope field | Type | Meaning |
| --- | --- | --- |
| `version` | Number | Driver logging protocol version. Version 1 is current. An event whose `version` a receiver does not know must be ignored. |
| `event` | String | **The field to filter on.** Names the event type and therefore the shape of the type-specific fields alongside the envelope. |
| `sequence` | Number | Monotonic across *every* event the driver emits, not per event type, so events of all types share one order. Restarts at driver startup. A gap means a datagram was lost, or an event type was filtered out upstream. |
| `timestamp_ms` | Number | When the driver emitted the event: milliseconds since the Unix epoch, UTC. Taken from the wall clock alongside `sequence`, so it reflects when the event happened rather than when the datagram arrived. **Says "when", never "in what order"** — see below. Optional; absent means the sender supplied none. |
| `suppressed` | Number | Identical events absorbed between the previous report *of the same type* and this one. Optional; absent means `0`. Event types that never suppress always report `0`. |
| `detail` | String | Compact English summary of the event, always present, so a receiver can render any event as one line without decoding its type-specific fields. Human-readable only — never parse it. |

Alongside the envelope, each event carries fields specific to its type:

| `event` | Type-specific fields | Described in |
| --- | --- | --- |
| `command_processed` | `source`, `command` | [Driver Command Logging](#driver-command-logging) |
| `haptic_vibration` | `device`, `haptic` | [Haptic Events](#haptic-events) |

### Ordering And Delivery

Delivery is plain UDP, so the transport guarantees nothing: datagrams may be
lost, duplicated, or reordered. `sequence` is what makes that recoverable, and it
is why it counts globally rather than per event type.

The driver takes a sequence number as late as it can, immediately before handing
the datagram to the socket. It cannot take one atomically with the send: two
threads report on this group — the UDP receive thread for `command_processed` and
SteamVR's `RunFrame` thread for `haptic_vibration` — and serializing them against
each other would put a lock on both hot paths. So a small window remains in which
two events can leave in the opposite order to their numbers, independent of
anything the network does.

A receiver is therefore expected to order by `sequence`, not by arrival:

- **Reorder.** Hold or insert by `sequence` rather than appending on arrival. In
  practice a late event is a few places behind, so a bounded window is enough;
  the companion UI walks back up to 64 rows.
- **Deduplicate.** A repeated `sequence` is a duplicated datagram, not a new
  event. Drop it.
- **Detect loss.** A missing `sequence` is a dropped datagram. Because the
  counter is global, a gap is visible even to a receiver that filters for one
  event type — the numbers it keeps are simply sparse. Distinguish that from
  loss by tracking which numbers you filtered out yourself.
- **Do not order by `timestamp_ms` either.** It is a wall-clock reading, so it
  can step backwards across an NTP correction, a manual clock change, or a VM
  resume, and two events can land in the same millisecond. Use it to report
  *when* something happened and to measure intervals; use `sequence` to order,
  deduplicate, and detect loss. Where the two disagree, `sequence` is right.
- **Do not order by arrival time or by `detail`.** Only `sequence` reflects the
  order the driver produced the events in.

Nothing is retransmitted and no acknowledgement is read, so a lost event is gone.
This is telemetry: it is designed to be dropped under pressure rather than to
delay the driver.

### Schema Stability

These rules are a contract. A receiver that follows them keeps working across
AnyaDance releases without changes:

- **Filter on `event`.** It is the only field that determines the rest of the
  shape. Do not infer the type from the presence of another field.
- **Ignore unknown `event` values.** New event types may be added to this group
  in any release. An unrecognized event is a normal event a receiver does not
  handle, not a corrupt datagram or a protocol error. It still carries the full
  envelope, so it can be logged or displayed generically.
- **Ignore unknown fields.** New fields may be added to the envelope or to an
  existing event's type-specific object. A receiver must not fail on them.
- **Within a `version`, existing fields do not change.** A field's name, type,
  and meaning are fixed once released. Removing a field, repurposing one, or
  narrowing what it can hold requires a new `version`.
- **Absent optional fields mean their documented default**, not an error.
- **Do not parse `detail`.** Its wording is not part of the contract and may be
  reworded at any time. Read the type-specific fields instead.

The reference listener in `scripts/listen_driver_log.ps1` follows these rules and
is the shortest working example.

### `command_processed`

Each packet uses this shape:

```json
{
  "version": 1,
  "event": "command_processed",
  "sequence": 42,
  "suppressed": 613,
  "detail": "accepted 2 device entries; clamped Y for 1",
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
  }
}
```

The envelope fields are described in [Event Schema](#event-schema); for this
event `suppressed` counts identical commands absorbed since the previous
`command_processed` report, and `detail` summarizes the processing outcome. The
type-specific fields are:

| Field | Meaning |
| --- | --- |
| `source.host`, `source.port` | Endpoint that sent the original pose datagram to port `39570`. |
| `command.protocol` | Command protocol name; version 1 uses `pose_frame`. |
| `command.bytes` | Byte length of the original command datagram. |
| `command.accepted` | `true` when at least one recognized device entry was accepted and stored. |
| `command.devices` | Recognized device entries accepted from this datagram. |
| `command.y_clamped` | Accepted device entries whose Y value was clamped. |
| `command.payload` | Original command datagram as a JSON string, retained for inspection and resend. |

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

The UI's **Monitor driver commands** switch is off by default and is remembered
across launches in `%LOCALAPPDATA%\AnyaDance\ui_state.ini`. It joins or leaves the default
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
  "suppressed": 0,
  "detail": "0.125 s at 160.5 Hz, amplitude 0.75",
  "device": "right_controller",
  "haptic": {
    "duration_seconds": 0.125,
    "frequency_hz": 160.5,
    "amplitude": 0.75
  }
}
```

The envelope fields are described in [Event Schema](#event-schema). This event
never suppresses repeats, so `suppressed` is always `0`. The type-specific fields
are:

| Field | Meaning |
| --- | --- |
| `device` | Controller asked to vibrate: `left_controller` or `right_controller`. |
| `haptic.duration_seconds` | Requested pulse length in seconds. `0` is a legitimate stop request. |
| `haptic.frequency_hz` | Requested vibration frequency in hertz. |
| `haptic.amplitude` | Requested strength, `0.0` through `1.0`. |

Values are passed through as SteamVR supplied them; the driver applies no policy
of its own beyond rejecting non-finite numbers.

A receiver that only wants haptics keeps the datagrams whose `event` equals
`haptic_vibration` and ignores the rest, including event types added later.

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
