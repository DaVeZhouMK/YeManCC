#pragma once
#include "worker_thread_name.h"
#include "steam_cssloader_guard.h"
#include "steam_cssloader_editor.h"
#include "steam_cssloader_launch.h"
#include "steam_cssloader_inventory.h"
#include "sleep_power_state.h"
#include <array>
#include <stdexcept>
#include <utility>
#include <sddl.h>
#include <wincrypt.h>

// Bounded appearance helpers and an independently resident CSS backend.
// Jobs own helpers only; the one verified backend explicitly breaks away.
// Releasing a use, changing Steam or closing YMCC never terminates CSS.
namespace ymcc::steamcssuse {
inline void verifyFileLeaf(const std::filesystem::path& path, const wchar_t* leaf) {
    if (path.empty() || _wcsicmp(path.filename().c_str(), leaf))
        throw std::runtime_error("CSS use path invalid");
    const auto fileAttributes = GetFileAttributesW(path.c_str());
    if (fileAttributes == INVALID_FILE_ATTRIBUTES || (fileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        throw std::runtime_error("CSS use file missing");
    if (fileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        throw std::runtime_error("CSS use path unverified");
}
inline void verifyFileAncestors(std::filesystem::path cursor) {
    for (; !cursor.empty();) {
        const auto attributes = GetFileAttributesW(cursor.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("CSS use path unverified");
        const auto parent = cursor.parent_path(); if (parent == cursor) break; cursor = parent;
    }
}
inline void verifyFile(const std::wstring& path, const wchar_t* leaf) {
    const std::filesystem::path file(path);
    verifyFileLeaf(file, leaf);
    verifyFileAncestors(file.parent_path());
}
inline void verifyResidentControl(const std::wstring& script, bool initializeAutomatic = false) {
    const std::filesystem::path entryPath(script);
    verifyFileLeaf(entryPath, L"steam-appearance.ps1");
    // Refuse a mixed old PowerControl deployment before any helper can start
    // a backend inside a kill-on-close Job. Packaging also verifies all files.
    const auto moduleDir = entryPath.parent_path() / L"steam-appearance";
    const auto module = moduleDir / L"resident.ps1";
    const auto renderer = moduleDir / L"idle.ps1";
    verifyFileLeaf(module, L"resident.ps1");
    verifyFileLeaf(renderer, L"idle.ps1");
    // This walk covers both embedded modules and the entry script's parents.
    verifyFileAncestors(moduleDir);
    for (const auto* file : {&entryPath, &module, &renderer}) {
        deckylaunch::Handle handle; handle.value = CreateFileW(file->c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        LARGE_INTEGER size{};
        if (handle.value == INVALID_HANDLE_VALUE) { handle.value = nullptr; throw std::runtime_error("CSS resident component unreadable"); }
        if (!GetFileSizeEx(handle.value, &size) || size.QuadPart <= 0 || size.QuadPart > 262144) throw std::runtime_error("CSS resident component size invalid");
        std::string text(static_cast<size_t>(size.QuadPart), '\0'); DWORD read = 0;
        if (!ReadFile(handle.value, text.data(), static_cast<DWORD>(text.size()), &read, nullptr) || read != text.size()) throw std::runtime_error("CSS resident component read incomplete");
        const bool entry = file == &entryPath;
        if (entry ? text.find("SharedProcessQueryProtocol=1") == std::string::npos || text.find("SharedSteamReadProtocol=1") == std::string::npos || text.find("ResidentBackendPolicy=3") == std::string::npos || text.find("AutomaticResultProtocol=2") == std::string::npos || text.find("ManualObservationProtocol=1") == std::string::npos
                  : file == &module ? text.find("function Start-ResidentAppearanceBackend") == std::string::npos || text.find("AppearanceProcessQueryProtocol=3") == std::string::npos
                  : text.find("AppearanceRenderProtocol=4") == std::string::npos || text.find("AppearanceSharedSteamProtocol=1") == std::string::npos || text.find("function Get-AppearanceVariableDefaults") == std::string::npos)
            throw std::runtime_error("CSS resident component version mismatch; update PowerControl together with YMCC");
        if (entry && initializeAutomatic && text.find("AutomaticInitializeGateProtocol=1") == std::string::npos)
            throw std::runtime_error("CSS initialization gate component mismatch; update PowerControl together with YMCC");
    }
}
inline constexpr size_t automaticResultWords = 8, automaticRenderMaxAttempts = 5;
inline constexpr size_t automaticRenderHeaderWords = 4;
inline constexpr size_t automaticResultTraceWords = automaticResultWords + automaticRenderHeaderWords + 2 * automaticRenderMaxAttempts;
inline constexpr DWORD automaticRenderOffer = 0x594D5452, automaticRenderCommit = 0x594D5431;
inline constexpr DWORD automaticTimingOffer = 0x594D544F, automaticTimingCommit = 0x594D5450;
inline constexpr size_t automaticResultTimingWords = automaticResultTraceWords + 9;
inline constexpr DWORD automaticCorrectionOffer = 0x594D434F, automaticCorrectionCommit = 0x594D4331;
inline constexpr size_t automaticResultCorrectionWords = automaticResultTimingWords + 9;
inline constexpr size_t automaticResultCorrectionReasonWords = automaticResultCorrectionWords + 1;
inline constexpr DWORD automaticCorrectionReasonOffer = 0x594D524F, automaticCorrectionReasonCommit = 0x594D5A00;
inline constexpr size_t automaticResultStatusPartsWords = automaticResultCorrectionReasonWords + 6;
inline constexpr DWORD automaticStatusPartsOffer = 0x594D534F, automaticStatusPartsCommit = 0x594D5331;
inline constexpr size_t automaticResultInitializationWords = automaticResultStatusPartsWords + 8;
inline constexpr DWORD automaticInitializationOffer = 0x594D494F, automaticInitializationCommit = 0x594D4931;
inline constexpr size_t automaticResultThemeStateWords = automaticResultInitializationWords + 8;
inline constexpr DWORD automaticThemeStateOffer = 0x594D504F, automaticThemeStateCommit = 0x594D5031;
struct InitializationTiming {
    bool known = false, launched = false;
    DWORD scriptSetupMs = 0, ensureChecksMs = 0, backendPreflightMs = 0, listenerWaitMs = 0, afterListenerMs = 0, totalMs = 0;
};
struct AutomaticCorrectionTiming {
    bool known = false;
    DWORD startMs = 0, endMs = 0, firstSetStartMs = MAXDWORD, lastSetAckMs = MAXDWORD,
        setAttempts = 0, setAcknowledged = 0, outcome = 0;
    bool reasonsKnown = false;
    DWORD flagOnly = 0, receiptOnly = 0, both = 0;
};
struct AutomaticTiming {
    bool known = false;
    DWORD entryToRefreshMs = 0, firstControlMs = 0, firstMutexWaitMs = 0, firstCssRpcMs = 0,
        firstCssRpcCount = 0, firstDomMs = 0, entryToReadyMs = 0;
    bool statusPartsKnown = false;
    DWORD statusIdentityMs = 0, cssCandidatesMs = 0, cssRpcOwnerMs = 0, themeStateMs = 0;
    bool themeStatePartsKnown = false;
    DWORD rowsMs = 0, bundleMs = 0, installedMs = 0, foreignMs = 0, backupMs = 0, statusPasses = 0;
};
struct AutomaticRenderTrace {
    struct Attempt { DWORD waitMs = 0, outcome = 0; };
    DWORD count = 0, correctionAttempt = 0;
    std::array<Attempt, automaticRenderMaxAttempts> attempts{};
};
struct Cleanup {
    unsigned verified = 0, fallback = 0, remaining = 0, unverified = 0;
    std::vector<std::pair<DWORD, ULONGLONG>> identities;
    std::string launchStage, launchMode;
    DWORD helperExitCode = 0; bool helperExitKnown = false;
    bool helperResultKnown = false;
    DWORD helperPhase = 0, helperFailureCode = 0, helperFlags = 0, helperBackendCount = 0, helperHResult = 0;
    AutomaticRenderTrace renderTrace;
    AutomaticTiming timing;
    AutomaticCorrectionTiming correctionTiming;
    InitializationTiming initializationTiming;
    bool editorProtected = false, editorStateUnknown = false;
    uint64_t useGeneration = 0;
    DWORD error = 0; bool deferred = false, holding = false, stale = false;
    bool ok() const { return !error && !remaining && !stale && !editorProtected; }
};
// Fixed 32-byte v2 prefix; no stdout pipe, files, paths or secrets.
// The same-user medium-integrity mapping is written before helper exit. Native
// reads final results after the pinned process is signaled; the initialization
// receipt alone is read while parked on the ready event. Magic is committed last.
inline const char* helperPhaseName(DWORD value) {
    const char* names[]{"unknown","initialize","wait-idle","steam-session","backend-status","dom-confirmation","unique-backend","resident-backend","complete"};
    return value < std::size(names) ? names[value] : "unknown";
}
inline const char* helperFailureName(DWORD value) {
    const char* names[]{"none","steam-session-changed","launch-mutex-busy","backend-state-unconfirmed","theme-revision-changed",
        "session-or-config-changed","dom-unconfirmed","backend-owner-unverified","minimum-hold-unverified","post-dom-state-changed",
        "backend-exit-unconfirmed","theme-control-busy","automatic-protocol-unconfirmed","idle-gate-expired","initialize-unconfirmed","desktop-editor-active"};
    return value < std::size(names) ? names[value] : "helper-exception";
}
inline bool readAutomaticResult(const DWORD* values, Cleanup& status) {
    if (!values || values[0] != 0x594D4353 || values[1] != 2) return false;
    const auto phase = values[2], reason = values[3], flags = values[4], count = values[5];
    if (phase > 8 || (reason > 15 && reason != 99) || flags > 31 || count > 128 || values[7] != 0) return false;
    status.helperResultKnown = true; status.helperPhase = phase; status.helperFailureCode = reason;
    status.helperFlags = flags; status.helperBackendCount = count; status.helperHResult = values[6];
    return true;
}
inline AutomaticTiming readAutomaticTiming(const DWORD* values, size_t words) {
    if (!values || words < automaticResultTimingWords) return {};
    const auto* tail = values + automaticResultTraceWords;
    if (tail[0] != automaticTimingOffer || tail[1] != automaticTimingCommit) return {};
    AutomaticTiming t{true, tail[2], tail[3], tail[4], tail[5], tail[6], tail[7], tail[8]};
    if (t.entryToReadyMs > 180000 || t.firstCssRpcCount > 128 || (!t.firstCssRpcCount && t.firstCssRpcMs) ||
        uint64_t(t.entryToRefreshMs) + t.firstControlMs + t.firstDomMs > t.entryToReadyMs ||
        uint64_t(t.firstMutexWaitMs) + t.firstCssRpcMs > t.firstControlMs) return {};
    if(words >= automaticResultStatusPartsWords) {
        const auto* parts=values+automaticResultCorrectionReasonWords;
        if(parts[0]==automaticStatusPartsOffer && parts[1]==automaticStatusPartsCommit &&
           uint64_t(t.firstMutexWaitMs)+t.firstCssRpcMs+parts[2]+parts[3]+parts[4]+parts[5]<=t.firstControlMs) {
            t.statusPartsKnown=true;
            t.statusIdentityMs=parts[2]; t.cssCandidatesMs=parts[3]; t.cssRpcOwnerMs=parts[4]; t.themeStateMs=parts[5];
        }
    }
    if(t.statusPartsKnown && words >= automaticResultThemeStateWords) {
        const auto* p = values + automaticResultInitializationWords;
        if(p[0]==automaticThemeStateOffer && p[1]==automaticThemeStateCommit && p[7]>0 && p[7]<=128 &&
           uint64_t(p[2])+p[3]+p[4]+p[5]+p[6]<=t.themeStateMs) {
            t.themeStatePartsKnown=true;
            t.rowsMs=p[2]; t.bundleMs=p[3]; t.installedMs=p[4]; t.foreignMs=p[5]; t.backupMs=p[6]; t.statusPasses=p[7];
        }
    }
    return t;
}
inline InitializationTiming readInitializationTiming(const DWORD* values, size_t words) {
    if (!values || words < automaticResultInitializationWords) return {};
    const auto* p = values + automaticResultStatusPartsWords;
    if (p[0] != automaticInitializationOffer || p[1] != automaticInitializationCommit ||
        (p[7] != 1 && p[7] != 2) || p[6] > 180000) return {};
    for (size_t i = 2; i < 6; ++i) if (p[i] > p[i+1]) return {};
    return {true, p[7] == 2, p[2], p[3]-p[2], p[4]-p[3], p[5]-p[4], p[6]-p[5], p[6]};
}
inline AutomaticCorrectionTiming readAutomaticCorrectionTiming(const DWORD* values, size_t words) {
    if (!values || words < automaticResultCorrectionWords) return {};
    const auto* tail = values + automaticResultTimingWords;
    if (tail[0] != automaticCorrectionOffer || tail[1] != automaticCorrectionCommit) return {};
    AutomaticCorrectionTiming t{true, tail[2], tail[3], tail[4], tail[5], tail[6], tail[7], tail[8]};
    if (t.endMs > 180000 || t.startMs > t.endMs || t.outcome < 1 || t.outcome > 5 ||
        t.setAttempts > 7 || t.setAcknowledged > t.setAttempts ||
        (t.setAttempts ? (t.firstSetStartMs < t.startMs || t.firstSetStartMs > t.endMs) : t.firstSetStartMs != MAXDWORD) ||
        (t.setAcknowledged ? (t.lastSetAckMs < t.firstSetStartMs || t.lastSetAckMs > t.endMs) : t.lastSetAckMs != MAXDWORD) ||
        ((t.outcome == 2 || t.outcome == 3) && t.setAttempts) ||
        (t.outcome >= 4 && t.setAcknowledged != t.setAttempts)) return {};
    if (words >= automaticResultCorrectionReasonWords) {
        const auto packed = values[automaticResultCorrectionWords];
        if ((packed & 0xFFFFFE00u) == automaticCorrectionReasonCommit) {
            const auto flag = packed & 7u, receipt = (packed >> 3) & 7u, both = (packed >> 6) & 7u;
            if (flag + receipt + both == t.setAttempts) {
                t.reasonsKnown = true; t.flagOnly = flag; t.receiptOnly = receipt; t.both = both;
            }
        }
    }
    return t;
}
inline AutomaticRenderTrace readAutomaticRenderTrace(const DWORD* values, size_t words) {
    // Optional v1 tail is read only after process exit, never at initialization.
    if (!values || words < automaticResultTraceWords) return {};
    const auto* tail = values + automaticResultWords;
    if (tail[0] != automaticRenderOffer || tail[1] != automaticRenderCommit || !tail[2] ||
        tail[2] > automaticRenderMaxAttempts || tail[3] > tail[2]) return {};
    AutomaticRenderTrace trace;
    trace.count = tail[2]; trace.correctionAttempt = tail[3];
    for (DWORD i = 0; i < trace.count; ++i) {
        const auto j = automaticRenderHeaderWords + 2 * i;
        const auto wait = tail[j], outcome = tail[j + 1];
        if (wait > 180000 || outcome < 1 || outcome > 6 ||
            (trace.correctionAttempt == i + 1 && outcome != 4 && outcome != 6)) return {};
        trace.attempts[i] = {wait, outcome};
    }
    return trace;
}
inline void requireAutomaticConfirmation(Cleanup& status, bool idleReleased) {
    // Completion needs every verification stage, not merely a zero process exit.
    if (idleReleased && !status.error && status.helperExitKnown && !status.helperExitCode &&
        status.helperResultKnown && !status.helperFailureCode && status.helperPhase == 8 &&
        status.helperFlags == 31 && status.helperBackendCount && status.helperBackendCount <= 128) return;
    if (!status.error) status.error = ERROR_INVALID_DATA;
    if (!status.helperFailureCode) status.helperFailureCode = 12;
}
class HelperLaunchError : public std::runtime_error {
public:
    Cleanup detail;
    HelperLaunchError(const std::string& stage, DWORD error, const std::string& mode = {})
        : std::runtime_error("CSS helper launch failed: " + stage + " (Windows " + std::to_string(error ? error : ERROR_GEN_FAILURE) + ")") {
        detail.launchStage = stage; detail.launchMode = mode; detail.error = error ? error : ERROR_GEN_FAILURE;
    }
};
class Group {
    steamcsseditor::Probe editorProbe_;
    std::function<bool(HANDLE)> adoptionAssign_; // Isolated tests only; no IPC/config override.
    unsigned fallback_ = 0, unverified_ = 0;
    HANDLE job_ = nullptr;
    // Backend and helper may originate in different parent Job hierarchies.
    // A separate empty Job for each launch avoids mixing populated hierarchies.
    std::vector<HANDLE> launchJobs_;
    std::vector<HANDLE> adopted_;
    static HANDLE createJob() {
        HANDLE job = CreateJobObjectW(nullptr, nullptr);
        if (!job) return nullptr;
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_BREAKAWAY_OK;
        if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
            const auto error = GetLastError(); CloseHandle(job); SetLastError(error); return nullptr;
        }
        return job;
    }
public:
    explicit Group(std::function<bool(HANDLE)> adoptionAssign = {}, steamcsseditor::Probe editorProbe = steamcsseditor::probe)
        : editorProbe_(std::move(editorProbe)), adoptionAssign_(std::move(adoptionAssign)) {}
    bool hasWork() const { return job_ || !launchJobs_.empty() || !adopted_.empty(); }
    steamcsseditor::State editorState() const {
        try { return editorProbe_ ? editorProbe_() : steamcsseditor::State::Unknown; } catch (...) { return steamcsseditor::State::Unknown; }
    }
    Cleanup preserve(steamcsseditor::State state) {
        auto result = close(steamcsseditor::State::Absent);
        result.editorProtected = true;
        result.editorStateUnknown = state == steamcsseditor::State::Unknown;
        return result;
    }
    Cleanup describe() const {
        Cleanup result;result.verified=static_cast<unsigned>(adopted_.size());result.fallback=fallback_;result.unverified=unverified_;
        for(auto process:adopted_){FILETIME b{},e{},k{},u{};if(GetProcessTimes(process,&b,&e,&k,&u))
            result.identities.push_back({GetProcessId(process),(static_cast<ULONGLONG>(b.dwHighDateTime)<<32)|b.dwLowDateTime});}
        return result;
    }
    ~Group() { close(); }
    void open() {
        if (job_) return;
        job_ = createJob();
        if (!job_) throw std::runtime_error("CSS use job unavailable");
    }
    bool assign(HANDLE process) {
        if (!job_) { SetLastError(ERROR_INVALID_HANDLE); return false; }
        for (auto it = launchJobs_.begin(); it != launchJobs_.end();) {
            BOOL already = FALSE;
            if (IsProcessInJob(process, *it, &already) && already) return true;
            JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
            if (QueryInformationJobObject(*it, JobObjectBasicAccountingInformation, &accounting, sizeof(accounting), nullptr) && !accounting.ActiveProcesses) {
                CloseHandle(*it); it = launchJobs_.erase(it);
            } else ++it;
        }
        HANDLE job = createJob(); if (!job) return false;
        if (!AssignProcessToJobObject(job, process)) {
            const auto error = GetLastError(); CloseHandle(job); SetLastError(error); return false;
        }
        launchJobs_.push_back(job); return true;
    }
    void adopt(const std::wstring& executable) {
        verifyFile(executable, L"CssLoader-Standalone-Headless.exe"); unverified_ = 0;
        if (editorState() != steamcsseditor::State::Absent) return; // Never adopt the editor's existing backend into a kill-on-close job.
        for (auto it = adopted_.begin(); it != adopted_.end();) {
            if (WaitForSingleObject(*it, 0) == WAIT_OBJECT_0) { CloseHandle(*it); it = adopted_.erase(it); }
            else ++it;
        }
        const auto topology = processInventory::read();
        if (!topology || topology->status != processInventory::Status::Complete)
            throw std::runtime_error("CSS shared process inventory unavailable");
        DWORD ourSession = 0;
        if (!ProcessIdToSessionId(GetCurrentProcessId(), &ourSession)) throw std::runtime_error("CSS use session unverified");
        for (const auto& entry : topology->rows) {
            if (_wcsicmp(entry.name.c_str(), L"CssLoader-Standalone-Headless.exe")) continue;
            DWORD targetSession = 0;
            if (!ProcessIdToSessionId(entry.pid, &targetSession) || targetSession != ourSession) continue;
            deckylaunch::Handle process;
            process.value = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.pid);
            if (!process.value) { ++unverified_; continue; }
            if (!steamcss::ownProcess(process.value)) continue; // Never another user or an inaccessible PID.
            wchar_t image[32768]{}; DWORD size = static_cast<DWORD>(std::size(image));
            if (!QueryFullProcessImageNameW(process.value, 0, image, &size) || _wcsicmp(image, executable.c_str()) ||
                WaitForSingleObject(process.value, 0) != WAIT_TIMEOUT) continue;
            if (std::any_of(adopted_.begin(), adopted_.end(), [&](HANDLE existing) {
                return GetProcessId(existing) == entry.pid && WaitForSingleObject(existing, 0) == WAIT_TIMEOUT;
            })) continue;
            if (adopted_.size() >= 128) throw std::runtime_error("CSS use process limit");
            // Observation handles carry no terminate or Job assignment rights.
            adopted_.push_back(process.value); process.value = nullptr;
        }
    }
    Cleanup close() { return close(hasWork() ? editorState() : steamcsseditor::State::Absent); }
    Cleanup close(steamcsseditor::State editor) {
        if (editor != steamcsseditor::State::Absent) return preserve(editor);
        auto result = describe();if(result.unverified)result.error=ERROR_ACCESS_DENIED;
        // Only bounded helpers belong to these Jobs. CSS is never adopted.
        if (job_) { if (!CloseHandle(job_)) result.error = GetLastError(); job_ = nullptr; }
        for (auto job : launchJobs_) if (!CloseHandle(job)) result.error = GetLastError();
        launchJobs_.clear();
        for (const auto process : adopted_) CloseHandle(process);
        adopted_.clear(); fallback_ = unverified_ = 0; return result;
    }
};
struct Ticket { uint64_t token = 0; DWORD pid = 0; ULONGLONG birth = 0; std::wstring backend; };
inline void requireRequest(const std::function<bool()>& current) {
    if(!current)return;
    bool accepted=false;try{accepted=current();}catch(...){}
    if(!accepted)throw std::runtime_error("BOOT_CSS_REQUEST_EXPIRED");
}
class Runtime {
    std::mutex lifecycle_, mutex_;
    // This lock only protects the wake handle lifetime and the scoped-use bit.
    // Settings completion must not wait on mutex_ or helper IO.
    std::mutex requestWakeMutex_;
    HANDLE requestWake_ = nullptr;
    bool requestScoped_ = false, identityWakePending_ = false;
    std::shared_ptr<const steamsession::IdentitySnapshot> requestIdentity_;
    steamcssinventory::Bridge inventory_;
    std::function<bool()> inventoryAllowed_, requestCurrent_;
    std::function<ULONGLONG()> powerGeneration_;
    sleeppower::Reader powerReader_ = nullptr;
    using SharedIdentity = std::shared_ptr<const steamsession::IdentitySnapshot>;
    std::function<SharedIdentity()> readIdentity_;
    std::function<bool(const SharedIdentity&)> identityCurrent_;
    steamcssinventory::Bridge::SharedRead sharedSteamRead_;
    steamcssinventory::Bridge::SourceRead inventorySourceRead_;
    SharedIdentity useIdentity_, confirmedIdentity_;
    ULONGLONG usePowerGeneration_ = 0, confirmedPowerGeneration_ = 0;
    sleeppower::SharedPowerV1 usePower_, confirmedPower_;
    Group group_; steamsession::Session session_, observed_, confirmedSession_; bool observationKnown_ = false, residentConfirmed_ = false;
    DWORD automaticAppliedPid_ = 0;
    ULONGLONG automaticAppliedBirth_ = 0, automaticAppliedRevision_ = 0;
    std::wstring script_, link_, backend_;
    HANDLE completed_ = nullptr;
    HANDLE autoReady_ = nullptr, idleGate_ = nullptr, resultMap_ = nullptr;
    const DWORD* resultView_ = nullptr;
    bool idleReleased_ = false;
    void closeAutoEventsLocked() {
        if (autoReady_) { CloseHandle(autoReady_); autoReady_ = nullptr; }
        if (idleGate_) { CloseHandle(idleGate_); idleGate_ = nullptr; }
        if (resultView_) { UnmapViewOfFile(resultView_); resultView_ = nullptr; }
        if (resultMap_) { CloseHandle(resultMap_); resultMap_ = nullptr; }
        idleReleased_ = false;
    }
    void readHelperResultLocked(Cleanup& status) {
        if (readAutomaticResult(resultView_, status)) {
            status.renderTrace = readAutomaticRenderTrace(resultView_, automaticResultTraceWords);
            status.timing = readAutomaticTiming(resultView_, automaticResultThemeStateWords);
            if (status.renderTrace.correctionAttempt)
                status.correctionTiming = readAutomaticCorrectionTiming(resultView_, automaticResultCorrectionReasonWords);
        }
    }
    void cancelAutomaticLocked() {
        if (helper_) {
            if (WaitForSingleObject(helper_, 0) == WAIT_TIMEOUT && !TerminateProcess(helper_, 1)) {
                const auto error = GetLastError();
                if (WaitForSingleObject(helper_, 0) != WAIT_OBJECT_0)
                    throw HelperLaunchError("automatic-helper-cancel", error);
            }
            const auto wait = WaitForSingleObject(helper_, 2000);
            if (wait != WAIT_OBJECT_0)
                throw HelperLaunchError("automatic-helper-cancel-wait", wait == WAIT_FAILED ? GetLastError() : ERROR_TIMEOUT);
            CloseHandle(helper_); helper_ = nullptr;
        }
        if (initializationGatePending_ && ensure_) {
            if (WaitForSingleObject(ensure_, 0) == WAIT_TIMEOUT && !TerminateProcess(ensure_, 1) && WaitForSingleObject(ensure_, 0) != WAIT_OBJECT_0)
                throw HelperLaunchError("automatic-initialization-cancel", GetLastError());
            if (WaitForSingleObject(ensure_, 2000) != WAIT_OBJECT_0)
                throw HelperLaunchError("automatic-initialization-cancel-wait", ERROR_TIMEOUT);
            CloseHandle(ensure_); ensure_ = nullptr;
        }
        initializationGatePending_ = false;
        closeAutoEventsLocked(); readOnlyUntil_ = 0;
    }
    std::pair<std::wstring, std::wstring> openAutoEventsLocked() {
        closeAutoEventsLocked();
        // The verified child has the same user/session at medium integrity.
        // Default elevated-object labels would deny its ready signal. Only this
        // user can open these unpredictable, per-use Local objects.
        deckylaunch::Handle token;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value))
            throw HelperLaunchError("automatic-event-token", GetLastError());
        const auto user = deckylaunch::tokenData(token.value, TokenUser);
        LPWSTR sid = nullptr;
        if (user.empty() || !ConvertSidToStringSidW(reinterpret_cast<const TOKEN_USER*>(user.data())->User.Sid, &sid))
            throw HelperLaunchError("automatic-event-user", GetLastError());
        const auto acl = L"D:P(A;;GA;;;" + std::wstring(sid) + L")S:(ML;;NW;;;ME)"; LocalFree(sid);
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(acl.c_str(), SDDL_REVISION_1, &descriptor, nullptr))
            throw HelperLaunchError("automatic-event-security", GetLastError());
        HCRYPTPROV provider = 0; BYTE random[16]{};
        const bool acquired = CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT | CRYPT_SILENT) != FALSE;
        const bool generated = acquired && CryptGenRandom(provider, sizeof(random), random) != FALSE;
        const auto randomError = generated ? ERROR_SUCCESS : GetLastError();
        if (provider) CryptReleaseContext(provider, 0);
        if (!generated) { LocalFree(descriptor); throw HelperLaunchError("automatic-event-random", randomError); }
        std::wstring name = L"Local\\YMCC.CssUse.";
        for (const auto byte : random) { name += L"0123456789abcdef"[byte >> 4]; name += L"0123456789abcdef"[byte & 15]; }
        const auto readyName = name + L".ready", gateName = name + L".idle";
        SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor, FALSE};
        SetLastError(ERROR_SUCCESS);
        autoReady_ = CreateEventW(&attributes, TRUE, FALSE, readyName.c_str());
        auto error = GetLastError();
        if (autoReady_ && error != ERROR_ALREADY_EXISTS) {
            SetLastError(ERROR_SUCCESS);
            idleGate_ = CreateEventW(&attributes, TRUE, FALSE, gateName.c_str()); error = GetLastError();
        }
        if (autoReady_ && idleGate_ && error != ERROR_ALREADY_EXISTS) {
            SetLastError(ERROR_SUCCESS);
            constexpr DWORD resultBytes = static_cast<DWORD>(automaticResultThemeStateWords * sizeof(DWORD));
            resultMap_ = CreateFileMappingW(INVALID_HANDLE_VALUE, &attributes, PAGE_READWRITE, 0, resultBytes, (name + L".result").c_str());
            error = GetLastError();
            if (resultMap_ && error != ERROR_ALREADY_EXISTS) {
                auto* view = static_cast<DWORD*>(MapViewOfFile(resultMap_, FILE_MAP_WRITE, 0, 0, resultBytes));
                if (view) {
                    view[automaticResultWords] = automaticRenderOffer;
                    view[automaticResultTraceWords] = automaticTimingOffer;
                    view[automaticResultTimingWords] = automaticCorrectionOffer;
                    view[automaticResultCorrectionWords] = automaticCorrectionReasonOffer;
                    view[automaticResultCorrectionReasonWords] = automaticStatusPartsOffer;
                    view[automaticResultInitializationWords] = automaticThemeStateOffer;
                    if (initializationGatePending_) view[automaticResultStatusPartsWords] = automaticInitializationOffer;
                    resultView_ = view;
                }
                else error = GetLastError();
            }
        }
        LocalFree(descriptor);
        if (!autoReady_ || !idleGate_ || !resultMap_ || !resultView_ || error == ERROR_ALREADY_EXISTS) {
            closeAutoEventsLocked(); throw HelperLaunchError("automatic-event-create", error);
        }
        return {readyName, gateName};
    }
    HANDLE steam_ = nullptr, helper_ = nullptr, ensure_ = nullptr, wake_ = nullptr, stop_ = nullptr;
    std::thread worker_; uint64_t generation_ = 0, token_ = 0;
    uint64_t ensureNoticeToken_ = 0;
    ULONGLONG due_ = 0, holdUntil_ = 0, readOnlyUntil_ = 0; DWORD lifetime_ = 180000, cleanupDelay_ = 4000, minimumHold_ = 0;
    bool idlePending_ = false, releasePending_ = false, manualCompletion_ = false;
    bool initializationGatePending_ = false;
    bool cleanupPending_ = false, allowActiveBackend_ = true;
    unsigned short backendPort_ = 35821;
    std::function<void(const std::string&, const steamsession::Session&, const std::wstring&, const Cleanup&)> changed_;
    void emit(const std::string& stage, const Cleanup& result) {
        try { if (changed_) changed_(stage, session_, backend_, result); } catch (...) {}
    }
    void cancelEnsureNoticeLocked() {
        if (!ensureNoticeToken_) return;
        Cleanup canceled; canceled.useGeneration = std::exchange(ensureNoticeToken_, 0);
        emit("ensure-helper-canceled", canceled);
    }
    void emitHelperFailure(const char* stage) {
        try { throw; }
        catch (const HelperLaunchError& error) { emit(stage, error.detail); }
        catch (...) { Cleanup failure; failure.error = ERROR_NOT_READY; failure.launchStage = "use-precondition-or-exception"; emit(stage, failure); }
    }
    bool lifecycleAllowed() const {
        try { return !inventoryAllowed_ || inventoryAllowed_(); } catch (...) { return false; }
    }
    bool currentUsePower() const {
        try { return (!powerGeneration_ || usePowerGeneration_ == powerGeneration_()) &&
            (!powerReader_ || sleeppower::powerCurrent(powerReader_, usePower_)); } catch (...) { return false; }
    }
    void setRequestLocked(const std::function<bool()>& current, bool activeIdentity = true) {
        requestCurrent_ = current;
        std::lock_guard wakeLock(requestWakeMutex_);
        requestScoped_ = static_cast<bool>(requestCurrent_);
        requestIdentity_ = activeIdentity ? useIdentity_ : SharedIdentity{}; identityWakePending_ = false;
    }
    void closeWakeLocked() {
        std::lock_guard wakeLock(requestWakeMutex_);
        requestWake_ = nullptr; requestIdentity_.reset(); identityWakePending_ = false;
        if (wake_) CloseHandle(wake_);
        wake_ = nullptr;
    }
    bool currentUseRequest() const {
        try { return (!requestCurrent_ || requestCurrent_()) &&
            (!readIdentity_ || (useIdentity_ && identityCurrent_ && identityCurrent_(useIdentity_))); } catch (...) { return false; }
    }
    SharedIdentity sharedIdentity(const steamsession::Session& session) const {
        if (!readIdentity_) return {}; // Isolated native consumers provide their own inert identity ports.
        const auto facts=readIdentity_();
        if (!facts || facts->presence!=1 || facts->session!=session || !facts->processEpoch ||
            facts->rootReadError!=ERROR_SUCCESS || !facts->windowsSessionKnown || !identityCurrent_ || !identityCurrent_(facts))
            throw std::runtime_error("CSS shared Steam identity unconfirmed");
        return facts;
    }
    bool captureAllowed() const {
        return lifecycleAllowed() && currentUsePower() && currentUseRequest() && steam_ && !backend_.empty() && WaitForSingleObject(steam_, 0) == WAIT_TIMEOUT &&
            (!observationKnown_ || steamcss::sameSession(session_, observed_));
    }
    Cleanup clearLocked(bool = false, bool allowManualCompletion = true, bool editorTookOver = false) {
        const bool hadUse = token_ != 0;
        const auto editor = manualCompletion_ && allowManualCompletion ? group_.editorState() : steamcsseditor::State::Absent;
        const bool manualEnded = manualCompletion_ && allowManualCompletion && captureAllowed() && editor == steamcsseditor::State::Absent;
        manualCompletion_ = false;
        const auto endedGeneration = token_;
        // Helpers remain owned and bounded, even on timeout, Steam exit or editor takeover.
        DWORD helperError = 0;
        for (const auto process : {helper_, ensure_}) {
            if (!process || WaitForSingleObject(process, 0) != WAIT_TIMEOUT) continue;
            if (!TerminateProcess(process, 1) && WaitForSingleObject(process, 0) != WAIT_OBJECT_0)
                helperError = GetLastError();
            else if (WaitForSingleObject(process, 2000) != WAIT_OBJECT_0) helperError = ERROR_TIMEOUT;
        }
        auto result = group_.close(steamcsseditor::State::Absent);
        if (editorTookOver || editor != steamcsseditor::State::Absent) {
            result.editorProtected = true; result.editorStateUnknown = editor == steamcsseditor::State::Unknown;
        }
        if (helperError && !result.error) result.error = helperError;
        if (ensure_) { CloseHandle(ensure_); ensure_ = nullptr; }
        if (helper_) { CloseHandle(helper_); helper_ = nullptr; }
        cancelEnsureNoticeLocked();
        closeAutoEventsLocked(); cleanupPending_ = false; initializationGatePending_ = false;
        if (hadUse || result.verified || result.error) emit("resident-backend-retained", result);
        if (manualEnded && !result.error && !editorTookOver) {
            result.useGeneration = endedGeneration;
            emit("manual-use-ended", result);
        }
        token_ = 0; setRequestLocked({}, false); usePower_ = {}; holdUntil_ = 0; readOnlyUntil_ = 0; idlePending_ = releasePending_ = false;
        if (completed_) { SetEvent(completed_); CloseHandle(completed_); completed_ = nullptr; }
        if (steam_) { CloseHandle(steam_); steam_ = nullptr; }
        session_ = {}; backend_.clear(); due_ = 0; useIdentity_.reset();
        return result;
    }
    void secondaryLocked() {
        auto session = session_; auto backend = backend_;
        const auto result = clearLocked(true, false);
        try { if (changed_) changed_("secondary-cleanup-complete", session, backend, result); } catch (...) {}
    }
    static HANDLE duplicate(HANDLE original) {
        HANDLE copy = nullptr;
        if (original && !DuplicateHandle(GetCurrentProcess(), original, GetCurrentProcess(), &copy, SYNCHRONIZE, FALSE, 0))
            throw std::runtime_error("CSS use wait lease unavailable");
        return copy;
    }
    Ticket beginLocked(const steamsession::Session& session, bool manual, const std::function<bool()>& current = {},
                       const sleeppower::SharedPowerV1* expectedPower = nullptr) {
        requireRequest(current);
        const auto identity=sharedIdentity(session);
        if (expectedPower && !sleeppower::powerCurrent(powerReader_, *expectedPower))
            throw HelperLaunchError("lease-expired", ERROR_INVALID_STATE);
        if (!wake_ || !stop_ || WaitForSingleObject(stop_, 0) != WAIT_TIMEOUT || !lifecycleAllowed()) throw std::runtime_error("CSS use lifecycle unavailable");
        if (observationKnown_ && !steamcss::sameSession(session, observed_)) throw std::runtime_error("CSS use Steam event changed");
        const auto configured = steamcss::readLink(link_);
        requireRequest(current);
        if (!configured.error.empty()) throw std::runtime_error("CSS use link invalid");
        auto backend = configured.executable;
        if (allowActiveBackend_) {
            // Port ownership makes this a KNOWN backend, not an arbitrary same-
            // name process. A copied/stale C: link must not miss a portable D: loader.
            auto active = steamcss::probeBackend(0, true, backendPort_);
            deckylaunch::Handle owner;owner.value=active.process;
            if (owner.value) {
                wchar_t image[32768]{};DWORD size=static_cast<DWORD>(std::size(image));
                if(!QueryFullProcessImageNameW(owner.value,0,image,&size))throw std::runtime_error("CSS active path unverified");
                backend.assign(image,size);verifyFile(backend,L"CssLoader-Standalone-Headless.exe");
            } else if(active.phase!="missing") throw std::runtime_error("CSS active owner unverified");
        }
        if (backend.empty()) backend = L"C:\\SOFT\\CSSLoader Desktop\\CssLoader-Standalone-Headless.exe";
        requireRequest(current);
        if (token_ && _wcsicmp(backend.c_str(), backend_.c_str()) == 0 && steamcss::sameSession(session, session_) && captureAllowed()) {
            if (manual && (helper_ || initializationGatePending_)) {
                try { cancelAutomaticLocked(); } catch (...) { emitHelperFailure("automatic-cancel-failed"); clearLocked(true); throw; }
            }
            requireRequest(current);
            if (manual) { cancelEnsureNoticeLocked(); setRequestLocked(current); token_ = ++generation_; manualCompletion_ = false; } // Stale completion cannot end the next click.
            if (manual) {
                idlePending_ = releasePending_ = false;
                if (completed_) { SetEvent(completed_); CloseHandle(completed_); completed_ = nullptr; }
            }
            holdUntil_ = GetTickCount64() + minimumHold_;
            due_ = GetTickCount64() + lifetime_; SetEvent(wake_);
            return {token_, session_.pid, session_.created, backend_};
        }
        clearLocked(); residentConfirmed_ = false; confirmedSession_ = {};
        requireRequest(current);
        steam_ = steamcss::steamLease(session);
        if (!steam_) throw std::runtime_error("CSS use Steam session unverified");
        try {
            requireRequest(current);
            verifyFile(script_, L"steam-appearance.ps1");requireRequest(current);
            group_.open();requireRequest(current);group_.adopt(backend);requireRequest(current);
            useIdentity_=identity;setRequestLocked(current);session_ = session; backend_ = backend; token_ = ++generation_;
            usePowerGeneration_ = powerGeneration_ ? powerGeneration_() : 0;
            // Borrow the public owner scope, including same-generation suspend
            // and notification revocation. Metadata-only publications remain valid.
            if (expectedPower) usePower_ = *expectedPower;
            else if (powerReader_ && !sleeppower::capturePower(powerReader_, usePower_))
                throw std::runtime_error("CSS public power scope unavailable");
            holdUntil_ = GetTickCount64() + minimumHold_; due_ = GetTickCount64() + lifetime_;
            SetEvent(wake_); emit("begin", group_.describe()); return {token_, session.pid, session.created, backend_};
        } catch (...) { clearLocked(); throw; }
    }
    HANDLE launchLocked(uint64_t token, bool automatic, bool startBackend = false, bool verifyOnly = false) {
        if (!token || token != token_ || !captureAllowed() || GetTickCount64() >= due_)
            throw HelperLaunchError("lease-expired", ERROR_INVALID_STATE);
        // Ensure and trusted resource/DOM helpers retain the same verified host
        // identity. CSS has its own launch policy; Decky's shell-token route
        // must not make an elevated existing Steam/CSS family unreadable.
        // Missing-backend initialization parks on the original events before
        // Native admits the next phase. VerifyOnly still cannot initialize CSS.
        // Fixed actions/path only; no arbitrary shell command is accepted over IPC.
        wchar_t system[32768]{}; const auto count = GetSystemDirectoryW(system, static_cast<UINT>(std::size(system)));
        if (!count || count >= std::size(system)) throw HelperLaunchError("system-path", GetLastError());
        const auto exe = std::wstring(system, count) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
        auto command = L"\"" + exe + L"\" -NoLogo -NoProfile -ExecutionPolicy Bypass -File \"" + script_ +
            L"\" -Action " + (automatic ? L"Automatic -SessionPid " + std::to_wstring(session_.pid) +
                L" -SessionBirth " + std::to_wstring(session_.created) : std::wstring(L"Ensure")) + L" -BackendPath \"" + backend_ + L"\"";
        command += L" -ProcessQuery \"" + inventory_.name() + L"\"";
        if (!automatic) command += L" -SessionPid " + std::to_wstring(session_.pid) + L" -SessionBirth " + std::to_wstring(session_.created);
        if (automatic) {
            const auto names = openAutoEventsLocked();
            command += L" -ReadyEvent \"" + names.first + L"\" -IdleEvent \"" + names.second + L"\" -GateTimeoutMs " +
                std::wstring(L"-1") + (startBackend ? L" -StartBackend" : L"");
        }
        if (verifyOnly) {
            if (!automatic || startBackend) throw HelperLaunchError("read-only-helper-policy", ERROR_INVALID_STATE);
            command += L" -VerifyOnly";
        }
        verifyResidentControl(script_, automatic && startBackend);
        requireRequest(requestCurrent_);
        if (automatic && !currentUsePower()) throw HelperLaunchError("lease-expired", ERROR_INVALID_STATE);
        STARTUPINFOW startup{sizeof(startup)}; startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION child{};
        const auto launch = steamcsslaunch::create(exe, command, nullptr,
            std::filesystem::path(script_).parent_path().wstring(), startup, child);
        if (!launch.created) throw HelperLaunchError(launch.stage, launch.error, launch.mode);
        const auto abortChild = [&] {
            const bool terminated = TerminateProcess(child.hProcess, 1) != FALSE;
            const auto terminateError = terminated ? ERROR_SUCCESS : GetLastError();
            const auto wait = WaitForSingleObject(child.hProcess, 2000);
            const auto waitError = wait == WAIT_FAILED ? GetLastError() : ERROR_TIMEOUT;
            CloseHandle(child.hThread);CloseHandle(child.hProcess);child = {};
            if(wait != WAIT_OBJECT_0)throw HelperLaunchError("helper-cancel-wait",terminateError ? terminateError : waitError,launch.mode);
        };
        const auto revalidateChild = [&] {
            if (!currentUseRequest()) { abortChild(); throw std::runtime_error("BOOT_CSS_REQUEST_EXPIRED"); }
            if (automatic && !currentUsePower()) { abortChild(); throw HelperLaunchError("lease-expired", ERROR_INVALID_STATE, launch.mode); }
        };
        revalidateChild();
        const bool assigned = group_.assign(child.hProcess);
        const auto assignError = assigned ? ERROR_SUCCESS : GetLastError();
        revalidateChild();
        const bool resumed = assigned && ResumeThread(child.hThread) != static_cast<DWORD>(-1);
        if (!resumed) {
            const auto error = assigned ? GetLastError() : assignError;
            abortChild();
            throw HelperLaunchError(assigned ? "helper-resume" : "helper-job-assign", error, launch.mode);
        }
        revalidateChild();
        CloseHandle(child.hThread);
        auto status = group_.describe(); status.launchStage = launch.stage; status.launchMode = launch.mode;
        emit(automatic ? "automatic-helper-start" : "ensure-helper-start", status); return child.hProcess;
    }
    void startAutomaticLocked(bool initialize) {
        if (initialize && ensure_) throw HelperLaunchError("initialization-already-pending", ERROR_INVALID_STATE);
        initializationGatePending_ = initialize;
        try { helper_ = launchLocked(token_, true, initialize); }
        catch (...) { initializationGatePending_ = false; throw; }
    }
    void run() {
        ymcc::threadRole::describe(L"YMCC.CssUse");
        for (;;) {
            deckylaunch::Handle steam, helper, ensure, ready; uint64_t generation = 0; DWORD timeout = INFINITE;
            { std::lock_guard lock(mutex_);
              // A settings wake may coalesce with begin's old-generation wake.
              // Validate the current lease before blocking again as well.
              if (token_ && !currentUseRequest()) {
                  residentConfirmed_ = false; confirmedSession_ = {};
                  Cleanup status; status.error = ERROR_INVALID_STATE; status.launchStage = "use-lifecycle-changed";
                  emit("use-lifecycle-changed", status); clearLocked(false, false);
              }
              generation = generation_; steam.value = duplicate(steam_); helper.value = duplicate(helper_); ensure.value = duplicate(ensure_); ready.value = duplicate(autoReady_);
              // Only active work has a deadline. A past generation must never
              // turn the released worker into a zero-timeout CPU loop.
              if (token_ || cleanupPending_) {
                  auto next = readOnlyUntil_ ? std::min(due_, readOnlyUntil_) : due_;
                  if (token_ && !ensure_ && !autoReady_ && (idlePending_ || releasePending_)) next = std::min(next, holdUntil_);
                  const auto now = GetTickCount64(); timeout = now >= next ? 0 : static_cast<DWORD>(next - now);
              } }
            HANDLE handles[7]{stop_, wake_}; DWORD count = 2;
            const auto inventoryIndex = count; handles[count++] = inventory_.event();
            if (steam.value) handles[count++] = steam.value;
            if (helper.value) handles[count++] = helper.value;
            if (ensure.value) handles[count++] = ensure.value;
            if (ready.value) handles[count++] = ready.value;
            const auto result = WaitForMultipleObjects(count, handles, FALSE, timeout);
            if (result == WAIT_OBJECT_0 || result == WAIT_FAILED) break;
            if (result == WAIT_OBJECT_0 + 1) {
                // Acknowledge before validating current identity, so a later
                // publication cannot be lost behind a consumed wake.
                std::lock_guard wakeLock(requestWakeMutex_); identityWakePending_ = false;
            }
            if (result == WAIT_OBJECT_0 + inventoryIndex) {
                steamsession::Session current;uint64_t requestToken=0;
                { std::lock_guard lock(mutex_); current = observationKnown_ ? observed_ : session_;requestToken=token_; }
                const auto read=[&](const nlohmann::json& request,const std::function<bool()>& serialCurrent){
                    const auto scopeCurrent=[&]{
                        if(!serialCurrent())return false;
                        std::lock_guard lock(mutex_);
                        return stop_&&WaitForSingleObject(stop_,0)==WAIT_TIMEOUT&&
                            (!requestToken||(token_==requestToken&&captureAllowed()));
                    };
                    if(!sharedSteamRead_||!scopeCurrent())return nlohmann::json{{"ok",false},{"reason","shared-steam-request-expired"}};
                    return sharedSteamRead_(request,scopeCurrent);
                };
                inventory_.serve(current, !inventoryAllowed_ || inventoryAllowed_(),read,inventorySourceRead_);
                continue;
            }
            std::lock_guard lock(mutex_);
            // A previous group's timeout/exit cannot release a newer click.
            if (generation != generation_) continue;
            if (token_ && (!lifecycleAllowed() || !currentUsePower() || !currentUseRequest())) {
                residentConfirmed_ = false; confirmedSession_ = {};
                Cleanup status; status.error = ERROR_INVALID_STATE; status.launchStage = "use-lifecycle-changed";
                emit("use-lifecycle-changed", status); clearLocked(false, false); continue;
            }
            if (steam_ && WaitForSingleObject(steam_, 0) == WAIT_OBJECT_0) { clearLocked(false, false); continue; }
            if (ensure_ && WaitForSingleObject(ensure_, 0) == WAIT_OBJECT_0) {
                DWORD exitCode = 0; const bool exitKnown = GetExitCodeProcess(ensure_, &exitCode) != FALSE;
                const auto exitError = exitKnown ? 0 : GetLastError();
                CloseHandle(ensure_); ensure_ = nullptr;
                // Launch/initialization time does not consume the injection
                // window, including when the frontend already reported failure.
                holdUntil_ = GetTickCount64() + minimumHold_;
                due_ = std::max(due_, holdUntil_);
                Cleanup status; status.error = exitKnown ? exitCode : exitError; status.helperExitCode = exitCode; status.helperExitKnown = exitKnown;
                if (ensureNoticeToken_ && (ensureNoticeToken_ != token_ || !captureAllowed())) cancelEnsureNoticeLocked();
                status.useGeneration = std::exchange(ensureNoticeToken_, 0);
                emit("ensure-helper-complete", status);
            }
            if (helper_ && WaitForSingleObject(helper_, 0) == WAIT_OBJECT_0) {
                Cleanup status; status.helperExitKnown = GetExitCodeProcess(helper_, &status.helperExitCode) != FALSE;
                status.error = status.helperExitKnown ? status.helperExitCode : GetLastError();
                if (group_.editorState() != steamcsseditor::State::Absent) {
                    residentConfirmed_ = false; confirmedSession_ = {};
                    clearLocked(false, false, true); continue;
                }
                readHelperResultLocked(status);
                requireAutomaticConfirmation(status, idleReleased_);
                if (!idleReleased_) status.launchStage = "automatic-helper-exited-before-idle";
                emit(idleReleased_ ? "idle-helper-complete" : "automatic-helper-failed", status);
                if (status.helperFailureCode == 15) { clearLocked(false, false, true); continue; }
                if (!status.error) {
                    if (!readOnlyUntil_ && captureAllowed()) {
                        automaticAppliedPid_ = session_.pid;
                        automaticAppliedBirth_ = session_.created;
                        ++automaticAppliedRevision_;
                    }
                    confirmedSession_ = session_; confirmedIdentity_=useIdentity_; confirmedPowerGeneration_ = usePowerGeneration_; confirmedPower_ = usePower_; residentConfirmed_ = true; clearLocked(true); continue;
                }
                residentConfirmed_ = false; confirmedSession_ = {};
                // An unconfirmed DOM/status is NOT permission to end injection
                // immediately. Keep this one use for at most one more hold,
                // within its hard lifetime, with no scans/relaunch/retry loop.
                CloseHandle(helper_); helper_ = nullptr; closeAutoEventsLocked();
                idlePending_ = releasePending_ = false;
                const auto now = GetTickCount64(); due_ = std::min(due_, now + minimumHold_);
                status.deferred = status.holding = now < due_;
                emit("confirmation-failed-hold", status); continue;
            }
            if (autoReady_ && WaitForSingleObject(autoReady_, 0) == WAIT_OBJECT_0) {
                if (initializationGatePending_) {
                    Cleanup status;
                    // The helper has released ThemeControl and is blocked on
                    // idleGate_. This receipt never grants final confirmation.
                    const bool initialized = readAutomaticResult(resultView_, status) &&
                        status.helperPhase == 1 && !status.helperFailureCode &&
                        !status.helperFlags && !status.helperBackendCount && !status.helperHResult;
                    if (initialized) status.initializationTiming = readInitializationTiming(resultView_, automaticResultInitializationWords);
                    emit("automatic-initialization-ready", status);
                    const auto editor = group_.editorState();
                    if (!initialized || !captureAllowed() || editor != steamcsseditor::State::Absent) {
                        status.error = ERROR_INVALID_STATE;
                        status.editorProtected = editor != steamcsseditor::State::Absent;
                        status.editorStateUnknown = editor == steamcsseditor::State::Unknown;
                        emit("automatic-initialization-denied", status); clearLocked(false, false); continue;
                    }
                    // Reset ready before releasing initialization. The helper
                    // resets idle before doing work, then signals ready again.
                    if (!ResetEvent(autoReady_) || !SetEvent(idleGate_)) {
                        status.error = GetLastError();
                        if (!status.error) status.error = ERROR_INVALID_STATE;
                        emit("automatic-initialization-gate-failed", status); clearLocked(false, false); continue;
                    }
                    initializationGatePending_ = false;
                    holdUntil_ = GetTickCount64() + minimumHold_;
                    due_ = std::max(due_, holdUntil_);
                    emit("automatic-initialization-admitted", status);
                    continue;
                }
                CloseHandle(autoReady_); autoReady_ = nullptr;
                // Ready is emitted AFTER initialization and AFTER releasing the
                // theme mutex. The same process waits without searching Steam.
                holdUntil_ = GetTickCount64() + minimumHold_;
                if (!readOnlyUntil_) due_ = std::max(due_, holdUntil_ + std::min<DWORD>(15000, lifetime_ - minimumHold_));
                emit("automatic-helper-ready", group_.describe());
            }
            const auto now = GetTickCount64();
            if ((token_ || cleanupPending_) && now >= (readOnlyUntil_ ? std::min(due_, readOnlyUntil_) : due_)) {
                if (cleanupPending_) secondaryLocked(); else clearLocked(true);
            } else if (token_ && !ensure_ && !autoReady_ && now >= holdUntil_) {
                if (releasePending_) clearLocked(true);
                else if (idlePending_) {
                    idlePending_ = false;
                    if (group_.editorState() != steamcsseditor::State::Absent) { clearLocked(false, false); continue; }
                    // Never reacquire Explorer/token or create another process
                    // at the end of the hold. Resume our already verified helper.
                    if (!helper_ || !idleGate_ || !SetEvent(idleGate_)) {
                        Cleanup failure; failure.error = GetLastError(); if (!failure.error) failure.error = ERROR_INVALID_STATE;
                        failure.launchStage = "idle-event-signal"; emit("idle-helper-failed", failure); clearLocked(true);
                    } else {
                        idleReleased_ = true;
                        Cleanup status; status.launchStage = "existing-helper-event"; emit("idle-helper-start", status);
                    }
                }
            }
        }
        std::lock_guard lock(mutex_); SetEvent(stop_); clearLocked(true, false);
    }
