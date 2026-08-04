# UDP 协议

[English](protocol.md) | **简体中文**

## 传输

```text
UDP
127.0.0.1:39570
UTF-8 JSON
version: 1
发送即完成（fire-and-forget）的数据报
被接受的数据报大小：小于 8192 字节
```

驱动仅绑定回环地址。发送端将 `sendto` 成功视为本地套接字成功。

## 驱动命令日志

当已处理的姿态数据报改变了命令所要求的内容时，驱动会以尽力而为的方式发送一份遥测数据报。驱动只向本机 IPv4 多播组发送一次，因此伴随 UI 与多个独立应用进程都能收到同一份报告：

```text
UDP
多播组：239.255.39.71
端口：39571
接口：127.0.0.1（仅回环）
UTF-8 JSON
日志版本：1
日志数据报最大尺寸：65507 字节
```

同一多播组会承载多种事件。每个数据报都共享同一个信封结构，并由 `event` 字段标识——这正是接收端用于过滤的字段。信封结构、事件清单以及保证两者稳定的兼容性规则，参见[事件结构](#事件结构)。

日志多播组独立于命令接收端点。可在 `steamvr.vrsettings` 的 `driver_anyadance` 小节中配置；修改后重启 SteamVR：

```json
"driver_anyadance": {
    "command_log_enabled": true,
    "command_log_multicast_group": "239.255.39.71",
    "command_log_port": 39571,
    "haptic_log_enabled": true
}
```

`command_log_multicast_group` 必须是 IPv4 多播地址。每个本机订阅者独立加入该多播组与端口；修改多播组即可让另一组监听器接收报告。`command_log_enabled` 控制是否生成报告。默认值来自随包提供的 `resources/settings/default.vrsettings`。驱动始终选择回环接口并将多播 TTL 设为 0，因此报告不会离开本机。

随附的 PowerShell 监听器是可运行的参考实现：

```powershell
# 持续监视报告，按 Ctrl+C 停止。
.\scripts\listen_driver_log.ps1

# 启动三个独立监听进程，并要求每个进程都收到同一份本机多播探测包。
.\scripts\listen_driver_log.ps1 -Validate -ListenerCount 3
```

## 编写接收端

任何本机进程都可以订阅。无需向驱动注册，伴随 UI 也不必运行——无论是否有人监听，驱动都会进行多播，而 UI 的 **监视驱动命令** 开关只决定 UI 自身是否加入。

套接字设置共四步，且顺序不能变。漏掉其中任何一步都会造成静默失败：套接字照常创建并绑定成功，却永远收不到数据报。

1. **创建 UDP 套接字**（`AF_INET`、`SOCK_DGRAM`）。
2. **在绑定之前设置 `SO_REUSEADDR`。** 该端口在设计上就要被多个进程共享。若不设置，后启动的进程要么绑定失败，要么什么也收不到。
3. **将端口 `39571` 绑定到 `0.0.0.0`**，而不是绑定到多播组地址。绑定多播地址本身在某些平台可行，但在 Windows 上不可行。
4. **通过 `127.0.0.1` 接口加入 `239.255.39.71`**（`IP_ADD_MEMBERSHIP`）。接口的选择很关键：驱动以 TTL 0 从回环地址发送，因此在其他接口上的成员关系永远看不到这些流量。

随后读取数据报、按 UTF-8 解码、解析 JSON，并依据 `event` 过滤。下面这个接收端处理了当前两种事件类型，跳过它不认识的类型，并依据编号报告丢失：

```python
import json
import socket
import struct

GROUP = "239.255.39.71"
PORT = 39571
INTERFACE = "127.0.0.1"

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
# 必须在 bind 之前设置：使其他接收端可以共享该端口。
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
# 绑定到任意地址上的该端口，而不是绑定多播组地址。
sock.bind(("", PORT))
# 在回环接口上加入：驱动以 TTL 0 从 127.0.0.1 发送。
sock.setsockopt(
    socket.IPPROTO_IP,
    socket.IP_ADD_MEMBERSHIP,
    struct.pack("=4s4s", socket.inet_aton(GROUP), socket.inet_aton(INTERFACE)),
)

expected = None
while True:
    datagram, _ = sock.recvfrom(65507)
    try:
        event = json.loads(datagram.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        continue

    if event.get("version") != 1:
        continue  # 该接收端不认识的版本

    sequence = event.get("sequence")
    if expected is not None and sequence > expected:
        print(f"  (lost {sequence - expected} event(s))")
    expected = sequence + 1

    name = event.get("event")
    if name == "haptic_vibration":
        haptic = event["haptic"]
        print(
            f"#{sequence} haptic {event['device']}: "
            f"{haptic['duration_seconds']:.3f}s "
            f"{haptic['frequency_hz']:.1f}Hz "
            f"amplitude {haptic['amplitude']:.2f}"
        )
    elif name == "command_processed":
        command = event["command"]
        print(
            f"#{sequence} command from {event['source']['host']}: "
            f"{'accepted' if command['accepted'] else 'rejected'} - {event['detail']}"
        )
    else:
        # 该接收端编写之后新增的事件类型。信封对所有事件都是共通的，
        # 因此仍然可以安全地读取并跳过。
        print(f"#{sequence} {name} - {event['detail']}")
```

该示例刻意保持精简。在正式使用之前，请阅读[顺序与投递](#顺序与投递)与[结构稳定性](#结构稳定性)：接收端还应当按 `sequence` 而非到达顺序排列事件，并丢弃重复事件——这些示例中并未实现。

### 收不到数据报

套接字成功创建并不说明任何问题，请按以下顺序排查：

- **驱动在运行吗？** 只有 SteamVR 加载了该驱动时才会产生报告。可在 SteamVR 网页控制台查看 `driver_anyadance`，或在 SteamVR 驱动日志中查找 `Command logging multicasts on loopback` 一行。
- **是否在 `127.0.0.1` 上加入？** 在默认接口或局域网接口上加入，是套接字静默无数据最常见的原因。
- **是否绑定了 `0.0.0.0` 而不是多播组地址？**
- **`SO_REUSEADDR` 是在 bind 之前设置的吗，而不是之后？**
- **上报是否被关闭？** `steamvr.vrsettings` 中的 `command_log_enabled` 与 `haptic_log_enabled` 默认均为 `true`；修改后需要重启 SteamVR。
- **多播组与端口是否一致？** 如果修改过 `command_log_multicast_group` 或 `command_log_port`，订阅者也必须同步修改。
- **流量本身存在吗？** 运行 `.\scripts\listen_driver_log.ps1 -Validate -ListenerCount 3`。它会自行生成多播探测包，因此通过即说明本机的多播组工作正常、问题出在你的接收端；未通过则说明问题在本机的多播配置。

## 事件结构

多播组上的每个数据报都是一个事件。无论类型如何，所有事件都共享下面这个信封结构，使接收端无需了解具体形状即可识别、过滤、排序并展示任何事件——包括它并不认识的事件：

```json
{
  "version": 1,
  "event": "<事件名称>",
  "sequence": 42,
  "suppressed": 0,
  "detail": "compact English summary"
}
```

| 信封字段 | 类型 | 含义 |
| --- | --- | --- |
| `version` | 数字 | 驱动日志协议版本；当前为版本 1。接收端遇到不认识的 `version` 时必须忽略该事件。 |
| `event` | 字符串 | **用于过滤的字段。** 标识事件类型，并据此决定信封之外那些类型专属字段的形状。 |
| `sequence` | 数字 | 在驱动发出的*所有*事件之间单调递增，而非按事件类型分别计数，因此各类型事件共享同一顺序。驱动启动时重新计数。出现断档意味着数据报丢失，或某类事件在上游被过滤掉。 |
| `suppressed` | 数字 | 在*同类型*的上一份报告与本报告之间被吸收掉的相同事件数量。该字段可选，缺失时按 `0` 处理。从不抑制重复的事件类型始终上报 `0`。 |
| `detail` | 字符串 | 该事件的简洁英文摘要，始终存在，使接收端无需解析类型专属字段即可将任意事件渲染为一行。仅供人阅读——请勿解析。 |

除信封外，每个事件还携带其类型专属字段：

| `event` | 类型专属字段 | 说明位置 |
| --- | --- | --- |
| `command_processed` | `source`、`command` | [驱动命令日志](#驱动命令日志) |
| `haptic_vibration` | `device`、`haptic` | [触觉事件](#触觉事件) |

### 顺序与投递

传输使用普通 UDP，因此传输层不作任何保证：数据报可能丢失、重复或乱序。`sequence` 正是使这些情况可以被纠正的字段，也正因如此它在全局范围计数，而不是按事件类型分别计数。

驱动会尽可能晚地取号，紧接着就把数据报交给套接字。它无法做到取号与发送的原子操作：有两个线程在该组上上报——UDP 接收线程负责 `command_processed`，SteamVR 的 `RunFrame` 线程负责 `haptic_vibration`——让它们相互串行化会在两条热路径上都加锁。因此仍存在一个很小的窗口，两个事件可能以与编号相反的顺序发出，这与网络行为无关。

所以接收端应当按 `sequence` 排序，而不是按到达顺序：

- **重排序。** 按 `sequence` 缓冲或插入，而不是到达即追加。实际情况下迟到的事件只落后几个位置，因此有界窗口就足够；伴随 UI 最多向前回溯 64 行。
- **去重。** 重复出现的 `sequence` 是重复的数据报，而非新事件，应当丢弃。
- **检测丢失。** 缺失的 `sequence` 意味着数据报被丢弃。由于计数是全局的，即使接收端只过滤某一种事件类型也能看到断档——它保留的编号只是稀疏的。请通过记录自己过滤掉了哪些编号来与真正的丢失相区分。
- **不要按到达时间或 `detail` 排序。** 只有 `sequence` 反映驱动产生事件的先后顺序。

不会重传，也不会读取任何确认，因此丢失的事件就是丢了。这是遥测数据：其设计目标是在压力下被丢弃，而不是拖慢驱动。

### 结构稳定性

以下规则构成一份约定。遵循这些规则的接收端可以跨 AnyaDance 版本继续工作而无需改动：

- **依据 `event` 过滤。** 它是唯一决定其余结构形状的字段。不要通过其他字段是否存在来推断事件类型。
- **忽略不认识的 `event` 取值。** 任何版本都可能向该组新增事件类型。无法识别的事件只是接收端不处理的正常事件，而非损坏的数据报或协议错误。它仍然携带完整信封，因此可以被通用地记录或展示。
- **忽略不认识的字段。** 信封或既有事件的类型专属对象中都可能新增字段，接收端不得因此失败。
- **在同一 `version` 内，既有字段不会改变。** 字段的名称、类型与含义一经发布即固定。删除字段、改变字段用途或收窄其取值范围都需要新的 `version`。
- **可选字段缺失时表示其文档所述的默认值**，而不是错误。
- **请勿解析 `detail`。** 其措辞不属于约定的一部分，随时可能改写。请改用类型专属字段。

`scripts/listen_driver_log.ps1` 中的参考监听器遵循上述规则，是最简短的可运行示例。

### `command_processed`

每个数据包采用以下结构：

```json
{
  "version": 1,
  "event": "command_processed",
  "sequence": 42,
  "suppressed": 613,
  "detail": "accepted 2 device entries; clamped Y for 1",
  "source": {
    "host": "127.0.0.1",
    "port": 54321
  },
  "command": {
    "protocol": "pose_frame",
    "bytes": 347,
    "accepted": true,
    "devices": ["hmd", "left_controller"],
    "y_clamped": ["hmd"],
    "payload": "{\"version\":1,...}"
  }
}
```

信封字段参见[事件结构](#事件结构)；对该事件而言，`suppressed` 表示自上一份 `command_processed` 报告以来被吸收掉的相同命令数量，`detail` 概述处理结果。类型专属字段如下：

| 字段 | 含义 |
| --- | --- |
| `source.host`、`source.port` | 向端口 `39570` 发送原始姿态数据报的端点。 |
| `command.protocol` | 命令协议名称；版本 1 使用 `pose_frame`。 |
| `command.bytes` | 原始命令数据报的字节长度。 |
| `command.accepted` | 至少有一个已识别设备条目被接受并保存时为 `true`。 |
| `command.devices` | 此数据报中被接受的已识别设备条目。 |
| `command.y_clamped` | Y 值经过钳制的已接受设备条目。 |
| `command.payload` | 以 JSON 字符串保存的原始命令数据报，可用于检查与重发。 |

报告套接字采用非阻塞方式，遥测传递按尽力而为原则工作。套接字缓冲区已满、多播配置无效或本机传递失败时会丢弃该报告，命令处理仍会继续。序列化与发送位于 UDP 接收线程，不进入 SteamVR 的 `RunFrame` 路径。无论订阅者数量多少，驱动对每条上报的命令都只发送一次。

### 保持不变的命令

发送端持续推送同一姿态时，会按其推流速率重复发送完全相同的命令；若逐条上报，则在毫无变化的情况下每秒也会产生 `kStreamRateHz` 份报告。因此驱动仅在命令与上一次已上报的命令不同的情况下才上报。当接受结果、已识别设备条目集合、被钳制条目集合，以及每个出现设备的姿态与手柄输入全部一致时，该命令即视为重复。比较不包含到达时间，且采用精确比较——保持不变的姿态由未变化的状态重新序列化，因而逐字节一致；而缓慢但确实存在的移动仍会逐帧上报。未通过校验的重复数据报改为按字节比较，因为被拒绝的数据报不会留下可信的解析结果。

抑制是变化过滤而非限速：不同的命令始终会被上报，因此记录一段移动序列的监听器仍能收到其中的每一帧。被吸收的重复数量会随下一份报告的 `suppressed` 字段一同送出，从而让"保持姿态"与"发送端停止推流"始终可以区分。

UI 中的 **监视驱动命令** 开关默认关闭，其状态记录在 `%LOCALAPPDATA%\AnyaDance\ui_state.ini` 中并在下次启动时恢复。该开关可立即加入或离开默认多播组。监听器加入期间，驱动报告是成功命令日志行的事实来源，UI 会抑制自身的成功发送记录。本地套接字错误仍会显示，因为驱动无法报告它没有收到的命令。来自同一发送端的快速成功报告会按 100 毫秒显示窗口合并；拒绝报告则逐条保留。合并仅作用于 UI 显示，并叠加在驱动的重复命令抑制之上：移动中的姿态每 100 毫秒仍产生一行，而保持不变的姿态不再产生新行。若某行对应的报告带有非零 `suppressed` 计数，则说明在它之前有多少条相同命令被保持。

## 触觉事件

两个虚拟手柄会像真实 Index 手柄那样公开 `/output/haptic` 组件，使 SteamVR 将触觉请求路由到它们。驱动没有马达，也不会回放任何振动；它只上报该请求，以便外部工具据此作出反应——例如驱动指示灯，或转发到实体硬件。

上报发送到与命令报告相同的多播组与端口，因此监听器只需加入一个组即可同时观察两者。`driver_anyadance` 小节中的 `haptic_log_enabled` 控制该上报，默认为 `true`；`/output/haptic` 组件始终会创建，因此关闭上报不会改变 SteamVR 或游戏所看到的内容。上报在 SteamVR 的 `RunFrame` 线程上以尽力而为且非阻塞的方式进行：套接字缓冲区已满或没有监听器时会丢弃该报告，而不会拖慢一帧。不需要也不会读取任何确认。

```json
{
  "version": 1,
  "event": "haptic_vibration",
  "sequence": 7,
  "suppressed": 0,
  "detail": "0.125 s at 160.5 Hz, amplitude 0.75",
  "device": "right_controller",
  "haptic": {
    "duration_seconds": 0.125,
    "frequency_hz": 160.5,
    "amplitude": 0.75
  }
}
```

信封字段参见[事件结构](#事件结构)。该事件从不抑制重复，因此 `suppressed` 始终为 `0`。类型专属字段如下：

| 字段 | 含义 |
| --- | --- |
| `device` | 被要求振动的手柄：`left_controller` 或 `right_controller`。 |
| `haptic.duration_seconds` | 请求的脉冲时长（秒）。`0` 是合法的停止请求。 |
| `haptic.frequency_hz` | 请求的振动频率（赫兹）。 |
| `haptic.amplitude` | 请求的强度，范围 `0.0` 到 `1.0`。 |

这些值按 SteamVR 提供的原样透传；除拒绝非有限数值外，驱动不施加自己的策略。

只关心触觉的接收端，保留 `event` 等于 `haptic_vibration` 的数据报并忽略其余即可，包括日后新增的事件类型。

## 坐标约定

位置以米为单位，处于该驱动向 SteamVR 提供的驱动姿态坐标空间中。四元数使用 XYZW 顺序：

```json
"rotation_xyzw": [x, y, z, w]
```

## 数据包结构

AnyaDance 伴随程序在每个数据报中发送完整快照：六个设备条目和两个控制器的输入条目都会存在。只有在有手指数据时才会发送 `finger_bends`。第三方发送端可以只发送部分设备，但至少需要一个可识别且有效的设备条目。

```json
{
  "version": 1,
  "devices": {
    "hmd": {
      "valid": true,
      "connected": true,
      "pose": {
        "position": [0.0, 1.5, 0.0],
        "rotation_xyzw": [0.0, 0.0, 0.0, 1.0]
      }
    },
    "left_controller": {
      "valid": true,
      "connected": true,
      "pose": {
        "position": [-0.26, 1.10, -0.54],
        "rotation_xyzw": [0.77, 0.10, -0.16, 0.61]
      }
    },
    "right_controller": {
      "valid": true,
      "connected": true,
      "pose": {
        "position": [0.27, 1.57, -0.54],
        "rotation_xyzw": [0.77, -0.10, 0.16, 0.61]
      }
    },
    "hip": {
      "valid": true,
      "connected": true,
      "pose": {
        "position": [0.0, 1.07, -0.05],
        "rotation_xyzw": [0.0, 0.0, 0.0, 1.0]
      }
    },
    "left_foot": {
      "valid": true,
      "connected": true,
      "pose": {
        "position": [-0.09, 0.26, 0.10],
        "rotation_xyzw": [0.0, 0.0, 0.0, 1.0]
      }
    },
    "right_foot": {
      "valid": true,
      "connected": true,
      "pose": {
        "position": [0.09, 0.26, 0.10],
        "rotation_xyzw": [0.0, 0.0, 0.0, 1.0]
      }
    }
  },
  "inputs": {
    "left_controller": {
      "trigger_click": false,
      "trigger_value": 0.0,
      "menu_click": false,
      "system_click": false,
      "a_click": false,
      "b_click": false,
      "grip_click": false,
      "grip_value": 0.0,
      "joystick_x": 0.0,
      "joystick_y": 0.0,
      "trackpad_x": 0.0,
      "trackpad_y": 0.0,
      "finger_bends": {
        "thumb": 0.0,
        "index": 0.0,
        "middle": 0.0,
        "ring": 0.0,
        "pinky": 0.0
      }
    },
    "right_controller": {
      "trigger_click": false,
      "trigger_value": 0.0,
      "menu_click": false,
      "system_click": false,
      "a_click": false,
      "b_click": false,
      "grip_click": false,
      "grip_value": 0.0,
      "joystick_x": 0.0,
      "joystick_y": 0.0,
      "trackpad_x": 0.0,
      "trackpad_y": 0.0
    }
  }
}
```

可识别的设备 ID 恰好为：

```text
hmd
left_controller
right_controller
hip
left_foot
right_foot
```

解析器使用可识别的设备 ID 与字段。格式错误的可识别设备条目会被跳过；同一数据包中其他有效的可识别条目仍可使用。

## 必需的设备字段

每个设备条目都需要：

```json
{
  "valid": true,
  "connected": true,
  "pose": {
    "position": [0.0, 0.0, 0.0],
    "rotation_xyzw": [0.0, 0.0, 0.0, 1.0]
  }
}
```

| 字段 | 类型 | 要求与行为 |
| --- | --- | --- |
| `valid` | 布尔值 | 设备条目要被解析时必需。版本 1 在启动后始终保持所有虚拟设备有效，因此无论此值为何，驱动当前都会向 SteamVR 报告 `true`。 |
| `connected` | 布尔值 | 设备条目要被解析时必需。版本 1 在启动后始终保持所有虚拟设备连接，因此无论此值为何，驱动当前都会向 SteamVR 报告 `true`。 |
| `pose.position` | 三个数字的数组 | 必需。单位为米，处于驱动姿态空间；每个分量都必须是有限值且位于 `-30.0` 至 `30.0`，Y 会钳制到 `0.0`–`25.0`。 |
| `pose.rotation_xyzw` | 四个数字的数组 | 必需。按 XYZW 排列的四元数；值必须有限，平方长度必须在 `0.5` 至 `1.5` 之间。接受后会被归一化。 |

`inputs` 对象是可选的。只有 `left_controller` 和 `right_controller` 的输入条目会影响 OpenVR 控制器状态。控制器输入条目中的每个成员都是可选的；省略时使用下表中的默认值或回退值。只有当同一数据报的 `devices` 中也存在对应控制器的有效条目时，输入条目才会被应用。如果设备条目存在但输入条目缺失，普通按钮和轴会重置为默认值；如果设备条目本身缺失或格式错误，则保留该设备之前的姿态与输入。

| 输入字段 | 类型 | 范围/默认值 | 驱动行为 |
| --- | --- | --- | --- |
| `trigger_click` | 布尔值 | 默认 `false` | 驱动 `/input/trigger/click`。 |
| `trigger_value` | 数字 | 钳制到 `0.0`–`1.0`；省略时，若 `trigger_click` 为 true 则为 `1.0`，否则为 `0.0` | 驱动 `/input/trigger/value`。 |
| `menu_click` | 布尔值 | 默认 `false` | 驱动 `/input/application_menu/click`。 |
| `system_click` | 布尔值 | 默认 `false` | 保留在版本 1 数据包中。 |
| `a_click` | 布尔值 | 默认 `false` | 驱动 `/input/a/click`。 |
| `b_click` | 布尔值 | 默认 `false` | 驱动 `/input/b/click`。 |
| `grip_click` | 布尔值 | 默认 `false` | 驱动 `/input/grip/click` 和 `/input/grip/touch`。 |
| `grip_value` | 数字 | 钳制到 `0.0`–`1.0`；省略时，若 `grip_click` 为 true 则为 `1.0`，否则为 `0.0` | 同时驱动 `/input/grip/value` 和 `/input/grip/force`。 |
| `joystick_x`、`joystick_y` | 数字 | 分别钳制到 `-1.0`–`1.0`；默认 `0.0` | 驱动 `/input/thumbstick/x` 和 `/input/thumbstick/y`。 |
| `trackpad_x`、`trackpad_y` | 数字 | 分别钳制到 `-1.0`–`1.0`；每个省略的轴回退到对应摇杆轴 | 驱动 `/input/trackpad/x` 和 `/input/trackpad/y`。 |
| `finger_bends` | 对象 | 可选；省略时保留上次应用的弯曲值（首次收到有效对象之前为张开） | 驱动手部骨骼。如果存在，以下五个成员必须全部成功解析，否则整个对象会被忽略。 |
| `finger_bends.thumb`、`.index`、`.middle`、`.ring`、`.pinky` | 数字 | 分别钳制到 `0.0`（张开）至 `1.0`（完全弯曲） | 应用于 `/input/skeleton/left` 或 `/input/skeleton/right` 中对应的手指。 |

## 校验

驱动会拒绝具有以下情况的数据包：

- 缺少或错误的 `version`
- JSON 结构格式错误
- 数据报大小大于或等于 8192 字节
- 没有任何有效的可识别设备

设备条目在以下情况会被忽略：

- 缺少必需字段
- 位置或四元数为非有限值
- 位置分量绝对值超过 `30.0 m`
- 四元数平方长度超出可接受的 `0.5` 到 `1.5` 范围

被接受的四元数在使用前会被归一化。

## 位置限制

所有位置分量都必须是有限值且位于 `±30 m` 范围内。伴随 UI 与驱动都会将设备 Y 的上限进一步限制为 `25 m`。

Y 的下限按设备区分。只有 `hmd` 会被保持在 `0 m` 及以上：它是游玩空间中的头部，若置于地平面以下会使视角进入地下。其余五个设备则可以合理地低于地平面——脚穿过地面、髋部做地板动作，或解算动作的原点高于地面——因此它们的 Y 仅受共享的 `±30 m` 位置范围约束。

| 设备 | Y 范围 |
| --- | --- |
| `hmd` | `0 m` 至 `25 m` |
| `left_controller`、`right_controller`、`hip`、`left_foot`、`right_foot` | `-30 m` 至 `25 m` |

## 姿态存活

在第一个数据包之前，所有六个虚拟设备都以中性姿态开始且保持连接、有效。当数据包到达时，每个设备最新的有效数据包会更新其姿态与控制器输入。数据包停止后，SteamVR 会继续看到设备在其最后姿态保持连接且有效。

被接受的设备样本被报告为已连接、有效且 `TrackingResult_Running_OK`。

## 控制器输入

驱动暴露与 Valve Index 兼容的控制器组件：

```text
/input/trigger/click
/input/trigger/value
/input/application_menu/click
/input/a/click
/input/b/click
/input/grip/click
/input/grip/touch
/input/grip/value
/input/grip/force
/input/thumbstick/x
/input/thumbstick/y
/input/trackpad/x
/input/trackpad/y
/input/skeleton/left
/input/skeleton/right
```

`grip_click` 驱动 `/input/grip/touch`，`grip_value` 驱动 `/input/grip/force`。骨骼组件的变换由 `finger_bends` 派生。
