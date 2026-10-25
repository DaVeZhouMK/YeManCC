#pragma once
#include <array>
#include <exception>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>
#include <utility>
#ifdef _WIN32
#include <windows.h>
#endif

namespace ymcc::logging {
struct DiagnosticLogGates {
    bool gyro = false;
    bool virtualGamepad = false;
    bool operator==(const DiagnosticLogGates& rhs) const noexcept {
        return gyro == rhs.gyro && virtualGamepad == rhs.virtualGamepad;
    }
};
struct CurrentLogFileResult { std::string name, state, error; };
struct DiagnosticLogMaintenanceResult {
    bool busy = false, captured = false, pauseAcknowledged = false;
    bool deleteAttempted = false, restored = false;
    DiagnosticLogGates original, after;
    std::vector<CurrentLogFileResult> files;
    std::string pauseError, deleteError, restoreError;
    bool ok() const noexcept {
        return captured && pauseAcknowledged && deleteAttempted && restored &&
            pauseError.empty() && deleteError.empty() && restoreError.empty();
    }
};

// Serialize gate owners separately from appendDomainLog, whose writerMx is held
// through restoration (gate owners can themselves log, so must not hold writerMx):
// an in-flight append finishes before capture, and no stale writer can reopen a
// stream between stop acknowledgement, deletion and restoration. No settings save,
// external-host flag, timer, device stop or restart belongs in this transaction.
class DiagnosticLogMaintenance {
public:
    template<class Capture, class Stop, class Acknowledge, class Clear, class Restore, class... Writers>
    DiagnosticLogMaintenanceResult run(std::mutex& gateMx, Capture capture,
            Stop stop, Acknowledge acknowledge, Clear clear, Restore restore, Writers&... writerMutexes) {
        DiagnosticLogMaintenanceResult result;
        std::unique_lock<std::mutex> operation(operationMx_, std::try_to_lock);
        if (!operation.owns_lock()) { result.busy = true; return result; }
        std::lock_guard<std::mutex> gateOwners(gateMx);
        std::scoped_lock writers(writerMutexes...);
        try { result.original = capture(); result.captured = true; }
        catch (const std::exception& e) { result.pauseError = e.what(); return result; }
        catch (...) { result.pauseError = "capture-failed"; return result; }
        // Restore even if stop only partly succeeded or acknowledgement failed.
        try {
            if (!stop() || !acknowledge()) result.pauseError = "stop-not-acknowledged";
            else {
                result.pauseAcknowledged = true;
                result.deleteAttempted = true;
                result.files = clear();
                for (const auto& file : result.files) {
                    if (file.state != "deleted" && file.state != "missing") {
                        if (!result.deleteError.empty()) result.deleteError += "; ";
                        result.deleteError += file.name + ": " + file.error;
                    }
                }
            }
        } catch (const std::exception& e) {
            (result.deleteAttempted ? result.deleteError : result.pauseError) = e.what();
        } catch (...) {
            (result.deleteAttempted ? result.deleteError : result.pauseError) = "operation-failed";
        }
        try {
            const bool acknowledged = restore(result.original);
            result.after = capture();
            result.restored = acknowledged && result.after == result.original;
            if (!result.restored) result.restoreError = "original-gates-not-restored";
        } catch (const std::exception& e) { result.restoreError = e.what(); }
        catch (...) { result.restoreError = "restore-failed"; }
        return result;
    }
private:
    std::mutex operationMx_;
};

enum class CurrentLogDirectory { data, app, sleep, webview };
struct CurrentLogSpec { const char* name; CurrentLogDirectory directory; };
inline constexpr std::array<CurrentLogSpec, 16> currentDiagnosticLogSpecs = {{
    {"gyro-motion.log", CurrentLogDirectory::data}, {"virtual-gamepad.log", CurrentLogDirectory::data},
    {"focus-trace.log", CurrentLogDirectory::data}, {"app.log", CurrentLogDirectory::app},
    {"native-lifecycle.log", CurrentLogDirectory::app}, {"native-detail.log", CurrentLogDirectory::app},
    {"fan-api.log", CurrentLogDirectory::data}, {"fan-lifecycle.log", CurrentLogDirectory::data},
    {"hardware-coordinator.log", CurrentLogDirectory::data}, {"sleep-facts.log", CurrentLogDirectory::sleep},
    {"tdp.log", CurrentLogDirectory::data}, {"tdp-detail.log", CurrentLogDirectory::data},
    {"frontend-errors.log", CurrentLogDirectory::webview}, {"frontend-trace.log", CurrentLogDirectory::webview},
    {"frontend-trace-detail.log", CurrentLogDirectory::webview}, {"webview-failures.log", CurrentLogDirectory::webview}
}};
struct DiagnosticLogDirectories { std::filesystem::path data, app, sleep, webview; };

#ifdef _WIN32
// Only fixed current filenames. Never enumerate, recurse, truncate an export or
// remove a directory. Refuse redirected parents and open the final file itself
// exclusively, so a still-open writer/export handle causes a reported failure.
inline CurrentLogFileResult deleteCurrentDiagnosticLogFile(const std::filesystem::path& directory, const char* name) {
    std::string parentError;
    if (!directory.is_absolute()) parentError = "absolute-log-directory-required";
    for (auto parent = directory; parentError.empty() && !parent.empty();) {
        const DWORD attrs = GetFileAttributesW(parent.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) {
            const DWORD error = GetLastError();
            if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
                parentError = "parent-inspection-failed:" + std::to_string(error);
        } else if ((attrs & FILE_ATTRIBUTE_REPARSE_POINT) || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            parentError = "redirected-or-nondirectory-parent";
        }
        const auto next = parent.parent_path();
        if (next == parent) break;
        parent = next;
    }
    CurrentLogFileResult row{name, "error", parentError};
    if (!parentError.empty()) return row;
    const auto path = directory / name;
    HANDLE file = CreateFileW(path.c_str(), DELETE | FILE_READ_ATTRIBUTES, 0,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) row.state = "missing";
        else row.error = "open-failed:" + std::to_string(error);
    } else {
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(file, &info)) row.error = "inspect-failed:" + std::to_string(GetLastError());
        else if (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
            row.error = "regular-current-log-required";
        else {
            FILE_DISPOSITION_INFO disposition{TRUE};
            if (SetFileInformationByHandle(file, FileDispositionInfo, &disposition, sizeof(disposition))) row.state = "deleted";
            else row.error = "delete-failed:" + std::to_string(GetLastError());
        }
        if (!CloseHandle(file)) { row.state = "error"; row.error = "close-failed:" + std::to_string(GetLastError()); }
    }
    return row;
}
inline std::vector<CurrentLogFileResult> deleteCurrentDiagnosticLogFiles(
        const DiagnosticLogDirectories& directories, bool standardAppLog = true) {
    std::vector<CurrentLogFileResult> results;
    for (const auto& spec : currentDiagnosticLogSpecs) {
        const auto& directory = spec.directory == CurrentLogDirectory::app ? directories.app :
            spec.directory == CurrentLogDirectory::sleep ? directories.sleep :
            spec.directory == CurrentLogDirectory::webview ? directories.webview : directories.data;
        if (!standardAppLog && std::string(spec.name) == "app.log")
            results.push_back({spec.name, "error", "custom-app-log-path-preserved"});
        else results.push_back(deleteCurrentDiagnosticLogFile(directory, spec.name));
    }
    return results;
}
#endif
} // namespace ymcc::logging
