#include "udp_pose_receiver.h"

#include "core/constants.h"
#include "core/driver_log_protocol.h"
#include "core/protocol.h"
#include "driver_log_sender.h"
#include "log.h"

#include <WinSock2.h>
#include <WS2tcpip.h>

#include <cstring>
#include <string>
#include <utility>

namespace {
constexpr int kSocketTimeoutMs = 100;

class CommandLogSender {
public:
    void Stop() { m_sender.Stop(); }

    bool Start(const DriverLogSenderConfig& config) {
        return m_sender.Start(config, "Command logging");
    }

    void Report(
        const char* data,
        int size,
        const sockaddr_in& sender,
        bool accepted,
        const anyadance::ParsedFrame& parsed) {
        if (!m_sender.IsOpen() || !data || size <= 0) {
            return;
        }

        // A sender holding a pose repeats the identical command at its stream
        // rate. Only a change in what the command asks for is worth reporting;
        // the number of absorbed repeats rides along on the next report so a
        // held pose stays distinguishable from a stalled sender.
        if (IsRepeatOfLast(data, size, accepted, parsed)) {
            ++m_suppressed;
            return;
        }

        anyadance::DriverCommandLogPacket packet;
        char senderHost[INET_ADDRSTRLEN]{};
        if (!InetNtopA(AF_INET, &sender.sin_addr, senderHost, sizeof(senderHost))) {
            return;
        }
        packet.senderHost = senderHost;
        packet.senderPort = ntohs(sender.sin_port);
        packet.receivedBytes = size;
        packet.accepted = accepted;
        packet.devices = parsed.present;
        packet.yClamped = parsed.y_clamped;
        packet.payload.assign(data, static_cast<std::size_t>(size));

        int acceptedCount = 0;
        int clampedCount = 0;
        for (std::size_t slot = 0; slot < parsed.present.size(); ++slot) {
            acceptedCount += parsed.present[slot] ? 1 : 0;
            clampedCount += parsed.y_clamped[slot] ? 1 : 0;
        }
        if (accepted) {
            packet.envelope.detail = "accepted " + std::to_string(acceptedCount) + " device entries";
            if (clampedCount > 0) {
                packet.envelope.detail += "; clamped Y for " + std::to_string(clampedCount);
            }
        } else {
            packet.envelope.detail = "invalid pose frame";
        }

        // Remember the reported state before serializing. An oversized report
        // is dropped below, and re-deriving it for every repeat of the same
        // command would burn the receive thread to no effect.
        RememberLast(data, size, accepted, parsed);
        packet.envelope.suppressed = m_suppressed;
        m_suppressed = 0;

        // Number the event as late as possible. Two threads send to this group,
        // so every instruction between taking a number and handing the datagram
        // to the socket is a window in which the two can leave in the wrong
        // order. This narrows that window to the serialization call; it cannot
        // close it, which is why receivers reorder by sequence.
        packet.envelope.sequence = m_sender.NextSequence();
        m_sender.Send(anyadance::SerializeDriverCommandLog(packet));
    }

private:
    bool IsRepeatOfLast(
        const char* data,
        int size,
        bool accepted,
        const anyadance::ParsedFrame& parsed) const {
        if (!m_hasLast || accepted != m_lastAccepted) {
            return false;
        }
        // A rejected datagram leaves no trustworthy parsed state, so repeated
        // rejections are recognized by their bytes instead.
        if (!accepted) {
            return m_lastPayload.size() == static_cast<std::size_t>(size) &&
                std::memcmp(m_lastPayload.data(), data, m_lastPayload.size()) == 0;
        }
        return anyadance::SamePoseCommand(parsed, m_lastFrame);
    }

    void RememberLast(
        const char* data,
        int size,
        bool accepted,
        const anyadance::ParsedFrame& parsed) {
        m_hasLast = true;
        m_lastAccepted = accepted;
        m_lastFrame = parsed;
        if (accepted) {
            m_lastPayload.clear();
        } else {
            m_lastPayload.assign(data, static_cast<std::size_t>(size));
        }
    }

