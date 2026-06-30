#pragma once

#include <cstddef>

namespace anyadance::ui {

// Languages are columns in the localization table. Add one here and add a
// matching column entry to every row in localization.cpp.
enum class Language : std::size_t {
    English = 0,
    ChineseSimplified,
    Count,
};

// UI strings are rows in the localization table. Add one here and add a
// matching row (all translations) in localization.cpp. The Device* entries
// must stay contiguous and ordered like the device slots.
enum class Text : std::size_t {
    Reset = 0,
    UdpLog,
    LogScrollLatest,
    Clear,
    Copy,
    CopyCommand,
    Resend,
    ResendReason,
    Time,
    Reason,
    Result,
    Sent,
    Failed,
    ResetReason,
    ManipulatedReason,
    KeyboardReason,
    ReleaseReason,
    SocketErrorReason,
    LanguageLabel,
    YMax,
    Capture,
    HmdHelp,
    KeyLine1,
    KeyLine2,
    MouseHelp,
    Mirror,
    FrameLabel,
    FrameHmd,
    FrameGlobal,
    UiModeLabel,
    UiModeFull,
    UiModeMini,
    AlwaysOnTop,
    RegisterDriver,
    UnregisterDriver,
    RestartSteamVr,
    Cancel,
    RestartConfirmBody,
    DriverStatusReady,
    StatusRegistered,
    StatusUnregistered,
    StatusManifestMissing,
    StatusDriverDllMissing,
    StatusOpenvrPathsMissing,
    StatusConfigWriteFailed,
    StatusRestarting,
    StatusRestartFailed,
    StatusFailed,
    DeviceHmd,
    DeviceLeftController,
    DeviceRightController,
    DeviceHip,
    DeviceLeftFoot,
    DeviceRightFoot,
    DisclaimerTitle,
    DisclaimerBody,
    DisclaimerAccept,
    DisclaimerQuit,
    DanceOpen,
    DanceTitle,
    DanceVmd,
    DanceModel,
    DanceBrowse,
    DanceLoop,
    DanceAnalyze,
    DancePlay,
    DancePause,
    DanceResume,
    DanceStop,
    DanceClose,
    DanceConverting,
    DanceHelp,
    DanceExperimental,
    DanceTimeline,
    DanceReason,
    DancePlaying,
    DanceAdvanced,
    DanceBlenderPath,
    DanceMmdToolsPath,
    DanceSaveNya,
    DanceLoadNya,
    PoseSave,
    PoseLoad,
    PoseStanding,
    PoseMenu,
    Count,
};

struct LanguageInfo {
    const char* code;         // persisted in preferences, e.g. "en-US"
    const char* displayName;  // shown in the language picker
};

constexpr std::size_t kLanguageCount = static_cast<std::size_t>(Language::Count);
constexpr std::size_t kTextCount = static_cast<std::size_t>(Text::Count);

// Translation lookup. Tr(id) uses the current language.
const char* Tr(Text id, Language language);
const char* Tr(Text id);

// Device display name by slot index (0..5), mapped onto the Device* rows.
const char* DeviceName(std::size_t slot, Language language);
const char* DeviceName(std::size_t slot);

Language CurrentLanguage();
void SetCurrentLanguage(Language language);
const LanguageInfo& GetLanguageInfo(Language language);
Language FindLanguageByCode(const char* code, Language fallback);

} // namespace anyadance::ui
