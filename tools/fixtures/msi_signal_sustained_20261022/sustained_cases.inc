// Included by the existing actual-source adapter fixture; no model adapter.
static json sustainedReport;
static void sustainedCases() {
    using namespace ymcc::msi::triggerEvidence;
    static_assert(std::is_trivially_destructible_v<Observer>);
    static_assert(sizeof(Observer) < 80 * 1024);
    DIJOYSTATE2 raw{};
    raw.lX=10000;raw.lY=raw.lZ=raw.lRz=32768;raw.rgdwPOV[0]=0xffffffffu;
    auto normalized=raw;normalized.lX=32768;
    XINPUT_GAMEPAD pad{};
    check(msiDinputCanonicalPad(normalized,pad),"sustained fixture uses actual mapper");
    const auto originalRaw=raw,originalNormalized=normalized;const auto originalPad=pad;
    rows.clear();++g_msiDinput.bindCount;hostEpoch=7;g_msiMainSourceRevision=5;
    const auto binding=g_msiDinput.bindCount;
    auto record=[&](ULONGLONG tick,unsigned power=1,unsigned mode=1,unsigned outcome=1) {
        msiDinputRecordTriggerEvidence(raw,normalized,outcome==3?nullptr:&pad,outcome,
            outcome==3?"canonical-rejected":"canonical-published",tick,tick,DI_OK,DI_OK,power,mode);
    };
    for(unsigned i=0;i<65;++i){pad.bLeftTrigger=(i&1)?127:0;record(1000+i);}
    check(rows.size()==65&&rows.back()["diagnosticCapacityReached"]==true,"actual adapter exhausts legacy64 plus one receipt");
    pad=originalPad;
    for(ULONGLONG tick=1072;tick<=601000;tick+=8)record(tick);
    check(rows.size()==185,"ten minutes after cap still writes exactly120 sustained witnesses");
    check(rows.back()["rawAxes"][0]==10000&&rows.back()["normalizedAxes"][0]==32768&&
        rows.back()["canonical"]["leftX"]==0,"sustained row carries same-read raw and converted values");
    auto snapshot=msiDinputSignalEvidenceSnapshot();
    check(snapshot["sustainedSelections"]==120&&snapshot["recordSelections"]==185&&snapshot["cappedLogAttempts"]==64,
        "sustained counters independent of legacy budget");
    check(snapshot["sustained"].size()==32&&snapshot["evictedSustainedSamples"]==88,
        "sustained export has bounded history with explicit eviction");
    check(snapshot["initial"].size()==64&&snapshot["initial"][0]["sampleWithinBinding"]==1,
        "initial onset survives many minutes");
    const auto allocations=allocationRequests.load();const auto before=rows.size();
    for(unsigned i=0;i<100000;++i)record(601000);
    check(rows.size()==before&&allocationRequests.load()==allocations,"duplicate completedTick steady path has no heap or log churn");
    record(606000,1,1,3);
    check(rows.size()==before+1&&rows.back()["canonical"].is_null()&&rows.back()["canonicalPublished"]==false,
        "periodic rejected sample preserves unknown canonical state");
    hostEpoch=8;g_msiMainSourceRevision=6;pad.bLeftTrigger=127;
    record(606001,2,2,6);
    snapshot=msiDinputSignalEvidenceSnapshot();
    check(g_msiDinput.bindCount==binding&&rows.size()==before+2&&snapshot["contextSelections"]==1,
        "wake context writes immediately after cap within same binding");
    check(snapshot["recentContexts"][0]["contextChangeBits"]==15&&
        snapshot["recentContexts"][0]["before"]["readCompletedTick"]==606000&&
        snapshot["recentContexts"][0]["after"]["samplePowerGeneration"]==2,
        "context boundary pins actual before and after plus all four changed fields");
    const auto wakeSample=snapshot["recentContexts"][0]["after"]["sampleWithinBinding"];
    for(unsigned i=0;i<10000;++i){pad.bLeftTrigger=(i&1)?127:0;record(606002,2,2,6);}
    snapshot=msiDinputSignalEvidenceSnapshot();
    check(snapshot["recentContexts"][0]["after"]["sampleWithinBinding"]==wakeSample&&
        snapshot["recent"][0]["sampleWithinBinding"]!=wakeSample&&snapshot["initial"][0]["sampleWithinBinding"]==1,
        "input storm cannot overwrite pinned initial or independent wake boundary");
    const auto stormBefore=rows.size();
    for(unsigned i=0;i<50000;++i)record(606002,10+i,2,6);
    const auto stormAllocations=allocationRequests.load();
    for(unsigned i=0;i<50000;++i)record(606002,50010+i,2,6);
    check(rows.size()==stormBefore+3&&allocationRequests.load()==stormAllocations,
        "context storm consumes remaining burst then stays allocation-free");
    snapshot=msiDinputSignalEvidenceSnapshot();
    check(snapshot["contextEdges"]==100001&&snapshot["contextSelections"]==4&&snapshot["contextSuppressed"]==99997,
        "every context change accounted while storm logs stay finite");
    check(snapshot["firstContexts"].size()==8&&snapshot["recentContexts"].size()==8&&
        snapshot["firstContexts"][0]["after"]["sampleWithinBinding"]==wakeSample,
        "context onset and recent boundary histories independently bounded");
    const auto rollbackBefore=rows.size();
    for(unsigned i=0;i<10000;++i)record((i&1)?606002:1,100010+i,2,6);
    check(rows.size()==rollbackBefore,"repeated and backward clocks never reopen storm budget");
    snapshot=msiDinputSignalEvidenceSnapshot();
    check(snapshot["clockRollbacks"]==5000&&snapshot["clockRepeats"].get<uint64_t>()>=100000,
        "repeat and regression omissions observable");
    record(1206002,200010,2,6);
    check(rows.size()==rollbackBefore+1,"large clock jump coalesces sustained and context in one row");
    snapshot=msiDinputSignalEvidenceSnapshot();
    check(snapshot["sustainedSkippedIntervals"]==119,"large jump reports skipped intervals without catch-up burst");
    const auto jumpBefore=rows.size();
    for(unsigned i=0;i<10000;++i)record(1206002,200011+i,2,6);
    check(rows.size()==jumpBefore+3,"large jump refills only a bounded context burst");
    sustainedReport={{"legacyRows",65},{"tenMinuteSustainedRows",120},{"tenMinuteTotalRows",185},
        {"sameBindingWake",true},{"stormChanges",100000},{"stormExtraRows",3},
        {"contextSuppressedAfterStorm",99997},{"clockRollbackSamples",5000},
        {"snapshot",msiDinputSignalEvidenceSnapshot()}};
    const auto timedStormRows=rows.size();
    const auto timedStormContext=msiDinputSignalEvidenceSnapshot()["contextSelections"].get<uint64_t>();
    const auto timedStormSustained=msiDinputSignalEvidenceSnapshot()["sustainedSelections"].get<uint64_t>();
    for(ULONGLONG tick=1206010;tick<=1506002;tick+=8)record(tick,static_cast<unsigned>(tick),2,6);
    snapshot=msiDinputSignalEvidenceSnapshot();
    check(rows.size()-timedStormRows<=120&&snapshot["contextSelections"].get<uint64_t>()-timedStormContext==60&&
        snapshot["sustainedSelections"].get<uint64_t>()-timedStormSustained==60,
        "five-minute advancing context storm remains within two independent five-second rates");
    sustainedReport["fiveMinuteContextStormRows"]=rows.size()-timedStormRows;
    sustainedReport["fiveMinuteContextStormChanges"]=37500;
    sustainedReport["snapshot"]=snapshot;
    // Independent field changes must each get their own reason bit.
    ++g_msiDinput.bindCount;rows.clear();record(1);
    record(2,2);record(3,2,2);hostEpoch=9;record(4,2,2);g_msiMainSourceRevision=7;record(5,2,2);
    snapshot=msiDinputSignalEvidenceSnapshot();
    check(snapshot["contextSelections"]==4&&snapshot["contextSuppressed"]==0,
        "power mode host revision share independent context quota");
    for(unsigned i=0;i<4;++i)check(snapshot["recentContexts"][i]["contextChangeBits"]==(1u<<i),"context change bits distinguish each source field");
    ++g_msiDinput.bindCount;rows.clear();record(0);
    snapshot=msiDinputSignalEvidenceSnapshot();
    check(snapshot["samples"]==1&&snapshot["recordSelections"]==1&&snapshot["sustainedSelections"]==0&&
        snapshot["contextSuppressed"]==0&&snapshot["sustained"].empty()&&snapshot["recentContexts"].empty()&&
        snapshot["initial"].size()==1,"bind reset clears counters and never exposes previous binding slots");
    record(4999);check(rows.size()==2,"second initial sample still records before deadline");
    record(5000);snapshot=msiDinputSignalEvidenceSnapshot();
    check(rows.size()==3&&snapshot["sustainedSelections"]==1,"zero tick is a valid cadence anchor and overlapping reasons deduplicate");
    for(unsigned i=0;i<65;++i){pad.bLeftTrigger=(i&1)?127:0;record(5000);}
    pad=originalPad;
    const auto maxTick=std::numeric_limits<uint64_t>::max();record(maxTick,2);
    const auto maxRows=rows.size();
    for(unsigned i=0;i<10000;++i)record((i&1)?maxTick:0,3+i);
    check(rows.size()<=maxRows+3,"uint64 clock extreme cannot overflow or manufacture more context tokens");
    // Pure observer path, including deadline/context selections, never allocates.
    {
        auto observerStorage=std::make_unique<Observer>();auto& observer=*observerStorage;Sample signal{};signal.canonicalKnown=true;
        const auto allocationStart=allocationRequests.load();
        Decision selected;unsigned legacySelections=0;
        for(unsigned i=0;i<100000;++i){signal.completedTick=uint64_t(i)*8;signal.power=i;selected=observer.observe(1,0,0,1,signal);legacySelections+=selected.legacyRecordSelected;}
        check(allocationRequests.load()==allocationStart,"all observer selections and history updates use fixed memory");
        check(legacySelections==65&&!selected.legacyRecordSelected,"legacyRecordSelected is exactly original64 plus cap independent of new channels");
        const auto decision=decisionJson(selected);
        check(decision["contextSuppressedWithinBinding"].get<uint64_t>()>99000&&decision["logCountersAreSelectionsNotPersistence"]==true,
            "main decision serializer exposes reasons and suppression counters");
        signal.completedTick+=5000;signal.power++;
        const auto merged=decisionJson(observer.observe(1,0,0,1,signal));
        check(merged["sustainedWitness"]==true&&merged["contextBoundarySelected"]==true&&
            (merged["reasonBits"].get<unsigned>()&(256u|512u|1024u))==(256u|512u|1024u),
            "main wiring helper describes merged periodic and context selection");
    }
    // Actual production adapter controls disk logging; observer histories keep updating.
    ++g_msiDinput.bindCount;rows.clear();virtualDetailedLogs=false;
    for(unsigned i=0;i<65;++i){pad.bLeftTrigger=(i&1)?127:0;record(1000+i);}
    check(rows.size()==65&&rows.back()["diagnosticCapacityReached"]==true&&rows.back()["legacyRecordSelected"]==true,
        "virtual log off preserves original64 plus cap base events");
    pad=originalPad;
    const auto logOffAllocations=allocationRequests.load();
    for(ULONGLONG tick=1072;tick<=301000;tick+=8)record(tick);
    for(unsigned i=0;i<10000;++i)record(301001,i+2);
    check(allocationRequests.load()==logOffAllocations,"virtual log off selects ongoing in-memory evidence without JSON allocations");
    snapshot=msiDinputSignalEvidenceSnapshot();
    check(rows.size()==65&&snapshot["sustainedSelections"]==60&&snapshot["sustained"].size()==32&&
        snapshot["contextSelections"]==4&&snapshot["contextSuppressed"]==9996,
        "virtual log off suppresses sustained and context disk rows while fixed histories continue");
    virtualDetailedLogs=true;record(306001,10001);
    check(rows.size()==66&&rows.back()["sustainedWitness"]==true&&rows.back()["legacyRecordSelected"]==false,
        "virtual log on resumes next sustained witness with explicit new-channel reason");
    record(306002,10002);
    check(rows.size()==67&&rows.back()["contextBoundarySelected"]==true&&rows.back()["legacyRecordSelected"]==false,
        "actual main decisionJson carries independent context reason after log enable");
    sustainedReport["virtualLogOffBaseRows"]=65;
    sustainedReport["virtualLogOffFiveMinuteSustainedSelections"]=60;
    sustainedReport["virtualLogOffContextChanges"]=10000;
    sustainedReport["virtualLogOffAdditionalDiskRows"]=0;
    // Exceptions still consume the same quota; successful logging can resume later.
    ++g_msiDinput.bindCount;rows.clear();loggerThrows=true;
    const auto failedBefore=loggerAttempts;
    for(unsigned i=0;i<65;++i){pad.bLeftTrigger=(i&1)?127:0;record(1000+i);}
    pad=originalPad;
    for(ULONGLONG tick=1072;tick<=601000;tick+=8)record(tick);
    check(loggerAttempts-failedBefore==185&&rows.empty(),"throwing logger has exactly the same bounded selections for ten minutes");
    const auto failedSteady=loggerAttempts;
    for(unsigned i=0;i<100000;++i)record(601000);
    check(loggerAttempts==failedSteady,"throwing logger never creates a same-tick retry loop");
    for(unsigned i=0;i<100000;++i)record(601001,i+2);
    check(loggerAttempts==failedSteady+4,"throwing logger context storm also obeys independent burst");
    snapshot=msiDinputSignalEvidenceSnapshot();
    check(snapshot["recordSelections"]==189&&snapshot["contextSuppressed"]==99996,
        "export counts selections despite logger exceptions without asserting persistence");
    loggerThrows=false;record(606001,100001);
    check(rows.size()==1&&rows.back()["rawAxes"][0]==10000,"logger recovery captures the next due witness");
    check(!std::memcmp(&raw,&originalRaw,sizeof(raw))&&!std::memcmp(&normalized,&originalNormalized,sizeof(normalized))&&
        !std::memcmp(&pad,&originalPad,sizeof(pad)),"diagnostic schedules and failures never modify raw normalized or pad");
    const auto nonMsiSamples=g_msiDinput.triggerEvidence.snapshot().samples;
    const auto nonMsiRows=rows.size();const auto nonMsiAttempts=loggerAttempts;
    knownMsi=false;g_msiDinput.enabled=false;
    for(unsigned i=0;i<100000;++i)actualDisabledPump();
    check(msiDinputSignalEvidenceSnapshot()["status"]=="not-msi"&&
        g_msiDinput.triggerEvidence.snapshot().samples==nonMsiSamples&&rows.size()==nonMsiRows&&loggerAttempts==nonMsiAttempts,
        "actual default disabled pump guard and non-MSI export leave evidence untouched");
    knownMsi=true;
    std::ofstream evidence("sustained-evidence.json");evidence<<sustainedReport.dump(2)<<"\n";
}
