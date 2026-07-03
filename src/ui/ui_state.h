#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <windows.h>
#include <d3d11.h>

#include "core/constants.h"
#include "core/finger_control.h"
#include "core/frame_state.h"
#include "core/input_state.h"
#include "core/manipulation.h"
#include "core/mmd_retarget.h"
#include "core/nya_format.h"
#include "core/protocol.h"
#include "core/solved_motion.h"
#include "core/tpose.h"
#include "core/udp_log.h"
#include "ui/driver_control.h"
#include "ui/localization.h"
#include "ui/mmd_dance.h"
#include "ui/theme.h"
#include "ui/window_size.h"

#include "imgui.h"

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace anyadance::ui {
inline constexpr float kFingerBendStep = 0.1f;
inline constexpr wchar_t kSingleInstanceMutexName[] = L"Local\\AnyaDance.SingleInstance";

struct ScopedHandle {
    explicit ScopedHandle(HANDLE value) : handle(value) {}
    ~ScopedHandle() {
        if (handle) {
            CloseHandle(handle);
        }
    }

    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;

    HANDLE handle = nullptr;
};

// Number keys held while scrolling over the body panel target a single finger.
// 1-5 walk the left hand from pinky to thumb; 6-0 walk the right hand from thumb
// to pinky, so 5/6 are the two thumbs and 1/0 the two pinkies (a mirror around
// the middle). VK_0..VK_9 equal the ASCII digits '0'..'9'. The `name` is the
// English label used in the UDP log, which is always English.
struct FingerKey {
    int vk;
    int hand;  // 0 = left, 1 = right
    anyadance::Finger finger;
    const char* name;
};
inline constexpr FingerKey kFingerKeys[] = {
    {'1', 0, anyadance::Finger::Pinky,  "Left pinky"},
    {'2', 0, anyadance::Finger::Ring,   "Left ring"},
    {'3', 0, anyadance::Finger::Middle, "Left middle"},
    {'4', 0, anyadance::Finger::Index,  "Left index"},
    {'5', 0, anyadance::Finger::Thumb,  "Left thumb"},
    {'6', 1, anyadance::Finger::Thumb,  "Right thumb"},
    {'7', 1, anyadance::Finger::Index,  "Right index"},
    {'8', 1, anyadance::Finger::Middle, "Right middle"},
    {'9', 1, anyadance::Finger::Ring,   "Right ring"},
    {'0', 1, anyadance::Finger::Pinky,  "Right pinky"},
};

// Mouse pixels of drag from the press point that map to full right-stick
// deflection (1.0) when dragging the empty area of the body panel.
inline constexpr float kJoystickDragRangePixels = 120.0f;
inline constexpr wchar_t kWindowClassName[] = L"AnyaDance";
inline constexpr wchar_t kWindowTitle[] = L"AnyaDance";

// The UDP log is always recorded in English regardless of the UI language.
inline const char* En(Text id) {
    return Tr(id, Language::English);
}

// Owns the UDP socket and a background thread that streams the current pose to
// the driver at kStreamRateHz. The UI thread pushes new frames (with an optional
// reason that becomes a log entry); the worker thread does the actual sending.
// m_mutex guards the shared frame/socket; m_logMutex guards the log only.
class UdpStreamer {
public:
    ~UdpStreamer() { Stop(); }

    bool Start(const FrameState& initialFrame) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_running) {
            return true;
        }
        WSADATA wsa{};
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            m_log.Add(m_socketErrorReason, m_failedResult, {}, "WSAStartup failed");
            return false;
        }
        m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (m_socket == INVALID_SOCKET) {
            m_log.Add(m_socketErrorReason, m_failedResult, {}, "socket() failed");
            WSACleanup();
            return false;
        }
        m_destination.sin_family = AF_INET;
        InetPtonA(AF_INET, kUdpHost, &m_destination.sin_addr);
        m_destination.sin_port = htons(kUdpPort);
        m_frame = initialFrame;
        m_running = true;
        m_thread = std::thread(&UdpStreamer::Loop, this);
        return true;
    }

    void Stop() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_running) {
                return;
            }
            NeutralizeControllerInputs(m_frame);
            m_pinnedPayload.clear();
            m_pendingReason = m_releaseReason;
            m_pendingManipulation = false;
            m_running = false;
        }
        m_cv.notify_all();
        if (m_thread.joinable()) {
            m_thread.join();
        }
        if (m_socket != INVALID_SOCKET) {
            closesocket(m_socket);
            m_socket = INVALID_SOCKET;
        }
        WSACleanup();
    }

    void SetLocalizedResults(std::string sent, std::string failed, std::string socketError, std::string releaseReason) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_sentResult = std::move(sent);
        m_failedResult = std::move(failed);
        m_socketErrorReason = std::move(socketError);
        m_releaseReason = std::move(releaseReason);
    }

    void UpdateFrame(const FrameState& frame, std::string reason = {}, bool manipulation = false) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_frame = frame;
            m_pinnedPayload.clear();  // a new live pose supersedes a held resend
            if (!reason.empty()) {
                m_pendingReason = std::move(reason);
                m_pendingManipulation = manipulation;
            }
        }
        m_cv.notify_all();
    }

    std::vector<UdpLogEntry> SnapshotLog() const {
        std::lock_guard<std::mutex> lock(m_logMutex);
        return std::vector<UdpLogEntry>(m_log.Entries().begin(), m_log.Entries().end());
    }

    void ClearLog() {
        std::lock_guard<std::mutex> lock(m_logMutex);
        m_log.Clear();
    }

    // Resend a captured payload once over the same UDP socket and log the result.
    // Holding m_mutex keeps Stop() from closing the socket mid-send. Used by the
    // log detail dialog's Resend button so a past datagram can be replayed.
    bool SendRaw(const std::string& payload, const std::string& reason) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running || m_socket == INVALID_SOCKET) {
            std::lock_guard<std::mutex> logLock(m_logMutex);
            m_log.Add(m_socketErrorReason, m_failedResult, payload, "not streaming");
            return false;
        }
        const int error = SendDatagram(payload);
        std::lock_guard<std::mutex> logLock(m_logMutex);
        if (error != 0) {
            m_log.Add(m_socketErrorReason, m_failedResult, payload, "WSA error " + std::to_string(error));
            return false;
        }
        // Pin the resent datagram so the streaming loop keeps holding this pose
        // instead of overwriting it with the live frame on the next tick. The next
        // UpdateFrame (any pose change) clears the pin.
        m_pinnedPayload = payload;
        m_log.Add(reason, m_sentResult, payload);
        return true;
    }

