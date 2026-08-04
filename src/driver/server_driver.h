#pragma once

#include <openvr_driver.h>

#include <memory>
#include <string>
#include <vector>

#include "driver_log_sender.h"
#include "udp_pose_receiver.h"
#include "virtual_device.h"

class ServerDriver : public vr::IServerTrackedDeviceProvider {
public:
    ServerDriver();
    ~ServerDriver() = default;

    vr::EVRInitError Init(vr::IVRDriverContext* pDriverContext) override;
    void Cleanup() override;
    const char* const* GetInterfaceVersions() override;
    void RunFrame() override;
    bool ShouldBlockStandbyMode() override;
    void EnterStandby() override;
    void LeaveStandby() override;

private:
    struct DeviceSlot {
        std::string deviceId;
        std::unique_ptr<VirtualDevice> device;
        bool seenUdpPose = false;
    };

    // Drains SteamVR's event queue and reports haptic requests. Runs on the
    // RunFrame thread, so it must not block.
    void PollDriverEvents();
    void ReportHaptic(const vr::VREvent_HapticVibration_t& haptic);

    std::vector<DeviceSlot> m_devices;
    std::unique_ptr<UdpPoseReceiver> m_poseReceiver;
    DriverLogSender m_hapticLog;
    bool m_hasVirtualHmd = false;
};