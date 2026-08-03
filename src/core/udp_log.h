#pragma once

#include <chrono>
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
        bool accepted);
    const std::deque<UdpLogEntry>& Entries() const { return m_entries; }
    void Clear();

private:
    void Push(UdpLogEntry entry);
    static std::string FormatTime(std::chrono::system_clock::time_point timePoint);

    std::deque<UdpLogEntry> m_entries;
    std::chrono::steady_clock::time_point m_lastManipulationLog{};
    std::chrono::steady_clock::time_point m_lastDriverCommandLog{};
    static constexpr std::size_t kCapacity = 1000;
};

} // namespace anyadance
