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
| `pose.position` | 三个数字的数组 | 必需。单位为米，处于驱动姿态空间；每个分量都必须是有限值且位于 `-10.0` 至 `10.0`，Y 还会被限制为最大 `2.0`。 |
| `pose.rotation_xyzw` | 四个数字的数组 | 必需。按 XYZW 排列的四元数；值必须有限，平方长度必须在 `0.5` 至 `1.5` 之间。接受后会被归一化。 |

`inputs` 对象是可选的。只有 `left_controller` 和 `right_controller` 的输入条目会影响 OpenVR 控制器状态。控制器输入条目中的每个成员都是可选的；省略时使用下表中的默认值或回退值。只有当同一数据报的 `devices` 中也存在对应控制器的有效条目时，输入条目才会被应用。如果设备条目存在但输入条目缺失，普通按钮和轴会重置为默认值；如果设备条目本身缺失或格式错误，则保留该设备之前的姿态与输入。

| 输入字段 | 类型 | 范围/默认值 | 驱动行为 |
| --- | --- | --- | --- |
| `trigger_click` | 布尔值 | 默认 `false` | 驱动 `/input/trigger/click`。 |
| `trigger_value` | 数字 | 钳制到 `0.0`–`1.0`；省略时，若 `trigger_click` 为 true 则为 `1.0`，否则为 `0.0` | 驱动 `/input/trigger/value`。 |
| `menu_click` | 布尔值 | 默认 `false` | 驱动 `/input/application_menu/click`。 |
| `system_click` | 布尔值 | 默认 `false` | 为版本 1 兼容性而接受并发送，但当前驱动未暴露 `/input/system/click` 组件，因此不会产生 OpenVR 效果。 |
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
- 位置分量绝对值超过 `10.0 m`
- 四元数平方长度超出可接受的 `0.5` 到 `1.5` 范围

被接受的四元数在使用前会被归一化。

## Y 钳制

所有设备都被钳制到最大 Y 值 `2.0 m`：

```cpp
position.y = std::min(position.y, 2.0f);
```

钳制作用于 Y。伴随 UI 在序列化前钳制。原生驱动在数据包校验后再次钳制，并对重复的钳制警告进行限频。

## 姿态存活

驱动不会让设备姿态超时。在第一个数据包之前，所有六个虚拟设备都以中性姿态开始且保持连接、有效。当数据包到达时，每个设备最新的有效数据包会更新其姿态与控制器输入。如果数据包停止，SteamVR 仍会看到设备在其最后姿态保持连接且有效。

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

`/input/grip/touch` 和 `/input/grip/force` 没有独立的线上字段；它们分别由 `grip_click` 和 `grip_value` 派生。骨骼组件的变换同样由 `finger_bends` 派生。
