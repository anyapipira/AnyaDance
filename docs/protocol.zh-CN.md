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

驱动处理每个能够装入接收缓冲区的姿态数据报后，都会以尽力而为的方式发送一份遥测数据报。默认目标是伴随 UI 的回环监听端点：

```text
UDP
127.0.0.1:39571
UTF-8 JSON
日志版本：1
日志数据报最大尺寸：65507 字节
```

日志目标端点独立于命令接收端点。可在 `steamvr.vrsettings` 的 `driver_anyadance` 小节中配置；修改后重启 SteamVR：

```json
"driver_anyadance": {
    "command_log_enabled": true,
    "command_log_host": "127.0.0.1",
    "command_log_port": 39571
}
```

`command_log_host` 接受 IPv4 地址。将其指向其他监听器即可在伴随 UI 之外接收驱动报告。`command_log_enabled` 控制是否生成报告。默认值来自随包提供的 `resources/settings/default.vrsettings`。

每个数据包采用以下结构：

```json
{
  "version": 1,
  "event": "command_processed",
  "sequence": 42,
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
  },
  "detail": "accepted 2 device entries; clamped Y for 1"
}
```

| 字段 | 含义 |
| --- | --- |
| `version` | 驱动日志协议版本；当前为版本 1。 |
| `event` | 事件类型；版本 1 发送 `command_processed`。 |
| `sequence` | 当前驱动接收器生命周期内单调递增的报告编号；驱动启动时重新计数。 |
| `source.host`、`source.port` | 向端口 `39570` 发送原始姿态数据报的端点。 |
| `command.protocol` | 命令协议名称；版本 1 使用 `pose_frame`。 |
| `command.bytes` | 原始命令数据报的字节长度。 |
| `command.accepted` | 至少有一个已识别设备条目被接受并保存时为 `true`。 |
| `command.devices` | 此数据报中被接受的已识别设备条目。 |
| `command.y_clamped` | Y 值经过钳制的已接受设备条目。 |
| `command.payload` | 以 JSON 字符串保存的原始命令数据报，可用于检查与重发。 |
| `detail` | 简洁的英文处理摘要。 |

报告套接字采用非阻塞方式，遥测传递按尽力而为原则工作。套接字缓冲区已满、监听器未运行、日志端点无效或网络传递失败时会丢弃该报告，命令处理仍会继续。序列化与发送位于 UDP 接收线程，不进入 SteamVR 的 `RunFrame` 路径。

UI 中的 **监视驱动命令** 开关可即时启动或停止默认监听器。监听器绑定期间，驱动报告是成功命令日志行的事实来源，UI 会抑制自身的成功发送记录。本地套接字错误仍会显示，因为驱动无法报告它没有收到的命令。来自同一发送端的快速成功报告会按 100 毫秒显示窗口合并；拒绝报告则逐条保留。合并仅作用于 UI 显示，驱动仍会为每条已处理命令发送报告。

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

所有位置分量都必须是有限值且位于 `±30 m` 范围内。伴随 UI 与驱动都会将设备 Y 进一步钳制到 `0–25 m`。

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
