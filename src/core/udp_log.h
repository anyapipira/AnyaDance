#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <string>

namespace anyadance {

struct UdpLogEntry {
    std::chrono::system_clock::time_point timestamp{};
    std::string timeText;
    std::string reason;
    std::string result;
    std::string endpoint;
    std::string payload;
    std::string detail;
    // Driver log event number, or 0 for a row this process produced itself.
    // Rows carrying one are ordered by it rather than by arrival.
    std::uint64_t sequence = 0;
};

class UdpLog {
public:
    void Add(std::string reason, std::string result, std::string payload, std::string detail = {});
    void AddManipulation(std::string result, std::string payload, std::string reason = "Device manipulated");
    void AddDriverCommand(
        std::string reason,
        std::string result,
        std::string endpoint,
        std::string payload,
        std::string detail,
        bool accepted,
        std::uint64_t sequence);
    // A driver event that is never coalesced, placed by its sequence like the
    // command rows so all driver events share one order.
    void AddDriverEvent(
        std::string reason,
        std::string result,
        std::string payload,
        std::string detail,
        std::uint64_t sequence);
    const std::deque<UdpLogEntry>& Entries() const { return m_entries; }
    void Clear();

private:
    void Push(UdpLogEntry entry);
    // Places a sequenced entry after every driver event that precedes it and
    // before every one that follows, so the log reads in the order the driver
    // produced the events rather than the order UDP delivered them.
    void InsertBySequence(UdpLogEntry entry);
    // True when this event number is already in the reorder window, which means
    // UDP delivered a duplicate.
    bool AlreadyLogged(std::uint64_t sequence) const;
    // True when nothing newer has been logged, so coalescing is safe.
    bool IsNewestDriverEvent(std::uint64_t sequence) const;
    static std::string FormatTime(std::chrono::system_clock::time_point timePoint);

    std::deque<UdpLogEntry> m_entries;
    std::chrono::steady_clock::time_point m_lastManipulationLog{};
    std::chrono::steady_clock::time_point m_lastDriverCommandLog{};
    static constexpr std::size_t kCapacity = 1000;
    // How far back a late event may be placed. Loopback reordering spans a
    // handful of datagrams at most, so this is generous; bounding it keeps the
    // insert cheap and stops an ancient stray event from rewriting history.
    static constexpr std::size_t kReorderWindow = 64;
};

} // namespace anyadance
