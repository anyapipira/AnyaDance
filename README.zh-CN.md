# AnyaDance

<p>
  <img src="docs/images/ui_main_zh.png" alt="AnyaDance 主界面" width="50%"><img src="docs/images/ui_mmd_zh.png" alt="AnyaDance MMD 界面" width="50%">
</p>

[English](README.md) | **简体中文**

AnyaDance 是一个用于驱动与制作 VRChat 虚拟形象全身动作的 Windows 工具集——你可以手动摆姿势、实时驱动，或在其上播放 MMD 舞蹈。其核心是一个 SteamVR/OpenVR 虚拟设备驱动，外加一个向其推送数据的伴随程序（`AnyaDance.exe`）。驱动向 VRChat 全身追踪测试提供六个虚拟设备：

- 头显（HMD）
- 左控制器
- 右控制器
- 髋部追踪器
- 左脚追踪器
- 右脚追踪器

驱动通过 `127.0.0.1:39570` 上的 UDP JSON 接收姿态与控制器输入帧。伴随程序 `AnyaDance.exe` 一经打开，便会以 60 Hz 开始推送六个设备的 T 姿势。

AnyaDance 作为 **Project Anya**（由 Pipira 开发的更大型项目）的一部分发布。Project Anya 本身为专有软件。使用本驱动或在其基础上开发时如能注明 Project Anya，作者十分感谢。

## 免责声明

本软件仅供合法、经授权的测试与开发使用。

将虚拟设备或伪造的追踪数据输入正在运行的在线游戏，可能违反该游戏的服务条款，并可能被其反作弊系统检测到，从而导致你的账号被封禁或永久封停。

注册驱动会更改你的 SteamVR 配置：它会将 SteamVR 切换为全虚拟模式并写入 `steamvr.vrsettings`，因此在驱动处于注册状态期间，你的真实头显、控制器与追踪器将无法被追踪（注册时会创建备份，取消注册会将其还原）。虚拟头显还会通过 SteamVR 合成器持续渲染左右两只眼睛，会占用额外的 GPU 与 CPU 资源；提高渲染分辨率会进一步加大该负载。

你需自行承担使用本软件的全部风险。本软件按“原样”提供，不附带任何形式的担保；对于因使用或滥用造成的任何后果（包括账号封禁或失去访问权限），作者概不负责，亦不承担任何责任。你同意使作者免于因你的使用而产生的任何索赔。

本项目与 VRChat、Valve、Steam 或 SteamVR 无任何关联，也未获其认可。所有商标归各自所有者所有。

完整文本见 [DISCLAIMER.md](DISCLAIMER.md)。伴随 UI 也会显示此免责声明，并要求在首次启动时接受。具有法律约束力的版本以英文为准。

## 状态

代码可在 Windows 上使用 Visual Studio 2022 构建，且自动化测试通过。随附 UI 可注册 SteamVR 驱动、推送六个虚拟设备，并从同一构建产物打包驱动目录。

## 环境要求

