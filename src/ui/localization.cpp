#include "ui/localization.h"

#include <cstring>

namespace anyadance::ui {
namespace {

// One row per Text, one column per Language. To add a string, add a row here
// (and a Text enum value); to add a language, add a column to every row (and a
// Language enum value plus a kLanguages entry). Translations for one string
// stay together on its row, keeping enum/table positions paired locally.
const char* const kStrings[kTextCount][kLanguageCount] = {
    /* Reset                 */ {"Reset to T-Pose", u8"重置为 T 姿势", u8"Tポーズにリセット"},
    /* UdpLog                */ {"UDP Log", u8"UDP 日志", u8"UDP ログ"},
    /* MonitorDriverCommands */ {"Monitor driver commands", u8"监视驱动命令", u8"ドライバーコマンドを監視"},
    /* LogScrollLatest       */ {"Scroll to latest", u8"滚动到最新", u8"最新までスクロール"},
    /* Clear                 */ {"Clear", u8"清除", u8"クリア"},
    /* Copy                  */ {"Copy", u8"复制", u8"コピー"},
    /* CopyCommand           */ {"Copy resend command", u8"复制重发命令", u8"再送コマンドをコピー"},
    /* Resend                */ {"Resend", u8"重新发送", u8"再送"},
    /* ResendReason          */ {"Resend", u8"重新发送", u8"再送"},
    /* Time                  */ {"Time", u8"时间", u8"時刻"},
    /* Reason                */ {"Reason", u8"原因", u8"理由"},
    /* Result                */ {"Result", u8"结果", u8"結果"},
    /* Sent                  */ {"Sent", u8"已发送", u8"送信済み"},
    /* Failed                */ {"Failed", u8"失败", u8"失敗"},
    /* ResetReason           */ {"Reset to T-Pose", u8"重置为 T 姿势", u8"Tポーズにリセット"},
    /* ManipulatedReason     */ {"Device manipulated", u8"设备已移动", u8"デバイスを操作"},
    /* KeyboardReason        */ {"Input captured", u8"输入已捕获", u8"入力を取得"},
    /* ReleaseReason         */ {"Input released", u8"输入已释放", u8"入力を解放"},
    /* SocketErrorReason     */ {"Socket error", u8"套接字错误", u8"ソケットエラー"},
    /* LanguageLabel         */ {"Language", u8"语言", u8"言語"},
    /* YMax                  */ {"Y MAX", "Y MAX", u8"Y 最大"},
    /* Capture               */ {"Captured", u8"捕获中", u8"操作中"},
    /* HmdHelp               */ {"Rotate | RMB: up/down", u8"旋转 | 右键上下", u8"回転 | 右ドラッグ上下"},
    /* KeyLine1              */ {"WASD Move | Q/E Turn | Space Jump | M Menu | V Voice", u8"WASD 移动 | Q/E 转向 | Space 跳跃 | M 菜单 | V 语音", u8"WASD 移動 | Q/E 旋回 | Space ジャンプ | M メニュー | V ボイス"},
    /* KeyLine2              */ {"Z Left Trigger | X Right Trigger | Wheel Fingers (hold 1-0 for one) | Full fist = grip", u8"Z 左扳机 | X 右扳机 | 滚轮开合手指（按住 1-0 控制单指）| 握拳=抓取", u8"Z 左トリガー | X 右トリガー | ホイールで指（1～0で1本）| 握り拳=グリップ"},
    /* MouseHelp             */ {"Box: LMB move XY | MMB rotate | +RMB move Z/roll. Empty: LMB right stick | MMB rotate rig | MMB+RMB roll rig | RMB rig up/down", u8"方框上：左键XY移动 | 中键旋转 | 加右键Z移动/横滚。空白处：左键=右摇杆 | 中键旋转全身 | 中键+右键横滚全身 | 右键升降全身", u8"ボックス：左でXY移動 | 中で回転 | +右でZ移動/ロール。空白：左=右スティック | 中=リグ回転 | 中+右=リグロール | 右=リグ上下"},
    /* Mirror                */ {"Mirror", u8"对称", u8"ミラー"},
    /* FrameLabel            */ {"Move/rotate", u8"移动/旋转参考系", u8"移動/回転"},
    /* FrameHmd              */ {"HMD", u8"头显", "HMD"},
    /* FrameGlobal           */ {"Global", u8"全局", u8"グローバル"},
    /* UiModeLabel           */ {"UI", u8"界面", "UI"},
    /* UiModeFull            */ {"Full", u8"完整", u8"フル"},
    /* UiModeMini            */ {"Mini", u8"迷你", u8"ミニ"},
    /* AlwaysOnTop           */ {"Always on top", u8"窗口置顶", u8"常に手前に表示"},
    /* RegisterDriver        */ {"Register Driver", u8"注册驱动", u8"ドライバーを登録"},
    /* UnregisterDriver      */ {"Unregister Driver", u8"取消注册", u8"登録を解除"},
    /* RestartSteamVr        */ {"Restart SteamVR", u8"重启 SteamVR", u8"SteamVR を再起動"},
    /* Cancel                */ {"Cancel", u8"取消", u8"キャンセル"},
    /* RestartConfirmBody    */
    {"Restarting SteamVR will close SteamVR and any VR game that is currently running.\n\n"
     "Disconnect any physical HMD from this PC before continuing. If a wireless HMD is connected, power it off. If a cabled HMD is connected, unplug its USB cable.\n\n"
     "Continue?",
     u8"重启 SteamVR 将关闭 SteamVR 以及任何正在运行的 VR 游戏。\n\n"
     u8"继续前请断开此电脑上的任何实体头显。如果已连接无线头显，请将其关机。如果已连接有线头显，请拔下它的 USB 线。\n\n"
     u8"是否继续？",
     u8"SteamVR を再起動すると、SteamVR と実行中のすべての VR ゲームが終了します。\n\n"
     u8"続行する前に、この PC から物理 HMD をすべて切断してください。無線 HMD は電源を切り、有線 HMD は USB ケーブルを抜いてください。\n\n"
     u8"続行しますか？"},
    /* DriverStatusReady     */ {"Register the driver, then restart SteamVR.", u8"先注册驱动，然后重启 SteamVR。", u8"ドライバーを登録してから SteamVR を再起動してください。"},
    /* StatusRegistered      */ {"Registered. Use Restart SteamVR to apply.", u8"已注册。点击“重启 SteamVR”以应用。", u8"登録しました。「SteamVR を再起動」で適用してください。"},
    /* StatusUnregistered    */ {"Unregistered. Use Restart SteamVR to apply.", u8"已取消注册。点击“重启 SteamVR”以应用。", u8"登録を解除しました。「SteamVR を再起動」で適用してください。"},
    /* StatusManifestMissing */ {"driver.vrdrivermanifest was not found next to the UI.", u8"未在工具旁找到 driver.vrdrivermanifest。", u8"UI と同じ場所に driver.vrdrivermanifest が見つかりません。"},
    /* StatusDriverDllMissing*/ {"bin/win64/driver_anyadance.dll was not found next to the UI.", u8"未在工具旁找到 bin/win64/driver_anyadance.dll。", u8"UI と同じ場所に bin/win64/driver_anyadance.dll が見つかりません。"},
    /* StatusOpenvrPaths..   */ {"openvrpaths.vrpath not found. Launch SteamVR once, then try again.", u8"未找到 openvrpaths.vrpath。请先启动一次 SteamVR，然后重试。", u8"openvrpaths.vrpath が見つかりません。SteamVR を一度起動してから再試行してください。"},
    /* StatusConfigWrite..   */ {"Could not update the SteamVR configuration.", u8"无法更新 SteamVR 配置。", u8"SteamVR の設定を更新できませんでした。"},
    /* StatusRestarting      */ {"Restarting SteamVR...", u8"正在重启 SteamVR……", u8"SteamVR を再起動しています…"},
    /* StatusRestartFailed   */ {"Failed to launch SteamVR. Is Steam installed?", u8"启动 SteamVR 失败。是否已安装 Steam？", u8"SteamVR を起動できませんでした。Steam はインストールされていますか？"},
    /* StatusFailed          */ {"Operation failed.", u8"操作失败。", u8"操作に失敗しました。"},
    /* DeviceHmd             */ {"HMD", u8"头显", "HMD"},
    /* DeviceLeftController  */ {"Left Controller", u8"左控制器", u8"左コントローラー"},
    /* DeviceRightController */ {"Right Controller", u8"右控制器", u8"右コントローラー"},
    /* DeviceHip             */ {"Hip", u8"臀部", u8"腰"},
    /* DeviceLeftFoot        */ {"Left Foot", u8"左脚", u8"左足"},
    /* DeviceRightFoot       */ {"Right Foot", u8"右脚", u8"右足"},
    /* DisclaimerTitle       */ {"Disclaimer", u8"免责声明", u8"免責事項"},
    /* DisclaimerBody        */
    {"AnyaDance is provided for legitimate, authorized testing and development only.\n\n"
     "Feeding virtual devices or spoofed tracking into a live online game may violate that game's "
     "Terms of Service and can be detected by its anti-cheat system, which may result in the "
     "suspension or permanent ban of your account.\n\n"
     "Registering the driver changes your SteamVR configuration: it activates AnyaDance's fully virtual "
     "HMD, controllers, and trackers and writes to steamvr.vrsettings. Registration creates a backup, "
     "and unregistering restores the original configuration. "
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
     u8"注册驱动会更改你的 SteamVR 配置：它会启用 AnyaDance 的全虚拟头显、控制器与追踪器，并写入 steamvr.vrsettings。"
     u8"注册时会创建备份，取消注册会还原原始配置。"
     u8"虚拟头显还会通过 SteamVR 合成器持续渲染左右两只眼睛，会占用额外的 GPU 与 CPU 资源；提高渲染分辨率会进一步加大该负载。\n\n"
     u8"你需自行承担使用本软件的全部风险。本软件按“原样”提供，不附带任何形式的担保；对于因使用或滥用造成的任何后果"
     u8"（包括账号封禁或失去访问权限），作者概不负责，亦不承担任何责任。\n\n"
     u8"本提示仅用于确认你已了解安全风险，不会修改 Apache License 2.0，也不会对使用、修改或再分发施加额外限制。\n\n"
     u8"本项目与 VRChat、Valve、Steam 或 SteamVR 无任何关联，也未获其认可。所有商标归各自所有者所有。",
     u8"AnyaDance は、正当かつ許可されたテストおよび開発のみを目的として提供されます。\n\n"
     u8"ライブのオンラインゲームへ仮想デバイスや偽装したトラッキング情報を入力すると、そのゲームの利用規約に違反する可能性があります。また、アンチチートシステムに検出され、アカウントの一時停止または永久停止につながる可能性があります。\n\n"
     u8"ドライバーを登録すると SteamVR の構成が変更されます。AnyaDance の完全仮想 HMD、コントローラー、トラッカーが有効化され、steamvr.vrsettings が書き換えられます。登録時にバックアップが作成され、登録解除時に元の構成が復元されます。"
     u8"仮想 HMD は SteamVR コンポジターを通じて左右の目を継続的に描画するため、追加の GPU と CPU リソースを消費します。レンダー解像度を上げると、その負荷も増加します。\n\n"
     u8"本ソフトウェアは、すべて自己責任で使用してください。本ソフトウェアは明示・黙示を問わずいかなる保証もなく「現状有姿」で提供され、作者はアカウントの停止やアクセス権の喪失を含め、使用または誤用によって生じるいかなる結果についても責任を負いません。\n\n"
     u8"この通知は安全上の確認事項に限られます。Apache License 2.0 を変更したり、使用、変更、再配布に追加の制限を課したりするものではありません。\n\n"
     u8"本プロジェクトは VRChat、Valve、Steam、SteamVR のいずれとも提携しておらず、それらから承認を受けたものでもありません。すべての商標は各権利者に帰属します。"},
    /* DisclaimerAccept      */ {"I Understand", u8"我已了解", u8"理解しました"},
    /* DisclaimerQuit        */ {"Quit", u8"退出", u8"終了"},
    /* DanceOpen             */ {"Dance (MMD)", u8"舞蹈 (MMD)", u8"ダンス (MMD)"},
    /* DanceTitle            */ {"MMD Dance", u8"MMD 舞蹈", u8"MMD ダンス"},
    /* DanceVmd              */ {"VMD motion", u8"VMD 动作", u8"VMD モーション"},
    /* DanceModel            */ {"Model (PMX/PMD)", u8"模型 (PMX/PMD)", u8"モデル (PMX/PMD)"},
    /* DanceBrowse           */ {"Browse...", u8"浏览…", u8"参照…"},
    /* DanceLoop             */ {"Loop", u8"循环", u8"ループ"},
    /* DanceAnalyze          */ {"Analyze", u8"分析", u8"解析"},
    /* DancePlay             */ {"Play", u8"播放", u8"再生"},
    /* DancePause            */ {"Pause", u8"暂停", u8"一時停止"},
    /* DanceResume           */ {"Resume", u8"继续", u8"再開"},
    /* DanceStop             */ {"Stop", u8"停止", u8"停止"},
    /* DanceClose            */ {"Close", u8"关闭", u8"閉じる"},
    /* DanceConverting       */ {"Solving with Blender, please wait...", u8"正在使用 Blender 解算，请稍候……", u8"Blender で解析しています。お待ちください…"},
    /* DanceHelp             */
    {"Use Advanced to set Blender and MMD Tools paths for custom installs. Those paths are saved for next time.",
     u8"自定义安装路径可在“高级”中设置 Blender 路径与 MMD Tools 路径。这些路径会保存以便下次使用。",
     u8"独自のインストール先を使う場合は、「詳細設定」で Blender と MMD Tools のパスを指定してください。パスは次回のために保存されます。"},
    /* DanceExperimental     */
    {"MMD conversion is still experimental and may not be accurate.",
     u8"MMD 转换仍是实验性功能，结果可能不准确。",
     u8"MMD 変換はまだ実験的な機能であり、正確でない場合があります。"},
    /* DanceTimeline         */ {"Timeline", u8"时间轴", u8"タイムライン"},
    /* DanceReason           */ {"MMD dance", u8"MMD 舞蹈", u8"MMD ダンス"},
    /* DancePlaying          */ {"Playing MMD dance", u8"正在播放 MMD 舞蹈", u8"MMD ダンスを再生中"},
    /* DanceAdvanced         */ {"Advanced", u8"高级", u8"詳細設定"},
    /* DanceBlenderPath      */ {"Blender path", u8"Blender 路径", u8"Blender のパス"},
    /* DanceMmdToolsPath     */ {"MMD Tools path", u8"MMD Tools 路径", u8"MMD Tools のパス"},
    /* DanceSaveNya          */ {"Save Dance", u8"保存舞蹈", u8"ダンスを保存"},
    /* DanceLoadNya          */ {"Load Dance", u8"加载舞蹈", u8"ダンスを読み込み"},
    /* PoseSave              */ {"Save Pose", u8"保存姿势", u8"ポーズを保存"},
    /* PoseLoad              */ {"Load Pose", u8"加载姿势", u8"ポーズを読み込み"},
    /* PoseStanding          */ {"Standing Pose", u8"站立姿势", u8"立ちポーズ"},
    /* PoseMenu              */ {"Menu Pose", u8"菜单姿势", u8"メニューポーズ"},
};

static_assert(sizeof(kStrings) / sizeof(kStrings[0]) == kTextCount,
              "kStrings must have exactly one row per Text value");

const LanguageInfo kLanguages[kLanguageCount] = {
    {"en-US", "English"},
    {"zh-CN", u8"中文"},
    {"ja-JP", u8"日本語"},
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
