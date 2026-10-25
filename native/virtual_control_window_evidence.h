#pragma once
// Included after FocusFrameContent. All per-frame work is fixed-size scalar
// accumulation. JSON is produced only for the existing once-per-second witness.
struct NativeControlWindowEvidence {
    struct Edge {unsigned long long tick=0,sample=0;int buttons=0,back=0;unsigned screen=0,touch=0;unsigned char lt=0,rt=0;unsigned sourceButtons=0,sourceLt=0,sourceRt=0;unsigned long long sourceRead=0;bool sourceKnown=false,sourceLive=false;unsigned long long observedTick=0;};
    std::array<Edge,16> edges{};std::size_t edgeBegin=0,edgeCount=0;
    unsigned long long observations=0,attempts=0,acks=0,firstTick=0,lastTick=0,firstSample=0,lastSample=0,edgeTransitions=0,edgeOverwritten=0,bothRawNonzero=0,bothRaw127=0,nonfinite=0;
    unsigned long long discardedOnBindingChange=0,epoch=0,power=0;
    std::string run,persona;bool haveBinding=false,haveLast=false;
    unsigned long long sourceObservations=0,sourceLiveObservations=0,sourceBoth127=0;unsigned sourceButtonsOr=0;unsigned long long firstObservedTick=0,lastObservedTick=0;
    std::array<unsigned,2> sourceTriggerMin{},sourceTriggerMax{};
    Edge last{};unsigned buttonsOr=0,buttonsAnd=0,backOr=0,screenOr=0,touchOr=0;
    std::array<double,26> minimum{},maximum{};std::array<bool,26> valueKnown{};
    void clearWindow() noexcept {observations=attempts=acks=firstTick=lastTick=firstSample=lastSample=edgeTransitions=edgeOverwritten=bothRawNonzero=bothRaw127=nonfinite=0;edgeBegin=edgeCount=0;buttonsOr=buttonsAnd=backOr=screenOr=touchOr=0;valueKnown={};sourceObservations=sourceLiveObservations=sourceBoth127=0;sourceButtonsOr=0;sourceTriggerMin={};sourceTriggerMax={};firstObservedTick=lastObservedTick=0;}
    void disabled() noexcept {clearWindow();haveLast=false;haveBinding=false;}
    bool observe(const FocusFrameContent& f,const json& receipt,unsigned long long tick,unsigned long long sample,unsigned long long requestPower,bool attempted,bool ack,unsigned long long observedTick,const XINPUT_GAMEPAD* sourcePad,bool sourceLive,unsigned long long sourceRead,NativeControlWindowEvidence* retired=nullptr) {
        bool rolled=false;
        if(receipt.is_object()){
            const auto ri=receipt.find("runId"),pi=receipt.find("persona"),ei=receipt.find("epoch");
            if(ri!=receipt.end()&&pi!=receipt.end()&&ei!=receipt.end()&&ri->is_string()&&pi->is_string()&&ei->is_number_unsigned()){
                const auto& nextRun=ri->get_ref<const std::string&>();const auto& nextPersona=pi->get_ref<const std::string&>();const auto nextEpoch=ei->get<unsigned long long>();
                if(!haveBinding||run!=nextRun||persona!=nextPersona||epoch!=nextEpoch||power!=requestPower){
                    if(observations&&retired){*retired=*this;rolled=true;}else discardedOnBindingChange+=observations;
                    clearWindow();haveLast=false;run=nextRun;persona=nextPersona;epoch=nextEpoch;power=requestPower;haveBinding=true;
                }
            }
        }
        Edge e{tick,sample,f.buttons,f.backButtons,f.screenButtons,
            (f.touchpads.present?1u:0u)|(f.touchpads.left.down?2u:0u)|(f.touchpads.right.down?4u:0u)|(f.touchpads.left.click?8u:0u)|(f.touchpads.right.click?16u:0u),f.rawLtByte,f.rawRtByte};
        e.observedTick=observedTick;e.sourceKnown=sourcePad!=nullptr;e.sourceLive=sourceLive;e.sourceRead=sourceRead;
        if(sourcePad){e.sourceButtons=sourcePad->wButtons;e.sourceLt=sourcePad->bLeftTrigger;e.sourceRt=sourcePad->bRightTrigger;
            sourceButtonsOr|=sourcePad->wButtons;sourceBoth127+=sourcePad->bLeftTrigger==127&&sourcePad->bRightTrigger==127;
            const std::array<unsigned,2> triggers{sourcePad->bLeftTrigger,sourcePad->bRightTrigger};
            for(std::size_t i=0;i<2;++i){if(!sourceObservations)sourceTriggerMin[i]=sourceTriggerMax[i]=triggers[i];else{sourceTriggerMin[i]=(std::min)(sourceTriggerMin[i],triggers[i]);sourceTriggerMax[i]=(std::max)(sourceTriggerMax[i],triggers[i]);}}
            ++sourceObservations;sourceLiveObservations+=sourceLive;}
        if(!observations){firstObservedTick=observedTick;firstTick=tick;firstSample=sample;buttonsAnd=static_cast<unsigned>(f.buttons);}
        lastObservedTick=observedTick;++observations;attempts+=attempted;acks+=ack;lastTick=tick;lastSample=sample;
        buttonsOr|=static_cast<unsigned>(f.buttons);buttonsAnd&=static_cast<unsigned>(f.buttons);backOr|=static_cast<unsigned>(f.backButtons);screenOr|=f.screenButtons;touchOr|=e.touch;
        bothRawNonzero+=f.rawLtByte!=0&&f.rawRtByte!=0;bothRaw127+=f.rawLtByte==127&&f.rawRtByte==127;
        const std::array<double,26> values{double(f.rawLtByte),double(f.rawRtByte),f.lt,f.rt,f.ltRaw,f.rtRaw,
            double(f.lxShort),double(f.lyShort),double(f.rxShort),double(f.ryShort),double(f.touchpads.left.x),double(f.touchpads.left.y),double(f.touchpads.right.x),double(f.touchpads.right.y),
            double(f.touchpads.left.pressure),double(f.touchpads.right.pressure),f.gyroX,f.gyroY,f.gyroZ,f.accelX,f.accelY,f.accelZ,f.qw,f.qx,f.qy,f.qz};
        for(std::size_t i=0;i<values.size();++i){if(!std::isfinite(values[i])){++nonfinite;continue;}
            if(!valueKnown[i]){minimum[i]=maximum[i]=values[i];valueKnown[i]=true;}else{minimum[i]=(std::min)(minimum[i],values[i]);maximum[i]=(std::max)(maximum[i],values[i]);}}
        const bool changed=!haveLast||last.buttons!=e.buttons||last.back!=e.back||last.screen!=e.screen||last.touch!=e.touch||last.lt!=e.lt||last.rt!=e.rt||last.sourceKnown!=e.sourceKnown||last.sourceButtons!=e.sourceButtons||last.sourceLt!=e.sourceLt||last.sourceRt!=e.sourceRt;
        if(changed){++edgeTransitions;if(edgeCount==edges.size()){edgeBegin=(edgeBegin+1)%edges.size();--edgeCount;++edgeOverwritten;}edges[(edgeBegin+edgeCount)%edges.size()]=e;++edgeCount;}
        last=e;haveLast=true;return rolled;
    }
    json takeJson() {
        auto edgeRows=json::array();for(std::size_t i=0;i<edgeCount;++i){const auto& e=edges[(edgeBegin+i)%edges.size()];edgeRows.push_back({{"tick",e.tick},{"observedTick",e.observedTick},{"wireTickKnown",e.tick!=0},{"sampleSequence",e.sample},{"buttons",e.buttons},{"backButtons",e.back},{"screenButtons",e.screen},{"touchBits",e.touch},{"rawLtByte",e.lt},{"rawRtByte",e.rt},{"sourceKnown",e.sourceKnown},{"sourceLive",e.sourceLive},{"sourceReadSequence",e.sourceRead},{"sourceButtons",e.sourceButtons},{"sourceLtByte",e.sourceLt},{"sourceRtByte",e.sourceRt}});}
        json result={{"schemaVersion",1},{"observations",observations},{"submitAttempts",attempts},{"hostAcks",acks},{"firstWireTick",firstTick},{"lastWireTick",lastTick},{"firstObservedTick",firstObservedTick},{"lastObservedTick",lastObservedTick},{"firstSampleSequence",firstSample},{"lastSampleSequence",lastSample},
            {"buttonsOr",buttonsOr},{"buttonsAnd",buttonsAnd},{"backButtonsOr",backOr},{"screenButtonsOr",screenOr},{"touchStateOr",touchOr},{"bothRawTriggersNonzeroFrames",bothRawNonzero},{"bothRawTriggers127Frames",bothRaw127},
            {"fieldOrder",{"rawLtByte","rawRtByte","lt","rt","ltRaw","rtRaw","lxShort","lyShort","rxShort","ryShort","leftTouchX","leftTouchY","rightTouchX","rightTouchY","leftPressure","rightPressure","gyroX","gyroY","gyroZ","accelX","accelY","accelZ","qw","qx","qy","qz"}},
            {"minimum",minimum},{"maximum",maximum},{"fieldValueKnown",valueKnown},{"nonfiniteValues",nonfinite},{"controlEdgeTransitions",edgeTransitions},{"controlEdges",std::move(edgeRows)},{"overwrittenOldestControlEdges",edgeOverwritten},
            {"bindingKnown",haveBinding},{"runId",run},{"epoch",epoch},{"persona",persona},{"powerGeneration",power},{"discardedObservationsOnBindingChange",discardedOnBindingChange},
            {"scope","all calls to this native final-frame observer since prior witness;includes non-submitted frames;not every physical packet or SDK/internal report;touch bits present=1,left-down=2,right-down=4,left-click=8,right-click=16;normal IMU ranges are not anomaly classification"},
            {"selectedSource",{{"observations",sourceObservations},{"liveObservations",sourceLiveObservations},{"buttonsOr",sourceButtonsOr},{"triggerMin",sourceTriggerMin},{"triggerMax",sourceTriggerMax},{"bothRawTriggers127Frames",sourceBoth127},{"scope","selected native source snapshot before mapping;not a fresh hardware packet read or device/UU ownership proof"}}},
            {"sdkOrGameConsumptionProven",false}};
        clearWindow();discardedOnBindingChange=0;return result;
    }
};
static std::mutex g_nativeControlWindowMutex;
static NativeControlWindowEvidence g_nativeControlWindow;
static std::array<NativeControlWindowEvidence,2> g_nativeControlRetired;
static std::size_t g_nativeControlRetiredBegin=0,g_nativeControlRetiredCount=0;
static std::uint64_t g_nativeControlRetiredOverwritten=0,g_nativeControlRetiredObservationsLost=0;
static std::atomic<std::uint64_t> g_nativeControlWindowContention{0};
static std::atomic<bool> g_nativeControlWindowWasEnabled{false};
static void nativeControlWindowDisabled() noexcept {
    if(!g_nativeControlWindowWasEnabled.exchange(false,std::memory_order_acq_rel))return;
    std::unique_lock<std::mutex> lock(g_nativeControlWindowMutex,std::try_to_lock);
    if(lock)g_nativeControlWindow.disabled();else {g_nativeControlWindowWasEnabled.store(true,std::memory_order_release);g_nativeControlWindowContention.fetch_add(1,std::memory_order_relaxed);}
}
static void nativeControlWindowObserve(const FocusFrameContent& frame,const json& receipt,unsigned long long tick,unsigned long long sample,unsigned long long power,bool attempted,bool accepted,unsigned long long observedTick,const XINPUT_GAMEPAD* sourcePad,bool sourceLive,unsigned long long sourceRead) {
    std::unique_lock<std::mutex> lock(g_nativeControlWindowMutex,std::try_to_lock);
    if(!lock){g_nativeControlWindowContention.fetch_add(1,std::memory_order_relaxed);return;}
    g_nativeControlWindowWasEnabled.store(true,std::memory_order_release);
    const auto index=(g_nativeControlRetiredBegin+g_nativeControlRetiredCount)%g_nativeControlRetired.size();
    const auto previousObservations=g_nativeControlRetired[index].observations;
    if(g_nativeControlWindow.observe(frame,receipt,tick,sample,power,attempted,accepted,observedTick,sourcePad,sourceLive,sourceRead,&g_nativeControlRetired[index])){
        if(g_nativeControlRetiredCount==g_nativeControlRetired.size()){g_nativeControlRetiredBegin=(g_nativeControlRetiredBegin+1)%g_nativeControlRetired.size();++g_nativeControlRetiredOverwritten;g_nativeControlRetiredObservationsLost+=previousObservations;}
        else ++g_nativeControlRetiredCount;
    }
}
static json nativeControlWindowSnapshot(bool drain) {
    NativeControlWindowEvidence copy;std::array<NativeControlWindowEvidence,2> retired;
    std::size_t retiredCount=0;std::uint64_t overwritten=0,observationsLost=0;
    {
        std::unique_lock<std::mutex> lock(g_nativeControlWindowMutex,std::try_to_lock);
        if(!lock){g_nativeControlWindowContention.fetch_add(1,std::memory_order_relaxed);return {{"snapshotKnown",false},{"reason","contended"}};}
        copy=g_nativeControlWindow;retiredCount=g_nativeControlRetiredCount;
        for(std::size_t i=0;i<retiredCount;++i)retired[i]=g_nativeControlRetired[(g_nativeControlRetiredBegin+i)%g_nativeControlRetired.size()];
        overwritten=g_nativeControlRetiredOverwritten;observationsLost=g_nativeControlRetiredObservationsLost;
        if(drain){g_nativeControlWindow.clearWindow();g_nativeControlWindow.discardedOnBindingChange=0;g_nativeControlRetiredBegin=g_nativeControlRetiredCount=0;g_nativeControlRetiredOverwritten=g_nativeControlRetiredObservationsLost=0;}
    }
    auto result=copy.takeJson();result["snapshotKnown"]=true;result["drained"]=drain;
    result["diagnosticContentionSinceProcessStart"]=g_nativeControlWindowContention.load(std::memory_order_relaxed);
    auto retiredRows=json::array();for(std::size_t i=0;i<retiredCount;++i)retiredRows.push_back(retired[i].takeJson());
    result["retiredBindingWindows"]=std::move(retiredRows);result["overwrittenRetiredBindingWindows"]=overwritten;
    result["lostRetiredBindingObservations"]=observationsLost;
    result["pendingTailAtExport"]=!drain;return result;
}
