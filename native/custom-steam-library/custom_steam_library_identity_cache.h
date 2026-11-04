#pragma once
#include "json.hpp"
#include "custom_steam_library_exclusions.h"
#include <string>
#include <cstdint>
#include <windows.h>
#include <bcrypt.h>
#include <filesystem>
#include <set>
#include <vector>
#include <limits>
#include <functional>
#include <cwctype>
#include "custom_steam_library_paths.h"

namespace custom_steam_library {
// An image search, numeric ID, or display name is never verified identity.
inline bool verifiedSteamIdentityMatch(const nlohmann::json& match) {
    if (!match.is_object() || builtinExcludedSteamTool(match)) return false;
    // Known authority fields cannot turn malformed values into missing hints.
    for (const auto key : {"source", "query", "formalName", "name", "displayName", "identityKind", "contentRelation"})
        if (match.contains(key) && !match[key].is_string()) return false;
    for (const auto key : {"matchedContentAppId", "baseGameAppId"}) if (match.contains(key) && !match[key].is_null()) {
        const auto& value = match[key];
        if (!value.is_number_integer() || (value.is_number_unsigned() ? value.get<uint64_t>() > 2147483647ULL :
            value.get<int64_t>() < 0 || value.get<int64_t>() > 2147483647LL)) return false;
    }
    auto text = [&](const char* key) { const auto it=match.find(key); return it!=match.end()&&it->is_string()?it->get<std::string>():std::string{}; };
    auto number = [&](const char* key) -> int64_t {
        const auto it=match.find(key); if(it==match.end()||!it->is_number_integer())return 0;
        try { const auto value=it->get<int64_t>(); return value>0&&value<=2147483647?value:0; } catch(...) { return 0; }
    };
    // Historical builds labeled local INI title comments as official offline
    // verification. Preserve their images, but require a real official lookup
    // before these candidate IDs can promote a new Steam import.
    if (text("source") == "local-install-metadata-offline") return false;
    const auto source = text("source"), query = text("query");
    if (source.starts_with("parent-") || source.starts_with("exe-")) {
        auto asciiKey = [](const std::string& value) {
            std::string result;
            for (unsigned char c : value) {
                if (c >= 128) return std::string(8, 'x');
                if (c >= 'A' && c <= 'Z') result += static_cast<char>(c + ('a' - 'A'));
                else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) result += static_cast<char>(c);
            }
            return result;
        };
        const auto key = asciiKey(query);
        auto name = text("formalName"); if (name.empty()) name = text("name");
        if (name.empty()) name = text("displayName");
        const bool uppercase = query.size() >= 2 && std::all_of(query.begin(), query.end(), [](unsigned char c) { return c >= 'A' && c <= 'Z'; });
        if (!key.empty() && key.size() <= 4 && !name.empty() &&
            (key != asciiKey(name) || (uppercase && query != name))) return false;
        if (key == "game" || key == "launcher" || key == "start" || key == "main") return false;
    }
    const auto appId=number("appId");
    if(appId<=0||text("primaryProvider")!="steam"||text("identityStatus")!="verified"||text("steamVerificationStatus")!="steam-verified")return false;
    const auto version=text("resolverVersion");
    if(version!="steam-official-fuzzy-global-v4"&&version!="steam-official-fuzzy-global-v2")return false;
    if(match.contains("storefrontAppId")&&!match["storefrontAppId"].is_null()&&number("storefrontAppId")!=appId)return false;
    return number("matchedContentAppId")<=0 || (number("baseGameAppId")>0&&!text("contentRelation").empty()&&text("identityKind").find("with-explicit-content")!=std::string::npos);
}

