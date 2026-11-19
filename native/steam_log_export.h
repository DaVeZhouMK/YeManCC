#pragma once
// One-shot, read-only collection for a user-requested export. Never starts Steam,
// scans libraries/drives, changes a log gate, or follows directory reparse points.
#include <windows.h>
#include "steam_installation_facts.h"
#include <algorithm>
#include <array>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
#include <utility>

namespace ymcc::logexport {
namespace fs = std::filesystem;
struct SteamRootCandidate { fs::path root; std::string source; DWORD pid = 0; };
struct SteamLogIssue { fs::path path; std::string reason; DWORD error = 0; };
struct SteamLogs {
    fs::path root;
    std::string source;
    std::string status = "not-installed-or-unavailable";
    std::vector<fs::path> files;
    std::vector<SteamLogIssue> issues;
};
inline std::wstring winPath(const fs::path& path) {
    const auto text = path.lexically_normal().wstring();
    if (text.rfind(L"\\\\?\\", 0) == 0 || !path.is_absolute()) return text;
    if (text.rfind(L"\\\\", 0) == 0) return L"\\\\?\\UNC\\" + text.substr(2);
    return L"\\\\?\\" + text;
}
inline bool reparsePoint(const fs::path& path) {
    const DWORD attributes = GetFileAttributesW(winPath(path).c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}
inline SteamLogs collectSelectedSteamRootLogs(SteamLogs result) {
    if (result.root.empty()) return result;
    const auto logs = result.root / L"logs";
    if (reparsePoint(logs)) {
        result.status = "logs-reparse-point-skipped";
        result.issues.push_back({logs, result.status}); return result;
    }
    std::error_code ec;
    const auto logStatus = fs::status(logs, ec);
    if (logStatus.type() == fs::file_type::not_found) {
        result.status = "logs-directory-missing"; return result;
    }
    if (ec || !fs::is_directory(logStatus)) {
        result.status = ec ? "logs-unavailable" : "logs-not-a-directory";
        if (ec) result.issues.push_back({logs, result.status, static_cast<DWORD>(ec.value())});
        return result;
    }
    result.status = "collected";
    std::vector<fs::path> pending{logs};
    while (!pending.empty()) {
        const auto dir = pending.back(); pending.pop_back();
        fs::directory_iterator it(dir, ec), end;
        if (ec) { result.issues.push_back({dir, "enumeration-failed", static_cast<DWORD>(ec.value())}); ec.clear(); continue; }
        while (it != end) {
            const auto path = it->path();
            const DWORD attributes = GetFileAttributesW(winPath(path).c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES) result.issues.push_back({path, "attributes-unavailable", GetLastError()});
            else if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) result.issues.push_back({path, "reparse-point-skipped"});
            else if (attributes & FILE_ATTRIBUTE_DIRECTORY) pending.push_back(path);
            else if (fs::is_regular_file(path, ec)) result.files.push_back(path);
            if (ec) { result.issues.push_back({path, "file-status-failed", static_cast<DWORD>(ec.value())}); ec.clear(); }
            it.increment(ec);
            if (ec) { result.issues.push_back({dir, "enumeration-incomplete", static_cast<DWORD>(ec.value())}); ec.clear(); break; }
        }
    }
    std::sort(result.files.begin(), result.files.end());
    if (!result.issues.empty()) result.status = "collection-partial";
    else if (result.files.empty()) result.status = "logs-directory-empty";
    return result;
}
inline SteamLogs collectSteamLogs(const std::vector<SteamRootCandidate>& candidates) {
    SteamLogs result;
    for (const auto& candidate : candidates) {
        if (candidate.root.empty() || !candidate.root.is_absolute()) continue;
        std::error_code ec;
        if (!fs::is_regular_file(candidate.root / L"steam.exe", ec) || ec) continue;
        result.root = candidate.root.lexically_normal(); result.source = candidate.source;
        break; // Do not fall back to a stale installation just because its logs exist.
    }
    return collectSelectedSteamRootLogs(std::move(result));
}
// This projection consumes public read-only facts. It performs no independent
// registry/process discovery and never grants an operation or source lease.
inline SteamLogs collectSteamLogs(const steaminfo::Facts& facts) {
    SteamLogs invalid;
    if(!facts.available||(facts.requested&(steaminfo::Installations|steaminfo::Presence))!=(steaminfo::Installations|steaminfo::Presence)){
        invalid.status="steam-information-unavailable";return invalid;
    }
    invalid.status="steam-information-invalid";
    try{
        if(!steaminfo::decode(steaminfo::encode(facts),facts.requested))return invalid;
        const auto wide=[](const std::string& text)->std::wstring{
            if(text.empty())return {};
            const int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);
            if(!size)throw 0;
            std::wstring value(size,L'\0');
            if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),value.data(),size)!=size)throw 0;
            return value;
        };
        std::vector<SteamRootCandidate> candidates;
        if(facts.presence==1){
            const fs::path image(wide(facts.processPath));
            if(!facts.processPid||!facts.processBirth||!image.is_absolute()||_wcsicmp(image.filename().c_str(),L"steam.exe"))return invalid;
            // Shared process identity selects this diagnostic root. Steam may
            // rename/update its image while running; image-file availability
            // cannot redirect the export to another installation's old logs.
            SteamLogs current;
            current.root=image.parent_path().lexically_normal();
            current.source="running-shared-process-image";
            return collectSelectedSteamRootLogs(std::move(current));
        }
        for(const auto& candidate:facts.installations){
            if(candidate.error==ERROR_SUCCESS&&!candidate.path.empty())candidates.push_back({fs::path(wide(candidate.path)),candidate.source});
        }
        return collectSteamLogs(candidates);
    }catch(...){return invalid;}
}