private:
    // Send one datagram to the configured destination. Returns 0 on success or
    // the WSA error code on failure; the caller owns logging so the streaming
    // loop and the on-demand resend can format their own log entries.
    int SendDatagram(const std::string& payload) {
        const int result = sendto(
            m_socket,
            payload.data(),
            static_cast<int>(payload.size()),
            0,
            reinterpret_cast<const sockaddr*>(&m_destination),
            sizeof(m_destination));
        return result == SOCKET_ERROR ? WSAGetLastError() : 0;
    }

    void Loop() {
        auto nextSend = std::chrono::steady_clock::now();
        while (true) {
            FrameState frame;
            std::string reason;
            bool manipulation = false;
            bool shouldExit = false;
            std::string pinnedPayload;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait_until(lock, nextSend, [this] { return !m_running || !m_pendingReason.empty(); });
                frame = m_frame;
                reason = std::move(m_pendingReason);
                manipulation = m_pendingManipulation;
                m_pendingReason.clear();
                m_pendingManipulation = false;
                shouldExit = !m_running;
                pinnedPayload = m_pinnedPayload;
            }

            // Always send the latest pose; only write a log entry when a reason
            // is attached so the 60 Hz idle stream does not flood the log. While a
            // resend is pinned, keep sending that exact datagram so the pose holds.
            const std::string payload = pinnedPayload.empty() ? SerializeFrame(frame) : pinnedPayload;
            const int error = SendDatagram(payload);
            if (error != 0) {
                std::lock_guard<std::mutex> logLock(m_logMutex);
                m_log.Add(m_socketErrorReason, m_failedResult, payload, "WSA error " + std::to_string(error));
            } else if (!reason.empty()) {
                std::lock_guard<std::mutex> logLock(m_logMutex);
                if (manipulation) {
                    m_log.AddManipulation(m_sentResult, payload, reason);
                } else {
                    m_log.Add(reason, m_sentResult, payload);
                }
            }

            if (shouldExit) {
                break;
            }
            nextSend = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000 / kStreamRateHz);
        }
    }

    mutable std::mutex m_logMutex;
    UdpLog m_log;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    FrameState m_frame{};
    SOCKET m_socket = INVALID_SOCKET;
    sockaddr_in m_destination{};
    std::thread m_thread;
    bool m_running = false;
    std::string m_pendingReason;
    bool m_pendingManipulation = false;
    // When non-empty, a resent datagram the loop keeps streaming verbatim so the
    // avatar holds that pose. Cleared by the next UpdateFrame or by Stop.
    std::string m_pinnedPayload;
    std::string m_sentResult = "Sent";
    std::string m_failedResult = "Failed";
    std::string m_socketErrorReason = "Socket error";
    std::string m_releaseReason = "Input released";
};

