static std::vector<std::string> runtimeProjectionCases;
static bool runtimeBusinessOnly=false;
static json runtimeBusinessTrace=json::array();
static void supplySdkCaps(const ymccCommonHid::Device& device) {
    fakeOs.fault.clear();fakeOs.apiTrace.clear();fakeOs.info=device.info;fakeOs.info.cbSize=sizeof(fakeOs.info);
    fakeOs.info.dwType=RIM_TYPEHID;fakeOs.info.hid.usUsagePage=1;fakeOs.info.hid.usUsage=5;
    fakeOs.caps=device.caps;fakeOs.maxData=device.maxData;
    fakeOs.buttons={};fakeOs.values={};
    for(std::size_t i=0;i<device.buttons.size();++i) {
        const auto& a=device.buttons[i];auto& b=fakeOs.buttons[i];
        b.UsagePage=static_cast<USHORT>(a.page);b.ReportID=static_cast<UCHAR>(a.reportId);
        b.BitField=static_cast<USHORT>(a.bitField);b.LinkCollection=static_cast<USHORT>(a.linkCollection);
        b.LinkUsagePage=static_cast<USHORT>(a.linkUsagePage);b.LinkUsage=static_cast<USHORT>(a.linkUsage);
        b.IsRange=a.isRange;b.IsAbsolute=a.isAbsolute;b.IsAlias=a.isAlias;
        if(a.isRange){b.Range.UsageMin=static_cast<USHORT>(a.usageFirst);b.Range.UsageMax=static_cast<USHORT>(a.usageLast);
            b.Range.DataIndexMin=static_cast<USHORT>(a.dataFirst);b.Range.DataIndexMax=static_cast<USHORT>(a.dataLast);}
        else {b.NotRange.Usage=static_cast<USHORT>(a.usageFirst);b.NotRange.DataIndex=static_cast<USHORT>(a.dataFirst);}
    }
    for(std::size_t i=0;i<device.values.size();++i) {
        const auto& a=device.values[i];auto& b=fakeOs.values[i];
        b.UsagePage=static_cast<USHORT>(a.page);b.ReportID=static_cast<UCHAR>(a.reportId);
        b.BitField=static_cast<USHORT>(a.bitField);b.LinkCollection=static_cast<USHORT>(a.linkCollection);
        b.LinkUsagePage=static_cast<USHORT>(a.linkUsagePage);b.LinkUsage=static_cast<USHORT>(a.linkUsage);
        b.IsRange=a.isRange;b.IsAbsolute=a.isAbsolute;b.IsAlias=a.isAlias;b.HasNull=a.hasNull;
        b.BitSize=static_cast<USHORT>(a.bitSize);b.ReportCount=static_cast<USHORT>(a.reportCount);
        b.LogicalMin=a.logicalMin;b.LogicalMax=a.logicalMax;
        if(a.isRange){b.Range.UsageMin=static_cast<USHORT>(a.usageFirst);b.Range.UsageMax=static_cast<USHORT>(a.usageLast);
            b.Range.DataIndexMin=static_cast<USHORT>(a.dataFirst);b.Range.DataIndexMax=static_cast<USHORT>(a.dataLast);}
        else {b.NotRange.Usage=static_cast<USHORT>(a.usageFirst);b.NotRange.DataIndex=static_cast<USHORT>(a.dataFirst);}
    }
    g_ymccXInputGetCapabilities14=fakeXinputCapabilities;
}
static unsigned eventCount(std::string_view name) {
    return static_cast<unsigned>(std::count_if(logs.begin(),logs.end(),[&](const json& row){return row.at("event").get<std::string>()==name;}));
}
static const json& lastObservation(){for(auto it=logs.rbegin();it!=logs.rend();++it)if(it->at("event")=="gamepad-common-hid-observation")return it->at("data");throw std::runtime_error("required read observation missing");}
static void earlyInspectionCases(const ymccCommonHid::Device& seed,HANDLE handle) {
    const auto path=U2W(*g_axisCalibrationXinputKeys[0]);
    for(const char* stage:{"device-info-read","not-gamepad-collection","device-name-size","device-name-read",
        "source-binding-unproven","xinput-capabilities-unavailable","xinput-subtype-unsupported","preparsed-size"}) {
        supplySdkCaps(seed);fakeOs.fault=stage;fakeOs.tick+=11000;logs.clear();
        const auto result=ymccCommonHid::inspect(handle,path,0);
        RID_DEVICE_INFO expected{};expected.cbSize=sizeof(expected);
        if(std::string_view(stage)!="device-info-read")expected=fakeOs.info;
        if(std::string_view(stage)=="not-gamepad-collection")expected.hid.usUsage=2;
        check(!result.ready&&result.info.cbSize==expected.cbSize&&result.info.dwType==expected.dwType&&
            result.info.hid.dwVendorId==expected.hid.dwVendorId&&result.info.hid.dwProductId==expected.hid.dwProductId&&
            result.info.hid.usUsagePage==expected.hid.usUsagePage&&result.info.hid.usUsage==expected.hid.usUsage,
            "early rejection preserves exact original info/cache shape");
        check(result.preparsed.empty()&&result.buttons.empty()&&result.values.empty()&&!result.maxData&&
            !result.caps.InputReportByteLength,"early rejection preserves original empty parser data");
        if(!runtimeBusinessOnly) {
            check(eventCount("gamepad-common-hid-inspection")==1&&logs.back().at("data").at("stage")==stage,
                "cache shape fix retains exact refusal diagnostic");
            check(logs.back().at("data").at("candidateComparedToSlot")==0&&
                logs.back().at("data").at("selectedSourceMatchProven")==
                    (std::string_view(stage)=="xinput-capabilities-unavailable"||std::string_view(stage)=="xinput-subtype-unsupported"||std::string_view(stage)=="preparsed-size"),
                "diagnostic source proof remains distinct from mere slot comparison");
        }
        runtimeProjectionCases.push_back(std::string("early-inspection/")+stage);
        runtimeBusinessTrace.push_back({{"stage",stage},{"ready",result.ready},{"info",{{"cbSize",result.info.cbSize},{"type",result.info.dwType},
            {"vendor",result.info.hid.dwVendorId},{"product",result.info.hid.dwProductId},{"page",result.info.hid.usUsagePage},{"usage",result.info.hid.usUsage}}},
            {"preparsedBytes",result.preparsed.size()},{"values",result.values.size()},{"buttons",result.buttons.size()},{"maxData",result.maxData},{"apis",fakeOs.apiTrace}});
    }
    ymccCommonHid::invalidate();ymccBackgroundUi::invalidate();
}
static void observationBoundaryCases(const ymccCommonHid::Device& seed,HANDLE handle) {
    using namespace ymccCommonHid;
    supplySdkCaps(seed);invalidate();ymccBackgroundUi::invalidate();logs.clear();fakeOs.tick=1000000;
    g_inputCaptureGamepadSnapshot.inputSource="xinput";g_inputCaptureGamepadSnapshot.commonHidReportSequence=0;
    XINPUT_GAMEPAD original{};original.wButtons=XINPUT_GAMEPAD_B;original.bLeftTrigger=51;original.bRightTrigger=121;
    const auto same=[](const XINPUT_GAMEPAD& a,const XINPUT_GAMEPAD& b){return a.wButtons==b.wButtons&&a.bLeftTrigger==b.bLeftTrigger&&a.bRightTrigger==b.bRightTrigger&&a.sThumbLX==b.sThumbLX&&a.sThumbLY==b.sThumbLY&&a.sThumbRX==b.sThumbRX&&a.sThumbRY==b.sThumbRY;};
    const auto read=[&](bool eligibleSource){std::lock_guard lock(g_gamepadReadMx);XINPUT_GAMEPAD pad=original;CommonControllerHidRead receipt{987,654,true,true,321};const bool used=commonControllerHidRead(pad,receipt,eligibleSource);if(!used){check(same(pad,original)&&receipt.sequence==987,"unadopted observation preserves pad and caller receipt");check(receipt.observedTick==654&&receipt.independentTriggers&&receipt.completeBasic&&receipt.combinedTriggerRaw==321,"unadopted observation preserves every caller receipt field");}return used;};
    const auto queries=fakeOs.apiTrace.size();
    const auto deviceLeaves=std::array<unsigned,4>{fakeOs.rawCalls,fakeOs.hidCalls,fakeOs.cfgCalls,fakeOs.bindingCalls};
    const auto eligibilityJson=[&](unsigned mask,json capture,json priority,json release,json active){
        const auto& facts=lastObservation().at("eligibility");
        check(facts.at("evaluatedMask")==mask&&facts.at("captureEnabled")==capture&&
            facts.at("externalPriorityPermits")==priority&&facts.at("physicalReleasePermits")==release&&
            facts.at("fseActive")==active,"actual read JSON records only original short-circuit evaluations");
    };
    const auto countedRead=[&](bool source,unsigned clocks,unsigned platforms,unsigned registrations){
        const auto c=fakeOs.clockCalls,p=commonHidPlatformQueries,q=registrationQueries,w=windowQueries,o=windowOwnerQueries;
        const bool used=read(source);
        check(fakeOs.clockCalls==c+clocks&&commonHidPlatformQueries==p+platforms,
            "original short-circuit and real FSE cache retain exact clock/platform leaf counts");
        check(registrationQueries==q+registrations&&windowQueries==w+registrations&&windowOwnerQueries==o+registrations,
            "only admitted diagnostic offers query original registration/window/owner leaves once");
        check(std::array<unsigned,4>{fakeOs.rawCalls,fakeOs.hidCalls,fakeOs.cfgCalls,fakeOs.bindingCalls}==deviceLeaves,
            "read observation adds no original device leaf calls");
        return used;
    };
    const auto initialClock=fakeOs.clockCalls,initialPlatform=commonHidPlatformQueries;
    check(!read(false),"unrelated source remains unadopted");
    check(lastObservation().at("reason")=="original-source-not-applicable","read observer reports applicability without device queries");
    eligibilityJson(0,nullptr,nullptr,nullptr,nullptr);
    check(lastObservation().at("contextKnown")==false&&registrationQueries==1&&windowQueries==1&&windowOwnerQueries==1&&
        fakeOs.clockCalls==initialClock+1&&commonHidPlatformQueries==initialPlatform,
        "first unrelated source offers registration with no eligibility/platform query");
    const auto initial=eventCount("gamepad-common-hid-observation");fakeOs.tick+=999;check(!read(true),"waiting retains original input before reason boundary");
    check(eventCount("gamepad-common-hid-observation")==initial,"changed reason suppressed at 999ms");
    ++fakeOs.tick;check(!read(true),"waiting remains non-adopting at boundary");
    check(eventCount("gamepad-common-hid-observation")==initial+1&&lastObservation().at("reason")=="awaiting-qualified-current-hid","changed reason emits at 1000ms");
    fakeOs.tick+=9999;check(!read(true),"same awaiting state preserves input");
    check(eventCount("gamepad-common-hid-observation")==initial+1,"awaiting repeat suppressed at 9999ms");
    ++fakeOs.tick;check(!read(true),"same awaiting state still uses original source");
    check(eventCount("gamepad-common-hid-observation")==initial+2,"awaiting repeat emits at 10000ms");
    eligibilityJson(15,true,true,true,true);
    check(lastObservation().contains("registration"),"awaiting observation carries actual main registration helper");
    const auto initialRegistration=lastObservation().at("registration");
    check(initialRegistration.at("readOk")==true&&initialRegistration.at("status")=="known"&&initialRegistration.at("allThreeAtExpectedSink")==true&&initialRegistration.at("matchingGamepadUsages")==3,"all three gamepad usages registered at expected sink");
    check(initialRegistration.at("gamepadRegistrations").size()==3&&initialRegistration.at("sinkPid")==2488&&initialRegistration.at("sinkThreadId")==4321&&initialRegistration.at("controlsChanged")==false,"registration process/window evidence stays non-authoritative");
    check(registrationQueries==3&&windowQueries==3&&windowOwnerQueries==3,"registration queried at first unrelated source and original 1s/10s awaiting events");
    for(const char* fault:{"dead-sink","wrong-target","no-inputsink","capacity","failure"}) {
        registrationFault=fault;fakeOs.tick+=10000;SetLastError(6789);const auto q0=registrationQueries;
        check(!read(true)&&GetLastError()==6789&&registrationQueries==q0+1,"bounded registration query preserves LastError and original input");
        const auto& registration=lastObservation().at("registration");const bool failed=registrationFault=="capacity"||registrationFault=="failure";
        check(registration.at("readOk")==!failed&&registration.at("status")== (failed?"unknown":"known")&&registration.at("allThreeAtExpectedSink")==false,"registration unknown and invalid routes never imply input authority");
        if(failed)check(registration.at("returnedCount").is_null()&&registration.at("gamepadRegistrations").empty()&&registration.at("readError")== (registrationFault=="capacity"?ERROR_INSUFFICIENT_BUFFER:ERROR_ACCESS_DENIED)&&registration.at("requiredOrReturnedCount")== (registrationFault=="capacity"?33:32),"API failure/capacity error retains explicit unknown fields");
        if(registrationFault=="dead-sink")check(registration.at("sinkLive")==false&&registration.at("sinkPid")==0&&registration.at("sinkThreadId")==0,"dead sink cannot acquire ownership evidence");
        if(registrationFault=="wrong-target")check(registration.at("gamepadRegistrations").at(0).at("atMainWindow")==true&&registration.at("matchingGamepadUsages")==0,"main target is not expected sink");
    }
    registrationFault.clear();SetLastError(6790);const auto direct=gamepadRawInputRegistrationEvidence();
    check(GetLastError()==6790&&direct.at("allThreeAtExpectedSink")==true,"actual complete helper independently restores LastError");
    const auto afterAwaitingQueries=registrationQueries;
    runtimeProjectionCases.push_back("observation/1s-change-10s-wait");
    fakeOs.tick+=1000;check(!read(false),"unrelated source transition remains unadopted");
    const auto count=eventCount("gamepad-common-hid-observation"),clock=fakeOs.clockCalls;
    fakeOs.tick+=10000;check(!read(false),"same unrelated source retains input");
    check(eventCount("gamepad-common-hid-observation")==count&&fakeOs.clockCalls==clock,"unallowed steady state has no periodic clock or log");
    Epoch e;check(context(e),"selected steady control retains actual context");
    original.wButtons=0;lane.observe(e,{{XINPUT_GAMEPAD_A},++sequence,fakeOs.tick,false,true,32768});
    fakeOs.tick+=1000;check(read(true),"selected observation retains admitted production sample");
    eligibilityJson(15,true,true,true,true);
    const auto selectedClock=fakeOs.clockCalls,selectedLogs=eventCount("gamepad-common-hid-observation"),selectedPlatform=commonHidPlatformQueries;
    for(unsigned i=0;i<20;++i)check(read(true),"steady selected read stays admitted");
    check(fakeOs.clockCalls==selectedClock+20&&eventCount("gamepad-common-hid-observation")==selectedLogs,"steady selected path retains only original fseActive clock and adds no observation clock");
    check(registrationQueries==afterAwaitingQueries+2,"selected and unrelated-source transitions add two registration queries; steady repeats add zero");
    check(commonHidPlatformQueries==selectedPlatform,"same-tick selected repeats use the real FSE cache without platform reads");
    lane.clear();g_xinputSlotUnknownMask=1;fakeOs.tick+=1000;
    check(!countedRead(true,1,0,1)&&lastObservation().at("reason")=="physical-context-unavailable","unknown context remains unadopted");
    eligibilityJson(0,nullptr,nullptr,nullptr,nullptr);g_xinputSlotUnknownMask=0;
    g_realStickTest.enabled=false;fakeOs.tick+=1000;
    check(!countedRead(true,1,0,1)&&lastObservation().at("reason")=="session-inactive","capture disabled short-circuits all later leaves");
    eligibilityJson(1,false,nullptr,nullptr,nullptr);
    g_realStickTest.enabled=true;g_externalPriorityVirtualSuspended=true;fakeOs.tick+=1000;
    check(!countedRead(true,1,0,1),"external priority short-circuits release and FSE");
    eligibilityJson(3,true,false,nullptr,nullptr);
    g_externalPriorityVirtualSuspended=false;releasing=true;fakeOs.tick+=1000;
    check(!countedRead(true,1,0,1),"physical release short-circuits FSE");
    eligibilityJson(7,true,true,false,nullptr);
    releasing=false;fse=false;fakeOs.tick+=1000;
    check(!countedRead(true,2,1,1),"real FSE refresh reports inactive session");
    eligibilityJson(15,true,true,true,false);
    g_realStickTest.enabled=false;fakeOs.tick+=1000;
    check(!countedRead(true,1,0,1),"same inactive reason admits changed capture mask");
    eligibilityJson(1,false,nullptr,nullptr,nullptr);
    const auto maskLogs=eventCount("gamepad-common-hid-observation");
    g_realStickTest.enabled=true;fakeOs.tick+=999;
    check(!countedRead(true,2,1,0)&&eventCount("gamepad-common-hid-observation")==maskLogs,
        "same SessionInactive mask1 to15 is suppressed at999ms");
    ++fakeOs.tick;
    check(!countedRead(true,2,0,1)&&eventCount("gamepad-common-hid-observation")==maskLogs+1,
        "same SessionInactive mask1 to15 is admitted at1000ms using cached FSE");
    eligibilityJson(15,true,true,true,false);
    for(unsigned i=0;i<20;++i)check(!countedRead(true,1,0,0),"stable inactive mask adds no diagnostic clock or query");
    check(eventCount("gamepad-common-hid-observation")==maskLogs+1,"stable inactive masks never publish periodic diagnostics");
    fse=true;fakeOs.tick+=998;
    check(!countedRead(true,1,0,0),"FSE remains cached false through999ms since actual platform read");
    ++fakeOs.tick;
    check(!countedRead(true,2,1,0),"FSE refreshes at1000ms while changed diagnostic is still at999ms");
    ++fakeOs.tick;
    check(!countedRead(true,2,0,1),"changed FSE value admits only at original diagnostic1000ms gate");
    eligibilityJson(15,true,true,true,true);
    lane.observe(e,{{XINPUT_GAMEPAD_A},++sequence,fakeOs.tick,false,true,32768});fakeOs.tick+=1000;
    check(countedRead(true,2,1,1),"restore original selected baseline before original throw control");
    runtimeProjectionCases.push_back("observation/steady-selected-unallowed");
    const auto throwQueries=registrationQueries,throwWindows=windowQueries,throwOwners=windowOwnerQueries;
    lane.clear();fakeOs.tick+=1000;throwDiagnosticEvent="gamepad-common-hid-observation";diagnosticThrowAttempts=0;SetLastError(6789);
    check(!read(true)&&GetLastError()==6789&&diagnosticThrowAttempts==1,"read diagnostic logger throw preserves original LastError and input");
    fakeOs.tick+=9999;check(!read(true)&&diagnosticThrowAttempts==1,"failed logger attempt still bounds waiting repeat");
    ++fakeOs.tick;check(!read(true)&&diagnosticThrowAttempts==2,"failed logger retries only at 10s");
    check(registrationQueries==throwQueries+2&&windowQueries==throwWindows+2&&windowOwnerQueries==throwOwners+2,
        "failed log offers still make only their two admitted bounded registration queries");
    const auto droppedLogs=eventCount("gamepad-common-hid-observation");
    g_realStickTest.enabled=false;fakeOs.tick+=1000;SetLastError(6789);
    check(!countedRead(true,1,0,1)&&GetLastError()==6789&&diagnosticThrowAttempts==3,
        "throwing inactive mask1 offer preserves input receipt and LastError");
    check(!countedRead(true,0,0,0)&&diagnosticThrowAttempts==3,"failed inactive mask baseline consumes same-tick offers");
    g_realStickTest.enabled=true;fse=false;fakeOs.tick+=999;SetLastError(6789);
    check(!countedRead(true,2,1,0)&&GetLastError()==6789&&diagnosticThrowAttempts==3,
        "failed baseline still suppresses same-reason mask change at999ms");
    ++fakeOs.tick;SetLastError(6789);
    check(!countedRead(true,2,0,1)&&GetLastError()==6789&&diagnosticThrowAttempts==4,
        "failed baseline admits changed inactive mask only at1000ms");
    check(!countedRead(true,1,0,0)&&diagnosticThrowAttempts==4&&eventCount("gamepad-common-hid-observation")==droppedLogs,
        "throw-dropped logs consume the mask baseline without retry storm");
    throwDiagnosticEvent.clear();
    check(!countedRead(true,1,0,0)&&eventCount("gamepad-common-hid-observation")==droppedLogs,
        "logger recovery does not replay a consumed inactive offer");
    fse=true;fakeOs.tick+=1000;check(!countedRead(true,2,1,1),"restore original awaiting state after throw boundaries");
    runtimeProjectionCases.push_back("observation/throw-last-error");
    const auto beforeDisabledRegistration=registrationQueries,beforeDisabledWindows=windowQueries,beforeDisabledOwners=windowOwnerQueries;
    const auto disabledAttempts=newDiagnosticAttempts;
    diagnosticEnabled=false;fakeOs.tick+=10000;const auto disabledLogs=eventCount("gamepad-common-hid-observation"),disabledClock=fakeOs.clockCalls;
    check(!read(true)&&eventCount("gamepad-common-hid-observation")==disabledLogs&&fakeOs.clockCalls==disabledClock+1,"domain off retains original fseActive clock without observation clock or log");
    g_realStickTest.enabled=false;check(!countedRead(true,0,0,0),"domain OFF capture mask adds zero clocks or queries");
    g_realStickTest.enabled=true;g_externalPriorityVirtualSuspended=true;
    check(!countedRead(true,0,0,0),"domain OFF priority mask adds zero clocks or queries");
    g_externalPriorityVirtualSuspended=false;releasing=true;
    check(!countedRead(true,0,0,0),"domain OFF release mask adds zero clocks or queries");
    releasing=false;fse=false;fakeOs.tick+=1000;
    check(!countedRead(true,1,1,0),"domain OFF FSE mask retains only original clock/platform leaf");
    g_xinputSlotUnknownMask=1;check(!countedRead(true,0,0,0),"domain OFF context mask adds zero clocks or queries");
    g_xinputSlotUnknownMask=0;check(!countedRead(false,0,0,0),"domain OFF unrelated source adds zero clocks or queries");
    g_realStickTest.enabled=false;check(!countedRead(true,0,0,0),"domain OFF leaves final capture mask unoffered");
    check(registrationQueries==beforeDisabledRegistration,"domain disabled adds zero registration queries");
    check(windowQueries==beforeDisabledWindows&&windowOwnerQueries==beforeDisabledOwners&&newDiagnosticAttempts==disabledAttempts&&
        eventCount("gamepad-common-hid-observation")==disabledLogs,"domain OFF changes add zero window/owner queries or log offers");
    diagnosticEnabled=true;check(fakeOs.apiTrace.size()==queries,"all observer states add zero original device API calls");
    check(!countedRead(true,1,0,1)&&lastObservation().at("reason")=="session-inactive",
        "domain OFF cannot consume the new mask baseline before diagnostics reenable");
    eligibilityJson(1,false,nullptr,nullptr,nullptr);
    g_realStickTest.enabled=true;fse=true;fakeOs.tick+=1000;
    check(!countedRead(true,2,1,1),"restore original eligibility after domain OFF boundaries");
    const auto savedCounts=pipelineObservation();const auto pipeError=GetLastError();
    diagnosticEnabled=false;observePipeline(PipelineStage::RawMessage);observePipeline(PipelineStage::KeyboardHeader);
    check(pipelineObservation()==savedCounts&&GetLastError()==pipeError,"disabled raw/header observations preserve all counters and LastError");diagnosticEnabled=true;
    for(auto stage:{PipelineStage::RawMessage,PipelineStage::SinkMessage,PipelineStage::MainMessage,PipelineStage::KeyboardHeader,PipelineStage::MouseHeader,PipelineStage::HidHeader})observePipeline(stage);
    const auto changedCounts=pipelineObservation();
    for(const char* key:{"rawMessages","sinkMessages","mainMessages","keyboardHeaders","mouseHeaders","hidHeaders"})check(changedCounts.at(key).get<std::uint64_t>()==savedCounts.at(key).get<std::uint64_t>()+1,"new counters preserve independent old/new fixed atomic observations");
    check(GetLastError()==pipeError&&changedCounts.at("snapshotAtomic")==false,"pipeline observer preserves LastError and nontransactional scope");
    runtimeProjectionCases.push_back("observation/domain-off-api-count");
    // Original capture retries at 2s; observation emits only on actual retry.
    invalidate();fakeOs.fault="device-info-read";logs.clear();fakeOs.tick=2000000;
    constexpr auto offset=offsetof(RAWINPUT,data)+offsetof(RAWHID,bRawData);
    std::vector<BYTE> memory(offset+15,0);auto* packet=reinterpret_cast<RAWINPUT*>(memory.data());
    packet->header.dwType=RIM_TYPEHID;packet->header.dwSize=static_cast<DWORD>(memory.size());packet->header.hDevice=handle;
    packet->data.hid.dwSizeHid=15;packet->data.hid.dwCount=1;
    const auto beforeCalls=fakeOs.rawCalls,beforeEval=evaluations;const auto beforeSequence=sequence;
    check(!commonControllerHidCapture(packet,static_cast<UINT>(memory.size())),"initial failed inspect remains unclaimed");
    check(fakeOs.rawCalls==beforeCalls+1&&eventCount("gamepad-common-hid-inspection")==1,"initial failure performs one original info API and one diagnostic");
    fakeOs.tick+=1999;check(!commonControllerHidCapture(packet,static_cast<UINT>(memory.size()))&&fakeOs.rawCalls==beforeCalls+1,"no new inspect before original 2s retry");
    ++fakeOs.tick;check(!commonControllerHidCapture(packet,static_cast<UINT>(memory.size()))&&fakeOs.rawCalls==beforeCalls+2,"original info API retries at exactly 2s");
    for(unsigned i=2;i<5;++i){fakeOs.tick=2000000+2000*i;check(!commonControllerHidCapture(packet,static_cast<UINT>(memory.size())),"original rejected retry remains unclaimed");}
    check(eventCount("gamepad-common-hid-inspection")==1,"same handle/stage logs suppressed through 8s actual retries");
    fakeOs.tick=2009999;check(!commonControllerHidCapture(packet,static_cast<UINT>(memory.size()))&&fakeOs.rawCalls==beforeCalls+5,"9999ms is before next original retry");
    fakeOs.tick=2010000;check(!commonControllerHidCapture(packet,static_cast<UINT>(memory.size()))&&eventCount("gamepad-common-hid-inspection")==2,"same handle/stage diagnostic repeats on actual 10s retry");
    check(evaluations==beforeEval&&sequence==beforeSequence,"refusal diagnostics add no semantic callbacks or sample sequence");
    runtimeProjectionCases.push_back("inspection/2s-retry-10s-diagnostic");
}
static void commonInspectRuntimeCases(const ymccCommonHid::Device& original,HANDLE handle) {
    using namespace ymccCommonHid;
    const auto selectedPath=U2W(*g_axisCalibrationXinputKeys[0]);
    for(bool rog:{false,true}) {
        auto seed=original;
        if(rog) {
            seed.info.hid.dwVendorId=0x0b05;seed.info.hid.dwProductId=0x1b4c;
            seed.caps.InputReportByteLength=16;seed.caps.NumberInputDataIndices=22;seed.maxData=22;
            seed.buttons[0].usageLast=16;seed.buttons[0].dataLast=20;
            seed.values.back().dataFirst=seed.values.back().dataLast=21;
        }
        supplySdkCaps(seed);invalidate();ymccBackgroundUi::invalidate();consumed.clear();
        g_inputCaptureGamepadSnapshot.inputSource="xinput";g_inputCaptureGamepadSnapshot.commonHidReportSequence=0;
        const unsigned hat=rog?21:15;
        testReports[0]={{0,32768,false},{1,32768,false},{2,32768,false},{3,32768,false},{4,32768,false},{hat,0,false},{5,1,true}};
        testReports[1]={{0,32768,false},{1,32768,false},{2,32768,false},{3,32768,false},{4,32768,false},{hat,0,false}};
        constexpr auto offset=offsetof(RAWINPUT,data)+offsetof(RAWHID,bRawData);
        const UINT reportBytes=seed.caps.InputReportByteLength;
        std::vector<BYTE> memory(offset+2*reportBytes,0);auto* raw=reinterpret_cast<RAWINPUT*>(memory.data());
        raw->header.dwType=RIM_TYPEHID;raw->header.dwSize=static_cast<DWORD>(memory.size());raw->header.hDevice=handle;
        raw->data.hid.dwSizeHid=reportBytes;raw->data.hid.dwCount=2;raw->data.hid.bRawData[reportBytes+1]=1;
        const auto before=evaluations,bindingBefore=fakeOs.bindingCalls;
        check(commonControllerHidCapture(raw,static_cast<UINT>(memory.size())),"actual inspect admits SDK-shaped collection through capture");
        check(fakeOs.bindingCalls==bindingBefore+1&&devices.at(handle).ready,"capture executes real inspect and exact selected-source port once");
        check(evaluations==before+2&&consumed.size()==2,"new inspect projection dispatches each edge once");
        check(consumed[0].wButtons==XINPUT_GAMEPAD_A&&!consumed[1].wButtons,"inspected Win5/ROG press and release preserved");
        XINPUT_GAMEPAD native{};native.bLeftTrigger=47;native.bRightTrigger=93;CommonControllerHidRead receipt;
        check(commonControllerHidRead(native,receipt,true)&&receipt.sequence==sequence&&receipt.completeBasic&&!receipt.independentTriggers,
            "actual main receipt ABI consumes production lane and complete partial-trigger metadata");
        check(native.bLeftTrigger==47&&native.bRightTrigger==93,"original native triggers survive common runtime read");
        const std::vector<std::string> expectedApi={"info","name-size","name-read","source-binding","xinput-capabilities","preparsed-size","preparsed-read","caps","button-caps","value-caps","max-data","data","data"};
        check(fakeOs.apiTrace==expectedApi,"capture/read retains exact original OS API order and query counts");
        runtimeBusinessTrace.push_back({{"case",rog?"rog16":"win5-15"},{"ready",devices.at(handle).ready},{"apis",fakeOs.apiTrace},
            {"press",consumed[0].wButtons},{"release",consumed[1].wButtons},{"lt",native.bLeftTrigger},{"rt",native.bRightTrigger},
            {"completeBasic",receipt.completeBasic},{"independentTriggers",receipt.independentTriggers},{"sequence",receipt.sequence}});
        runtimeProjectionCases.push_back(rog?"rog16-inspect-capture-read":"win5-15-inspect-capture-read");
    }
    // String assertions deliberately compile against the unmodified old runtime.
    for(const char* stage:{"device-info-read","not-gamepad-collection","device-name-size","device-name-read",
        "source-binding-unproven","xinput-capabilities-unavailable","xinput-subtype-unsupported","preparsed-size",
        "preparsed-read","hid-caps-read","hid-caps-bounds","button-caps-read","value-caps-read","descriptor-unsupported","data-capacity"}) {
        supplySdkCaps(original);fakeOs.fault=stage;logs.clear();fakeOs.tick+=11000;
        const auto result=inspect(handle,selectedPath,0);
        check(!result.ready,"actual inspect refusal preserves original ready=false");
        runtimeBusinessTrace.push_back({{"case",stage},{"ready",result.ready},{"apis",fakeOs.apiTrace}});
        if(!runtimeBusinessOnly) {
            check(eventCount("gamepad-common-hid-inspection")==1,"required inspection diagnostic exists on actual refusal");
            const auto& diagnostic=logs.back().at("data");
            check(diagnostic.at("stage")==stage,"inspection diagnostic names exact refusing stage");
        }
        runtimeProjectionCases.push_back(std::string("inspection/")+stage);
    }
    if(runtimeBusinessOnly){invalidate();ymccBackgroundUi::invalidate();return;}
    supplySdkCaps(original);diagnosticEnabled=false;fakeOs.fault="device-info-read";logs.clear();
    check(!inspect(handle,selectedPath,0).ready&&logs.empty(),"disabled diagnostics preserve rejection without output");
    diagnosticEnabled=true;runtimeProjectionCases.push_back("inspection/domain-off");
    throwDiagnosticEvent="gamepad-common-hid-inspection";diagnosticThrowAttempts=0;fakeOs.tick+=11000;
    SetLastError(6789);
    check(!inspect(handle,selectedPath,0).ready&&GetLastError()==6789,"diagnostic exception cannot escape inspect or change admission or LastError");
    check(diagnosticThrowAttempts==1,"refusal diagnostic exception is exercised");
    throwDiagnosticEvent.clear();runtimeProjectionCases.push_back("inspection/diagnostic-throw");
    observationBoundaryCases(original,handle);
    invalidate();ymccBackgroundUi::invalidate();
    check(devices.empty()&&!boundDevice&&!lane.hasSample(),"projection returns original source cache and lane");
    check(fakeOs.cfgCalls==0,"precise inspect source-binding port executes without CFG IO");
}

