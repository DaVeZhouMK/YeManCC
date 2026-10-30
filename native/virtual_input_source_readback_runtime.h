#pragma once
#include "virtual_input_token_evidence.h"
#include "rog_fse_experiment_policy.h"

// VC-FSE-SOURCE-20261026-R1. Read-only evidence within the existing BUS 1 Hz
// System32 observer. Never selects a source, changes XInputEnable, writes a
// controller, rebuilds Host, or changes foreground/HidHide. Complete states are
// not proof of the same physical device, completed delivery, or game consumption.
// Included after the native source globals and before systemXInputReadbackTick.
static json inputSourceReadbackPadJson(const XINPUT_GAMEPAD& pad) {
    return {{"buttons",pad.wButtons},{"ltByte",pad.bLeftTrigger},
        {"rtByte",pad.bRightTrigger},{"lxShort",pad.sThumbLX},
        {"lyShort",pad.sThumbLY},{"rxShort",pad.sThumbRX},{"ryShort",pad.sThumbRY}};
}

static json inputSourceReadbackTokenJson() {
    // First-observation process metadata only; no token is changed or opened
    // again per frame/window. The snapshot is not a diagnosis of FSE policy.
    static const auto token=ymcc::virtualInputEvidence::processTokenEvidence();
    const auto field=[](const ymcc::virtualInputEvidence::TokenValue& value)->json {
        return {{"known",value.known},{"win32Error",value.error},
            {"value",value.known?json(value.value):json(nullptr)}};
    };
    return {{"scope","current-process-token-at-first-detailed-observation;not-FSE-causality-proof"},
        {"processId",token.processId},{"openError",token.openError},
        {"elevation",field(token.elevation)},{"elevationType",field(token.elevationType)},
        {"integrityRid",field(token.integrityRid)},{"uiAccess",field(token.uiAccess)},
        {"isAppContainer",field(token.isAppContainer)},{"sessionId",field(token.sessionId)}};
}

// R46: inspect the proxy's current slot mapping only on existing diagnostic
// context changes. This never admits/selects a source or triggers recovery.
// A path match does not bind a sequential GetState result to a hardware sample.
static json inputSourceReadbackRogPathEvidence(DWORD selectedSlot,bool sourceContextKnown) {
    json row={{"version",1},{"diagnosticOnly",true},{"queryAttempted",false},
        {"state","source-context-unavailable"},{"pathMatchKnown",false},
        {"pathMatchesSelectedRoot",false},{"pathMatchesSelectedHid",false},
        {"sourcePathBindingProven",false},{"controlsChanged",false},
        {"scope","sequential-OpenXInput-slot-path-vs-cached-physical-identity;not-atomic-with-pad-or-hardware-sample"}};
    if(!sourceContextKnown || selectedSlot>=4)return row;
    row["selectedSlot"]=selectedSlot;
    const auto topologyBefore=g_xinputTopologyEpoch.load(std::memory_order_acquire);
    const auto identityBefore=g_xinputSlotIdentityEpoch.load(std::memory_order_acquire);
    std::string rootPath,hidPath,physicalIdentity;
    bool identityKnown=false;
    {
        // Never stall the source/identity owner to obtain diagnostic text.
        std::unique_lock<std::mutex> lock(g_hidHidePhysicalIdentityMx,std::try_to_lock);
        if(lock.owns_lock()) {
            identityKnown=g_hidHidePhysicalIdentity.resolved;
            rootPath=g_hidHidePhysicalIdentity.baseContainerDevicePath;
            hidPath=g_hidHidePhysicalIdentity.sourcePath;
            physicalIdentity=g_hidHidePhysicalIdentity.physicalIdentity;
        }
    }
    row["queryAttempted"]=true;
    const auto queryBegin=GetTickCount64();
    const auto observedPath=ymccOpenXInputGetDevicePath(selectedSlot);
    const auto queryEnd=GetTickCount64();
    const auto topologyAfter=g_xinputTopologyEpoch.load(std::memory_order_acquire);
    const auto identityAfter=g_xinputSlotIdentityEpoch.load(std::memory_order_acquire);
    const bool stable=topologyBefore==topologyAfter && identityBefore==identityAfter;
    const bool comparable=stable && identityKnown && !observedPath.empty() &&
        (!rootPath.empty() || !hidPath.empty());
    const bool rootMatch=comparable && !rootPath.empty() &&
        _wcsicmp(observedPath.c_str(),U2W(rootPath).c_str())==0;
    const bool hidMatch=comparable && !hidPath.empty() &&
        _wcsicmp(observedPath.c_str(),U2W(hidPath).c_str())==0;
    row["queryBeginTick"]=queryBegin;row["queryEndTick"]=queryEnd;
    row["topologyBefore"]=topologyBefore;row["topologyAfter"]=topologyAfter;
    row["identityEpochBefore"]=identityBefore;row["identityEpochAfter"]=identityAfter;
    row["generationsStableDuringPathQuery"]=stable;
    row["cachedPhysicalIdentityKnown"]=identityKnown;
    row["observedProxyPath"]=W2U(observedPath);
    row["selectedPhysicalRootPath"]=rootPath;row["selectedPhysicalHidPath"]=hidPath;
    row["cachedPhysicalIdentity"]=physicalIdentity;
    row["pathMatchKnown"]=comparable;row["pathMatchesSelectedRoot"]=rootMatch;
    row["pathMatchesSelectedHid"]=hidMatch;
    row["state"]=observedPath.empty()?"proxy-path-unavailable":!stable?"path-generation-changed":
        !identityKnown?"physical-identity-unavailable":!comparable?"physical-path-unavailable":
        (rootMatch || hidMatch)?"cached-physical-path-matches-proxy-slot":"proxy-path-differs-from-cached-physical";
    return row;
}

