# 故障排查

[English](troubleshooting.md) | **简体中文**

## SteamVR 看不到驱动

运行：

```powershell
.\scripts\register_driver.ps1
```

确认 `%LOCALAPPDATA%\openvr\openvrpaths.vrpath` 在 `external_drivers` 下包含暂存的 `build\out\anyadance` 路径。更改注册后请重启 SteamVR。

## 虚拟头显不出现

重新运行注册脚本，它也会应用全虚拟设置：

```powershell
.\scripts\register_driver.ps1
.\scripts\restart_steamvr.ps1
```

启动前请断开物理头显，使 AnyaDance 虚拟头显成为活动头显。

## 画面模糊 / 不清晰

虚拟头显默认每只眼睛以 `1920x1080` 渲染。如果画面看起来模糊或不够清晰，可以提高每眼渲染分辨率：编辑 `steamvr.vrsettings` 的 `driver_anyadance` 小节，将 `headset_render_width` 与 `headset_render_height` 调高（例如 4K 为 `3840x2160`），然后重启 SteamVR。具体步骤见 README 的“HMD 渲染分辨率”一节与 [docs/device-model.zh-CN.md](device-model.zh-CN.md) 的“虚拟头显显示”。

> **注意：** 提高每眼渲染分辨率会大幅增加 GPU 负担。头显需要为左右两只眼睛分别渲染（相当于两块屏幕），因此分辨率的提升会同时作用在两只眼睛上：例如 4K（`3840x2160`）的像素量约为 1080p 的四倍，再叠加双眼，开销十分可观。宽高比可以自由选择（例如与你的显示器保持一致）：投影会按所设的渲染分辨率自动适配，因此不会被拉伸。

## 控制器或追踪器不动

即使数据包停止，控制器与追踪器仍保持连接且有效。如果它们不动，请启动 `AnyaDance.exe` 并确认它正以 60 Hz 推送完整的六设备帧。

## UI 关闭时头显变灰

启用虚拟头显后，数据包停止时驱动会保持头显连接且有效。检查 SteamVR 日志中的 `[anyadance]` 消息，并确认加载的 DLL 是来自当前构建产物的 `driver_anyadance.dll`。

## 按键绑定不起作用

测试 UI 仅在其窗口聚焦时读取键盘状态。当 ImGui 文本编辑控件处于活动状态时，它也会禁用按键映射。点击 UI 窗口后再试。

## SteamVR 设置看起来不对

注册脚本会把设置备份到：

```text
%LOCALAPPDATA%\AnyaDance\steamvr.vrsettings.backup
```

`unregister_driver.ps1` 会还原该备份并清除备份文件。若要手动还原，请先检查备份，然后将其复制回去。更改 `steamvr.vrsettings` 后请重启 SteamVR。

## 数据包被忽略

检查：

- `version` 为 `1`
- 数据报小于 8192 字节
- 设备 ID 是六个可识别 ID 之一
- 该设备所有必需字段都存在
- 位置值为有限数值且位于 ±30 m 范围内；Y 位于 0–25 m 范围内
- 四元数顺序为 XYZW，且平方长度在 0.5 与 1.5 之间