// Explicit original-consumer projection for a physical read with virtual output OFF.
static void commonOffFseCases(const ymccCommonHid::Device& original,HANDLE handle) {
    using namespace ymccCommonHid;
    auto seed=original;
    seed.info.hid.dwVendorId=0x0b05;seed.info.hid.dwProductId=0x1b4c;
    seed.caps.InputReportByteLength=16;seed.caps.NumberInputDataIndices=22;seed.maxData=22;
    seed.buttons[0].usageLast=16;seed.buttons[0].dataLast=20;
    seed.values.back().dataFirst=seed.values.back().dataLast=21;
    testReports[0]={{0,32768,false},{1,32768,false},{2,32768,false},{3,32768,false},{4,32768,false},{21,0,false},{5,1,true}};
    testReports[1]={{0,32768,false},{1,32768,false},{2,32768,false},{3,32768,false},{4,32768,false},{21,0,false}};
    constexpr auto offset=offsetof(RAWINPUT,data)+offsetof(RAWHID,bRawData);
    std::vector<BYTE> memory(offset+32,0);auto* raw=reinterpret_cast<RAWINPUT*>(memory.data());
    raw->header.dwType=RIM_TYPEHID;raw->header.dwSize=static_cast<DWORD>(memory.size());raw->header.hDevice=handle;
    raw->data.hid.dwSizeHid=16;raw->data.hid.dwCount=2;raw->data.hid.bRawData[17]=1;
    const auto prepare=[&]{
        supplySdkCaps(seed);invalidate();ymccBackgroundUi::invalidate();consumed.clear();fakeOs.tick+=11000;
        g_inputCaptureGamepadSnapshot.inputSource="xinput";g_inputCaptureGamepadSnapshot.commonHidReportSequence=0;
        g_xinputSlotUnknownMask=0;g_externalPriorityVirtualSuspended=false;releasing=false;
        g_realStickTest.enabled=true;g_realStickTest.virtualTargetEnabled=false;enabled=false;fse=true;
    };
    prepare();const auto before=evaluations;const auto sequenceBefore=sequence;
    check(commonControllerHidCapture(raw,static_cast<UINT>(memory.size())),"OFF physical source can enter actual qualified capture");
    check(evaluations==before+2&&consumed.size()==2&&consumed[0].wButtons==XINPUT_GAMEPAD_A&&!consumed[1].wButtons,
        "ROG16 OFF press/release reaches each original evaluator exactly once");
    check(sequence==sequenceBefore+2&&!g_realStickTest.virtualTargetEnabled&&!enabled,"physical reading cannot enable virtual output or overlay");
    XINPUT_GAMEPAD native{};native.bLeftTrigger=47;native.bRightTrigger=93;CommonControllerHidRead receipt;
    check(commonControllerHidRead(native,receipt,true)&&receipt.sequence==sequence&&!receipt.independentTriggers&&native.bLeftTrigger==47&&native.bRightTrigger==93,
        "OFF physical reading preserves independent native triggers and actual report sequence");
    runtimeProjectionCases.push_back("off-fse/rog16-press-release");
    prepare();g_realStickTest.virtualTargetEnabled=true;
    raw->data.hid.dwCount=1;raw->header.dwSize=static_cast<DWORD>(offset+16);
    check(commonControllerHidCapture(raw,static_cast<UINT>(offset+16)),"ON arms original physical held report");
    const auto heldSequence=sequence;const auto heldEvaluations=evaluations;
    g_realStickTest.virtualTargetEnabled=false;native={};
    check(commonControllerHidRead(native,receipt,true)&&native.wButtons==XINPUT_GAMEPAD_A&&receipt.sequence==heldSequence,
        "ON to OFF retains physical held state without synthesizing a packet");
    check(evaluations==heldEvaluations&&sequence==heldSequence,"reading held cache cannot replay semantic callbacks or advance hardware sequence");
    raw->data.hid.bRawData[1]=1;
    check(commonControllerHidCapture(raw,static_cast<UINT>(offset+16))&&sequence==heldSequence+1&&consumed.back().wButtons==0,
        "fresh OFF release closes the original held input");
    runtimeProjectionCases.push_back("off-fse/target-toggle-held-no-replay");
    raw->data.hid.bRawData[1]=0;raw->data.hid.dwCount=2;raw->header.dwSize=static_cast<DWORD>(memory.size());
    for(unsigned refusal=0;refusal<5;++refusal){
        prepare();
        if(refusal==0)g_externalPriorityVirtualSuspended=true;
        else if(refusal==1)releasing=true;
        else if(refusal==2)fse=false;
        else if(refusal==3)g_realStickTest.enabled=false;
        else g_xinputSlotUnknownMask=1;
        const auto count=evaluations;const auto seq=sequence;
        check(!commonControllerHidCapture(raw,static_cast<UINT>(memory.size()))&&!lane.hasSample(),"original authority boundary still refuses OFF capture");
        check(evaluations==count&&sequence==seq,"refusal cannot dispatch a report or invent a sequence");
        runtimeProjectionCases.push_back(std::string("off-fse/refused/")+std::array<const char*,5>{"external-priority","physical-return","not-fse","capture-disabled","unknown-identity"}[refusal]);
    }
    prepare();invalidateOnEvaluation=true;const auto prior=evaluations;
    check(commonControllerHidCapture(raw,static_cast<UINT>(memory.size()))&&evaluations==prior+1&&!lane.hasSample(),
        "registration revocation retires old OFF batch suffix without replay");
    runtimeProjectionCases.push_back("off-fse/registration-retires-batch");
    prepare();parserFails=true;const auto failedSequence=sequence;
    check(commonControllerHidCapture(raw,static_cast<UINT>(memory.size()))&&!lane.hasSample()&&sequence==failedSequence,
        "OFF parser failure cannot retain held physical input");parserFails=false;
    native={};check(!commonControllerHidRead(native,receipt,true),"parser failure remains unselected");
    fakeOs.tick+=11000;check(!commonControllerHidRead(native,receipt,true),"waiting permits only bounded diagnostic publication");
    check(lastObservation().at("pipeline").at("parseRejected").get<unsigned>()>=2&&
        lastObservation().at("pipeline").at("reportsObserved").get<unsigned>()>=3&&
        !lastObservation().at("pipeline").at("snapshotAtomic").get<bool>(),"read observation distinguishes parse rejection from observed packets without claiming atomic coverage");
    runtimeProjectionCases.push_back("off-fse/parser-rejection-observed");
    prepare();diagnosticEnabled=false;const auto logsBefore=eventCount("gamepad-common-hid-observation");
    SetLastError(6789);
    check(commonControllerHidCapture(raw,static_cast<UINT>(memory.size()))&&GetLastError()==6789,
        "disabled diagnostics preserve original capture and LastError");
    check(eventCount("gamepad-common-hid-observation")==logsBefore,"disabled diagnostics do not publish periodic observations");
    const std::vector<std::string> expectedApi={"info","name-size","name-read","source-binding","xinput-capabilities","preparsed-size","preparsed-read","caps","button-caps","value-caps","max-data","data","data"};
    check(fakeOs.apiTrace==expectedApi,"OFF diagnostics add no device reads or eligibility queries to original capture");
    diagnosticEnabled=true;runtimeProjectionCases.push_back("off-fse/domain-off-original-api-order");

    // Win5 152424: slot returns before a new usable Raw Input report arrives.
    prepare();supplySdkCaps(original);
    testReports[0]={{0,32768,false},{1,32768,false},{2,32768,false},{3,32768,false},{4,32768,false},{15,0,false},{5,1,true}};
    testReports[1]={{0,32768,false},{1,32768,false},{2,32768,false},{3,32768,false},{4,32768,false},{15,0,false}};
    memory.resize(offset+15);raw=reinterpret_cast<RAWINPUT*>(memory.data());
    raw->header.dwSize=static_cast<DWORD>(memory.size());raw->data.hid.dwSizeHid=15;
    raw->data.hid.dwCount=1;raw->data.hid.bRawData[1]=0;
    const auto captureWin5=[&]{return commonControllerHidCapture(raw,static_cast<UINT>(memory.size()));};
    check(captureWin5()&&lane.selected()&&consumed.size()==1&&consumed.back().wButtons==XINPUT_GAMEPAD_A,
        "Win5 OFF starts with an actual HID press before the refresh");
    const auto preRefreshSequence=sequence;
    const auto preRefreshEvaluations=evaluations;
    fakeOs.tick+=100;
    ++g_xinputTopologyEpoch;invalidate();ymccBackgroundUi::invalidate();
    native={};receipt={};
    Epoch refreshed;
    check(!context(refreshed),"pre-refresh XInput snapshot cannot authenticate the new topology");
    g_inputCaptureGamepadSnapshot.inputSource="xinput";
    g_inputCaptureGamepadSnapshot.commonHidReportSequence=0;
    ++g_inputCaptureGamepadSnapshot.readTopologyGeneration;
    check(context(refreshed)&&refreshed.slot==0&&refreshed.topology==g_xinputTopologyEpoch.load(),
        "the original reader now supplies a current same-slot physical context");
    check(!commonControllerHidRead(native,receipt,true)&&!native.wButtons&&!receipt.sequence&&!lane.hasSample(),
        "same slot after topology change cannot replay its old HID press");
    native.wButtons=XINPUT_GAMEPAD_B;
    check(!commonControllerHidRead(native,receipt,true)&&native.wButtons==XINPUT_GAMEPAD_B&&!receipt.sequence,
        "a genuine original XInput state passes through while waiting for new HID");
    fakeOs.tick+=10;fakeMessageAgeMs=20;
    const auto bindsBefore=fakeOs.bindingCalls;
    const auto ageBefore=pipelineCounts[static_cast<unsigned>(PipelineStage::QueueOrLengthRejected)].load();
    const bool oldAdmitted=captureWin5();
    check(fakeOs.bindingCalls==bindsBefore+1&&devices.at(handle).ready,
        "reused handle is inspected in the new authenticated context");
    check(!oldAdmitted&&pipelineCounts[static_cast<unsigned>(PipelineStage::QueueOrLengthRejected)].load()==ageBefore+1&&
        !boundDevice&&!lane.hasSample()&&
        sequence==preRefreshSequence&&evaluations==preRefreshEvaluations,
        "a queued pre-refresh packet on the reused handle is not admitted even after inspection");
    fakeMessageAgeMs=0;
    ++fakeOs.tick;
    check(captureWin5()&&boundDevice==handle&&lane.selected()&&sequence==preRefreshSequence+1&&
        evaluations==preRefreshEvaluations+1&&consumed.size()==2&&consumed.back().wButtons==XINPUT_GAMEPAD_A,
        "a fresh qualified Win5 report on the reused handle establishes the new epoch");
    ++fakeOs.tick;raw->data.hid.bRawData[1]=1;
    check(captureWin5()&&sequence==preRefreshSequence+2&&evaluations==preRefreshEvaluations+2&&
        consumed.size()==3&&!consumed.back().wButtons&&!g_realStickTest.virtualTargetEnabled,
        "fresh release completes the new Win5 press without creating virtual output");
    runtimeProjectionCases.push_back("off-fse/win5-refresh-gap-and-fresh-rebind");
    invalidate();ymccBackgroundUi::invalidate();g_realStickTest.virtualTargetEnabled=true;enabled=true;
}