// All mutable UI/session state for the UI, held in a single global instance so
// the Win32 message handlers and the per-frame ImGui render code share one view.
struct AppState {
    FrameState frame = BuildResetTPose(MakeNeutralFrame());
    KeyboardInputState keyboard;
    UdpStreamer streamer;
    bool focused = true;
    bool captureActive = false;
    bool cursorHidden = false;
    POINT lastMouse{};
    DragSnapshot drag{};
    DeviceIndex dragDevice = DeviceIndex::Hmd;
    int captureButton = VK_LBUTTON;
    ManipulationFrame manipulationFrame = ManipulationFrame::Hmd;
    int captureModifiers = ManipulationModifier_None;
    bool mirrorHands = false;
    bool mirrorFeet = false;
    // Per-hand finger bends ([0] left, [1] right). The wheel adjusts all fingers,
    // or just the one whose number key is held (see kFingerKeys). lastFingerBends
    // tracks the previous values to detect wheel changes for the log.
    std::array<FingerBends, 2> fingerBends{};
    std::array<FingerBends, 2> lastFingerBends{};
    bool joystickActive = false;  // dragging the empty body panel as the right stick
    ImVec2 joystickOrigin{};      // press point (ImGui space) used as the stick center
    float joystickX = 0.0f;       // current right-stick deflection, [-1, 1]
    float joystickY = 0.0f;
    bool alwaysOnTop = false;
    UiMode uiMode = UiMode::Full;
    bool uiModeChangePending = false;
    UiMode pendingUiMode = UiMode::Full;
    bool disclaimerAccepted = false;
    bool driverStatusSet = false;
    DriverStatus driverStatus = DriverStatus::Failed;
    std::string driverStatusDetail;
    int selectedLogIndex = -1;
    bool logScrollToLatest = true;

    // MMD dance: dialog parameters, async Blender solve, and playback state.
    bool danceDialogOpen = false;
    char danceVmdPath[1024] = {};
    char danceModelPath[1024] = {};
    char danceBlenderPath[1024] = {};   // Advanced: blender.exe (auto-filled)
    char danceMmdToolsPath[1024] = {};  // Advanced: MMD Tools dir (auto-filled)
    // Fixed playback and remap defaults used by the dance dialog.
    float danceHeight = 1.5f;
    float danceSpeed = 1.0f;
    float danceFps = 60.0f;
    float danceHandReach = 1.22f;
    bool danceLoop = true;
    std::string danceStatus;
    bool danceConverting = false;
    std::future<MmdExportResult> danceFuture;
    DanceMotion danceMotion;
    bool dancePlaying = false;
    bool dancePaused = false;
    float dancePausedElapsed = 0.0f;
    std::chrono::steady_clock::time_point danceStartTime;
    float danceRootX = 0.0f;
    float danceRootZ = 0.0f;
};

extern AppState g_app;

extern ID3D11ShaderResourceView* g_bodyBackgroundTexture;
extern int g_bodyBackgroundWidth;
extern int g_bodyBackgroundHeight;
extern ID3D11ShaderResourceView* g_footerBannerTexture;
extern int g_footerBannerWidth;
extern int g_footerBannerHeight;
extern ImFont* g_monoFont;

int MinWindowWidth();
int MinWindowHeight();
void CopyPreferenceString(char* buffer, std::size_t size, const std::string& value);
void ApplyAlwaysOnTop(HWND hwnd, bool onTop);
void ApplyPendingUiMode(HWND hwnd);
void RenderUiModeControls(HWND hwnd, bool includeAlwaysOnTop);

void HandleFocusLost();
void ReleaseMouseCapture();
bool IsKeyDown(int vk);
void BeginMouseCapture(HWND hwnd, DeviceIndex device, int captureButton);
void UpdateCapture(HWND hwnd);
void ApplyFingerBend(FrameState& frame);
FrameState FrameWithCurrentFingerBends();
void ApplyRightJoystick(FrameState& frame, float x, float y);
void HandleKeyboard(HWND hwnd);

void RenderBodyPanel(HWND hwnd, bool miniMode = false);
void RenderLogPanel();
void RenderDanceDialog(HWND hwnd);
void RenderUi(HWND hwnd);
void RenderMiniUi(HWND hwnd);

void StopDanceToTPose();
void RestorePose(const FrameState& pose, const char* reason = "Pose restored");
void PollDanceExport();
void UpdateDancePlayback();

} // namespace anyadance::ui
