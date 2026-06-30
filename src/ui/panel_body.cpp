#include "ui/ui_state.h"

#include <algorithm>
#include <string>

namespace anyadance::ui {

// Draw the faint body silhouette behind the device boxes, centered and scaled
// to the panel height while preserving the image's aspect ratio.
void DrawBodyBackground(const ImVec2& panelMin, const ImVec2& panelSize) {
    if (!g_bodyBackgroundTexture || g_bodyBackgroundWidth <= 0 || g_bodyBackgroundHeight <= 0 ||
        panelSize.x <= 0.0f || panelSize.y <= 0.0f) {
        return;
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 panelMax(panelMin.x + panelSize.x, panelMin.y + panelSize.y);
    const float imageAspect = static_cast<float>(g_bodyBackgroundWidth) / static_cast<float>(g_bodyBackgroundHeight);
    const float drawHeight = panelSize.y;
    const float drawWidth = drawHeight * imageAspect;
    const ImVec2 imageMin(panelMin.x + (panelSize.x - drawWidth) * 0.5f, panelMin.y);
    const ImVec2 imageMax(imageMin.x + drawWidth, imageMin.y + drawHeight);
    draw->PushClipRect(panelMin, panelMax, true);
    draw->AddImage(
        reinterpret_cast<ImTextureID>(g_bodyBackgroundTexture),
        imageMin,
        imageMax,
        ImVec2(0.0f, 0.0f),
        ImVec2(1.0f, 1.0f),
        IM_COL32(255, 255, 255, 92));
    draw->PopClipRect();
}

// One-line summaries shown inside each device box: position (x y z) and the
// orientation quaternion laid out as a single x y z w row.
std::string PoseSummary(const DeviceState& device) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "P %.2f %.2f %.2f", device.position.x, device.position.y, device.position.z);
    return buf;
}

std::string RotationSummary(const DeviceState& device) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "Q %.2f %.2f %.2f %.2f",
                  device.rotation.x, device.rotation.y, device.rotation.z, device.rotation.w);
    return buf;
}

// Draw one device's box and start a mouse drag when it is left- or middle-clicked,
// so the box doubles as a grab handle.
void DeviceBox(HWND hwnd, DeviceIndex deviceIndex, ImVec2 size, bool miniMode = false) {
    const std::size_t slot = DeviceSlot(deviceIndex);
    DeviceState& device = g_app.frame.devices[slot];
    ImGui::InvisibleButton(kDevices[slot].id, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool leftClicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool middleClicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle);
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    // Region accent (head/hands/hip/feet) tints the card so the body reads at a
    // glance: a dark accent-tinted fill, an accent border that brightens on hover,
    // and the device name in a light accent.
    const ImVec4 accent = DeviceRegionColor(deviceIndex);
    const auto chan = [](float v) { return static_cast<int>(v * 255.0f + 0.5f); };
    const ImU32 bg = IM_COL32(chan(accent.x * 0.22f + 0.10f), chan(accent.y * 0.22f + 0.10f),
                              chan(accent.z * 0.22f + 0.12f), 150);
    const ImU32 border = hovered
        ? IM_COL32(chan(accent.x), chan(accent.y), chan(accent.z), 255)
        : IM_COL32(chan(accent.x * 0.72f), chan(accent.y * 0.72f), chan(accent.z * 0.72f), 220);
    const ImU32 nameCol = IM_COL32(chan(accent.x * 0.45f + 0.55f), chan(accent.y * 0.45f + 0.55f),
                                   chan(accent.z * 0.45f + 0.55f), 255);
    draw->AddRectFilled(min, max, bg, 6.0f);
    draw->AddRect(min, max, border, 6.0f, 0, hovered ? 2.0f : 1.0f);
    ImVec2 textPos = ImVec2(min.x + 10.0f, min.y + 8.0f);
    draw->AddText(textPos, nameCol, DeviceName(slot));
    textPos.y += 20.0f;
    if (!miniMode) {
        const std::string pos = PoseSummary(device);
        draw->AddText(textPos, IM_COL32(198, 205, 214, 255), pos.c_str());
        textPos.y += 18.0f;
        const std::string rot = RotationSummary(device);
        draw->AddText(textPos, IM_COL32(198, 205, 214, 255), rot.c_str());
        textPos.y += 18.0f;
    }
    if (g_app.captureActive && g_app.dragDevice == deviceIndex) {
        draw->AddText(textPos, IM_COL32(107, 203, 119, 255), Tr(Text::Capture));
        textPos.y += 18.0f;
    }
    if (device.position.y >= kMaxDeviceY || device.y_clamped) {
        draw->AddText(ImVec2(max.x - 56.0f, min.y + 8.0f), IM_COL32(255, 196, 87, 255), Tr(Text::YMax));
    }
    if (!miniMode && deviceIndex == DeviceIndex::Hmd) {
        draw->AddText(textPos, IM_COL32(164, 174, 187, 255), Tr(Text::HmdHelp));
    }
    if (leftClicked) {
        BeginMouseCapture(hwnd, deviceIndex, VK_LBUTTON);
    } else if (middleClicked) {
        BeginMouseCapture(hwnd, deviceIndex, VK_MBUTTON);
    }
}

