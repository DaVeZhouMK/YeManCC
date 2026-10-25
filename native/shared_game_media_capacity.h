#pragma once
#include <filesystem>
#include <cstdint>
#include <chrono>
#include <string>
#include <stdexcept>

namespace ymcc::media::capacity {
inline constexpr uint64_t maxAssetBytes = 300ull * 1024 * 1024;
inline constexpr uint64_t admissionBytes = 64ull * 1024 * 1024;
inline constexpr uint64_t diskReserveBytes = 1024ull * 1024 * 1024;
inline constexpr uint64_t automaticCacheBytes = 4ull * 1024 * 1024 * 1024;
struct Snapshot {
    bool diskKnown = false;
    bool cacheKnown = true;
    uint64_t available = 0;
    uint64_t owned = 0;
};
inline bool ownedName(const std::filesystem::path& path) {
    const auto stem = path.stem().wstring(), ext = path.extension().wstring();
    if(stem.size() != 32 || stem.find_first_not_of(L"0123456789abcdef") != std::wstring::npos) return false;
    return ext == L".mp4" || ext == L".webm" || ext == L".jpg" || ext == L".png" || ext == L".webp" || ext == L".bmp" || ext == L".ico";
}
// Called only for queued work or publication, never by a resident scan timer.
inline Snapshot inspect(const std::filesystem::path& root, size_t maxEntries = 8192) {
    namespace fs = std::filesystem;
    Snapshot result; std::error_code ec; auto existing = root;
    for(size_t n = 0; n < 64 && !existing.empty() && !fs::exists(existing, ec); ++n) {
        const auto parent = existing.parent_path(); if(parent == existing) break; existing = parent; ec.clear();
    }
    if(!existing.empty()) { const auto space = fs::space(existing, ec); if(!ec) { result.diskKnown = true; result.available = space.available; } }
    ec.clear(); const auto folder = root / L"artwork" / L"video";
    if(!fs::exists(folder, ec)) { result.cacheKnown = !ec; return result; }
    size_t visited = 0; fs::directory_iterator it(folder, ec), end;
    if(ec) { result.cacheKnown = false; return result; }
    for(; it != end; it.increment(ec)) {
        if(ec || ++visited > maxEntries) { result.cacheKnown = false; break; }
        if(!ownedName(it->path())) continue;
        if(it->is_symlink(ec)) continue;
        if(ec) { result.cacheKnown = false; break; }
        if(!it->is_regular_file(ec)) { if(ec) result.cacheKnown = false; continue; }
        const auto bytes = it->file_size(ec);
        if(ec || bytes > UINT64_MAX - result.owned) { result.cacheKnown = false; break; }
        result.owned += bytes;
    }
    if(ec) result.cacheKnown = false;
    return result;
}
inline const char* reason(const Snapshot& value, uint64_t incoming, bool automatic, uint64_t diskPeak = 0) {
    if(automatic && !value.diskKnown) return "media-storage-unavailable";
    const auto required = diskPeak ? diskPeak : incoming;
    if(value.diskKnown && (required > UINT64_MAX - diskReserveBytes || value.available < diskReserveBytes + required)) return "media-disk-space-low";
    if(automatic && !value.cacheKnown) return "media-cache-budget-unknown";
    if(automatic && (incoming > automaticCacheBytes || value.owned > automaticCacheBytes - incoming)) return "media-cache-budget-full";
    return "";
}
inline void require(const std::filesystem::path& root, uint64_t incoming, bool automatic, uint64_t diskPeak = 0) {
    const auto blocked = reason(inspect(root), incoming, automatic, diskPeak);
    if(*blocked) throw std::runtime_error(blocked);
}
// Only abandoned files in the media owner's own nonrecursive job directory.
// A live request is bounded to two minutes; use a full day before reclamation.
inline size_t collectTemporary(const std::filesystem::path& root, size_t maxEntries = 8192) {
    namespace fs = std::filesystem; std::error_code ec; size_t visited = 0, removed = 0;
    for(fs::directory_iterator it(root / L"cache" / L"jobs" / L"game-media", ec), end; !ec && it != end && visited < maxEntries; it.increment(ec), ++visited) {
        if(it->is_symlink(ec) || !it->is_regular_file(ec)) continue;
        const auto name = it->path().filename().wstring();
        if(name.size() <= 32 || name.substr(0, 32).find_first_not_of(L"0123456789abcdef") != std::wstring::npos) continue;
        const auto suffix = name.substr(32); if(suffix != L".part" && suffix != L".part.segment") continue;
        const auto stamp = it->last_write_time(ec); if(ec || fs::file_time_type::clock::now() - stamp < std::chrono::hours(24)) continue;
        if(fs::remove(it->path(), ec)) ++removed; ec.clear();
    }
    return removed;
}
// Zero keeps existing downloader callers unchanged; subtraction cannot wrap.
inline bool downloadFits(uint64_t received, uint64_t next, uint64_t maximum) {
    return !maximum || (received <= maximum && next <= maximum - received);
}
}
