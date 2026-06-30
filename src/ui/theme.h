#pragma once

#include "core/constants.h"

#include "imgui.h"

namespace anyadance::ui {

// "Project Anya" palette. Buttons use just two tints — a rose Primary for the
// hero actions and a calm azure Secondary for everything else. The region colors
// below are reserved for the body device cards, and Green/Red tint the log result.
namespace col {
inline const ImVec4 Primary  {0.886f, 0.420f, 0.612f, 1.00f};  // hero buttons (poses, play, dance)
inline const ImVec4 Secondary{0.255f, 0.553f, 0.761f, 1.00f};  // utility/system buttons

// Device card accents, grouped by body region.
inline const ImVec4 Teal   {0.247f, 0.714f, 0.784f, 1.00f};  // hands
inline const ImVec4 Violet {0.608f, 0.447f, 0.878f, 1.00f};  // hip
inline const ImVec4 Amber  {0.878f, 0.663f, 0.247f, 1.00f};  // head
inline const ImVec4 Green  {0.275f, 0.725f, 0.451f, 1.00f};  // feet / Sent
inline const ImVec4 Red    {0.851f, 0.325f, 0.310f, 1.00f};  // failed result

// Accent reused by the theme itself (checkmarks, sliders, selection).
inline const ImVec4 Rose = Primary;
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