    DriverLogSender m_sender;
    std::uint64_t m_suppressed = 0;
    bool m_hasLast = false;
    bool m_lastAccepted = false;
    anyadance::ParsedFrame m_lastFrame;
    std::string m_lastPayload;
};

bool SlotForDeviceId(const std::string& deviceId, std::size_t& slot) {
    for (const anyadance::DeviceInfo& device : anyadance::kDevices) {
        if (deviceId == device.id) {
            slot = anyadance::DeviceSlot(device.index);
            return true;
        }
    }
    return false;
}
}

UdpPoseReceiver::~UdpPoseReceiver() {
    Stop();
}

bool UdpPoseReceiver::Start(unsigned short port, DriverLogSenderConfig logConfig) {
    if (m_running.exchange(true)) {
        return true;
    }

    m_thread = std::thread(&UdpPoseReceiver::Run, this, port, std::move(logConfig));
    return true;
}

void UdpPoseReceiver::Stop() {
    if (!m_running.exchange(false)) {
        return;
    }

    if (m_thread.joinable()) {
        m_thread.join();
    }
}

bool UdpPoseReceiver::TryGetLatest(const std::string& deviceId, anyadance::PoseSample& sample) const {
    std::size_t slot = 0;
    if (!SlotForDeviceId(deviceId, slot)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_hasLatest[slot]) {
        return false;
    }
    sample = m_latest[slot];
    return true;
}

void UdpPoseReceiver::Run(unsigned short port, DriverLogSenderConfig logConfig) {
    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        DriverLog("[anyadance] WSAStartup failed\n");
        return;
    }

    SOCKET socketHandle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socketHandle == INVALID_SOCKET) {
        DriverLog("[anyadance] Failed to create UDP socket\n");
        WSACleanup();
        return;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);

    if (bind(socketHandle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
        DriverLog("[anyadance] Failed to bind UDP port %u\n", port);
        closesocket(socketHandle);
        WSACleanup();
        return;
    }

    setsockopt(
        socketHandle,
        SOL_SOCKET,
        SO_RCVTIMEO,
        reinterpret_cast<const char*>(&kSocketTimeoutMs),
        sizeof(kSocketTimeoutMs));

    DriverLog("[anyadance] Listening for UDP pose frames on 127.0.0.1:%u\n", port);
    CommandLogSender commandLog;
    commandLog.Start(logConfig);

    char buffer[anyadance::kMaxPacketBytes + 1]{};
    while (m_running.load()) {
        sockaddr_in sender{};
        int senderLen = sizeof(sender);
        const int size = recvfrom(
            socketHandle,
            buffer,
            anyadance::kMaxPacketBytes,
            0,
            reinterpret_cast<sockaddr*>(&sender),
            &senderLen);

        if (size == SOCKET_ERROR) {
            const int error = WSAGetLastError();
            if (error == WSAETIMEDOUT || error == WSAEWOULDBLOCK) {
                continue;
            }
            DriverLog("[anyadance] UDP receive error %d\n", error);
            continue;
        }

        if (size <= 0) {
            continue;
        }
        anyadance::ParsedFrame parsed;
        const bool accepted = StoreIfValid(buffer, size, parsed);
        commandLog.Report(buffer, size, sender, accepted, parsed);
    }

    commandLog.Stop();
    closesocket(socketHandle);
    WSACleanup();
}

bool UdpPoseReceiver::StoreIfValid(
    const char* data,
    int size,
    anyadance::ParsedFrame& parsed) {
    if (!anyadance::ParsePoseFrameBytes(data, size, parsed)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    for (std::size_t slot = 0; slot < parsed.samples.size(); ++slot) {
        if (parsed.present[slot]) {
            m_latest[slot] = parsed.samples[slot];
            m_hasLatest[slot] = true;
        }
    }
    return true;
}
