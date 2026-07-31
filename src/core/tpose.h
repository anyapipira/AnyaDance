#pragma once

#include "core/frame_state.h"

namespace anyadance {

struct TPoseConstants {
    // Plane-mirrored across the body's sagittal (X) plane: left/right share Y and
    // Z, only X flips (matching ApplySymmetricMirror). The shared Z places both
    // hands slightly forward.
    Vec3 leftControllerOffset{-0.68f, -0.17f, -0.10f};
    Vec3 rightControllerOffset{0.68f, -0.17f, -0.10f};
    Vec3 hipOffset{0.0f, -0.43f, -0.05f};
    Vec3 leftFootOffset{-0.09f, -1.24f, 0.10f};
    Vec3 rightFootOffset{0.09f, -1.24f, 0.10f};
};

inline constexpr TPoseConstants kDefaultTPoseConstants{};
inline constexpr Quat kLeftControllerCanonicalRotation{0.0f, 0.0f, -0.7071067811865475f, 0.7071067811865475f};
inline constexpr Quat kRightControllerCanonicalRotation{0.0f, 0.0f, 0.7071067811865475f, 0.7071067811865475f};

FrameState BuildResetTPose(const FrameState& current, TPoseConstants constants = kDefaultTPoseConstants);

} // namespace anyadance
