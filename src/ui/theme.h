#pragma once

#include "core/constants.h"

#include "imgui.h"

namespace anyadance::ui {

// "Project Anya" semantic palette. Each hue is tied to a meaning so the UI reads
// at a glance: cool tones for poses, warm tones for files, green/amber/red for
// the risk ladder of driver actions, magenta for the dance feature.
namespace col {
inline const ImVec4 Rose   {0.886f, 0.420f, 0.612f, 1.00f};  // primary accent (Reset, focus)
inline const ImVec4 Teal   {0.247f, 0.714f, 0.784f, 1.00f};  // standing pose / hands
inline const ImVec4 Violet {0.608f, 0.447f, 0.878f, 1.00f};  // menu pose / hip
inline const ImVec4 Amber  {0.878f, 0.663f, 0.247f, 1.00f};  // save/load / head
inline const ImVec4 Green  {0.275f, 0.725f, 0.451f, 1.00f};  // register / feet / Sent
inline const ImVec4 Danger {0.886f, 0.412f, 0.247f, 1.00f};  // restart SteamVR (disruptive)
inline const ImVec4 Magenta{0.882f, 0.333f, 0.620f, 1.00f};  // dance (MMD)
inline const ImVec4 Slate  {0.420f, 0.451f, 0.510f, 1.00f};  // unregister / neutral
inline const ImVec4 Red    {0.851f, 0.325f, 0.310f, 1.00f};  // failed / clear
}  // namespace col

// Install the Anya theme over ImGui's dark base (rounded, indigo night-sky bg,
// rose accent). Call once after the ImGui context exists.
void ApplyAnyaTheme();

// RAII tint for the button(s) drawn within its scope: pushes Button/Hovered/Active
// derived from one semantic base color, pops them on destruction.
struct ScopedButtonColor {
    explicit ScopedButtonColor(const ImVec4& base);
    ~ScopedButtonColor();

    ScopedButtonColor(const ScopedButtonColor&) = delete;
    ScopedButtonColor& operator=(const ScopedButtonColor&) = delete;
};

// Accent color for a device card, grouped by body region (head / hands / hip /
// feet) so the body panel is scannable.
ImVec4 DeviceRegionColor(DeviceIndex device);

}  // namespace anyadance::ui
