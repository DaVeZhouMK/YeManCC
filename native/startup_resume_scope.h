#pragma once
#include <map>
#include <system_error>
#include "startup_resume_file_identity.h"
#include "sleep_recovery_record_name.h"

namespace ymcc::sleepStartupResume {

using Inventory = std::map<DWORD, Records>;

inline bool collect(const std::filesystem::path& directory, Inventory& result, DWORD self) noexcept {
    using namespace sleepLegacyResume;
    try {
        const auto existing = presence(directory);
        if (existing == Presence::Absent) return true;
        if (existing == Presence::Unknown) return false;
        std::vector<File> pins;
        if (!pinPlainDirectory(directory, pins)) return false;
        std::error_code error;
        auto it = fs::directory_iterator(directory, error);
        const auto end = fs::directory_iterator{};
        for (; !error && it != end; it.increment(error)) {
            const auto name = it->path().filename().wstring();
            const auto pid = sleepRecoveryRecords::pidForName(name);
            if (!pid || pid == self || result.contains(pid)) continue;
            result.emplace(pid, readRecordVersions(paths(directory, pid)));
        }
        return !error;
    } catch (...) { return false; }
}

inline bool settled(const std::filesystem::path& directory, const Inventory& scope) {
    using namespace sleepLegacyResume;
    if (scope.empty() || presence(directory) == Presence::Absent) return true;
    std::vector<File> pins;
    if (!pinPlainDirectory(directory, pins)) return false;
    for (const auto& [pid, initial] : scope) {
        const auto current = readRecordVersions(paths(directory, pid));
        for (size_t i = 0; i != current.size(); ++i) {
            if (initial[i].state == RecordState::Absent || current[i].state == RecordState::Absent) continue;
            // A replacement in one slot does not retire an original in another.
            if (initial[i].state == RecordState::Unknown || current[i].state == RecordState::Unknown ||
                initial[i].sameRecord(current[i])) return false;
        }
    }
    return true;
}

} // namespace ymcc::sleepStartupResume
