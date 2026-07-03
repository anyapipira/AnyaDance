#include "ui/ui_state.h"
#include "ui/resource.h"

// Define before any Windows header so the windows.h min/max macros do not
// shadow std::min/std::max/std::clamp. Guarded so it does not clash with the
// same definitions the build passes on the command line.
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
#include <wincodec.h>
#include <tchar.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <future>
#include <iomanip>
#include <sstream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#pragma comment(lib, "ws2_32.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace anyadance::ui {

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_deviceContext = nullptr;
IDXGISwapChain* g_swapChain = nullptr;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
ID3D11ShaderResourceView* g_bodyBackgroundTexture = nullptr;
int g_bodyBackgroundWidth = 0;
int g_bodyBackgroundHeight = 0;
ID3D11ShaderResourceView* g_footerBannerTexture = nullptr;
int g_footerBannerWidth = 0;
int g_footerBannerHeight = 0;

ImFont* g_uiFont = nullptr;
ImFont* g_monoFont = nullptr;
ImVector<ImWchar> g_uiGlyphRanges;

std::string SettingsDirectory() {
    const char* localAppData = std::getenv("LOCALAPPDATA");
    if (!localAppData || localAppData[0] == '\0') {
        return ".";
    }
    return std::string(localAppData) + "\\AnyaDance";
}

std::string SettingsPath() {
    return SettingsDirectory() + "\\ui_state.ini";
}

std::string LegacySettingsPath() {
    return SettingsDirectory() + "\\tool_state.ini";
}

const char* UiModeCode(UiMode mode) {
    return mode == UiMode::Mini ? "mini" : "full";
}

UiMode ParseUiMode(const std::string& value) {
    return value == "mini" ? UiMode::Mini : UiMode::Full;
}

int MinClientWidth() {
    return MinClientWidthForMode(g_app.uiMode);
}

int MinClientHeight() {
    return MinClientHeightForMode(g_app.uiMode);
}

void CopyPreferenceString(char* buffer, std::size_t size, const std::string& value) {
    if (size == 0) {
        return;
    }
    std::snprintf(buffer, size, "%s", value.c_str());
}

void LoadPreferences(HWND hwnd) {
    std::ifstream in(SettingsPath());
    if (!in) {
        in.open(LegacySettingsPath());
    }
    std::string key;
    while (in >> key) {
        if (key == "language") {
            std::string value;
            in >> value;
            SetCurrentLanguage(FindLanguageByCode(value.c_str(), CurrentLanguage()));
        } else if (key == "disclaimer_accepted") {
            int value = 0;
            in >> value;
            g_app.disclaimerAccepted = value != 0;
        } else if (key == "always_on_top") {
            int value = 0;
            in >> value;
            g_app.alwaysOnTop = value != 0;
        } else if (key == "ui_mode") {
            std::string value;
            in >> value;
            g_app.uiMode = ParseUiMode(value);
        } else if (key == "dance_blender_path") {
            std::string value;
            in >> std::quoted(value);
            CopyPreferenceString(g_app.danceBlenderPath, sizeof(g_app.danceBlenderPath), value);
        } else if (key == "dance_mmd_tools_path") {
            std::string value;
            in >> std::quoted(value);
            CopyPreferenceString(g_app.danceMmdToolsPath, sizeof(g_app.danceMmdToolsPath), value);
        } else if (key == "window") {
            int x = 100;
            int y = 100;
            int w = kDefaultClientWidth;
            int h = kDefaultClientHeight;
            in >> x >> y >> w >> h;
            const SIZE minWindow = OuterWindowSizeForClient(hwnd, MinClientWidth(), MinClientHeight());
            w = std::max(w, static_cast<int>(minWindow.cx));
            h = std::max(h, static_cast<int>(minWindow.cy));
            MoveWindow(hwnd, x, y, w, h, FALSE);
            EnsureMinimumClientArea(hwnd, MinClientWidth(), MinClientHeight());
        }
    }
}