#if defined(YMCC_COMMON_HID_SOURCE_BINDING_FIXTURE)
static void resetSourceBindingModel() {
    cfgModel={};
    const std::wstring selected=L"\\\\?\\hid#vid_0b05&pid_1b4c#fixture#{ec87f1e3-c13b-4100-b5f7-8b84d54260cb}";
    fakeOs.rawPath=L"\\\\?\\hid#vid_0b05&pid_1b4c#fixture#{4d1e55b2-f16f-11cf-88cb-001111000030}";
    const std::wstring instance=L"HID\\VID_0B05&PID_1B4C&MI_05&IG_00\\8&FIXTURE&0&0000";
    g_axisCalibrationXinputKeys[0]=std::make_shared<const std::string>(W2U(selected));
    cfgModel.interfaces={{selected,instance,11},{fakeOs.rawPath,instance,11}};
    cfgModel.nodes.emplace(11,CommonCfgNode{instance,L"HidUsb",0});
}
static void commonSourceBindingCases(const ymccCommonHid::Device& original,HANDLE handle) {
    using namespace ymccCommonHid;
    auto rog=original;
    rog.info.hid.dwVendorId=0x0b05;rog.info.hid.dwProductId=0x1b4c;
    rog.caps.InputReportByteLength=16;rog.caps.NumberInputDataIndices=22;rog.maxData=22;
    rog.buttons[0].usageLast=16;rog.buttons[0].dataLast=20;
    rog.values.back().dataFirst=rog.values.back().dataLast=21;
    supplySdkCaps(rog);resetSourceBindingModel();invalidate();ymccBackgroundUi::invalidate();consumed.clear();
    const auto selected=U2W(*g_axisCalibrationXinputKeys[0]);
    const auto first=inspect(handle,selected,0);
    if(!first.ready)std::cerr<<"SOURCE_BINDING_NECESSARY_FAILURE "<<json{{"case","source-binding/same-devnode-no-xusb"},
        {"stage",inspectionStageName(first.inspectionStage)},{"cfgTrace",cfgModel.trace},{"fakeBindingCalls",fakeOs.bindingCalls}}.dump()<<'\n';
    check(first.ready,"exact same devnode without xusb must reach actual inspect");
    check(cfgModel.trace==std::vector<std::string>{"interface-instance","locate","interface-instance","locate"},
        "exact devnode uses both original interfaceNode calls without service or ancestor bypass");
    check(fakeOs.bindingCalls==0,"source-binding variant never uses substituted boolean binding port");
    runtimeProjectionCases.push_back("source-binding/same-devnode-no-xusb");

    const auto bindingCase=[&](const char* name,auto configure,bool expected) {
        resetSourceBindingModel();configure();
        const bool actual=sameXusbSource(fakeOs.rawPath,U2W(*g_axisCalibrationXinputKeys[0]));
        check(actual==expected,name);check(!cfgModel.trace.empty(),"real original CFG calls observed");
        runtimeProjectionCases.push_back(name);
    };
    bindingCase("source-binding/case-insensitive-full-id",[]{
        cfgModel.interfaces[1].instance=L"hid\\vid_0b05&pid_1b4c&mi_05&ig_00\\8&fixture&0&0000";
    },true);
    bindingCase("source-binding/same-node-different-id",[]{cfgModel.interfaces[1].instance+=L"-other";},false);
    bindingCase("source-binding/different-node-same-id",[]{
        cfgModel.interfaces[1].node=12;cfgModel.nodes.emplace(12,CommonCfgNode{cfgModel.interfaces[1].instance,L"HidUsb",0});
    },false);
    bindingCase("source-binding/same-vidpid-different-node-id",[]{
        cfgModel.interfaces[1].node=12;cfgModel.interfaces[1].instance+=L"-other";
        cfgModel.nodes.emplace(12,CommonCfgNode{cfgModel.interfaces[1].instance,L"HidUsb",0});
    },false);
    bindingCase("source-binding/shared-hub-is-not-source",[]{
        cfgModel.interfaces[1].node=12;cfgModel.interfaces[1].instance+=L"-other";
        cfgModel.nodes.at(11).parent=100;
        cfgModel.nodes.emplace(12,CommonCfgNode{cfgModel.interfaces[1].instance,L"HidUsb",100});
        cfgModel.nodes.emplace(100,CommonCfgNode{L"USB\\COMMON_HUB",L"USBHUB3",0});
    },false);
    for(unsigned side=0;side<2;++side){
        bindingCase(side?"source-binding/raw-interface-query-failed":"source-binding/selected-interface-query-failed",[side]{
            cfgModel.interfaces[side].propertyResult=CR_NO_SUCH_DEVNODE;
        },false);
        bindingCase(side?"source-binding/raw-locate-failed":"source-binding/selected-locate-failed",[side]{
            cfgModel.interfaces[side].locateResult=CR_NO_SUCH_DEVNODE;
        },false);
        bindingCase(side?"source-binding/raw-empty-id":"source-binding/selected-empty-id",[side]{cfgModel.interfaces[side].instance.clear();},false);
        bindingCase(side?"source-binding/raw-property-type-invalid":"source-binding/selected-property-type-invalid",[side]{
            cfgModel.interfaces[side].type=DEVPROP_TYPE_UINT32;
        },false);
    }
    const auto xusbModel=[] {
        cfgModel.interfaces[0].instance=L"USB\\VID_045E&PID_028E\\EXACT_SOURCE";
        cfgModel.nodes.at(11)={cfgModel.interfaces[0].instance,L"xusb22",100};
        cfgModel.interfaces[1].node=22;
        cfgModel.nodes.emplace(22,CommonCfgNode{cfgModel.interfaces[1].instance,L"HidUsb",11});
        cfgModel.nodes.emplace(100,CommonCfgNode{L"USB\\COMMON_HUB",L"USBHUB3",0});
    };
    bindingCase("source-binding/original-xusb-ancestor",xusbModel,true);
    bindingCase("source-binding/original-alias-xusb-ancestor",[&]{
        xusbModel();cfgModel.interfaces[0].instance=L"HID\\ALIAS\\SELECTED";
        cfgModel.nodes.at(11)={cfgModel.interfaces[0].instance,L"HidUsb",31};
        cfgModel.nodes.emplace(31,CommonCfgNode{L"USB\\VID_045E&PID_028E\\EXACT_SOURCE",L"xusb22",100});
        cfgModel.nodes.at(22).parent=31;
    },true);
    bindingCase("source-binding/xusb-other-root-common-hub",[&]{
        xusbModel();cfgModel.nodes.at(22).parent=41;
        cfgModel.nodes.emplace(41,CommonCfgNode{L"USB\\VID_045E&PID_028E\\OTHER_SOURCE",L"xusb22",100});
    },false);
    bindingCase("source-binding/xusb-device-id-query-failed",[&]{xusbModel();cfgModel.nodes.at(11).idKnown=false;},false);
    bindingCase("source-binding/xusb-raw-parent-query-failed",[&]{xusbModel();cfgModel.nodes.at(22).parentKnown=false;},false);
    bindingCase("source-binding/unknown-interface",[]{cfgModel.interfaces.pop_back();},false);
    resetSourceBindingModel();
    for(const char* fault:{"xinput-capabilities-unavailable","descriptor-unsupported"}){
        supplySdkCaps(rog);fakeOs.fault=fault;logs.clear();
        const auto rejected=inspect(handle,U2W(*g_axisCalibrationXinputKeys[0]),0);
        check(!rejected.ready&&std::string_view(inspectionStageName(rejected.inspectionStage))==fault,
            "exact devnode match does not bypass later XInput or descriptor admission");
        runtimeProjectionCases.push_back(std::string("source-binding/post-binding/")+fault);
    }
    supplySdkCaps(rog);resetSourceBindingModel();invalidate();ymccBackgroundUi::invalidate();consumed.clear();
    g_inputCaptureGamepadSnapshot.inputSource="xinput";g_inputCaptureGamepadSnapshot.commonHidReportSequence=0;
    testReports[0]={{0,32768,false},{1,32768,false},{2,32768,false},{3,32768,false},{4,32768,false},{21,0,false},{5,1,true}};
    testReports[1]={{0,32768,false},{1,32768,false},{2,32768,false},{3,32768,false},{4,32768,false},{21,0,false}};
    constexpr auto offset=offsetof(RAWINPUT,data)+offsetof(RAWHID,bRawData);
    std::vector<BYTE> memory(offset+32,0);auto* packet=reinterpret_cast<RAWINPUT*>(memory.data());
    packet->header.dwType=RIM_TYPEHID;packet->header.dwSize=static_cast<DWORD>(memory.size());packet->header.hDevice=handle;
    packet->data.hid.dwSizeHid=16;packet->data.hid.dwCount=2;packet->data.hid.bRawData[17]=1;
    const auto before=evaluations;
    check(commonControllerHidCapture(packet,static_cast<UINT>(memory.size())),"real binding inspect capture admits exact-node two-edge packet");
    check(devices.at(handle).ready&&evaluations==before+2&&consumed.size()==2,"actual inspect and original evaluators consume both edges");
    check(consumed[0].wButtons==XINPUT_GAMEPAD_A&&!consumed[1].wButtons,"exact-node press and release stay ordered");
    XINPUT_GAMEPAD native{};native.bLeftTrigger=47;native.bRightTrigger=93;CommonControllerHidRead receipt;
    check(commonControllerHidRead(native,receipt,true)&&receipt.sequence==sequence&&receipt.completeBasic&&!receipt.independentTriggers,
        "actual common HID read consumes exact-node sample and original metadata");
    check(native.bLeftTrigger==47&&native.bRightTrigger==93,"native independent triggers remain original");
    check(fakeOs.bindingCalls==0&&fakeOs.cfgCalls>0,"integrated packet used actual binding and typed CFG leaves");
    runtimeProjectionCases.push_back("source-binding/rog16-inspect-capture-read");
    g_xinputSlotUnknownMask=1;const auto evaluationsBefore=evaluations;
    check(!commonControllerHidCapture(packet,static_cast<UINT>(memory.size()))&&evaluations==evaluationsBefore,
        "unknown current source identity still rejects a previously qualified exact devnode");
    runtimeProjectionCases.push_back("source-binding/current-identity-unknown");
    g_xinputSlotUnknownMask=0;++power;CommonControllerHidRead stale;
    check(!commonControllerHidRead(native,stale,true),"new power generation cannot consume old exact-devnode sample");
    runtimeProjectionCases.push_back("source-binding/stale-power-generation");
    --power;invalidate();ymccBackgroundUi::invalidate();
}
#endif
