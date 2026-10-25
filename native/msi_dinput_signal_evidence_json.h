#pragma once
#include "msi_dinput_trigger_evidence.h"
#include "json.hpp"

// Serialization only on a selected evidence row or explicit export.
namespace ymcc::msi::triggerEvidence {
inline nlohmann::json sampleJson(const Sample& s) {
    return {{"rawAxes",s.rawAxes},{"normalizedAxes",s.normalizedAxes},
        {"actualRangeMinimum",s.actualMin},{"actualRangeMaximum",s.actualMax},
        {"rawButtonMasks",s.rawButtons},{"rawButtonBytes",s.rawButtonBytes},
        {"rawLowerBitMasks",s.rawLowerBitButtons},{"rawLowBitOnlyButtonMasks",s.rawLowBitOnlyButtons},
        {"reportedButtonCount",s.reportedButtonCount},{"rawPov",s.pov},{"canonicalKnown",s.canonicalKnown},
        {"buttonMask",s.buttons},{"sticks",s.sticks},{"readStartedTick",s.startedTick},
        {"readCompletedTick",s.completedTick},{"powerGeneration",s.power},{"samplePowerGeneration",s.power},
        {"modeGeneration",s.mode},{"sampleModeGeneration",s.mode},
        {"hostEpoch",s.hostEpoch},{"sourceRevision",s.sourceRevision},{"canonicalReadSequence",s.canonicalReadSequence},
        {"rawRangeKnownMask",s.rawRangeKnownMask},{"rawRangeViolationMask",s.rawRangeViolationMask},
        {"normalizedRangeViolationMask",s.normalizedRangeViolationMask}};
}
// main may merge this after sampleJson on selection.record. All counts describe
// selection before serialization/logger invocation, never successful persistence.
inline nlohmann::json decisionJson(const Decision& d) {
    return {{"legacyRecordSelected",d.legacyRecordSelected},{"reasonBits",d.reasonBits},{"contextChangeBits",d.contextChangeBits},
        {"sustainedWitness",d.sustainedWitness},{"contextBoundarySelected",d.contextBoundarySelected},
        {"contextBoundarySuppressed",d.contextBoundarySuppressed},
        {"recordSelectionsWithinBinding",d.recordSelections},{"sustainedSelectionsWithinBinding",d.sustainedSelections},
        {"contextSelectionsWithinBinding",d.contextSelections},{"contextSuppressedWithinBinding",d.contextSuppressed},
        {"meaningfulSuppressedWithinBinding",d.meaningfulSuppressed},{"logCountersAreSelectionsNotPersistence",true}};
}
inline nlohmann::json edgeJson(const Edge& e) {
    auto row=sampleJson(e.signal);
    row.update({{"sampleWithinBinding",e.sample},{"leftTrigger",e.left},{"rightTrigger",e.right},
        {"outcome",e.outcome},{"reasonBits",e.reasons},{"canonicalPublished",e.outcome==1}});
    return row;
}
inline nlohmann::json boundaryJson(const ContextBoundary& b) {
    return {{"ordinal",b.ordinal},{"contextChangeBits",b.changeBits},{"contextQuotaSelected",b.contextQuotaSelected},{"recordSelected",b.recordSelected},
        {"before",edgeJson(b.before)},{"after",edgeJson(b.after)}};
}
inline nlohmann::json snapshotJson(const Snapshot& s) {
    auto rows=nlohmann::json::array(), initial=nlohmann::json::array(), sustained=nlohmann::json::array();
    auto firstContexts=nlohmann::json::array(), recentContexts=nlohmann::json::array();
    const auto count=s.meaningful < Observer::capacity ? s.meaningful : Observer::capacity;
    const auto sustainedCount=s.sustainedSelections < sustainedCapacity ? s.sustainedSelections : sustainedCapacity;
    const auto contextCount=s.contextEdges < contextCapacity ? s.contextEdges : contextCapacity;
    for (std::uint64_t i=0;i<count;++i) rows.push_back(edgeJson(s.recent[static_cast<unsigned>(i)]));
    for (unsigned i=0;i<s.logAttempts;++i) initial.push_back(edgeJson(s.initial[i]));
    for (std::uint64_t i=0;i<sustainedCount;++i) sustained.push_back(edgeJson(s.sustained[static_cast<unsigned>(i)]));
    for (std::uint64_t i=0;i<contextCount;++i) {
        firstContexts.push_back(boundaryJson(s.firstContexts[static_cast<unsigned>(i)]));
        recentContexts.push_back(boundaryJson(s.recentContexts[static_cast<unsigned>(i)]));
    }
    return {{"formatVersion",2},{"sustainedEvidenceVersion",1},{"diagnosticOnly",true},{"bindingGeneration",s.generation},
        {"samples",s.samples},{"meaningfulSamples",s.meaningful},{"retainedRecentSamples",count},
        {"evictedRecentSamples",s.meaningful-count},{"cappedLogAttempts",s.logAttempts},
        {"logCapacityReceiptSelected",s.capacityReported},{"recordSelections",s.recordSelections},
        {"sustainedSelections",s.sustainedSelections},{"sustainedSkippedIntervals",s.sustainedSkippedIntervals},
        {"contextSelections",s.contextSelections},{"contextSuppressed",s.contextSuppressed},
        {"meaningfulSuppressed",s.meaningfulSuppressed},{"clockRepeats",s.clockRepeats},
        {"clockRollbacks",s.clockRollbacks},{"clockHighWater",s.clockHighWater},
        {"logCountersAreSelectionsNotPersistence",true},
        {"contextSuppressionScope","Independent context quota; legacy or sustained selection may still carry this sample"},
        {"meaningfulSuppressionScope","No row selected by any channel"},
        {"sustainedIntervalMs",sustainedIntervalMs},{"contextBurst",contextBurst},{"contextRefillIntervalMs",sustainedIntervalMs},
        {"evictedSustainedSamples",s.sustainedSelections-sustainedCount},
        {"evictedRecentContexts",s.contextEdges-contextCount},
        {"contextHistoryCapacity",contextCapacity},{"sustainedHistoryCapacity",sustainedCapacity},
        {"initial",std::move(initial)},{"sustained",std::move(sustained)},
        {"firstContexts",std::move(firstContexts)},{"recentContexts",std::move(recentContexts)},
        {"triggerEdges",s.triggerEdges},{"buttonEdges",s.buttonEdges},{"rawButtonEdges",s.rawButtonEdges},
        {"povEdges",s.povEdges},{"stickEdges",s.stickEdges},{"rangeEdges",s.rangeEdges},
        {"outcomeEdges",s.outcomeEdges},{"contextEdges",s.contextEdges},{"recent",std::move(rows)},
        {"outcomeLegend",{{"1","canonical-published"},{"2","axis-range-unverified"},{"3","canonical-rejected"},
            {"4","sample-generation-changed"},{"5","reacquire-generation-changed"},{"6","startup-neutral-pending"}}},
        {"reasonBitsLegend",{{"1","first-samples"},{"2","trigger-class"},{"4","outcome"},{"8","canonical-button"},
            {"16","raw-button"},{"32","pov"},{"64","stick-class"},{"128","axis-range"},{"256","source-context"},
            {"512","sustained-witness"},{"1024","context-boundary-selected"},{"2048","legacy-capacity-receipt"}}},
        {"contextChangeBitsLegend",{{"1","power"},{"2","mode"},{"4","host"},{"8","sourceRevision"}}},
        {"captureScope","Already-read MSI DInput values; bounded initial, recent, sustained and context histories in current binding"},
        {"hostSubmissionProven",false},{"steamConsumptionProven",false}};
}
} // namespace ymcc::msi::triggerEvidence
