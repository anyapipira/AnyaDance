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
    const char* host,
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

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (InetPtonA(AF_INET, host, &address.sin_addr) != 1) {
        error = "invalid listener IPv4 address";
        closesocket(socketHandle);
        WSACleanup();
        return false;
    }
    if (bind(
            socketHandle,
            reinterpret_cast<const sockaddr*>(&address),
            sizeof(address)) == SOCKET_ERROR) {
        error = "bind() failed: WSA error " + std::to_string(WSAGetLastError());
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

        DriverCommandLogPacket packet;
        if (ParseDriverCommandLogBytes(buffer.data(), size, packet) && m_callback) {
            m_callback(std::move(packet));
        }
    }
    closesocket(socketHandle);
    m_running = false;
}

} // namespace anyadance::ui
