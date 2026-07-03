# 构建

[English](building.md) | **简体中文**

## 前置条件

- Windows 10 或更高版本
- Visual Studio 2022，或带有“使用 C++ 的桌面开发”工作负载的生成工具
- CMake 3.22 或更高版本
- 用于运行时测试的 SteamVR

## 默认构建

```powershell
.\scripts\build_driver.ps1
```

首次默认构建会下载固定版本的源码归档：

- Valve OpenVR SDK `v2.2.3`
- Dear ImGui `v1.90.9`

## 本地依赖

传入依赖根路径会直接使用该检出：

```powershell
.\scripts\build_driver.ps1 `
  -OpenVRSdkRoot F:\deps\openvr `
  -ImguiRoot F:\deps\imgui
```

## 构建产物

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
```

UI 会构建进驱动文件夹，因此 `build/out/anyadance/` 是一个自包含的捆绑包：exe 会把自身所在的文件夹注册为 SteamVR 驱动。

## 测试

```powershell
ctest --test-dir build -C Release --output-on-failure
```

仅构建测试、无需依赖：

```powershell
cmake -S . -B build-tests -DANYADANCE_BUILD_DRIVER=OFF -DANYADANCE_BUILD_UI=OFF
cmake --build build-tests --config Debug
ctest --test-dir build-tests -C Debug --output-on-failure
```
