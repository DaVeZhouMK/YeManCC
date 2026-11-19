#pragma once
#include <windows.h>
#include <shellapi.h>
#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include "steam_session_observer.h"
#include "steam_game_request_cancellation.h"

// PRIVATE SOURCE CANDIDATE; not built, tested, imported or SOURCE_READY.
// Facts for a game already selected by existing rules. No process discovery,
// tracker reader/cache, browser selection, CDP, worker, timer or action authority.
// Observer is borrowed for this request and MUST outlive every ProcessLease and
// OverlayHostLease created from it. Main uses its existing global Observer.
// This is in-process ownership only, never a transferable process ticket.
// current() is process-only. Final account/document/power/OFF/foreground/rules/
// config and request admission remain with the caller and common transport.
namespace ymcc::steamgamefacts {
using Observer = ymcc::steamsession::Observer;
using IdentitySnapshot = ymcc::steamsession::IdentitySnapshot;

struct ProcessWitness {
    DWORD pid = 0;
    std::uint64_t created = 0;
    std::wstring image;
};

inline bool samePath(std::wstring a, std::wstring b) {
    for (auto& c : a) if (c == L'/') c = L'\\';
    for (auto& c : b) if (c == L'/') c = L'\\';
    return !a.empty() && _wcsicmp(a.c_str(), b.c_str()) == 0;
}
template<class T> inline bool decimal(const std::wstring& text, T& result) {
    static_assert(std::is_unsigned_v<T>);
    if (text.empty() || text.size() > 20 || (text.size() > 1 && text[0] == L'0')) return false;
    T value = 0;
    for (const auto c : text) {
        if (c < L'0' || c > L'9' || value > ((std::numeric_limits<T>::max)() - T(c - L'0')) / 10) return false;
        value = value * 10 + T(c - L'0');
    }
    if (!value) return false;
    result = value; return true;
}
struct Arguments { DWORD gamePid = 0, steamPid = 0; std::uint64_t gameId = 0; };
inline std::optional<Arguments> arguments(const std::wstring& command) {
    if (command.empty() || command.size() > 32767 || command.find(L'\0') != std::wstring::npos) return {};
    int count = 0; LPWSTR* argv = CommandLineToArgvW(command.c_str(), &count);
    if (!argv) return {};
    // The old parser's accepted language is unchanged. The argv allocation now
    // has an RAII guard as well, including on std::wstring allocation failure.
    const auto freeArguments = [](LPWSTR* value) noexcept { if (value) LocalFree(value); };
    std::unique_ptr<LPWSTR, decltype(freeArguments)> ownedArguments(argv, freeArguments);
    Arguments result; unsigned mask = 0; bool valid = count >= 7 && count <= 64;
    for (int i = 1; valid && i < count; ++i) {
        const std::wstring key = argv[i];
        const unsigned bit = key == L"-pid" ? 1 : key == L"-steampid" ? 2 : key == L"-gameid" ? 4 : 0;
        if (!bit) continue;
        if ((mask & bit) || ++i >= count) { valid = false; break; }
        const std::wstring value = argv[i]; mask |= bit;
        valid = bit == 1 ? decimal(value, result.gamePid) : bit == 2 ? decimal(value, result.steamPid) : decimal(value, result.gameId);
    }
    return valid && mask == 7 ? std::optional<Arguments>(result) : std::nullopt;
}
inline bool exactGameId(std::uint32_t appid, std::uint64_t gameid) noexcept {
    return appid && ((appid <= 0xffffff && gameid == appid) ||
        ((gameid >> 32) == appid && (gameid & 0xffffffffULL) == 0x02000000ULL));
}
inline bool argumentsMatch(const Arguments& args, DWORD gamePid, DWORD steamPid,
    std::uint32_t appid, std::uint64_t fullGameId) noexcept {
    if (!gamePid || !steamPid || gamePid == steamPid || !exactGameId(appid, fullGameId) ||
        args.gamePid != gamePid || args.steamPid != steamPid) return false;
    // The independently verified tracker supplies the complete ID. A host that
    // received exactly its short ID is a known repair candidate, never proof by
    // low24 AppID alone or permission to accept another complete game identity.
    return args.gameId == fullGameId || ((fullGameId >> 32) == appid && args.gameId == appid);
}

// Supplied by the separate shared tracker producer; this header does not read
// a tracker, certify its provenance or derive full IDs from a registration.
struct TrackerIdentity {
    std::uint32_t appid = 0;
    std::uint64_t fullGameId = 0;
};
// Verbatim reported metadata, separate from the tracker full identity. AppID
// action compatibility (the old menu registeredPair) remains with the consumer.
struct ReportedRegistration {
    std::uint32_t appid = 0;
    std::uint64_t gameId = 0;
};
enum class RegistrationFact {
    Unknown,
    Exact,
    ShortIdBoundToHeldGame,
    ReportedAliasBoundToHeldGame,
    CanonicalFullIdReportedShortBoundToHeldGame
};
inline RegistrationFact registrationFact(const Arguments& args, DWORD gamePid,
    DWORD steamPid, const TrackerIdentity& tracker,
    const std::optional<ReportedRegistration>& reported = std::nullopt) noexcept {
    if (!gamePid || !steamPid || gamePid == steamPid ||
        !exactGameId(tracker.appid, tracker.fullGameId) ||
        args.gamePid != gamePid || args.steamPid != steamPid) return RegistrationFact::Unknown;
    // A canonical Type2 command-line full ID may be reported by the UI as the
    // tracker's complete short ID. Keep this fact distinct from Exact/Short/
    // ReportedAlias; the common guard above already binds both held PIDs.
    if (reported && reported->appid &&
        (tracker.fullGameId >> 32) == tracker.appid &&
        (tracker.fullGameId & 0xffffffffULL) == 0x02000000ULL &&
        args.gameId == tracker.fullGameId && reported->gameId == tracker.appid &&
        (reported->appid == tracker.appid ||
            reported->appid == (tracker.appid & 0xffffffU)))
        return RegistrationFact::CanonicalFullIdReportedShortBoundToHeldGame;
    if (reported && (!reported->appid || !reported->gameId ||
        args.gameId != reported->gameId)) return RegistrationFact::Unknown;
    if (argumentsMatch(args, gamePid, steamPid, tracker.appid, tracker.fullGameId)) {
        return args.gameId == tracker.fullGameId ? RegistrationFact::Exact :
            RegistrationFact::ShortIdBoundToHeldGame;
    }
    // Preserve only the old inspectHost registered-ID candidate branch. This is
    // NOT equality of full IDs and NOT a low24 comparison against the tracker.
    // It needs an explicit identical reported ID AND the held game/Steam PID
    // pair. The consumer still owns registeredPair and any alias repair action.
    if (reported && (tracker.fullGameId >> 32) == tracker.appid &&
        reported->gameId <= 0xffffffULL) return RegistrationFact::ReportedAliasBoundToHeldGame;
    return RegistrationFact::Unknown;
}

namespace detail {
struct Handle {
    HANDLE value = nullptr;
    Handle() = default;
    explicit Handle(HANDLE h) : value(h) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value(std::exchange(other.value, nullptr)) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) { if (value) CloseHandle(value); value = std::exchange(other.value, nullptr); }
        return *this;
    }
    ~Handle() { if (value) CloseHandle(value); }
};
inline std::uint64_t birth(HANDLE h) noexcept {
    FILETIME created{}, exit{}, kernel{}, user{};
    if (!h || !GetProcessTimes(h, &created, &exit, &kernel, &user)) return 0;
    return (std::uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
}
inline std::wstring image(HANDLE h) {
    std::wstring out(32768, L'\0'); DWORD count = static_cast<DWORD>(out.size());
    if (!h || !QueryFullProcessImageNameW(h, 0, out.data(), &count) || !count) return {};
    out.resize(count); return out;
}
inline bool alive(HANDLE h) noexcept {
    return h && WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
}
inline bool complete(const ProcessWitness& value) noexcept {
    return value.pid && value.created && !value.image.empty() &&
        value.image.size() <= 32767 && value.image.find(L'\0') == std::wstring::npos;
}
struct PinnedProcess {
    ProcessWitness witness;
    Handle handle;
    DWORD windowsSession = 0;
};
inline std::optional<PinnedProcess> pin(const ProcessWitness& expected) {
    if (!complete(expected)) return {};
    PinnedProcess result;
    result.handle = Handle(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, expected.pid));
    if (!alive(result.handle.value)) return {};
    // Once per acquired HANDLE: PID, birth, path. The handle keeps referring to
    // this object for its entire lifetime; current() must not repeat these reads.
    result.witness = { GetProcessId(result.handle.value), birth(result.handle.value), image(result.handle.value) };
    if (result.witness.pid != expected.pid || result.witness.created != expected.created ||
        !samePath(result.witness.image, expected.image) ||
        !ProcessIdToSessionId(result.witness.pid, &result.windowsSession) ||
        !alive(result.handle.value)) return {};
    // Session lookup is PID-based Win32 API. Liveness before and after it on
    // the same held handle excludes a successful observation of a reused PID.
    return std::optional<PinnedProcess>(std::move(result));
}
using Query = LONG (NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
inline Query processQuery() noexcept {
    static const auto fn = reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess"));
    return fn;
}
inline DWORD parent(HANDLE h) noexcept {
    struct Basic { LONG status; PVOID peb; ULONG_PTR affinity; LONG priority; ULONG_PTR pid, parent; } value{};
    const auto query = processQuery();
    if (!query || query(h, 0, &value, sizeof(value), nullptr) < 0 || value.parent > MAXDWORD) return 0;
    return static_cast<DWORD>(value.parent);
}
inline std::optional<std::wstring> commandLine(HANDLE h) {
    const auto query = processQuery(); if (!query) return {};
    ULONG required = 0; query(h, 60, nullptr, 0, &required);
    struct Unicode { USHORT length, maximum; PWSTR buffer; };
    if (required < sizeof(Unicode) || required > 131072) return {};
    std::vector<unsigned char> bytes(required);
    if (query(h, 60, bytes.data(), required, &required) < 0 || required > bytes.size()) return {};
    const auto value = reinterpret_cast<const Unicode*>(bytes.data());
    const auto start = reinterpret_cast<std::uintptr_t>(bytes.data()), end = start + bytes.size();
    const auto buffer = reinterpret_cast<std::uintptr_t>(value->buffer);
    if (value->length % sizeof(wchar_t) || value->length > value->maximum ||
        value->length > 65534 || buffer < start || buffer > end || value->length > end - buffer) return {};
    const std::wstring out(value->buffer, value->length / sizeof(wchar_t));
    return out.find(L'\0') == std::wstring::npos ? std::optional<std::wstring>(out) : std::nullopt;
}
}

