#include "driver_log_sender.h"

#include "log.h"

#include <WS2tcpip.h>

bool DriverLogSender::Start(const DriverLogSenderConfig& config, const char* purpose) {
    if (!config.enabled) {
        return false;
    }
    m_sequence = config.sequence;
    m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_socket == INVALID_SOCKET) {
        DriverLog("[anyadance] Failed to create %s UDP socket\n", purpose);
        return false;
    }

    u_long nonBlocking = 1;
    if (ioctlsocket(m_socket, FIONBIO, &nonBlocking) == SOCKET_ERROR) {
        DriverLog("[anyadance] Failed to make %s socket non-blocking\n", purpose);
        Stop();
        return false;
    }

    m_destination.sin_family = AF_INET;
    m_destination.sin_port = htons(config.port);
    if (InetPtonA(AF_INET, config.multicastGroup.c_str(), &m_destination.sin_addr) != 1 ||
        (ntohl(m_destination.sin_addr.s_addr) & 0xf0000000u) != 0xe0000000u) {
        DriverLog(
            "[anyadance] Invalid %s IPv4 multicast group: %s\n",
            purpose,
            config.multicastGroup.c_str());
        Stop();
        return false;
    }

    in_addr loopbackInterface{};
    if (InetPtonA(AF_INET, anyadance::kDriverLogMulticastInterface, &loopbackInterface) != 1) {
        DriverLog("[anyadance] Invalid %s multicast interface\n", purpose);
        Stop();
        return false;
    }
    if (setsockopt(
            m_socket,
            IPPROTO_IP,
            IP_MULTICAST_IF,
            reinterpret_cast<const char*>(&loopbackInterface),
            sizeof(loopbackInterface)) == SOCKET_ERROR) {
        DriverLog("[anyadance] Failed to select loopback for %s multicast\n", purpose);
        Stop();
        return false;
    }

    // Scope zero and an explicit loopback interface keep telemetry on this
    // machine while allowing every joined process to receive it.
    const DWORD multicastTtl = 0;
    if (setsockopt(
            m_socket,
            IPPROTO_IP,
            IP_MULTICAST_TTL,
            reinterpret_cast<const char*>(&multicastTtl),
            sizeof(multicastTtl)) == SOCKET_ERROR) {
        DriverLog("[anyadance] Failed to scope %s multicast to this host\n", purpose);
        Stop();
        return false;
    }

    DriverLog(
        "[anyadance] %s multicasts on loopback to %s:%u (best-effort, non-blocking)\n",
        purpose,
        config.multicastGroup.c_str(),
        config.port);
    return true;
}

void DriverLogSender::Stop() {
    if (m_socket != INVALID_SOCKET) {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }
}

void DriverLogSender::Send(const std::string& payload) {
    if (m_socket == INVALID_SOCKET || payload.empty() ||
        payload.size() > static_cast<std::size_t>(anyadance::kMaxDriverLogPacketBytes)) {
        return;
    }
    // Telemetry is deliberately lossy. A full socket buffer or an unreachable
    // listener drops this report without slowing the caller.
    sendto(
        m_socket,
        payload.data(),
        static_cast<int>(payload.size()),
        0,
        reinterpret_cast<const sockaddr*>(&m_destination),
        sizeof(m_destination));
}
