#include "ui/ui_state.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <future>
#include <string>

namespace anyadance::ui {

// Kick off the Blender solve on a worker thread so the UI keeps rendering while
// Blender runs. The result is picked up by PollDanceExport.
void StartDanceExport() {
    if (g_app.danceConverting) {
        return;
    }
    MmdDanceConfig config;
    config.vmdPath = g_app.danceVmdPath;
    config.modelPath = g_app.danceModelPath;
    config.blenderPath = g_app.danceBlenderPath;
    config.mmdToolsPath = g_app.danceMmdToolsPath;
    config.fps = g_app.danceFps;
    g_app.danceConverting = true;
    g_app.danceStatus = Tr(Text::DanceConverting);
    g_app.danceFuture = std::async(std::launch::async, [config] { return RunMmdExport(config); });
}

DanceTransportRange DanceTransport() {
    return BuildDanceTransportRange(
        g_app.danceMotion.valid ? g_app.danceMotion.duration : 0.0,
        g_app.danceAudio.HasAudio(),
        g_app.danceAudio.DurationSeconds(),
        g_app.danceAudioOffset);
}

float ClampDanceElapsed(float elapsed) {
    if (!g_app.danceMotion.valid || g_app.danceMotion.duration <= 0.0f) {
        return 0.0f;
    }
    return static_cast<float>(NormalizeDanceTransportTime(elapsed, DanceTransport(), false));
}

float DanceTimelineElapsed() {
    if (!g_app.danceMotion.valid || g_app.danceMotion.duration <= 0.0f) {
        return 0.0f;
    }
    if (g_app.dancePaused || !g_app.dancePlaying) {
        return ClampDanceElapsed(g_app.dancePausedElapsed);
    }
    const auto now = std::chrono::steady_clock::now();
    const float elapsed =
        std::chrono::duration<float>(now - g_app.danceStartTime).count() * g_app.danceSpeed;
    return static_cast<float>(NormalizeDanceTransportTime(
        elapsed, DanceTransport(), g_app.danceLoop));
}

void SetDanceStartForElapsed(float elapsed) {
    const float speed = g_app.danceSpeed != 0.0f ? g_app.danceSpeed : 1.0f;
    const auto offset = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<float>(elapsed / speed));
    g_app.danceStartTime = std::chrono::steady_clock::now() - offset;
}

void ApplyDanceFrameAt(float transportElapsed) {
    if (!g_app.danceMotion.valid) {
        return;
    }
    const float motionElapsed = static_cast<float>(MotionTimeForDanceTransport(
        transportElapsed, g_app.danceMotion.duration));
    // The combined transport owns looping. Motion sampling clamps so an audio
    // intro/outro holds the first/last pose instead of independently looping.
    FrameState danceFrame = SampleDanceMotion(g_app.danceMotion, motionElapsed, false);
    AnchorDanceFrame(danceFrame, g_app.danceRootX, g_app.danceRootZ);

    const std::array<ControllerState, 2> danceControllers = danceFrame.controllers;
    // Keep keyboard-driven inputs (joystick turn, triggers) set this frame by
    // HandleKeyboard; the dance only owns poses and finger bends. Mirroring the
    // dance fingers into g_app.fingerBends keeps that store the single source of
    // truth, so Save Pose and the paused stream capture the dance's hands instead
    // of the stale wheel value.
    danceFrame.controllers = g_app.frame.controllers;
    ApplyDanceFingerBends(danceControllers, danceFrame.controllers, g_app.fingerBends);
    ClampFrameY(danceFrame);
    g_app.frame = danceFrame;
    g_app.streamer.UpdateFrame(g_app.frame);  // no reason: keep the 60 Hz log quiet
}

