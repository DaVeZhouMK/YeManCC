#pragma once

#include <windows.h>
#include <wincrypt.h>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include "json.hpp"

namespace ymcc::sleepLegacyResume {

enum class Result { None, Cleaned, Pending };
enum class Operation { Resume, Pause };
using Json = nlohmann::json;
namespace fs = std::filesystem;

struct Close {
    void operator()(void* value) const noexcept { CloseHandle(value); }
};
using File = std::unique_ptr<void, Close>;

inline File open(const fs::path& path, DWORD access, DWORD share, DWORD how, DWORD flags) {
    const auto handle = CreateFileW(path.c_str(), access, share, nullptr, how, flags, nullptr);
    return File(handle == INVALID_HANDLE_VALUE ? nullptr : handle);
}

inline bool plain(HANDLE handle, bool directory) {
    BY_HANDLE_FILE_INFORMATION info{};
    return GetFileInformationByHandle(handle, &info) &&
           !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
           static_cast<bool>(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == directory &&
           (directory || info.nNumberOfLinks == 1);
}

enum class Presence { Absent, Present, Unknown };
inline Presence presence(const fs::path& path) {
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return Presence::Present;
    const auto error = GetLastError();
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND
        ? Presence::Absent : Presence::Unknown;
}

struct Paths {
    fs::path marker, pending, journal, lock;
};
inline Paths paths(const fs::path& directory, DWORD pid) {
    const auto name = std::to_wstring(pid);
    return {directory / (name + L".txt"), directory / (L"." + name + L".suspend-pending.json"),
            directory / (L"." + name + L".resume.json"), directory / (L"." + name + L".suspend.lock")};
}

inline bool noOwnershipRecord(const Paths& files) {
    return presence(files.marker) == Presence::Absent && presence(files.pending) == Presence::Absent &&
           presence(files.journal) == Presence::Absent;
}

inline bool pinPlainDirectory(const fs::path& directory, std::vector<File>& pins) {
    if (!directory.is_absolute() || directory != directory.lexically_normal()) return false;
    std::vector<fs::path> parents;
    for (auto path = directory; !path.empty(); path = path.parent_path()) {
        if (parents.size() == 64) return false;
        parents.push_back(path);
        if (path.parent_path() == path) break;
    }
    for (auto it = parents.rbegin(); it != parents.rend(); ++it) {
        auto pin = open(*it, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING,
                        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT);
        if (!pin || !plain(pin.get(), true)) return false;
        pins.push_back(std::move(pin));
    }
    return true;
}

struct Snapshot {
    fs::path path;
    File file;
    std::string bytes;
    std::string token;
};

inline std::uint64_t fileTime(FILETIME time) noexcept {
    return (std::uint64_t{time.dwHighDateTime} << 32) | time.dwLowDateTime;
}

inline bool read(Snapshot& out, DWORD limit) {
    out.file = open(out.path, GENERIC_READ | DELETE, 0, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT);
    if (!out.file) return GetLastError() == ERROR_FILE_NOT_FOUND;
    BY_HANDLE_FILE_INFORMATION info{};
    if (!plain(out.file.get(), false) || !GetFileInformationByHandle(out.file.get(), &info) ||
        info.nFileSizeHigh || !info.nFileSizeLow || info.nFileSizeLow > limit) return false;
    out.bytes.resize(info.nFileSizeLow);
    DWORD received = 0;
    if (!ReadFile(out.file.get(), out.bytes.data(), info.nFileSizeLow, &received, nullptr) ||
        received != info.nFileSizeLow) return false;
    // PowerShell records DateTime.Ticks, which start at 0001 rather than 1601.
    constexpr std::uint64_t epoch = 504911232000000000ULL;
    const auto created = fileTime(info.ftCreationTime), written = fileTime(info.ftLastWriteTime);
    if (!created || !written || created > 2650467743999999999ULL || written > 2650467743999999999ULL)
        return false;
    const auto data = reinterpret_cast<const BYTE*>(out.bytes.data());
    constexpr DWORD flags = CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF;
    DWORD size = 0;
    if (!CryptBinaryToStringA(data, received, flags, nullptr, &size) || !size) return false;
    std::string encoded(size, '\0');
    if (!CryptBinaryToStringA(data, received, flags, encoded.data(), &size)) return false;
    encoded.resize(size);
    out.token = std::to_string(created + epoch) + ':' + std::to_string(written + epoch) + ':' +
                std::to_string(received) + ':' + encoded;
    return true;
}

inline std::string legacyPath(const fs::path& path) {
    const auto& value = path.native();
    if (value.empty() || value.size() > 32767) return {};
    const auto count = static_cast<int>(value.size());
    const int mapped = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, value.data(), count,
                                     nullptr, 0, nullptr, nullptr, 0);
    if (mapped <= 0) return {};
    std::wstring lower(mapped, L'\0');
    if (LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, value.data(), count,
                      lower.data(), mapped, nullptr, nullptr, 0) != mapped) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, lower.data(), mapped,
                                           nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string result(needed, '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, lower.data(), mapped, result.data(),
                            needed, nullptr, nullptr) != needed) return {};
    return result;
}

