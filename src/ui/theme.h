#pragma once

#include "core/constants.h"

#include "imgui.h"

namespace anyadance::ui {

// "Project Anya" palette: a gold + blue scheme echoing the banner. Buttons use at
// most three tints, and within a pair the two members differ so they read apart:
//   Primary (gold)    - go / create / first-of-pair  (Save, Play, Register, ...)
//   Secondary (blue)  - neutral / second-of-pair      (Load, Pause, Unregister, ...)
//   Tertiary (red)    - stop / destructive            (Stop, Restart)
// The region colors below are reserved for the body device cards, and Green/Red
// tint the log result.
namespace col {
inline const ImVec4 Primary  {0.745f, 0.561f, 0.220f, 1.00f};  // gold
inline const ImVec4 Secondary{0.200f, 0.353f, 0.522f, 1.00f};  // blue
inline const ImVec4 Tertiary {0.710f, 0.337f, 0.294f, 1.00f};  // red (stop/destructive)

// Theme accent for checkmarks, radios, sliders, selection.
inline const ImVec4 Accent   {0.850f, 0.700f, 0.350f, 1.00f};  // gold

// Device card accents, grouped by body region.
inline const ImVec4 Teal   {0.247f, 0.714f, 0.784f, 1.00f};  // hands
inline const ImVec4 Violet {0.608f, 0.447f, 0.878f, 1.00f};  // hip
inline const ImVec4 Amber  {0.878f, 0.663f, 0.247f, 1.00f};  // head
inline const ImVec4 Green  {0.275f, 0.725f, 0.451f, 1.00f};  // feet / Sent
inline const ImVec4 Red    {0.851f, 0.325f, 0.310f, 1.00f};  // failed result
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