class ProcessOwner;
class ProcessLease;
class ProcessGroupLease;
class TrackerOwner;
struct ProcessGroupSeed;
struct ProcessGroupCapture;
class OverlayHostLease;
struct HostInspection;

class Request final {
    friend class ProcessParentEvidenceOwner;
    friend class ProcessOwner;
    friend class ProcessLease;
    friend class OverlayHostLease;
    friend class ProcessGroupLease;
    Observer* const observer_; // Borrowed; see lifetime contract at the top.
    const std::shared_ptr<const IdentitySnapshot> expected_;
    const ProcessWitness game_, steam_, selectedGame_;
    detail::Handle gameHandle_, steamHandle_;
    const DWORD creatorPid_, windowsSession_;
    std::atomic<bool> cancelled_{false};
    mutable std::atomic<std::shared_ptr<RequestCancellation>> cancellationSignal_{};

    Request(Observer& observer, std::shared_ptr<const IdentitySnapshot> expected,
        detail::PinnedProcess game, detail::PinnedProcess steam, ProcessWitness selectedGame,
        DWORD ownPid, DWORD session)
        : observer_(&observer), expected_(std::move(expected)), game_(std::move(game.witness)),
          steam_(std::move(steam.witness)), selectedGame_(std::move(selectedGame)), gameHandle_(std::move(game.handle)),
          steamHandle_(std::move(steam.handle)), creatorPid_(ownPid), windowsSession_(session) {}
    bool sameProcessPublication(const std::shared_ptr<const IdentitySnapshot>& now) const noexcept {
        return now && now->presence == 1 && now->rootReadError == ERROR_SUCCESS &&
            now->processEpoch == expected_->processEpoch &&
            now->session.pid == steam_.pid && now->session.created == steam_.created &&
            now->session.root == expected_->session.root && now->windowsSessionKnown &&
            now->windowsSessionError == ERROR_SUCCESS && now->windowsSession == windowsSession_;
    }
public:
    Request(const Request&) = delete;
    Request& operator=(const Request&) = delete;
    bool processCurrent() const noexcept {
        try {
            if (cancelled_.load(std::memory_order_acquire) || GetCurrentProcessId() != creatorPid_) return false;
            const auto now = observer_->identitySnapshot();
            return sameProcessPublication(now) && detail::alive(gameHandle_.value) &&
                detail::alive(steamHandle_.value) && !cancelled_.load(std::memory_order_acquire);
        } catch (...) { return false; }
    }
    bool current() const noexcept { return processCurrent(); }
    // Registry/process correlation ONLY. A false result never contradicts an
    // independently verified current-document UI/CM account. Not write authority.
    bool registryAccountCurrent() const noexcept {
        try {
            return expected_->currentRegistryAccountKnown() && processCurrent() &&
                observer_->sameIdentityEpoch(expected_) && !cancelled_.load(std::memory_order_acquire);
        } catch (...) { return false; }
    }
    std::shared_ptr<RequestCancellation> cancellation() const noexcept {
        return requestCancellationSignal(cancellationSignal_,cancelled_);
    }
    void cancel() noexcept {
        cancelled_.store(true,std::memory_order_release);
        if(const auto held=cancellationSignal_.load(std::memory_order_acquire))held->cancel();
    }
};

