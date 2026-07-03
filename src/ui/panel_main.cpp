#include "ui/ui_state.h"
#include "ui/layout.h"

#include <algorithm>
#include <string>

namespace anyadance::ui {
namespace {

constexpr ImGuiWindowFlags kRootWindowFlags =
    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
static_assert((kRootWindowFlags & ImGuiWindowFlags_NoScrollbar) != 0);
static_assert((kRootWindowFlags & ImGuiWindowFlags_NoScrollWithMouse) != 0);

float MainFooterHeight() {
    const ImGuiStyle& style = ImGui::GetStyle();
    return MainFooterHeightForMetrics(
        ImGui::GetFontSize(), style.FramePadding.y, style.ItemSpacing.y, style.WindowPadding.y);
}

} // namespace

float DrawFooterBanner(const ImVec2& footerMin, const ImVec2& footerSize) {
    if (!g_footerBannerTexture || g_footerBannerWidth <= 0 || g_footerBannerHeight <= 0 || footerSize.x <= 0.0f || footerSize.y <= 0.0f) {
        return footerMin.x + footerSize.x;
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const float imageAspect = static_cast<float>(g_footerBannerWidth) / static_cast<float>(g_footerBannerHeight);
    const float drawHeight = footerSize.y;
    const float drawWidth = drawHeight * imageAspect;
    const ImVec2 imageMin(footerMin.x + footerSize.x - drawWidth, footerMin.y);
    const ImVec2 imageMax(footerMin.x + footerSize.x, footerMin.y + drawHeight);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(footerMin, ImVec2(footerMin.x + footerSize.x, footerMin.y + footerSize.y), true);
    draw->AddImage(
        reinterpret_cast<ImTextureID>(g_footerBannerTexture),
        imageMin,
        imageMax,
        ImVec2(0.0f, 0.0f),
        ImVec2(1.0f, 1.0f),
        IM_COL32(255, 255, 255, 120));
    draw->PopClipRect();
    return imageMin.x - style.ItemSpacing.x;
}

void ApplyAlwaysOnTop(HWND hwnd, bool onTop) {
    SetWindowPos(hwnd, onTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void ApplyUiMode(HWND hwnd, UiMode mode) {
    g_app.uiMode = mode;
    g_app.pendingUiMode = mode;
    g_app.uiModeChangePending = false;
    RECT rect{};
    if (GetWindowRect(hwnd, &rect)) {
        const int currentW = rect.right - rect.left;
        const int currentH = rect.bottom - rect.top;
        const int minClientW = MinClientWidthForMode(mode);
        const int minClientH = MinClientHeightForMode(mode);
        const SIZE minWindow = OuterWindowSizeForClient(hwnd, minClientW, minClientH);
        const int minWindowW = static_cast<int>(minWindow.cx);
        const int minWindowH = static_cast<int>(minWindow.cy);
        const int targetW = mode == UiMode::Mini ? minWindowW : std::max(currentW, minWindowW);
        const int targetH = mode == UiMode::Mini ? minWindowH : std::max(currentH, minWindowH);
        SetWindowPos(hwnd, nullptr, rect.left, rect.top, targetW, targetH,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
}

void SetUiMode(UiMode mode) {
    if (g_app.uiMode == mode || (g_app.uiModeChangePending && g_app.pendingUiMode == mode)) {
        return;
    }
    g_app.pendingUiMode = mode;
    g_app.uiModeChangePending = true;
}

void ApplyPendingUiMode(HWND hwnd) {
    if (g_app.uiModeChangePending) {
        ApplyUiMode(hwnd, g_app.pendingUiMode);
    }
}

void RenderUiModeControls(HWND hwnd, bool includeAlwaysOnTop) {
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s:", Tr(Text::UiModeLabel));
    ImGui::SameLine();
    if (ImGui::RadioButton(Tr(Text::UiModeFull), g_app.uiMode == UiMode::Full)) {
        SetUiMode(UiMode::Full);
    }
    ImGui::SameLine();
    if (ImGui::RadioButton(Tr(Text::UiModeMini), g_app.uiMode == UiMode::Mini)) {
        SetUiMode(UiMode::Mini);
    }
    if (includeAlwaysOnTop) {
        ImGui::SameLine();
        if (ImGui::Checkbox(Tr(Text::AlwaysOnTop), &g_app.alwaysOnTop)) {
            ApplyAlwaysOnTop(hwnd, g_app.alwaysOnTop);
        }
    }
}

// Map a driver registration/restart outcome to its localized status string.
Text DriverStatusText(DriverStatus status) {
    switch (status) {
    case DriverStatus::Registered: return Text::StatusRegistered;
    case DriverStatus::Unregistered: return Text::StatusUnregistered;
    case DriverStatus::ManifestMissing: return Text::StatusManifestMissing;
    case DriverStatus::DriverDllMissing: return Text::StatusDriverDllMissing;
    case DriverStatus::OpenvrPathsMissing: return Text::StatusOpenvrPathsMissing;
    case DriverStatus::ConfigWriteFailed: return Text::StatusConfigWriteFailed;
    case DriverStatus::Restarting: return Text::StatusRestarting;
    case DriverStatus::RestartFailed: return Text::StatusRestartFailed;
    case DriverStatus::Failed: break;
    }
    return Text::StatusFailed;
}

// Build the whole single-page UI for one frame: Reset, driver controls and
// status, the body + log panels, and the language/help footer.
void RenderUi(HWND hwnd) {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("AnyaDance", nullptr, kRootWindowFlags);

    RenderUiModeControls(hwnd, true);
    ImGui::Separator();

    // Pose row: Standing Pose | Reset to T-Pose | Menu Pose. Reset rebuilds the
    // T-pose and stops any MMD playback; the two presets apply through RestorePose
    // (which also stops playback) and leave finger bends untouched.
    const float poseButtonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
    // Posed presets use the primary tint; Reset (return to the neutral T-pose) uses
    // the secondary tint so it reads apart from the two stances on either side.
    {
        ScopedButtonColor tint(col::Primary);
        if (ImGui::Button(Tr(Text::PoseStanding), ImVec2(poseButtonWidth, 44))) {
            RestorePose(MakeStandingPose(), En(Text::PoseStanding));
        }
    }
    ImGui::SameLine();
    {
        ScopedButtonColor tint(col::Secondary);
        if (ImGui::Button(Tr(Text::Reset), ImVec2(poseButtonWidth, 44))) {
            StopDanceToTPose();  // a manual reset also stops any MMD playback
            g_app.keyboard.Neutralize();
            g_app.fingerBends[0] = FingerBends{};
            g_app.fingerBends[1] = FingerBends{};
            g_app.streamer.UpdateFrame(g_app.frame, En(Text::ResetReason), false);
        }
    }
    ImGui::SameLine();
    {
        ScopedButtonColor tint(col::Primary);
        if (ImGui::Button(Tr(Text::PoseMenu), ImVec2(poseButtonWidth, 44))) {
            RestorePose(MakeMenuPose(), En(Text::PoseMenu));
        }
    }

    const auto recordStatus = [](const DriverActionResult& result) {
        g_app.driverStatusSet = true;
        g_app.driverStatus = result.status;
        g_app.driverStatusDetail = result.detail;
    };

    bool restartConfirmRequested = false;
    if (ImGui::BeginTable("main_controls", 2, ImGuiTableFlags_SizingStretchSame)) {
        // Save the current pose (device poses + finger bends) to a .nya file, or
        // load one back. A pose is a one-frame clip, so it shares the format with
        // dances.
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        {
            ScopedButtonColor tint(col::Primary);  // Save = primary (writes out)
            if (ImGui::Button(Tr(Text::PoseSave), ImVec2(-1.0f, 0.0f))) {
                const std::string path = SaveFileDialog(hwnd, Tr(Text::PoseSave), "AnyaDance (*.nya)", "*.nya", "nya");
                if (!path.empty() && !WriteFileUtf8(path, SerializeNya(MakePoseClip(FrameWithCurrentFingerBends())))) {
                    MessageBoxA(hwnd, "Could not write the .nya file.", "AnyaDance", MB_OK | MB_ICONWARNING);
                }
            }
        }
        ImGui::TableSetColumnIndex(1);
        {
            ScopedButtonColor tint(col::Secondary);  // Load = secondary (reads in)
            if (ImGui::Button(Tr(Text::PoseLoad), ImVec2(-1.0f, 0.0f))) {
                const std::string path = OpenFileDialog(hwnd, Tr(Text::PoseLoad), "AnyaDance (*.nya)", "*.nya");
                if (!path.empty()) {
                    NyaClip clip;
                    std::string error;
                    if (ParseNya(ReadFileUtf8(path), clip, error) && !clip.motion.frames.empty()) {
                        RestorePose(clip.motion.frames.front());
                    } else {
                        MessageBoxA(hwnd, ("Load failed: " + error).c_str(), "AnyaDance", MB_OK | MB_ICONWARNING);
                    }
                }
            }
        }

        // Driver/system controls stay in one compact row in the left column; the
        // MMD dance entry point starts the right column below Load Pose.
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        const float systemButtonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
        // Driver controls: Register (primary), Unregister (secondary), and the
        // disruptive Restart (tertiary).
        {
            ScopedButtonColor tint(col::Primary);
            if (ImGui::Button(Tr(Text::RegisterDriver), ImVec2(systemButtonWidth, 0.0f))) {
                recordStatus(RegisterDriver());
            }
        }
        ImGui::SameLine();
        {
            ScopedButtonColor tint(col::Secondary);
            if (ImGui::Button(Tr(Text::UnregisterDriver), ImVec2(systemButtonWidth, 0.0f))) {
                const DriverActionResult result = UnregisterDriver();
                recordStatus(result);
                if (result.ok) {
                    restartConfirmRequested = true;
                }
            }
        }
        ImGui::SameLine();
        {
            ScopedButtonColor tint(col::Tertiary);
            if (ImGui::Button(Tr(Text::RestartSteamVr), ImVec2(systemButtonWidth, 0.0f))) {
                restartConfirmRequested = true;
            }
        }
        ImGui::TableSetColumnIndex(1);
        {
            // Dance is a primary feature entry point.
            ScopedButtonColor tint(col::Primary);
            if (ImGui::Button(Tr(Text::DanceOpen), ImVec2(-1.0f, 0.0f))) {
                g_app.danceDialogOpen = true;
            }
        }
        ImGui::EndTable();
    }

    if (restartConfirmRequested) {
        ImGui::OpenPopup("restart_confirm");
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("restart_confirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(440.0f);
        ImGui::TextUnformatted(Tr(Text::RestartConfirmBody));
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        {
            ScopedButtonColor tint(col::Tertiary);
            if (ImGui::Button(Tr(Text::RestartSteamVr), ImVec2(160.0f, 0.0f))) {
                recordStatus(RestartSteamVR());
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        {
            ScopedButtonColor tint(col::Secondary);
            if (ImGui::Button(Tr(Text::Cancel), ImVec2(120.0f, 0.0f))) {
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    if (g_app.driverStatusSet) {
        std::string status = Tr(DriverStatusText(g_app.driverStatus));
        if (!g_app.driverStatusDetail.empty()) {
            status += " (" + g_app.driverStatusDetail + ")";
        }
        ImGui::TextWrapped("%s", status.c_str());
    } else {
        ImGui::TextWrapped("%s", Tr(Text::DriverStatusReady));
    }

    ImGui::Separator();
    const float footerHeight = MainFooterHeight();
    ImGui::BeginChild("main", ImVec2(0, -footerHeight), false);
    RenderBodyPanel(hwnd);
    ImGui::SameLine();
    RenderLogPanel();
    ImGui::EndChild();

    ImGui::Separator();
    const ImVec2 footerMin = ImGui::GetCursorScreenPos();
    const ImVec2 footerSize = ImGui::GetContentRegionAvail();
    const float footerTextMaxX = DrawFooterBanner(footerMin, footerSize);
    ImGui::PushTextWrapPos(footerTextMaxX);
    ImGui::Text("%s: ", Tr(Text::LanguageLabel));
    for (std::size_t i = 0; i < kLanguageCount; ++i) {
        const Language language = static_cast<Language>(i);
        ImGui::SameLine();
        if (ImGui::RadioButton(GetLanguageInfo(language).displayName, CurrentLanguage() == language)) {
            SetCurrentLanguage(language);
            g_app.streamer.SetLocalizedResults(En(Text::Sent), En(Text::Failed), En(Text::SocketErrorReason), En(Text::ReleaseReason));
        }
    }
    ImGui::TextUnformatted(Tr(Text::KeyLine1));
    ImGui::TextUnformatted(Tr(Text::KeyLine2));
    ImGui::PopTextWrapPos();

    RenderDanceDialog(hwnd);

    ImGui::End();
}

void RenderMiniUi(HWND hwnd) {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("AnyaDance", nullptr, kRootWindowFlags);

    RenderUiModeControls(hwnd, true);
    ImGui::Separator();
    RenderBodyPanel(hwnd, true);

    ImGui::End();
}

} // namespace anyadance::ui
