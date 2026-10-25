#pragma once
#include "oem_input_contract.h"
#include "oem_gamepad_mode_session.h"
namespace ymcc::oem::gamepad {
enum class OwnershipResult { Acquired, AlreadyOwned, Busy, InvalidContract, Conflict, Stale, Released };
// ONE session per physical controller, not one per OEM frontend. The immutable
// contract prevents competing foreground/SDK routes from silently replacing the
// capture source. All methods are serialized by the existing lifecycle worker.
class InputSession {
    std::optional<InputContract> contract_;
    ModeArbiter arbiter_;
    static bool sameContract(const InputContract& a,const InputContract& b) {
        return a.owner==b.owner && a.identity.frontend==b.identity.frontend &&
            a.capability.binding==b.capability.binding && a.intent==b.intent &&
            a.route==b.route && a.physicalSource==b.physicalSource &&
            a.sourceGeneration==b.sourceGeneration && a.virtualTarget==b.virtualTarget &&
            a.virtualTargetEpoch==b.virtualTargetEpoch && a.persona==b.persona &&
            a.isolationRequired==b.isolationRequired && a.hostRunId==b.hostRunId && a.requiredFields==b.requiredFields;
    }
public:
    const InputContract* contract() const {return contract_?&*contract_:nullptr;}
    OwnershipResult acquire(const InputContract& c) {
        if(admit(c)!=Admission::Accepted) return OwnershipResult::InvalidContract;
        if(contract_) {
            if(!(contract_->owner==c.owner)) return OwnershipResult::Busy;
            return sameContract(*contract_,c)?OwnershipResult::AlreadyOwned:OwnershipResult::Conflict;
        }
        if(!arbiter_.begin({c.owner,c.intent,c.userAuthorized},c.capability)) return OwnershipResult::Stale;
        contract_=c;return OwnershipResult::Acquired;
    }
    OwnershipResult handoff(const Owner& oldOwner,const InputContract& c,
                            bool physicalOwnershipRetained,bool oldVirtualReleaseProven) {
        if(!contract_ || !(oldOwner==contract_->owner)) return OwnershipResult::Stale;
        if(admit(c)!=Admission::Accepted || !physicalOwnershipRetained ||
           c.identity.frontend!=contract_->identity.frontend || c.capability.binding!=contract_->capability.binding ||
           c.intent!=contract_->intent || !(c.route==contract_->route) ||
           c.physicalSource!=contract_->physicalSource || c.sourceGeneration<contract_->sourceGeneration ||
           c.isolationRequired!=contract_->isolationRequired || c.requiredFields!=contract_->requiredFields) return OwnershipResult::Conflict;
        const bool targetChanged=c.hostRunId!=contract_->hostRunId || c.virtualTarget!=contract_->virtualTarget || c.persona!=contract_->persona ||
            c.virtualTargetEpoch!=contract_->virtualTargetEpoch;
        if(targetChanged && (!oldVirtualReleaseProven || c.owner.hostEpoch<=oldOwner.hostEpoch ||
           c.virtualTargetEpoch<=contract_->virtualTargetEpoch)) return OwnershipResult::Conflict;
        if(!arbiter_.handoff(oldOwner,c.owner)) return OwnershipResult::Stale;
        contract_=c;return OwnershipResult::AlreadyOwned;
    }
    ChainResult inspect(const ChainProofs& p,uint64_t now) const {
        return contract_?evaluate(*contract_,p,now):ChainResult{};
    }
    ModeDecision deviation(const ModeProof& proof,uint64_t now,bool routeOwned,bool hardwareReady,bool exitFence) {
        if(!contract_ || !matches(*contract_,proof.token)) return {Decision::Stale};
        Witness witness=Witness::Unknown;
        if(proof.witness==ModeWitness::FirmwareReadback) witness=Witness::FirmwareReadback;
        if(proof.witness==ModeWitness::PhysicalSourceIdentity) witness=Witness::PhysicalInputIdentity;
        return arbiter_.observe({contract_->owner,witness,proof.observedRoute,
            proof.physicalIdentityVerified,routeOwned,hardwareReady,exitFence,proof.tick,now,proof.state,proof.origin});
    }
    OwnershipResult release(const Owner& owner) {
        if(!contract_ || !(contract_->owner==owner)) return OwnershipResult::Stale;
        if(!arbiter_.end(owner)) return OwnershipResult::Stale;
        contract_.reset();return OwnershipResult::Released;
    }
};
} // namespace ymcc::oem::gamepad


