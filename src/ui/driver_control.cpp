#include "ui/driver_control.h"

#include "core/json.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>

namespace anyadance::ui {
namespace {

namespace fs = std::filesystem;
using anyadance::json::Value;

fs::path ExeDirectory() {
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(length);
    return fs::path(buffer).parent_path();
}

// The driver root is the executable's own folder. The shipped bundle is one flat
// folder: the exe next to driver.vrdrivermanifest, bin/win64/driver_anyadance.dll,
// and resources/.
fs::path DriverRoot() {
    fs::path root = ExeDirectory();
    root.make_preferred();
    return root;
}

// OpenVR loads <root>/bin/<platform>/driver_<name>.dll, so the DLL location is
// fixed by the runtime and cannot be a bare file at the root.
fs::path DriverDllPath() {
    fs::path dll = DriverRoot() / L"bin" / L"win64" / L"driver_anyadance.dll";
    dll.make_preferred();
    return dll;
}

fs::path EnvPath(const wchar_t* name) {
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD length = GetEnvironmentVariableW(name, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length > buffer.size()) {
        return {};
    }
    buffer.resize(length);
    return fs::path(buffer);
}

fs::path OpenvrPathsFile() {
    return EnvPath(L"LOCALAPPDATA") / L"openvr" / L"openvrpaths.vrpath";
}

fs::path BackupPath() {
    return EnvPath(L"LOCALAPPDATA") / L"AnyaDance" / L"steamvr.vrsettings.backup";
}

// Register records the exact driver-root path it added to openvrpaths here, in the
// stable per-user AppData folder. The folder name never moves, so Unregister can
// remove the original entry even if the bundle was moved or renamed after it was
// registered. PathToUtf8/SamePath compare these consistently.
fs::path RegisteredPathRecord() {
    return EnvPath(L"LOCALAPPDATA") / L"AnyaDance" / L"registered_driver_path.txt";
}

// Drop trailing CR/LF/whitespace so a recorded path round-trips through SamePath,
// which compares lengths exactly.
std::string TrimTrailing(std::string value) {
    while (!value.empty() &&
           (value.back() == '\r' || value.back() == '\n' || value.back() == ' ' || value.back() == '\t')) {
        value.pop_back();
    }
    return value;
}

std::optional<std::wstring> ReadRegistryString(HKEY root, const wchar_t* subkey, const wchar_t* valueName) {
    wchar_t buffer[1024] = {};
    DWORD bytes = sizeof(buffer);
    const LSTATUS status = RegGetValueW(root, subkey, valueName, RRF_RT_REG_SZ, nullptr, buffer, &bytes);
    if (status != ERROR_SUCCESS || bytes < sizeof(wchar_t)) {
        return std::nullopt;
    }
    return std::wstring(buffer);
}

std::optional<fs::path> ResolveSteamExe() {
    for (const auto& [root, subkey] : {
             std::pair{HKEY_CURRENT_USER, L"Software\\Valve\\Steam"},
             std::pair{HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam"},
         }) {
        if (std::optional<std::wstring> steamPath = ReadRegistryString(root, subkey, L"SteamPath")) {
            fs::path candidate = fs::path(*steamPath) / L"steam.exe";
            std::error_code ec;
            if (fs::exists(candidate, ec)) {
                return candidate;
            }
        }
    }

    fs::path fallback = L"C:\\Program Files (x86)\\Steam\\steam.exe";
    std::error_code ec;
    if (fs::exists(fallback, ec)) {
        return fallback;
    }
    return std::nullopt;
}

std::string PathToUtf8(const fs::path& path) {
    return path.u8string();
}

std::optional<std::string> ReadTextFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool WriteTextFile(const fs::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out << content;
    return static_cast<bool>(out);
}

bool SamePath(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        char ca = a[i] == '/' ? '\\' : a[i];
        char cb = b[i] == '/' ? '\\' : b[i];
        if (std::tolower(static_cast<unsigned char>(ca)) != std::tolower(static_cast<unsigned char>(cb))) {
            return false;
        }
    }
    return true;
}

bool IsAnyaDanceDriverRoot(const std::string& root) {
    const std::optional<std::string> manifestText =
        ReadTextFile(fs::u8path(root) / L"driver.vrdrivermanifest");
    if (!manifestText) {
        return false;
    }
    const std::optional<Value> manifest = anyadance::json::Parse(*manifestText);
    if (!manifest || !manifest->IsObject()) {
        return false;
    }
    const Value* name = manifest->Find("name");
    return name && name->type == anyadance::json::Type::String && name->string == "anyadance";
}

fs::path ResolveSettingsPath(const Value& openvrPaths) {
    const Value* config = openvrPaths.Find("config");
    if (config && config->IsArray() && !config->array.empty() &&
        config->array[0].type == anyadance::json::Type::String) {
        return fs::u8path(config->array[0].string) / L"steamvr.vrsettings";
    }
    return fs::path(L"C:\\Program Files (x86)\\Steam\\config\\steamvr.vrsettings");
}

void KillProcessByName(const wchar_t* exeName) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, exeName) == 0) {
                if (HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, entry.th32ProcessID)) {
                    TerminateProcess(process, 0);
                    WaitForSingleObject(process, 2000);
                    CloseHandle(process);
                }
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
}

