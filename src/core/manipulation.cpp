#include "core/manipulation.h"

#include <algorithm>
#include <limits>

namespace anyadance {
namespace {

Quat RotationDelta(float yaw, float pitch, float roll) {
    Quat delta = FromYaw(yaw);
    if (pitch != 0.0f) {
        delta = Multiply(delta, FromAxisAngle({1.0f, 0.0f, 0.0f}, pitch));
    }
    if (roll != 0.0f) {
        delta = Multiply(delta, FromAxisAngle({0.0f, 0.0f, 1.0f}, roll));
    }
    return Normalized(delta);
}

void ApplyRotation(DeviceState& device, Quat startRotation, Quat yawBasis, float yawCounts, float pitchCounts, float rollCounts) {
    const float yaw = -yawCounts * DegToRad(kRotationDegreesPerCount);
    const float pitch = ClampFloat(-pitchCounts * kRotationDegreesPerCount, -kPitchLimitDegrees, kPitchLimitDegrees);
    const float roll = rollCounts * DegToRad(kRotationDegreesPerCount);
    // Rotate about the head-aligned axes so pitch/roll match where the head faces,
    // consistent with the yaw-basis translation. Conjugating by the basis re-expresses
    // the world-axis delta about the basis axes; yaw (about Y) is left unchanged since
    // the basis is itself a yaw rotation.
    const Quat delta = RotationDelta(yaw, DegToRad(pitch), roll);
    const Quat deltaInBasis = Normalized(Multiply(Multiply(yawBasis, delta), Conjugate(yawBasis)));
    device.rotation = Normalized(Multiply(deltaInBasis, startRotation));
}

Vec3 Subtract(Vec3 lhs, Vec3 rhs) {
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

// Translate Y by the accumulated vertical drag, clamped to the shared ceiling
// and the caller's floor. The floor is device-dependent, so the caller supplies
// it. When the target overshoots a bound, re-anchor the accumulator so a
// reversed drag moves away immediately, and report that a bound was hit — the
// drag absorbs the overshoot, so the returned value alone cannot show it.
float ClampedDragY(float startY, float& accumulatedDy, float floorY, bool& clamped) {
    float targetY = startY - accumulatedDy * kTranslationMetersPerCount;
    clamped = false;
    if (targetY > kMaxDeviceY) {
        accumulatedDy = (startY - kMaxDeviceY) / kTranslationMetersPerCount;
        targetY = kMaxDeviceY;
        clamped = true;
    } else if (targetY < floorY) {
        accumulatedDy = (startY - floorY) / kTranslationMetersPerCount;
        targetY = floorY;
        clamped = true;
    }
    return targetY;
}

// For callers that bound a whole-rig move: hitting the bound stops every device
// as a unit rather than overriding any single device's Y, so there is no
// per-device clamp to report.
float ClampedDragY(float startY, float& accumulatedDy, float floorY) {
    bool clamped = false;
    return ClampedDragY(startY, accumulatedDy, floorY, clamped);
}

Quat MirrorRotationInYawBasis(Quat worldRotation, float yawBasisRadians) {
    const Quat yawBasis = FromYaw(yawBasisRadians);
    Quat localRotation = Normalized(Multiply(Conjugate(yawBasis), worldRotation));
    localRotation.y = -localRotation.y;
    localRotation.z = -localRotation.z;
    return Normalized(Multiply(yawBasis, localRotation));
}

} // namespace

DragSnapshot BeginDrag(const FrameState& frame, DeviceIndex device) {
    DragSnapshot drag{};
    drag.startFrame = frame;
    drag.device = device;
    drag.hmdYawBasis = YawFromQuaternion(frame.devices[DeviceSlot(DeviceIndex::Hmd)].rotation);
    return drag;
}

void ApplyDragDelta(DragSnapshot& drag, FrameState& frame, float dxCounts, float dyCounts, int modifiers,
                    ManipulationFrame manipulationFrame) {
    drag.accumulatedDx += dxCounts;
    drag.accumulatedDy += dyCounts;

    const bool ctrl = (modifiers & ManipulationModifier_Ctrl) != 0;
    const bool shift = (modifiers & ManipulationModifier_Shift) != 0;
    const std::size_t slot = DeviceSlot(drag.device);
    DeviceState device = drag.startFrame.devices[slot];
    // Both translation and rotation act in this basis: the head heading in Hmd mode,
    // or fixed world axes (identity) in Global mode. Vertical translation is world up
    // either way.
    const Quat basis = manipulationFrame == ManipulationFrame::Global ? FromYaw(0.0f) : FromYaw(drag.hmdYawBasis);

    if (drag.device == DeviceIndex::Hmd) {
        device.position = drag.startFrame.devices[slot].position;
        if (ctrl && shift) {
            ApplyRotation(device, drag.startFrame.devices[slot].rotation, basis, 0.0f, 0.0f, drag.accumulatedDx);
        } else if (shift) {
            // The head allows vertical (Y) translation; horizontal and depth stay
            // locked so the play-space origin does not drift.
            bool clamped = false;
            device.position.y = ClampedDragY(drag.startFrame.devices[slot].position.y, drag.accumulatedDy,
                                             kMinHmdY, clamped);
            device.y_clamped = clamped;
        } else {
            ApplyRotation(device, drag.startFrame.devices[slot].rotation, basis, drag.accumulatedDx, drag.accumulatedDy, 0.0f);
        }
        frame.devices[slot] = device;
        return;
    }

    bool clamped = false;
    if (ctrl && shift) {
        ApplyRotation(device, drag.startFrame.devices[slot].rotation, basis, 0.0f, 0.0f, drag.accumulatedDx);
    } else if (ctrl) {
        ApplyRotation(device, drag.startFrame.devices[slot].rotation, basis, drag.accumulatedDx, drag.accumulatedDy, 0.0f);
    } else if (shift) {
        const Vec3 forward = Rotate(basis, {0.0f, 0.0f, 1.0f});
        device.position = Add(device.position, Scale(forward, drag.accumulatedDy * kTranslationMetersPerCount));
    } else {
        const Vec3 right = Rotate(basis, {1.0f, 0.0f, 0.0f});
        device.position = Add(device.position, Scale(right, drag.accumulatedDx * kTranslationMetersPerCount));
        device.position.y = ClampedDragY(drag.startFrame.devices[slot].position.y, drag.accumulatedDy,
                                         MinDeviceY(drag.device), clamped);
    }

    // y_clamped means the safety clamp overrode what the gesture asked for, so
    // it compares against the requested Y. Comparing against the pre-drag Y
    // would flag every gesture that legitimately moves a device vertically.
    const float requestedY = device.position.y;
    device.position.y = ClampDeviceY(drag.device, requestedY);
    device.y_clamped = clamped || device.position.y != requestedY;
    frame.devices[slot] = device;
}

void ApplyRigDragDelta(DragSnapshot& drag, FrameState& frame, float dxCounts, float dyCounts, int modifiers,
                       ManipulationFrame manipulationFrame) {
    drag.accumulatedDx += dxCounts;
    drag.accumulatedDy += dyCounts;

    const bool ctrl = (modifiers & ManipulationModifier_Ctrl) != 0;
    const bool shift = (modifiers & ManipulationModifier_Shift) != 0;
    const Quat basis = manipulationFrame == ManipulationFrame::Global ? FromYaw(0.0f) : FromYaw(drag.hmdYawBasis);
    const Vec3 pivot = drag.startFrame.devices[DeviceSlot(DeviceIndex::Hmd)].position;

    if (ctrl) {
        // Ctrl+Shift rolls; Ctrl alone yaws/pitches. Same per-count scale and
        // pitch clamp as the single-device gestures, rotating positions and
        // orientations together about the HMD pivot so the rig stays rigid.
        const bool roll = shift;
        const float yawRadians = roll ? 0.0f : -drag.accumulatedDx * DegToRad(kRotationDegreesPerCount);
        const float pitchRadians = roll ? 0.0f
            : DegToRad(ClampFloat(-drag.accumulatedDy * kRotationDegreesPerCount, -kPitchLimitDegrees, kPitchLimitDegrees));
        const float rollRadians = roll ? drag.accumulatedDx * DegToRad(kRotationDegreesPerCount) : 0.0f;
        const Quat delta = RotationDelta(yawRadians, pitchRadians, rollRadians);
        const Quat deltaInBasis = Normalized(Multiply(Multiply(basis, delta), Conjugate(basis)));
        for (std::size_t slot = 0; slot < frame.devices.size(); ++slot) {
            const DeviceState& start = drag.startFrame.devices[slot];
            DeviceState device = start;
            device.position = Add(pivot, Rotate(deltaInBasis, Subtract(start.position, pivot)));
            device.rotation = Normalized(Multiply(deltaInBasis, start.rotation));
            // Rotating about the pivot moves devices vertically by design, so the
            // clamp flag compares against the rotated Y, not the pre-rotation Y.
            const float rotatedY = device.position.y;
            device.position.y = ClampDeviceY(slot, rotatedY);
            device.y_clamped = device.position.y != rotatedY;
            frame.devices[slot] = device;
        }
        return;
    }

    // Vertical move: shift the whole rig on world Y. Express the bounds in the
    // highest device's terms so the rig remains rigid within the Y range. The
    // ceiling belongs to the highest device, but the floor is per-device, so it
    // is the tightest of the shifts each device can still absorb — in practice
    // the HMD, the only device held above the ground plane.
    float highestStartY = drag.startFrame.devices[0].position.y;
    for (const DeviceState& start : drag.startFrame.devices) {
        highestStartY = std::max(highestStartY, start.position.y);
    }
    float minimumDeltaY = MinDeviceY(std::size_t{0}) - drag.startFrame.devices[0].position.y;
    for (std::size_t slot = 0; slot < drag.startFrame.devices.size(); ++slot) {
        minimumDeltaY = std::max(minimumDeltaY,
                                 MinDeviceY(slot) - drag.startFrame.devices[slot].position.y);
    }
    const float rigFloorY = highestStartY + minimumDeltaY;
    const float deltaY = ClampedDragY(highestStartY, drag.accumulatedDy, rigFloorY) - highestStartY;
    for (std::size_t slot = 0; slot < frame.devices.size(); ++slot) {
        const DeviceState& start = drag.startFrame.devices[slot];
        DeviceState device = start;
        device.position.y = ClampDeviceY(slot, start.position.y + deltaY);
        device.y_clamped = device.position.y != start.position.y + deltaY;
        frame.devices[slot] = device;
    }
}

bool MirroredDeviceFor(DeviceIndex device, DeviceIndex& mirroredDevice) {
    switch (device) {
    case DeviceIndex::LeftController:
        mirroredDevice = DeviceIndex::RightController;
        return true;
    case DeviceIndex::RightController:
        mirroredDevice = DeviceIndex::LeftController;
        return true;
    case DeviceIndex::LeftFoot:
        mirroredDevice = DeviceIndex::RightFoot;
        return true;
    case DeviceIndex::RightFoot:
        mirroredDevice = DeviceIndex::LeftFoot;
        return true;
    default:
        return false;
    }
}

void ApplySymmetricMirror(const DragSnapshot& drag, FrameState& frame, DeviceIndex mirroredDevice,
                          ManipulationFrame manipulationFrame) {
    const std::size_t activeSlot = DeviceSlot(drag.device);
    const std::size_t mirroredSlot = DeviceSlot(mirroredDevice);
    const DeviceState& active = frame.devices[activeSlot];
    DeviceState mirrored = frame.devices[mirroredSlot];

    const float yawBasisRadians = manipulationFrame == ManipulationFrame::Global ? 0.0f : drag.hmdYawBasis;
    const Quat yawBasis = FromYaw(yawBasisRadians);
    const Vec3 origin = frame.devices[DeviceSlot(DeviceIndex::Hmd)].position;
    Vec3 activeLocal = Rotate(Conjugate(yawBasis), Subtract(active.position, origin));
    activeLocal.x = -activeLocal.x;
    mirrored.position = Add(origin, Rotate(yawBasis, activeLocal));
    const float mirroredY = mirrored.position.y;
    mirrored.position.y = ClampDeviceY(mirroredDevice, mirroredY);
    mirrored.y_clamped = mirrored.position.y != mirroredY;
    mirrored.rotation = MirrorRotationInYawBasis(active.rotation, yawBasisRadians);

    frame.devices[mirroredSlot] = mirrored;
}

} // namespace anyadance
