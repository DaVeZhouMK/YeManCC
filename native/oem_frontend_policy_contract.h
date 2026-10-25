#pragma once
#include "oem_input_contract.h"
namespace ymcc::oem::gamepad {
enum class ManualControlSelection { Unknown, Auto, Desktop, Gamepad };
enum class AutoSwitchPolicy { Unknown, Disabled, Enabled };
enum class ObservationSource { Unknown, OfficialConfigRead, OfficialApiRead,
    ParsedDeviceResponse, ProviderCache, ProcessInventory, UiLabel };
enum class ProfileMatch { Unknown, NotRegistered, RegisteredNoCustomProfile,
    ExactRegisteredProfile, ExePathMismatch };
struct FrontendPolicyObservation {
    Owner owner;
    Identification identity;
    // These are four INDEPENDENT layers. Auto need not be a firmware mode.
    ManualControlSelection manualSelection=ManualControlSelection::Unknown;
    AutoSwitchPolicy autoPolicy=AutoSwitchPolicy::Unknown;
    PolicyValue effectivePolicy=PolicyValue::Unknown;
    ObservationSource source=ObservationSource::Unknown;
    bool installedVersionBound=false, exactOfficialStateRead=false;
    uint64_t tick=0;
    std::string foregroundExePath, registeredGamePath, effectiveProfileId;
    ProfileMatch profileMatch=ProfileMatch::Unknown;
    bool foregroundVerified=false, registrationVerified=false, profileMatchVerified=false;
    std::string officialStateReceipt; // Needed for an actual live-query claim.
};
enum class PolicyRecognition { Unknown, Stale, InventoryOnly, CacheOnly, OfficialPolicy,
    AutoContextIncomplete, AutoPathMismatch, OfficialAutoContext };
inline PolicyRecognition recognize(const InputContract& c,const FrontendPolicyObservation& p,uint64_t now) {
    if(!(c.owner==p.owner) || c.identity.frontend!=p.identity.frontend ||
       c.capability.binding!=p.identity.capabilityBinding || !fresh(p.tick,now,2000)) return PolicyRecognition::Stale;
    if(p.source==ObservationSource::ProcessInventory || p.source==ObservationSource::UiLabel) return PolicyRecognition::InventoryOnly;
    if(p.source==ObservationSource::ProviderCache) return PolicyRecognition::CacheOnly;
    if(!p.identity.machineVerified || !p.identity.installedComponentsVerified ||
       p.identity.machineVendor!=c.identity.machineVendor || !p.installedVersionBound || !p.exactOfficialStateRead ||
       (p.source!=ObservationSource::OfficialConfigRead && p.source!=ObservationSource::OfficialApiRead) ||
       !boundedIdentity(p.officialStateReceipt)) return PolicyRecognition::Unknown;
    if(p.manualSelection==ManualControlSelection::Unknown || p.autoPolicy==AutoSwitchPolicy::Unknown)
        return PolicyRecognition::Unknown;
    if(p.manualSelection!=ManualControlSelection::Auto) return PolicyRecognition::OfficialPolicy;
    // For ASUS Auto, exe/library/profile correlation is required. This records
    // observed official context; it does NOT implement the Auto algorithm or
    // manufacture a live mode reader from the official FAQ.
    if(c.identity.frontend==Frontend::AsusArmouryCrate || c.identity.frontend==Frontend::AsusArmouryCrateSE) {
        if(!p.foregroundVerified || !p.registrationVerified || !p.profileMatchVerified ||
           !boundedIdentity(p.foregroundExePath) || p.profileMatch==ProfileMatch::Unknown)
            return PolicyRecognition::AutoContextIncomplete;
        if(p.profileMatch==ProfileMatch::ExePathMismatch) return PolicyRecognition::AutoPathMismatch;
        if(p.profileMatch!=ProfileMatch::NotRegistered && !boundedIdentity(p.registeredGamePath))
            return PolicyRecognition::AutoContextIncomplete;
        if(p.profileMatch==ProfileMatch::ExactRegisteredProfile && !boundedIdentity(p.effectiveProfileId))
            return PolicyRecognition::AutoContextIncomplete;
        return PolicyRecognition::OfficialAutoContext;
    }
    return PolicyRecognition::OfficialPolicy;
}
// Mutation scopes are declarations checked against the ACTUAL encoded command
// sequence, not the UI feature label. No command bytes or OEM control API here.
inline constexpr uint32_t scopeMode=1u<<0,scopeRearButtons=1u<<1,scopeMainButtons=1u<<2,
    scopeSticksDpad=1u<<3,scopeTriggers=1u<<4,scopeDefaultsCommit=1u<<5;
inline constexpr uint32_t allMappingScopes=scopeMode|scopeRearButtons|scopeMainButtons|
    scopeSticksDpad|scopeTriggers|scopeDefaultsCommit;
struct MappingWriteFacts {
    Owner owner;
    std::string capabilityBinding, protocolProfileId, firmwareVersion, endpointIdentity;
    bool userAuthorized=false, exactProtocolVerified=false, endpointVerified=false;
    bool currentOwner=false, singleYmccWriterOwned=false, hardwareReady=false, exitFence=false;
    uint32_t requestedScope=0, approvedScope=0;
    bool originalExactStateVerified=false, originalRestorable=false, durableWriteAheadCommitted=false;
    ObservationSource originalSource=ObservationSource::Unknown;
    bool externalOverrideObserved=false;
};
enum class MappingAdmission { Allowed, Unauthorized, IdentityUnknown, ScopeExceeded,
    Stale, NotOwned, OriginalUnknown, JournalMissing, ExternalOverride };
inline MappingAdmission admitMapping(const InputContract& c,const MappingWriteFacts& f) {
    if(!f.userAuthorized) return MappingAdmission::Unauthorized;
    if(admit(c)!=Admission::Accepted || !c.capability.firmwareModeCapabilityVerified ||
       f.capabilityBinding!=c.capability.binding ||
       !f.exactProtocolVerified || !f.endpointVerified || !boundedIdentity(f.protocolProfileId) ||
       !boundedIdentity(f.firmwareVersion) || !boundedIdentity(f.endpointIdentity)) return MappingAdmission::IdentityUnknown;
    if(!f.requestedScope || (f.requestedScope&~allMappingScopes) || (f.approvedScope&~allMappingScopes) ||
       (f.requestedScope&~f.approvedScope)) return MappingAdmission::ScopeExceeded;
    if(!(f.owner==c.owner) || !f.currentOwner || !f.hardwareReady || f.exitFence) return MappingAdmission::Stale;
    if(!f.singleYmccWriterOwned) return MappingAdmission::NotOwned;
    if(f.externalOverrideObserved) return MappingAdmission::ExternalOverride;
    if(!f.originalExactStateVerified || !f.originalRestorable ||
       f.originalSource!=ObservationSource::ParsedDeviceResponse) return MappingAdmission::OriginalUnknown;
    if(!f.durableWriteAheadCommitted) return MappingAdmission::JournalMissing;
    return MappingAdmission::Allowed;
}
} // namespace ymcc::oem::gamepad


