#pragma once
static bool msiVisibilityWakeDiagnosticsRegression() {
    using namespace ymcc::msi::visibility::wake;unsigned cases=0,failed=0;std::ostringstream report;
    auto check=[&](const char* n,bool ok){++cases;failed+=!ok;report<<(ok?"PASS ":"FAIL ")<<n<<"\n";};
    {Policy p;check("ready without observed resume does not arm",!p.arm(true,2,100));p.noteResume(false,2,100);check("ineligible receipt does not arm",!p.arm(true,2,100));
        p.noteResume(true,2,100);check("real resume + ready arms once",p.arm(true,2,110));check("duplicate ready does not reset deadline",!p.arm(true,2,200)&&p.snapshot().armedTick==110);
        p.noteResume(true,2,200);check("duplicate user/automatic wake does not reset receipt clock",p.snapshot().observedTick==100);
        auto d=p.poll(true,2,111,Active::Running,2);check("alive child is not forgotten/replaced",d.action==Action::Wait&&d.log&&p.snapshot().pending);
        d=p.poll(true,2,112,Active::Running,2);check("waiting receipt is not periodic spam",d.action==Action::Wait&&!d.log);
        d=p.poll(true,2,113,Active::Absent,2);check("confirmed old exit permits one new reader",d.action==Action::Launch&&d.episode==1&&d.generation==2&&!p.snapshot().pending);
        check("post launch polling has no restart",p.poll(true,2,114,Active::Absent,2).action==Action::Idle);
        p.noteResume(true,3,1000);check("second real resume is independent bounded episode",p.arm(true,3,1001)&&p.poll(true,3,1002,Active::Absent,3).episode==2);
        p.noteResume(true,4,2000);check("third wake exceeds process diagnostic budget",!p.arm(true,4,2001)&&p.snapshot().wakeAttempts==2);
    }
    for(int bad=0;bad<5;++bad){Policy p;p.noteResume(true,2,100);p.arm(true,2,101);const auto d=p.poll(bad!=0,bad==1?3:2,bad==2?100:bad==3?60101:102,bad==4?Active::Unknown:Active::Absent,2);
        check("ineligible stale backwards expired unknown ends without launch",d.action==Action::Refuse&&!p.snapshot().pending);}
    {Policy p;p.noteResume(true,2,100);p.arm(true,2,101);p.noteResume(true,3,200);p.arm(true,3,201);
        check("old worker snapshot cannot consume new wake ticket",p.poll(true,3,202,Active::Unknown,2).action==Action::Idle&&p.snapshot().pending&&p.snapshot().pendingGeneration==3);
        check("current pending ticket still launches",p.poll(true,3,203,Active::Absent,3).action==Action::Launch);}
    {Policy p;p.noteResume(true,2,100);check("delayed Ready after receipt ceiling does not create reader",!p.arm(true,2,60100));}
    {Policy p;check("startup scope gate is not consumed by ineligibility",p.startup(false,Active::Absent).action==Action::Idle&&!p.snapshot().startupAttempted);
        check("startup still at most one attempt",p.startup(true,Active::Unknown).action==Action::Refuse&&p.startup(true,Active::Absent).action==Action::Idle);}
    if(g_inputCaptureThread||g_inputHostProcess)return false;
    const auto machine=g_machineIdentity;const auto config=g_realStickTest;const auto policy=g_msiVisibilityWakePolicy.snapshot();const auto pending=g_msiVisibilityWakePending.load();
    const auto activeHook=g_msiVisibilityObserverActiveForTest;const auto launchHook=g_msiVisibilityObserverLaunchForTest;const auto emitHook=g_msiControllerTraceEmitForTest;const auto metadataHook=g_msiVisibilityObserverMetadataForTest;
    const auto power=g_powerGeneration.load();const auto lifecycle=g_powerLifecycle.load();const bool ready=g_inputReady.load(),detail=g_domainLogEnabled[1].load(),exit=g_exitRequested.load(),fence=g_inputHostExitFence.load();
    const auto hostEpoch=g_inputHostEpoch;const auto connected=g_xinputConnectedMask,physical=g_xinputPhysicalSourceMask,virt=g_xinputVirtualEndpointMask;
    const bool locked=g_xinputPhysicalSourceLocked,quarantine=g_inputHostReleaseQuarantined,blocked=g_inputHostStartBlocked,armed=g_msiDinputRouteDinputArmed.load();
    unsigned queries=0,launches=0;bool failLaunch=false;std::vector<json> rows;
    g_msiControllerTraceEmitForTest=[&](const json& r){rows.push_back(r);};
    auto reset=[&](){g_machineIdentity.family=YmccFamily::MsiClaw;g_machineIdentity.manufacturer="MICRO-STAR INTERNATIONAL CO., LTD.";g_machineIdentity.product="MS-1T91";g_machineIdentity.attempted=g_machineIdentity.resolved=true;
        g_domainLogEnabled[1]=true;g_exitRequested=false;g_inputHostExitFence=false;g_powerGeneration=2;g_powerLifecycle=PowerLifecycle::Ready;g_inputReady=true;
        g_msiVisibilityWakePolicy.restore(State{});g_msiVisibilityWakePending=0;queries=launches=0;failLaunch=false;rows.clear();g_msiVisibilityObserverMetadataForTest={};
        g_msiVisibilityObserverActiveForTest=[&](){++queries;return Active::Absent;};g_msiVisibilityObserverLaunchForTest=[&](const Decision& d){++launches;return !failLaunch;};};
    for(int gate=0;gate<6;++gate){reset();switch(gate){case 0:g_machineIdentity.family=YmccFamily::AsusRogAlly;break;case 1:g_machineIdentity.product="unregistered";break;
        case 2:g_domainLogEnabled[1]=false;break;case 3:g_inputHostExitFence=true;break;case 4:g_exitRequested=true;break;case 5:g_powerLifecycle=PowerLifecycle::Resuming;break;}
        msiVisibilityObserverNoteResume(2);msiVisibilityObserverWakeReady(2);msiVisibilityObserverWakeMaintenance();
        check("actual nonMSI/unregistered/detail/fence/exit/notReady wake has zero query/launch",queries==0&&launches==0&&g_msiVisibilityWakePending.load()==0);}
    reset();msiVisibilityObserverWakeReady(2);msiVisibilityObserverWakeMaintenance();check("actual Ready without resume cannot query",queries+launches==0);
    reset();msiVisibilityObserverNoteResume(2);msiVisibilityObserverWakeReady(2);msiVisibilityObserverWakeReady(2);
    check("UI receipt/ready do not query process or launch",queries+launches==0);
    msiVisibilityObserverWakeMaintenance();check("actual lifecycle wake performs one optional fake launch",queries==unsigned(buildEnabled)&&launches==unsigned(buildEnabled));
    msiVisibilityObserverWakeMaintenance();msiVisibilityObserverWakeReady(2);msiVisibilityObserverWakeMaintenance();check("actual duplicate wake is not a restart",launches==unsigned(buildEnabled));
    check("actual child callback never mutates Host/masks/control gates",g_inputHostEpoch==hostEpoch&&g_xinputConnectedMask==connected&&g_xinputPhysicalSourceMask==physical&&g_xinputVirtualEndpointMask==virt&&g_xinputPhysicalSourceLocked==locked&&g_inputHostReleaseQuarantined==quarantine&&g_inputHostStartBlocked==blocked&&g_msiDinputRouteDinputArmed.load()==armed);
    {std::deque<std::string> oldQueue;{std::lock_guard<std::mutex> lock(g_inputHostLifecycleWorkMx);oldQueue=g_inputHostLifecycleWorkQueue;g_inputHostLifecycleWorkQueue.clear();}
        const bool oldRequested=g_inputHostLifecycleWorkRequested.exchange(false),oldInflight=g_inputHostLifecycleWorkInFlight.exchange(false);
        const auto oldApp=g_appDataDir;const auto logDir=fspath::path(g_yemanccDataDir)/L"wake-observer-owner-offline";fspath::create_directories(logDir);
        g_nativeLifecycleLog.close();g_nativeDetailLog.close();g_nativeLifecycleLog.clear();g_nativeDetailLog.clear();g_appDataDir=logDir.wstring();
        reset();msiVisibilityObserverNoteResume(2);msiVisibilityObserverWakeReady(2);inputHostRunQueuedLifecycleWork();
        check("actual existing worker drains diagnostic even with no controller job",launches==unsigned(buildEnabled)&&!g_inputHostProcess&&g_inputHostLifecycleWorkQueue.empty()&&!g_inputHostLifecycleWorkInFlight.load());
        {std::lock_guard<std::mutex> lock(g_inputHostLifecycleWorkMx);g_inputHostLifecycleWorkQueue=oldQueue;}
        g_inputHostLifecycleWorkRequested=oldRequested;g_inputHostLifecycleWorkInFlight=oldInflight;
        g_nativeLifecycleLog.close();g_nativeDetailLog.close();g_nativeLifecycleLog.clear();g_nativeDetailLog.clear();g_appDataDir=oldApp;
    }
    reset();g_inputReady=false;msiVisibilityObserverNoteResume(2);msiVisibilityObserverWakeReady(2);msiVisibilityObserverWakeMaintenance();check("failed native input readiness is observable, never changed",launches==unsigned(buildEnabled)&&!g_inputReady.load());
    reset();g_msiVisibilityObserverActiveForTest={};msiVisibilityObserverNoteResume(2);msiVisibilityObserverWakeReady(2);msiVisibilityObserverWakeMaintenance();check("missing fault liveness hook cannot fallthrough or launch",queries+launches==0&&g_msiVisibilityWakePending.load()==0);
    reset();g_msiVisibilityObserverLaunchForTest={};msiVisibilityObserverNoteResume(2);msiVisibilityObserverWakeReady(2);msiVisibilityObserverWakeMaintenance();check("missing launch hook has no CreateProcess fallback",queries==unsigned(buildEnabled)&&launches==0);
    reset();failLaunch=true;msiVisibilityObserverNoteResume(2);msiVisibilityObserverWakeReady(2);msiVisibilityObserverWakeMaintenance();msiVisibilityObserverWakeMaintenance();check("failed child launch is not retried",launches==unsigned(buildEnabled));
    reset();g_msiVisibilityObserverActiveForTest=[&](){++queries;return Active::Running;};msiVisibilityObserverNoteResume(2);msiVisibilityObserverWakeReady(2);msiVisibilityObserverWakeMaintenance();msiVisibilityObserverWakeMaintenance();
    check("actual live child is retained, no concurrent reader",queries==2u*unsigned(buildEnabled)&&launches==0);
    reset();g_msiVisibilityObserverActiveForTest=[&](){++queries;return Active::Unknown;};msiVisibilityObserverNoteResume(2);msiVisibilityObserverWakeReady(2);msiVisibilityObserverWakeMaintenance();msiVisibilityObserverWakeMaintenance();check("unknown child state stops without overlapping launcher",queries==unsigned(buildEnabled)&&launches==0);
    reset();msiVisibilityObserverNoteResume(2);msiVisibilityObserverWakeReady(2);g_powerGeneration=3;msiVisibilityObserverWakeMaintenance();check("stale power ticket refused before child API",queries+launches==0&&g_msiVisibilityWakePending.load()==0);
    reset();bool injected=false;g_msiVisibilityObserverActiveForTest=[&](){++queries;if(!injected){injected=true;g_powerGeneration=3;msiVisibilityObserverNoteResume(3);msiVisibilityObserverWakeReady(3);}return Active::Absent;};
    msiVisibilityObserverNoteResume(2);msiVisibilityObserverWakeReady(2);msiVisibilityObserverWakeMaintenance();
    check("actual older process-query callback cannot erase a newer wake",buildEnabled?(injected&&g_msiVisibilityWakePending.load()==3&&launches==0):queries+launches==0);
    msiVisibilityObserverWakeMaintenance();check("actual newer wake remains consumable once",launches==unsigned(buildEnabled)&&g_msiVisibilityWakePending.load()==0);
    reset();g_msiVisibilityObserverActiveForTest=[&]()->Active{++queries;throw 1;};msiVisibilityObserverNoteResume(2);msiVisibilityObserverWakeReady(2);msiVisibilityObserverWakeMaintenance();msiVisibilityObserverWakeMaintenance();
    check("throwing status hook ends pending without repeated queries",queries==unsigned(buildEnabled)&&!g_msiVisibilityWakePolicy.snapshot().pending&&g_msiVisibilityWakePending.load()==0);
    reset();g_msiVisibilityObserverActiveForTest=[&]()->Active{++queries;throw 1;};msiVisibilityObserverStartupDiagnostic();msiVisibilityObserverStartupDiagnostic();
    check("startup query exception still consumes its one attempt",queries==unsigned(buildEnabled));
    for(int stale=0;stale<4;++stale){reset();g_msiVisibilityObserverMetadataForTest=[&](){switch(stale){case 0:g_inputHostExitFence=true;break;case 1:g_domainLogEnabled[1]=false;break;case 2:g_powerGeneration=3;break;case 3:g_powerLifecycle=PowerLifecycle::Suspended;break;}};
        msiVisibilityObserverNoteResume(2);msiVisibilityObserverWakeReady(2);msiVisibilityObserverWakeMaintenance();
        check("post-metadata exit/detail/power/Ready drift refuses before create hook",queries==unsigned(buildEnabled)&&launches==0);
    }
    reset();msiVisibilityObserverStartupDiagnostic();msiVisibilityObserverStartupDiagnostic();check("new startup wrapper remains one-shot",queries==unsigned(buildEnabled)&&launches==unsigned(buildEnabled));
    g_machineIdentity=machine;g_realStickTest=config;g_msiVisibilityWakePolicy.restore(policy);g_msiVisibilityWakePending=pending;
    g_msiVisibilityObserverActiveForTest=activeHook;g_msiVisibilityObserverLaunchForTest=launchHook;g_msiControllerTraceEmitForTest=emitHook;g_msiVisibilityObserverMetadataForTest=metadataHook;
    g_powerGeneration=power;g_powerLifecycle=lifecycle;g_inputReady=ready;g_domainLogEnabled[1]=detail;g_exitRequested=exit;g_inputHostExitFence=fence;
    report<<"buildEnabled="<<buildEnabled<<" cases="<<cases<<" failed="<<failed<<"\n";std::ofstream out(fspath::path(g_yemanccDataDir)/L"msi-visibility-wake-diagnostics-native-entry.txt");out<<report.str();return failed==0&&out.good();
}
