#pragma once

#include "core/constants.h"
#include "core/driver_log_protocol.h"

#include <WinSock2.h>

#include <atomic>
#include <chrono>
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

    // Fills in the fields the sender owns, immediately before Send. Every
    // reporting path stamps rather than setting the fields itself, so a new
    // event type cannot ship numbered but untimed, or timed but unnumbered.
    //
    // Call this as late as possible: two threads report on this group, so every
    // instruction between stamping and handing the datagram to the socket is a
    // window in which they can leave in the opposite order to their numbers.
    void Stamp(anyadance::DriverLogEnvelope& envelope) {
        envelope.sequence = NextSequence();
        envelope.timestampMs = EpochMilliseconds();
    }

    // Next report number, starting at 1. Shared with every sender on the group
    // when Start was given a counter, so numbering is global rather than per
    // stream.
    std::uint64_t NextSequence() {
        return m_sequence ? ++*m_sequence : ++m_ownSequence;
    }

    // Wall clock in milliseconds since the Unix epoch. system_clock rather than
    // steady_clock because this crosses a process boundary and has to mean
    // something to a receiver; that it can step is why it never orders events.
    static std::uint64_t EpochMilliseconds() {
        const auto since = std::chrono::system_clock::now().time_since_epoch();
        const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(since).count();
        return millis > 0 ? static_cast<std::uint64_t>(millis) : 0;
    }

private:
    SOCKET m_socket = INVALID_SOCKET;
    sockaddr_in m_destination{};
    DriverLogSequence m_sequence;
    std::uint64_t m_ownSequence = 0;
};
