#pragma once

#include "core/constants.h"

#include <WinSock2.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

// Numbers every driver log event, whatever its type and whichever thread
// produced it, so `sequence` has one meaning across the whole group and the
// reports of all types share a single order.
using DriverLogSequence = std::shared_ptr<std::atomic<std::uint64_t>>;

inline DriverLogSequence MakeDriverLogSequence() {
    return std::make_shared<std::atomic<std::uint64_t>>(0);
}

struct DriverLogSenderConfig {
    bool enabled = true;
    std::string multicastGroup = anyadance::kDriverLogMulticastGroup;
    unsigned short port = anyadance::kDriverLogPort;
    // Shared with every other sender on this group. A sender without one still
    // works; it just numbers its own events.
    DriverLogSequence sequence;
};

// Non-blocking sender for the driver log multicast group. Reporting is
// best-effort: a full socket buffer or an absent listener drops the datagram
// rather than delaying the caller, so this is safe both on the UDP receive
// thread and on SteamVR's RunFrame thread.
//
// One instance owns one socket and is not thread-safe. Each reporting path keeps
// its own, which avoids sharing a socket across threads; they share only the
// atomic sequence counter, so event numbering stays global.
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

    // Next report number, starting at 1. Shared with every sender on the group
    // when Start was given a counter, so numbering is global rather than per
    // stream.
    std::uint64_t NextSequence() {
        return m_sequence ? ++*m_sequence : ++m_ownSequence;
    }

private:
    SOCKET m_socket = INVALID_SOCKET;
    sockaddr_in m_destination{};
    DriverLogSequence m_sequence;
    std::uint64_t m_ownSequence = 0;
};
