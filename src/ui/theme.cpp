#include "ui/theme.h"

#include <algorithm>

namespace anyadance::ui {
namespace {

ImVec4 Shade(const ImVec4& c, float factor) {
    return ImVec4(std::min(c.x * factor, 1.0f), std::min(c.y * factor, 1.0f),
                  std::min(c.z * factor, 1.0f), c.w);
}

ImVec4 WithAlpha(const ImVec4& c, float alpha) {
    return ImVec4(c.x, c.y, c.z, alpha);
}

}  // namespace

void ApplyAnyaTheme() {
    ImGui::StyleColorsDark();

    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 6.0f;
    s.ChildRounding = 6.0f;
    s.FrameRounding = 5.0f;
    s.PopupRounding = 6.0f;
    s.GrabRounding = 5.0f;
    s.TabRounding = 5.0f;
    s.ScrollbarRounding = 6.0f;
    s.FrameBorderSize = 0.0f;
    s.WindowBorderSize = 0.0f;
    s.FramePadding = ImVec2(8.0f, 5.0f);
    s.ItemSpacing = ImVec2(8.0f, 6.0f);

    // Indigo night-sky base with a cool sky-blue accent.
    const ImVec4 bg{0.075f, 0.071f, 0.110f, 1.00f};
    const ImVec4 bgLight{0.130f, 0.125f, 0.190f, 1.00f};
    const ImVec4 bgLift{0.170f, 0.162f, 0.240f, 1.00f};
    const ImVec4 text{0.925f, 0.930f, 0.960f, 1.00f};

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = text;
    c[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.52f, 0.58f, 1.00f);
    c[ImGuiCol_WindowBg] = bg;
    c[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);  // let body/banner art show
    c[ImGuiCol_PopupBg] = ImVec4(0.100f, 0.095f, 0.150f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(0.26f, 0.26f, 0.36f, 0.50f);
    c[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_FrameBg] = bgLight;
    c[ImGuiCol_FrameBgHovered] = Shade(bgLift, 1.15f);
    c[ImGuiCol_FrameBgActive] = WithAlpha(col::Accent, 0.45f);
    c[ImGuiCol_TitleBg] = bg;
    c[ImGuiCol_TitleBgActive] = bgLift;
    c[ImGuiCol_TitleBgCollapsed] = bg;
    c[ImGuiCol_MenuBarBg] = bgLight;
    c[ImGuiCol_Button] = bgLift;
    c[ImGuiCol_ButtonHovered] = Shade(bgLift, 1.30f);
    c[ImGuiCol_ButtonActive] = WithAlpha(col::Accent, 0.55f);
    c[ImGuiCol_Header] = WithAlpha(col::Accent, 0.32f);
    c[ImGuiCol_HeaderHovered] = WithAlpha(col::Accent, 0.50f);
    c[ImGuiCol_HeaderActive] = WithAlpha(col::Accent, 0.68f);
    c[ImGuiCol_CheckMark] = col::Accent;
    c[ImGuiCol_SliderGrab] = col::Accent;
    c[ImGuiCol_SliderGrabActive] = Shade(col::Accent, 1.15f);
    c[ImGuiCol_Separator] = ImVec4(0.26f, 0.26f, 0.36f, 0.50f);
    c[ImGuiCol_SeparatorHovered] = WithAlpha(col::Accent, 0.60f);
    c[ImGuiCol_SeparatorActive] = WithAlpha(col::Accent, 0.85f);
    c[ImGuiCol_ResizeGrip] = WithAlpha(col::Accent, 0.22f);
    c[ImGuiCol_ResizeGripHovered] = WithAlpha(col::Accent, 0.55f);
    c[ImGuiCol_ResizeGripActive] = WithAlpha(col::Accent, 0.85f);
    c[ImGuiCol_Tab] = bgLight;
    c[ImGuiCol_TabHovered] = WithAlpha(col::Accent, 0.55f);
    c[ImGuiCol_TabActive] = bgLift;
    c[ImGuiCol_TabUnfocused] = bgLight;
    c[ImGuiCol_TabUnfocusedActive] = bgLift;
    c[ImGuiCol_TextSelectedBg] = WithAlpha(col::Accent, 0.35f);
    c[ImGuiCol_NavHighlight] = col::Accent;
    c[ImGuiCol_TableHeaderBg] = bgLift;
    c[ImGuiCol_TableBorderStrong] = ImVec4(0.26f, 0.26f, 0.36f, 0.70f);
    c[ImGuiCol_TableBorderLight] = ImVec4(0.20f, 0.20f, 0.28f, 0.50f);
    c[ImGuiCol_TableRowBg] = ImVec4(1.0f, 1.0f, 1.0f, 0.012f);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.035f);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_ScrollbarGrab] = bgLift;
    c[ImGuiCol_ScrollbarGrabHovered] = WithAlpha(col::Accent, 0.50f);
    c[ImGuiCol_ScrollbarGrabActive] = WithAlpha(col::Accent, 0.75f);
}

ScopedButtonColor::ScopedButtonColor(const ImVec4& base) {
    ImGui::PushStyleColor(ImGuiCol_Button, WithAlpha(base, 0.82f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, WithAlpha(Shade(base, 1.15f), 0.95f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, Shade(base, 0.82f));
}

ScopedButtonColor::~ScopedButtonColor() {
    ImGui::PopStyleColor(3);
}

ImVec4 DeviceRegionColor(DeviceIndex device) {
    switch (device) {
    case DeviceIndex::Hmd:
        return col::Amber;
    case DeviceIndex::LeftController:
    case DeviceIndex::RightController:
        return col::Teal;
    case DeviceIndex::Hip:
        return col::Violet;
    case DeviceIndex::LeftFoot:
    case DeviceIndex::RightFoot:
        return col::Green;
    }
    return col::Secondary;
}

}  // namespace anyadance::ui
