#pragma once
#include <utility>
#include "virtual_signal_evidence.h"

namespace ymcc::nativeSignalWindowLogScope {
inline constexpr char markerField[]="nativeSignalScopeSchema";
inline constexpr unsigned version=1;
struct Text {const char* object;const char* field;const char* value;};
inline constexpr Text texts[]={
    {nullptr,"sourceActivityScope","exact-nonzero-before-mapping;includes-noise;window-may-span-foregrounds;not-hardware-packet-freshness"},
    {nullptr,"counterScope","same-native-thread-since-last-witness;may-span-targets;not-total-bus-callbacks"},
    {nullptr,"frameScope","effective-native-wire-fields;join-exact-request-tuple-to-Host-SDK-witness"},
    {"source","padScope","selected-native-source-pad;raw-MSI-DInput-is-recorded-separately"},
    {"composition","padScope","post-focus-release-barrier-input;outputPad-is-actual-pre-blend-pad"},
    {"composition","blendScope","actual-computed-stick-blend;before-final-semantic-neutral-override"},
    {"composition","finalFrameScope","nativeFinalFrame-is-effective-frame-after-semantic-neutral-override"},
    {"foregroundObservation","scope","selected-witness-observation;not-focus-decision-or-Xbox-session-identity"},
};

template<class Json>
void write(Json& row,const virtualSignal::Decision& decision,bool sourceLive,bool drainValid) {
    const auto& foreground=row.at("foregroundObservation");
    const auto& controls=row.at("allObservedControls");
    const auto& window=decision.window;
    const bool compact=decision.wireClockKnown && decision.elapsedMs && row.at("submitAttempted")==true &&
        row.at("hostAckAccepted")==true && row.at("semanticNeutral")==false && sourceLive && drainValid &&
        window.attempts && window.attempts==window.observations && window.accepted==window.attempts &&
        !window.neutral && !window.clockRegressions &&
        foreground.value("windowStableDuringRead",false) && foreground.value("classReadSucceeded",false) &&
        controls.value("snapshotKnown",false) && controls.value("bindingKnown",false) &&
        controls.at("retiredBindingWindows").empty() && !controls.value("nonfiniteValues",1ULL) &&
        !controls.value("overwrittenOldestControlEdges",1ULL) && !controls.value("overwrittenRetiredBindingWindows",1ULL) &&
        !controls.value("lostRetiredBindingObservations",1ULL);
    if(compact) {row[markerField]=version;return;}
    for(const auto& text:texts) (text.object?row.at(text.object):row)[text.field]=text.value;
}

template<class Json>
Json guide() {
    auto fields=Json::object();
    for(const auto& text:texts) (text.object?fields[text.object]:fields["root"])[text.field]=text.value;
    return {{"event","input-native-signal-window"},{"formatVersion",1},{"markerField",markerField},{"markerValue",version},
        {"fields",std::move(fields)},
        {"readRule","For the matching event, format and marker only, fill absent fields at root or within existing named objects. Preserve any inline value. Omitted text is not a missing observation. Unknown markers do not supply defaults."}};
}
}