public:
    explicit Runtime(bool allowActiveBackend = true, unsigned short backendPort = 35821,
        steamcsseditor::Probe editorProbe = steamcsseditor::probe)
        : group_({}, std::move(editorProbe)), allowActiveBackend_(allowActiveBackend), backendPort_(backendPort) {} // Isolated fixtures use their own port/disable discovery.
    ~Runtime() { shutdown(); }
    bool notifyRequestChanged() noexcept {
        try {
            std::lock_guard wakeLock(requestWakeMutex_);
            return requestScoped_ && requestWake_ && SetEvent(requestWake_) != FALSE;
        } catch (...) { return false; }
    }
    bool notifyIdentityPublished(const SharedIdentity& source) noexcept {
        try {
            std::lock_guard wakeLock(requestWakeMutex_);
            if (!requestWake_ || !requestIdentity_ || identityWakePending_) return false;
            const auto& expected = requestIdentity_;
            if (source && expected->processEpoch == source->processEpoch &&
                expected->accountEpoch == source->accountEpoch && expected->session == source->session &&
                expected->presence == source->presence) return false;
            // Revocation hint only. The existing worker rechecks the original
            // public current predicate; this callback never cancels or launches.
            if (!SetEvent(requestWake_)) return false;
            identityWakePending_ = true; return true;
        } catch (...) { return false; }
    }
    bool initialize(std::wstring script, std::wstring link, DWORD lifetime = 180000, DWORD cleanupDelay = 4000,
        std::function<void(const std::string&, const steamsession::Session&, const std::wstring&, const Cleanup&)> changed = {}, DWORD minimumHold = 0, std::function<bool()> inventoryAllowed = {}, std::function<ULONGLONG()> powerGeneration = {},
        std::function<std::shared_ptr<const steamsession::IdentitySnapshot>()> readIdentity = {},
        std::function<bool(const std::shared_ptr<const steamsession::IdentitySnapshot>&)> identityCurrent = {},
        steamcssinventory::Bridge::SharedRead sharedSteamRead = {},
        steamcssinventory::Bridge::SourceRead inventorySourceRead = {}, sleeppower::Reader powerReader = nullptr) {
        std::lock_guard lifecycle(lifecycle_); if (worker_.joinable()) return true;
        std::lock_guard lock(mutex_);
        if (!inventory_.initialize()) return false;
        inventoryAllowed_ = std::move(inventoryAllowed); powerGeneration_ = std::move(powerGeneration); powerReader_ = powerReader;
        readIdentity_=std::move(readIdentity);identityCurrent_=std::move(identityCurrent);
        sharedSteamRead_=std::move(sharedSteamRead);
        inventorySourceRead_=std::move(inventorySourceRead);
        useIdentity_.reset();confirmedIdentity_.reset();
        usePowerGeneration_ = confirmedPowerGeneration_ = 0; usePower_ = {}; confirmedPower_ = {};
        observationKnown_ = false; observed_ = {}; residentConfirmed_ = false; confirmedSession_ = {};
        script_ = std::move(script); link_ = std::move(link); minimumHold_ = minimumHold; lifetime_ = std::max(lifetime, minimumHold_); cleanupDelay_ = cleanupDelay; changed_ = std::move(changed);
        wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr); stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!wake_ || !stop_) { closeWakeLocked(); if (stop_) CloseHandle(stop_); stop_ = nullptr; return false; }
        { std::lock_guard wakeLock(requestWakeMutex_); requestWake_ = wake_; }
        try { worker_ = std::thread([this] { try { run(); } catch (...) { std::lock_guard lock(mutex_); SetEvent(stop_); clearLocked(true, false); } }); }
        catch (...) { closeWakeLocked(); CloseHandle(stop_); stop_ = nullptr; return false; }
        return true;
    }
    Ticket begin(const steamsession::Session& session, const std::function<bool()>& current = {}) {
        requireRequest(current);std::lock_guard lock(mutex_);requireRequest(current);
        const auto previousToken=token_;
        try { const auto ticket=beginLocked(session,true,current);requireRequest(current);return ticket; }
        catch (...) { if(token_&&token_!=previousToken)clearLocked(false,false);throw; }
    }
    bool invalidateForResume(const steamsession::Session& session, bool preserveCurrentManual = true,
        const sleeppower::SharedPowerV1* expectedPower = nullptr) {
        std::lock_guard lock(mutex_);
        // Another Ready may have acquired the Use mutex before this work.
        // Refuse the stale request before retiring any ticket or receipt.
        if (expectedPower) {
            try { (void)sharedIdentity(session); } catch (...) { return false; }
            if (!sleeppower::powerCurrent(powerReader_, *expectedPower)) return false;
        }
        const auto reason = !stop_ || WaitForSingleObject(stop_, 0) != WAIT_TIMEOUT ? "resume-use-worker-unavailable" :
            observationKnown_ && !steamcss::sameSession(observed_, session) ? "resume-use-observation-changed" :
            token_ && !steamcss::sameSession(session_, session) ? "resume-use-active-session-changed" : nullptr;
        if (reason) {
            Cleanup result; result.error = ERROR_INVALID_STATE; result.launchStage = reason;
            try { if (changed_) changed_(reason, session, backend_, result); } catch (...) {}
            return false;
        }
        // A user edit acquired in the CURRENT power episode owns its result.
        // Only old-episode manual work (or bounded automatic work) is revoked.
        if (preserveCurrentManual && token_ && !completed_ && captureAllowed()) {
            Cleanup result; result.error = ERROR_BUSY; result.launchStage = "resume-use-manual-busy";
            try { if (changed_) changed_("resume-use-manual-busy", session, backend_, result); } catch (...) {}
            return false;
        }
        // End only bounded pre-resume helpers/tickets. CSS observation handles
        // and Jobs are already non-destructive under the resident policy.
        residentConfirmed_ = false; confirmedSession_ = {};
        if (token_ || group_.hasWork()) clearLocked(false, false);
        if (wake_) SetEvent(wake_);
        return true;
    }
    bool residentConfirmed(const steamsession::Session& session) {
        std::lock_guard lock(mutex_);
        try { return residentConfirmed_ && lifecycleAllowed() && (!readIdentity_ || (confirmedIdentity_ && identityCurrent_ && identityCurrent_(confirmedIdentity_))) && (!powerGeneration_ || confirmedPowerGeneration_ == powerGeneration_()) &&
            (!powerReader_ || sleeppower::powerCurrent(powerReader_, confirmedPower_)) && steamcss::sameSession(confirmedSession_, session); }
        catch (...) { return false; }
    }
    std::wstring processQuery() { std::lock_guard lock(mutex_); return inventory_.name(); }
    struct ReadPolicy { bool ready = false, automaticRepairActive = false; std::wstring processQuery; ULONGLONG automaticAppliedRevision = 0; };
    ReadPolicy readPolicy(const steamsession::Session& session) {
        std::lock_guard lock(mutex_);
        const bool ready = stop_ && WaitForSingleObject(stop_, 0) == WAIT_TIMEOUT;
        // VerifyOnly and manual holds cannot take over repair from the card.
        const bool repairing = ready && token_ && completed_ && helper_ && !readOnlyUntil_ &&
            session == session_ && GetTickCount64() < due_ &&
            WaitForSingleObject(helper_, 0) == WAIT_TIMEOUT && captureAllowed();
        // A freshness stamp, not a claim that the current configuration is still applied.
        const auto applied = session.pid == automaticAppliedPid_ && session.created == automaticAppliedBirth_ ? automaticAppliedRevision_ : 0;
        return {ready, repairing, inventory_.name(), applied};
    }
    bool ensure(uint64_t token, const std::function<bool()>& current = {}) {
        requireRequest(current);std::lock_guard lock(mutex_);requireRequest(current);
        if (!token || token != token_) throw std::runtime_error("CSS use expired");
        if(!currentUseRequest()){clearLocked(false,false);throw std::runtime_error("BOOT_CSS_REQUEST_EXPIRED");}
        if(current)setRequestLocked(current);
        // A signaled helper still belongs to the worker until it consumes the
        // exit receipt. Do not replace it while a completion can be delivered.
        if (ensure_ && (WaitForSingleObject(ensure_, 0) == WAIT_TIMEOUT || (!current && ensureNoticeToken_ == token))) {
            if (!current) ensureNoticeToken_ = token;
            SetEvent(wake_); return true;
        }
        if (ensure_) { cancelEnsureNoticeLocked(); CloseHandle(ensure_); ensure_ = nullptr; }
        try { ensure_ = launchLocked(token, false); }
        catch (...) { emitHelperFailure("ensure-helper-failed");if(!currentUseRequest())clearLocked(false,false);throw; }
        if(!currentUseRequest()){clearLocked(false,false);throw std::runtime_error("BOOT_CSS_REQUEST_EXPIRED");}
        if (!current) ensureNoticeToken_ = token;
        SetEvent(wake_); return true;
    }
    HANDLE confirmAndIdle(const steamsession::Session& session, bool startBackend = false, bool renewForMode = false,
                          SharedIdentity* observedIdentity = nullptr, const sleeppower::SharedPowerV1* expectedPower = nullptr) {
        if (observedIdentity) observedIdentity->reset();
        std::lock_guard lock(mutex_);
        if (expectedPower && !sleeppower::powerCurrent(powerReader_, *expectedPower)) return nullptr;
        if (token_ && renewForMode && steamcss::sameSession(session, session_) && captureAllowed()) {
            if (expectedPower &&
                !sleeppower::samePowerRevocation(usePower_, *expectedPower) &&
                !sleeppower::samePowerRevocation(*expectedPower, usePower_)) return nullptr;
            if (readOnlyUntil_) cancelAutomaticLocked(); // A genuine new mode use owns its separate apply window.
            // Re-entering Big Picture needs a full injection window even if an
            // earlier startup/manual use is just about to clean up. Reuse it.
            holdUntil_ = GetTickCount64() + minimumHold_; due_ = GetTickCount64() + lifetime_;
            if (completed_) { // Automatic use; manual work owns its own cleanup.
                // Before gate release, renewal only moves a deadline. Once Idle
                // has begun, cancel that stale confirmation before a fresh use.
                try {
                    // startBackend means the shared guard has JUST verified a
                    // missing backend. A parked helper cannot initialize it by
                    // merely extending a deadline. An executing/finished Idle
                    // may also have closed CSS since that guard snapshot.
                    const bool initialize = startBackend || idleReleased_ ||
                        (helper_ && WaitForSingleObject(helper_, 0) == WAIT_OBJECT_0);
                    if (!initializationGatePending_) {
                        if (initialize && helper_) cancelAutomaticLocked();
                        if (!helper_) startAutomaticLocked(initialize);
                    }
                }
                catch (...) { emitHelperFailure("mode-ensure-failed"); clearLocked(true); return nullptr; }
                idlePending_ = true; releasePending_ = false;
            }
            emit("mode-hold-renewed", group_.describe()); SetEvent(wake_);
            const auto completion = duplicate(completed_);
            if (completion && observedIdentity) *observedIdentity = useIdentity_;
            return completion;
        }
        if (token_) return nullptr; // Manual edits already own their hold and cleanup.
        try {
            const auto ticket = beginLocked(session, false, {}, expectedPower);
            completed_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!completed_) throw std::runtime_error("CSS use completion unavailable");
            startAutomaticLocked(startBackend);
            idlePending_ = true; SetEvent(wake_);
            const auto completion = duplicate(completed_); // Signals after cleanup or manual takeover, not after Ensure.
            if (completion && observedIdentity) *observedIdentity = useIdentity_;
            return completion;
        } catch (...) { emitHelperFailure("idle-helper-failed"); clearLocked(true); return nullptr; }
    }
    HANDLE confirmExisting(const steamsession::Session& session) {
        std::lock_guard lock(mutex_);
        if (token_) return nullptr; // A newer click/automatic use keeps ownership.
        try {
            const auto ticket = beginLocked(session, false);
            completed_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!completed_) throw std::runtime_error("CSS read-only completion unavailable");
            due_ = GetTickCount64() + std::min<DWORD>(lifetime_, 30000);
            helper_ = launchLocked(ticket.token, true, false, true);
            readOnlyUntil_ = due_; // Ready and renewals cannot extend this verification deadline.
            idlePending_ = true; SetEvent(wake_);
            return duplicate(completed_);
        } catch (...) { emitHelperFailure("read-only-confirmation-failed"); clearLocked(true); return nullptr; }
    }
    Cleanup release(uint64_t token) {
        std::lock_guard lock(mutex_);
        if (!token || token != token_) { Cleanup stale;stale.stale=true;return stale; }
        if(!currentUseRequest()){const auto result=clearLocked(false,false);if(wake_)SetEvent(wake_);return result;}
        manualCompletion_ = true;
        if (ensure_ || GetTickCount64() < holdUntil_) {
            releasePending_ = true; idlePending_ = false;
            Cleanup result; result.deferred = result.holding = true; emit("release-deferred", result); SetEvent(wake_); return result;
        }
        const auto result = clearLocked(true); if (wake_) SetEvent(wake_); return result;
    }
    void observeSteam(const steamsession::Session& session) {
        std::lock_guard lock(mutex_); observed_ = session; observationKnown_ = true;
        if (!steamcss::sameSession(confirmedSession_, session) || (readIdentity_ && (!confirmedIdentity_ || !identityCurrent_ || !identityCurrent_(confirmedIdentity_)))) { residentConfirmed_ = false; confirmedSession_ = {}; confirmedIdentity_.reset(); }
        if ((token_ || cleanupPending_) && (!steamcss::sameSession(session, session_) || !currentUseRequest())) { clearLocked(false, false); if (wake_) SetEvent(wake_); }
    }
    void shutdown() {
        std::lock_guard lifecycle(lifecycle_);
        { std::lock_guard lock(mutex_); if (stop_) SetEvent(stop_); }
        if (worker_.joinable()) worker_.join();
        std::lock_guard lock(mutex_); clearLocked(true, false); residentConfirmed_ = false; confirmedSession_ = {};
        closeWakeLocked(); if (stop_) CloseHandle(stop_); stop_ = nullptr;
        inventory_.close();
    }
};
}
