#pragma once
#include "oem_gamepad_mode_contract.h"
#include <optional>

namespace ymcc::oem::gamepad {
// Caller serializes all methods on the existing lifecycle worker. This mechanism
// neither starts workers nor touches the physical-mode writer or virtual target.
class PolicySession {
    std::optional<Record> lease_;
    bool acquired_=false;
    static bool usable(const ReadResult& r) {
        return (r.state==ReadState::Value && r.value.exists) ||
               (r.state==ReadState::MissingValue && !r.value.exists);
    }
    Result settle(const Adapter& adapter, RegistryIo& io, JournalStore& journal,
                  Status success) {
        Result out;out.status=Status::CleanupPending;
        if (!lease_) {out.status=Status::NoLease;return out;}
        Record& r=*lease_;
        if (!adapter.validRecord(r)) {
            out.status=Status::JournalInvalid;out.cleanupPending=true;return out;
        }
        // Reverse-order unwind; compare-before-write preserves differing external
        // changes. Registry has NO atomic value CAS; same-value ABA remains unknown.
        for (auto i=r.entries.rbegin();i!=r.entries.rend();++i) {
            if (!i->mayHaveWritten) continue;
            const auto now=io.read(i->patch.address);
            if (!usable(now)) break;
            if (now.value==i->original) {i->mayHaveWritten=false;continue;}
            if (!(now.value==i->patch.desired)) {
                ++out.externalChangesPreserved;i->mayHaveWritten=false;continue;
            }
            // A second comparison narrows a race, but is not a cross-process lock.
            const auto before=io.read(i->patch.address);
            if (!usable(before) || !(before.value==i->patch.desired)) break;
            ++out.attemptedWrites;
            const bool writeOk=io.write(i->patch.address,i->original);
            const auto after=io.read(i->patch.address);
            if (writeOk && usable(after) && after.value==i->original) {
                ++out.verifiedWrites;i->mayHaveWritten=false;
            } else break; // Keep automatic switching suppressed until preference cleanup resolves.
        }
        bool pending=false;
        for(const auto& e:r.entries) pending=pending||e.mayHaveWritten;
        r.pending=pending;
        if (!journal.save(r)) {
            r.pending=true;out.status=Status::JournalFailure;out.cleanupPending=true;return out;
        }
        out.cleanupPending=pending;
        if (!pending) {lease_.reset();acquired_=false;out.status=success;}
        out.leaseActive=lease_.has_value();
        return out;
    }
public:
    bool ownsLease() const {return lease_.has_value();}
    const Record* record() const {return lease_?&*lease_:nullptr;}
    Result acquire(const Request& request,const Adapter& adapter,RegistryIo& io,
                   JournalStore& journal,AuthorityGate& gate) {
        Result out;
        if (!request.userAuthorized) {out.status=Status::Unauthorized;return out;}
        if (!validOwner(request.owner) || !validIntent(request.intent)) {out.status=Status::InvalidOwner;return out;}
        const auto c=adapter.capability();
        out.requestedRoute=request.intent==Intent::VirtualCapture?c.virtualRoute:c.nativeRoute;
        if (!c.policyCapabilityVerified || c.binding.empty() || !out.requestedRoute.supported() || out.requestedRoute.vendor!=c.vendor) return out;
        if (lease_) {
            // Idempotence is a receipt of the earlier acquisition, not a fresh
            // verification of current settings and NOT a hardware-mode claim.
            out.status=acquired_ && lease_->pending && lease_->owner==request.owner &&
                lease_->intent==request.intent && lease_->binding==c.binding
                ? Status::AlreadyOwned : Status::Busy;
            out.leaseActive=true;out.cleanupPending=out.status==Status::Busy;return out;
        }
        const auto stored=journal.load();
        if(stored.state==JournalState::Invalid) {out.status=Status::JournalInvalid;return out;}
        if(stored.state==JournalState::Record) {
            if(!adapter.validRecord(stored.record)) {out.status=Status::JournalInvalid;return out;}
            if(stored.record.pending) {out.status=Status::CleanupPending;out.cleanupPending=true;return out;}
        }
        Record r;r.vendor=c.vendor;r.owner=request.owner;r.intent=request.intent;r.binding=c.binding;
        for(const auto& patch:adapter.patches(request.intent)) {
            const auto original=io.read(patch.address);
            if(!usable(original)) {out.status=Status::ReadUnknown;return out;}
            r.entries.push_back({patch,original.value,false});
        }
        if(!adapter.validRecord(r)) {out.status=Status::JournalInvalid;return out;}
        if(!gate.current(r.owner)) {out.status=Status::Stale;return out;}
        if(!journal.save(r)) {out.status=Status::JournalFailure;return out;}
        lease_=r;out.leaseActive=true;
        for(auto& e:lease_->entries) {
            const auto now=io.read(e.patch.address);
            if(!usable(now)) {out.status=Status::ReadUnknown;out.cleanupPending=true;return out;}
            if(!(now.value==e.original)) {out.status=Status::Conflict;out.cleanupPending=true;return out;}
            if(now.value==e.patch.desired) continue; // Never own/restore a preexisting matching setting.
            e.mayHaveWritten=true;
            if(!journal.save(*lease_)) {out.status=Status::JournalFailure;out.cleanupPending=true;return out;}
            if(!gate.current(lease_->owner)) {out.status=Status::Stale;out.cleanupPending=true;return out;}
            const auto before=io.read(e.patch.address);
            if(!usable(before) || !(before.value==e.original)) {
                out.status=Status::Conflict;out.cleanupPending=true;return out;
            }
            ++out.attemptedWrites;
            const bool ok=io.write(e.patch.address,e.patch.desired);
            const auto after=io.read(e.patch.address);
            if(!ok || !usable(after) || !(after.value==e.patch.desired)) {
                out.status=Status::WriteFailure;out.cleanupPending=true;return out;
            }
            ++out.verifiedWrites;
        }
        if(!gate.current(lease_->owner)) {out.status=Status::Stale;out.cleanupPending=true;return out;}
        // Full policy readback: a previously verified first key may have changed
        // while the second key was being written. No blind reassertion follows.
        for(const auto& e:lease_->entries) {
            const auto now=io.read(e.patch.address);
            if(!usable(now) || !(now.value==e.patch.desired)) {
                out.status=Status::Conflict;out.cleanupPending=true;return out;
            }
        }
        acquired_=true;out.status=Status::Acquired;out.policyVerified=true;out.leaseActive=true;
        return out;
    }
    // C persona / power handoff changes evidence tokens, not settings, original
    // snapshots or the correction budget. Failed acquisitions cannot be handed on.
    Result handoff(const Owner& oldOwner,const Owner& next,const Adapter& adapter,
                   JournalStore& journal,AuthorityGate& gate) {
        Result out;out.status=Status::Stale;
        if(!lease_ || !acquired_ || !(lease_->owner==oldOwner) || !validOwner(next) ||
           oldOwner.processNonce!=next.processNonce || oldOwner.session!=next.session ||
           oldOwner.intentRevision!=next.intentRevision || next.hostEpoch<oldOwner.hostEpoch ||
           next.powerGeneration<oldOwner.powerGeneration || !gate.current(next)) return out;
        Record moved=*lease_;moved.owner=next;
        if(!adapter.validRecord(moved)) {out.status=Status::JournalInvalid;return out;}
        if(!journal.save(moved)) {out.status=Status::JournalFailure;out.cleanupPending=true;return out;}
        lease_=std::move(moved);out.status=Status::AlreadyOwned;out.leaseActive=true;return out;
    }
    Result release(const Owner& owner,const Adapter& adapter,RegistryIo& io,JournalStore& journal) {
        if(lease_ && !(lease_->owner==owner)) {Result r;r.status=Status::Stale;return r;}
        return settle(adapter,io,journal,Status::Released);
    }
    // Must run BEFORE a new acquisition, under the application's exclusive
    // lifecycle/single-instance lock. No hardware or mode1 restoration is done.
    Result recover(const Adapter& adapter,RegistryIo& io,JournalStore& journal) {
        if(lease_) {Result r;r.status=Status::Busy;return r;}
        const auto stored=journal.load();
        if(stored.state==JournalState::Absent) {Result r;r.status=Status::NoLease;return r;}
        if(stored.state==JournalState::Invalid || !adapter.validRecord(stored.record)) {
            Result r;r.status=Status::JournalInvalid;return r;
        }
        if(!stored.record.pending) {Result r;r.status=Status::NoLease;return r;}
        lease_=stored.record;return settle(adapter,io,journal,Status::Recovered);
    }
};

// Optional arbitration interface for later integration with the EXISTING serial
// writer, not an additional repair loop. Budget never resets on ACK/success,
// focus, mode oscillation, sleep or persona replacement.
enum class Witness { Unknown, FirmwareReadback, PhysicalInputIdentity };
struct Deviation {
    Owner owner;
    Witness witness= Witness::Unknown;
    PhysicalRoute observed=unsupportedRoute;
    bool physicalIdentityVerified=false, routeOwned=false, hardwareReady=false, exitFence=false;
    uint64_t observedAt=0, now=0;
    HardwareState state=HardwareState::Unknown;
    TransitionOrigin origin=TransitionOrigin::Unknown;
};
enum class Decision { None, Unknown, Stale, NotOwned, Unsupported, Correct, ProtectedPhysicalChoice, Exhausted, QueueExistingWriter };
struct ModeDecision { Decision decision=Decision::None; PhysicalRoute route=unsupportedRoute; };
class ModeArbiter {
    std::optional<Owner> owner_;
    PhysicalRoute target_=unsupportedRoute;
    bool spent_=false, correctionAllowed_=false;
    std::string processNonce_;
    uint64_t highestSession_=0;
public:
    bool begin(const Request& r,const Capability& c) {
        if(owner_ || !r.userAuthorized || !validOwner(r.owner) || !validIntent(r.intent) || !c.captureCapabilityVerified) return false;
        const auto route=r.intent==Intent::VirtualCapture?c.virtualRoute:c.nativeRoute;
        if(!route.supported() || route.vendor!=c.vendor) return false;
        if(!processNonce_.empty() && (processNonce_!=r.owner.processNonce || r.owner.session<=highestSession_)) return false;
        processNonce_=r.owner.processNonce;highestSession_=r.owner.session;
        owner_=r.owner;target_=route;spent_=false;correctionAllowed_=c.firmwareModeCapabilityVerified;return true;
    }
    // Generation changes invalidate evidence; they do NOT create another budget.
    bool handoff(const Owner& oldOwner,const Owner& next) {
        if(!owner_ || !(*owner_==oldOwner) || !validOwner(next) ||
           oldOwner.processNonce!=next.processNonce || oldOwner.session!=next.session ||
           oldOwner.intentRevision!=next.intentRevision || next.hostEpoch<oldOwner.hostEpoch ||
           next.powerGeneration<oldOwner.powerGeneration) return false;
        owner_=next;return true;
    }
    ModeDecision observe(const Deviation& d) {
        if(!owner_ || !(d.owner==*owner_)) return {Decision::Stale};
        if(d.exitFence || !d.hardwareReady || !d.routeOwned) return {Decision::NotOwned};
        if(!d.physicalIdentityVerified || d.witness==Witness::Unknown || !d.observedAt ||
           d.now<d.observedAt || d.now-d.observedAt>2000) return {Decision::Unknown};
        if(d.state==HardwareState::DetachedDualInput || d.state==HardwareState::FpsHardware ||
           d.state==HardwareState::ControllerOff) return {Decision::ProtectedPhysicalChoice};
        if(!d.observed.supported() || d.observed.vendor!=target_.vendor ||
           d.state==HardwareState::Unknown) return {Decision::Unknown};
        if(d.observed==target_) return {Decision::Correct};
        if(d.origin==TransitionOrigin::ExplicitUserIntent || d.origin==TransitionOrigin::PhysicalTopology ||
           d.origin==TransitionOrigin::PhysicalSwitch || d.origin==TransitionOrigin::OfficialShortcut)
            return {Decision::ProtectedPhysicalChoice};
        if(!correctionAllowed_) return {Decision::Unsupported};
        if(spent_) return {Decision::Exhausted};
        spent_=true;return {Decision::QueueExistingWriter,target_};
    }
    bool end(const Owner& owner) {
        if(!owner_ || !(owner==*owner_)) return false;
        owner_.reset();return true;
    }
};
} // namespace ymcc::oem::gamepad