void SavePreferences(HWND hwnd) {
    const std::string dir = SettingsDirectory();
    CreateDirectoryA(dir.c_str(), nullptr);
    RECT rect{};
    GetWindowRect(hwnd, &rect);
    std::ofstream out(SettingsPath(), std::ios::trunc);
    out << "language " << GetLanguageInfo(CurrentLanguage()).code << '\n';
    out << "disclaimer_accepted " << (g_app.disclaimerAccepted ? 1 : 0) << '\n';
    out << "always_on_top " << (g_app.alwaysOnTop ? 1 : 0) << '\n';
    out << "ui_mode " << UiModeCode(g_app.uiMode) << '\n';
    out << "dance_blender_path " << std::quoted(std::string(g_app.danceBlenderPath)) << '\n';
    out << "dance_mmd_tools_path " << std::quoted(std::string(g_app.danceMmdToolsPath)) << '\n';
    out << "window " << rect.left << ' ' << rect.top << ' ' << (rect.right - rect.left) << ' ' << (rect.bottom - rect.top) << '\n';
}

bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    return D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createDeviceFlags,
        featureLevelArray,
        2,
        D3D11_SDK_VERSION,
        &sd,
        &g_swapChain,
        &g_device,
        &featureLevel,
        &g_deviceContext) == S_OK;
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}

void CleanupBodyBackgroundTexture() {
    if (g_bodyBackgroundTexture) {
        g_bodyBackgroundTexture->Release();
        g_bodyBackgroundTexture = nullptr;
    }
    g_bodyBackgroundWidth = 0;
    g_bodyBackgroundHeight = 0;
}

void CleanupFooterBannerTexture() {
    if (g_footerBannerTexture) {
        g_footerBannerTexture->Release();
        g_footerBannerTexture = nullptr;
    }
    g_footerBannerWidth = 0;
    g_footerBannerHeight = 0;
}

void CreateRenderTarget() {
    ID3D11Texture2D* backBuffer = nullptr;
    g_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    g_device->CreateRenderTargetView(backBuffer, nullptr, &g_mainRenderTargetView);
    backBuffer->Release();
}

void CleanupDeviceD3D() {
    CleanupBodyBackgroundTexture();
    CleanupFooterBannerTexture();
    CleanupRenderTarget();
    if (g_swapChain) { g_swapChain->Release(); g_swapChain = nullptr; }
    if (g_deviceContext) { g_deviceContext->Release(); g_deviceContext = nullptr; }
    if (g_device) { g_device->Release(); g_device = nullptr; }
}

