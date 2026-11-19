#pragma once
// sourcePrepareOnly; SOURCE_READY=false. No compile/test/integration receipt.
// Compose the existing publishers only: no Observer, worker, timer or power owner.
// NativeIntake/CANCEL-COMPLETION-INTAKE.json qualifies the Native dependency:
// sleep_power_revocation.h, 5889B,
// 0473ad43bdbf59d458f04e6901744adcf7b8d4f0084b714bced8c153b92ec0df.
// Its cancel() completes SetEvent even when Native revoke is already in flight.
// The predecessor's early-exchange return and its failure evidence remain history.
#include "steam_source_lease_contract.h"
#include "steam_native_power_access.h"
#include "steam_account_facts.h"
#include "steam_ui_mode_observer.h"
#include "sleep_power_state.h"
#include "sleep_power_revocation.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ymcc::steamsource {
// Bind these three actual same-process main entries, respectively:
// captureSharedPowerLifecycle, sharedPowerLifecycleCurrent,
// observeSharedPowerRevocation. Missing bindings make the factory Unknown.
// NativePowerAccess is the same three callbacks, shared with Native qualification.
struct Diagnostics {
    std::uint64_t providerCreated = 0, providerClosed = 0, providerCreateFailed = 0, closeFailed = 0;
    DWORD lastError = ERROR_SUCCESS;
};
namespace detail {
struct Status {
    std::atomic<std::uint64_t> providerCreated{0}, providerClosed{0}, providerCreateFailed{0}, closeFailed{0};
    std::atomic<DWORD> lastError{ERROR_SUCCESS};
};
struct ProviderProcess {
    HANDLE handle = nullptr;
    const std::shared_ptr<Status> status;
    explicit ProviderProcess(std::shared_ptr<Status> diagnostics) : status(std::move(diagnostics)) {
        // One real self-process duplication per owner; synchronization rights only.
        if (DuplicateHandle(GetCurrentProcess(),GetCurrentProcess(),GetCurrentProcess(),
                &handle,SYNCHRONIZE,FALSE,0)) ++status->providerCreated;
        else { ++status->providerCreateFailed; status->lastError = GetLastError(); }
    }
    ProviderProcess(const ProviderProcess&) = delete;
    ~ProviderProcess() {
        if (!handle) return;
        if (CloseHandle(handle)) ++status->providerClosed;
        else { ++status->closeFailed; status->lastError = GetLastError(); }
    }
    bool current() const noexcept { return handle && WaitForSingleObject(handle,0) == WAIT_TIMEOUT; }
};
inline bool documentUi(const steamaccount::Facts& facts) noexcept {
    const auto& ui = facts.ui;
    if (!ui.currentAccountKnown() || ui.deckyAccountWatchAvailable != steamaccount::Truth::True ||
        !ui.modeEpochKnown || ui.modeEpoch.empty() || ui.modeEpoch.size() > 96 ||
        ui.modeEpoch.find('\0') != std::string::npos || !ui.documentKnown ||
        ui.document.empty() || ui.document.size() > 96 || ui.document.find('\0') != std::string::npos) return false;
    const auto parsed = steamui::parseModeEpoch(ui.modeEpoch);
    return parsed.valid && parsed.document == ui.document && parsed.entry == ui.documentEntry;
}
inline std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const auto count = WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    if (!count) throw std::runtime_error("source-root-encoding");
    std::string result(static_cast<std::size_t>(count),'\0');
    if (WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),count,nullptr,nullptr) != count)
        throw std::runtime_error("source-root-encoding");
    return result;
}
// Publication labels only. Neither equality nor ordering here admits power,
// cancels a registration or participates in an existing lease's current().
struct SourceKey {
    steamsession::Session session;
    std::uint64_t identityRevision = 0, processEpoch = 0, accountEpoch = 0, modeRevision = 0;
    sleeppower::SharedPowerV1 nativeStamp;
    steaminfo::Selection selection = steaminfo::Selection::Unknown;
    DWORD selectedAccount = 0;
    bool operator==(const SourceKey& other) const noexcept {
        const auto& a = nativeStamp;
        const auto& b = other.nativeStamp;
        return session == other.session && identityRevision == other.identityRevision &&
            processEpoch == other.processEpoch && accountEpoch == other.accountEpoch && modeRevision == other.modeRevision &&
            a.size == b.size && a.version == b.version && a.generation == b.generation &&
            a.publicationRevision == b.publicationRevision && a.observedNotificationSequence == b.observedNotificationSequence &&
            a.phase == b.phase && a.flags == b.flags && a.suspendSerial == b.suspendSerial &&
            selection == other.selection && selectedAccount == other.selectedAccount;
    }
    bool olderPublicationThan(const SourceKey& published) const noexcept {
        return identityRevision < published.identityRevision || processEpoch < published.processEpoch ||
            accountEpoch < published.accountEpoch || modeRevision < published.modeRevision ||
            nativeStamp.publicationRevision < published.nativeStamp.publicationRevision;
    }
};
struct LeaseState;
struct SourceRootCore {
    std::atomic<bool> closed{false};
    std::mutex mutex;
    std::vector<std::weak_ptr<LeaseState>> leases;
    std::uint64_t sourceRevision = 0;
    std::optional<SourceKey> last;
    const std::shared_ptr<Status> status = std::make_shared<Status>();
};
// Keep this stable state (and thus all borrowed events and the provider handle)
// through final child/Job resource return. Cancellation never resets or closes it.
struct LeaseState final {
    const std::shared_ptr<SourceRootCore> core;
    const std::shared_ptr<steamsession::IdentityDelegation> identity;
    const steamui::ModeSourceLease mode;
    const std::shared_ptr<ProviderProcess> provider;
    const NativePowerAccess access;
    const sleeppower::SharedPowerV1 captured;
    std::mutex mutex;
    // Moved here once. Never reassigned/reset while this state can be borrowed.
    sleeppower::RevocationMirrors::Registration powerRegistration;
    // Wait-any indices 0/1/2 are Identity/Mode/this Native registration.
    // Index 2 means this request was cancelled, not proof of physical sleep.
    // provider->handle is the independent fourth wait target (process lifetime).
    const std::array<void*,3> events;
    bool invalid = false; // Protected by this lease's mutex, never the Core mutex.

