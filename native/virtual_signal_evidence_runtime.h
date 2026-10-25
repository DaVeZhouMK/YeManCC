#pragma once
#include <cmath>
#include "virtual_control_window_evidence.h"
// Included after FocusFrameContent and the ordinary capture/Host diagnostics.
// This passive observer owns no thread, device handle, timer, or retry policy.
// Called only for an already selected <=1 Hz witness. No title/process-image
// lookup, device I/O, timer, or change to focus policy. This observation is NOT
// the earlier focus decision and a class name alone cannot identify Xbox mode.
static json inputHostSignalForegroundObservation() {
    const HWND before=GetForegroundWindow();
    DWORD pid=0;
    const DWORD thread=before ? GetWindowThreadProcessId(before,&pid) : 0;
    char cls[128]{};
    const int length=before ? GetClassNameA(before,cls,static_cast<int>(sizeof(cls))) : 0;
    const DWORD classError=before && length<=0 ? GetLastError() : ERROR_SUCCESS;
    const HWND after=GetForegroundWindow();
    return {{"schemaVersion",1},{"windowBefore",reinterpret_cast<std::uintptr_t>(before)},
        {"windowAfter",reinterpret_cast<std::uintptr_t>(after)},
        {"windowStableDuringRead",before==after},{"pid",pid},{"threadId",thread},
        {"className",length>0 ? std::string(cls,static_cast<std::size_t>(length)) : std::string{}},
        {"classReadSucceeded",length>0},{"classReadError",classError},
        {"scope","selected-witness-observation;not-focus-decision-or-Xbox-session-identity"}};
}

