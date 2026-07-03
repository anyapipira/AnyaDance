#pragma once

namespace anyadance::ui {

// Vertical space consumed by the main footer: separator gap, language-control
// row, two help lines, and bottom padding. Keep this independent of ImGui so
// theme changes can be regression-tested without creating a graphics device.
constexpr float MainFooterHeightForMetrics(
    float fontSize,
    float framePaddingY,
    float itemSpacingY,
    float windowPaddingY) {
    return 1.0f + itemSpacingY +
           (fontSize + 2.0f * framePaddingY + itemSpacingY) +
           2.0f * (fontSize + itemSpacingY) +
           windowPaddingY;
}

} // namespace anyadance::ui
