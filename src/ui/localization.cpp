#include "ui/localization.h"

#include <cstring>

namespace anyadance::ui {
namespace {

// One row per Text, one column per Language. To add a string, add a row here
// (and a Text enum value); to add a language, add a column to every row (and a
// Language enum value plus a kLanguages entry). Translations for one string
// stay together on its row, keeping enum/table positions paired locally.
const char* const kStrings[kTextCount][kLanguageCount] = {
    /* Reset                 */ {"Reset to T-Pose", u8"重置为 T 姿势"},
    /* UdpLog                */ {"UDP Log", u8"UDP 日志"},
    /* LogScrollLatest       */ {"Scroll to latest", u8"滚动到最新"},
    /* Clear                 */ {"Clear", u8"清除"},
    /* Copy                  */ {"Copy", u8"复制"},
    /* CopyCommand           */ {"Copy resend command", u8"复制重发命令"},
    /* Resend                */ {"Resend", u8"重新发送"},
    /* ResendReason          */ {"Resend", u8"重新发送"},
    /* Time                  */ {"Time", u8"时间"},
    /* Reason                */ {"Reason", u8"原因"},
    /* Result                */ {"Result", u8"结果"},
    /* Sent                  */ {"Sent", u8"已发送"},
    /* Failed                */ {"Failed", u8"失败"},
    /* ResetReason           */ {"Reset to T-Pose", u8"重置为 T 姿势"},
    /* ManipulatedReason     */ {"Device manipulated", u8"设备已移动"},
    /* KeyboardReason        */ {"Input captured", u8"输入已捕获"},
    /* ReleaseReason         */ {"Input released", u8"输入已释放"},
    /* SocketErrorReason     */ {"Socket error", u8"套接字错误"},
    /* LanguageLabel         */ {"Language", u8"语言"},
    /* YMax                  */ {"Y MAX", "Y MAX"},
    /* Capture               */ {"Captured", u8"捕获中"},
    /* HmdHelp               */ {"HMD: rotate; LMB+RMB to move up/down", u8"头显：旋转；左键+右键上下移动"},
    /* KeyLine1              */ {"WASD Move | Q/E Turn | Space Jump | M Menu | V Voice", u8"WASD 移动 | Q/E 转向 | Space 跳跃 | M 菜单 | V 语音"},
    /* KeyLine2              */ {"Z Left Trigger | X Right Trigger | Wheel Fingers (hold 1-0 for one) | Full fist = grip", u8"Z 左扳机 | X 右扳机 | 滚轮开合手指（按住 1-0 控制单指）| 握拳=抓取"},
    /* MouseHelp             */ {"On a box: LMB XY move | MMB pitch/yaw rotate | hold RMB with LMB/MMB for Z move/roll. Drag empty space = right stick (for radial menus)", u8"在方框上：左键XY移动 | 中键俯仰偏航旋转 | 同时按住右键Z移动/横滚旋转。在空白处拖拽=右摇杆（便于操作圆盘菜单）"},
    /* Mirror                */ {"Mirror", u8"对称"},
    /* FrameLabel            */ {"Move/rotate", u8"移动/旋转参考系"},
    /* FrameHmd              */ {"HMD", u8"头显"},
    /* FrameGlobal           */ {"Global", u8"全局"},
    /* UiModeLabel           */ {"UI", u8"界面"},
    /* UiModeFull            */ {"Full", u8"完整"},
    /* UiModeMini            */ {"Mini", u8"迷你"},
    /* AlwaysOnTop           */ {"Always on top", u8"窗口置顶"},
    /* RegisterDriver        */ {"Register Driver", u8"注册驱动"},
    /* UnregisterDriver      */ {"Unregister Driver", u8"取消注册"},
    /* RestartSteamVr        */ {"Restart SteamVR", u8"重启 SteamVR"},
    /* Cancel                */ {"Cancel", u8"取消"},
    /* RestartConfirmBody    */
    {"Restarting SteamVR will close SteamVR and any VR game that is currently running.\n\n"
     "Disconnect any physical HMD from this PC before continuing. If a wireless HMD is connected, power it off. If a cabled HMD is connected, unplug its USB cable.\n\n"
     "Continue?",
     u8"重启 SteamVR 将关闭 SteamVR 以及任何正在运行的 VR 游戏。\n\n"
     u8"继续前请断开此电脑上的任何实体头显。如果已连接无线头显，请将其关机。如果已连接有线头显，请拔下它的 USB 线。\n\n"
     u8"是否继续？"},
    /* DriverStatusReady     */ {"Register the driver, then restart SteamVR.", u8"先注册驱动，然后重启 SteamVR。"},
    /* StatusRegistered      */ {"Registered. Use Restart SteamVR to apply.", u8"已注册。点击“重启 SteamVR”以应用。"},
    /* StatusUnregistered    */ {"Unregistered. Use Restart SteamVR to apply.", u8"已取消注册。点击“重启 SteamVR”以应用。"},
    /* StatusManifestMissing */ {"driver.vrdrivermanifest was not found next to the UI.", u8"未在工具旁找到 driver.vrdrivermanifest。"},
    /* StatusDriverDllMissing*/ {"bin/win64/driver_anyadance.dll was not found next to the UI.", u8"未在工具旁找到 bin/win64/driver_anyadance.dll。"},
    /* StatusOpenvrPaths..   */ {"openvrpaths.vrpath not found. Launch SteamVR once, then try again.", u8"未找到 openvrpaths.vrpath。请先启动一次 SteamVR，然后重试。"},
    /* StatusConfigWrite..   */ {"Could not update the SteamVR configuration.", u8"无法更新 SteamVR 配置。"},
    /* StatusRestarting      */ {"Restarting SteamVR...", u8"正在重启 SteamVR……"},
    /* StatusRestartFailed   */ {"Failed to launch SteamVR. Is Steam installed?", u8"启动 SteamVR 失败。是否已安装 Steam？"},
    /* StatusFailed          */ {"Operation failed.", u8"操作失败。"},
    /* DeviceHmd             */ {"HMD", u8"头显"},
    /* DeviceLeftController  */ {"Left Controller", u8"左控制器"},
    /* DeviceRightController */ {"Right Controller", u8"右控制器"},
    /* DeviceHip             */ {"Hip", u8"臀部"},
    /* DeviceLeftFoot        */ {"Left Foot", u8"左脚"},
    /* DeviceRightFoot       */ {"Right Foot", u8"右脚"},
    /* DisclaimerTitle       */ {"Disclaimer", u8"免责声明"},
    /* DisclaimerBody        */
    {"AnyaDance is provided for legitimate, authorized testing and development only.\n\n"
     "Feeding virtual devices or spoofed tracking into a live online game may violate that game's "
     "Terms of Service and can be detected by its anti-cheat system, which may result in the "
     "suspension or permanent ban of your account.\n\n"
     "Registering the driver changes your SteamVR configuration: it puts SteamVR into a fully virtual "
     "mode and writes to steamvr.vrsettings, so while the driver is registered your real headset, "
     "controllers, and trackers will not be tracked (a backup is made, and unregistering restores it). "
     "The virtual HMD also continuously renders both eyes through the SteamVR compositor, which consumes "
     "additional GPU and CPU; raising the render resolution increases that load further.\n\n"
     "You use this software entirely at your own risk. It is provided \"as is\" without warranty of "
     "any kind, and the authors accept no responsibility or liability for any consequences of use or "
     "misuse, including account bans or loss of access.\n\n"
     "This notice is a safety acknowledgment only. It does not modify the Apache License 2.0 or "
     "impose additional restrictions on use, modification, or redistribution.\n\n"
     "This project is not affiliated with or endorsed by VRChat, Valve, Steam, or SteamVR. All "
     "trademarks belong to their respective owners.",
     u8"AnyaDance 仅供合法、经授权的测试与开发使用。\n\n"
     u8"将虚拟设备或伪造的追踪数据输入正在运行的在线游戏，可能违反该游戏的服务条款，并可能被其反作弊系统检测到，"
     u8"从而导致你的账号被封禁或永久封停。\n\n"
     u8"注册驱动会更改你的 SteamVR 配置：它会将 SteamVR 切换为全虚拟模式并写入 steamvr.vrsettings，因此在驱动处于"
     u8"注册状态期间，你的真实头显、控制器与追踪器将无法被追踪（注册时会创建备份，取消注册会将其还原）。"
     u8"虚拟头显还会通过 SteamVR 合成器持续渲染左右两只眼睛，会占用额外的 GPU 与 CPU 资源；提高渲染分辨率会进一步加大该负载。\n\n"
     u8"你需自行承担使用本软件的全部风险。本软件按“原样”提供，不附带任何形式的担保；对于因使用或滥用造成的任何后果"
     u8"（包括账号封禁或失去访问权限），作者概不负责，亦不承担任何责任。\n\n"
     u8"本提示仅用于确认你已了解安全风险，不会修改 Apache License 2.0，也不会对使用、修改或再分发施加额外限制。\n\n"
     u8"本项目与 VRChat、Valve、Steam 或 SteamVR 无任何关联，也未获其认可。所有商标归各自所有者所有。"},
    /* DisclaimerAccept      */ {"I Understand", u8"我已了解"},
    /* DisclaimerQuit        */ {"Quit", u8"退出"},
    /* DanceOpen             */ {"Dance (MMD)", u8"舞蹈 (MMD)"},
    /* DanceTitle            */ {"MMD Dance", u8"MMD 舞蹈"},
    /* DanceVmd              */ {"VMD motion", u8"VMD 动作"},
    /* DanceModel            */ {"Model (PMX/PMD)", u8"模型 (PMX/PMD)"},
    /* DanceBrowse           */ {"Browse...", u8"浏览…"},
    /* DanceLoop             */ {"Loop", u8"循环"},
    /* DanceAnalyze          */ {"Analyze", u8"分析"},
    /* DancePlay             */ {"Play", u8"播放"},
    /* DancePause            */ {"Pause", u8"暂停"},
    /* DanceResume           */ {"Resume", u8"继续"},
    /* DanceStop             */ {"Stop", u8"停止"},
    /* DanceClose            */ {"Close", u8"关闭"},
    /* DanceConverting       */ {"Solving with Blender, please wait...", u8"正在使用 Blender 解算，请稍候……"},
    /* DanceHelp             */
    {"Use Advanced to set Blender and MMD Tools paths for custom installs. Those paths are saved for next time.",
     u8"自定义安装路径可在“高级”中设置 Blender 路径与 MMD Tools 路径。这些路径会保存以便下次使用。"},
    /* DanceExperimental     */
    {"MMD conversion is still experimental and may not be accurate.",
     u8"MMD 转换仍是实验性功能，结果可能不准确。"},
    /* DanceTimeline         */ {"Timeline", u8"时间轴"},
    /* DanceReason           */ {"MMD dance", u8"MMD 舞蹈"},
    /* DancePlaying          */ {"Playing MMD dance", u8"正在播放 MMD 舞蹈"},
    /* DanceAdvanced         */ {"Advanced", u8"高级"},
    /* DanceBlenderPath      */ {"Blender path", u8"Blender 路径"},
    /* DanceMmdToolsPath     */ {"MMD Tools path", u8"MMD Tools 路径"},
    /* DanceSaveNya          */ {"Save Dance", u8"保存舞蹈"},
    /* DanceLoadNya          */ {"Load Dance", u8"加载舞蹈"},
    /* PoseSave              */ {"Save Pose", u8"保存姿势"},
    /* PoseLoad              */ {"Load Pose", u8"加载姿势"},
    /* PoseStanding          */ {"Standing Pose", u8"站立姿势"},
    /* PoseMenu              */ {"Menu Pose", u8"菜单姿势"},
};

static_assert(sizeof(kStrings) / sizeof(kStrings[0]) == kTextCount,
              "kStrings must have exactly one row per Text value");

const LanguageInfo kLanguages[kLanguageCount] = {
    {"en-US", "English"},
    {"zh-CN", u8"中文"},
};

Language g_currentLanguage = Language::English;

} // namespace

const char* Tr(Text id, Language language) {
    return kStrings[static_cast<std::size_t>(id)][static_cast<std::size_t>(language)];
}

const char* Tr(Text id) {
    return Tr(id, g_currentLanguage);
}

const char* DeviceName(std::size_t slot, Language language) {
    return Tr(static_cast<Text>(static_cast<std::size_t>(Text::DeviceHmd) + slot), language);
}

const char* DeviceName(std::size_t slot) {
    return DeviceName(slot, g_currentLanguage);
}

Language CurrentLanguage() {
    return g_currentLanguage;
}

void SetCurrentLanguage(Language language) {
    g_currentLanguage = language;
}

const LanguageInfo& GetLanguageInfo(Language language) {
    return kLanguages[static_cast<std::size_t>(language)];
}

Language FindLanguageByCode(const char* code, Language fallback) {
    if (code) {
        for (std::size_t i = 0; i < kLanguageCount; ++i) {
            if (std::strcmp(kLanguages[i].code, code) == 0) {
                return static_cast<Language>(i);
            }
        }
    }
    return fallback;
}

} // namespace anyadance::ui
