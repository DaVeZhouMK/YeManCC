#pragma once
// Included by the offline actual-TU runner only, after all historical suites.
// No production executable is launched. All Host/mode/visibility I/O uses the
// existing YMCC_FAULT_INJECT seam, never HID or controller enumeration.
static bool msiPersonaModeHandoffRegression(){
    using namespace ymcc::msi::persona;
    unsigned cases=0,failed=0;std::ostringstream report;
    auto check=[&](const char* name,bool ok){++cases;if(!ok)++failed;report<<(ok?"PASS ":"FAIL ")<<name<<"\n";};
    Facts f;f.enabled=f.knownBoard=f.startScope=f.personaOnly=f.targetWanted=f.pumpQuiesced=true;
    f.oldTargetPrepared=f.sourceKnown=f.routeArmed=f.routeConfirmed=f.modeOwned=f.powerReady=true;
    f.epoch=10;f.power=f.boundPower=3;f.sourceGeneration=7;f.from="elite";f.to="steamdeck";
    check("pure confirmed known MSI persona replacement admitted",capture(f));
    for(const char* from:{"steamdeck","dualsense-edge","elite"})for(const char* to:{"steamdeck","dualsense-edge","elite"}){
        auto v=f;v.from=from;v.to=to;check("current replacement table",capture(v)==(v.from!=v.to));
    }
    for(const char* retired:{"xbox360","dualshock4","dualsense"}){
        auto v=f;v.from=retired;check("legacy runtime source rejected",!capture(v));
        v=f;v.to=retired;check("legacy runtime target rejected",!capture(v));
        v=f;v.from=ymcc::controllerPersona::saved(retired);
        check("explicit saved migration produces current source",capture(v)&&!virtualPersona(retired));
    }
    auto reject=[&](const char* name,auto mutate){auto v=f;mutate(v);check(name,!capture(v));};
    reject("flag off",[](auto& v){v.enabled=false;});reject("ROG or common cannot capture",[](auto& v){v.knownBoard=false;});
    reject("standalone stop cannot capture",[](auto& v){v.startScope=false;});reject("not persona-only",[](auto& v){v.personaOnly=false;});
    reject("virtual-off",[](auto& v){v.targetWanted=false;});reject("pump still reading",[](auto& v){v.pumpQuiesced=false;});
    reject("target unprepared",[](auto& v){v.oldTargetPrepared=false;});reject("unknown physical root",[](auto& v){v.sourceKnown=false;});
    reject("route unarmed",[](auto& v){v.routeArmed=false;});reject("source unconfirmed",[](auto& v){v.routeConfirmed=false;});
    reject("no mode ownership",[](auto& v){v.modeOwned=false;});reject("suspend",[](auto& v){v.powerReady=false;});
    reject("exit fence",[](auto& v){v.exitFence=true;});reject("epoch zero",[](auto& v){v.epoch=0;});
    reject("power zero",[](auto& v){v.power=v.boundPower=0;});reject("different power",[](auto& v){v.boundPower++;});
    reject("generation zero",[](auto& v){v.sourceGeneration=0;});reject("same persona",[](auto& v){v.to=v.from;});
    reject("disabled target",[](auto& v){v.to="disabled";});reject("unknown target",[](auto& v){v.to="unknown";});
    auto r=f;r.releaseProven=r.visibilityHeld=r.sourceStillCurrent=true;
    check("pure proven release may retain ownership",retain(r));
    for(int i=0;i<4;i++){auto v=r;if(i==0)v.releaseProven=false;if(i==1)v.visibilityHeld=false;if(i==2)v.sourceStillCurrent=false;if(i==3)v.quarantined=true;check("unproven or quarantined release cannot retain",!retain(v));}
    check("pure new epoch same power resumes",resume(true,true,true,false,10,11,3,3));
    check("epoch zero never resumes",!resume(true,true,true,false,10,0,3,3));
    check("old epoch never resumes",!resume(true,true,true,false,10,10,3,3));
    check("new power never resumes",!resume(true,true,true,false,10,11,3,4));
    check("exit never resumes",!resume(true,true,true,true,10,11,3,3));

    const auto savedAppDir=g_appDataDir;const bool savedDetail=g_domainLogEnabled[1].load();
    const auto emittedDir=fspath::path(g_yemanccDataDir)/(L"persona-handoff-native-log-"+std::to_wstring(GetTickCount64()));
    fspath::create_directories(emittedDir);g_nativeLifecycleLog.close();g_nativeLifecycleLog.clear();g_nativeDetailLog.close();g_nativeDetailLog.clear();
    g_appDataDir=emittedDir.wstring();g_domainLogEnabled[1]=false; // Prove existing base lane, not a new global logging exception.
    // Use actual InputHost start/stop, not an alternate ordering model.
    auto count=[&](const char* v){return std::count(g_startEntrySeam.trace.begin(),g_startEntrySeam.trace.end(),v);};
    auto record=[&](unsigned generation,const char* root){
        std::lock_guard<std::mutex> l(g_msiMainSourceMx);g_msiMainSource={};g_msiMainSource.valid=true;
        g_msiMainSource.generation=generation;g_msiMainSource.interfacePath="offline-msi-1902-main";
        g_msiMainSource.usbInstance=root;g_msiMainSource.vendorId=0x0db0;g_msiMainSource.productId=0x1902;
    };
    auto seed=[&](){
        if(g_inputHostProcess){CloseHandle(g_inputHostProcess);g_inputHostProcess=nullptr;}
        if(g_inputHostServerPipe){CloseHandle(g_inputHostServerPipe);g_inputHostServerPipe=nullptr;}
        msiPersonaModeReset();g_msiPersonaStartScope=false;
        g_startEntrySeam=YmccStartEntrySeam{};g_startEntrySeam.active=true;g_startEntrySeam.confirmRouteAfterEnsure=false;
        g_machineIdentity.family=YmccFamily::MsiClaw;g_machineIdentity.manufacturer="MICRO-STAR INTERNATIONAL CO., LTD.";
        g_machineIdentity.product="MS-1T91";g_machineIdentity.attempted=true;
        g_hardwareWriteGate=true;g_powerLifecycle=PowerLifecycle::Ready;g_powerGeneration=3;g_aiFanMockSession.isolated=false;
        g_exitRequested=false;g_inputHostExitFence=false;g_inputReady=true;g_inputHostPowerAdmissionRevoked=false;g_inputHostFaultHoldsStart=false;g_inputHostReleaseQuarantined=false;g_inputHostStartBlocked=false;
        g_wakeHoldPhysicalUnhide=false;g_hidHideVisibilityJournal={};g_msiM12RemapDone=true;g_hidHideLastHealTick=GetTickCount64();
        g_realStickTest.enabled=true;g_realStickTest.virtualTargetEnabled=true;g_realStickTest.virtualTargetPersona="steamdeck";
        g_inputHostBoundPersona="elite";g_inputHostPrepared=g_inputHostNeutralized=g_inputHostHello=true;
        g_inputHostTransportFault=false;g_inputHostPipeRetired=false;g_inputHostRunId="offline-old-run";
        g_inputHostPipeName=L"offline-old-pipe";g_inputHostStartupNonce="offline-old-nonce";
        g_inputHostEpoch=10;g_inputHostEpochMirror=10;g_inputHostEpochCounter=100;g_inputHostBoundPowerGeneration=3;
        g_inputHostProcess=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        g_msiDinput.pad=nullptr;g_msiDinput.enabled=true;g_msiDinput.productId=0x1902;g_msiDinput.mainInterfaceMatched=true;
        g_msiDinputSourceValid=true;g_msiDinputRouteDinputArmed=true;g_msiDinputRouteConfirmed=true;
        g_msiDinputReturnOwed=true;g_msiMode1TerminalAttempted=false;g_msiDinputAdmissionLogged=false;
        record(7,"USB\\MSI\\PHYSICAL");
    };
    auto stop=[&](const char* reason){std::lock_guard<std::mutex> l(g_inputHostMx);inputHostStopLocked(reason);};
    seed();const bool started=inputHostStart();
    check("actual entry target replacement succeeds",started);
    check("actual entry new epoch does not reuse epoch 10",g_inputHostEpoch!=0&&g_inputHostEpoch!=10);
    check("actual candidate no mode1 on healthy persona replacement",count("mode-write:1")==(!buildEnabled?1:0));
    check("actual candidate no redundant mode2 on healthy replacement",count("mode-write:2")==(!buildEnabled?1:0));
    check("actual entry old source and confirmation invalidated",!g_msiDinputSourceValid.load()&&!g_msiDinputRouteConfirmed.load());
    check("actual entry ownership remains owed",g_msiDinputReturnOwed.load());
    check("actual entry no pause latch",!g_msiPersonaReadPause.load()&&!g_msiPersonaModeHandoff.active&&!g_msiPersonaPumpHold.owns_lock());
    g_startEntrySeam.trace.clear();g_realStickTest.virtualTargetEnabled=false;stop("virtual-target-disabled");
    check("actual off issues mode1 once but retains unverified debt",count("mode-write:1")==1&&g_msiDinputReturnOwed.load()&&g_msiMode1TerminalAttempted.load());

    const auto fixture=fspath::path(g_yemanccDataDir)/L"msi-persona-log-replay-input.json";
    if(fspath::exists(fixture)){
        std::ifstream in(fixture);json data;in>>data;
        check("real replay fixture has exactly four current-PID pairs",data.at("pairs").size()==4);
        for(const auto& pair:data.at("pairs")){
            seed();g_inputHostBoundPersona=pair.at("from").get<std::string>();g_realStickTest.virtualTargetPersona=pair.at("to").get<std::string>();
            const auto epoch=pair.at("oldHostEpoch").get<unsigned long long>(),power=pair.at("powerGeneration").get<unsigned long long>();
            g_inputHostEpoch=g_inputHostEpochMirror=epoch;g_powerGeneration=power;g_inputHostBoundPowerGeneration=power;
            const auto root=pair.at("sourceRoot").get<std::string>();record(pair.at("sourceGeneration").get<unsigned>(),root.c_str());
            const bool ok=inputHostStart();
            const bool elite=pair.at("to")=="elite";
            // Fixture has no external target identity/OpenXInput readback.
            // Elite must fail that unchanged gate, then compensate normally.
            check("real persona pair preserves healthy handoff or required Elite identity refusal",ok==!elite&&count("mode-write:1")== (buildEnabled?(elite?1:0):(elite?2:1))&&count("mode-write:2")==(!buildEnabled?1:0));
            check("real persona replacement still revokes old source/frame",!g_msiDinputSourceValid.load()&&!g_msiDinputRouteConfirmed.load());
            report<<"realPid="<<pair.at("pid")<<" observedPairGapMs="<<pair.at("pairGapMs")<<" measuredModeWritePair=true hardwareCauseProven=false\n";
            stop("offline-replay-cleanup");
        }
    }

    for(const char* reason:{"persona-changed","app-exit","virtual-target-disabled","coordinator-stop"}){
        seed();stop(reason);check("actual standalone or terminal stop never retains",!g_msiDinputRouteDinputArmed.load()&&count("mode-write:1")==1&&!g_msiPersonaReadPause.load());
    }
    // Fail after the old target release, both before and after the new epoch.
    for(int fault=0;fault<4;fault++){
        seed();if(fault==0)g_startEntrySeam.assetsPresent=false;if(fault==1)g_startEntrySeam.executablePresent=false;
        if(fault==2)g_startEntrySeam.pipeFailCommand="PREPARE_TARGET";
        if(fault==3)g_startEntrySeam.pipeThrowCommand="HELLO";
        bool result=false,thrown=false;try{result=inputHostStart();}catch(...){thrown=true;}
        check("actual early-return or exception does not sign start",!result&&(fault!=3||thrown));
        if(!buildEnabled&&fault==1){
            check("flag-off executable refusal preserves original route behavior",count("mode-write:1")==1&&count("mode-write:2")==1&&g_msiDinputRouteDinputArmed.load()&&!g_msiMode1TerminalAttempted.load());
            stop("offline-baseline-executable-cleanup");
        }
        if(!buildEnabled&&fault==3){g_startEntrySeam.pipeThrowCommand.clear();stop("offline-baseline-exception-cleanup");}
        check("actual abort issues compensation not fabricated readback",count("mode-write:1")== (buildEnabled||fault==0?1:2)&&g_msiDinputReturnOwed.load()&&g_msiMode1TerminalAttempted.load()&&!g_msiDinputRouteDinputArmed.load());
        check("actual abort never leaves pause or handoff",!g_msiPersonaReadPause.load()&&!g_msiPersonaModeHandoff.active&&!g_msiPersonaStartScope&&!g_msiPersonaPumpHold.owns_lock());
    }
    if(buildEnabled){
        // Actual capture refuses an in-flight pump, without waiting.
        seed();{
            std::lock_guard<std::mutex> h(g_inputHostMx);MsiPersonaModeStartScope scope;
            // Hold from another thread: try_lock on a mutex owned by this
            // thread is not a portable test of contention.
            HANDLE entered=CreateEventW(nullptr,TRUE,FALSE,nullptr),release=CreateEventW(nullptr,TRUE,FALSE,nullptr);
            std::thread busy([&]{std::lock_guard<std::mutex> p(g_msiPersonaPumpMx);SetEvent(entered);WaitForSingleObject(release,2000);});
            WaitForSingleObject(entered,1000);msiPersonaModeCapture("persona-changed",true,"elite");
            check("actual busy pump refuses handoff",!g_msiPersonaModeHandoff.active);SetEvent(release);busy.join();CloseHandle(entered);CloseHandle(release);
        }
        for(int fault=0;fault<5;fault++){
            seed();{
                std::lock_guard<std::mutex> h(g_inputHostMx);MsiPersonaModeStartScope scope;inputHostStopLocked("persona-changed");
                check("actual helper healthy release retained",g_msiPersonaModeHandoff.retained&&!g_msiDinputSourceValid.load());
                g_inputHostEpoch=g_inputHostEpochMirror=11;g_inputHostBoundPowerGeneration=3;
                if(fault==0)g_inputHostEpoch=g_inputHostEpochMirror=0;
                if(fault==1)g_inputHostEpoch=g_inputHostEpochMirror=10;
                if(fault==2)g_powerGeneration=4;
                const bool resumed=msiPersonaModeResumeOnNewTuple();
                if(fault<3)check("actual zero/old epoch or power change cannot unpause",!resumed&&g_msiPersonaReadPause.load());
                else {check("actual new tuple resumes clean",resumed&&!g_msiPersonaReadPause.load());
                    record(fault==3?7:8,fault==3?"USB\\MSI\\PHYSICAL":"USB\\MSI\\OTHER");
                    g_inputHostPrepared=g_inputHostNeutralized=true;
                    check("actual old generation or other root cannot finish",!msiPersonaModeFinish());
                }
            }
            check("actual context rejection releases pause",!g_msiPersonaReadPause.load()&&!g_msiPersonaModeHandoff.active);
            // Changed power is a legitimate existing restore defer, not a
            // fake successful hardware return. Only the pause must clear.
        }
        seed();{
            std::lock_guard<std::mutex> h(g_inputHostMx);MsiPersonaModeStartScope scope;inputHostStopLocked("persona-changed");
            g_inputHostEpoch=g_inputHostEpochMirror=11;g_inputHostBoundPowerGeneration=3;
            check("actual healthy tuple resume",msiPersonaModeResumeOnNewTuple());
            record(8,"USB\\MSI\\PHYSICAL");g_inputHostPrepared=g_inputHostNeutralized=true;
            check("actual new generation same root may complete",msiPersonaModeFinish());
        }
        seed();g_startEntrySeam.hostExitWait=WAIT_TIMEOUT;{
            std::lock_guard<std::mutex> h(g_inputHostMx);MsiPersonaModeStartScope scope;inputHostStopLocked("persona-changed");
            check("actual unproven old Host release cannot retain",!g_msiPersonaModeHandoff.active&&!g_msiDinputRouteDinputArmed.load()&&count("mode-write:1")==1);
        }
        seed();g_inputHostReleaseQuarantined=true;{
            std::lock_guard<std::mutex> h(g_inputHostMx);MsiPersonaModeStartScope scope;inputHostStopLocked("persona-changed");
            check("actual release quarantine cannot retain",!g_msiPersonaModeHandoff.active&&!g_msiDinputRouteDinputArmed.load());
        }
        // Candidate-only source failure proves fall-back, not widened hide.
        for(int fault=0;fault<3;fault++){
            seed();if(fault==0)g_startEntrySeam.hidHideHoldResult=false;
            if(fault==1){std::lock_guard<std::mutex> l(g_msiMainSourceMx);g_msiMainSource.usbInstance.clear();}
            if(fault==2)g_machineIdentity.family=YmccFamily::AsusRogAlly;
            inputHostStart();check("actual unsafe source/hold/ROG does not retain",count("mode-write:1")==(fault==2?0:1)&&!g_msiPersonaReadPause.load());
        }
    }
    g_startEntrySeam.pipeFailCommand.clear();g_startEntrySeam.pipeThrowCommand.clear();stop("offline-final-cleanup");
    g_startEntrySeam.active=false;
    g_nativeLifecycleLog.flush();g_nativeLifecycleLog.close();g_nativeLifecycleLog.clear();g_nativeDetailLog.close();g_nativeDetailLog.clear();
    std::ifstream emittedFile(emittedDir/L"native-lifecycle.log");std::string line;unsigned malformed=0,rows=0;std::map<std::string,unsigned> phases;
    bool emittedIdentity=true,discardedOldFrame=true,newTupleIdentity=true;
    while(std::getline(emittedFile,line)){
        auto r=json::parse(line,nullptr,false);if(r.is_discarded()){++malformed;continue;}
        if(r.value("action","")!="persona-physical-mode-handoff")continue;
        ++rows;const auto phase=r.value("phase","");++phases[phase];
        emittedIdentity=emittedIdentity&&r.contains("time")&&r.at("pid")==GetCurrentProcessId()&&r.value("deviceModeVerified",true)==false&&r.value("modeWriteIssued",true)==false;
        if(phase=="old-source-released")discardedOldFrame=discardedOldFrame&&!r.at("sourceValid").get<bool>()&&!r.at("routeConfirmed").get<bool>()&&r.at("readPaused").get<bool>();
        if(phase=="complete")newTupleIdentity=newTupleIdentity&&r.at("newHostEpoch")!=0&&r.at("newHostEpoch")!=r.at("oldHostEpoch")&&!r.at("readPaused").get<bool>();
    }
    check("actual base-file writer JSON parses with detail off",emittedFile.is_open()&&malformed==0);
    check("actual file contains candidate phases only when flag enabled",buildEnabled?(phases["captured"]>0&&phases["retained"]>0&&phases["old-source-released"]>0&&phases["complete"]>0&&phases["aborted"]>0&&phases["ineligible"]>0):rows==0);
    check("actual file retains PID/time and no fabricated mode proof",emittedIdentity);
    check("actual durable old-source-release rows reject old frame and confirm",discardedOldFrame);
    check("actual durable completion rows require new tuple and unpaused read",newTupleIdentity);
    report<<"persistedHandoffRows="<<rows<<" malformedRows="<<malformed<<"\n";
    g_appDataDir=savedAppDir;g_domainLogEnabled[1]=savedDetail;
    report<<"handoffBuild="<<buildEnabled<<" cases="<<cases<<" failed="<<failed<<"\n";
    std::ofstream out(fspath::path(g_yemanccDataDir)/L"msi-persona-mode-handoff-regression.txt");out<<report.str();return failed==0&&out.good();
}