bool LoadPngResourceTexture(HINSTANCE instance, int resourceId, ID3D11ShaderResourceView** outTexture, int* outWidth, int* outHeight) {
    HRSRC resource = FindResourceW(instance, MAKEINTRESOURCEW(resourceId), MAKEINTRESOURCEW(10));
    if (!resource) {
        return false;
    }
    HGLOBAL loadedResource = LoadResource(instance, resource);
    const DWORD resourceSize = SizeofResource(instance, resource);
    const void* resourceData = LockResource(loadedResource);
    if (!loadedResource || resourceSize == 0 || !resourceData) {
        return false;
    }

    IWICImagingFactory* factory = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    bool ok = false;

    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr)) {
        hr = factory->CreateStream(&stream);
    }
    if (SUCCEEDED(hr)) {
        hr = stream->InitializeFromMemory(
            reinterpret_cast<BYTE*>(const_cast<void*>(resourceData)),
            resourceSize);
    }
    if (SUCCEEDED(hr)) {
        hr = factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder);
    }
    if (SUCCEEDED(hr)) {
        hr = decoder->GetFrame(0, &frame);
    }
    if (SUCCEEDED(hr)) {
        hr = factory->CreateFormatConverter(&converter);
    }
    if (SUCCEEDED(hr)) {
        hr = converter->Initialize(
            frame,
            GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom);
    }

    UINT width = 0;
    UINT height = 0;
    if (SUCCEEDED(hr)) {
        hr = converter->GetSize(&width, &height);
    }
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 4);
    if (SUCCEEDED(hr)) {
        hr = converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data());
    }
    if (SUCCEEDED(hr)) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA data{};
        data.pSysMem = pixels.data();
        data.SysMemPitch = width * 4;

        ID3D11Texture2D* texture = nullptr;
        hr = g_device->CreateTexture2D(&desc, &data, &texture);
        if (SUCCEEDED(hr)) {
            hr = g_device->CreateShaderResourceView(texture, nullptr, outTexture);
            texture->Release();
        }
    }

    if (SUCCEEDED(hr)) {
        *outWidth = static_cast<int>(width);
        *outHeight = static_cast<int>(height);
        ok = true;
    }

    if (converter) { converter->Release(); }
    if (frame) { frame->Release(); }
    if (decoder) { decoder->Release(); }
    if (stream) { stream->Release(); }
    if (factory) { factory->Release(); }
    return ok;
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // The mouse wheel is handled inside the frame (see RenderBodyPanel) so that
    // ImGui window z-order decides whether it bends fingers or scrolls the log.
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) {
        return true;
    }
    switch (msg) {
    case WM_GETMINMAXINFO: {
        MINMAXINFO* minMax = reinterpret_cast<MINMAXINFO*>(lParam);
        const SIZE minWindow = OuterWindowSizeForClient(hWnd, MinClientWidth(), MinClientHeight());
        minMax->ptMinTrackSize.x = minWindow.cx;
        minMax->ptMinTrackSize.y = minWindow.cy;
        return 0;
    }
    case WM_SIZE:
        if (g_device != nullptr && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            g_swapChain->ResizeBuffers(0, static_cast<UINT>(LOWORD(lParam)), static_cast<UINT>(HIWORD(lParam)), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) {
            return 0;
        }
        break;
    case WM_KILLFOCUS:
        HandleFocusLost();
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

const ImWchar* BuildUiGlyphRanges(ImGuiIO& io) {
    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
    for (std::size_t l = 0; l < kLanguageCount; ++l) {
        const Language language = static_cast<Language>(l);
        builder.AddText(GetLanguageInfo(language).displayName);
        for (std::size_t t = 0; t < kTextCount; ++t) {
            builder.AddText(Tr(static_cast<Text>(t), language));
        }
    }
    builder.BuildRanges(&g_uiGlyphRanges);
    return g_uiGlyphRanges.Data;
}
ImFont* LoadUiFont(ImGuiIO& io) {
    const ImWchar* ranges = BuildUiGlyphRanges(io);
    ImFontConfig config;
    config.OversampleH = 2;
    config.OversampleV = 2;
    config.RasterizerMultiply = 1.15f;
    const char* fontCandidates[] = {
        "C:\\Windows\\Fonts\\msyh.ttc",
        "C:\\Windows\\Fonts\\msyh.ttf",
        "C:\\Windows\\Fonts\\simhei.ttf",
        "C:\\Windows\\Fonts\\simsun.ttc",
        "C:\\Windows\\Fonts\\NotoSansCJK-Regular.ttc",
        "C:\\Windows\\Fonts\\NotoSansSC-Regular.otf",
    };
    for (const char* path : fontCandidates) {
        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
            continue;
        }
        if (ImFont* font = io.Fonts->AddFontFromFileTTF(path, 16.0f, &config, ranges)) {
            return font;
        }
    }
    return io.Fonts->AddFontDefault();
}

enum class DisclaimerAction { None, Accept, Quit };

DisclaimerAction RenderDisclaimerGate() {
    DisclaimerAction action = DisclaimerAction::None;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(660.0f, 0.0f));
    ImGui::Begin("disclaimer_gate", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);

    ImGui::TextUnformatted(Tr(Text::LanguageLabel));
    for (std::size_t i = 0; i < kLanguageCount; ++i) {
        const Language language = static_cast<Language>(i);
        ImGui::SameLine();
        if (ImGui::RadioButton(GetLanguageInfo(language).displayName, CurrentLanguage() == language)) {
            SetCurrentLanguage(language);
        }
    }

    ImGui::Separator();
    ImGui::TextUnformatted(Tr(Text::DisclaimerTitle));
    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(Tr(Text::DisclaimerBody));
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button(Tr(Text::DisclaimerAccept), ImVec2(280.0f, 36.0f))) {
        action = DisclaimerAction::Accept;
    }
    ImGui::SameLine();
    if (ImGui::Button(Tr(Text::DisclaimerQuit), ImVec2(140.0f, 36.0f))) {
        action = DisclaimerAction::Quit;
    }
    ImGui::End();
    return action;
}
} // namespace anyadance::ui

