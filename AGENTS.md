# AGENTS.md

Guidance for AI coding agents working in this repository.

## Project

AnyaDance is a Windows-only toolkit for driving and animating a VRChat avatar's
full body (pose by hand, live drive, or MMD dance playback). At its core is a
SteamVR/OpenVR virtual-device driver plus a Dear ImGui companion UI. The driver
exposes six virtual devices (HMD, two controllers, hip and two foot trackers) and
receives pose/input frames over loopback UDP JSON at `127.0.0.1:39570`. The
companion UI (`AnyaDance.exe`) streams a six-device T-pose at 60 Hz.
Released under the Apache License 2.0 as part of Project Anya.

## Layout

- `src/core/` — shared, platform-light logic: command and driver-log protocols, frame state, freshness,
  T-pose math, manipulation, input state, UDP log, a small JSON DOM (`json`), and
  the MMD dance remap (`solved_motion` parses the Blender solve JSON,
  `mmd_retarget` remaps it onto the six devices), and the `.nya` clip format
  (`nya_format` serializes/parses device-level `DanceMotion` frames; a pose is a
  one-frame clip, an animation is many). Built as `anyadance_core` and exercised
  by the tests. Keep it free of OpenVR and Win32 so tests build it.
- `src/driver/` — the SteamVR driver DLL (`server_driver`, `virtual_device`,
  `udp_pose_receiver`, `driver_log_sender`). Links OpenVR.
- `src/ui/` — the Dear ImGui Win32/DX11 UI (`main.cpp`, `localization`,
  `driver_control`, `mmd_dance`). `mmd_dance` launches Blender headless with
  `scripts/blender_export_mmd.py` to solve a VMD against a model and reads back the
  single solved-motion JSON. `driver_control` registers/unregisters the driver by editing
  `openvrpaths.vrpath` and `steamvr.vrsettings` directly (no `vrpathreg` process),
  and restarts SteamVR (stops the vrserver/vrmonitor/vrcompositor processes, then
  relaunches via `steam://run/250820`). The driver root is the exe's own folder,
  so the UI ships inside the driver bundle and registers that folder; OpenVR
  loads `bin/win64/driver_anyadance.dll` (it cannot be a loose root DLL). The
  build outputs the exe into `out/anyadance/` for this reason, and the
  `package_driver` target zips that folder into `out/AnyaDance.zip`.
- `driver/resources/` — `default.vrsettings`, input profiles, and images shipped
  with the driver.
- `scripts/` — PowerShell helpers (build, register, unregister, restart).
- `docs/` — architecture, device model, protocol, building, installation,
  troubleshooting, MMD dance (`mmd-dance.md`).
- `tests/` — single-binary test runner (`anyadance_tests`).

## Build / test

Windows, PowerShell, CMake + Visual Studio 2022, C++17.

```powershell
.\scripts\build_driver.ps1                                   # configure + build (fetches OpenVR + ImGui)
cmake --build build --config Release --target anyadance_tests
ctest --test-dir build -C Release --output-on-failure
```

Pass `-OpenVRSdkRoot` / `-ImguiRoot` to build against local checkouts; supplying
a root turns off fetching for that dependency.

## SteamVR

```powershell
.\scripts\register_driver.ps1     # register + write the fully-virtual vrsettings (backs up first)
.\scripts\unregister_driver.ps1   # unregister + restore the backup
.\scripts\restart_steamvr.ps1     # restart SteamVR
```

## CI / release

- GitHub Actions CI (`.github/workflows/ci.yml`) configures, builds, and runs
  CTest on every push and pull request. It pins `runs-on: windows-2022` on
  purpose — `windows-latest` now resolves to an image without Visual Studio 2022,
  which breaks the `Visual Studio 17 2022` generator. Do not switch it back.
