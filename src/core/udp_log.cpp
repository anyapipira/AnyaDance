#include "core/udp_log.h"

#include <ctime>
#include <iomanip>
#include <sstream>

namespace anyadance {

void UdpLog::Add(std::string reason, std::string result, std::string payload, std::string detail) {
    UdpLogEntry entry{};
    entry.timestamp = std::chrono::system_clock::now();
    entry.timeText = FormatTime(entry.timestamp);
    entry.reason = std::move(reason);
    entry.result = std::move(result);
    entry.payload = std::move(payload);
    entry.detail = std::move(detail);
    Push(std::move(entry));
}

void UdpLog::AddManipulation(std::string result, std::string payload, std::string reason) {
    const auto now = std::chrono::steady_clock::now();
    UdpLogEntry entry{};
    entry.timestamp = std::chrono::system_clock::now();
    entry.timeText = FormatTime(entry.timestamp);
    entry.reason = std::move(reason);
    entry.result = std::move(result);
    entry.payload = std::move(payload);

    if (!m_entries.empty() && m_entries.back().reason == entry.reason &&
        now - m_lastManipulationLog < std::chrono::milliseconds(100)) {
        m_entries.back() = std::move(entry);
        return;
    }

    m_lastManipulationLog = now;
    Push(std::move(entry));
}

void UdpLog::AddDriverCommand(
    std::string reason,
    std::string result,
    std::string endpoint,
    std::string payload,
    std::string detail,
    bool accepted) {
    const auto now = std::chrono::steady_clock::now();
    UdpLogEntry entry{};
    entry.timestamp = std::chrono::system_clock::now();
    entry.timeText = FormatTime(entry.timestamp);
    entry.reason = std::move(reason);
    entry.result = std::move(result);
    entry.endpoint = std::move(endpoint);
    entry.payload = std::move(payload);
    entry.detail = std::move(detail);

    // Moving poses commonly arrive at 60 Hz. Keep the latest accepted command
    // from a sender in each 100 ms display window so monitoring does not turn
    // the log view into a rendering bottleneck. Rejections are never coalesced.
    if (accepted && !m_entries.empty() &&
        m_entries.back().reason == entry.reason &&
        m_entries.back().endpoint == entry.endpoint &&
        now - m_lastDriverCommandLog < std::chrono::milliseconds(100)) {
        m_entries.back() = std::move(entry);
        return;
    }

    m_lastDriverCommandLog = now;
    Push(std::move(entry));
}

void UdpLog::Clear() {
    m_entries.clear();
}

void UdpLog::Push(UdpLogEntry entry) {
    m_entries.push_back(std::move(entry));
    while (m_entries.size() > kCapacity) {
        m_entries.pop_front();
    }
}

std::string UdpLog::FormatTime(std::chrono::system_clock::time_point timePoint) {
    const auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(timePoint);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(timePoint - seconds).count();
    const std::time_t time = std::chrono::system_clock::to_time_t(timePoint);
    std::tm localTime{};
#ifdef _WIN32
    localtime_s(&localTime, &time);
#else
    localtime_r(&time, &localTime);
#endif
    std::ostringstream out;
    out << std::put_time(&localTime, "%H:%M:%S") << '.' << std::setw(3) << std::setfill('0') << millis;
    return out.str();
}

} // namespace anyadance
