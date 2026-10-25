// RTSS Global configuration observation, owned by the existing sole 1 Hz YMCC
// monitor publisher. This is NOT an SDK/hook-effective frame limiter receipt.
// No process/registry scan, timer, worker, or writer lives in this helper.
#pragma once
#include <windows.h>
#include <string>
#include <string_view>
#include <mutex>
#include <cstdint>
#include <cmath>
#include <utility>

namespace ymcc::monitorrtss {
constexpr std::size_t kMaxGlobalBytes = 64 * 1024;
constexpr double kMaxLimit = 1000000;
struct Limit { bool valid = false; double fps = 0; long long ts = 0; };
inline std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}
inline bool equalsAscii(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        auto c = a[i]; if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c != b[i]) return false;
    }
    return true;
}
inline bool integer(std::string_view s, std::uint32_t& value) {
    s = trim(s); if (s.empty()) return false;
    value = 0;
    for (char c : s) {
        if (c < '0' || c > '9' || value > (1000000u - (c - '0')) / 10u) return false;
        value = value * 10u + (c - '0');
    }
    return true;
}
// Duplicate sections/keys, signs, suffixes, missing Limit and zero denominators
// are unknown. A missing denominator is RTSS's integral-FPS form, NOT Limit=0.
inline Limit parseGlobal(std::string_view text) {
    if (text.empty() || text.size() > kMaxGlobalBytes || text.find('\0') != text.npos) return {};
    if (text.substr(0, 3) == "\xef\xbb\xbf") text.remove_prefix(3);
    bool in = false, section = false, haveLimit = false, haveDenom = false;
    std::uint32_t value = 0, denominator = 1;
    while (!text.empty()) {
        const auto end = text.find('\n');
        auto line = trim(text.substr(0, end));
        if (end == text.npos) text = {}; else text.remove_prefix(end + 1);
        if (line.empty() || line.front() == ';' || line.front() == '#') continue;
        if (line.front() == '[') {
            const auto close = line.find(']');
            if (close == line.npos) return {};
            const auto tail = trim(line.substr(close + 1));
            if (!tail.empty() && tail.front() != ';' && tail.front() != '#') return {};
            in = equalsAscii(trim(line.substr(1, close - 1)), "framerate");
            if (in && section) return {};
            if (in) section = true;
            continue;
        }
        if (!in) continue;
        const auto eq = line.find('=');
        if (eq == line.npos) return {}; // Damaged/incomplete target section.
        const auto key = trim(line.substr(0, eq));
        auto val = trim(line.substr(eq + 1));
        const auto comment = val.find_first_of(";#");
        if (comment != val.npos) {
            if (comment == 0 || (val[comment - 1] != ' ' && val[comment - 1] != '\t')) return {};
            val = trim(val.substr(0, comment));
        }
        if (equalsAscii(key, "limit")) {
            if (haveLimit || !integer(val, value)) return {};
            haveLimit = true;
        } else if (equalsAscii(key, "limitdenominator")) {
            if (haveDenom || !integer(val, denominator) || denominator == 0) return {};
            haveDenom = true;
        }
    }
    if (!section || !haveLimit) return {};
    return {true, static_cast<double>(value) / denominator, 0};
}

// Existing resolve/prepare/reload supplies a validated installation directory.
// Until that existing owner registers its path, publish unknown: do not guess
// a different standard installation when more than one copy exists.
inline std::mutex directoryMx;
inline std::wstring directory;
inline void registerDirectory(const std::wstring& dir) {
    std::lock_guard<std::mutex> lock(directoryMx); directory = dir;
}
inline std::wstring globalPath() {
    std::lock_guard<std::mutex> lock(directoryMx); return directory.empty() ? std::wstring{} : directory + L"\\Profiles\\Global";
}
struct FileState {
    DWORD volume = 0, indexHigh = 0, indexLow = 0, size = 0;
    FILETIME created{}, written{};
    bool operator==(const FileState& b) const {
        return volume == b.volume && indexHigh == b.indexHigh && indexLow == b.indexLow && size == b.size &&
            created.dwLowDateTime == b.created.dwLowDateTime && created.dwHighDateTime == b.created.dwHighDateTime &&
            written.dwLowDateTime == b.written.dwLowDateTime && written.dwHighDateTime == b.written.dwHighDateTime;
    }
};
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(const std::wstring& path) : value(CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)) {}
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete; Handle& operator=(const Handle&) = delete;
};
inline bool state(HANDLE handle, FileState& result) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (handle == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(handle, &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || info.nFileSizeHigh ||
        !info.nFileSizeLow || info.nFileSizeLow > kMaxGlobalBytes) return false;
    result = {info.dwVolumeSerialNumber, info.nFileIndexHigh, info.nFileIndexLow, info.nFileSizeLow,
              info.ftCreationTime, info.ftLastWriteTime};
    return true;
}
// One bounded byte read per existing monitor tick. Unchanged content is not
// reparsed. Two equal reads on DIFFERENT 1 Hz ticks confirm startup/changes;
// half-written or replaced files must not instantly become "unlimited".
// Reading bytes even on equal mtime also detects same-length/coarse-mtime edits.
class GlobalReader {
    std::wstring path_;
    FileState last_{};
    std::string text_, scratch_;
    Limit parsed_{};
    ULONGLONG observed_ = 0;
    bool have_ = false, confirmed_ = false;
public:
    void reset() { path_.clear(); text_.clear(); scratch_.clear(); parsed_ = {}; observed_ = 0; have_ = confirmed_ = false; }
    Limit sample(const std::wstring& path, long long now, ULONGLONG tick) {
        if (now <= 0 || path.empty()) { reset(); return {}; }
        if (path != path_) { reset(); path_ = path; }
        Handle file(path); FileState before{}, after{}, current{};
        if (!state(file.value, before)) { reset(); return {}; }
        scratch_.resize(before.size); DWORD bytes = 0;
        if (!ReadFile(file.value, scratch_.data(), before.size, &bytes, nullptr) || bytes != before.size ||
            !state(file.value, after) || !(before == after)) { reset(); return {}; }
        // A replace/rename can leave the first handle pointing to the old file.
        Handle again(path);
        if (!state(again.value, current) || !(before == current)) { reset(); return {}; }
        const bool same = have_ && before == last_ && scratch_ == text_;
        if (!same) {
            last_ = before; text_.swap(scratch_); parsed_ = parseGlobal(text_);
            have_ = true; confirmed_ = false; observed_ = tick;
            return {};
        }
        if (tick < observed_ || tick - observed_ > 5000) {
            confirmed_ = false; observed_ = tick; return {};
        }
        if (!confirmed_ && tick - observed_ < 500) return {};
        observed_ = tick; confirmed_ = true;
        return parsed_.valid ? Limit{true, parsed_.fps, now} : Limit{};
    }
    Limit sample(long long now, ULONGLONG tick) {
        const auto path = globalPath(); auto result = sample(path, now, tick);
        if (path != globalPath()) { reset(); return {}; } // concurrent directory change
        return result;
    }
};
} // namespace ymcc::monitorrtss
