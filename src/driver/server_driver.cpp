#include "server_driver.h"

#include "core/constants.h"
#include "core/driver_log_protocol.h"
#include "log.h"

#include <cstdio>
#include <utility>

using namespace vr;

namespace {

const VirtualDeviceDefinition kDeviceDefinitions[] = {
    {anyadance::DeviceIndex::Hmd, "hmd", "anyadance_hmd_001", VirtualDeviceKind::Hmd, {0.0f, 1.50f, 0.0f}, TrackedControllerRole_Invalid},
    {anyadance::DeviceIndex::LeftController, "left_controller", "anyadance_left_controller_001", VirtualDeviceKind::Controller, {-0.45f, 1.15f, 0.0f}, TrackedControllerRole_LeftHand},
    {anyadance::DeviceIndex::RightController, "right_controller", "anyadance_right_controller_001", VirtualDeviceKind::Controller, {0.45f, 1.15f, 0.0f}, TrackedControllerRole_RightHand},
    {anyadance::DeviceIndex::Hip, "hip", "anyadance_hip_001", VirtualDeviceKind::Tracker, {0.0f, 0.85f, 0.0f}, TrackedControllerRole_Invalid},
    {anyadance::DeviceIndex::LeftFoot, "left_foot", "anyadance_left_foot_001", VirtualDeviceKind::Tracker, {-0.12f, 0.0f, 0.0f}, TrackedControllerRole_Invalid},
    {anyadance::DeviceIndex::RightFoot, "right_foot", "anyadance_right_foot_001", VirtualDeviceKind::Tracker, {0.12f, 0.0f, 0.0f}, TrackedControllerRole_Invalid},
};

ETrackedDeviceClass DeviceClassFor(VirtualDeviceKind kind) {
    switch (kind) {
    case VirtualDeviceKind::Hmd:
        return TrackedDeviceClass_HMD;
    case VirtualDeviceKind::Controller:
        return TrackedDeviceClass_Controller;
    case VirtualDeviceKind::Tracker:
        return TrackedDeviceClass_GenericTracker;
    }
    return TrackedDeviceClass_Invalid;
}

bool GetBoolSetting(const char* key, bool defaultValue) {
    EVRSettingsError error = VRSettingsError_None;
    const bool enabled = VRSettings()->GetBool(anyadance::kDriverSettingsSection, key, &error);
    if (error != VRSettingsError_None) {
        DriverLog(
            "[anyadance] Setting %s.%s not found or invalid; using %s\n",
            anyadance::kDriverSettingsSection,
            key,
            defaultValue ? "true" : "false");
        return defaultValue;
    }
    return enabled;
}

int GetIntSetting(const char* key, int defaultValue) {
    EVRSettingsError error = VRSettingsError_None;
    const int value = VRSettings()->GetInt32(anyadance::kDriverSettingsSection, key, &error);
    if (error != VRSettingsError_None) {
        DriverLog(
            "[anyadance] Setting %s.%s not found or invalid; using %d\n",
            anyadance::kDriverSettingsSection,
            key,
            defaultValue);
        return defaultValue;
    }
    return value;
}

std::string GetStringSetting(const char* key, const char* defaultValue) {
    char value[256]{};
    EVRSettingsError error = VRSettingsError_None;
    VRSettings()->GetString(
        anyadance::kDriverSettingsSection,
        key,
        value,
        sizeof(value),
        &error);
    if (error != VRSettingsError_None || value[0] == '\0') {
        DriverLog(
            "[anyadance] Setting %s.%s not found or invalid; using %s\n",
            anyadance::kDriverSettingsSection,
            key,
            defaultValue);
        return defaultValue;
    }
    return value;
}

bool ShouldRegisterDevice(
    const VirtualDeviceDefinition& definition,
    bool enableHmd,
    bool enableControllers,
    bool enableTrackers) {
    switch (definition.kind) {
    case VirtualDeviceKind::Hmd:
        return enableHmd;
    case VirtualDeviceKind::Controller:
        return enableControllers;
    case VirtualDeviceKind::Tracker:
        return enableTrackers;
    }
    return false;
}

} // namespace

ServerDriver::ServerDriver() = default;

