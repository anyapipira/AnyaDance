#include "test_framework.h"

#include "ui/layout.h"
#include "ui/window_size.h"

namespace anyadance::tests {

void TestUiLayout() {
    using namespace anyadance::ui;

    EXPECT_TRUE(MinClientWidthForMode(UiMode::Full) == kMinClientWidth);
    EXPECT_TRUE(MinClientHeightForMode(UiMode::Full) == kMinClientHeight);
    EXPECT_TRUE(MinClientWidthForMode(UiMode::Mini) == kMiniMinClientWidth);
    EXPECT_TRUE(MinClientHeightForMode(UiMode::Mini) == kMiniMinClientHeight);

    // The startup dimensions are outer-window dimensions which must reserve
    // space for the title bar and borders in addition to the requested canvas.
    const SIZE outer = DefaultOuterWindowSize();
    EXPECT_TRUE(outer.cx >= kDefaultClientWidth);
    EXPECT_TRUE(outer.cy > kDefaultClientHeight);

    // Verify the conversion against a real (hidden) Win32 window. Use a client
    // size that fits the 1024x768 virtual desktop on Windows CI runners; Windows
    // clamps top-level windows to that desktop before they can realize a
    // 780-pixel client height.
    constexpr int testClientWidth = 640;
    constexpr int testClientHeight = 480;
    const SIZE testOuter = OuterWindowSizeForClient(
        testClientWidth, testClientHeight, kMainWindowStyle, kMainWindowExStyle);
    HWND window = CreateWindowExW(
        kMainWindowExStyle,
        L"STATIC",
        L"AnyaDance layout test",
        kMainWindowStyle,
        0,
        0,
        testOuter.cx,
        testOuter.cy,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
    EXPECT_TRUE(window != nullptr);
    if (window) {
        EXPECT_TRUE(EnsureMinimumClientArea(window, testClientWidth, testClientHeight));
        RECT client{};
        EXPECT_TRUE(GetClientRect(window, &client) != FALSE);
        EXPECT_TRUE(client.right - client.left >= testClientWidth);
        EXPECT_TRUE(client.bottom - client.top >= testClientHeight);
        DestroyWindow(window);
    }

    constexpr float baseFooter = MainFooterHeightForMetrics(16.0f, 3.0f, 4.0f, 8.0f);
    EXPECT_NEAR(baseFooter, 79.0f, 0.0001f);
    // Button-theme changes affect FramePadding. The footer must grow with them
    // instead of remaining at a stale hardcoded height.
    constexpr float paddedFooter = MainFooterHeightForMetrics(16.0f, 6.0f, 4.0f, 8.0f);
    EXPECT_NEAR(paddedFooter - baseFooter, 6.0f, 0.0001f);
    constexpr float spacedFooter = MainFooterHeightForMetrics(16.0f, 3.0f, 6.0f, 8.0f);
    EXPECT_NEAR(spacedFooter - baseFooter, 8.0f, 0.0001f);
}

} // namespace anyadance::tests