static void inputSourceReadbackEvidenceTick(ULONGLONG systemReadBeginTick,
        ULONGLONG systemReadEndTick,const std::array<DWORD,4>& systemResults,
        const std::array<XINPUT_STATE,4>& systemStates,FARPROC systemGetter) noexcept {
#if !YMCC_ROG_FSE_DIAGNOSTICS
    // The ROG adaptation probe is absent from normal operation even if the
    // player enables detailed logs. Return before clocks, APIs, locks or JSON.
    if(nativeForegroundProfile()==ymcc::nativeForeground::Profile::Rog)return;
#endif
    // Logging OFF or virtual target OFF: no extra FSE/API/token probes. This
    // adds no timer/thread. During an armed window there is at most one extra
    // canonical GetState per existing 1 Hz pass, never one per BUS frame.
    static bool observed=false;
    static bool priorActiveKnown=false,priorActive=false,priorPlatformKnown=false;
    static HWND priorForeground=nullptr;
    static std::uint64_t priorPower=0;
    static ULONGLONG until=0;
    static unsigned long long dropped=0;
    // Re-enable begins a fresh diagnostic observation window even when the
    // previous 30-second window expired. OFF performs no source/FSE/token reads.
    if(!tickCostEnabled() || !g_realStickTest.virtualTargetEnabled) {
        observed=false;until=0;return;
    }
    try {
        const auto powerBefore=currentPowerGeneration();
        const auto foregroundBefore=GetForegroundWindow();
        DWORD foregroundPidBefore=0;
        if(foregroundBefore)GetWindowThreadProcessId(foregroundBefore,&foregroundPidBefore);
        const auto platformTick=GetTickCount64();
        const auto platform=ymcc::bootmode::readPlatform(); // Query only; never setPlatform.
        const auto now=GetTickCount64();
        const bool powerChanged=observed && priorPower!=powerBefore;
        const bool fseChanged=observed && (priorPlatformKnown!=platform.known ||
            priorActiveKnown!=platform.activeKnown ||
            (platform.activeKnown && priorActive!=platform.active));
        const bool foregroundChanged=observed && priorForeground!=foregroundBefore;
        const char* why=!observed?"first":powerChanged?"power-generation-change":
            fseChanged?"fse-state-change":foregroundChanged?"foreground-change":"armed-sample";
        const bool contextChanged=!observed || powerChanged || fseChanged || foregroundChanged;
        if(contextChanged)until=now+30000; // Fixed bounded evidence window, not a recovery budget.
        if(!contextChanged && (until==0 || now>until))return;

        DWORD selectedSlot=0,connectedMask=0,physicalMask=0,virtualMask=0,unknownMask=0;
        bool physicalLocked=false,sourceContextKnown=false;
        XINPUT_GAMEPAD cachedPad{};
        {
            // Never wait for the real sampling owner to collect a diagnostic.
            std::unique_lock<std::mutex> sourceLock(g_gamepadReadMx,std::try_to_lock);
            if(sourceLock.owns_lock()) {
                sourceContextKnown=true;selectedSlot=g_xinputPrimarySlot;
                connectedMask=g_xinputConnectedMask;physicalMask=g_xinputPhysicalSourceMask;
                virtualMask=g_xinputVirtualEndpointMask;unknownMask=g_xinputSlotUnknownMask;
                physicalLocked=g_xinputPhysicalSourceLocked;
                if(selectedSlot<g_xinputPads.size())cachedPad=g_xinputPads[selectedSlot];
            }
        }
        const bool nativeReadAttempted=sourceContextKnown && physicalLocked && selectedSlot<4 &&
            (physicalMask&(1u<<selectedSlot))!=0 && g_ymccXInputGetState14!=nullptr;
        XINPUT_STATE nativeState{};
        DWORD nativeResult=ERROR_NOT_READY;
        const auto nativeReadBeginTick=GetTickCount64();
        if(nativeReadAttempted)nativeResult=g_ymccXInputGetState14(selectedSlot,&nativeState);
        const auto nativeReadEndTick=GetTickCount64();
        const auto foregroundAfter=GetForegroundWindow();
        DWORD foregroundPidAfter=0;
        if(foregroundAfter)GetWindowThreadProcessId(foregroundAfter,&foregroundPidAfter);
        const auto powerAfter=currentPowerGeneration();
        json slots=json::array();
        for(unsigned i=0;i<4;++i) {
            const bool ok=systemResults[i]==ERROR_SUCCESS;
            slots.push_back({{"slot",i},{"result",systemResults[i]},
                {"packet",ok?json(systemStates[i].dwPacketNumber):json(nullptr)},
                {"pad",ok?inputSourceReadbackPadJson(systemStates[i].Gamepad):json(nullptr)}});
        }
        HMODULE nativeOwner=nullptr,systemOwner=nullptr;
        const auto nativeApi=nativeXInputFunctionModuleEvidence(
            reinterpret_cast<FARPROC>(g_ymccXInputGetState14),nativeOwner);
        const auto systemApi=nativeXInputFunctionModuleEvidence(systemGetter,systemOwner);
        json evidence={
            {"formatVersion",1},{"reason",why},{"diagnosticOnly",true},
            {"scope","sequential-extra-canonical-read-vs-existing-System32-reads;never-used-for-source-or-output"},
            {"systemReadBeginTick",systemReadBeginTick},{"systemReadEndTick",systemReadEndTick},
            {"fseObservedTick",platformTick},{"fseObservedAfterSystemRead",true},
            {"fse",ymcc::bootmode::platformJson(platform)},
            {"nativeReadBeginTick",nativeReadBeginTick},{"nativeReadEndTick",nativeReadEndTick},
            {"powerBefore",powerBefore},{"powerAfter",powerAfter},
            {"powerStableDuringProbe",powerBefore==powerAfter},
            {"foregroundBefore",static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(foregroundBefore))},
            {"foregroundAfter",static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(foregroundAfter))},
            {"foregroundPidBefore",foregroundPidBefore},{"foregroundPidAfter",foregroundPidAfter},
            {"foregroundStableDuringProbe",foregroundBefore==foregroundAfter && foregroundPidBefore==foregroundPidAfter},
            {"ymccForeground",focusMainWindowIsForeground()},
            {"persona",inputHostPersona()},{"hostEpoch",inputHostEpochToken()},
            {"systemApi",systemApi},{"nativeApi",nativeApi},
            {"sourceContextKnown",sourceContextKnown},
            {"selectedSlot",sourceContextKnown?json(selectedSlot):json(nullptr)},
            {"sourceContext",{{"physicalLocked",physicalLocked},{"connectedMask",connectedMask},
                {"physicalSourceMask",physicalMask},{"virtualEndpointMask",virtualMask},{"unknownSlotMask",unknownMask}}},
            {"nativeProducerCache",sourceContextKnown && selectedSlot<4?
                inputSourceReadbackPadJson(cachedPad):json(nullptr)},
            {"nativeProducerCacheScope","copied-under-source-lock;not-an-extra-hardware-read-or-atomic-with-system-state"},
            {"nativeProbe",{{"attempted",nativeReadAttempted},
                {"result",nativeReadAttempted?json(nativeResult):json(nullptr)},
                {"packet",nativeReadAttempted && nativeResult==ERROR_SUCCESS?json(nativeState.dwPacketNumber):json(nullptr)},
                {"pad",nativeReadAttempted && nativeResult==ERROR_SUCCESS?
                    inputSourceReadbackPadJson(nativeState.Gamepad):json(nullptr)}}},
            {"systemSlots",std::move(slots)},
            {"readPairAtomic",false},{"samePhysicalDeviceBindingProven",false},
            {"driverDeliveryProven",false},{"gameConsumptionProven",false},
            {"controlsChanged",false},{"droppedDiagnosticAttempts",dropped}};
        if(contextChanged) {
            // ROG only, on already-existing context observations. No per-frame
            // path scan and no source/output decision consumes this result.
            if(nativeForegroundProfile()==ymcc::nativeForeground::Profile::Rog)
                evidence["rogSourcePathEvidence"]=inputSourceReadbackRogPathEvidence(selectedSlot,sourceContextKnown);
            auto context=evidence;
            context["processToken"]=inputSourceReadbackTokenJson();
            context["windowMs"]=30000;
            appendNativeLifecycleLog("input-source-readback-context",std::move(context));
        }
        appendDomainLog("virtual","input-source-readback-pair",std::move(evidence));
        observed=true;priorPower=powerBefore;priorForeground=foregroundBefore;
        priorPlatformKnown=platform.known;priorActiveKnown=platform.activeKnown;priorActive=platform.active;
    } catch(...) {
        ++dropped; // Diagnostics cannot escape into the BUS/Host lifecycle.
    }
}
