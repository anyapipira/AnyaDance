#pragma once

#include "core/constants.h"

#include <WinSock2.h>

#include <cstdint>
#include <string>

struct DriverLogSenderConfig {
    bool enabled = true;
    std::string multicastGroup = anyadance::kDriverLogMulticastGroup;
    unsigned short port = anyadance::kDriverLogPort;
};

// Non-blocking sender for the driver log multicast group. Reporting is
// best-effort: a full socket buffer or an absent listener drops the datagram
// rather than delaying the caller, so this is safe both on the UDP receive
// thread and on SteamVR's RunFrame thread.
//
// One instance owns one socket and is not thread-safe. Each reporting path keeps
// its own, which avoids sharing a socket across threads and keeps the two
// sequence counters independent.
class DriverLogSender {
public:
    DriverLogSender() = default;
    ~DriverLogSender() { Stop(); }

    DriverLogSender(const DriverLogSender&) = delete;
    DriverLogSender& operator=(const DriverLogSender&) = delete;

    // Opens the socket. Returns false when disabled by config or on failure, in
    // which case Send is a no-op and the caller keeps working normally.
    // `purpose` names this stream in the driver log line written on success.
    bool Start(const DriverLogSenderConfig& config, const char* purpose);
    void Stop();
    bool IsOpen() const { return m_socket != INVALID_SOCKET; }

    // Sends one datagram. An oversized payload is dropped.
    void Send(const std::string& payload);

    // Next report number for this sender, starting at 1.
    std::uint64_t NextSequence() { return ++m_sequence; }

private:
    SOCKET m_socket = INVALID_SOCKET;
    sockaddr_in m_destination{};
    std::uint64_t m_sequence = 0;
};
