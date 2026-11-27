#pragma once

#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <algorithm>
#include <filesystem>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace custom_steam_library::launch_file {
namespace fs = std::filesystem;

inline constexpr wchar_t scriptName[] = L"StartGame.vbs";
inline constexpr size_t maxCodeUnits = 64 * 1024;

struct Configuration {
    fs::path gameRoot;
    fs::path primaryExecutable;
    fs::path source;
    fs::path workingDirectory;
    std::wstring command;
    bool manual = false;
};

struct ShortcutLaunch {
    fs::path target;
    fs::path workingDirectory;
    std::wstring arguments;
};

inline bool equal(std::wstring_view a, std::wstring_view b) {
    return !a.empty() && a.size() == b.size() &&
        CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

inline bool batchExtension(std::wstring_view extension) {
    return equal(extension, L".bat") || equal(extension, L".cmd");
}

inline bool samePath(const fs::path& a, const fs::path& b) {
    return equal(a.lexically_normal().make_preferred().native(), b.lexically_normal().make_preferred().native());
}

inline bool absolutePath(const fs::path& path) {
    const auto& value = path.native();
    return path.is_absolute() && !value.starts_with(L"\\\\?\\") && !value.starts_with(L"\\\\.\\") &&
        value.find_first_of(std::wstring_view(L"\0\"\r\n", 4)) == std::wstring::npos;
}

inline std::wstring launchOverridePathKey(const fs::path& path) {
    if (!absolutePath(path)) return {};
    std::error_code error;
    auto normalized = fs::weakly_canonical(path, error);
    if (error) normalized = path;
    auto key = normalized.lexically_normal().make_preferred().native();
    if (!key.empty()) CharLowerBuffW(key.data(), static_cast<DWORD>(key.size()));
    while (key.size() > 3 && key.back() == L'\\') key.pop_back();
    return key;
}

template<class Json>
struct SavedLaunchConfiguration {
    Json value;
    bool exact = false;
};

template<class Json, class ToWide>
std::map<std::wstring, SavedLaunchConfiguration<Json>> indexSavedLaunchConfigurations(const Json& overrides, ToWide toWide) {
    std::map<std::wstring, SavedLaunchConfiguration<Json>> result;
    const auto items = overrides.find("items");
    if (items == overrides.end() || !items->is_object()) return result;
    for (auto it = items->begin(); it != items->end(); ++it) {
        const auto config = it->find("launchConfiguration");
        if (config == it->end()) continue;
        const auto stored = toWide(it.key());
        const auto key = launchOverridePathKey(fs::path(stored));
        if (key.empty()) continue;
        const bool exact = stored == key;
        auto [found, inserted] = result.try_emplace(key, SavedLaunchConfiguration<Json>{*config, exact});
        if (inserted) continue;
        if (exact) found->second = {*config, true};
        else if (!found->second.exact && found->second.value != *config)
            found->second.value = Json{}; // Conflicting legacy settings do not opt in to VBS.
    }
    return result;
}

inline fs::path systemFile(const wchar_t* fileName) {
    wchar_t buffer[MAX_PATH]{};
    const auto length = GetSystemDirectoryW(buffer, MAX_PATH);
    if (!length || length >= MAX_PATH) throw std::runtime_error("launch-system-directory-unavailable");
    return fs::path(buffer) / fileName;
}

inline fs::path wrapperPath(const fs::path& gameRoot) {
    return gameRoot / scriptName;
}

inline bool usableWrapper(const fs::path& script) {
    const auto attributes = GetFileAttributesW(script.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    std::error_code error;
    const auto size = fs::file_size(script, error);
    return !error && size && size <= 4u * 1024 * 1024;
}

inline bool mayCreateWrapper(const fs::path& script) {
    return GetFileAttributesW(script.c_str()) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND;
}

inline std::wstring steamOptions(const fs::path& script) {
    if (!absolutePath(script) || !equal(script.filename().native(), scriptName))
        throw std::runtime_error("launch-script-path-invalid");
    return L"//B //NoLogo \"" + script.native() + L"\"";
}

inline fs::path steamScriptOperand(std::wstring_view target, std::wstring_view options) {
    if (target.size() >= 2 && target.front() == L'"' && target.back() == L'"') {
        target.remove_prefix(1);
        target.remove_suffix(1);
    }
    if (!samePath(fs::path(target), systemFile(L"wscript.exe")) || options.empty() ||
        options.find(L'\0') != std::wstring_view::npos) return {};
    const auto line = L"wscript.exe " + std::wstring(options);
    int count = 0;
    wchar_t** raw = CommandLineToArgvW(line.c_str(), &count);
    if (!raw) return {};
    const std::unique_ptr<void, decltype(&LocalFree)> args(raw, LocalFree);
    if (count != 4 || !equal(raw[1], L"//B") || !equal(raw[2], L"//NoLogo")) return {};
    const fs::path script(raw[3]);
    return absolutePath(script) && equal(script.filename().native(), scriptName) ? script : fs::path{};
}

inline std::wstring literal(std::wstring_view value) {
    std::wstring result(1, L'"');
    for (const auto ch : value) {
        if (ch == L'"') result.push_back(ch);
        result.push_back(ch);
    }
    result.push_back(L'"');
    return result;
}

inline void appendCommand(std::wstring& script, std::wstring_view code) {
    // VBScript line length is bounded; preserve the command verbatim as UTF-8
    // batch text instead of trying to translate CMD grammar to VBScript.
    script += L"body = \"@chcp 65001 >nul\" & vbCrLf\r\n";
    while (!code.empty()) {
        const auto end = code.find_first_of(L"\r\n");
        auto part = code.substr(0, end);
        while (!part.empty()) {
            const auto units = (std::min)(part.size(), size_t{200});
            script += L"body = body & " + literal(part.substr(0, units)) + L"\r\n";
            part.remove_prefix(units);
        }
        script += L"body = body & vbCrLf\r\n";
        if (end == std::wstring_view::npos) break;
        code.remove_prefix(end + (code[end] == L'\r' && end + 1 < code.size() && code[end + 1] == L'\n' ? 2 : 1));
    }
}

inline ShortcutLaunch readShortcut(const fs::path& source) {
    struct Apartment {
        HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
    } apartment;
    if (FAILED(apartment.result) && apartment.result != RPC_E_CHANGED_MODE)
        throw std::runtime_error("launch-shortcut-reader-unavailable");
    Microsoft::WRL::ComPtr<IShellLinkW> link;
    Microsoft::WRL::ComPtr<IPersistFile> file;
    if (FAILED(CoCreateInstance(__uuidof(ShellLink), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))) ||
        FAILED(link.As(&file)) || FAILED(file->Load(source.c_str(), STGM_READ)))
        throw std::runtime_error("launch-shortcut-unreadable");

    // Do not call Resolve: it can search for a moved target or prompt the user.
    std::wstring buffer(32768, L'\0');
    const auto count = static_cast<int>(buffer.size());
    ShortcutLaunch result;
    if (link->GetPath(buffer.data(), count, nullptr, 0) != S_OK || !buffer.front())
        throw std::runtime_error("launch-shortcut-target-unavailable");
    result.target = buffer.c_str();
    buffer.front() = L'\0';
    if (FAILED(link->GetWorkingDirectory(buffer.data(), count)))
        throw std::runtime_error("launch-shortcut-working-directory-unreadable");
    result.workingDirectory = buffer.c_str();
    buffer.front() = L'\0';
    if (FAILED(link->GetArguments(buffer.data(), count)))
        throw std::runtime_error("launch-shortcut-arguments-unreadable");
    result.arguments = buffer.c_str();
    if (result.arguments.size() >= buffer.size() - 1 || result.arguments.find_first_of(L"\r\n") != std::wstring::npos)
        throw std::runtime_error("launch-shortcut-arguments-invalid");
    return result;
}

inline Configuration validated(Configuration config, std::wstring* shortcutArguments = nullptr) {
    if (!absolutePath(config.gameRoot) || !fs::is_directory(config.gameRoot))
        throw std::runtime_error("launch-game-directory-unavailable");
    if (!absolutePath(config.primaryExecutable) || !equal(config.primaryExecutable.extension().native(), L".exe") ||
        !fs::is_regular_file(config.primaryExecutable)) throw std::runtime_error("launch-primary-executable-unavailable");
    if (config.manual) {
        if (config.command.size() > maxCodeUnits || config.command.find(L'\0') != std::wstring::npos ||
            config.command.find_first_not_of(L" \t\r\n") == std::wstring::npos)
            throw std::runtime_error("launch-command-invalid");
        if (config.workingDirectory.empty()) config.workingDirectory = config.gameRoot;
    } else {
        if (config.source.empty()) config.source = config.primaryExecutable;
        if (!absolutePath(config.source) || !fs::is_regular_file(config.source))
            throw std::runtime_error("launch-source-unavailable");
        const bool isShortcut = equal(config.source.extension().native(), L".lnk");
        if (isShortcut) {
            auto link = readShortcut(config.source);
            config.source = std::move(link.target);
            if (!absolutePath(config.source) || !fs::is_regular_file(config.source))
                throw std::runtime_error("launch-shortcut-target-unavailable");
            if (config.workingDirectory.empty()) config.workingDirectory = std::move(link.workingDirectory);
            if (shortcutArguments) *shortcutArguments = std::move(link.arguments);
        }
        const auto wrapper = wrapperPath(config.gameRoot);
        std::error_code error;
        if (samePath(fs::weakly_canonical(config.source), fs::weakly_canonical(wrapper)) ||
            fs::equivalent(config.source, wrapper, error))
            throw std::runtime_error("launch-source-recursive");
        const auto ext = config.source.extension().native();
        if (isShortcut && !equal(ext, L".exe") && !equal(ext, L".com") && !batchExtension(ext))
            throw std::runtime_error("launch-shortcut-target-not-executable: select the script directly or use manual mode");
        if (config.workingDirectory.empty()) config.workingDirectory = config.source.parent_path();
    }
    if (!absolutePath(config.workingDirectory) || !fs::is_directory(config.workingDirectory))
        throw std::runtime_error("launch-working-directory-unavailable");
    return config;
}

inline std::vector<unsigned char> scriptBytes(Configuration config) {
    std::wstring shortcutArguments;
    config = validated(std::move(config), &shortcutArguments);
    const auto ext = config.source.extension().native();
    const bool batch = config.manual || batchExtension(ext);
    const bool visualBasic = equal(ext, L".vbs") || equal(ext, L".vbe");
    std::wstring text = L"Option Explicit\r\n"
        L"' Generated by YeManCC. The Steam card identity is not stored here.\r\n"
        L"Dim runner, fso, entry, status, env, quote, ref, body, temp, output, bytes\r\n"
        L"Set runner = CreateObject(\"WScript.Shell\")\r\n"
        L"Set fso = CreateObject(\"Scripting.FileSystemObject\")\r\n"
        L"runner.CurrentDirectory = " + literal(config.workingDirectory.native()) + L"\r\n"
        L"quote = Chr(34)\r\n"
        L"status = 0\r\n";
    if (config.manual) {
        appendCommand(text, config.command);
        text += L"temp = fso.BuildPath(runner.CurrentDirectory, fso.GetTempName & \".cmd\")\r\n"
            L"Set output = fso.CreateTextFile(temp, False)\r\n"
            L"output.Close\r\n"
            L"Set output = CreateObject(\"ADODB.Stream\")\r\n"
            L"output.Type = 2\r\noutput.Charset = \"utf-8\"\r\noutput.Open\r\noutput.WriteText body\r\n"
            L"output.Position = 0\r\noutput.Type = 1\r\noutput.Position = 3\r\n"
            L"Set bytes = CreateObject(\"ADODB.Stream\")\r\nbytes.Type = 1\r\nbytes.Open\r\n"
            L"output.CopyTo bytes\r\nbytes.SaveToFile temp, 2\r\nbytes.Close\r\noutput.Close\r\n"
            L"entry = temp\r\n";
    } else {
        text += L"entry = " + literal(config.source.native()) + L"\r\n"
            L"If Not fso.FileExists(entry) Then WScript.Quit 2\r\n";
    }

    if (batch || visualBasic || equal(ext, L".exe") || equal(ext, L".com")) {
        text += L"Set env = runner.Environment(\"Process\")\r\n"
            L"env(\"YMCC_VBS_LAUNCH_ENTRY\") = entry\r\n";
        if (!shortcutArguments.empty()) {
            // Pass the argument tail as data so WshShell.Run does not expand its percent signs.
            text += L"ref = \"\"\r\n";
            const std::wstring_view args(shortcutArguments);
            for (size_t i = 0; i < args.size(); i += 200)
                text += L"ref = ref & " + literal(args.substr(i, 200)) + L"\r\n";
            text += L"env(\"YMCC_VBS_LAUNCH_ARGS\") = ref\r\n";
        }
        if (batch) {
            text += L"env(\"YMCC_VBS_LITERAL_PERCENT\") = \"%\"\r\n"
                L"env(\"YMCC_VBS_CMD\") = " + literal(systemFile(L"cmd.exe").native()) + L"\r\n"
                L"ref = \"%YMCC_VBS_LITERAL_PERCENT%YMCC_VBS_LAUNCH_ENTRY%YMCC_VBS_LITERAL_PERCENT%\"\r\n"
                L"On Error Resume Next\r\n"
                L"status = runner.Run(quote & \"%YMCC_VBS_CMD%\" & quote & \" /d /v:off /s /c \" & quote & quote & ref & quote";
            if (!shortcutArguments.empty())
                text += L" & \" %YMCC_VBS_LITERAL_PERCENT%YMCC_VBS_LAUNCH_ARGS%YMCC_VBS_LITERAL_PERCENT%\"";
            text += L" & quote, 1, True)\r\n";
        } else if (visualBasic) {
            // A launcher script must execute, not follow the user's file association.
            text += L"env(\"YMCC_VBS_WSCRIPT\") = " + literal(systemFile(L"wscript.exe").native()) + L"\r\n"
                L"On Error Resume Next\r\n"
                L"status = runner.Run(quote & \"%YMCC_VBS_WSCRIPT%\" & quote & \" //B //NoLogo \" & quote & \"%YMCC_VBS_LAUNCH_ENTRY%\" & quote, 1, True)\r\n";
        } else if (shortcutArguments.empty()) {
            text += L"On Error Resume Next\r\n"
                L"status = runner.Run(quote & \"%YMCC_VBS_LAUNCH_ENTRY%\" & quote, 1, True)\r\n";
        } else {
            text += L"On Error Resume Next\r\n"
                L"status = runner.Run(quote & \"%YMCC_VBS_LAUNCH_ENTRY%\" & quote & \" %YMCC_VBS_LAUNCH_ARGS%\", 1, True)\r\n";
        }
        text += L"If Err.Number <> 0 Then status = 1\r\n";
        if (config.manual) text += L"fso.DeleteFile temp\r\n";
        text += L"On Error GoTo 0\r\n";
    } else {
        text += L"On Error Resume Next\r\n"
            L"CreateObject(\"Shell.Application\").ShellExecute entry, \"\", runner.CurrentDirectory, \"open\", 1\r\n"
            L"If Err.Number <> 0 Then status = 1\r\nOn Error GoTo 0\r\n";
    }
    text += L"WScript.Quit status\r\n";
    std::vector<unsigned char> output{0xFF, 0xFE};
    output.reserve(2 + text.size() * 2);
    for (const auto unit : text) {
        output.push_back(static_cast<unsigned char>(unit & 0xff));
        output.push_back(static_cast<unsigned char>(unit >> 8));
    }
    return output;
}

} // namespace custom_steam_library::launch_file