    LeaseState(std::shared_ptr<SourceRootCore> owner,std::shared_ptr<steamsession::IdentityDelegation> identityTicket,
            steamui::ModeSourceLease modeTicket,std::shared_ptr<ProviderProcess> process,NativePowerAccess native,
            sleeppower::SharedPowerV1 power,sleeppower::RevocationMirrors::Registration&& registration) noexcept
        : core(std::move(owner)),identity(std::move(identityTicket)),mode(std::move(modeTicket)),provider(std::move(process)),
          access(native),captured(power),powerRegistration(std::move(registration)),
          events{identity->cancellationEvent(),mode.cancellationEvent(),powerRegistration.event()} {}
    LeaseState(const LeaseState&) = delete;
    ~LeaseState() {
        cancel();
        // Registration destruction is the only reset, after the last lease/Job
        // owner releases this state. It precedes provider/Mode/Identity teardown.
    }
    bool invalidateLocked() noexcept {
        // Complete this registration's signal before publishing sticky failure.
        // This also closes an in-flight Native revoke's pre-SetEvent interval.
        powerRegistration.cancel();
        invalid = true;
        return false;
    }
    void cancel() noexcept {
        std::lock_guard lock(mutex);
        invalidateLocked();
    }
    bool ticketsCurrentLocked() const noexcept {
        return !core->closed.load(std::memory_order_acquire) && provider->current() &&
            events[0] && events[1] && events[2] && identity->current() && mode.current() &&
            !powerRegistration.cancelled();
    }
    bool current() noexcept {
        std::lock_guard lock(mutex);
        if (invalid) return false; // A preceding completed cancel holds the signal.
        if (!ticketsCurrentLocked() || !access.current || !access.current(captured) ||
            !ticketsCurrentLocked()) return invalidateLocked();
        // Only Native decides whether its captured power is still current.
        // In particular, forward diagnostic publications do not cancel/warm.
        return true;
    }
};
class Lease final : public steaminfo::SourceLeaseV2 {
    const steaminfo::SourceSnapshot snapshot_;
    const std::shared_ptr<LeaseState> state_;
public:
    Lease(steaminfo::SourceSnapshot snapshot,std::shared_ptr<LeaseState> state)
        : snapshot_(std::move(snapshot)),state_(std::move(state)) {}
    const steaminfo::SourceSnapshot& snapshot() const noexcept override { return snapshot_; }
    const std::array<void*,3>& cancellationEvents() const noexcept override { return state_->events; }
    void* providerProcess() const noexcept override { return state_->provider->handle; }
    bool current() noexcept override { return state_->current(); }
};
}

class SourceOwner final {
    steamsession::Observer& identity_;
    steamui::ModeObserver& mode_;
    const NativePowerAccess access_;
    const std::shared_ptr<detail::SourceRootCore> control_;
    const std::shared_ptr<detail::ProviderProcess> provider_;

