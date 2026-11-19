#pragma once
#include <array>
#include <cstring>
#include <optional>
#include "sleep_legacy_resume_journal.h"

namespace ymcc::sleepStartupResume {

enum class RecordState { Absent, Known, Unknown };

struct RecordVersion {
    FILE_ID_INFO identity{};
    FILE_BASIC_INFO info{};
    FILE_STANDARD_INFO size{};
    RecordState state = RecordState::Unknown;

    bool sameRecord(const RecordVersion& other) const noexcept {
        return state == RecordState::Known && other.state == RecordState::Known &&
            identity.VolumeSerialNumber == other.identity.VolumeSerialNumber &&
            std::memcmp(identity.FileId.Identifier, other.identity.FileId.Identifier,
                        sizeof(identity.FileId.Identifier)) == 0 &&
            info.CreationTime.QuadPart == other.info.CreationTime.QuadPart &&
            info.ChangeTime.QuadPart == other.info.ChangeTime.QuadPart &&
            info.LastWriteTime.QuadPart == other.info.LastWriteTime.QuadPart &&
            size.EndOfFile.QuadPart == other.size.EndOfFile.QuadPart;
    }
};

inline RecordVersion readRecordVersion(const std::filesystem::path& path) {
    using namespace sleepLegacyResume;
    RecordVersion result;
    auto file = open(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                     OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT);
    if (!file) {
        const auto error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) result.state = RecordState::Absent;
        return result;
    }
    if (GetFileInformationByHandleEx(file.get(), FileIdInfo, &result.identity, sizeof(result.identity)) &&
        GetFileInformationByHandleEx(file.get(), FileBasicInfo, &result.info, sizeof(result.info)) &&
        GetFileInformationByHandleEx(file.get(), FileStandardInfo, &result.size, sizeof(result.size)) &&
        !(result.info.FileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) &&
        result.size.NumberOfLinks == 1 && !result.size.DeletePending && result.info.ChangeTime.QuadPart > 0)
        result.state = RecordState::Known;
    return result;
}

using Records = std::array<RecordVersion, 3>;

inline Records readRecordVersions(const sleepLegacyResume::Paths& files) {
    return {readRecordVersion(files.marker), readRecordVersion(files.pending), readRecordVersion(files.journal)};
}

enum class Match { Retired, Matching, Replaced, Unknown };

// Compare after acquiring the existing directory and PID ownership locks.
// Cleaned members may disappear. A newly appearing member must not inherit
// the old startup request, even if it carries the same PID and birth time.
inline Match compareRecordVersions(const Records& initial, const Records& current) noexcept {
    bool present = false;
    bool unknown = false;
    for (size_t i = 0; i != current.size(); ++i) {
        if (initial[i].state == RecordState::Unknown || current[i].state == RecordState::Unknown) {
            unknown = true;
            continue;
        }
        if (current[i].state == RecordState::Absent) continue;
        if (!initial[i].sameRecord(current[i])) return Match::Replaced;
        present = true;
    }
    if (unknown) return Match::Unknown;
    return present ? Match::Matching : Match::Retired;
}

} // namespace ymcc::sleepStartupResume
