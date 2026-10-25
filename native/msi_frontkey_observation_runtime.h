#pragma once
// Included after MSI source/power/queue state exists. Producer/UI only publish
// bounded observations. Registry/process/service reads happen on the existing
// serial worker; no setters, mode writes, process stops or action replacements.
static ymcc::msi::frontObservation::Plan g_msiFrontObservationPlan;
static ymcc::msi::frontObservation::Context msiFrontObservationContext(){
    return {msiClawKnownBoardAuthority(),hardwareWriteAllowed(),g_exitRequested.load(std::memory_order_acquire),
        g_shortcutRecordingActive.load(std::memory_order_acquire),currentPowerGeneration(),
        g_oemShortcutEdgeEpoch.load(std::memory_order_acquire)};
}
static bool msiFrontObservationCurrent(const ymcc::msi::frontObservation::Request& r){
    const bool source=r.origin!=ymcc::msi::frontObservation::Origin::Click||msiWmiGenerationCurrent(r.source);
    const bool vendor=r.origin!=ymcc::msi::frontObservation::Origin::Takeover||oemVendorStackIntentCurrent(r.vendorIntent);
    return ymcc::msi::frontObservation::current(r,msiFrontObservationContext(),source,vendor);
}
static json msiFrontObservationWidgetRouting(){
    // CTW uses the 64-bit value. Read both views, preserving unknown/missing/raw
    // data. A matching routing string does not prove a callback or input writer.
    ymcc::msi::policyMetadata::WindowsReader reader;
    const ymcc::msi::policyMetadata::Field field{L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\GamingConfiguration",L"OEMGameBarWidget","oem-widget-route"};
    json rows=json::array();
    for(const auto view:{ymcc::msi::policyMetadata::View::View32,ymcc::msi::policyMetadata::View::View64}){
        const auto value=reader.read(view,field);const auto text=ymcc::msi::policyMetadata::exactText(value);
        rows.push_back({{"view",view==ymcc::msi::policyMetadata::View::View32?32:64},
            {"state",ymcc::msi::policyMetadata::stateName(value.state)},{"type",value.type},{"error",value.error},
            {"rawBytes",value.bytes},{"exactRegSzText",text},
            {"msiQuickSettingsRoute",!text.is_null()&&text.get<std::string>()=="9426MICRO-STARINTERNATION.MSIQuickSettings_kzh8wxbdkxb8p"},
            {"registryMutated",false},{"defaultActionSuppressed",false},{"writerProven",false}});
    }
    return {{"subkey","SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\GamingConfiguration"},
        {"name","OEMGameBarWidget"},{"ctwReadView",64},{"views",rows},{"readOnly",true}};
}
static void msiFrontObservationRun(ymcc::msi::frontObservation::Request request) noexcept {
    // Exactly the same queued receipt/power/rule/ownership must survive the read.
    // Failure never changes rule execution, vendor ownership or input admission.
    if(!msiFrontObservationCurrent(request)||GetTickCount64()>request.expires)return;
    try{
        const auto begin=GetTickCount64();auto stack=ymcc::msi::cause::officialStack();
        json route={{"readOnly",true},{"complete",false},{"reason","soft-budget-spent-before-widget-route"}};
        const auto now=GetTickCount64();if(now>=begin&&now-begin<140)route=msiFrontObservationWidgetRouting();
        const bool stable=msiFrontObservationCurrent(request);
        const auto stats=g_msiFrontObservationPlan.stats();
        appendNativeLifecycleLog("msi-oem-suppression-observation",{
            {"schema",1},{"origin",request.origin==ymcc::msi::frontObservation::Origin::Click?"wmi-click":"vendor-takeover-result"},
            {"phase",request.phase==ymcc::msi::frontObservation::Phase::Immediate?"immediate":"settled-1500ms"},
            {"code",request.code},{"sourceGeneration",request.source},{"sequence",request.sequence},
            {"ruleEpoch",request.rule},{"capturedAt",request.capturedAt},{"requestPowerGeneration",request.power},
            {"vendorIntent",request.vendorIntent},{"configured",request.configured},
            {"actionDispatchAccepted",request.actionDispatchAccepted},{"takeoverApplied",request.takeoverApplied},
            {"beginTick",begin},{"endTick",GetTickCount64()},{"contextStableAcrossRead",stable},
            {"officialStack",std::move(stack)},{"oemWidgetRouting",std::move(route)},
            {"observationEpisodes",stats.episodes},{"episodeBudget",8},{"snapshotBudget",16},
            {"queueDrops",stats.queueDrops},{"expired",stats.expired},
            {"readOnly",true},{"registryMutated",false},{"hardwareWriteIssued",false},
            {"defaultActionSuppressionVerified",false},{"modeWriterProven",false},
            {"scope","observed process/service/config presence, not live WMI subscriber or action-writer proof"},
            {"budgetMeaning","soft budget between APIs; no hard cancellation of an in-flight read"}});
    }catch(...){/* Diagnostics are never input/control authority. No retry. */}
}
static void msiOemSuppressionObservationPump() noexcept {
    try {
    if(!msiClawKnownBoardAuthority())return;
    ymcc::msi::frontObservation::Request request;
    if(!g_msiFrontObservationPlan.take(GetTickCount64(),request)||!msiFrontObservationCurrent(request))return;
    if(!gamepadSerialSubmit([request]{msiFrontObservationRun(request);}))g_msiFrontObservationPlan.queueDropped();
    }catch(...){/* Diagnostic scheduling never breaks input/action delivery. */}
}
static void msiOemSuppressionObserveAfterTakeover(unsigned long long epoch,bool applied) noexcept {
    try{
        if(!msiClawKnownBoardAuthority()||!hardwareWriteAllowed()||g_exitRequested.load())return;
        ymcc::msi::frontObservation::Request request;request.origin=ymcc::msi::frontObservation::Origin::Takeover;
        request.power=currentPowerGeneration();request.rule=g_oemShortcutEdgeEpoch.load(std::memory_order_acquire);
        request.vendorIntent=epoch;request.takeoverApplied=applied;request.capturedAt=GetTickCount64();
        if(msiFrontObservationCurrent(request)&&g_msiFrontObservationPlan.offer(request,request.capturedAt))msiOemSuppressionObservationPump();
    }catch(...){/* Does not affect the apply result. */}
}
static void msiOemSuppressionObserveClick(const MsiWmiClickReceipt& receipt,bool configured,bool accepted) noexcept {
    try{
        if(!msiClawKnownBoardAuthority())return;
        ymcc::msi::frontObservation::Request request;request.origin=ymcc::msi::frontObservation::Origin::Click;
        request.power=currentPowerGeneration();request.rule=receipt.ruleEpoch;request.source=receipt.generation;
        request.sequence=receipt.sequence;request.code=receipt.code;request.capturedAt=receipt.capturedAt;
        request.configured=configured;request.actionDispatchAccepted=accepted;
        if(msiFrontObservationCurrent(request)&&g_msiFrontObservationPlan.offer(request,GetTickCount64()))msiOemSuppressionObservationPump();
    }catch(...){/* Does not affect the action result. */}
}