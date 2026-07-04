# Installation

**English** | [简体中文](installation.zh-CN.md)

## Install A Release

Requirements:

- Windows 10 or newer
- SteamVR
- [Microsoft Visual C++ Redistributable for Visual Studio 2015-2022 (x64)](https://aka.ms/vc14/vc_redist.x64.exe)

Download `AnyaDance-<version>-windows-x64.zip` from [GitHub Releases](https://github.com/anyapipira/AnyaDance/releases), then extract the complete `anyadance` folder to a permanent location. Do not run `AnyaDance.exe` from inside the ZIP.

Run `AnyaDance.exe`, acknowledge the safety notice, and use **Register Driver** followed by **Restart SteamVR**. The application registers its own folder, so do not move or delete that folder while the driver remains registered.

## Install From Source

Build first, then either use the application's **Register Driver** and **Restart SteamVR** buttons or run:

```powershell
.\scripts\build_driver.ps1
.\scripts\register_driver.ps1
.\scripts\restart_steamvr.ps1
```

The registered folder is normally:

```text
build\out\anyadance
```

## Fully Virtual Settings

`register_driver.ps1` also applies the fully virtual settings, so the virtual HMD is the active HMD:

```text
virtual HMD enabled
controllers enabled
trackers enabled
activateMultipleDrivers enabled
forcedDriver = anyadance
requireHmd = true
```

## Backups

`register_driver.ps1` backs up the current SteamVR settings file before writing these settings. The default backup path is:

```text
%LOCALAPPDATA%\AnyaDance\steamvr.vrsettings.backup
```

The first registration writes the backup, and repeated registration keeps that original pristine settings file. `unregister_driver.ps1` restores that backup and then clears it, so the next registration captures a fresh baseline.

Changing registration or startup settings requires restarting SteamVR.

## Unregister

For a release installation, click **Unregister Driver**, then confirm the **Restart SteamVR** prompt. Do this before moving or deleting the extracted folder.

For a source installation, the equivalent scripts are:

```powershell
.\scripts\uninstall.ps1
```

`uninstall.ps1` saves a timestamped recovery snapshot under
`%LOCALAPPDATA%\AnyaDance\uninstall-recovery`, unregisters the driver, restores
the original settings backup when available, repairs known AnyaDance overrides
when that backup is missing, verifies removal, and restarts SteamVR. Pass
`-NoRestart` to leave SteamVR stopped. It does not delete the application files.