void MirrorCheckbox(const char* id, bool* value, float rowHeight) {
    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(1.0f, rowHeight * 0.34f));
    ImGui::PushID(id);
    ImGui::Checkbox(Tr(Text::Mirror), value);
    ImGui::PopID();
    ImGui::EndGroup();
}

// Left half of the window: the mouse-help line and the six device boxes laid out
// over the body silhouette (HMD centered, controllers and feet paired with a
// Mirror checkbox between them, hip centered).
void RenderBodyPanel(HWND hwnd, bool miniMode) {
    const float availableW = ImGui::GetContentRegionAvail().x;
    float panelW = availableW;
    if (!miniMode) {
        const float logMinW = 330.0f;
        panelW = std::clamp(availableW * 0.46f, 450.0f, 620.0f);
        panelW = std::min(panelW, std::max(450.0f, availableW - logMinW));
    }

    ImGui::BeginChild("body", ImVec2(panelW, 0), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImVec2 bodyPanelMin = ImGui::GetWindowPos();
    const ImVec2 bodyPanelSize = ImGui::GetWindowSize();
    DrawBodyBackground(bodyPanelMin, bodyPanelSize);

    // The mouse wheel over the body bends the fingers. IsWindowHovered respects
    // z-order, so a log detail window on top owns wheel scrolling for its JSON
    // view. The body child uses NoScrollWithMouse, making this the sole body
    // wheel path.
    const ImGuiIO& io = ImGui::GetIO();
    if (io.MouseWheel != 0.0f && !io.WantTextInput &&
        ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
        // Scroll up opens (decreases bend), scroll down closes, matching the prior
        // all-finger behavior. Holding number keys narrows the wheel to just those
        // fingers; otherwise every finger on both hands moves together.
        const float increment = -io.MouseWheel * kFingerBendStep;
        bool anyFingerKey = false;
        for (const FingerKey& key : kFingerKeys) {
            if (IsKeyDown(key.vk)) {
                AdjustFingerBend(g_app.fingerBends[key.hand], key.finger, increment);
                anyFingerKey = true;
            }
        }
        if (!anyFingerKey) {
            AdjustAllFingerBends(g_app.fingerBends[0], g_app.fingerBends[1], increment);
        }
    }
    if (!miniMode) {
        const float instructionH = ImGui::GetTextLineHeightWithSpacing() * 2.0f;
        ImGui::BeginChild("mouse_help", ImVec2(panelW - 12.0f, instructionH), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + panelW - 12.0f);
        ImGui::TextUnformatted(Tr(Text::MouseHelp));
        ImGui::PopTextWrapPos();
        ImGui::EndChild();

        // Manipulation frame: HMD moves/rotates along the head heading, Global along
        // fixed world axes. Vertical translation is world up either way.
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(Tr(Text::FrameLabel));
        ImGui::SameLine();
        if (ImGui::RadioButton(Tr(Text::FrameHmd), g_app.manipulationFrame == ManipulationFrame::Hmd)) {
            g_app.manipulationFrame = ManipulationFrame::Hmd;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton(Tr(Text::FrameGlobal), g_app.manipulationFrame == ManipulationFrame::Global)) {
            g_app.manipulationFrame = ManipulationFrame::Global;
        }
    }

    const float rowsTopY = ImGui::GetCursorPosY() + (miniMode ? 4.0f : 10.0f);
    const float rowsAvailH = std::max(0.0f, ImGui::GetContentRegionAvail().y - 10.0f);
    const float pairGap = 8.0f;
    const float rowGap = std::clamp(rowsAvailH * 0.018f, 8.0f, 14.0f);
    const float boxH = std::max(1.0f, (rowsAvailH - rowGap * 3.0f) * 0.25f);
    const float mirrorW = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize(Tr(Text::Mirror)).x;
    const float minBoxW = miniMode ? 72.0f : 150.0f;
    const float maxBoxW = std::max(minBoxW, std::min(240.0f, (panelW - mirrorW - pairGap * 2.0f) * 0.5f));
    const float boxW = std::clamp((panelW - mirrorW - pairGap * 2.0f) * 0.5f, minBoxW, maxBoxW);
    const float pairRowW = boxW * 2.0f + mirrorW + pairGap * 2.0f;
    const float pairLeftX = std::max(0.0f, (panelW - pairRowW) * 0.5f);
    const float centerX = std::max(0.0f, (panelW - boxW) * 0.5f);
    const auto rowY = [rowsTopY, boxH, rowGap](int row) {
        return rowsTopY + static_cast<float>(row) * (boxH + rowGap);
    };

    ImGui::SetCursorPos(ImVec2(centerX, rowY(0)));
    DeviceBox(hwnd, DeviceIndex::Hmd, ImVec2(boxW, boxH), miniMode);
    ImGui::SetCursorPos(ImVec2(pairLeftX, rowY(1)));
    DeviceBox(hwnd, DeviceIndex::LeftController, ImVec2(boxW, boxH), miniMode);
    ImGui::SameLine(0.0f, pairGap);
    MirrorCheckbox("hands", &g_app.mirrorHands, boxH);
    ImGui::SameLine(0.0f, pairGap);
    DeviceBox(hwnd, DeviceIndex::RightController, ImVec2(boxW, boxH), miniMode);
    ImGui::SetCursorPos(ImVec2(centerX, rowY(2)));
    DeviceBox(hwnd, DeviceIndex::Hip, ImVec2(boxW, boxH), miniMode);
    ImGui::SetCursorPos(ImVec2(pairLeftX, rowY(3)));
    DeviceBox(hwnd, DeviceIndex::LeftFoot, ImVec2(boxW, boxH), miniMode);
    ImGui::SameLine(0.0f, pairGap);
    MirrorCheckbox("feet", &g_app.mirrorFeet, boxH);
    ImGui::SameLine(0.0f, pairGap);
    DeviceBox(hwnd, DeviceIndex::RightFoot, ImVec2(boxW, boxH), miniMode);
    ImGui::SetCursorPosY(rowY(3) + boxH);

    // Empty area of the body panel acts as the right thumbstick: left-press sets
    // the neutral center, then dragging deflects the stick within [-1, 1]. This
    // lets the user aim the right-hand quick menu (opened by holding M) and pick
    // items. The ImGui Win32 backend captures the mouse while a button is held,
    // so the drag keeps tracking even when the cursor leaves the panel.
    const bool overEmptyBody = ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered();
    if (!g_app.captureActive && !g_app.joystickActive && overEmptyBody &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        g_app.joystickActive = true;
        g_app.joystickOrigin = ImGui::GetIO().MousePos;
        g_app.joystickX = 0.0f;
        g_app.joystickY = 0.0f;
    }
    if (g_app.joystickActive) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            g_app.joystickActive = false;
            g_app.joystickX = 0.0f;
            g_app.joystickY = 0.0f;
            ApplyRightJoystick(g_app.frame, 0.0f, 0.0f);
            g_app.streamer.UpdateFrame(g_app.frame, "Right stick release", false);
        } else {
            // Deflection from the press point; screen Y grows downward, so negate
            // it to make dragging up push the stick forward (+Y).
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const float x = std::clamp((mouse.x - g_app.joystickOrigin.x) / kJoystickDragRangePixels, -1.0f, 1.0f);
            const float y = std::clamp((g_app.joystickOrigin.y - mouse.y) / kJoystickDragRangePixels, -1.0f, 1.0f);
            if (x != g_app.joystickX || y != g_app.joystickY) {
                g_app.joystickX = x;
                g_app.joystickY = y;
                ApplyRightJoystick(g_app.frame, x, y);
                g_app.streamer.UpdateFrame(g_app.frame, "Right stick", true);
            }
            // Guide overlay: the range ring at the press point and a knob at the
            // current deflection.
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const ImVec2 origin = g_app.joystickOrigin;
            const ImVec2 knob(origin.x + g_app.joystickX * kJoystickDragRangePixels,
                              origin.y - g_app.joystickY * kJoystickDragRangePixels);
            draw->AddCircle(origin, kJoystickDragRangePixels, IM_COL32(118, 161, 220, 130), 0, 1.5f);
            draw->AddLine(origin, knob, IM_COL32(118, 161, 220, 160), 1.5f);
            draw->AddCircleFilled(knob, 7.0f, IM_COL32(107, 203, 119, 235));
        }
    }
    ImGui::EndChild();
}

} // namespace anyadance::ui