    bool captureInputsCurrent(const std::shared_ptr<const steamsession::IdentitySnapshot>& expected,
            const steamui::ModeSourceLease& actualModeSourceLease) noexcept {
        try {
            if (control_->closed.load(std::memory_order_acquire) || !provider_->current() ||
                !expected || !actualModeSourceLease || !actualModeSourceLease.current() ||
                !identity_.sameIdentityEpoch(expected)) return false;
            const auto now = mode_.observedSource(expected->session);
            return now && now.current() && actualModeSourceLease.current() && now.cancellationEvent() &&
                now.cancellationEvent() == actualModeSourceLease.cancellationEvent() &&
                now.snapshot().modeRevision == actualModeSourceLease.snapshot().modeRevision &&
                now.snapshot().session == expected->session && identity_.sameIdentityEpoch(expected) &&
                !control_->closed.load(std::memory_order_acquire) && provider_->current();
        } catch (...) { return false; }
    }
    std::shared_ptr<steaminfo::SourceLeaseV2> acquire(bool candidateRead) noexcept {
        try {
            if (!configured()) return {};
            // Consume only the real publishers' existing cached receipts.
            const auto identity = identity_.identitySnapshot();
            if (!identity || identity->presence != 1 || !identity->session.pid || !identity->session.created ||
                identity->session.root.empty() || identity->rootReadError != ERROR_SUCCESS || !identity->windowsSessionKnown) return {};
            const auto actualModeSourceLease = mode_.observedSource(identity->session);
            if (!actualModeSourceLease || !actualModeSourceLease.cancellationEvent() ||
                !(actualModeSourceLease.snapshot().session == identity->session) ||
                !captureInputsCurrent(identity,actualModeSourceLease)) return {};
            const auto facts = steamaccount::project(*identity,actualModeSourceLease.snapshot().modeFacts);
            const bool uiLocal = !candidateRead;
            const bool documentKnown = detail::documentUi(facts);
            if (uiLocal ? !documentKnown : !identity->registryAccountCandidateKnown()) return {};
            const auto account = uiLocal ? facts.ui.accountID32 : identity->session.account;
            if (!captureInputsCurrent(identity,actualModeSourceLease)) return {};
            // Only this real Observer publishes IdentityDelegation. UiLocal stays
            // UiLocal when ui == registry; fromUi is never used as provenance.
            auto delegation = identity_.delegateIdentity(identity,account,uiLocal);
            if (!delegation || !delegation->cancellationEvent() || !delegation->current() ||
                !captureInputsCurrent(identity,actualModeSourceLease)) return {};

            sleeppower::SharedPowerV1 power;
            if (!access_.capture(power)) return {};
            if (!captureInputsCurrent(identity,actualModeSourceLease) || !delegation->current() ||
                !access_.current(power) || !captureInputsCurrent(identity,actualModeSourceLease) || !delegation->current()) return {};
            auto registration = access_.observe(power);
            if (!registration || !registration.event()) { registration.cancel(); return {}; }
            auto state = std::make_shared<detail::LeaseState>(control_,delegation,actualModeSourceLease,
                provider_,access_,power,std::move(registration));
            // Native current is bracketed by real ticket and cancelled() checks.
            // Repeat the cached receipt/Mode revision+event check after observe.
            if (!state->current() || !captureInputsCurrent(identity,actualModeSourceLease) || !state->current()) {
                state->cancel(); return {};
            }

            steaminfo::SourceSnapshot snapshot;
            auto& wire = snapshot.facts;
            wire.available = true; wire.requested = steaminfo::Session|steaminfo::Presence; wire.observedAt = identity->observedAt;
            wire.presence = identity->presence; wire.processPid = identity->session.pid; wire.processBirth = identity->session.created;
            wire.root = detail::utf8(identity->session.root);
            // Location metadata from the receipt, never a new image/path proof.
            wire.processPath = detail::utf8(identity->session.root+L"\\steam.exe");
            wire.rootError = identity->rootReadError; wire.registryAccount = identity->session.account; wire.accountError = identity->accountReadError;
            wire.uiAccount = facts.ui.accountID32; wire.uiAccountKnown = documentKnown;
            wire.identityRevision = identity->revision; wire.processEpoch = identity->processEpoch; wire.accountEpoch = identity->accountEpoch;
            snapshot.modeRevision = actualModeSourceLease.snapshot().modeRevision; snapshot.powerGeneration = power.generation;
            snapshot.mode = actualModeSourceLease.snapshot().mode; snapshot.modeEpoch = actualModeSourceLease.snapshot().modeEpoch;
            snapshot.selection = uiLocal ? steaminfo::Selection::UiLocal : steaminfo::Selection::RegistryCandidate;
            snapshot.selectedAccount = account;
            // Candidate mode/document fields are observations only; that factory
            // retains RegistryCandidate selection and never gets document scope.
            detail::SourceKey key{identity->session,identity->revision,identity->processEpoch,identity->accountEpoch,
                snapshot.modeRevision,power,snapshot.selection,account};
            bool published = false;
            {
                // No publisher/native callback or lease current under Core mutex.
                std::lock_guard lock(control_->mutex);
                if (!control_->closed.load(std::memory_order_acquire) &&
                    (!control_->last || !key.olderPublicationThan(*control_->last))) {
                    auto& leases = control_->leases;
                    leases.erase(std::remove_if(leases.begin(),leases.end(),
                        [](const auto& weak) { return weak.expired(); }),leases.end());
                    leases.emplace_back(state);
                    if (!control_->last || !(*control_->last == key)) {
                        control_->last = std::move(key);
                        ++control_->sourceRevision;
                    }
                    snapshot.sourceRevision = control_->sourceRevision;
                    published = true;
                }
            }
            // A late capture only cancels its own registration. New metadata
            // never changes an existing lease's tickets, Native stamp or budget.
            if (!published || !captureInputsCurrent(identity,actualModeSourceLease) || !state->current()) {
                state->cancel(); return {};
            }
            auto lease = std::make_shared<detail::Lease>(std::move(snapshot),std::move(state));
            return lease->current() ? lease : std::shared_ptr<steaminfo::SourceLeaseV2>{};
        } catch (...) { return {}; }
    }
public:
    SourceOwner(steamsession::Observer& identity,steamui::ModeObserver& mode,NativePowerAccess access)
        : identity_(identity),mode_(mode),access_(access),control_(std::make_shared<detail::SourceRootCore>()),
          provider_(std::make_shared<detail::ProviderProcess>(control_->status)) {}
    ~SourceOwner() { shutdown(); }
    SourceOwner(const SourceOwner&) = delete;
    bool configured() const noexcept {
        return access_.capture && access_.current && access_.observe && provider_->current() &&
            !control_->closed.load(std::memory_order_acquire);
    }
    std::shared_ptr<steaminfo::SourceLeaseV2> acquireSourceV2() noexcept { return acquire(false); }
    // Explicit candidate-only read scope; missing UI never falls back here.
    std::shared_ptr<steaminfo::SourceLeaseV2> acquireRegistryCandidateSourceV2() noexcept { return acquire(true); }
    void shutdown() noexcept {
        control_->closed.store(true,std::memory_order_release);
        std::lock_guard lock(control_->mutex);
        // Only this owner's request registrations. Do not close/revoke Native's
        // registry or release borrowed handles. current() never takes this lock.
        for (const auto& weak : control_->leases) if (const auto state = weak.lock()) state->cancel();
    }
    Diagnostics diagnostics() const noexcept {
        const auto& d = *control_->status;
        return {d.providerCreated.load(),d.providerClosed.load(),d.providerCreateFailed.load(),d.closeFailed.load(),d.lastError.load()};
    }
};
namespace detail {
struct Binding { std::atomic<std::shared_ptr<SourceOwner>> owner; };
inline Binding& binding() { static Binding value; return value; }
inline std::shared_ptr<SourceOwner> installed() noexcept {
    return binding().owner.load(std::memory_order_acquire);
}
}
// Main retains the actual Observer/ModeObserver until owner shutdown and any
// in-flight acquire return. Consumers retain each lease through final Job return.
// Main binding example: NativePowerAccess{&captureSharedPowerLifecycle,
//     &sharedPowerLifecycleCurrent,&observeSharedPowerRevocation}.
inline bool install(const std::shared_ptr<SourceOwner>& owner) noexcept {
    try {
        if (!owner || !owner->configured()) return false;
        auto& slot = detail::binding(); std::shared_ptr<SourceOwner> expected;
        return slot.owner.compare_exchange_strong(expected,owner,std::memory_order_acq_rel) || expected == owner;
    } catch (...) { return false; }
}
inline void shutdownSource() noexcept {
    if (const auto owner = detail::binding().owner.exchange({},std::memory_order_acq_rel)) owner->shutdown();
}
}
namespace ymcc::steaminfo {
inline std::shared_ptr<SourceLeaseV2> acquireSourceV2() noexcept {
    const auto owner = steamsource::detail::installed(); return owner ? owner->acquireSourceV2() : std::shared_ptr<SourceLeaseV2>{};
}
inline std::shared_ptr<SourceLeaseV2> acquireRegistryCandidateSourceV2() noexcept {
    const auto owner = steamsource::detail::installed(); return owner ? owner->acquireRegistryCandidateSourceV2() : std::shared_ptr<SourceLeaseV2>{};
}
// V1 acquireSource() deliberately has no implementation in this producer.
}
