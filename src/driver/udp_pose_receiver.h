#pragma once

#include "core/constants.h"
#include "core/pose_sample.h"
#include "core/protocol.h"

#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

struct DriverCommandLogConfig {
    bool enabled = true;
    std::string host = anyadance::kDriverLogHost;
    unsigned short port = anyadance::kDriverLogPort;
};

class UdpPoseReceiver {
public:
    UdpPoseReceiver() = default;
    ~UdpPoseReceiver();

    UdpPoseReceiver(const UdpPoseReceiver&) = delete;
    UdpPoseReceiver& operator=(const UdpPoseReceiver&) = delete;

    bool Start(unsigned short port, DriverCommandLogConfig logConfig);
    void Stop();
    bool TryGetLatest(const std::string& deviceId, anyadance::PoseSample& sample) const;

private:
    void Run(unsigned short port, DriverCommandLogConfig logConfig);
    bool StoreIfValid(const char* data, int size, anyadance::ParsedFrame& parsed);

    mutable std::mutex m_mutex;
    std::array<anyadance::PoseSample, anyadance::kDevices.size()> m_latest{};
    std::array<bool, anyadance::kDevices.size()> m_hasLatest{};
    std::atomic<bool> m_running{false};
    std::thread m_thread;
};
