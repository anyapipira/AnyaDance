#pragma once

#include "core/constants.h"
#include "core/pose_sample.h"

#include <array>
#include <memory>
#include <openvr_driver.h>
#include <string>

enum class VirtualDeviceKind {
    Hmd,
    Controller,
    Tracker,
};

struct VirtualDeviceDefinition {
    // Identifies the device to shared core rules such as its Y floor.
    anyadance::DeviceIndex index = anyadance::DeviceIndex::Hmd;
    std::string deviceId;
    std::string serial;
    VirtualDeviceKind kind = VirtualDeviceKind::Tracker;
    std::array<float, 3> neutralPosition = {0.0f, 1.0f, 0.0f};
    vr::ETrackedControllerRole controllerRole = vr::TrackedControllerRole_Invalid;
};

class VirtualDevice : public vr::ITrackedDeviceServerDriver {
public:
    explicit VirtualDevice(VirtualDeviceDefinition definition);
    ~VirtualDevice();

    vr::EVRInitError Activate(uint32_t unObjectId) override;
    void Deactivate() override;
    void EnterStandby() override;
    void* GetComponent(const char* pchComponentNameAndVersion) override;
    void DebugRequest(const char* pchRequest, char* pchResponseBuffer, uint32_t unResponseBufferSize) override;
    vr::DriverPose_t GetPose() override;

    void ApplyPoseSample(const anyadance::PoseSample& sample);
    void ApplyNeutralPose();
    void ApplyInvalidPose();
    void UpdateInputs();
    void UpdatePose();

    const VirtualDeviceDefinition& GetDefinition() const { return m_definition; }
    const std::string& GetSerialNumber() const { return m_definition.serial; }

private:
    struct DisplayComponent;

    void ActivateCommon(vr::PropertyContainerHandle_t container);
    void ActivateHmd(vr::PropertyContainerHandle_t container);
    void ActivateController(vr::PropertyContainerHandle_t container);
    void ActivateTracker(vr::PropertyContainerHandle_t container);

    VirtualDeviceDefinition m_definition;
    uint32_t m_objectId;
    vr::DriverPose_t m_pose;
    std::unique_ptr<DisplayComponent> m_display;
    vr::VRInputComponentHandle_t m_triggerClick;
    vr::VRInputComponentHandle_t m_triggerValue;
    vr::VRInputComponentHandle_t m_menuClick;
    vr::VRInputComponentHandle_t m_aClick;
    vr::VRInputComponentHandle_t m_bClick;
    vr::VRInputComponentHandle_t m_gripClick;
    vr::VRInputComponentHandle_t m_gripValue;
    vr::VRInputComponentHandle_t m_gripForce;
    vr::VRInputComponentHandle_t m_gripTouch;
    vr::VRInputComponentHandle_t m_joystickX;
    vr::VRInputComponentHandle_t m_joystickY;
    vr::VRInputComponentHandle_t m_trackpadX;
    vr::VRInputComponentHandle_t m_trackpadY;
    vr::VRInputComponentHandle_t m_skeletonHandle;
    bool m_currentTriggerClick = false;
    float m_currentTriggerValue = 0.0f;
    bool m_currentMenuClick = false;
    bool m_currentAClick = false;
    bool m_currentBClick = false;
    bool m_currentGripClick = false;
    float m_currentGripValue = 0.0f;
    float m_currentJoystickX = 0.0f;
    float m_currentJoystickY = 0.0f;
    float m_currentTrackpadX = 0.0f;
    float m_currentTrackpadY = 0.0f;
    anyadance::FingerBends m_currentFingerBends{};
    bool m_hasFingerBends = false;
};
