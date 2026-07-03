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
    const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
    const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
    return OuterWindowSizeForClient(clientWidth, clientHeight, style, exStyle);
}

inline SIZE DefaultOuterWindowSize() {
    return OuterWindowSizeForClient(
        kDefaultClientWidth, kDefaultClientHeight, kMainWindowStyle, kMainWindowExStyle);
}

} // namespace anyadance::ui