void SeekDancePlayback(float elapsed) {
    if (!g_app.danceMotion.valid) {
        return;
    }
    elapsed = ClampDanceElapsed(elapsed);
    g_app.dancePausedElapsed = elapsed;
    if (g_app.dancePlaying && !g_app.dancePaused) {
        SetDanceStartForElapsed(elapsed);
    }
    // Root anchor is owned by StartDancePlayback; seeking never changes it.
    // Re-rooting here from g_app.frame would accumulate because the frame already
    // has the previous root baked in via AnchorDanceFrame.
    ApplyDanceFrameAt(elapsed);
    const DanceAudioPosition audio = AudioPositionForDanceTransport(
        elapsed, g_app.danceAudio.DurationSeconds(), g_app.danceAudioOffset);
    g_app.danceAudio.Synchronize(
        audio.seconds, g_app.dancePlaying && !g_app.dancePaused && audio.active);
}

void StartDancePlayback() {
    if (!g_app.danceMotion.valid) {
        return;
    }
    const DanceTransportRange range = DanceTransport();
    float startElapsed = ClampDanceElapsed(g_app.dancePausedElapsed);
    if (!g_app.danceLoop && startElapsed >= static_cast<float>(range.end) - 0.0001f) {
        startElapsed = static_cast<float>(range.start);
        g_app.dancePausedElapsed = startElapsed;
    }
    g_app.dancePlaying = true;
    g_app.dancePaused = false;
    SetDanceStartForElapsed(startElapsed);
    // Anchor the dance where the HMD currently stands so the avatar dances in place.
    const DeviceState& hmd = g_app.frame.devices[DeviceSlot(DeviceIndex::Hmd)];
    g_app.danceRootX = hmd.position.x;
    g_app.danceRootZ = hmd.position.z;
    ApplyDanceFrameAt(startElapsed);
}

// Freeze playback on the current pose. The elapsed offset is remembered so Resume
// can pick the clock back up from exactly here.
void PauseDancePlayback() {
    if (!g_app.dancePlaying || g_app.dancePaused) {
        return;
    }
    g_app.dancePausedElapsed = DanceTimelineElapsed();
    g_app.dancePaused = true;
    g_app.danceAudio.StopPlayback();
}

// Re-anchor danceStartTime so UpdateDancePlayback continues from the paused offset,
// honoring the current speed.
void ResumeDancePlayback() {
    if (!g_app.dancePlaying || !g_app.dancePaused) {
        return;
    }
    SetDanceStartForElapsed(ClampDanceElapsed(g_app.dancePausedElapsed));
    g_app.dancePaused = false;
}

// Stop any dance playback and rebuild the canonical T-pose. BuildResetTPose anchors
// the rig at tracking-space origin, so the HMD's drifted dance position is simply
// discarded. Callers stream the resulting frame themselves.
void StopDanceToTPose() {
    g_app.dancePlaying = false;
    g_app.dancePaused = false;
    g_app.dancePausedElapsed = static_cast<float>(DanceTransport().start);
    g_app.danceAudio.StopPlayback();
    g_app.frame = BuildResetTPose(g_app.frame);
}

// Stopping a dance settles the rig into the standing idle pose (arms at the sides)
// rather than the T-pose; the explicit Reset button is the way back to the T-pose.
void StopDancePlayback() {
    if (!g_app.dancePlaying) {
        return;
    }
    g_app.dancePlaying = false;
    g_app.dancePaused = false;
    g_app.dancePausedElapsed = static_cast<float>(DanceTransport().start);
    g_app.danceAudio.StopPlayback();
    g_app.frame = MakeStandingPose();
    g_app.streamer.UpdateFrame(g_app.frame, En(Text::PoseStanding), false);
}

