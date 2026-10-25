#pragma once
#include <cstdint>
#include "msi_dinput_acquire_recovery.h"
#include "json.hpp"

namespace ymcc::msi {
// Pump-local observation only. Wrappers return the original operands unchanged,
// never read a generation/gate themselves, and leave argument order unspecified.
struct ReacquireCancelEvidence {
    struct BoolObservation { bool checked=false, value=false; };
    struct GenerationObservation { bool checked=false; std::uint64_t value=0; unsigned readOrdinal=0; };
    bool episodeActive=false;
    unsigned attempts=0, bindCount=0;
    std::uint64_t nextDue=0, episodePower=0, episodeTopology=0, episodeMode=0;
    GenerationObservation power, topology, mode;
    BoolObservation stale, hardwareAllowed, guardExhaustion, legacyLabelExhaustion;
    unsigned generationReadCount=0;

    ReacquireCancelEvidence(const AcquireRecovery& recovery,unsigned bind) noexcept
        : episodeActive(recovery.active), attempts(recovery.attempts), bindCount(bind),
          nextDue(recovery.nextDue), episodePower(recovery.power),
          episodeTopology(recovery.topology), episodeMode(recovery.mode) {}
    std::uint64_t observeGeneration(GenerationObservation& row,std::uint64_t value) noexcept {
        row.checked=true; row.value=value; row.readOrdinal=++generationReadCount; return value;
    }
    std::uint64_t observePower(std::uint64_t value) noexcept { return observeGeneration(power,value); }
    std::uint64_t observeTopology(std::uint64_t value) noexcept { return observeGeneration(topology,value); }
    std::uint64_t observeMode(std::uint64_t value) noexcept { return observeGeneration(mode,value); }
    static bool observeBool(BoolObservation& row,bool value) noexcept {
        row.checked=true; row.value=value; return value;
    }
    bool observeStale(bool value) noexcept { return observeBool(stale,value); }
    bool observeHardware(bool value) noexcept { return observeBool(hardwareAllowed,value); }
    bool observeGuardExhaustion(bool value) noexcept { return observeBool(guardExhaustion,value); }
    bool observeLegacyLabelExhaustion(bool value) noexcept { return observeBool(legacyLabelExhaustion,value); }

    nlohmann::json fields() const {
        using Json=nlohmann::json;
        const auto boolean=[](const BoolObservation& row) -> Json {
            return { {"checked",row.checked}, {"result",row.checked?Json(row.value):Json(nullptr)} };
        };
        const auto generation=[](const GenerationObservation& row) -> Json {
            return { {"checked",row.checked}, {"value",row.checked?Json(row.value):Json(nullptr)},
                {"readOrdinal",row.checked?Json(row.readOrdinal):Json(nullptr)} };
        };
        const bool allReads=power.checked&&topology.checked&&mode.checked;
        const unsigned mismatch=(power.checked&&power.value!=episodePower?1u:0u)|
            (topology.checked&&topology.value!=episodeTopology?2u:0u)|
            (mode.checked&&mode.value!=episodeMode?4u:0u);
        // Derived from the unchanged AcquireRecovery::stale definition, not extra
        // comparisons inserted into that predicate. All arguments are read even
        // when its internal || comparison short-circuits.
        unsigned reached=0;
        if(stale.checked&&episodeActive&&allReads){
            reached=1; if(!(mismatch&1)){reached|=2; if(!(mismatch&2))reached|=4;}
        }
        const char* first=stale.checked&&stale.value?"stale":
            hardwareAllowed.checked&&!hardwareAllowed.value?"hardware-write-not-allowed":
            guardExhaustion.checked&&guardExhaustion.value?"exhaustion":"unknown";
        return {
            {"version",1}, {"scope","actual-cancel-guard-operands-and-legacy-label-recheck"},
            {"generationReadsAreAtomicTogether",false},
            {"generationReadScope","independent-original-reads; argument-evaluation-order-unspecified"},
            {"generationReads",{{"power",generation(power)},{"topology",generation(topology)},{"mode",generation(mode)}}},
            {"mismatchMask",allReads?Json(mismatch):Json(nullptr)},
            {"mismatchMaskBits",{{"power",1},{"topology",2},{"mode",4}}},
            {"staleComparisonReachedMask",stale.checked&&allReads?Json(reached):Json(nullptr)},
            {"staleComparisonReachedMaskBasis","derived-from-unchanged-stale-definition-and-recorded-inputs"},
            {"stale",boolean(stale)}, {"hardwareWriteAllowed",boolean(hardwareAllowed)},
            {"guardExhaustion",boolean(guardExhaustion)},
            {"legacyLabelExhaustion",boolean(legacyLabelExhaustion)},
            {"guardFirstTrue",first}, {"reasonIsLegacyLabel",true},
            {"legacyReasonScope","power-changed-label-also-covers-topology-mode-or-hardware-gate; label-rechecks-exhaustion"},
            {"episodeBeforeReleaseReset",{{"active",episodeActive},{"attempts",attempts},
                {"budget",AcquireRecovery::budget},{"nextDue",nextDue},{"power",episodePower},
                {"topology",episodeTopology},{"mode",episodeMode},{"bindCount",bindCount}}},
            {"controlAuthority",false}
        };
    }
};
} // namespace ymcc::msi