- Pushing a `v*` tag triggers the release workflow
  (`.github/workflows/release.yml`): it builds, tests, then publishes a GitHub
  Release with `AnyaDance-<tag>-windows-x64.zip` (the packaged
  `out/AnyaDance.zip`, renamed). It is idempotent — it uploads the asset
  even if the release already exists.

## Conventions

- C++17, Windows-only. Define `NOMINMAX` before `windows.h` (the UI does) so
  `std::min`/`std::max`/`std::clamp` are not shadowed.
- UDP protocol is version 1; keep it backward compatible unless
  `kProtocolVersion` changes. Shared constants live in `src/core/constants.h`.
- Preserve OpenVR-required names: `HmdDriverFactory`, `driver.vrdrivermanifest`,
  `knuckles`, and OpenVR input paths.
- Fully virtual mode makes the AnyaDance virtual HMD the active HMD.
- All six devices start connected and valid at neutral poses. Accepted packets
  update the latest pose and controller inputs. If packets stop, SteamVR
  continues seeing each device connected, valid, and `TrackingResult_Running_OK`
  at the last accepted pose. The HMD reports pose only, and is the active
  virtual HMD in fully virtual mode.
- All devices set `Prop_IgnoreMotionForStandby_Bool` so SteamVR does not idle a
  held-still virtual device into standby/powersave.
- Device Y shares the `kMaxDeviceY` ceiling of `25 m`, but the floor is
  per-device: only the HMD is held above the ground plane (`kMinHmdY`, `0 m`),
  because it is the play-space head. The other five may go below it and are
  bounded only by `kMinTrackerY` (`-30 m`, the shared position range). Use
  `MinDeviceY(index)` / `ClampDeviceY(index, y)` rather than a single constant.
  Clamping happens in the UI before sending and in the driver after packet
  validation.
- The UDP log is always recorded in English. UI strings are localized through the
  row-per-string table in `src/ui/localization.*`; access with `Tr(Text::...)`.
  Adding a string is one enum value plus one table row (a `static_assert` guards
  the row count).
- Driver command reports use logging protocol version 1 and default to the
  loopback-only multicast group `239.255.39.71:39571`. Reporting is best-effort
  and non-blocking. When the UI joins that group, driver reports are the source
  of truth for successful commands;
  keep UI-side successful-send rows suppressed to avoid duplicates.
- The driver log group carries more than one event type. Dispatch on the `event`
  field (`ParseDriverLogBytes`) rather than assuming a command report. Haptic
  requests to the two controllers are reported as `haptic_vibration`, gated by
  `haptic_log_enabled` (default true); the `/output/haptic` component is always
  created, so the switch never changes what SteamVR sees. The driver observes
  haptics only — it has no motor and plays nothing back.
- The driver reports a command only when it differs from the last reported one
  (`SamePoseCommand`), because a held pose repeats at the stream rate. Keep this
  a change filter, not a rate limit: every distinct command still goes out, and
  absorbed repeats are counted in the next report's optional `suppressed` field.
- Add focused tests in the relevant `tests/test_*.cpp` file for protocol,
  safety, freshness, T-pose, input, manipulation, and log behavior. Update
  `docs/` when behavior changes.
- Do not commit build artifacts (`build/`), PDBs, SteamVR backups, or local
  settings; `.gitignore` covers these.

## Contributing

- See `CONTRIBUTING.md` for the build/test gate and `CODE_OF_CONDUCT.md` for the
  full policy.
- Pull requests are squash-merged, so the squash commit that lands on `main`
  comes from the PR title and description — write both in English, since they are
  the permanent history. Work-in-progress commit messages on a branch are not
  policed.
- Write code comments in English. User-facing UI strings (localized via
  `src/ui/localization.*`) and translated docs (`README.zh-CN.md`) are exempt.

## Licensing

Apache-2.0 (`LICENSE` + `NOTICE`). Dependencies: Dear ImGui (MIT) and Valve
OpenVR (BSD 3-Clause); full texts are reproduced in `THIRD_PARTY_NOTICES.md` and
must travel with distributed binaries.