// Snapshot exactly the length observed at open, not an indefinitely growing log.
// FILE_SHARE_DELETE permits Steam rotation; failures never modify the source.
struct SnapshotCopyEvidence {
    const char* stage = "not-started";
    bool sourceSizeKnown = false, destinationCreated = false, copyComplete = false;
    unsigned long long sourceBytesAtOpen = 0, successfulReadBytes = 0, successfulWriteBytes = 0;
    unsigned long long readCalls = 0, writeCalls = 0;
    const char* writerBoundaryField = nullptr;
    const char* writerBoundaryState = nullptr;
};
using SnapshotSourceSync = std::function<bool(const std::function<void()>&)>;
inline bool disappearedRotatedSnapshot(const std::wstring& source,
        const std::vector<std::wstring>& discovered,DWORD error,const SnapshotCopyEvidence& observation) {
    if((error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_PATH_NOT_FOUND)||
            std::string(observation.stage)!="source-open"||observation.sourceSizeKnown||observation.destinationCreated)return false;
    // An alias is optional only when its exact same-directory base was also
    // discovered. Arbitrary missing files and mandatory current logs still fail.
    for(const wchar_t* suffix:{L".1",L".prev",L".previous"}) {
        const auto count=std::char_traits<wchar_t>::length(suffix);
        if(source.size()>count&&source.compare(source.size()-count,count,suffix)==0) {
            const auto base=source.substr(0,source.size()-count);
            return std::find(discovered.begin(),discovered.end(),base)!=discovered.end();
        }
    }
    return false;
}
inline bool snapshotSharedLog(const fs::path& source, const fs::path& destination, DWORD& error,
        unsigned long long maxBytes = 0, SnapshotCopyEvidence* evidence = nullptr,
        const SnapshotSourceSync* writerSync = nullptr,
        const char* writerBoundaryField = "monitorWriterBoundary") {
    if (evidence) *evidence = {};
    error = ERROR_SUCCESS;
    struct SourceHandle {
        HANDLE value = INVALID_HANDLE_VALUE;
        void close() { if (value != INVALID_HANDLE_VALUE) CloseHandle(std::exchange(value, INVALID_HANDLE_VALUE)); }
        ~SourceHandle() { close(); }
    } sourceHandle;
    const auto sourcePath = winPath(source);
    LARGE_INTEGER size{};
    const char* boundary = "source-open-failed";
    const auto capture = [&] {
        if (evidence) evidence->stage = "source-open";
        sourceHandle.value = CreateFileW(sourcePath.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (sourceHandle.value == INVALID_HANDLE_VALUE) { error = GetLastError(); return; }
        boundary = "source-size-failed";
        if (evidence) evidence->stage = "source-size";
        const BOOL sizeRead = GetFileSizeEx(sourceHandle.value, &size);
        if (!sizeRead || size.QuadPart < 0) {
            error = sizeRead ? ERROR_INVALID_DATA : GetLastError();
            if (!error) error = ERROR_INVALID_DATA;
            return;
        }
        boundary = "captured-under-writer-lock";
        if (evidence) { evidence->sourceSizeKnown = true; evidence->sourceBytesAtOpen = static_cast<unsigned long long>(size.QuadPart); }
    };
    if (writerSync) {
        const bool flushed = (*writerSync)(capture);
        // Keep the raw-byte copy attempt even if the writer cannot flush.
        if (evidence) {
            evidence->writerBoundaryField = writerBoundaryField;
            evidence->writerBoundaryState = flushed ? boundary : "flush-failed";
        }
    } else {
        capture();
    }
    if (sourceHandle.value == INVALID_HANDLE_VALUE || error) return false;
    const HANDLE input = sourceHandle.value;
    if (maxBytes && static_cast<unsigned long long>(size.QuadPart) > maxBytes) {
        if (evidence) evidence->stage = "size-limit";
        error = ERROR_FILE_TOO_LARGE; return false;
    }
    if (evidence) evidence->stage = "destination-open";
    HANDLE output = CreateFileW(winPath(destination).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) { error = GetLastError(); return false; }
    if (evidence) evidence->destinationCreated = true;
    std::array<char, 65536> buffer{};
    LONGLONG remaining = size.QuadPart;
    bool ok = true;
    while (remaining > 0) {
        DWORD read = 0;
        const DWORD requested = static_cast<DWORD>((std::min)(remaining, static_cast<LONGLONG>(buffer.size())));
        if (evidence) { evidence->stage = "source-read"; ++evidence->readCalls; }
        const BOOL readOk = ReadFile(input, buffer.data(), requested, &read, nullptr);
        if (evidence && readOk) evidence->successfulReadBytes += read;
        if (!readOk || !read) {
            error = readOk ? ERROR_HANDLE_EOF : GetLastError();
            if (!error) error = ERROR_HANDLE_EOF; ok = false; break;
        }
        DWORD written = 0;
        if (evidence) { evidence->stage = "destination-write"; ++evidence->writeCalls; }
        const BOOL writeOk = WriteFile(output, buffer.data(), read, &written, nullptr);
        if (evidence && writeOk) evidence->successfulWriteBytes += written;
        if (!writeOk || written != read) {
            error = writeOk ? ERROR_WRITE_FAULT : GetLastError();
            if (!error) error = ERROR_WRITE_FAULT; ok = false; break;
        }
        remaining -= read;
    }
    if (evidence && ok) evidence->stage = "destination-close";
    if (!CloseHandle(output) && ok) { error = GetLastError(); ok = false; }
    sourceHandle.close();
    if (!ok) DeleteFileW(winPath(destination).c_str()); // only this CREATE_NEW fixture/staging file
    if (evidence && ok) { evidence->stage = "copied"; evidence->copyComplete = true; }
    return ok;
}
} // namespace ymcc::logexport