void SetFullyVirtualSettings(Value& settings) {
    Value steamvr = Value::Object();
    if (const Value* existing = settings.Find("steamvr"); existing && existing->IsObject()) {
        steamvr = *existing;
    }
    steamvr.Set("activateMultipleDrivers", Value::Bool(true));
    steamvr.Set("forcedDriver", Value::String("anyadance"));
    steamvr.Set("requireHmd", Value::Bool(true));
    settings.Set("steamvr", steamvr);

    Value driver = Value::Object();
    if (const Value* existing = settings.Find("driver_anyadance"); existing && existing->IsObject()) {
        driver = *existing;
    }
    driver.Set("enable", Value::Bool(true));
    driver.Set("enable_hmd", Value::Bool(true));
    driver.Set("enable_controllers", Value::Bool(true));
    driver.Set("enable_trackers", Value::Bool(true));
    settings.Set("driver_anyadance", driver);

    // Keep the virtual HMD display awake: a held-still virtual headset never
    // triggers SteamVR's idle screen-off, so push the timeout out far and keep
    // the compositor running through standby. Restored from the backup on
    // unregister.
    Value power = Value::Object();
    if (const Value* existing = settings.Find("power"); existing && existing->IsObject()) {
        power = *existing;
    }
    power.Set("turnOffScreensTimeout", Value::Number(86400.0));
    power.Set("pauseCompositorOnStandby", Value::Bool(false));
    settings.Set("power", power);
}

} // namespace

DriverActionResult RegisterDriver() {
    try {
        const fs::path driverRoot = DriverRoot();
        if (!fs::exists(driverRoot / L"driver.vrdrivermanifest")) {
            return {false, DriverStatus::ManifestMissing, {}};
        }
        if (!fs::exists(DriverDllPath())) {
            return {false, DriverStatus::DriverDllMissing, {}};
        }

        const fs::path pathsFile = OpenvrPathsFile();
        std::optional<std::string> pathsText = ReadTextFile(pathsFile);
        if (!pathsText) {
            return {false, DriverStatus::OpenvrPathsMissing, {}};
        }
        std::optional<Value> pathsJson = anyadance::json::Parse(*pathsText);
        if (!pathsJson || !pathsJson->IsObject()) {
            return {false, DriverStatus::ConfigWriteFailed, {}};
        }

        const fs::path settingsPath = ResolveSettingsPath(*pathsJson);
        const fs::path backupPath = BackupPath();
        std::error_code ec;
        const bool settingsExists = fs::exists(settingsPath, ec);
        if (ec) {
            return {false, DriverStatus::ConfigWriteFailed, {}};
        }
        ec.clear();
        const bool backupExists = fs::exists(backupPath, ec);
        if (ec) {
            return {false, DriverStatus::ConfigWriteFailed, {}};
        }
        if (settingsExists && !backupExists) {
            fs::create_directories(backupPath.parent_path(), ec);
            if (ec) {
                return {false, DriverStatus::ConfigWriteFailed, {}};
            }
            ec.clear();
            if (!fs::copy_file(settingsPath, backupPath, fs::copy_options::none, ec) || ec) {
                return {false, DriverStatus::ConfigWriteFailed, {}};
            }
        }

        // Registration is not safe without a durable pointer to the exact
        // bundle root. Write it before changing OpenVR or SteamVR state so an
        // unregister launched from another copy can always find this entry.
        ec.clear();
        fs::create_directories(RegisteredPathRecord().parent_path(), ec);
        if (ec || !WriteTextFile(RegisteredPathRecord(), PathToUtf8(driverRoot))) {
            return {false, DriverStatus::ConfigWriteFailed, {}};
        }

        const std::string driverUtf8 = PathToUtf8(driverRoot);
        Value drivers = Value::Array();
        if (const Value* existing = pathsJson->Find("external_drivers"); existing && existing->IsArray()) {
            drivers = *existing;
        }
        bool present = false;
        for (const Value& entry : drivers.array) {
            if (entry.type == anyadance::json::Type::String && SamePath(entry.string, driverUtf8)) {
                present = true;
                break;
            }
        }
        if (!present) {
            drivers.array.push_back(Value::String(driverUtf8));
        }
        pathsJson->Set("external_drivers", drivers);
        if (!WriteTextFile(pathsFile, anyadance::json::Serialize(*pathsJson))) {
            return {false, DriverStatus::ConfigWriteFailed, {}};
        }

        Value settings = Value::Object();
        if (std::optional<std::string> settingsText = ReadTextFile(settingsPath)) {
            if (std::optional<Value> parsed = anyadance::json::Parse(*settingsText); parsed && parsed->IsObject()) {
                settings = *parsed;
            }
        }
        SetFullyVirtualSettings(settings);
        if (!WriteTextFile(settingsPath, anyadance::json::Serialize(settings))) {
            return {false, DriverStatus::ConfigWriteFailed, {}};
        }

        return {true, DriverStatus::Registered, {}};
    } catch (const std::exception& e) {
        return {false, DriverStatus::Failed, e.what()};
    }
}