class ProcessLease final {
    friend class ProcessParentEvidenceOwner;
    friend class ProcessOwner;
    friend class ProcessGroupLease;
    const std::shared_ptr<Request> request_;
    explicit ProcessLease(std::shared_ptr<Request> request) : request_(std::move(request)) {}
public:
    ProcessLease(const ProcessLease&) = delete;
    ProcessLease& operator=(const ProcessLease&) = delete;
    // Destruction releases only this reference; a live host lease retains both
    // original processes. Explicit cancel revokes, without racing CloseHandle.
    bool current() const noexcept { return request_->current(); }
    bool processCurrent() const noexcept { return request_->processCurrent(); }
    bool registryAccountCurrent() const noexcept { return request_->registryAccountCurrent(); }
    void cancel() noexcept { request_->cancel(); }
    std::shared_ptr<RequestCancellation> requestCancellation() const noexcept { return request_->cancellation(); }
    const ProcessWitness& gameWitness() const noexcept { return request_->game_; }
    const ProcessWitness& steamWitness() const noexcept { return request_->steam_; }
    // Preserve the caller's selected path spelling for the existing exact-path
    // tracker policy; gameWitness() separately reports the actual queried image.
    const ProcessWitness& selectedGameWitness() const noexcept { return request_->selectedGame_; }
    DWORD windowsSession() const noexcept { return request_->windowsSession_; }
    const std::wstring& steamRoot() const noexcept { return request_->expected_->session.root; }
    const std::shared_ptr<const IdentitySnapshot>& identitySnapshot() const noexcept { return request_->expected_; }
    std::uint64_t processEpoch() const noexcept { return request_->expected_->processEpoch; }
    // Cache invalidation fact only. No tracker cache or UI/CM account is created.
    std::uint64_t accountEpoch() const noexcept { return request_->expected_->accountEpoch; }
    HostInspection inspectHost(DWORD hostPid, const TrackerIdentity& tracker,
        const std::optional<ReportedRegistration>& reported = std::nullopt) const;
};

