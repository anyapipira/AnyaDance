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
    bool accepted,
    std::uint64_t sequence,
    std::uint64_t timestampMs) {
    const auto now = std::chrono::steady_clock::now();
    if (sequence != 0 && AlreadyLogged(sequence)) {
        return;  // UDP delivered this event twice
    }

    UdpLogEntry entry{};
    entry.timestamp = DriverEventTime(timestampMs);
    entry.timeText = FormatTime(entry.timestamp);
    entry.reason = std::move(reason);
    entry.result = std::move(result);
    entry.endpoint = std::move(endpoint);
    entry.payload = std::move(payload);
    entry.detail = std::move(detail);
    entry.sequence = sequence;

    // Moving poses commonly arrive at 60 Hz. Keep the latest accepted command
    // from a sender in each 100 ms display window so monitoring does not turn
    // the log view into a rendering bottleneck. Rejections are never coalesced,
    // and neither is an event that arrived late: collapsing it into the newest
    // row would drop the row it belongs behind.
    if (accepted && IsNewestDriverEvent(sequence) && !m_entries.empty() &&
        m_entries.back().reason == entry.reason &&
        m_entries.back().endpoint == entry.endpoint &&
        now - m_lastDriverCommandLog < std::chrono::milliseconds(100)) {
        m_entries.back() = std::move(entry);
        return;
    }

    m_lastDriverCommandLog = now;
    InsertBySequence(std::move(entry));
}

void UdpLog::AddDriverEvent(
    std::string reason,
    std::string result,
    std::string payload,
    std::string detail,
    std::uint64_t sequence,
    std::uint64_t timestampMs) {
    if (sequence != 0 && AlreadyLogged(sequence)) {
        return;
    }

    UdpLogEntry entry{};
    entry.timestamp = DriverEventTime(timestampMs);
    entry.timeText = FormatTime(entry.timestamp);
    entry.reason = std::move(reason);
    entry.result = std::move(result);
    entry.payload = std::move(payload);
    entry.detail = std::move(detail);
    entry.sequence = sequence;
    InsertBySequence(std::move(entry));
}

bool UdpLog::AlreadyLogged(std::uint64_t sequence) const {
    std::size_t steps = 0;
    for (auto it = m_entries.rbegin();
         it != m_entries.rend() && steps < kReorderWindow;
         ++it, ++steps) {
        if (it->sequence == sequence) {
            return true;
        }
    }
    return false;
}

bool UdpLog::IsNewestDriverEvent(std::uint64_t sequence) const {
    if (sequence == 0 || m_entries.empty()) {
        return true;
    }
    const std::uint64_t newest = m_entries.back().sequence;
    return newest == 0 || newest < sequence;
}

void UdpLog::InsertBySequence(UdpLogEntry entry) {
    if (entry.sequence == 0) {
        Push(std::move(entry));
        return;
    }

    // Walk back over driver events that belong after this one. Rows without a
    // sequence are this process's own and stop the walk, so a late driver event
    // never jumps ahead of a locally logged one it genuinely followed.
    auto position = m_entries.end();
    std::size_t steps = 0;
    while (position != m_entries.begin() && steps < kReorderWindow) {
        const auto previous = std::prev(position);
        if (previous->sequence == 0 || previous->sequence < entry.sequence) {
            break;
        }
        position = previous;
        ++steps;
    }

    m_entries.insert(position, std::move(entry));
    while (m_entries.size() > kCapacity) {
        m_entries.pop_front();
    }
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

std::chrono::system_clock::time_point UdpLog::DriverEventTime(std::uint64_t timestampMs) {
    if (timestampMs == 0) {
        return std::chrono::system_clock::now();
    }
    return std::chrono::system_clock::time_point(
        std::chrono::milliseconds(static_cast<std::chrono::milliseconds::rep>(timestampMs)));
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
