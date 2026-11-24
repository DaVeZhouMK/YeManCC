#pragma once
#include <windows.h>
#include <algorithm>
#include <array>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

namespace ymcc::rtssinstall {
inline constexpr wchar_t uninstall[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
struct Result { std::wstring directory; bool known = true; };
struct Key {
    HKEY value = nullptr;
    Key() = default;
    Key(const Key&) = delete;
    Key& operator=(const Key&) = delete;
    ~Key() { if (value) RegCloseKey(value); }
};

inline std::wstring text(HKEY key, const wchar_t* name) {
    DWORD bytes = 0;
    constexpr DWORD flags = RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ;
    if (RegGetValueW(key, nullptr, name, flags, nullptr, nullptr, &bytes) != ERROR_SUCCESS ||
        bytes < sizeof(wchar_t) || bytes > 32768 * sizeof(wchar_t)) return {};
    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    if (RegGetValueW(key, nullptr, name, flags, nullptr, value.data(), &bytes) != ERROR_SUCCESS) return {};
    if (const auto end = value.find(L'\0'); end != std::wstring::npos) value.resize(end);
    return value;
}

inline bool displayNameMatches(std::wstring_view value) {
    for (const std::wstring_view name : {std::wstring_view(L"RivaTuner Statistics Server"), std::wstring_view(L"RTSS")}) {
        for (size_t i = 0; i + name.size() <= value.size(); ++i) {
            if (CompareStringOrdinal(value.data() + i, static_cast<int>(name.size()),
                    name.data(), static_cast<int>(name.size()), TRUE) == CSTR_EQUAL) return true;
        }
    }
    return false;
}

inline bool file(const std::wstring& path) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

inline std::wstring directoryForImage(std::wstring image) {
    std::replace(image.begin(), image.end(), L'/', L'\\');
    if (image.size() < 4 || !((image[0] >= L'A' && image[0] <= L'Z') ||
        (image[0] >= L'a' && image[0] <= L'z')) || image[1] != L':' || image[2] != L'\\' ||
        image.find_first_of(L"*?\"") != std::wstring::npos) return {};
    const size_t separator = image.rfind(L'\\');
    const auto prefix = image.substr(0, separator + 1);
    if (!file(image) || !file(prefix + L"RTSS.exe") || !file(prefix + L"RTSSHooks64.dll")) return {};
    return image.substr(0, separator == 2 ? 3 : separator);
}

inline Result readUninstall(HKEY hive, const wchar_t* path, REGSAM view) {
    Key root;
    const auto opened = RegOpenKeyExW(hive, path, 0, KEY_ENUMERATE_SUB_KEYS | view, &root.value);
    if (opened == ERROR_FILE_NOT_FOUND || opened == ERROR_PATH_NOT_FOUND) return {};
    if (opened != ERROR_SUCCESS) return {{}, false};
    bool complete = true;
    for (DWORD index = 0; index < 8192; ++index) {
        wchar_t name[256]{};
        DWORD count = static_cast<DWORD>(std::size(name));
        const auto enumerated = RegEnumKeyExW(root.value, index, name, &count, nullptr, nullptr, nullptr, nullptr);
        if (enumerated == ERROR_NO_MORE_ITEMS) return {{}, complete};
        if (enumerated != ERROR_SUCCESS) return {{}, false};
        Key entry;
        const auto child = RegOpenKeyExW(root.value, name, 0, KEY_QUERY_VALUE | view, &entry.value);
        if (child != ERROR_SUCCESS) { complete = false; continue; }
        if (!displayNameMatches(text(entry.value, L"DisplayName"))) continue;

        auto image = text(entry.value, L"InstallLocation");
        if (!image.empty()) {
            if (image.back() != L'\\' && image.back() != L'/') image += L'\\';
            image += L"RTSS.exe";
        } else {
            image = text(entry.value, L"DisplayIcon");
            if (const auto comma = image.find(L','); comma != std::wstring::npos) image.resize(comma);
            const auto first = image.find_first_not_of(L'"');
            if (first == std::wstring::npos) continue;
            image = image.substr(first, image.find_last_not_of(L'"') - first + 1);
        }
        auto directory = directoryForImage(std::move(image));
        if (!directory.empty()) return {std::move(directory), true};
    }
    return {{}, false};
}

inline Result read() {
    bool complete = true;
    for (const auto& source : std::array<std::pair<HKEY, REGSAM>, 3>{{
             {HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY}, {HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY}, {HKEY_CURRENT_USER, KEY_WOW64_64KEY}}}) {
        auto result = readUninstall(source.first, uninstall, source.second);
        if (!result.directory.empty()) return result;
        complete = complete && result.known;
    }
    return {{}, complete};
}
}