class OverlayHostLease final {
    friend class ProcessLease;
    const std::shared_ptr<Request> request_;
    const ProcessWitness hostWitness_;
    detail::Handle hostHandle_;
    OverlayHostLease(std::shared_ptr<Request> request, ProcessWitness host, detail::Handle handle)
        : request_(std::move(request)), hostWitness_(std::move(host)), hostHandle_(std::move(handle)) {}
public:
    OverlayHostLease(const OverlayHostLease&) = delete;
    OverlayHostLease& operator=(const OverlayHostLease&) = delete;
    bool current() const noexcept {
        return request_->current() && detail::alive(hostHandle_.value);
    }
    void cancel() noexcept { request_->cancel(); }
    const ProcessWitness& hostWitness() const noexcept { return hostWitness_; }
    const ProcessWitness& gameWitness() const noexcept { return request_->game_; }
    const ProcessWitness& steamWitness() const noexcept { return request_->steam_; }
};

enum class HostRejection {
    None, RequestNotCurrent, InvalidHostPid, HostUnavailable, HostIdentityMismatch,
    HostBirthMismatch, HostPathMismatch, HostParentMismatch, HostSessionMismatch,
    CommandLineUnavailable, ArgumentsInvalid, RegistrationUnknown
};
struct HostInspection {
    ProcessWitness hostWitness;
    DWORD parentPid = 0, windowsSession = 0;
    bool windowsSessionKnown = false;
    std::optional<std::wstring> rawCommandLine;
    std::optional<Arguments> parsedArguments;
    TrackerIdentity tracker;
    std::optional<ReportedRegistration> reportedRegistration;
    // Exact describes command-line ID vs tracker, not UI account/AppID policy.
    RegistrationFact fact = RegistrationFact::Unknown;
    HostRejection rejection = HostRejection::RequestNotCurrent;
    std::shared_ptr<OverlayHostLease> hostLease;
};

