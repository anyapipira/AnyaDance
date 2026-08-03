#include "udp_pose_receiver.h"

#include "core/constants.h"
#include "core/driver_log_protocol.h"
#include "core/protocol.h"
#include "log.h"

#include <WinSock2.h>
#include <WS2tcpip.h>

#include <utility>

namespace {
constexpr int kSocketTimeoutMs = 100;

class CommandLogSender {
public:
    ~CommandLogSender() { Stop(); }

    void Stop() {
        if (m_socket != INVALID_SOCKET) {
            closesocket(m_socket);
            m_socket = INVALID_SOCKET;
        }
    }

    bool Start(const DriverCommandLogConfig& config) {
        if (!config.enabled) {
            return false;
        }
        m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (m_socket == INVALID_SOCKET) {
            DriverLog("[anyadance] Failed to create command-log UDP socket\n");
            return false;
        }

        u_long nonBlocking = 1;
        if (ioctlsocket(m_socket, FIONBIO, &nonBlocking) == SOCKET_ERROR) {
            DriverLog("[anyadance] Failed to make command-log socket non-blocking\n");
            closesocket(m_socket);
            m_socket = INVALID_SOCKET;
            return false;
        }

        m_destination.sin_family = AF_INET;
        m_destination.sin_port = htons(config.port);
        if (InetPtonA(AF_INET, config.host.c_str(), &m_destination.sin_addr) != 1) {
            DriverLog("[anyadance] Invalid command-log IPv4 address: %s\n", config.host.c_str());
            closesocket(m_socket);
            m_socket = INVALID_SOCKET;
            return false;
        }

        DriverLog(
            "[anyadance] Command logging sends to %s:%u (best-effort, non-blocking)\n",
            config.host.c_str(),
            config.port);
        return true;
    }

    void Report(
        const char* data,
        int size,
        const sockaddr_in& sender,
        bool accepted,
        const anyadance::ParsedFrame& parsed) {
        if (m_socket == INVALID_SOCKET || !data || size <= 0) {
            return;
        }

        anyadance::DriverCommandLogPacket packet;
        packet.sequence = ++m_sequence;
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
            packet.detail = "accepted " + std::to_string(acceptedCount) + " device entries";
            if (clampedCount > 0) {
                packet.detail += "; clamped Y for " + std::to_string(clampedCount);
            }
        } else {
            packet.detail = "invalid pose frame";
        }

        const std::string encoded = anyadance::SerializeDriverCommandLog(packet);
        if (encoded.size() > static_cast<std::size_t>(anyadance::kMaxDriverLogPacketBytes)) {
            return;
        }
        // Telemetry is deliberately lossy. A full socket buffer or unreachable
        // listener drops this report without slowing command processing.
        sendto(
            m_socket,
            encoded.data(),
            static_cast<int>(encoded.size()),
            0,
            reinterpret_cast<const sockaddr*>(&m_destination),
            sizeof(m_destination));
    }

private:
    SOCKET m_socket = INVALID_SOCKET;
    sockaddr_in m_destination{};
    std::uint64_t m_sequence = 0;
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

bool UdpPoseReceiver::Start(unsigned short port, DriverCommandLogConfig logConfig) {
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

void UdpPoseReceiver::Run(unsigned short port, DriverCommandLogConfig logConfig) {
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