EVRInitError ServerDriver::Init(IVRDriverContext* pDriverContext) {
    VR_INIT_SERVER_DRIVER_CONTEXT(pDriverContext);
    DriverLog_InitDriverLog();
    DriverLog("[anyadance] ServerDriver initialized\n");

    const bool enableHmd = GetBoolSetting("enable_hmd", true);
    const bool enableControllers = GetBoolSetting("enable_controllers", true);
    const bool enableTrackers = GetBoolSetting("enable_trackers", true);
    DriverLog(
        "[anyadance] Device groups: hmd=%s controllers=%s trackers=%s\n",
        enableHmd ? "enabled" : "disabled",
        enableControllers ? "enabled" : "disabled",
        enableTrackers ? "enabled" : "disabled");

    for (const VirtualDeviceDefinition& definition : kDeviceDefinitions) {
        if (!ShouldRegisterDevice(definition, enableHmd, enableControllers, enableTrackers)) {
            DriverLog("[anyadance] Skipping virtual device %s by settings\n", definition.serial.c_str());
            continue;
        }

        DeviceSlot slot;
        slot.deviceId = definition.deviceId;
        slot.device = std::make_unique<VirtualDevice>(definition);

        const bool added = VRServerDriverHost()->TrackedDeviceAdded(
            slot.device->GetSerialNumber().c_str(),
            DeviceClassFor(definition.kind),
            slot.device.get());
        if (!added) {
            DriverLog("[anyadance] Failed to add virtual device %s\n", definition.serial.c_str());
            continue;
        }

        if (definition.kind == VirtualDeviceKind::Hmd) {
            m_hasVirtualHmd = true;
        }
        m_devices.push_back(std::move(slot));
    }

    if (m_devices.empty()) {
        DriverLog("[anyadance] No virtual devices registered; driver initialized without virtual outputs\n");
    }

    m_poseReceiver = std::make_unique<UdpPoseReceiver>();
    // Both reporting paths share one counter so `sequence` numbers every event
    // on the group, whatever its type and whichever thread produced it.
    m_logSequence = MakeDriverLogSequence();
    DriverLogSenderConfig commandLog;
    commandLog.sequence = m_logSequence;
    commandLog.enabled = GetBoolSetting("command_log_enabled", true);
    commandLog.multicastGroup = GetStringSetting(
        "command_log_multicast_group",
        anyadance::kDriverLogMulticastGroup);
    const int configuredLogPort = GetIntSetting("command_log_port", anyadance::kDriverLogPort);
    if (configuredLogPort > 0 && configuredLogPort <= 65535) {
        commandLog.port = static_cast<unsigned short>(configuredLogPort);
    } else {
        DriverLog(
            "[anyadance] Invalid command-log port %d; using %u\n",
            configuredLogPort,
            anyadance::kDriverLogPort);
    }
    // Haptic reports ride the same multicast group and port as command reports,
    // so a listener joins one group to see both. This sender is separate only so
    // the RunFrame thread never shares a socket with the UDP receive thread.
    DriverLogSenderConfig hapticLog;
    hapticLog.sequence = m_logSequence;
    hapticLog.enabled = GetBoolSetting("haptic_log_enabled", true);
    hapticLog.multicastGroup = commandLog.multicastGroup;
    hapticLog.port = commandLog.port;
    m_hapticLog.Start(hapticLog, "Haptic logging");

    m_poseReceiver->Start(anyadance::kUdpPort, std::move(commandLog));

    return VRInitError_None;
}

void ServerDriver::Cleanup() {
    DriverLog("[anyadance] ServerDriver cleanup\n");
    if (m_poseReceiver) {
        m_poseReceiver->Stop();
        m_poseReceiver.reset();
    }
    m_hapticLog.Stop();
    m_devices.clear();
    DriverLog_CleanupDriverLog();
    VR_CLEANUP_SERVER_DRIVER_CONTEXT();
}

const char* const* ServerDriver::GetInterfaceVersions() {
    return k_InterfaceVersions;
}

void ServerDriver::PollDriverEvents() {
    // SteamVR queues events for this driver's devices. Drain the queue every
    // frame: leaving it unread would let it grow, and haptic requests are the
    // only entries this driver acts on today.
    VREvent_t event{};
    while (VRServerDriverHost()->PollNextEvent(&event, sizeof(event))) {
        if (event.eventType == VREvent_Input_HapticVibration) {
            ReportHaptic(event.data.hapticVibration);
        }
    }
}

void ServerDriver::ReportHaptic(const VREvent_HapticVibration_t& haptic) {
    if (!m_hapticLog.IsOpen()) {
        return;
    }
    // The event names a component handle, not a device, so map it back to the
    // controller that owns it. An unknown handle is not ours to report.
    for (const DeviceSlot& slot : m_devices) {
        if (!slot.device ||
            slot.device->GetHapticComponentHandle() != haptic.componentHandle ||
            slot.device->GetHapticComponentHandle() == k_ulInvalidInputComponentHandle) {
            continue;
        }
        anyadance::DriverHapticLogPacket packet;
        packet.device = slot.device->GetDefinition().index;
        packet.durationSeconds = haptic.fDurationSeconds;
        packet.frequencyHz = haptic.fFrequency;
        packet.amplitude = haptic.fAmplitude;
        // Every event carries a ready-made summary so a reader can render it
        // without knowing this event's shape.
        char detail[96];
        std::snprintf(
            detail,
            sizeof(detail),
            "%.3f s at %.1f Hz, amplitude %.2f",
            static_cast<double>(packet.durationSeconds),
            static_cast<double>(packet.frequencyHz),
            static_cast<double>(packet.amplitude));
        packet.envelope.detail = detail;
        // Numbered last, for the reason given in the command reporter: the two
        // sender threads share this counter, so the gap between numbering and
        // sending is the window in which they can leave out of order.
        packet.envelope.sequence = m_hapticLog.NextSequence();
        m_hapticLog.Send(anyadance::SerializeDriverHapticLog(packet));
        return;
    }
}

void ServerDriver::RunFrame() {
    PollDriverEvents();
    for (DeviceSlot& slot : m_devices) {
        anyadance::PoseSample sample;
        const bool hasSample = m_poseReceiver && m_poseReceiver->TryGetLatest(slot.deviceId, sample);
        if (hasSample) {
            slot.seenUdpPose = true;
            slot.device->ApplyPoseSample(sample);
        } else if (!slot.seenUdpPose) {
            slot.device->ApplyNeutralPose();
        }
        slot.device->UpdateInputs();
        slot.device->UpdatePose();
    }
}

bool ServerDriver::ShouldBlockStandbyMode() {
    const bool block = m_hasVirtualHmd;
    DriverLog("[anyadance] ShouldBlockStandbyMode -> %s\n", block ? "true" : "false");
    return block;
}

void ServerDriver::EnterStandby() {}

void ServerDriver::LeaveStandby() {}