using namespace anyadance::ui;

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    ScopedHandle instanceMutex(CreateMutexW(nullptr, TRUE, kSingleInstanceMutexName));
    if (!instanceMutex.handle) {
        const DWORD err = GetLastError();
        wchar_t msg[256]{};
        swprintf_s(msg, L"CreateMutexW failed. GetLastError = %lu", err);
        MessageBoxW(nullptr, msg, kWindowTitle, MB_OK | MB_ICONERROR);
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowW(kWindowClassName, kWindowTitle)) {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        return 0;
    }

    const HRESULT comInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    HICON appIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APPICON));

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.cbClsExtra = 0L;
    wc.cbWndExtra = 0L;
    wc.hInstance = hInstance;
    wc.hIcon = appIcon;
    wc.hCursor = nullptr;
    wc.hbrBackground = nullptr;
    wc.lpszMenuName = nullptr;
    wc.lpszClassName = kWindowClassName;
    wc.hIconSm = appIcon;

    ATOM classAtom = RegisterClassExW(&wc);
    if (classAtom == 0) {
        const DWORD err = GetLastError();
        wchar_t msg[256]{};
        swprintf_s(msg, L"RegisterClassExW failed. GetLastError = %lu", err);
        MessageBoxW(nullptr, msg, kWindowTitle, MB_OK | MB_ICONERROR);
        if (SUCCEEDED(comInit)) {
            CoUninitialize();
        }
        return 1;
    }

    const SIZE defaultWindow = DefaultOuterWindowSize();
    HWND hwnd = CreateWindowExW(
        kMainWindowExStyle,
        kWindowClassName,
        kWindowTitle,
        kMainWindowStyle,
        100,
        100,
        defaultWindow.cx,
        defaultWindow.cy,
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );

    if (!hwnd) {
        const DWORD err = GetLastError();
        wchar_t msg[256]{};
        swprintf_s(msg, L"CreateWindowExW failed. GetLastError = %lu", err);
        MessageBoxW(nullptr, msg, kWindowTitle, MB_OK | MB_ICONERROR);
        UnregisterClassW(kWindowClassName, hInstance);
        if (SUCCEEDED(comInit)) {
            CoUninitialize();
        }
        return 1;
    }

    EnsureMinimumClientArea(hwnd, kDefaultClientWidth, kDefaultClientHeight);

    SetWindowTextW(hwnd, kWindowTitle);

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        DestroyWindow(hwnd);
        UnregisterClassW(kWindowClassName, hInstance);
        if (SUCCEEDED(comInit)) {
            CoUninitialize();
        }
        return 1;
    }

    LoadPngResourceTexture(
        hInstance,
        IDR_ANYA_PNG,
        &g_bodyBackgroundTexture,
        &g_bodyBackgroundWidth,
        &g_bodyBackgroundHeight
    );
    LoadPngResourceTexture(
        hInstance,
        IDR_ANYA_BANNER_PNG,
        &g_footerBannerTexture,
        &g_footerBannerWidth,
        &g_footerBannerHeight
    );

    LoadPreferences(hwnd);
    ShowWindow(hwnd, nCmdShow == 0 ? SW_SHOWDEFAULT : nCmdShow);
    UpdateWindow(hwnd);
    ApplyAlwaysOnTop(hwnd, g_app.alwaysOnTop);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    g_uiFont = LoadUiFont(io);
    io.FontDefault = g_uiFont;
    g_monoFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\consola.ttf", 13.0f);

    ApplyAnyaTheme();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_device, g_deviceContext);

    g_app.streamer.SetLocalizedResults(En(Text::Sent), En(Text::Failed), En(Text::SocketErrorReason), En(Text::ReleaseReason));
    // Do not stream until the disclaimer has been accepted.
    if (g_app.disclaimerAccepted) {
        g_app.streamer.Start(g_app.frame);
    }

    bool done = false;
    while (!done) {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) {
                done = true;
            }
        }
        if (done) {
            break;
        }

        if (g_app.disclaimerAccepted) {
            HandleKeyboard(hwnd);
            UpdateCapture(hwnd);
            PollDanceExport();
            UpdateDancePlayback();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        if (g_app.disclaimerAccepted) {
            if (g_app.uiMode == UiMode::Mini) {
                RenderMiniUi(hwnd);
            } else {
                RenderUi(hwnd);
            }
        } else {
            switch (RenderDisclaimerGate()) {
            case DisclaimerAction::Accept:
                g_app.disclaimerAccepted = true;
                SavePreferences(hwnd);
                g_app.streamer.Start(g_app.frame);
                break;
            case DisclaimerAction::Quit:
                done = true;
                break;
            case DisclaimerAction::None:
                break;
            }
        }

        ImGui::Render();

        const float clearColor[4] = {0.08f, 0.09f, 0.10f, 1.00f};
        g_deviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_deviceContext->ClearRenderTargetView(g_mainRenderTargetView, clearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swapChain->Present(1, 0);

        ApplyPendingUiMode(hwnd);
    }

    ReleaseMouseCapture();
    SavePreferences(hwnd);
    g_app.streamer.Stop();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(kWindowClassName, hInstance);

    if (SUCCEEDED(comInit)) {
        CoUninitialize();
    }

    return 0;
}
