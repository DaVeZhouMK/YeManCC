#pragma once
#include "steam_cssloader_guard.h"
#include "steam_cssloader_editor.h"
#include "steam_cssloader_launch.h"
#include <stdexcept>
#include <sddl.h>
#include <wincrypt.h>

// Bounded appearance helpers and an independently resident CSS backend.
// Jobs own helpers only; the one verified backend explicitly breaks away.
// Releasing a use, changing Steam or closing YMCC never terminates CSS.
namespace ymcc::steamcssuse {
inline void verifyFile(const std::wstring& path, const wchar_t* leaf) {
    if (path.empty() || _wcsicmp(std::filesystem::path(path).filename().c_str(), leaf))
        throw std::runtime_error("CSS use path invalid");
    const auto fileAttributes = GetFileAttributesW(path.c_str());
    if (fileAttributes == INVALID_FILE_ATTRIBUTES || (fileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        throw std::runtime_error("CSS use file missing");
    for (auto cursor = std::filesystem::path(path); !cursor.empty();) {
        const auto attributes = GetFileAttributesW(cursor.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("CSS use path unverified");
        const auto parent = cursor.parent_path(); if (parent == cursor) break; cursor = parent;
    }
}
inline void verifyResidentControl(const std::wstring& script) {
    verifyFile(script, L"steam-appearance.ps1");
    // Refuse a mixed old PowerControl deployment before any helper can start
    // a backend inside a kill-on-close Job. Packaging also verifies all files.
    const auto module = (std::filesystem::path(script).parent_path() / L"steam-appearance" / L"resident.ps1").wstring();
    verifyFile(module, L"resident.ps1");
    const auto renderer = (std::filesystem::path(script).parent_path() / L"steam-appearance" / L"idle.ps1").wstring();
    verifyFile(renderer, L"idle.ps1");
    for (const auto& file : {script, module, renderer}) {
        deckylaunch::Handle handle; handle.value = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        LARGE_INTEGER size{};
        if (handle.value == INVALID_HANDLE_VALUE) { handle.value = nullptr; throw std::runtime_error("CSS resident component unreadable"); }
        if (!GetFileSizeEx(handle.value, &size) || size.QuadPart <= 0 || size.QuadPart > 262144) throw std::runtime_error("CSS resident component size invalid");
        std::string text(static_cast<size_t>(size.QuadPart), '\0'); DWORD read = 0;
        if (!ReadFile(handle.value, text.data(), static_cast<DWORD>(text.size()), &read, nullptr) || read != text.size()) throw std::runtime_error("CSS resident component read incomplete");
        const bool entry = file == script;
        if (entry ? text.find("ResidentBackendPolicy=3") == std::string::npos || text.find("AutomaticResultProtocol=2") == std::string::npos || text.find("ManualObservationProtocol=1") == std::string::npos
                  : file == module ? text.find("function Start-ResidentAppearanceBackend") == std::string::npos
                  : text.find("AppearanceRenderProtocol=4") == std::string::npos || text.find("function Get-AppearanceVariableDefaults") == std::string::npos)
            throw std::runtime_error("CSS resident component version mismatch; update PowerControl together with YMCC");
    }
}
struct Cleanup {
    unsigned verified = 0, fallback = 0, remaining = 0, unverified = 0;
    std::vector<std::pair<DWORD, ULONGLONG>> identities;
    std::string launchStage, launchMode;
    DWORD helperExitCode = 0; bool helperExitKnown = false;
    bool helperResultKnown = false;
    DWORD helperPhase = 0, helperFailureCode = 0, helperFlags = 0, helperBackendCount = 0, helperHResult = 0;
    bool editorProtected = false, editorStateUnknown = false;
    uint64_t useGeneration = 0;
    DWORD error = 0; bool deferred = false, holding = false, stale = false;
    bool ok() const { return !error && !remaining && !stale && !editorProtected; }
};
// Fixed 32-byte per-use result; no stdout pipe, files, paths or secrets.
// The same-user medium-integrity mapping is written before helper exit. Native
// only reads it after the pinned process is signaled; magic is committed last.
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
        deckylaunch::Handle snapshot;
        snapshot.value = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot.value == INVALID_HANDLE_VALUE) { snapshot.value = nullptr; throw std::runtime_error("CSS use snapshot unavailable"); }
        PROCESSENTRY32W entry{sizeof(entry)};
        if (!Process32FirstW(snapshot.value, &entry)) throw std::runtime_error("CSS use snapshot unreadable");
        DWORD ourSession = 0;
        if (!ProcessIdToSessionId(GetCurrentProcessId(), &ourSession)) throw std::runtime_error("CSS use session unverified");
        do {
            if (_wcsicmp(entry.szExeFile, L"CssLoader-Standalone-Headless.exe")) continue;
            DWORD targetSession = 0;
            if (!ProcessIdToSessionId(entry.th32ProcessID, &targetSession) || targetSession != ourSession) continue;
            deckylaunch::Handle process;
            process.value = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
            if (!process.value) { ++unverified_; continue; }
            if (!steamcss::ownProcess(process.value)) continue; // Never another user or an inaccessible PID.
            wchar_t image[32768]{}; DWORD size = static_cast<DWORD>(std::size(image));
            if (!QueryFullProcessImageNameW(process.value, 0, image, &size) || _wcsicmp(image, executable.c_str()) ||
                WaitForSingleObject(process.value, 0) != WAIT_TIMEOUT) continue;
            if (std::any_of(adopted_.begin(), adopted_.end(), [&](HANDLE existing) {
                return GetProcessId(existing) == entry.th32ProcessID && WaitForSingleObject(existing, 0) == WAIT_TIMEOUT;
            })) continue;
            if (adopted_.size() >= 128) throw std::runtime_error("CSS use process limit");
            // Observation handles carry no terminate or Job assignment rights.
            adopted_.push_back(process.value); process.value = nullptr;
        } while (Process32NextW(snapshot.value, &entry));
        if (GetLastError() != ERROR_NO_MORE_FILES) throw std::runtime_error("CSS use snapshot incomplete");
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
class Runtime {
    std::mutex lifecycle_, mutex_;
    Group group_; steamsession::Session session_, observed_, confirmedSession_; bool observationKnown_ = false, residentConfirmed_ = false;
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
    void readHelperResultLocked(Cleanup& status) { readAutomaticResult(resultView_, status); }
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
        if (automaticAfterEnsure_ && ensure_) {
            if (WaitForSingleObject(ensure_, 0) == WAIT_TIMEOUT && !TerminateProcess(ensure_, 1) && WaitForSingleObject(ensure_, 0) != WAIT_OBJECT_0)
                throw HelperLaunchError("automatic-initialization-cancel", GetLastError());
            if (WaitForSingleObject(ensure_, 2000) != WAIT_OBJECT_0)
                throw HelperLaunchError("automatic-initialization-cancel-wait", ERROR_TIMEOUT);
            CloseHandle(ensure_); ensure_ = nullptr;
        }
        automaticAfterEnsure_ = false;
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
            resultMap_ = CreateFileMappingW(INVALID_HANDLE_VALUE, &attributes, PAGE_READWRITE, 0, 32, (name + L".result").c_str());
            error = GetLastError();
            if (resultMap_ && error != ERROR_ALREADY_EXISTS) {
                resultView_ = static_cast<const DWORD*>(MapViewOfFile(resultMap_, FILE_MAP_READ, 0, 0, 32));
                if (!resultView_) error = GetLastError();
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
    ULONGLONG due_ = 0, holdUntil_ = 0, readOnlyUntil_ = 0; DWORD lifetime_ = 180000, cleanupDelay_ = 4000, minimumHold_ = 0;
    bool idlePending_ = false, releasePending_ = false, manualCompletion_ = false;
    bool automaticAfterEnsure_ = false;
    bool cleanupPending_ = false, allowActiveBackend_ = true;
    unsigned short backendPort_ = 35821;
    std::function<void(const std::string&, const steamsession::Session&, const std::wstring&, const Cleanup&)> changed_;
    void emit(const std::string& stage, const Cleanup& result) {
        try { if (changed_) changed_(stage, session_, backend_, result); } catch (...) {}
    }
    void emitHelperFailure(const char* stage) {
        try { throw; }
        catch (const HelperLaunchError& error) { emit(stage, error.detail); }
        catch (...) { Cleanup failure; failure.error = ERROR_NOT_READY; failure.launchStage = "use-precondition-or-exception"; emit(stage, failure); }
    }
    bool captureAllowed() const {
        return steam_ && !backend_.empty() && WaitForSingleObject(steam_, 0) == WAIT_TIMEOUT &&
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
        closeAutoEventsLocked(); cleanupPending_ = false; automaticAfterEnsure_ = false;
        if (hadUse || result.verified || result.error) emit("resident-backend-retained", result);
        if (manualEnded && !result.error && !editorTookOver) {
            result.useGeneration = endedGeneration;
            emit("manual-use-ended", result);
        }
        token_ = 0; holdUntil_ = 0; readOnlyUntil_ = 0; idlePending_ = releasePending_ = false;
        if (completed_) { SetEvent(completed_); CloseHandle(completed_); completed_ = nullptr; }
        if (steam_) { CloseHandle(steam_); steam_ = nullptr; }
        session_ = {}; backend_.clear(); due_ = 0;
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
    Ticket beginLocked(const steamsession::Session& session, bool manual) {
        if (!wake_ || !stop_ || WaitForSingleObject(stop_, 0) != WAIT_TIMEOUT) throw std::runtime_error("CSS use stopped");
        if (observationKnown_ && !steamcss::sameSession(session, observed_)) throw std::runtime_error("CSS use Steam event changed");
        const auto configured = steamcss::readLink(link_);
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
        if (token_ && _wcsicmp(backend.c_str(), backend_.c_str()) == 0 && steamcss::sameSession(session, session_) && WaitForSingleObject(steam_, 0) == WAIT_TIMEOUT) {
            if (manual && (helper_ || automaticAfterEnsure_)) {
                try { cancelAutomaticLocked(); } catch (...) { emitHelperFailure("automatic-cancel-failed"); clearLocked(true); throw; }
            }
            if (manual) { token_ = ++generation_; manualCompletion_ = false; } // Stale completion cannot end the next click.
            if (manual) {
                idlePending_ = releasePending_ = false;
                if (completed_) { SetEvent(completed_); CloseHandle(completed_); completed_ = nullptr; }
            }
            holdUntil_ = GetTickCount64() + minimumHold_;
            due_ = GetTickCount64() + lifetime_; SetEvent(wake_);
            return {token_, session_.pid, session_.created, backend_};
        }
        clearLocked(); residentConfirmed_ = false; confirmedSession_ = {};
        steam_ = steamcss::steamLease(session);
        if (!steam_) throw std::runtime_error("CSS use Steam session unverified");
        try {
            verifyFile(script_, L"steam-appearance.ps1");
            group_.open(); group_.adopt(backend);
            session_ = session; backend_ = backend; token_ = ++generation_;
            holdUntil_ = GetTickCount64() + minimumHold_; due_ = GetTickCount64() + lifetime_;
            SetEvent(wake_); emit("begin", group_.describe()); return {token_, session.pid, session.created, backend_};
        } catch (...) { clearLocked(); throw; }
    }
    HANDLE launchLocked(uint64_t token, bool automatic, bool startBackend = false, bool verifyOnly = false) {
        if (!token || token != token_ || !steam_ || WaitForSingleObject(steam_, 0) != WAIT_TIMEOUT || GetTickCount64() >= due_)
            throw HelperLaunchError("lease-expired", ERROR_INVALID_STATE);
        // Ensure and trusted resource/DOM helpers retain the same verified host
        // identity. CSS has its own launch policy; Decky's shell-token route
        // must not make an elevated existing Steam/CSS family unreadable.
        if (automatic && startBackend) throw HelperLaunchError("automatic-initialization-policy", ERROR_INVALID_STATE);
        // Fixed actions/path only; no arbitrary shell command is accepted over IPC.
        wchar_t system[32768]{}; const auto count = GetSystemDirectoryW(system, static_cast<UINT>(std::size(system)));
        if (!count || count >= std::size(system)) throw HelperLaunchError("system-path", GetLastError());
        const auto exe = std::wstring(system, count) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
        auto command = L"\"" + exe + L"\" -NoLogo -NoProfile -ExecutionPolicy Bypass -File \"" + script_ +
            L"\" -Action " + (automatic ? L"Automatic -SessionPid " + std::to_wstring(session_.pid) +
                L" -SessionBirth " + std::to_wstring(session_.created) : std::wstring(L"Ensure")) + L" -BackendPath \"" + backend_ + L"\"";
        if (automatic) {
            const auto names = openAutoEventsLocked();
            command += L" -ReadyEvent \"" + names.first + L"\" -IdleEvent \"" + names.second + L"\" -GateTimeoutMs " +
                std::wstring(L"-1") + (startBackend ? L" -StartBackend" : L"");
        }
        if (verifyOnly) {
            if (!automatic || startBackend) throw HelperLaunchError("read-only-helper-policy", ERROR_INVALID_STATE);
            command += L" -VerifyOnly";
        }
        verifyResidentControl(script_);
        STARTUPINFOW startup{sizeof(startup)}; startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION child{};
        const auto launch = steamcsslaunch::create(exe, command, nullptr,
            std::filesystem::path(script_).parent_path().wstring(), startup, child);
        if (!launch.created) throw HelperLaunchError(launch.stage, launch.error, launch.mode);
        const bool assigned = group_.assign(child.hProcess);
        const bool resumed = assigned && ResumeThread(child.hThread) != static_cast<DWORD>(-1);
        if (!resumed) {
            const auto error = GetLastError(); // Capture before termination/handle cleanup replaces it.
            TerminateProcess(child.hProcess, 1); CloseHandle(child.hThread); CloseHandle(child.hProcess);
            throw HelperLaunchError(assigned ? "helper-resume" : "helper-job-assign", error, launch.mode);
        }
        CloseHandle(child.hThread);
        auto status = group_.describe(); status.launchStage = launch.stage; status.launchMode = launch.mode;
        emit(automatic ? "automatic-helper-start" : "ensure-helper-start", status); return child.hProcess;
    }
    void startAutomaticLocked(bool initialize) {
        if (initialize) {
            if (ensure_) throw HelperLaunchError("initialization-already-pending", ERROR_INVALID_STATE);
            ensure_ = launchLocked(token_, false);
            automaticAfterEnsure_ = true;
        } else helper_ = launchLocked(token_, true);
    }
    void run() {
        for (;;) {
            deckylaunch::Handle steam, helper, ensure, ready; uint64_t generation = 0; DWORD timeout = INFINITE;
            { std::lock_guard lock(mutex_);
              generation = generation_; steam.value = duplicate(steam_); helper.value = duplicate(helper_); ensure.value = duplicate(ensure_); ready.value = duplicate(autoReady_);
              // Only active work has a deadline. A past generation must never
              // turn the released worker into a zero-timeout CPU loop.
              if (token_ || cleanupPending_) {
                  auto next = readOnlyUntil_ ? std::min(due_, readOnlyUntil_) : due_;
                  if (token_ && !ensure_ && !autoReady_ && (idlePending_ || releasePending_)) next = std::min(next, holdUntil_);
                  const auto now = GetTickCount64(); timeout = now >= next ? 0 : static_cast<DWORD>(next - now);
              } }
            HANDLE handles[6]{stop_, wake_}; DWORD count = 2;
            if (steam.value) handles[count++] = steam.value;
            if (helper.value) handles[count++] = helper.value;
            if (ensure.value) handles[count++] = ensure.value;
            if (ready.value) handles[count++] = ready.value;
            const auto result = WaitForMultipleObjects(count, handles, FALSE, timeout);
            if (result == WAIT_OBJECT_0 || result == WAIT_FAILED) break;
            std::lock_guard lock(mutex_);
            // A previous group's timeout/exit cannot release a newer click.
            if (generation != generation_) continue;
            if (steam_ && WaitForSingleObject(steam_, 0) == WAIT_OBJECT_0) { clearLocked(false, false); continue; }
            if (ensure_ && WaitForSingleObject(ensure_, 0) == WAIT_OBJECT_0) {
                DWORD exitCode = 0; const bool exitKnown = GetExitCodeProcess(ensure_, &exitCode) != FALSE;
                const auto exitError = exitKnown ? 0 : GetLastError();
                CloseHandle(ensure_); ensure_ = nullptr;
                // Launch/initialization time does not consume the injection
                // window, including when the frontend already reported failure.
                holdUntil_ = GetTickCount64() + minimumHold_;
                due_ = std::max(due_, holdUntil_);
                Cleanup status; status.error = exitKnown ? exitCode : exitError; status.helperExitCode = exitCode; status.helperExitKnown = exitKnown; emit("ensure-helper-complete", status);
                if (automaticAfterEnsure_) {
                    automaticAfterEnsure_ = false;
                    if (!exitKnown || exitCode || !captureAllowed() || group_.editorState() != steamcsseditor::State::Absent) {
                        emit("automatic-initialization-failed", status); clearLocked(false, false); continue;
                    }
                    try { helper_ = launchLocked(token_, true); }
                    catch (...) { emitHelperFailure("automatic-confirmation-launch-failed"); clearLocked(false, false); continue; }
                }
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
                if (!status.error) { confirmedSession_ = session_; residentConfirmed_ = true; clearLocked(true); continue; }
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
    bool initialize(std::wstring script, std::wstring link, DWORD lifetime = 180000, DWORD cleanupDelay = 4000,
        std::function<void(const std::string&, const steamsession::Session&, const std::wstring&, const Cleanup&)> changed = {}, DWORD minimumHold = 0) {
        std::lock_guard lifecycle(lifecycle_); if (worker_.joinable()) return true;
        std::lock_guard lock(mutex_);
        observationKnown_ = false; observed_ = {}; residentConfirmed_ = false; confirmedSession_ = {};
        script_ = std::move(script); link_ = std::move(link); minimumHold_ = minimumHold; lifetime_ = std::max(lifetime, minimumHold_); cleanupDelay_ = cleanupDelay; changed_ = std::move(changed);
        wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr); stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!wake_ || !stop_) { if (wake_) CloseHandle(wake_); if (stop_) CloseHandle(stop_); wake_ = stop_ = nullptr; return false; }
        try { worker_ = std::thread([this] { try { run(); } catch (...) { std::lock_guard lock(mutex_); SetEvent(stop_); clearLocked(true, false); } }); }
        catch (...) { CloseHandle(wake_); CloseHandle(stop_); wake_ = stop_ = nullptr; return false; }
        return true;
    }
    Ticket begin(const steamsession::Session& session) { std::lock_guard lock(mutex_); return beginLocked(session, true); }
    bool invalidateForResume(const steamsession::Session& session) {
        std::lock_guard lock(mutex_);
        if (!stop_ || WaitForSingleObject(stop_, 0) != WAIT_TIMEOUT ||
            (observationKnown_ && !steamcss::sameSession(observed_, session)) ||
            (token_ && !steamcss::sameSession(session_, session))) return false;
        // End only bounded pre-resume helpers/tickets. CSS observation handles
        // and Jobs are already non-destructive under the resident policy.
        residentConfirmed_ = false; confirmedSession_ = {};
        if (token_ || group_.hasWork()) clearLocked(false, false);
        if (wake_) SetEvent(wake_);
        return true;
    }
    bool residentConfirmed(const steamsession::Session& session) { std::lock_guard lock(mutex_); return residentConfirmed_ && steamcss::sameSession(confirmedSession_, session); }
    bool ready() { std::lock_guard lock(mutex_); return stop_ && WaitForSingleObject(stop_, 0) == WAIT_TIMEOUT; }
    bool ensure(uint64_t token) {
        std::lock_guard lock(mutex_);
        if (!token || token != token_) throw std::runtime_error("CSS use expired");
        if (ensure_ && WaitForSingleObject(ensure_, 0) == WAIT_TIMEOUT) return true;
        if (ensure_) { CloseHandle(ensure_); ensure_ = nullptr; }
        try { ensure_ = launchLocked(token, false); }
        catch (...) { emitHelperFailure("ensure-helper-failed"); throw; }
        SetEvent(wake_); return true;
    }
    HANDLE confirmAndIdle(const steamsession::Session& session, bool startBackend = false, bool renewForMode = false) {
        std::lock_guard lock(mutex_);
        if (token_ && renewForMode && steamcss::sameSession(session, session_) && captureAllowed()) {
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
                    if (!automaticAfterEnsure_) {
                        if (initialize && helper_) cancelAutomaticLocked();
                        if (!helper_) startAutomaticLocked(initialize);
                    }
                }
                catch (...) { emitHelperFailure("mode-ensure-failed"); clearLocked(true); return nullptr; }
                idlePending_ = true; releasePending_ = false;
            }
            emit("mode-hold-renewed", group_.describe()); SetEvent(wake_); return duplicate(completed_);
        }
        if (token_) return nullptr; // Manual edits already own their hold and cleanup.
        try {
            const auto ticket = beginLocked(session, false);
            completed_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!completed_) throw std::runtime_error("CSS use completion unavailable");
            startAutomaticLocked(startBackend);
            idlePending_ = true; SetEvent(wake_);
            return duplicate(completed_); // Signals after cleanup or manual takeover, not after Ensure.
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
        manualCompletion_ = true;
        if (ensure_ || GetTickCount64() < holdUntil_) {
            releasePending_ = true; idlePending_ = false;
            Cleanup result; result.deferred = result.holding = true; emit("release-deferred", result); SetEvent(wake_); return result;
        }
        const auto result = clearLocked(true); if (wake_) SetEvent(wake_); return result;
    }
    void observeSteam(const steamsession::Session& session) {
        std::lock_guard lock(mutex_); observed_ = session; observationKnown_ = true;
        if (!steamcss::sameSession(confirmedSession_, session)) { residentConfirmed_ = false; confirmedSession_ = {}; }
        if ((token_ || cleanupPending_) && !steamcss::sameSession(session, session_)) { clearLocked(false, false); if (wake_) SetEvent(wake_); }
    }
    void shutdown() {
        std::lock_guard lifecycle(lifecycle_);
        { std::lock_guard lock(mutex_); if (stop_) SetEvent(stop_); }
        if (worker_.joinable()) worker_.join();
        std::lock_guard lock(mutex_); clearLocked(true, false); residentConfirmed_ = false; confirmedSession_ = {};
        if (wake_) CloseHandle(wake_); if (stop_) CloseHandle(stop_); wake_ = stop_ = nullptr;
    }
};
}