inline HostInspection ProcessLease::inspectHost(DWORD hostPid, const TrackerIdentity& tracker,
    const std::optional<ReportedRegistration>& reported) const {
    HostInspection out;
    out.tracker = tracker;
    out.reportedRegistration = reported;
    const auto reject = [&](HostRejection reason) {
        out.rejection = reason;
        out.fact = RegistrationFact::Unknown;
        return out;
    };
    if (!current()) return reject(HostRejection::RequestNotCurrent);
    const auto& game = request_->game_;
    const auto& steam = request_->steam_;
    const auto& root = request_->expected_->session.root;
    if (!hostPid || hostPid == game.pid || hostPid == steam.pid) return reject(HostRejection::InvalidHostPid);
    if (!exactGameId(tracker.appid, tracker.fullGameId) ||
        (reported && (!reported->appid || !reported->gameId))) return reject(HostRejection::RegistrationUnknown);
    // The only open in this method. Game and Steam are the Request's existing
    // handles and one-time witnesses; neither is pinned or re-queried here.
    detail::Handle host(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, hostPid));
    if (!detail::alive(host.value)) return reject(HostRejection::HostUnavailable);
    out.hostWitness = { GetProcessId(host.value), detail::birth(host.value), detail::image(host.value) };
    if (out.hostWitness.pid != hostPid || !detail::complete(out.hostWitness)) return reject(HostRejection::HostIdentityMismatch);
    if (out.hostWitness.created < game.created || out.hostWitness.created < steam.created) return reject(HostRejection::HostBirthMismatch);
    if (!samePath(out.hostWitness.image, root + L"\\gameoverlayui64.exe") &&
        !samePath(out.hostWitness.image, root + L"\\gameoverlayui.exe")) return reject(HostRejection::HostPathMismatch);
    out.parentPid = detail::parent(host.value);
    if (out.parentPid != steam.pid) return reject(HostRejection::HostParentMismatch);
    if (!ProcessIdToSessionId(hostPid, &out.windowsSession)) return reject(HostRejection::HostSessionMismatch);
    out.windowsSessionKnown = true;
    if (out.windowsSession != request_->windowsSession_) return reject(HostRejection::HostSessionMismatch);
    out.rawCommandLine = detail::commandLine(host.value);
    if (!out.rawCommandLine) return reject(HostRejection::CommandLineUnavailable);
    out.parsedArguments = arguments(*out.rawCommandLine);
    if (!out.parsedArguments) return reject(HostRejection::ArgumentsInvalid);
    out.fact = registrationFact(*out.parsedArguments, game.pid, steam.pid, tracker, reported);
    if (out.fact == RegistrationFact::Unknown) return reject(HostRejection::RegistrationUnknown);
    if (!current() || !detail::alive(host.value)) return reject(HostRejection::RequestNotCurrent);
    out.hostLease = std::shared_ptr<OverlayHostLease>(new OverlayHostLease(request_, out.hostWitness, std::move(host)));
    if (!out.hostLease->current()) {
        out.hostLease.reset();
        return reject(HostRejection::RequestNotCurrent);
    }
    out.rejection = HostRejection::None;
    return out;
}