inline bool decimalFileTime(std::string_view value) {
    constexpr std::string_view max = "9223372036854775807";
    return !value.empty() && value.front() != '0' && value.size() <= max.size() &&
           value.find_first_not_of("0123456789") == value.npos && (value.size() < max.size() || value <= max);
}

inline bool guid(std::string_view value) {
    if (value.size() != 36) return false;
    bool nonzero = false;
    for (std::size_t i = 0; i != value.size(); ++i) {
        const char c = value[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (c != '-') return false; }
        else {
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
            nonzero = nonzero || c != '0';
        }
    }
    return nonzero;
}

inline bool markerIdentity(std::string_view text, DWORD pid, std::string_view birth) {
    unsigned matched = 0;
    const auto pidText = std::to_string(pid);
    while (!text.empty()) {
        const auto end = text.find('|');
        const auto field = text.substr(0, end);
        const auto sep = field.find('=');
        if (sep == 0 || sep == field.npos || sep + 1 == field.size()) return false;
        const auto name = field.substr(0, sep), value = field.substr(sep + 1);
        unsigned bit = 0;
        if (name == "pid") { bit = 1; if (value != pidText) return false; }
        if (name == "created") { bit = 2; if (value != birth) return false; }
        if (name == "state") { bit = 4; if (value != "suspended") return false; }
        if (matched & bit) return false;
        matched |= bit;
        if (end == text.npos) break;
        text.remove_prefix(end + 1);
    }
    return matched == 7;
}

inline bool completed(const Snapshot& journal, const Snapshot& marker, const Snapshot& pending, DWORD pid,
                      std::uint64_t& completedBirth) {
    std::set<std::string> keys;
    auto record = Json::parse(journal.bytes, [&](int depth, Json::parse_event_t event, Json& field) {
        if (event == Json::parse_event_t::array_start || (event == Json::parse_event_t::object_start && depth != 0))
            throw std::invalid_argument("non-flat legacy journal");
        if (event == Json::parse_event_t::key && !keys.insert(field.get<std::string>()).second)
            throw std::invalid_argument("duplicate legacy journal key");
        return true;
    }, false);
    if (!record.is_object() || record.size() != 10) return false;
    const auto& schema = record.at("schemaVersion");
    const auto& number = record.at("pid");
    const auto& nt = record.at("ntStatus");
    const auto& phase = record.at("phase");
    if (!schema.is_number_integer() || schema != 1 || !number.is_number_integer() || number != pid ||
        !nt.is_number_integer() || nt != 0 || phase != "completed") return false;
    using String = const std::string&;
    const auto& birth = record.at("processCreated").get_ref<String>();
    const auto& token = record.at("markerToken").get_ref<String>();
    const auto& pendingToken = record.at("pendingToken").get_ref<String>();
    const auto markerName = legacyPath(marker.path), pendingName = legacyPath(pending.path);
    const bool matches = decimalFileTime(birth) && guid(record.at("operation").get_ref<String>()) && !token.empty() &&
           !markerName.empty() && !pendingName.empty() && record.at("markerPath") == markerName &&
           record.at("pendingPath") == pendingName &&
           (!marker.file || (marker.token == token && markerIdentity(marker.bytes, pid, birth))) &&
           (!pending.file || pending.token == pendingToken);
    if (matches) completedBirth = std::stoull(birth);
    return matches;
}