- Windows 10 或更高版本
- 已安装 SteamVR
- [Microsoft Visual C++ Redistributable for Visual Studio 2015-2022 (x64)](https://aka.ms/vc14/vc_redist.x64.exe)

从源代码构建还需要：

- Visual Studio 2022 或带有“使用 C++ 的桌面开发”工作负载的 Visual Studio 生成工具
- CMake 3.22 或更高版本
- 默认首次构建需要网络访问，除非提供了本地依赖路径

固定版本的依赖：

- Valve OpenVR SDK `2.2.3`
- Dear ImGui `v1.90.9`

## 安装发布版本

1. 从 [GitHub Releases](https://github.com/anyapipira/AnyaDance/releases) 页面下载 `AnyaDance-<版本>-windows-x64.zip`。
2. 将完整的 `anyadance` 文件夹解压到固定位置。请勿直接从 ZIP 内运行程序；驱动处于注册状态时，也不要移动或删除该文件夹。
3. 运行 `AnyaDance.exe`，阅读并接受免责声明，然后点击 **注册驱动**。
4. 点击 **重启 SteamVR**。SteamVR 将以全虚拟模式启动，并使用 AnyaDance 的头显、控制器和追踪器。

使用完毕后，请先点击 **取消注册驱动** 并重启 SteamVR，再移动或删除该文件夹。这样会还原 SteamVR 设置备份，并恢复真实设备追踪。详见[安装](docs/installation.zh-CN.md)。

## 构建

```powershell
.\scripts\build_driver.ps1
```

输出：

```text
build\out\anyadance\AnyaDance.exe
build\out\anyadance\driver.vrdrivermanifest
build\out\anyadance\bin\win64\driver_anyadance.dll
build\out\anyadance\resources\...
build\out\anyadance\LICENSE
build\out\anyadance\NOTICE
build\out\anyadance\THIRD_PARTY_NOTICES.md
build\out\anyadance\DISCLAIMER.md
build\out\anyadance\TRADEMARKS.md
build\out\anyadance\README.md
build\out\anyadance\README.zh-CN.md
build\out\AnyaDance.zip
```

UI 程序就放在驱动文件夹内，因此 `build\out\anyadance\` 即为一个可直接分发的目录，
构建时还会将其打包为 `build\out\AnyaDance.zip`，可直接发给他人。
exe 会将自身所在的文件夹注册为 SteamVR 驱动，OpenVR 便能在其旁边找到
`driver.vrdrivermanifest` 与 `bin\win64\driver_anyadance.dll`。OpenVR 始终从
`bin\win64\` 加载驱动 DLL，驱动根目录保存 manifest 与 UI 程序。

使用本地依赖检出：

```powershell
.\scripts\build_driver.ps1 -OpenVRSdkRoot F:\deps\openvr -ImguiRoot F:\deps\imgui
```

## 测试

```powershell
cmake --build build --config Release --target anyadance_tests
ctest --test-dir build -C Release --output-on-failure
```

测试涵盖协议校验、安全钳制、T 姿势重置运算、键盘控制、鼠标操控运算、MMD 重映射、`.nya` 片段处理以及 UDP 日志行为。

## 注册

```powershell
.\scripts\register_driver.ps1
.\scripts\restart_steamvr.ps1
```

`register_driver.ps1` 会注册驱动并应用全虚拟设置（启用虚拟头显、控制器与追踪器），并在写入前先将 `steamvr.vrsettings` 备份到 `%LOCALAPPDATA%\AnyaDance\steamvr.vrsettings.backup`。

在更改注册或重新构建驱动 DLL 后，必须重启 SteamVR。

> **使用完毕后请取消注册。** 驱动处于注册状态时，SteamVR 会以全虚拟模式运行——真实头显、控制器与追踪器将无法被追踪。请参阅[取消注册](#取消注册)以还原原始设置。

## HMD 渲染分辨率

虚拟头显默认每只眼睛以 `1920x1080` 渲染，以控制 SteamVR 合成器的负载。高级用户无需重新构建驱动即可通过覆盖两个设置项来提高分辨率。

编辑全局的 `steamvr.vrsettings`（位于 `<Steam>\config\steamvr.vrsettings`），在其中新增或扩展 `driver_anyadance` 小节：

```json
"driver_anyadance": {
    "headset_render_width": 3840,
    "headset_render_height": 2160
}
```

然后重启 SteamVR（`.\scripts\restart_steamvr.ps1`）。`3840x2160` 即 4K。宽高比可以自由选择（例如与你的显示器保持一致）：投影会按所设的渲染分辨率自动适配，不会被拉伸。分辨率越高，GPU 渲染时间大致按缩放倍数的平方增长——4K 约为 1080p 的四倍。

在 `steamvr.vrsettings` 中设置的值会覆盖驱动默认值（`resources\settings\default.vrsettings`）。同一小节还提供 `headset_window_width`、`headset_window_height`、`headset_window_eye_mode` 与 `headset_window_preserve_aspect`，用于配置桌面镜像窗口；详见 [docs/device-model.zh-CN.md](docs/device-model.zh-CN.md)。

## 运行测试界面

```powershell
.\build\out\anyadance\AnyaDance.exe
```

该界面：

- 自动以 60 Hz 开始 UDP 推送
- 在最小化或失去焦点时仍继续推送
- 正常退出时发送一帧中性输入
- UDP 日志始终为英文，省略未变化的保活数据包，并标明每条记录的具体原因：变化的按键与动作（例如 `Z left trigger down`）、被拖拽的设备（例如 `Hip manipulated`），或手指弯曲的变化
- 支持悬停或固定行查看详情，并提供三种针对载荷的操作：“复制”（原始请求体）、“复制重发命令”（可直接运行的 PowerShell UDP 单行命令）以及“重新发送”（由 UI 通过自身套接字重发该数据报）
- 可通过自身按钮将其所在文件夹注册/取消注册为 SteamVR 驱动并重启 SteamVR（带确认）
- 提供“窗口置顶”复选框，可将窗口固定在其他窗口之上；该选择会在多次运行间记住
- 可播放 MMD 舞蹈：点击 **舞蹈 (MMD)** 选择 `.vmd` 动作和 `.pmx`/`.pmd` 模型，分析后即可把舞蹈实时推送到六个设备（见 [docs/mmd-dance.zh-CN.md](docs/mmd-dance.zh-CN.md)）
- 可将姿势与舞蹈保存为 `.nya` 片段：主窗口的 **保存姿势** / **加载姿势** 用于捕获并恢复当前姿势；舞蹈对话框可 **保存 .nya**（已分析的舞蹈）并 **加载 .nya** 直接重新播放，无需再次解算
- 通过 `src/ui/localization.*` 与本地化表支持英文与简体中文界面

按键绑定：

```text
WASD  左摇杆移动
Q/E   右摇杆转向
Space 按住时右 A
M     按住时右 B
V     按住时左 A
Z     按住时左扳机
X     按住时右扳机
```

鼠标操控使用六个设备方框。捕获面板与方框会随 UI 窗口缩放。头显方框可旋转，并可通过左键+右键拖拽进行垂直（Y）移动（受 2 m Y 上限限制）。其他设备使用鼠标左键拖拽进行本地 X/Y 移动，中键拖拽进行旋转，右键拖拽进行深度移动。HMD/Global 坐标系单选项决定操控使用头显偏航基准还是固定世界轴。手部与脚部成对的对称复选框使用相同坐标系设置：HMD 模式相对头显偏航的 YZ 平面对称，Global 模式以头显位置为中心、按世界轴对称。滚轮可开合双手手指。滚动时按住数字键可单独弯曲某根手指：`1`-`5` 对应左手从小指到拇指，`6`-`0` 对应右手从拇指到小指（即 `5`/`6` 为拇指，`1`/`0` 为小指）。每根手指都钳制在 `[0, 1]` 范围内，朝一个方向滚到底即可把所有手指重置为完全张开或完全握紧。当一只手的所有手指都握紧成拳（弯曲度均接近最大）时，会触发该手的握持（grip）；只要有任一手指松开即释放，从而驱动 VRChat 的抓取。

在身体面板空白处拖拽相当于右摇杆：按下点即为摇杆中心，拖拽在每个轴上偏移 ±1，松开后回到中位。该功能用于操作右手快捷菜单（按住 `M` 打开）。

## MMD 舞蹈

<p>
  <img src="docs/images/anya_dance.gif" alt="AnyaDance MMD 舞蹈播放" width="25%"><img src="docs/images/anya_dance.gif" alt="AnyaDance MMD 舞蹈播放" width="25%"><img src="docs/images/anya_dance.gif" alt="AnyaDance MMD 舞蹈播放" width="25%"><img src="docs/images/anya_dance.gif" alt="AnyaDance MMD 舞蹈播放" width="25%">
</p>

**舞蹈 (MMD)** 按钮会把 MMD 舞蹈在内存中实时播放到六个虚拟设备上。Blender + MMD Tools 会基于你提供的 PMX/PMD 模型解算 `.vmd` 动作，UI 再把解算结果重映射到固定设备骨架并以 60 Hz 推送。自定义安装路径可在 **高级** 中指定；UI 会记住这些路径。

需要：[Blender](https://www.blender.org/) 与 [MMD Tools](https://github.com/MMD-Blender/blender_mmd_tools) 插件，以及你自己的模型。MMD 模型属于第三方作品并带有各自授权。详见 [docs/mmd-dance.zh-CN.md](docs/mmd-dance.zh-CN.md)。

舞蹈分析完成后，**保存 .nya** 会把结果写入片段文件；**加载 .nya** 读取后即可立即播放——加载会跳过 Blender 解算与重映射，因此已保存的舞蹈可瞬间播放。

## 片段文件（.nya）

`.nya` 是一个小型 JSON 片段，存储设备级帧——六个设备的姿态加上每只手的手指弯曲——可直接推送、无需再转换。姿势与动画使用同一格式：**姿势**是单帧片段（以该单帧循环的方式保持住），而**动画**（例如已保存的 MMD 舞蹈）是多帧带时间戳的序列。加载时设备 Y 会钳制到 2 m 上限、手指弯曲钳制到 `[0, 1]`，因此手工编辑的文件也不会超出安全范围。

## 安全与存活

所有六个设备的 Y 值都有 `2.0 m` 的硬上限。UI 在序列化前钳制，原生驱动在数据包校验后再次钳制。

六个设备都会以中性姿态开始，并保持连接且有效。被接受的数据包会更新最新姿态与控制器输入。数据包停止后，SteamVR 仍会看到每个设备停留在最后接受的姿态，并保持连接、有效、`TrackingResult_Running_OK`。

## 协议概览

- UDP 到 `127.0.0.1:39570`
- UTF-8 JSON
- `version` 必须为 `1`
- 发送即完成的数据报
- 接受的数据报小于 8192 字节
- 四元数顺序为 XYZW
- 可识别的设备 ID 为 `hmd`、`left_controller`、`right_controller`、`hip`、`left_foot`、`right_foot`

完整协议见 [docs/protocol.zh-CN.md](docs/protocol.zh-CN.md)。

## 取消注册

```powershell
.\scripts\unregister_driver.ps1
.\scripts\restart_steamvr.ps1
```

**取消注册是恢复正常 SteamVR 状态的必要操作。** 在 AnyaDance 驱动处于注册状态期间，SteamVR 会将六个虚拟设备视为真实硬件，这将导致真实头显、控制器和追踪器无法被识别——在驱动移除之前，你的物理 VR 设备将无法正常工作。

`unregister_driver.ps1` 会移除驱动条目，并将 `steamvr.vrsettings` 从注册时创建的备份中还原。SteamVR 重启后，真实设备即可恢复正常追踪。

你也可以在 UI 内通过 **取消注册驱动** 按钮完成该操作，它会触发相同的脚本并自动提示重启 SteamVR。

## 卸载

1. **取消注册驱动**（见上方[取消注册](#取消注册)），并重启 SteamVR 以恢复真实设备。
2. 删除你解压的 AnyaDance 文件夹。
3. 可选：删除 UI 在 AppData 中保存的状态：
   - `%LOCALAPPDATA%\AnyaDance\ui_state.ini` — 保存的偏好设置（窗口大小、路径、窗口置顶等）
   - `%LOCALAPPDATA%\AnyaDance\steamvr.vrsettings.backup` — 注册时备份的 SteamVR 设置文件（取消注册时自动删除）
   - `%LOCALAPPDATA%\AnyaDance\registered_driver_path.txt` — 注册时记录的驱动路径，便于在文件夹被移动后仍能取消注册（取消注册时自动删除）

如果在删除文件夹之前跳过了第 1 步，SteamVR 仍会引用（现已不存在的）驱动路径。可从脚本文件夹的副本运行 `unregister_driver.ps1`，或手动在 `%LOCALAPPDATA%\openvr\openvrpaths.vrpath` 中移除该驱动条目。

## 许可证

AnyaDance 是开源软件，依据 Apache License 2.0 授权。

依据 Apache License 2.0 授权。参见 [LICENSE](LICENSE) 与署名文件 [NOTICE](NOTICE)。再分发时必须保留 NOTICE 内容并标注修改过的文件。捆绑的第三方组件保留各自的许可证；详见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

AnyaDance 与 Project Anya 的名称、标识和品牌使用说明见 [TRADEMARKS.md](TRADEMARKS.md)。

![Project Anya banner](driver/resources/images/anya_banner.png)
