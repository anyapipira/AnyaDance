# Troubleshooting

**English** | [简体中文](troubleshooting.zh-CN.md) | [日本語](troubleshooting.ja.md)

## SteamVR Does Not See The Driver

Run:

```powershell
.\scripts\register_driver.ps1
```

Confirm that `%LOCALAPPDATA%\openvr\openvrpaths.vrpath` contains the staged `build\out\anyadance` path under `external_drivers`. Restart SteamVR after registration changes.

## Virtual HMD Does Not Appear

Re-run the registration script, which also applies the fully virtual settings:

```powershell
.\scripts\register_driver.ps1
.\scripts\restart_steamvr.ps1
```

Disconnect the physical HMD before startup so AnyaDance's virtual HMD becomes the active HMD.

## Display Is Blurry Or Unclear

The virtual HMD renders at `1920x1080` per eye by default. If the image looks soft or blurry, raise the per-eye render resolution: edit the `driver_anyadance` section of `steamvr.vrsettings`, set `headset_render_width` and `headset_render_height` higher (for example `3840x2160` for 4K), and restart SteamVR. See the HMD Render Resolution section of the README and [docs/device-model.md](device-model.md).

> **Note:** Raising the per-eye render resolution greatly increases GPU load. The headset renders both eyes separately (effectively two screens), so the increase applies to each eye: 4K (`3840x2160`) is about four times the pixels of 1080p, and that cost is paid for both eyes. Any aspect ratio works — the projection adapts to the configured resolution, so nothing is stretched.

## Controllers Or Trackers Do Not Move

Controllers and trackers stay connected and valid even when packets stop. If they do not move, start `AnyaDance.exe` and confirm it is streaming the full six-device frame at 60 Hz.

## HMD Goes Grey When the UI Closes

With the virtual HMD enabled, the driver keeps the HMD connected and valid when packets stop. Check SteamVR logs for `[anyadance]` messages and verify that the loaded DLL is `driver_anyadance.dll` from the current build output.

## Key Bindings Do Not Work

The UI only reads keyboard state while its window is focused. It also disables key mappings while an ImGui text edit widget is active. Click the UI window and try again.

## SteamVR Settings Look Wrong

The registration script backs up settings to:

```text
%LOCALAPPDATA%\AnyaDance\steamvr.vrsettings.backup
```

`unregister_driver.ps1` restores that backup and clears the backup file. For a manual restore, inspect the backup first, then copy it back. Restart SteamVR after changing `steamvr.vrsettings`.

## Packet Is Ignored

Check:

- `version` is `1`
- datagram is smaller than 8192 bytes
- device ID is one of the six recognized IDs
- all required fields are present for the device
- position values are finite and within +/-30 m; Y is within 0–25 m for the HMD and -30–25 m for the other five devices
- quaternion order is XYZW and squared length is between 0.5 and 1.5
