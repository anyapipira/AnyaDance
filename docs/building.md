# Building

**English** | [简体中文](building.zh-CN.md)

## Prerequisites

- Windows 10 or newer
- Visual Studio 2022 or Build Tools with Desktop development with C++
- CMake 3.22 or newer
- SteamVR for runtime testing

## Default Build

```powershell
.\scripts\build_driver.ps1
```

The first default build downloads pinned source archives:

- Valve OpenVR SDK `v2.2.3`
- Dear ImGui `v1.90.9`

## Local Dependencies

Passing a dependency root uses that checkout directly:

```powershell
.\scripts\build_driver.ps1 `
  -OpenVRSdkRoot F:\deps\openvr `
  -ImguiRoot F:\deps\imgui
```

## Build Outputs

```text
build/out/anyadance/AnyaDance.exe
build/out/anyadance/driver.vrdrivermanifest
build/out/anyadance/bin/win64/driver_anyadance.dll
build/out/anyadance/resources/input/anyadance_controller_profile.json
build/out/anyadance/resources/input/anyadance_hmd_profile.json
build/out/anyadance/resources/settings/default.vrsettings
build/out/anyadance/scripts/uninstall.ps1
build/out/anyadance/scripts/unregister_driver.ps1
build/out/anyadance/scripts/restart_steamvr.ps1
build/out/anyadance/scripts/common_steamvr.ps1
build/out/anyadance/LICENSE
build/out/anyadance/NOTICE
build/out/anyadance/THIRD_PARTY_NOTICES.md
build/out/anyadance/README.md
build/out/anyadance/README.zh-CN.md
```

The UI builds into the driver folder, so `build/out/anyadance/` is one
self-contained bundle with the license/notices: the exe registers its own folder
as the SteamVR driver.

## Tests

```powershell
ctest --test-dir build -C Release --output-on-failure
```

For a dependency-free test-only build:

```powershell
cmake -S . -B build-tests -DANYADANCE_BUILD_DRIVER=OFF -DANYADANCE_BUILD_UI=OFF
cmake --build build-tests --config Debug
ctest --test-dir build-tests -C Debug --output-on-failure
```