// Apply a loaded .nya frame as the live pose. Device poses persist in g_app.frame
// (only mouse/reset/dance move them), while finger bends go through g_app.fingerBends
// so the per-frame ApplyFingerBend keeps them instead of overwriting with the wheel
// state. A restored pose takes over from any in-progress dance playback.
void RestorePose(const FrameState& pose, const char* reason) {
    g_app.dancePlaying = false;
    g_app.dancePaused = false;
    g_app.dancePausedElapsed = static_cast<float>(DanceTransport().start);
    g_app.danceAudio.StopPlayback();
    for (std::size_t d = 0; d < g_app.frame.devices.size(); ++d) {
        g_app.frame.devices[d].position = pose.devices[d].position;
        g_app.frame.devices[d].rotation = pose.devices[d].rotation;
        g_app.frame.devices[d].valid = true;
        g_app.frame.devices[d].connected = true;
    }
    for (std::size_t i = 0; i < 2; ++i) {
        if (pose.controllers[i].has_finger_bends) {
            g_app.fingerBends[i] = pose.controllers[i].finger_bends;
        }
    }
    g_app.streamer.UpdateFrame(FrameWithCurrentFingerBends(), reason, false);
}

// Pick up a finished Blender solve, retarget it, and report the outcome. Runs
// once per frame from the main loop.
void PollDanceExport() {
    if (!g_app.danceConverting || !g_app.danceFuture.valid()) {
        return;
    }
    if (g_app.danceFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }
    const MmdExportResult result = g_app.danceFuture.get();
    g_app.danceConverting = false;

    if (!result.ok) {
        g_app.danceStatus = result.message;
        return;
    }
    SolvedMotion motion;
    std::string error;
    if (!ParseSolvedMotion(result.solvedJson, motion, error)) {
        g_app.danceStatus = "Parse failed: " + error;
        return;
    }
    MmdRetargetParams params;
    params.targetHeightM = g_app.danceHeight;
    params.handReachScale = g_app.danceHandReach;
    g_app.danceMotion = BuildDanceMotion(motion, params);
    if (!g_app.danceMotion.valid) {
        g_app.danceStatus = "Retarget failed.";
        return;
    }
    g_app.dancePlaying = false;
    g_app.dancePaused = false;
    g_app.dancePausedElapsed = static_cast<float>(DanceTransport().start);
    g_app.danceAudio.StopPlayback();
    char buf[160];
    std::snprintf(buf, sizeof(buf), "Ready: %.1fs, %zu frames, fingers %s, scale %.2f",
                  g_app.danceMotion.duration,
                  g_app.danceMotion.frames.size(),
                  g_app.danceMotion.hasFingers ? "yes" : "no",
                  g_app.danceMotion.scale);
    g_app.danceStatus = buf;
}

// Drive one frame of MMD playback: sample the dance pose, anchor it in place,
// keep the live joystick/keyboard inputs (turning stays on the joystick), and
// re-apply the dance's finger bends on top.
void UpdateDancePlayback() {
    if (!g_app.dancePlaying || g_app.dancePaused || !g_app.danceMotion.valid) {
        return;  // paused playback holds the last streamed pose in place
    }
    const float elapsed = DanceTimelineElapsed();
    g_app.dancePausedElapsed = elapsed;
    ApplyDanceFrameAt(elapsed);
    const DanceAudioPosition audio = AudioPositionForDanceTransport(
        elapsed, g_app.danceAudio.DurationSeconds(), g_app.danceAudioOffset);
    g_app.danceAudio.Synchronize(audio.seconds, audio.active);
    const DanceTransportRange range = DanceTransport();
    if (!g_app.danceLoop && elapsed >= static_cast<float>(range.end)) {
        // Leave the final pose applied, but finish the transport so Play can
        // restart the combined motion/audio interval from its beginning.
        g_app.dancePausedElapsed = static_cast<float>(range.end);
        g_app.dancePlaying = false;
        g_app.dancePaused = false;
        g_app.danceAudio.StopPlayback();
    }
}

// File-picker helpers for the dance dialog; fill the target buffer on success.
void BrowseInto(HWND hwnd, char* buffer, std::size_t size, Text label, const char* pattern) {
    const std::string picked = OpenFileDialog(hwnd, Tr(label), Tr(label), pattern);
    if (!picked.empty()) {
        std::snprintf(buffer, size, "%s", picked.c_str());
    }
}

