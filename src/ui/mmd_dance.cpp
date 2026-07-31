#include "ui/mmd_dance.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

namespace anyadance::ui {
namespace {

std::wstring Widen(const std::string& utf8) {
    if (utf8.empty()) {
        return std::wstring();
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(size > 0 ? size - 1 : 0), L'\0');
    if (size > 0) {
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, wide.data(), size);
    }
    return wide;
}

std::string Narrow(const std::wstring& wide) {
    if (wide.empty()) {
        return std::string();
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<std::size_t>(size > 0 ? size - 1 : 0), '\0');
    if (size > 0) {
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, utf8.data(), size, nullptr, nullptr);
    }
    return utf8;
}

bool FileExists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

bool DirExists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring EnvVar(const wchar_t* name) {
    wchar_t buffer[MAX_PATH * 2] = {};
    const DWORD length = GetEnvironmentVariableW(name, buffer, static_cast<DWORD>(std::size(buffer)));
    return (length > 0 && length < std::size(buffer)) ? std::wstring(buffer, length) : std::wstring();
}

std::wstring ExeDirectory() {
    wchar_t buffer[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    std::wstring path(buffer, length);
    const std::size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

std::wstring FindExecutableOnPath(const wchar_t* name) {
    wchar_t found[MAX_PATH] = {};
    const DWORD length = SearchPathW(
        nullptr, name, nullptr, static_cast<DWORD>(std::size(found)), found, nullptr);
    return (length > 0 && length < std::size(found)) ? std::wstring(found, length) : std::wstring();
}

// Newest-first list of "Blender Foundation\Blender X.Y" child dirs in a root.
std::vector<std::wstring> BlenderVersionDirs(const std::wstring& root) {
    std::vector<std::wstring> dirs;
    WIN32_FIND_DATAW find{};
    const std::wstring pattern = root + L"\\*";
    HANDLE handle = FindFirstFileW(pattern.c_str(), &find);
    if (handle == INVALID_HANDLE_VALUE) {
        return dirs;
    }
    do {
        if ((find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            find.cFileName[0] != L'.') {
            dirs.push_back(root + L"\\" + find.cFileName);
        }
    } while (FindNextFileW(handle, &find));
    FindClose(handle);
    // Lexicographic descending puts "Blender 5.1" ahead of "Blender 4.2"; good
    // enough for the single-digit-major version dirs Blender uses.
    std::sort(dirs.begin(), dirs.end(), std::greater<>());
    return dirs;
}

std::wstring DetectBlenderExeW() {
    if (const std::wstring override = EnvVar(L"ANYADANCE_BLENDER"); !override.empty() && FileExists(override)) {
        return override;
    }
    for (const wchar_t* programFiles : {L"ProgramFiles", L"ProgramFiles(x86)"}) {
        const std::wstring base = EnvVar(programFiles);
        if (base.empty()) {
            continue;
        }
        const std::wstring root = base + L"\\Blender Foundation";
        for (const std::wstring& dir : BlenderVersionDirs(root)) {
            const std::wstring exe = dir + L"\\blender.exe";
            if (FileExists(exe)) {
                return exe;
            }
        }
    }
    // Fall back to executables on PATH. Microsoft Store installations expose
    // blender-launcher.exe as an app execution alias instead of blender.exe;
    // the launcher forwards Blender's headless command-line arguments.
    if (const std::wstring blender = FindExecutableOnPath(L"blender.exe"); !blender.empty()) {
        return blender;
    }
    if (const std::wstring launcher = FindExecutableOnPath(L"blender-launcher.exe"); !launcher.empty()) {
        return launcher;
    }

    // WindowsApps normally appears on PATH, but check its per-user location as
    // well in case the process inherited a customized PATH.
    if (const std::wstring localAppData = EnvVar(L"LOCALAPPDATA"); !localAppData.empty()) {
        const std::wstring launcher = localAppData + L"\\Microsoft\\WindowsApps\\blender-launcher.exe";
        if (FileExists(launcher)) {
            return launcher;
        }
    }
    return std::wstring();
}

std::wstring DetectMmdToolsW() {
    const std::wstring appData = EnvVar(L"APPDATA");
    if (appData.empty()) {
        return std::wstring();
    }
    const std::wstring root = appData + L"\\Blender Foundation\\Blender";
    for (const std::wstring& versionDir : BlenderVersionDirs(root)) {
        // Modern Blender installs add-ons as extensions; older ones under addons.
        const std::wstring candidates[] = {
            versionDir + L"\\extensions\\blender_org\\mmd_tools",
            versionDir + L"\\extensions\\user_default\\mmd_tools",
            versionDir + L"\\scripts\\addons\\mmd_tools",
        };
        for (const std::wstring& candidate : candidates) {
            if (DirExists(candidate)) {
                return candidate;
            }
        }
    }
    return std::wstring();
}

std::wstring ScriptPath() {
    return ExeDirectory() + L"\\scripts\\blender_export_mmd.py";
}

std::wstring SolvedOutputPath() {
    std::wstring temp = EnvVar(L"TEMP");
    if (temp.empty()) {
        temp = EnvVar(L"TMP");
    }
    if (temp.empty()) {
        temp = ExeDirectory();
    }
    const std::wstring dir = temp + L"\\AnyaDance";
    CreateDirectoryW(dir.c_str(), nullptr);
    // A single, reused output file so an export never accumulates leftovers.
    return dir + L"\\mmd_solved.json";
}

std::string ReadWholeFile(const std::wstring& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::string();
    }
    std::ostringstream contents;
    contents << in.rdbuf();
    return contents.str();
}

// Run a command line, draining stdout+stderr into captured, returning the exit
// code (or -1 if the process could not be launched).
int RunCaptured(std::wstring commandLine, std::string& captured) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
        return -1;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    si.hStdInput = nullptr;

    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');

    const BOOL created = CreateProcessW(
        nullptr,
        mutableCommand.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi);
    CloseHandle(writePipe);  // parent keeps only the read end
    if (!created) {
        CloseHandle(readPipe);
        return -1;
    }

    // Read to EOF first: the child closes its write end on exit, so this never
    // deadlocks against a full pipe.
    char buffer[4096];
    DWORD read = 0;
    while (ReadFile(readPipe, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
        captured.append(buffer, read);
    }
    CloseHandle(readPipe);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return static_cast<int>(exitCode);
}

// Keep only the last line or two of Blender output for a compact status message.
std::string LastLines(const std::string& text, std::size_t maxChars) {
    std::string trimmed = text;
    while (!trimmed.empty() && (trimmed.back() == '\n' || trimmed.back() == '\r' || trimmed.back() == ' ')) {
        trimmed.pop_back();
    }
    if (trimmed.size() <= maxChars) {
        return trimmed;
    }
    return "..." + trimmed.substr(trimmed.size() - maxChars);
}

} // namespace

std::string DetectBlenderExe() {
    return Narrow(DetectBlenderExeW());
}

std::string DetectMmdToolsPath() {
    return Narrow(DetectMmdToolsW());
}

std::string OpenFileDialog(void* ownerHwnd, const char* title, const char* label, const char* pattern) {
    // Build the double-NUL terminated filter Win32 expects: "Label\0*.ext\0\0".
    std::wstring filter = Widen(label ? label : "Files");
    filter.push_back(L'\0');
    filter += Widen(pattern ? pattern : "*.*");
    filter.push_back(L'\0');
    filter.push_back(L'\0');

    wchar_t fileBuffer[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = reinterpret_cast<HWND>(ownerHwnd);
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = fileBuffer;
    ofn.nMaxFile = static_cast<DWORD>(std::size(fileBuffer));
    const std::wstring wideTitle = Widen(title ? title : "Open");
    ofn.lpstrTitle = wideTitle.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (!GetOpenFileNameW(&ofn)) {
        return std::string();
    }
    return Narrow(std::wstring(fileBuffer));
}

std::string OpenFolderDialog(void* ownerHwnd, const char* title) {
    const std::wstring wideTitle = Widen(title ? title : "Select folder");
    BROWSEINFOW bi{};
    bi.hwndOwner = reinterpret_cast<HWND>(ownerHwnd);
    bi.lpszTitle = wideTitle.c_str();
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl == nullptr) {
        return std::string();
    }
    wchar_t path[MAX_PATH] = {};
    const BOOL ok = SHGetPathFromIDListW(pidl, path);
    CoTaskMemFree(pidl);
    return ok ? Narrow(std::wstring(path)) : std::string();
}

std::string SaveFileDialog(void* ownerHwnd, const char* title, const char* label, const char* pattern, const char* defaultExt) {
    std::wstring filter = Widen(label ? label : "Files");
    filter.push_back(L'\0');
    filter += Widen(pattern ? pattern : "*.*");
    filter.push_back(L'\0');
    filter.push_back(L'\0');

    wchar_t fileBuffer[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = reinterpret_cast<HWND>(ownerHwnd);
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = fileBuffer;
    ofn.nMaxFile = static_cast<DWORD>(std::size(fileBuffer));
    const std::wstring wideTitle = Widen(title ? title : "Save");
    ofn.lpstrTitle = wideTitle.c_str();
    const std::wstring wideExt = Widen(defaultExt ? defaultExt : "");
    ofn.lpstrDefExt = wideExt.empty() ? nullptr : wideExt.c_str();
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (!GetSaveFileNameW(&ofn)) {
        return std::string();
    }
    return Narrow(std::wstring(fileBuffer));
}

std::string ReadFileUtf8(const std::string& utf8Path) {
    return ReadWholeFile(Widen(utf8Path));
}

bool WriteFileUtf8(const std::string& utf8Path, const std::string& contents) {
    std::ofstream out(Widen(utf8Path), std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    return out.good();
}

MmdExportResult RunMmdExport(const MmdDanceConfig& config) {
    MmdExportResult result;

    if (config.vmdPath.empty() || config.modelPath.empty()) {
        result.message = "Choose both a VMD motion and a PMX/PMD model.";
        return result;
    }

    const std::wstring blender = config.blenderPath.empty() ? DetectBlenderExeW() : Widen(config.blenderPath);
    if (blender.empty() || !FileExists(blender)) {
        result.message = "Blender was not found. Install Blender or set its path.";
        return result;
    }

    const std::wstring script = ScriptPath();
    if (!FileExists(script)) {
        result.message = "blender_export_mmd.py was not found next to the UI.";
        return result;
    }

    const std::wstring vmd = Widen(config.vmdPath);
    const std::wstring model = Widen(config.modelPath);
    if (!FileExists(vmd)) {
        result.message = "The VMD motion file does not exist.";
        return result;
    }
    if (!FileExists(model)) {
        result.message = "The model file does not exist.";
        return result;
    }

    const std::wstring output = SolvedOutputPath();
    DeleteFileW(output.c_str());  // drop any prior export so a failure is detectable
    const std::wstring mmdTools = config.mmdToolsPath.empty() ? DetectMmdToolsW() : Widen(config.mmdToolsPath);

    wchar_t fpsText[32] = {};
    swprintf(fpsText, static_cast<int>(std::size(fpsText)), L"%g", config.fps > 0.0f ? config.fps : 60.0f);

    std::wstring command;
    command += L"\"" + blender + L"\"";
    command += L" --background";
    command += L" --python \"" + script + L"\"";
    command += L" --";
    command += L" --model \"" + model + L"\"";
    command += L" --vmd \"" + vmd + L"\"";
    command += L" --output \"" + output + L"\"";
    command += L" --fps ";
    command += fpsText;
    if (!mmdTools.empty()) {
        command += L" --mmd-tools-path \"" + mmdTools + L"\"";
    }

    std::string captured;
    const int exitCode = RunCaptured(command, captured);
    if (exitCode < 0) {
        result.message = "Could not launch Blender.";
        return result;
    }

    result.solvedJson = ReadWholeFile(output);
    result.solvedJsonPath = Narrow(output);
    if (exitCode != 0 || result.solvedJson.empty()) {
        const std::string detail = LastLines(captured, 240);
        result.message = "Blender solve failed" + (detail.empty() ? "." : (": " + detail));
        return result;
    }

    result.ok = true;
    result.message = "Solved.";
    return result;
}

} // namespace anyadance::ui