class ProcessOwner final {
    friend ProcessOwner& processOwner() noexcept;
    friend class TrackerOwner;
    ProcessOwner() = default;
    // Only the existing source owner can combine its actual fresh observation
    // with held process facts. Caller-constructed source DTOs cannot mint this.
    ProcessGroupCapture acquireGroup(const std::shared_ptr<ProcessLease>&,
        const ProcessGroupSeed&) const;
public:
    ProcessOwner(const ProcessOwner&) = delete;
    ProcessOwner& operator=(const ProcessOwner&) = delete;
    std::shared_ptr<ProcessLease> acquire(Observer& observer,
        std::shared_ptr<const IdentitySnapshot> expected, const ProcessWitness& game) const {
        // Correlate the expected epoch with the actual common publication, then
        // keep that publication. Even a reconstructed expected token cannot
        // inject status/window/account facts. Same-epoch revisions stay usable.
        const auto published = observer.identitySnapshot();
        if (!expected || !published || !observer.sameIdentityEpoch(expected) ||
            expected->processEpoch != published->processEpoch || expected->accountEpoch != published->accountEpoch ||
            expected->session != published->session || expected->presence != published->presence) return {};
        expected = published;
        if (expected->presence != 1 || !expected->session.pid || !expected->session.created ||
            expected->session.root.empty() ||
            expected->session.root.find(L'\0') != std::wstring::npos ||
            expected->rootReadError != ERROR_SUCCESS || !expected->processEpoch ||
            !expected->windowsSessionKnown || expected->windowsSessionError != ERROR_SUCCESS ||
            !detail::complete(game) || game.pid == expected->session.pid) return {};
        const ProcessWitness steam{expected->session.pid, expected->session.created, expected->session.root + L"\\steam.exe"};
        auto gamePin = detail::pin(game);
        if (!gamePin) return {};
        auto steamPin = detail::pin(steam);
        if (!steamPin) return {};
        const DWORD ownPid = GetCurrentProcessId();
        DWORD ownSession = 0;
        if (!ProcessIdToSessionId(ownPid, &ownSession) || ownSession != gamePin->windowsSession ||
            ownSession != steamPin->windowsSession || ownSession != expected->windowsSession ||
            !samePath(steamPin->witness.image, expected->session.root + L"\\steam.exe") ||
            !observer.sameIdentityEpoch(expected)) return {};
        auto request = std::shared_ptr<Request>(new Request(observer, std::move(expected),
            std::move(*gamePin), std::move(*steamPin), game, ownPid, ownSession));
        if (!request->current()) return {};
        return std::shared_ptr<ProcessLease>(new ProcessLease(std::move(request)));
    }
};
inline ProcessOwner& processOwner() noexcept {
    // One owner per linked process. It retains no requests/handles, registry
    // account permissions or cross-request cache; each acquire owns fresh leases.
    static ProcessOwner value;
    return value;
}
inline HostInspection inspectHost(const std::shared_ptr<ProcessLease>& lease, DWORD hostPid,
    const TrackerIdentity& tracker, const std::optional<ReportedRegistration>& reported = std::nullopt) {
    if (lease) return lease->inspectHost(hostPid, tracker, reported);
    HostInspection out;
    out.tracker = tracker;
    out.reportedRegistration = reported;
    return out;
}
}
