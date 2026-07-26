#include "core/math3d.h"

#include <algorithm>
#include <cmath>

namespace anyadance {

float LengthSquared(Quat q) {
    return q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
}

bool NormalizeIfAcceptable(Quat& q) {
    const float lenSq = LengthSquared(q);
    if (!std::isfinite(lenSq) || lenSq < 0.5f || lenSq > 1.5f) {
        return false;
    }
    const float invLen = 1.0f / std::sqrt(lenSq);
    q.x *= invLen;
    q.y *= invLen;
    q.z *= invLen;
    q.w *= invLen;
    return true;
}

Quat Normalized(Quat q) {
    const float lenSq = LengthSquared(q);
    if (!std::isfinite(lenSq) || lenSq <= 0.0f) {
        return {};
    }
    const float invLen = 1.0f / std::sqrt(lenSq);
    q.x *= invLen;
    q.y *= invLen;
    q.z *= invLen;
    q.w *= invLen;
    return q;
}

Quat Conjugate(Quat q) {
    return {-q.x, -q.y, -q.z, q.w};
}

Quat Multiply(Quat lhs, Quat rhs) {
    return {
        lhs.w * rhs.x + lhs.x * rhs.w + lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.w * rhs.y - lhs.x * rhs.z + lhs.y * rhs.w + lhs.z * rhs.x,
        lhs.w * rhs.z + lhs.x * rhs.y - lhs.y * rhs.x + lhs.z * rhs.w,
        lhs.w * rhs.w - lhs.x * rhs.x - lhs.y * rhs.y - lhs.z * rhs.z,
    };
}

Quat FromAxisAngle(Vec3 axis, float radians) {
    const float len = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    if (len <= 0.0f || !std::isfinite(len)) {
        return {};
    }
    const float invLen = 1.0f / len;
    const float half = radians * 0.5f;
    const float s = std::sin(half);
    return {axis.x * invLen * s, axis.y * invLen * s, axis.z * invLen * s, std::cos(half)};
}

Quat FromYaw(float yawRadians) {
    return FromAxisAngle({0.0f, 1.0f, 0.0f}, yawRadians);
}

Vec3 Rotate(Quat rotation, Vec3 value) {
    const Quat qv{value.x, value.y, value.z, 0.0f};
    const Quat rotated = Multiply(Multiply(rotation, qv), Conjugate(rotation));
    return {rotated.x, rotated.y, rotated.z};
}

float YawFromQuaternion(Quat rotation) {
    rotation = Normalized(rotation);
    const Vec3 forward = Rotate(rotation, {0.0f, 0.0f, 1.0f});
    return std::atan2(forward.x, forward.z);
}

Vec3 Add(Vec3 lhs, Vec3 rhs) {
    return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

Vec3 Scale(Vec3 value, float scalar) {
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

float ClampFloat(float value, float low, float high) {
    return std::max(low, std::min(value, high));
}

float ClampDeviceY(float y) {
    return ClampFloat(y, kMinDeviceY, kMaxDeviceY);
}

} // namespace anyadance