static void inputHostNativeSignalEvidence(const FocusFrameContent& finalContent,
    const json& requestReceipt, bool attempted, bool accepted, bool semanticNeutral,
    unsigned long long wireTick, unsigned long long wireSample, unsigned long long requestPower, unsigned long long barrierMask,
    const XINPUT_GAMEPAD& inputPad, const XINPUT_GAMEPAD& outputPad,
    const FocusStickBlend& blend, bool analogAdmitted) noexcept {
    try {
        if(!domainLogEnabled("virtual")){nativeControlWindowDisabled();return;}
        const auto now=GetTickCount64();
        const auto* sample=t_commonFrameSource;
        nativeControlWindowObserve(finalContent,requestReceipt,wireTick,wireSample,requestPower,attempted,accepted,now,
            sample?&sample->pad:nullptr,sample&&sample->live,sample?sample->readAttemptSequence:0);
        static thread_local ymcc::virtualSignal::Sampler sampler;
        const bool sourceNonzero=sample && (sample->specialXbox || sample->pad.wButtons || sample->pad.bLeftTrigger ||
            sample->pad.bRightTrigger || sample->pad.sThumbLX || sample->pad.sThumbLY ||
            sample->pad.sThumbRX || sample->pad.sThumbRY);
        const auto d=sampler.observe(wireTick,now,attempted,accepted,semanticNeutral,
            sample?sample->readAttemptSequence:0,sample?sample->readPowerGeneration:0,
            sample?sample->readMsiSourceRevision:0,sample?sample->readMsiModeGeneration:0,
            sample!=nullptr,sample&&sample->live,sourceNonzero);
        if(!d.record)return;
        json source={{"snapshotKnown",sample!=nullptr},{"hardwarePacketFreshnessProven",false},
            {"padScope","selected-native-source-pad;raw-MSI-DInput-is-recorded-separately"}};
        if(sample) {
            const auto& p=sample->pad;
            source.update({{"inputSource",sample->inputSource},{"live",sample->live},
                {"readAttemptSequence",sample->readAttemptSequence},{"readCompletedTick",sample->readCompletedTick},
                {"powerGeneration",sample->readPowerGeneration},{"sourceRevision",sample->readMsiSourceRevision},
                {"modeGeneration",sample->readMsiModeGeneration},{"topologyGeneration",sample->readTopologyGeneration},
                {"readContextStable",sample->readContextStable},
                {"pad",{{"buttons",p.wButtons},{"ltByte",p.bLeftTrigger},{"rtByte",p.bRightTrigger},
                    {"lxShort",p.sThumbLX},{"lyShort",p.sThumbLY},{"rxShort",p.sThumbRX},{"ryShort",p.sThumbRY}}}});
        }
        // Assemble once per selected witness from the SAME effective typed value
        // already serialized for this request. This is not another submission.
        json wireRequest=requestReceipt;
        if(wireRequest.is_object() && attempted && wireSample) {
            wireRequest["receiptCapturedSampleSequence"]=wireRequest.value("sampleSequence",0ULL);
            wireRequest["sampleSequence"]=wireSample; // Actual prepared/legacy serialized field.
        }
        // These are the caller's actual post-barrier input and pre-blend pad,
        // plus the already-computed blend. Never infer execution from settings.
        // Keep all composition serialization behind the existing witness gate.
        const auto padJson=[](const XINPUT_GAMEPAD& p) {
            return json{{"buttons",p.wButtons},{"ltByte",p.bLeftTrigger},{"rtByte",p.bRightTrigger},
                {"lxShort",p.sThumbLX},{"lyShort",p.sThumbLY},{"rxShort",p.sThumbRX},{"ryShort",p.sThumbRY}};
        };
        json composition={{"version",1},{"inputPad",padJson(inputPad)},{"outputPad",padJson(outputPad)},
            {"padScope","post-focus-release-barrier-input;outputPad-is-actual-pre-blend-pad"},
            {"analogAdmitted",analogAdmitted},
            {"blend",{{"useMotion",blend.useMotion},{"targetLeft",blend.targetLeft},
                {"targetStick",blend.targetLeft ? "left" : "right"},
                {"contributionX",blend.contributionX},{"contributionY",blend.contributionY},
                {"baseX",blend.baseX},{"baseY",blend.baseY},{"stickNorm",blend.stickNorm},
                {"weightFactor",blend.weightFactor},{"outX",blend.outX},{"outY",blend.outY},
                {"lxShort",blend.lxShort},{"lyShort",blend.lyShort},
                {"rxShort",blend.rxShort},{"ryShort",blend.ryShort}}},
            {"blendScope","actual-computed-stick-blend;before-final-semantic-neutral-override"},
            {"finalOverride",semanticNeutral ? "semantic-neutral" : "none"},
            {"finalUseMotion",blend.useMotion && !semanticNeutral},
            {"finalFrameScope","nativeFinalFrame-is-effective-frame-after-semantic-neutral-override"}};
        json row={{"formatVersion",1},{"diagnosticOnly",true},{"observedTick",now},
            {"wireMonotonicTick",wireTick},{"wireSampleSequence",wireSample},{"protocolPowerGeneration",requestPower},{"wireClockKnown",d.wireClockKnown},{"sampleBucket",d.bucket},
            {"samplePeriodMs",ymcc::virtualSignal::Sampler::intervalMs},{"windowMs",d.elapsedMs},
            {"request",std::move(wireRequest)},{"source",std::move(source)},
            {"composition",std::move(composition)},{"allObservedControls",nativeControlWindowSnapshot(true)},
            {"foregroundObservation",inputHostSignalForegroundObservation()},
            {"nativeFinalFrame",focusIsolationAssembleFrame(finalContent)},
            {"submitAttempted",attempted},{"hostAckAccepted",accepted},{"semanticNeutral",semanticNeutral},
            {"focusReleaseBarrierMask",barrierMask},
            {"window",{{"observations",d.window.observations},{"attempts",d.window.attempts},
                {"accepted",d.window.accepted},{"semanticNeutral",d.window.neutral},
                {"semanticNeutralAccepted",d.window.neutralAccepted},
                {"repeatedKnownSourceReads",d.window.repeatedKnownSourceReads},
                {"clockRegressions",d.window.clockRegressions},
                {"selectedSourceKnownReads",d.window.selectedSourceKnownReads},
                {"selectedSourceLiveReads",d.window.selectedSourceLiveReads},
                {"selectedSourceNonzeroReads",d.window.selectedSourceNonzeroReads}}},
            {"sourceActivityScope","exact-nonzero-before-mapping;includes-noise;window-may-span-foregrounds;not-hardware-packet-freshness"},
            {"counterScope","same-native-thread-since-last-witness;may-span-targets;not-total-bus-callbacks"},
            {"frameScope","effective-native-wire-fields;join-exact-request-tuple-to-Host-SDK-witness"},
            {"physicalSuppressionProven",false},{"driverDeliveryProven",false},{"gameConsumptionProven",false}};
        appendNativeLifecycleLog("input-native-signal-window",std::move(row));
    }catch(...) {++g_commonFrameEvidenceFailures;}
}
