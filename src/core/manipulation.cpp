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

// Translate Y by the accumulated vertical drag, clamped to the shared device
// range and an optional per-caller floor. When the target overshoots a bound,
// re-anchor the accumulator so a reversed drag moves away immediately.
float ClampedDragY(float startY, float& accumulatedDy,
                   float floorY = std::numeric_limits<float>::lowest()) {
    float targetY = startY - accumulatedDy * kTranslationMetersPerCount;
    if (targetY > kMaxDeviceY) {
        accumulatedDy = (startY - kMaxDeviceY) / kTranslationMetersPerCount;
        targetY = kMaxDeviceY;
    } else if (targetY < std::max(floorY, kMinDeviceY)) {
        const float minimumY = std::max(floorY, kMinDeviceY);
        accumulatedDy = (startY - minimumY) / kTranslationMetersPerCount;
        targetY = minimumY;
    }
    return targetY;
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
            device.position.y = ClampedDragY(drag.startFrame.devices[slot].position.y, drag.accumulatedDy, kMinHmdY);
            device.y_clamped = device.position.y != drag.startFrame.devices[slot].position.y &&
                               (device.position.y == kMinDeviceY || device.position.y == kMaxDeviceY);
        } else {
            ApplyRotation(device, drag.startFrame.devices[slot].rotation, basis, drag.accumulatedDx, drag.accumulatedDy, 0.0f);
        }
        frame.devices[slot] = device;
        return;
    }

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
        device.position.y = ClampedDragY(drag.startFrame.devices[slot].position.y, drag.accumulatedDy);
    }

    device.position.y = ClampDeviceY(device.position.y);
    device.y_clamped = device.position.y != drag.startFrame.devices[slot].position.y;
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
            device.position.y = ClampDeviceY(device.position.y);
            device.y_clamped = device.position.y != start.position.y;
            frame.devices[slot] = device;
        }
        return;
    }

    // Vertical move: shift the whole rig on world Y. Express the bounds in the
    // highest device's terms so the rig remains rigid within the shared Y range.
    float highestStartY = drag.startFrame.devices[0].position.y;
    for (const DeviceState& start : drag.startFrame.devices) {
        highestStartY = std::max(highestStartY, start.position.y);
    }
    float lowestStartY = drag.startFrame.devices[0].position.y;
    for (const DeviceState& start : drag.startFrame.devices) {
        lowestStartY = std::min(lowestStartY, start.position.y);
    }
    const float rigFloorY = kMinDeviceY + (highestStartY - lowestStartY);
    const float deltaY = ClampedDragY(highestStartY, drag.accumulatedDy, rigFloorY) - highestStartY;
    for (std::size_t slot = 0; slot < frame.devices.size(); ++slot) {
        const DeviceState& start = drag.startFrame.devices[slot];
        DeviceState device = start;
        device.position.y = ClampDeviceY(start.position.y + deltaY);
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
    mirrored.position.y = ClampDeviceY(mirrored.position.y);
    mirrored.y_clamped = mirrored.position.y != active.position.y;
    mirrored.rotation = MirrorRotationInYawBasis(active.rotation, yawBasisRadians);

    frame.devices[mirroredSlot] = mirrored;
}

} // namespace anyadance
