#pragma once

#include "core/driver_log_protocol.h"

#include <WinSock2.h>

#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace anyadance::ui {

// Hot-swappable UDP listener for driver command telemetry. It owns a separate
// socket and thread so monitoring can be toggled without disturbing pose sends.
class DriverLogListener {
public:
    using Callback = std::function<void(DriverCommandLogPacket)>;

    ~DriverLogListener() { Stop(); }

    DriverLogListener(const DriverLogListener&) = delete;
    DriverLogListener& operator=(const DriverLogListener&) = delete;

    DriverLogListener() = default;
    bool Start(
        const char* multicastGroup,
        unsigned short port,
        Callback callback,
        std::string& error);
    void Stop();
    bool IsRunning() const { return m_running.load(); }

private:
    void Run(SOCKET socketHandle);

    std::atomic<bool> m_running{false};
    Callback m_callback;
    std::thread m_thread;
};

} // namespace anyadance::ui
