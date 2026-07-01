# 安装

[English](installation.md) | **简体中文**

## 安装发布版本

环境要求：

- Windows 10 或更高版本
- SteamVR
- [Microsoft Visual C++ Redistributable for Visual Studio 2015-2022 (x64)](https://aka.ms/vc14/vc_redist.x64.exe)

从 [GitHub Releases](https://github.com/anyapipira/AnyaDance/releases) 下载 `AnyaDance-<版本>-windows-x64.zip`，然后将完整的 `anyadance` 文件夹解压到固定位置。请勿直接从 ZIP 内运行 `AnyaDance.exe`。

运行 `AnyaDance.exe`，接受免责声明，然后依次点击 **注册驱动** 和 **重启 SteamVR**。应用会注册其自身所在的文件夹，因此驱动处于注册状态时不要移动或删除该文件夹。

## 从源代码安装

先构建，然后使用应用内的 **注册驱动** 与 **重启 SteamVR** 按钮，或运行：

```powershell
.\scripts\build_driver.ps1
.\scripts\register_driver.ps1
.\scripts\restart_steamvr.ps1
```

被注册的文件夹通常是：

```text
build\out\anyadance
```

## 全虚拟设置

`register_driver.ps1` 还会应用全虚拟设置，使虚拟头显成为活动头显：

```text
启用虚拟头显
启用控制器
启用追踪器
启用 activateMultipleDrivers
forcedDriver = anyadance
requireHmd = true
```

## 备份

`register_driver.ps1` 在写入这些设置之前，会先备份当前的 SteamVR 设置文件。默认备份路径为：

```text
%LOCALAPPDATA%\AnyaDance\steamvr.vrsettings.backup
```

首次注册会写入备份，重复注册会保留那份原始、未被修改的设置文件。`unregister_driver.ps1` 会还原该备份并随后清除它，以便下次注册重新捕获一份全新的基线。

更改注册或启动设置后需要重启 SteamVR。

## 取消注册

如果使用发布版本，请在应用中依次点击 **取消注册驱动** 与 **重启 SteamVR**。移动或删除解压后的文件夹前必须先完成此操作。

如果从源代码安装，对应的脚本为：

```powershell
.\scripts\unregister_driver.ps1
.\scripts\restart_steamvr.ps1
```
