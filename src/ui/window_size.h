#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

namespace anyadance::ui {

inline constexpr int kDefaultClientWidth = 980;
inline constexpr int kDefaultClientHeight = 780;
inline constexpr int kMinClientWidth = 980;
inline constexpr int kMinClientHeight = 780;
inline constexpr int kMiniClientWidth = 360;
inline constexpr int kMiniClientHeight = 480;
inline constexpr int kMiniMinClientWidth = 280;
inline constexpr int kMiniMinClientHeight = 340;

inline constexpr DWORD kMainWindowStyle = WS_OVERLAPPEDWINDOW;
inline constexpr DWORD kMainWindowExStyle = WS_EX_APPWINDOW;

enum class UiMode {
    Full,
    Mini,
};

constexpr int MinClientWidthForMode(UiMode mode) {
    return mode == UiMode::Mini ? kMiniMinClientWidth : kMinClientWidth;
}

constexpr int MinClientHeightForMode(UiMode mode) {
    return mode == UiMode::Mini ? kMiniMinClientHeight : kMinClientHeight;
}

// Win32 CreateWindow/MoveWindow dimensions include the non-client title bar and
// borders. Convert a required ImGui client area into the corresponding outer
// window size so the canvas never starts smaller than the layout expects.
inline SIZE OuterWindowSizeForClient(int clientWidth, int clientHeight, DWORD style, DWORD exStyle) {
    RECT rect{0, 0, clientWidth, clientHeight};
    if (!AdjustWindowRectEx(&rect, style, FALSE, exStyle)) {
        return SIZE{clientWidth, clientHeight};
    }
    return SIZE{rect.right - rect.left, rect.bottom - rect.top};
}

inline SIZE OuterWindowSizeForClient(HWND hwnd, int clientWidth, int clientHeight) {
    RECT windowRect{};
    RECT clientRect{};
    if (GetWindowRect(hwnd, &windowRect) && GetClientRect(hwnd, &clientRect)) {
        const int currentWindowWidth = windowRect.right - windowRect.left;
        const int currentWindowHeight = windowRect.bottom - windowRect.top;
        const int currentClientWidth = clientRect.right - clientRect.left;
        const int currentClientHeight = clientRect.bottom - clientRect.top;
        return SIZE{
            clientWidth + currentWindowWidth - currentClientWidth,
            clientHeight + currentWindowHeight - currentClientHeight};
    }

    const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
    const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
    return OuterWindowSizeForClient(clientWidth, clientHeight, style, exStyle);
}

// AdjustWindowRectEx can differ from the realized non-client frame by a pixel
// on headless Windows and across DPI/theme configurations. Measure the actual
// window after creation and correct any deficit so the requested client area is
// a runtime guarantee, not an estimate based only on system metrics.
inline bool EnsureMinimumClientArea(HWND hwnd, int minClientWidth, int minClientHeight) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        RECT clientRect{};
        if (!GetClientRect(hwnd, &clientRect)) {
            return false;
        }
        const int clientWidth = clientRect.right - clientRect.left;
        const int clientHeight = clientRect.bottom - clientRect.top;
        if (clientWidth >= minClientWidth && clientHeight >= minClientHeight) {
            return true;
        }

        const int targetClientWidth = clientWidth < minClientWidth ? minClientWidth : clientWidth;
        const int targetClientHeight = clientHeight < minClientHeight ? minClientHeight : clientHeight;
        const SIZE targetWindow = OuterWindowSizeForClient(hwnd, targetClientWidth, targetClientHeight);
        if (!SetWindowPos(
                hwnd,
                nullptr,
                0,
                0,
                targetWindow.cx,
                targetWindow.cy,
                SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE)) {
            return false;
        }
    }

    RECT clientRect{};
    return GetClientRect(hwnd, &clientRect) &&
           clientRect.right - clientRect.left >= minClientWidth &&
           clientRect.bottom - clientRect.top >= minClientHeight;
}

inline SIZE DefaultOuterWindowSize() {
    return OuterWindowSizeForClient(
        kDefaultClientWidth, kDefaultClientHeight, kMainWindowStyle, kMainWindowExStyle);
}

} // namespace anyadance::ui