inline bool retire(Snapshot& record) {
    if (!record.file) return true;
    FILE_DISPOSITION_INFO disposition{TRUE};
    const auto handle = record.file.get();
    if (!SetFileInformationByHandle(handle, FileDispositionInfo, &disposition, sizeof(disposition))) return false;
    record.file.reset();
    return presence(record.path) == Presence::Absent;
}

struct ResumeLease {
    Result result = Result::Pending;
    std::uint64_t completedBirth = 0;
    std::vector<File> pins;
    File localLock, otherLock;
    fs::path absentPeer;

    // The caller still owns power admission, marker identity and the NT action.
    template<class Current>
    bool current(const Current& admitted) const {
        return result == Result::None && localLock && admitted() &&
            (absentPeer.empty() || presence(absentPeer) == Presence::Absent) && admitted();
    }
};

inline File openLegacyLock(const fs::path& path) {
    auto lock = open(path, GENERIC_READ | GENERIC_WRITE | DELETE, 0, CREATE_NEW,
                     FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_DELETE_ON_CLOSE);
    if (!lock) {
        const DWORD error = GetLastError();
        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) return {};
        lock = open(path, GENERIC_READ | GENERIC_WRITE, 0, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT);
    }
    if (!lock || !plain(lock.get(), false)) return {};
    return lock;
}

// Retain the legacy writer's locks through the native call and marker settlement.
template<class Current>
ResumeLease acquire(const fs::path& directory, const fs::path& otherDirectory, DWORD pid,
                    const Current& current, std::uint64_t expectedCompletedBirth = 0,
                    Operation operation = Operation::Resume,
                    const std::function<bool(const Paths&)>& localCurrent = {}) noexcept {
    try {
        if (!pid || pid > 0x7fffffff || directory == otherDirectory || !current()) return {};
        const auto local = paths(directory, pid);
        const auto other = paths(otherDirectory, pid);
        ResumeLease lease;
        if (!pinPlainDirectory(directory, lease.pins)) return {};
        const auto otherPresence = presence(otherDirectory);
        if (otherPresence == Presence::Unknown || (otherPresence == Presence::Present &&
            !pinPlainDirectory(otherDirectory, lease.pins))) return {};
        if (!current()) return {};
        lease.localLock = openLegacyLock(local.lock);
        if (!lease.localLock) return {};
        if (otherPresence == Presence::Present) {
            if (!current()) return {};
            lease.otherLock = openLegacyLock(other.lock);
            if (!lease.otherLock) return {};
        } else {
            lease.absentPeer = otherDirectory;
        }
        const auto ownershipCurrent = [&] {
            return current() && (lease.absentPeer.empty() || presence(lease.absentPeer) == Presence::Absent);
        };
        if (!noOwnershipRecord(other) || !ownershipCurrent() ||
            (localCurrent && !localCurrent(local)) || !ownershipCurrent()) return {};
        if (presence(local.journal) == Presence::Absent) {
            if (presence(local.pending) != Presence::Absent || !ownershipCurrent()) return {};
            lease.result = Result::None;
            return lease;
        }
        if (operation == Operation::Pause) return {};
        Snapshot journal{local.journal}, marker{local.marker}, pending{local.pending};
        if (!ownershipCurrent() || !read(journal, 262144) || !journal.file || !read(marker, 65536) ||
            !read(pending, 65536) || !completed(journal, marker, pending, pid, lease.completedBirth)) return {};
        if (expectedCompletedBirth && lease.completedBirth != expectedCompletedBirth) return {};
        // Keep the journal until both members are absent. A later admitted request can finish a partial cleanup.
        if (!ownershipCurrent() || !retire(marker) || !ownershipCurrent() || !retire(pending) ||
            !ownershipCurrent() || !retire(journal)) return {};
        lease.result = Result::Cleaned;
        return lease;
    } catch (...) { return {}; }
}

template<class Current>
Result recoverCompleted(const fs::path& directory, const fs::path& otherDirectory, DWORD pid,
                        const Current& current) noexcept {
    return acquire(directory, otherDirectory, pid, current).result;
}

} // namespace ymcc::sleepLegacyResume
