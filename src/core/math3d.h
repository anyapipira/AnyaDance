#pragma once

#include "core/constants.h"

namespace anyadance {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Quat {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

float LengthSquared(Quat q);
bool NormalizeIfAcceptable(Quat& q);
Quat Normalized(Quat q);
Quat Conjugate(Quat q);
Quat Multiply(Quat lhs, Quat rhs);
Quat FromAxisAngle(Vec3 axis, float radians);
Quat FromYaw(float yawRadians);
Vec3 Rotate(Quat rotation, Vec3 value);
float YawFromQuaternion(Quat rotation);
Vec3 Add(Vec3 lhs, Vec3 rhs);
Vec3 Scale(Vec3 value, float scalar);
float ClampFloat(float value, float low, float high);
// Clamps to the Y range of the given device. The floor is device-dependent, so
// the caller must say which device the value belongs to.
float ClampDeviceY(DeviceIndex index, float y);
float ClampDeviceY(std::size_t slot, float y);

} // namespace anyadance
