#include "ui/ui_state.h"

#include <array>
#include <cstdio>
#include <string>

namespace anyadance::ui {

bool FingerBendsEqual(const FingerBends& lhs, const FingerBends& rhs) {
    return lhs.thumb == rhs.thumb &&
        lhs.index == rhs.index &&
        lhs.middle == rhs.middle &&
        lhs.ring == rhs.ring &&
        lhs.pinky == rhs.pinky;
}

bool ControllerEqual(const ControllerState& lhs, const ControllerState& rhs) {
    return lhs.trigger_click == rhs.trigger_click &&
        lhs.trigger_value == rhs.trigger_value &&
        lhs.menu_click == rhs.menu_click &&
        lhs.system_click == rhs.system_click &&
        lhs.a_click == rhs.a_click &&
        lhs.b_click == rhs.b_click &&
        lhs.grip_click == rhs.grip_click &&
        lhs.grip_value == rhs.grip_value &&
        lhs.joystick_x == rhs.joystick_x &&
        lhs.joystick_y == rhs.joystick_y &&
        lhs.trackpad_x == rhs.trackpad_x &&
        lhs.trackpad_y == rhs.trackpad_y &&
        lhs.has_finger_bends == rhs.has_finger_bends &&
        FingerBendsEqual(lhs.finger_bends, rhs.finger_bends);
}

bool ControllerChanged(const std::array<ControllerState, 2>& lhs, const std::array<ControllerState, 2>& rhs) {
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        if (!ControllerEqual(lhs[i], rhs[i])) {
            return true;
        }
    }
    return false;
}

void ReleaseMouseCapture();

void HandleFocusLost() {
    const auto previous = g_app.frame.controllers;
    ReleaseMouseCapture();
    // End any body-panel stick drag; NeutralizeControllerInputs recenters it.
    g_app.joystickActive = false;
    g_app.joystickX = 0.0f;
    g_app.joystickY = 0.0f;
    g_app.focused = false;
    g_app.keyboard.SetFocus(false);
    NeutralizeControllerInputs(g_app.frame);
    if (ControllerChanged(previous, g_app.frame.controllers)) {
        g_app.streamer.UpdateFrame(g_app.frame, En(Text::ReleaseReason), false);
    }
}

void ReleaseMouseCapture() {
    if (!g_app.captureActive) {
        return;
    }
    SetCursorPos(g_app.lastMouse.x, g_app.lastMouse.y);
    ReleaseCapture();
    if (g_app.cursorHidden) {
        while (ShowCursor(TRUE) < 0) {}
        g_app.cursorHidden = false;
    }
    g_app.captureActive = false;
}