DriverActionResult UnregisterDriver() {
    try {
        const std::string driverUtf8 = PathToUtf8(DriverRoot());
        // The bundle may have been moved since it was registered; fall back to the
        // path recorded at register time so the original entry is removed too.
        std::string recordedUtf8;
        if (std::optional<std::string> recorded = ReadTextFile(RegisteredPathRecord())) {
            recordedUtf8 = TrimTrailing(*recorded);
        }
        const fs::path pathsFile = OpenvrPathsFile();
        fs::path settingsPath(L"C:\\Program Files (x86)\\Steam\\config\\steamvr.vrsettings");

        const std::optional<std::string> pathsText = ReadTextFile(pathsFile);
        if (!pathsText) {
            return {false, DriverStatus::OpenvrPathsMissing, {}};
        }
        std::optional<Value> pathsJson = anyadance::json::Parse(*pathsText);
        if (!pathsJson || !pathsJson->IsObject()) {
            return {false, DriverStatus::ConfigWriteFailed, {}};
        }
        settingsPath = ResolveSettingsPath(*pathsJson);
        if (Value* drivers = pathsJson->Find("external_drivers"); drivers && drivers->IsArray()) {
            auto& entries = drivers->array;
            entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const Value& v) {
                if (v.type != anyadance::json::Type::String) {
                    return false;
                }
                return SamePath(v.string, driverUtf8) ||
                       (!recordedUtf8.empty() && SamePath(v.string, recordedUtf8)) ||
                       IsAnyaDanceDriverRoot(v.string);
            }), entries.end());
            if (!WriteTextFile(pathsFile, anyadance::json::Serialize(*pathsJson))) {
                return {false, DriverStatus::ConfigWriteFailed, {}};
            }
        }

        const fs::path backupPath = BackupPath();
        std::error_code ec;
        if (fs::exists(backupPath)) {
            if (!fs::copy_file(backupPath, settingsPath, fs::copy_options::overwrite_existing, ec) || ec) {
                return {false, DriverStatus::ConfigWriteFailed, {}};
            }
            ec.clear();
            if (!fs::remove(backupPath, ec) || ec) {
                return {false, DriverStatus::ConfigWriteFailed, {}};
            }
        }
        ec.clear();
        const bool recordExists = fs::exists(RegisteredPathRecord(), ec);
        if (ec || (recordExists && (!fs::remove(RegisteredPathRecord(), ec) || ec))) {
            return {false, DriverStatus::ConfigWriteFailed, {}};
        }
        return {true, DriverStatus::Unregistered, {}};
    } catch (const std::exception& e) {
        return {false, DriverStatus::Failed, e.what()};
    }
}

DriverActionResult RestartSteamVR() {
    for (const wchar_t* name : {L"vrserver.exe", L"vrmonitor.exe", L"vrcompositor.exe"}) {
        KillProcessByName(name);
    }

    HINSTANCE result = nullptr;
    if (std::optional<fs::path> steamExe = ResolveSteamExe()) {
        result = ShellExecuteW(nullptr, L"open", steamExe->c_str(), L"steam://run/250820", nullptr, SW_SHOWNORMAL);
    } else {
        result = ShellExecuteW(nullptr, L"open", L"steam://run/250820", nullptr, nullptr, SW_SHOWNORMAL);
    }
    if (reinterpret_cast<INT_PTR>(result) > 32) {
        return {true, DriverStatus::Restarting, {}};
    }
    return {false, DriverStatus::RestartFailed, {}};
}

} // namespace anyadance::ui