void RestartDanceTransportAtBeginning() {
    g_app.danceAudio.StopPlayback();
    g_app.dancePlaying = false;
    g_app.dancePaused = false;
    g_app.dancePausedElapsed = static_cast<float>(DanceTransport().start);
    if (g_app.danceMotion.valid) {
        ApplyDanceFrameAt(g_app.dancePausedElapsed);
    }
}

void PreserveDanceTransport(float elapsed) {
    g_app.danceAudio.StopPlayback();
    g_app.dancePausedElapsed = ClampDanceElapsed(elapsed);
    if (g_app.dancePlaying && !g_app.dancePaused) {
        SetDanceStartForElapsed(g_app.dancePausedElapsed);
    }
    if (g_app.danceMotion.valid) {
        ApplyDanceFrameAt(g_app.dancePausedElapsed);
    }
}

// The MMD dance dialog: choose a VMD + model, verify (solve), and play. Blender
// does the FK/IK solve; the remap onto the hardcoded rig happens in-UI.
void RenderDanceDialog(HWND hwnd) {
    // OpenPopup and BeginPopupModal must use the exact same string: ImGui hashes
    // the "###id" suffix verbatim, so a localized label plus a fixed "###mmd_dance"
    // id keeps both calls in sync and survives a language switch.
    const std::string title = std::string(Tr(Text::DanceTitle)) + "###mmd_dance";
    if (g_app.danceDialogOpen) {
        ImGui::OpenPopup(title.c_str());
        g_app.danceDialogOpen = false;
        // Persisted advanced paths win over auto-detection. Empty fields mean the
        // user has not picked an override yet, so show the detected path as a
        // starting point.
        if (g_app.danceBlenderPath[0] == '\0') {
            CopyPreferenceString(g_app.danceBlenderPath, sizeof(g_app.danceBlenderPath), DetectBlenderExe());
        }
        if (g_app.danceMmdToolsPath[0] == '\0') {
            CopyPreferenceString(g_app.danceMmdToolsPath, sizeof(g_app.danceMmdToolsPath), DetectMmdToolsPath());
        }
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(640.0f, 0.0f), ImGuiCond_Appearing);
    // Passing a bool gives the modal a title-bar close (X) button; ImGui closes
    // the popup when it is clicked, so the dialog needs no explicit Close button.
    bool stayOpen = true;
    if (!ImGui::BeginPopupModal(title.c_str(), &stayOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::PushTextWrapPos(620.0f);
    ImGui::TextUnformatted(Tr(Text::DanceHelp));
    ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.34f, 1.0f), "%s", Tr(Text::DanceExperimental));
    ImGui::PopTextWrapPos();
    ImGui::Separator();

    // Each path row uses a fixed-width label column so every input box is the same
    // length and left-aligned, with a Browse button pinned to the right edge.
    const ImGuiStyle& style = ImGui::GetStyle();
    const float browseWidth = ImGui::CalcTextSize(Tr(Text::DanceBrowse)).x + style.FramePadding.x * 2.0f;
    float labelColumn = 0.0f;
    for (Text label : {Text::DanceVmd, Text::DanceModel, Text::DanceAudio,
                       Text::DanceBlenderPath, Text::DanceMmdToolsPath}) {
        labelColumn = std::max(labelColumn, ImGui::CalcTextSize(Tr(label)).x);
    }
    auto pathRow = [&](const char* id, Text label, char* buffer, std::size_t size, auto&& onBrowse) {
        const float rowStart = ImGui::GetCursorPosX();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(Tr(label));
        ImGui::SameLine();
        ImGui::SetCursorPosX(rowStart + labelColumn + style.ItemSpacing.x);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - browseWidth - style.ItemSpacing.x);
        ImGui::InputText((std::string("##") + id).c_str(), buffer, size);
        ImGui::SameLine();
        ImGui::PushID(id);
        if (ImGui::Button(Tr(Text::DanceBrowse), ImVec2(browseWidth, 0.0f))) {
            onBrowse();
        }
        ImGui::PopID();
    };

    pathRow("vmd", Text::DanceVmd, g_app.danceVmdPath, sizeof(g_app.danceVmdPath), [&] {
        BrowseInto(hwnd, g_app.danceVmdPath, sizeof(g_app.danceVmdPath), Text::DanceVmd, "*.vmd");
    });
    pathRow("model", Text::DanceModel, g_app.danceModelPath, sizeof(g_app.danceModelPath), [&] {
        BrowseInto(hwnd, g_app.danceModelPath, sizeof(g_app.danceModelPath), Text::DanceModel, "*.pmx;*.pmd");
    });
    pathRow("audio", Text::DanceAudio, g_app.danceAudioPath, sizeof(g_app.danceAudioPath), [&] {
        const std::string picked = OpenFileDialog(
            hwnd, Tr(Text::DanceAudio), Tr(Text::DanceAudio), "*.wav;*.mp3;*.m4a;*.aac;*.wma");
        if (picked.empty()) {
            return;
        }
        std::string error;
        if (g_app.danceAudio.Load(picked, error)) {
            CopyPreferenceString(g_app.danceAudioPath, sizeof(g_app.danceAudioPath), picked);
            RestartDanceTransportAtBeginning();
            char status[256] = {};
            std::snprintf(status, sizeof(status), Tr(Text::DanceAudioLoaded),
                          g_app.danceAudio.DurationSeconds());
            g_app.danceStatus = status;
        } else {
            char status[768] = {};
            std::snprintf(status, sizeof(status), Tr(Text::DanceAudioLoadFailed), error.c_str());
            g_app.danceStatus = status;
        }
    });

    const float audioControlX = ImGui::GetCursorPosX() + labelColumn + style.ItemSpacing.x;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(Tr(Text::DanceAudioOffset));
    ImGui::SameLine();
    const float elapsedBeforeOffsetEdit = DanceTimelineElapsed();
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::InputFloat("##dance_audio_offset", &g_app.danceAudioOffset, 0.01f, 0.10f, "%.3f")) {
        g_app.danceAudioOffset = std::clamp(g_app.danceAudioOffset, -3600.0f, 3600.0f);
        if (g_app.dancePlaying) {
            PreserveDanceTransport(elapsedBeforeOffsetEdit);
        } else {
            RestartDanceTransportAtBeginning();
        }
    }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(Tr(Text::DanceAudioVolume));
    ImGui::SameLine();
    int volumePercent = static_cast<int>(std::lround(g_app.danceAudioVolume * 100.0f));
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderInt("##dance_audio_volume", &volumePercent, 0, 100, "%d%%")) {
        g_app.danceAudioVolume = static_cast<float>(volumePercent) / 100.0f;
        g_app.danceAudio.SetVolume(g_app.danceAudioVolume);
    }
    ImGui::SetCursorPosX(audioControlX);
    ImGui::PushTextWrapPos(620.0f);
    ImGui::TextDisabled("%s", Tr(Text::DanceAudioOffsetHelp));
    ImGui::PopTextWrapPos();

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(Tr(Text::DanceAudioOutput));
    ImGui::SameLine();
    ImGui::SetCursorPosX(audioControlX);
    const auto& devices = g_app.danceAudio.OutputDevices();
    const int selectedDevice = g_app.danceAudio.SelectedOutputDevice();
    const char* selectedDeviceName =
        selectedDevice >= 0 && selectedDevice < static_cast<int>(devices.size())
            ? devices[static_cast<std::size_t>(selectedDevice)].name.c_str()
            : "--";
    const float refreshWidth = ImGui::CalcTextSize(Tr(Text::DanceAudioRefresh)).x +
        style.FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - refreshWidth - style.ItemSpacing.x);
    if (ImGui::BeginCombo("##dance_audio_output", selectedDeviceName)) {
        for (std::size_t i = 0; i < devices.size(); ++i) {
            const bool selected = static_cast<int>(i) == selectedDevice;
            if (ImGui::Selectable(devices[i].name.c_str(), selected)) {
                std::string error;
                if (g_app.danceAudio.SelectOutputDevice(static_cast<int>(i), error)) {
                    g_app.danceAudioOutputDeviceId = devices[i].id;
                } else {
                    g_app.danceStatus = error;
                }
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button(Tr(Text::DanceAudioRefresh), ImVec2(refreshWidth, 0.0f))) {
        const std::string previousId =
            selectedDevice >= 0 && selectedDevice < static_cast<int>(devices.size())
                ? devices[static_cast<std::size_t>(selectedDevice)].id
                : g_app.danceAudioOutputDeviceId;
        std::string error;
        if (!g_app.danceAudio.RefreshOutputDevices(error)) {
            g_app.danceStatus = error;
        } else if (!previousId.empty() &&
                   g_app.danceAudio.SelectOutputDeviceById(previousId, error)) {
            g_app.danceAudioOutputDeviceId = previousId;
        } else if (!g_app.danceAudio.OutputDevices().empty() &&
                   g_app.danceAudio.SelectOutputDevice(0, error)) {
            g_app.danceAudioOutputDeviceId = g_app.danceAudio.OutputDevices()[0].id;
        } else if (!error.empty()) {
            g_app.danceStatus = error;
        }
    }

    if (g_app.danceAudio.HasAudio()) {
        ImGui::SetCursorPosX(audioControlX);
        if (ImGui::Button(Tr(Text::DanceAudioRemove))) {
            const float currentElapsed = DanceTimelineElapsed();
            g_app.danceAudio.Unload();
            g_app.danceAudioPath[0] = '\0';
            PreserveDanceTransport(currentElapsed);
        }
    }

    // Analyze runs the Blender solve and lives right under the inputs it consumes.
    ImGui::BeginDisabled(g_app.danceConverting);
    {
        ScopedButtonColor tint(col::Primary);
        if (ImGui::Button(Tr(Text::DanceAnalyze), ImVec2(-1.0f, 0.0f))) {
            StartDanceExport();
        }
    }
    ImGui::EndDisabled();

    // Advanced: only needed if Blender / MMD Tools were not auto-detected. The
    // fields are pre-filled with the detected paths when the dialog opens.
    if (ImGui::CollapsingHeader(Tr(Text::DanceAdvanced))) {
        pathRow("blender", Text::DanceBlenderPath, g_app.danceBlenderPath, sizeof(g_app.danceBlenderPath), [&] {
            BrowseInto(hwnd, g_app.danceBlenderPath, sizeof(g_app.danceBlenderPath), Text::DanceBlenderPath, "*.exe");
        });
        pathRow("mmdtools", Text::DanceMmdToolsPath, g_app.danceMmdToolsPath, sizeof(g_app.danceMmdToolsPath), [&] {
            const std::string picked = OpenFolderDialog(hwnd, Tr(Text::DanceMmdToolsPath));
            if (!picked.empty()) {
                std::snprintf(g_app.danceMmdToolsPath, sizeof(g_app.danceMmdToolsPath), "%s", picked.c_str());
            }
        });
    }

    ImGui::Separator();
    const DanceTransportRange transport = DanceTransport();
    const float duration = static_cast<float>(transport.end - transport.start);
    float timeline = DanceTimelineElapsed();
    ImGui::Text("%s: %.2fs (%.2f .. %.2f)", Tr(Text::DanceTimeline), timeline,
                transport.start, transport.end);
    ImGui::BeginDisabled(!g_app.danceMotion.valid || duration <= 0.0f);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("##dance_timeline", &timeline,
                           static_cast<float>(transport.start),
                           static_cast<float>(transport.end), "")) {
        SeekDancePlayback(timeline);
    }
    ImGui::EndDisabled();

    // Play stays disabled until a solve (or a loaded clip) is ready and (re)starts
    // from the top; Pause/Resume freezes and continues in place; Stop settles into
    // the standing pose; the Loop checkbox fills the last cell of the row. Play is
    // primary, Pause secondary, and Stop the tertiary (stop/destructive) tint.
    const float quadWidth = (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x * 3.0f) / 4.0f;
    ImGui::BeginDisabled(g_app.danceConverting || !g_app.danceMotion.valid);
    {
        ScopedButtonColor tint(col::Primary);
        if (ImGui::Button(Tr(Text::DancePlay), ImVec2(quadWidth, 0.0f))) {
            StartDancePlayback();
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!g_app.dancePlaying);
    {
        ScopedButtonColor tint(col::Secondary);
        if (ImGui::Button(g_app.dancePaused ? Tr(Text::DanceResume) : Tr(Text::DancePause),
                          ImVec2(quadWidth, 0.0f))) {
            if (g_app.dancePaused) {
                ResumeDancePlayback();
            } else {
                PauseDancePlayback();
            }
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!g_app.dancePlaying);
    {
        ScopedButtonColor tint(col::Tertiary);
        if (ImGui::Button(Tr(Text::DanceStop), ImVec2(quadWidth, 0.0f))) {
            StopDancePlayback();
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Checkbox(Tr(Text::DanceLoop), &g_app.danceLoop);

    // Save the analyzed motion as a .nya clip, or load one to play directly. A
    // loaded clip skips both Blender and the remap, so Play is ready immediately.
    const float halfWidth = (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) / 2.0f;
    // Save (primary) / Load (secondary), matching the pose Save/Load pair. Each tint
    // closes before EndPopup so the ImGui color stack stays balanced at the boundary.
    ImGui::BeginDisabled(!g_app.danceMotion.valid);
    {
        ScopedButtonColor tint(col::Primary);
        if (ImGui::Button(Tr(Text::DanceSaveNya), ImVec2(halfWidth, 0.0f))) {
            const std::string path = SaveFileDialog(hwnd, Tr(Text::DanceSaveNya), "AnyaDance (*.nya)", "*.nya", "nya");
            if (!path.empty()) {
                NyaClip clip = MakeAnimationClip(g_app.danceMotion, g_app.danceFps, g_app.danceModelPath);
                clip.loop = g_app.danceLoop;
                g_app.danceStatus = WriteFileUtf8(path, SerializeNya(clip))
                                        ? ("Saved " + path)
                                        : std::string("Could not write the .nya file.");
            }
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    {
        ScopedButtonColor tint(col::Secondary);
        if (ImGui::Button(Tr(Text::DanceLoadNya), ImVec2(halfWidth, 0.0f))) {
            const std::string path = OpenFileDialog(hwnd, Tr(Text::DanceLoadNya), "AnyaDance (*.nya)", "*.nya");
            if (!path.empty()) {
                NyaClip clip;
                std::string error;
                if (ParseNya(ReadFileUtf8(path), clip, error)) {
                    g_app.danceMotion = clip.motion;
                    g_app.danceLoop = clip.loop;
                    g_app.dancePlaying = false;
                    g_app.dancePaused = false;
                    g_app.dancePausedElapsed = static_cast<float>(DanceTransport().start);
                    g_app.danceAudio.StopPlayback();
                    char buf[160];
                    std::snprintf(buf, sizeof(buf), "Loaded %zu frames, %.1fs, fingers %s. Press Play.",
                                  clip.motion.frames.size(), clip.motion.duration,
                                  clip.motion.hasFingers ? "yes" : "no");
                    g_app.danceStatus = buf;
                } else {
                    g_app.danceStatus = "Load failed: " + error;
                }
            }
        }
    }

    if (!g_app.danceStatus.empty()) {
        ImGui::Spacing();
        ImGui::PushTextWrapPos(620.0f);
        ImGui::TextWrapped("%s", g_app.danceStatus.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::EndPopup();
}

} // namespace anyadance::ui
