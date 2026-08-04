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
- driver command-log protocol parsing and serialization
- Per-device Y safety clamp (`0 <= Y <= 25.0 m` for the HMD, `-30.0 <= Y <= 25.0 m` for the rest)
- canonical T-pose reset
- keyboard input mapping (every key maps directly to a held button or axis)
- mouse manipulation math
- log ring buffer and manipulation coalescing

## Native Driver

The SteamVR driver registers up to six devices:

- HMD
- left and right `knuckles` controllers
- hip, left foot, and right foot generic trackers

The driver starts a loopback UDP receiver on `127.0.0.1:39570`. Valid samples update per-device pose state. Invalid packets are ignored. The driver clamps device Y after validation as a defense in depth: `0–25 m` for the HMD, `-30–25 m` for the other five devices, since only the HMD is held above the ground plane. After each parse attempt, the receiver thread sends one versioned command report through a non-blocking UDP socket to loopback multicast group `239.255.39.71:39571`. The group and port are configurable through the driver settings, while the interface and TTL keep delivery on the local machine. Multiple local application processes can join the group without increasing the number of driver sends.

All devices start valid at neutral poses and remain valid if packets stop. The driver reports the latest accepted pose as connected, valid, and `TrackingResult_Running_OK`.

## UI

The companion UI has a UI thread, a streaming thread, and an optional driver-log listener thread. The UI thread owns ImGui rendering, keyboard polling while focused, and mouse manipulation. The streaming thread copies synchronized state, serializes once per frame, and sends a full six-device frame at 60 Hz. The **Monitor driver commands** switch joins or leaves the logging multicast group at runtime. While joined, accepted and rejected driver reports feed the existing log and successful local-send rows are suppressed. Accepted 60 Hz reports coalesce into 100 ms display windows to keep log rendering bounded.