namespace identity_cache {
namespace fs = std::filesystem;
using Json = nlohmann::json;
inline constexpr uint64_t byteLimit = 16 * 1024 * 1024;
inline void need(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() { if (value != INVALID_HANDLE_VALUE && value) CloseHandle(value); }
    Handle(const Handle&) = delete;
};
inline std::string executableKey(const std::string& text) {
    need(!text.empty() && text.size() <= 32760 && text.find('\0') == std::string::npos, "identity-path-invalid");
    const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    need(count > 0, "identity-path-encoding");
    std::wstring path(count, 0); MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), path.data(), count);
    std::replace(path.begin(), path.end(), L'/', L'\\');
    const bool drive = path.size() >= 4 && ((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) && path[1] == L':' && path[2] == L'\\';
    const bool unc = path.size() > 4 && path.starts_with(L"\\\\") && path[2] != L'?' && path[2] != L'.';
    need(drive || unc, "identity-path-not-absolute");
    size_t begin = drive ? 3 : 2; unsigned parts = 0;
    while (begin < path.size()) {
        const auto end = path.find(L'\\', begin); const auto part = path.substr(begin, end == std::wstring::npos ? end : end - begin);
        need(!part.empty() && part != L"." && part != L".." && part.back() != L'.' && part.back() != L' ', "identity-path-segment");
        need(part.find_first_of(L":*?\"<>|") == std::wstring::npos, "identity-path-stream");
        for (const auto c : part) need(c >= 32, "identity-path-control");
        ++parts; if (end == std::wstring::npos) break; begin = end + 1;
    }
    need(parts >= (unc ? 3u : 1u) && path.back() != L'\\', "identity-path-file-required");
    for (auto& c : path) c = static_cast<wchar_t>(towlower(c));
    const auto bytes = WideCharToMultiByte(CP_UTF8, 0, path.data(), static_cast<int>(path.size()), nullptr, 0, nullptr, nullptr);
    need(bytes > 0, "identity-path-encoding"); std::string result(bytes, 0);
    WideCharToMultiByte(CP_UTF8, 0, path.data(), static_cast<int>(path.size()), result.data(), bytes, nullptr, nullptr); return result;
}
inline std::wstring wide(const std::string& text) {
    const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    need(count > 0, "identity-path-encoding"); std::wstring value(count, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), value.data(), count); return value;
}
inline fs::path cachePath(const fs::path& root, const std::string& executable) {
    auto key = executableKey(executable);
    for (auto& c : key) if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') c = '_';
    need(key.size() <= 230, "identity-cache-key-too-long"); return root / L"cache" / L"identity" / wide(key + ".json");
}
inline void ancestry(const fs::path& file) {
    need(file.is_absolute(), "identity-storage-root-not-absolute");
    fs::path current;
    for (const auto& part : file) {
        need(part != L"." && part != L"..", "identity-storage-path-segment"); current /= part;
        const auto attributes = GetFileAttributesW(ioPath(current).c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            const auto error = GetLastError(); need(error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND, "identity-storage-inspect-failed");
        } else need(!(attributes & FILE_ATTRIBUTE_REPARSE_POINT), "identity-storage-reparse-refused");
    }
}
struct Snapshot {
    bool exists = false, oversized = false;
    BY_HANDLE_FILE_INFORMATION info{};
    std::string bytes;
    Json value = Json::object();
    bool parsed = false;
};
inline bool sameFile(const BY_HANDLE_FILE_INFORMATION& a, const BY_HANDLE_FILE_INFORMATION& b) {
    return a.dwVolumeSerialNumber == b.dwVolumeSerialNumber && a.nFileIndexHigh == b.nFileIndexHigh &&
        a.nFileIndexLow == b.nFileIndexLow && a.nFileSizeHigh == b.nFileSizeHigh && a.nFileSizeLow == b.nFileSizeLow &&
        a.ftLastWriteTime.dwHighDateTime == b.ftLastWriteTime.dwHighDateTime && a.ftLastWriteTime.dwLowDateTime == b.ftLastWriteTime.dwLowDateTime;
}
inline Json strictJson(const std::string& bytes) {
    std::vector<std::set<std::string>> objects; size_t events = 0;
    auto callback = [&](int depth, Json::parse_event_t event, Json& value) {
        need(depth <= 64 && ++events <= 200000, "identity-json-complexity");
        if (event == Json::parse_event_t::object_start) objects.emplace_back();
        else if (event == Json::parse_event_t::key) need(!objects.empty() && objects.back().insert(value.get<std::string>()).second, "identity-json-duplicate");
        else if (event == Json::parse_event_t::object_end) objects.pop_back();
        return true;
    };
    auto value = Json::parse(bytes, callback); need(value.is_object(), "identity-json-root"); return value;
}
inline Snapshot readSnapshot(const fs::path& file) {
    ancestry(file); Snapshot result;
    Handle handle(CreateFileW(ioPath(file).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (handle.value == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError(); need(error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND, "identity-read-unavailable"); return result;
    }
    result.exists = true; need(GetFileInformationByHandle(handle.value, &result.info) != FALSE, "identity-read-stat-failed");
    need(!(result.info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) && result.info.nNumberOfLinks == 1, "identity-read-type-refused");
    const auto size = (uint64_t(result.info.nFileSizeHigh) << 32) | result.info.nFileSizeLow;
    result.oversized = size > byteLimit;
    if (!result.oversized) {
        result.bytes.resize(static_cast<size_t>(size)); size_t offset = 0;
        while (offset < result.bytes.size()) {
            DWORD count = 0; const auto wanted = static_cast<DWORD>((std::min)(size_t(65536), result.bytes.size() - offset));
            need(ReadFile(handle.value, result.bytes.data() + offset, wanted, &count, nullptr) && count > 0, "identity-read-incomplete"); offset += count;
        }
        BY_HANDLE_FILE_INFORMATION after{}; need(GetFileInformationByHandle(handle.value, &after) && sameFile(result.info, after), "identity-read-changed");
        try { result.value = strictJson(result.bytes); result.parsed = true; } catch (...) { result.value = Json::object(); }
    }
    return result;
}
inline bool sameSnapshot(const Snapshot& a, const Snapshot& b) {
    return a.exists == b.exists && (!a.exists || (sameFile(a.info, b.info) && a.oversized == b.oversized && a.bytes == b.bytes));
}
inline bool futureSchema(const Json& value) {
    if (!value.is_object()) return false;
    if (value.contains("schemaVersion") && value["schemaVersion"].is_number_integer() && value["schemaVersion"] != 1) return true;
    return value.contains("schema") && value["schema"].is_string() && value["schema"] != "ymcc.library-identity-cache.v1";
}
inline bool knownSchema(const Json& value) {
    return value.is_object() && !futureSchema(value) &&
        (!value.contains("schemaVersion") || (value["schemaVersion"].is_number_integer() && value["schemaVersion"] == 1)) &&
        (!value.contains("schema") || (value["schema"].is_string() && value["schema"] == "ymcc.library-identity-cache.v1"));
}
inline int64_t integer(const Json& object, const char* key, int64_t fallback = 0) {
    const auto it = object.find(key); if (it == object.end()) return fallback;
    need(it->is_number_integer(), "identity-integer-invalid");
    if (it->is_number_unsigned()) need(it->get<uint64_t>() <= static_cast<uint64_t>((std::numeric_limits<int64_t>::max)()), "identity-integer-overflow");
    return it->get<int64_t>();
}
inline bool recordValid(const Snapshot& snapshot, const std::string& executable, int64_t time) {
    if (!snapshot.exists || !snapshot.parsed || !knownSchema(snapshot.value)) return false;
    const auto& value = snapshot.value;
    if (!value.contains("executable") || !value["executable"].is_string() || executableKey(value["executable"].get<std::string>()) != executableKey(executable)) return false;
    if (!value.contains("match") || !verifiedSteamIdentityMatch(value["match"])) return false;
    for (const auto key : {"formalName", "manifestPath"}) if (value.contains(key) && !value[key].is_string()) return false;
    if (value.contains("metadata") && !value["metadata"].is_object()) return false;
    const auto updated = integer(value, "updatedAt"); if (updated < 0 || updated > time + 300000) return false;
    if (value.contains("steamAppId") && integer(value, "steamAppId") != integer(value["match"], "appId")) return false;
    return true;
}
inline std::wstring nonce() {
    unsigned char bytes[16]{}; need(BCryptGenRandom(nullptr, bytes, sizeof bytes, BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0, "identity-entropy-failed");
    std::wstring result; for (const auto byte : bytes) { result += L"0123456789abcdef"[byte >> 4]; result += L"0123456789abcdef"[byte & 15]; } return result;
}
inline void commit(const fs::path& file, const Snapshot& before, const Json& value, bool preserveCorrupt, const std::function<void()>& observationGuard) {
    ancestry(file); std::error_code error; fs::create_directories(ioPath(file.parent_path()), error); need(!error, "identity-directory-unavailable"); ancestry(file);
    const auto bytes = value.dump(2); need(bytes.size() <= byteLimit, "identity-write-too-large");
    auto stage = file; stage += L".identity-next-" + nonce();
    try {
        {
            Handle output(CreateFileW(ioPath(stage).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr));
            need(output.value != INVALID_HANDLE_VALUE, "identity-stage-open-failed"); size_t offset = 0;
            while (offset < bytes.size()) { DWORD written = 0; const auto wanted = static_cast<DWORD>((std::min)(size_t(65536), bytes.size() - offset));
                need(WriteFile(output.value, bytes.data() + offset, wanted, &written, nullptr) && written == wanted, "identity-stage-write-failed"); offset += written; }
            need(FlushFileBuffers(output.value) != FALSE, "identity-stage-flush-failed");
        }
        const auto staged = readSnapshot(stage); need(staged.parsed && staged.value == value && staged.bytes == bytes, "identity-stage-readback-failed");
        observationGuard();
        need(sameSnapshot(before, readSnapshot(file)), "identity-concurrent-cache-change");
        if (preserveCorrupt && before.exists) {
            auto backup = file; backup += L".identity-corrupt-" + nonce();
            need(MoveFileExW(ioPath(file).c_str(), ioPath(backup).c_str(), MOVEFILE_WRITE_THROUGH) != FALSE, "identity-preserve-failed");
            need(sameSnapshot(before, readSnapshot(backup)), "identity-preserve-readback-failed");
            need(!readSnapshot(file).exists, "identity-concurrent-cache-created");
        }
        const auto flags = MOVEFILE_WRITE_THROUGH | ((before.exists && !preserveCorrupt) ? MOVEFILE_REPLACE_EXISTING : 0);
        need(MoveFileExW(ioPath(stage).c_str(), ioPath(file).c_str(), flags) != FALSE, "identity-publish-failed");
        const auto saved = readSnapshot(file); need(sameFile(staged.info, saved.info) && saved.bytes == bytes && saved.parsed && saved.value == value, "identity-publish-readback-failed");
    } catch (...) { std::error_code cleanup; fs::remove(ioPath(stage), cleanup); throw; }
    // Cooperating writers use the named mutex. Snapshot checks/readback detect
    // other writers where observable; this is not an OS expected-version CAS.
}
} // namespace identity_cache

} // namespace custom_steam_library
