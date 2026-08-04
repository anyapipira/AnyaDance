#include "ui/driver_log_listener.h"

#include "core/constants.h"

#include <WS2tcpip.h>

#include <vector>
#include <utility>

namespace anyadance::ui {
namespace {
constexpr int kReceiveTimeoutMs = 50;
}

bool DriverLogListener::Start(
    const char* multicastGroup,
    unsigned short port,
    Callback callback,
    std::string& error) {
    error.clear();
    if (m_running.load()) {
        return true;
    }
    if (m_thread.joinable()) {
        Stop();
    }

    WSADATA wsa{};
    const int startupError = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (startupError != 0) {
        error = "WSAStartup failed: " + std::to_string(startupError);
        return false;
    }

    const SOCKET socketHandle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socketHandle == INVALID_SOCKET) {
        error = "socket() failed: " + std::to_string(WSAGetLastError());
        WSACleanup();
        return false;
    }

    in_addr groupAddress{};
    if (InetPtonA(AF_INET, multicastGroup, &groupAddress) != 1 ||
        (ntohl(groupAddress.s_addr) & 0xf0000000u) != 0xe0000000u) {
        error = "invalid IPv4 multicast group";
        closesocket(socketHandle);
        WSACleanup();
        return false;
    }

    const BOOL reuseAddress = TRUE;
    if (setsockopt(
            socketHandle,
            SOL_SOCKET,
            SO_REUSEADDR,
            reinterpret_cast<const char*>(&reuseAddress),
            sizeof(reuseAddress)) == SOCKET_ERROR) {
        error = "SO_REUSEADDR failed: WSA error " + std::to_string(WSAGetLastError());
        closesocket(socketHandle);
        WSACleanup();
        return false;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(
            socketHandle,
            reinterpret_cast<const sockaddr*>(&address),
            sizeof(address)) == SOCKET_ERROR) {
        error = "bind() failed: WSA error " + std::to_string(WSAGetLastError());
        closesocket(socketHandle);
        WSACleanup();
        return false;
    }

    ip_mreq membership{};
    membership.imr_multiaddr = groupAddress;
    if (InetPtonA(
            AF_INET,
            kDriverLogMulticastInterface,
            &membership.imr_interface) != 1) {
        error = "invalid multicast interface";
        closesocket(socketHandle);
        WSACleanup();
        return false;
    }
    if (setsockopt(
            socketHandle,
            IPPROTO_IP,
            IP_ADD_MEMBERSHIP,
            reinterpret_cast<const char*>(&membership),
            sizeof(membership)) == SOCKET_ERROR) {
        error = "IP_ADD_MEMBERSHIP failed: WSA error " +
            std::to_string(WSAGetLastError());
        closesocket(socketHandle);
        WSACleanup();
        return false;
    }

    // Winsock applies IP_MULTICAST_LOOP on the receive path. Keep it explicit
    // so this listener receives datagrams emitted by the local driver.
    const DWORD receiveLocalMulticast = 1;
    if (setsockopt(
            socketHandle,
            IPPROTO_IP,
            IP_MULTICAST_LOOP,
            reinterpret_cast<const char*>(&receiveLocalMulticast),
            sizeof(receiveLocalMulticast)) == SOCKET_ERROR) {
        error = "IP_MULTICAST_LOOP failed: WSA error " +
            std::to_string(WSAGetLastError());
        closesocket(socketHandle);
        WSACleanup();
        return false;
    }
    setsockopt(
        socketHandle,
        SOL_SOCKET,
        SO_RCVTIMEO,
        reinterpret_cast<const char*>(&kReceiveTimeoutMs),
        sizeof(kReceiveTimeoutMs));

    m_callback = std::move(callback);
    m_running = true;
    m_thread = std::thread(&DriverLogListener::Run, this, socketHandle);
    return true;
}

void DriverLogListener::Stop() {
    m_running = false;
    if (m_thread.joinable()) {
        m_thread.join();
        WSACleanup();
    }
    m_callback = {};
}

void DriverLogListener::Run(SOCKET socketHandle) {
    std::vector<char> buffer(
        static_cast<std::size_t>(kMaxDriverLogPacketBytes) + 1);
    while (m_running.load()) {
        const int size = recvfrom(
            socketHandle,
            buffer.data(),
            kMaxDriverLogPacketBytes,
            0,
            nullptr,
            nullptr);
        if (size == SOCKET_ERROR) {
            const int error = WSAGetLastError();
            if (error == WSAETIMEDOUT || error == WSAEWOULDBLOCK) {
                continue;
            }
            break;
        }

        // The group carries more than one event type, so dispatch on the event
        // name rather than assuming a command report.
        DriverLogEvent event;
        if (ParseDriverLogBytes(buffer.data(), size, event) && m_callback) {
            m_callback(std::move(event));
        }
    }
    closesocket(socketHandle);
    m_running = false;
}

} // namespace anyadance::ui
