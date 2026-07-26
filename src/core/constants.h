#pragma once

#include <array>
#include <cstddef>

namespace anyadance {

inline constexpr const char* kProjectName = "AnyaDance";
inline constexpr const char* kDriverSlug = "anyadance";
inline constexpr const char* kDriverTarget = "driver_anyadance";
inline constexpr const char* kDriverDllName = "driver_anyadance.dll";
inline constexpr const char* kDriverFolderName = "anyadance";
inline constexpr const char* kDriverSettingsSection = "driver_anyadance";
inline constexpr const char* kManifestName = "anyadance";
inline constexpr const char* kResourceNamespace = "{anyadance}";
inline constexpr const char* kLogPrefix = "[anyadance]";
inline constexpr const char* kToolExecutableName = "AnyaDance.exe";

inline constexpr const char* kUdpHost = "127.0.0.1";
inline constexpr unsigned short kUdpPort = 39570;
inline constexpr int kProtocolVersion = 1;
inline constexpr int kMaxPacketBytes = 8192;
inline constexpr float kMaxAbsPositionMeters = 30.0f;
inline constexpr float kMinDeviceY = 0.0f;
inline constexpr float kMaxDeviceY = 25.0f;
// The HMD uses the shared floor while dragging.
inline constexpr float kMinHmdY = 0.0f;
inline constexpr int kStreamRateHz = 60;

inline constexpr float kResetHmdY = 1.50f;
inline constexpr float kTranslationMetersPerCount = 0.002f;
inline constexpr float kRotationDegreesPerCount = 0.08f;
inline constexpr float kPitchLimitDegrees = 80.0f;

inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float DegToRad(float degrees) { return degrees * kPi / 180.0f; }
inline constexpr float RadToDeg(float radians) { return radians * 180.0f / kPi; }

enum class DeviceIndex : std::size_t {
    Hmd = 0,
    LeftController = 1,
    RightController = 2,
    Hip = 3,
    LeftFoot = 4,
    RightFoot = 5,
};

struct DeviceInfo {
    DeviceIndex index;
    const char* id;
    const char* serial;
};

inline constexpr std::array<DeviceInfo, 6> kDevices = {{
    {DeviceIndex::Hmd, "hmd", "anyadance_hmd_001"},
    {DeviceIndex::LeftController, "left_controller", "anyadance_left_controller_001"},
    {DeviceIndex::RightController, "right_controller", "anyadance_right_controller_001"},
    {DeviceIndex::Hip, "hip", "anyadance_hip_001"},
    {DeviceIndex::LeftFoot, "left_foot", "anyadance_left_foot_001"},
    {DeviceIndex::RightFoot, "right_foot", "anyadance_right_foot_001"},
}};

inline constexpr std::size_t DeviceSlot(DeviceIndex index) {
    return static_cast<std::size_t>(index);
}

} // namespace anyadance
