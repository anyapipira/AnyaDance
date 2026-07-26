# Architecture

**English** | [简体中文](architecture.zh-CN.md)

AnyaDance has three runtime parts:

1. `anyadance_core`: dependency-free C++17 logic shared by the driver, UI, and tests.
2. `driver_anyadance.dll`: SteamVR/OpenVR server driver loaded by SteamVR.
3. `AnyaDance.exe`: Dear ImGui Win32/DX11 companion UI.

## Core Library

The core library owns data structures and testable behavior:

- device constants and public identifiers
- vector/quaternion helpers using XYZW quaternions for wire poses
- UDP protocol parsing and serialization
- `Y <= 2.0 m` safety clamp
- canonical T-pose reset
- keyboard input mapping (every key maps directly to a held button or axis)
- mouse manipulation math
- log ring buffer and manipulation coalescing

## Native Driver

The SteamVR driver registers up to six devices:

- HMD
- left and right `knuckles` controllers
- hip, left foot, and right foot generic trackers

The driver starts a loopback UDP receiver on `127.0.0.1:39570`. Valid samples update per-device pose state. Invalid packets are ignored. The driver clamps device Y to the supported `0–25 m` range after validation as a defense in depth.

All devices start valid at neutral poses and remain valid if packets stop. The driver reports the latest accepted pose as connected, valid, and `TrackingResult_Running_OK`.

## UI

The companion UI has a UI thread and a streaming thread. The UI thread owns ImGui rendering, keyboard polling while focused, and mouse manipulation. The streaming thread copies synchronized state, serializes once per frame, and sends a full six-device frame at 60 Hz. The UI log shows state-changing sends; unchanged keepalive packets stay quiet.