bool IsKeyDown(int vk) {
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

// Translate the active mouse button chord into the manipulation modifier used by
// the capture loop.
int ModifiersForCaptureButton(int captureButton) {
    const bool rightDown = IsKeyDown(VK_RBUTTON);
    if (captureButton == VK_MBUTTON) {
        return ManipulationModifier_Ctrl | (rightDown ? ManipulationModifier_Shift : 0);
    }
    if (captureButton == VK_RBUTTON) {
        // Rig drag started with RMB: vertical move, or roll while MMB is chorded.
        return ManipulationModifier_Shift | (IsKeyDown(VK_MBUTTON) ? ManipulationModifier_Ctrl : 0);
    }
    if (captureButton == VK_LBUTTON && rightDown) {
        return ManipulationModifier_Shift;
    }
    return ManipulationModifier_None;
}

void BeginMouseCapture(HWND hwnd, DeviceIndex device, int captureButton) {
    g_app.captureActive = true;
    g_app.dragRig = false;
    g_app.dragDevice = device;
    g_app.captureButton = captureButton;
    g_app.captureModifiers = ModifiersForCaptureButton(captureButton);
    g_app.drag = BeginDrag(g_app.frame, device);
    GetCursorPos(&g_app.lastMouse);
    SetCapture(hwnd);
    while (ShowCursor(FALSE) >= 0) {}
    g_app.cursorHidden = true;
}

// Start a whole-rig drag from the empty body-panel area. The snapshot device is
// irrelevant for a rig drag; BeginDrag captures the full frame and the HMD yaw
// basis, which is all ApplyRigDragDelta uses.
void BeginRigMouseCapture(HWND hwnd, int captureButton) {
    BeginMouseCapture(hwnd, DeviceIndex::Hmd, captureButton);
    g_app.dragRig = true;
}

bool MirrorEnabledFor(DeviceIndex device) {
    if (device == DeviceIndex::LeftController || device == DeviceIndex::RightController) {
        return g_app.mirrorHands;
    }
    if (device == DeviceIndex::LeftFoot || device == DeviceIndex::RightFoot) {
        return g_app.mirrorFeet;
    }
    return false;
}

int CurrentModifiers() {
    return ModifiersForCaptureButton(g_app.captureButton);
}

void UpdateCapture(HWND hwnd) {
    if (!g_app.captureActive) {
        return;
    }
    const bool focused = GetForegroundWindow() == hwnd;
    const bool captureButtonDown = IsKeyDown(g_app.captureButton);
    const bool escapeDown = IsKeyDown(VK_ESCAPE);
    if (!focused || !captureButtonDown || escapeDown) {
        ReleaseMouseCapture();
        return;
    }

    POINT current{};
    GetCursorPos(&current);
    const float dx = static_cast<float>(current.x - g_app.lastMouse.x);
    const float dy = static_cast<float>(current.y - g_app.lastMouse.y);
    if (current.x != g_app.lastMouse.x || current.y != g_app.lastMouse.y) {
        SetCursorPos(g_app.lastMouse.x, g_app.lastMouse.y);
    }
    const int modifiers = CurrentModifiers();
    if (modifiers != g_app.captureModifiers) {
        g_app.drag = BeginDrag(g_app.frame, g_app.dragDevice);
        g_app.captureModifiers = modifiers;
    }
    if (dx != 0.0f || dy != 0.0f) {
        if (g_app.dragRig) {
            ApplyRigDragDelta(g_app.drag, g_app.frame, dx, dy, modifiers, g_app.manipulationFrame);
            g_app.streamer.UpdateFrame(g_app.frame, "Rig manipulated", true);
            return;
        }
        ApplyDragDelta(g_app.drag, g_app.frame, dx, dy, modifiers, g_app.manipulationFrame);
        DeviceIndex mirroredDevice = DeviceIndex::Hmd;
        if (MirrorEnabledFor(g_app.dragDevice) && MirroredDeviceFor(g_app.dragDevice, mirroredDevice)) {
            ApplySymmetricMirror(g_app.drag, g_app.frame, mirroredDevice, g_app.manipulationFrame);
        }
        // Name the dragged device so the log distinguishes, e.g., "Hip manipulated".
        const std::string reason =
            std::string(DeviceName(DeviceSlot(g_app.dragDevice), Language::English)) + " manipulated";
        g_app.streamer.UpdateFrame(g_app.frame, reason, true);
    }
}

void ApplyFingerBend(FrameState& frame) {
    for (std::size_t i = 0; i < frame.controllers.size(); ++i) {
        frame.controllers[i].has_finger_bends = true;
        frame.controllers[i].finger_bends = g_app.fingerBends[i];
        // A fully closed fist presses grip; releasing any finger releases it.
        ApplyFingerGrip(frame.controllers[i]);
    }
}

FrameState FrameWithCurrentFingerBends() {
    FrameState frame = g_app.frame;
    ApplyFingerBend(frame);
    return frame;
}

// Drive the right controller's thumbstick (and matching trackpad) directly, used
// by the body-panel stick drag. controllers[1] is the right controller.
void ApplyRightJoystick(FrameState& frame, float x, float y) {
    ControllerState& right = frame.controllers[1];
    right.joystick_x = x;
    right.joystick_y = y;
    right.trackpad_x = x;
    right.trackpad_y = y;
}

// The physical key name and the VR action it drives, kept separate so a log
// entry can read "E down (turn right)".
struct KeyInfo {
    const char* key;
    const char* action;
};

KeyInfo DescribeKey(ToolKey key) {
    switch (key) {
    case ToolKey::W: return {"W", "forward"};
    case ToolKey::A: return {"A", "left"};
    case ToolKey::S: return {"S", "back"};
    case ToolKey::D: return {"D", "right"};
    case ToolKey::Q: return {"Q", "turn left"};
    case ToolKey::E: return {"E", "turn right"};
    case ToolKey::Space: return {"Space", "jump"};
    case ToolKey::M: return {"M", "menu"};
    case ToolKey::V: return {"V", "voice"};
    case ToolKey::Z: return {"Z", "left trigger"};
    case ToolKey::X: return {"X", "right trigger"};
    case ToolKey::Count: break;
    }
    return {"Key", ""};
}

void HandleKeyboard(HWND hwnd) {
    // Track focus so releasing keys/inputs is reported when the window loses it.
    const bool focused = GetForegroundWindow() == hwnd;
    const bool textEditing = ImGui::GetIO().WantTextInput;
    if (focused != g_app.focused) {
        if (!focused) {
            HandleFocusLost();
        } else {
            g_app.focused = true;
            g_app.keyboard.SetFocus(true);
        }
    }
    g_app.keyboard.SetTextEditing(textEditing);
    if (!focused || textEditing) {
        return;
    }

    struct Binding { int vk; ToolKey key; };
    const Binding bindings[] = {
        {'W', ToolKey::W}, {'A', ToolKey::A}, {'S', ToolKey::S}, {'D', ToolKey::D},
        {'Q', ToolKey::Q}, {'E', ToolKey::E}, {VK_SPACE, ToolKey::Space}, {'M', ToolKey::M},
        {'V', ToolKey::V}, {'Z', ToolKey::Z}, {'X', ToolKey::X},
    };

    // Name each key whose state changed this frame, e.g. "W down (forward),
    // Z up (left trigger)", so the log says exactly what happened. Every key maps
    // straight to its button/axis, so a key edge is exactly the input edge.
    std::string reason;
    for (const Binding& binding : bindings) {
        const bool down = (GetAsyncKeyState(binding.vk) & 0x8000) != 0;
        if (g_app.keyboard.HandleKey(binding.key, down, false)) {
            if (!reason.empty()) {
                reason += ", ";
            }
            const KeyInfo info = DescribeKey(binding.key);
            reason += info.key;
            reason += down ? " down (" : " up (";
            reason += info.action;
            reason += ")";
        }
    }

    const auto previous = g_app.frame.controllers;
    g_app.keyboard.UpdateFrameInputs(g_app.frame);
    // While a dance plays it owns the finger bends. UpdateDancePlayback merges
    // keyboard inputs and streams the final frame. When paused, the dance loop
    // returns early, so we apply finger bends here so they are not lost from
    // the controller frame that UpdateFrameInputs just cleared.
    if (!g_app.dancePlaying || g_app.dancePaused) {
        ApplyFingerBend(g_app.frame);
    }
    // The keyboard rebuild above resets the right stick to the Q/E turn axis, so
    // re-apply an in-progress body-panel stick drag (updated each frame in
    // RenderBodyPanel) to keep it from snapping back to neutral.
    if (g_app.joystickActive) {
        ApplyRightJoystick(g_app.frame, g_app.joystickX, g_app.joystickY);
    }

    // A mouse-wheel finger bend leaves no key edge, so report it with its value.
    // Name the single finger when only one changed; otherwise call it "Fingers".
    if (reason.empty()) {
        int changedCount = 0;
        const FingerKey* changed = nullptr;
        for (const FingerKey& key : kFingerKeys) {
            if (FingerBendValue(g_app.fingerBends[key.hand], key.finger) !=
                FingerBendValue(g_app.lastFingerBends[key.hand], key.finger)) {
                ++changedCount;
                changed = &key;
            }
        }
        if (changedCount == 1) {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%s %.0f%%", changed->name,
                          FingerBendValue(g_app.fingerBends[changed->hand], changed->finger) * 100.0f);
            reason = buf;
        } else if (changedCount > 1) {
            reason = "Fingers";
        }
    }
    g_app.lastFingerBends[0] = g_app.fingerBends[0];
    g_app.lastFingerBends[1] = g_app.fingerBends[1];

    // Any remaining controller change with no attributed cause falls back to the
    // generic reason (defensive; the cases above normally cover every change).
    // When playing (not paused), UpdateDancePlayback is the sole streamer to avoid
    // dueling finger packets. When paused, UpdateDancePlayback returns early, so
    // stream controller changes here to keep joystick/WASD movement working.
    if ((!g_app.dancePlaying || g_app.dancePaused) && (!reason.empty() || ControllerChanged(previous, g_app.frame.controllers))) {
        if (reason.empty()) {
            reason = En(Text::KeyboardReason);
        }
        g_app.streamer.UpdateFrame(g_app.frame, reason, false);
    }
}

} // namespace anyadance::ui
