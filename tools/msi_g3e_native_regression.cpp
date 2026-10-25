// Offline native regression runner. Including the actual production TU makes
// the delayed-MSI outer BUS gate part of the behavior test, not a copied model.
// This runner never enters wWinMain and never starts the hardware/runtime hosts.
#include "../native/main.cpp"
#include "msi_motion_log_replay.h"
static bool sensorStaMailboxEntryRegression() {
    unsigned cases=0,failed=0;
    std::ostringstream report;
    auto check=[&](const char* name,bool ok){++cases;if(!ok)++failed;report<<(ok ? "PASS " : "FAIL ")<<name<<"\n";};
    if(g_sensorThread) return false; // Never take ownership of an unrelated live owner.
    const bool oldPoll=g_sensorWakePollActive.exchange(false),oldJudge=g_gyroJudgeTaskEnabled.exchange(false);
    const HANDLE startEntered=CreateEventW(nullptr,TRUE,FALSE,nullptr),startRelease=CreateEventW(nullptr,TRUE,FALSE,nullptr),
        stopEntered=CreateEventW(nullptr,TRUE,FALSE,nullptr),stopRelease=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    std::atomic<bool> apartmentSta{false},startReturned{false},stopReturned{false};
    bool startOk=false,stopOk=false;
    g_sensorThreadCommandForTest=[&](SensorThreadRequest request){
        APTTYPE type;APTTYPEQUALIFIER qualifier;
        apartmentSta=SUCCEEDED(CoGetApartmentType(&type,&qualifier))&&(type==APTTYPE_STA||type==APTTYPE_MAINSTA);
        if(request==SensorThreadRequest::Start){SetEvent(startEntered);WaitForSingleObject(startRelease,2000);}
        if(request==SensorThreadRequest::Suspended){SetEvent(stopEntered);WaitForSingleObject(stopRelease,2000);}
        if(request==SensorThreadRequest::Rebind) throw std::runtime_error("test-command-failure");
    };
    check("actual dedicated sensor STA is created",inputCaptureSensorThreadEnsure());
    std::thread starter([&]{startOk=inputCaptureSensorThreadPost(SensorThreadRequest::Start);startReturned=true;});
    check("actual Start enters sensor owner not caller",WaitForSingleObject(startEntered,1500)==WAIT_OBJECT_0&&apartmentSta);
    std::thread stopper([&]{stopOk=inputCaptureSensorThreadPost(SensorThreadRequest::Suspended);stopReturned=true;});
    const ULONGLONG deadline=GetTickCount64()+1000;
    while(!inputCaptureSensorStopRequested()&&GetTickCount64()<deadline) Sleep(1);
    check("actual queued Suspended is visible during Start",inputCaptureSensorStopRequested());
    SetEvent(startRelease);
    const bool stopInFlight=WaitForSingleObject(stopEntered,1500)==WAIT_OBJECT_0;
    starter.join();
    check("actual Start ack cannot release the Suspended poster",stopInFlight&&startOk&&startReturned&&!stopReturned);
    SetEvent(stopRelease);stopper.join();
    check("actual Suspended waits for its own completion",stopOk&&stopReturned);
    check("actual command exception is not signed successful",!inputCaptureSensorThreadPost(SensorThreadRequest::Rebind));
    check("actual Shutdown joins owner before closing handles",inputCaptureSensorThreadShutdown()&&!g_sensorThread&&!g_sensorThreadWake);
    check("post after actual owner exit refuses without hanging",!inputCaptureSensorThreadPost(SensorThreadRequest::Start));
    g_sensorThreadCommandForTest={};g_sensorWakePollActive=oldPoll;g_gyroJudgeTaskEnabled=oldJudge;
    for(HANDLE h:{startEntered,startRelease,stopEntered,stopRelease})CloseHandle(h);
    report<<"cases="<<cases<<" failed="<<failed<<"\n";
    std::ofstream output(fspath::path(g_yemanccDataDir)/L"sensor-sta-mailbox-entry-selftest.txt");output<<report.str();
    return failed==0&&output.good();
}
static bool controllerIdleNativeEntryRegression() {
    unsigned cases=0,failed=0;std::ostringstream report;
    auto check=[&](const char* name,bool ok){++cases;if(!ok)++failed;report<<(ok?"PASS ":"FAIL ")<<name<<"\n";};
    g_inputIdleActivity.enable(false,10);
    FocusFrameContent frame;frame.buttons=4096;frame.lxShort=-1;frame.rawLtByte=2;frame.imuAdmitted=true;frame.gyroX=1;
    const auto original=focusIsolationAssembleFrame(frame);
    inputHostNoteIdleSubmit(frame,true);
    check("actual submit hook respects disabled diagnostics gate",g_inputIdleActivity.take(11).attempted.samples==0);
    g_inputIdleActivity.enable(true,20);
    inputHostNoteIdleSubmit(frame,false);inputHostNoteIdleSubmit(frame,true);
    auto w=g_inputIdleActivity.take(30);
    check("actual submit hook does not turn rejected ACK into accepted data",w.attempted.samples==2&&w.accepted.samples==1&&w.rejected==1);
    check("actual submit hook reads final raw rather than canonical/pre-gate input",w.accepted.last.buttons==4096&&w.accepted.last.sticks[0]==-1&&w.accepted.last.triggers[0]==2);
    inputHostNoteIdleSubmit(focusIsolationNeutralContent(),true);w=g_inputIdleActivity.take(40);
    check("actual semantic neutral is not logged as nonneutral just because live",w.accepted.samples==1&&w.accepted.meaningfulFrames==0&&w.accepted.rawActiveFrames==0&&w.imuFrames==0);
    check("diagnostic hook never modifies report or final content",focusIsolationAssembleFrame(frame)==original);
    check("periodic idle windows use gated domain not always-on base lane",!nativeLifecycleEventIsBase("input-idle-window")&&nativeLifecycleEventIsBase("display-idle-state"));
    g_inputIdleActivity.enable(false,50);report<<"cases="<<cases<<" failed="<<failed<<"\n";
    std::ofstream out(fspath::path(g_yemanccDataDir)/L"controller-idle-native-entry.txt");out<<report.str();return failed==0&&out.good();
}
static bool msiModeRecoveryNativeRegression() {
    unsigned cases=0,failed=0;std::ostringstream report;
    auto check=[&](const char* name,bool ok){++cases;if(!ok)++failed;report<<(ok?"PASS ":"FAIL ")<<name<<"\n";};
    ymcc::msi::ModeRecoveryFacts f{true,true,true,false,true,false,4,4,4,100,1100};
    check("fresh same-epoch owned return is admitted",ymcc::msi::observedXinputReturnAdmitted(f));
    auto deny=[&](const char* name,auto mutate){auto bad=f;mutate(bad);check(name,!ymcc::msi::observedXinputReturnAdmitted(bad));};
    deny("unknown board refuses",[](auto& v){v.knownBoard=false;});
    deny("disabled output refuses",[](auto& v){v.targetEnabled=false;});
    deny("disarmed route refuses",[](auto& v){v.routeArmed=false;});
    deny("already healed route refuses",[](auto& v){v.routeConfirmed=true;});
    deny("unowned mode refuses",[](auto& v){v.modeWriteOwned=false;});
    deny("exit fence refuses",[](auto& v){v.exitFence=true;});
    deny("no host epoch refuses",[](auto& v){v.hostEpoch=0;});
    deny("old confirmed lease refuses",[](auto& v){v.confirmedEpoch=3;});
    deny("old return observation refuses",[](auto& v){v.observedEpoch=3;});
    deny("missing observation refuses",[](auto& v){v.observedTick=0;});
    deny("stale observation refuses",[](auto& v){v.now=2101;});
    deny("clock inversion refuses",[](auto& v){v.now=99;});
    check("timeout boundary remains admitted",[](){auto v=ymcc::msi::ModeRecoveryFacts{true,true,true,false,true,false,4,4,4,100,2100};return ymcc::msi::observedXinputReturnAdmitted(v);}());
    check("actual route accelerates observed return before five seconds",msiDinputRouteWindowDecide(true,false,false,1100,true)==MsiDinputRouteWindowAction::Resend);
    check("ordinary cold admission retains five second window",msiDinputRouteWindowDecide(true,false,false,1100,false)==MsiDinputRouteWindowAction::Poll);
    check("early recovery remains single-shot",msiDinputRouteWindowDecide(true,false,true,1100,true)==MsiDinputRouteWindowAction::Poll);
    check("total budget still wins over early fact",msiDinputRouteWindowDecide(true,false,false,10000,true)==MsiDinputRouteWindowAction::Revert);
    const auto machine=g_machineIdentity;const auto seam=g_startEntrySeam;
    const auto epoch=g_inputHostEpochMirror.load(),confirmedEpoch=g_msiModeRecoveryConfirmedEpoch.load();
    const auto observedEpoch=g_msiModeRecoveryObservedEpoch.load(),observedTick=g_msiModeRecoveryObservedTick.load();
    const bool armed=g_msiDinputRouteDinputArmed.load(),confirmed=g_msiDinputRouteConfirmed.load(),sourceValid=g_msiDinputSourceValid.load();
    const bool owed=g_msiDinputReturnOwed.load(),fence=g_inputHostExitFence.load();
    const bool enabled=g_realStickTest.enabled,target=g_realStickTest.virtualTargetEnabled;
    const auto powerLifecycle=g_powerLifecycle.load();const bool hardwareWriteGate=g_hardwareWriteGate.load(),mockIsolated=g_aiFanMockSession.isolated;
    const auto powerGeneration=g_powerGeneration.load(),modeGeneration=g_msiFeedbackModeGeneration.load();
    const bool terminalAttempted=g_msiMode1TerminalAttempted.load(),feedbackInvalidated=g_msiFeedbackLeaseInvalidated.load();
    const auto disruptiveTick=g_msiLastDisruptiveTx.tick.load();const auto disruptiveKind=g_msiLastDisruptiveTx.kind.load();
    g_machineIdentity.family=YmccFamily::MsiClaw;g_machineIdentity.manufacturer="MICRO-STAR INTERNATIONAL CO., LTD.";
    g_machineIdentity.product="MS-1T91";g_machineIdentity.attempted=true;g_machineIdentity.resolved=true;
    g_inputHostEpochMirror=4;g_msiModeRecoveryConfirmedEpoch=4;g_msiModeRecoveryObservedEpoch=0;
    g_msiDinputRouteDinputArmed=true;g_msiDinputRouteConfirmed=false;g_msiDinputSourceValid=false;g_msiDinputReturnOwed=true;
    g_inputHostExitFence=false;g_realStickTest.enabled=true;g_realStickTest.virtualTargetEnabled=true;
    g_powerLifecycle=PowerLifecycle::Ready;g_hardwareWriteGate=true;g_aiFanMockSession.isolated=false;
    g_powerGeneration=7;g_msiFeedbackModeGeneration=9;
    HidHidePhysicalIdentity physical;physical.resolved=true;physical.vendorId=0x0DB0;physical.productId=0x1901;
    physical.usagePage=1;physical.usage=5;physical.deviceInstanceId="HID\\VID_0DB0&PID_1901&IG_00\\PHYSICAL";
    physical.baseContainerDeviceInstanceId="USB\\VID_0DB0&PID_1901\\PHYSICAL";
    auto rejectIdentity=[&](const char* name,auto mutate){auto bad=physical;mutate(bad);check(name,!msiDinputObservePhysicalXinputReturn(bad,GetTickCount64()));};
    rejectIdentity("vendor-control cannot trigger recovery",[](auto& v){v.usagePage=65520;v.usage=64;});
    rejectIdentity("keyboard cannot trigger recovery",[](auto& v){v.usage=6;});
    rejectIdentity("mouse cannot trigger recovery",[](auto& v){v.usage=2;});
    rejectIdentity("wrong PID cannot trigger recovery",[](auto& v){v.productId=0x1902;});
    rejectIdentity("unknown identity cannot trigger recovery",[](auto& v){v.resolved=false;});
    rejectIdentity("virtual root cannot trigger recovery",[](auto& v){v.baseContainerDeviceInstanceId="ROOT\\HIDMAESTRO\\0000";});
    check("actual observer accepts physical game-input return",msiDinputObservePhysicalXinputReturn(physical,GetTickCount64())&&msiDinputObservedXinputReturnAdmitted(GetTickCount64()));
    g_startEntrySeam=YmccStartEntrySeam{};g_startEntrySeam.active=true;g_startEntrySeam.modeWriteOk=true;
    auto writes=[&](){return std::count(g_startEntrySeam.trace.begin(),g_startEntrySeam.trace.end(),"mode-write:2");};
    auto captureTicket=[&](){
        const auto current=msiModeWriteCurrent();
        const ymcc::msi::modeTicket::Ticket ticket{true,current.host,current.power,current.source,current.mode,"dinput-route-observed-xinput-return"};
        const auto encoded=ymcc::msi::modeTicket::encode(ticket);ymcc::msi::modeTicket::Ticket parsed;
        check("actual v2 recovery ticket parses and matches admissible capture context",
              ymcc::msi::modeTicket::parse(encoded,parsed)&&parsed.dinput&&parsed.reason==ticket.reason&&
              !ymcc::msi::modeTicket::reject(parsed,current));
        return encoded;
    };
    // Capture queue authority first; change execution facts only afterwards.
    const auto oldEpochTicket=captureTicket();
    g_inputHostEpochMirror=5;g_msiModeRecoveryConfirmedEpoch=5;
    check("new epoch has fresh physical-return evidence despite old queued ticket",
          msiDinputObservePhysicalXinputReturn(physical,GetTickCount64())&&msiDinputObservedXinputReturnAdmitted(GetTickCount64()));
    msiDinputHandleModeWriteIntent(oldEpochTicket);
    check("actual lifecycle rejects old epoch before writing",writes()==0);
    g_inputHostEpochMirror=4;g_msiModeRecoveryConfirmedEpoch=4;
    check("restored epoch has fresh physical-return evidence",
          msiDinputObservePhysicalXinputReturn(physical,GetTickCount64())&&msiDinputObservedXinputReturnAdmitted(GetTickCount64()));
    const auto healedTicket=captureTicket();
    g_msiDinputRouteConfirmed=true;
    msiDinputHandleModeWriteIntent(healedTicket);
    check("actual lifecycle rejects healed route before writing",writes()==0);
    g_msiDinputRouteConfirmed=false;g_msiDinputSourceValid=false;
    const auto agedTicket=captureTicket();
    g_msiModeRecoveryObservedTick=GetTickCount64()-2001;
    check("captured return observation is aged at execution",!msiDinputObservedXinputReturnAdmitted(GetTickCount64()));
    msiDinputHandleModeWriteIntent(agedTicket);
    check("actual lifecycle rejects aged evidence before writing",writes()==0);
    const auto freshTicket=captureTicket();
    check("actual observer refreshes current physical-return evidence",
          msiDinputObservePhysicalXinputReturn(physical,GetTickCount64())&&msiDinputObservedXinputReturnAdmitted(GetTickCount64()));
    msiDinputHandleModeWriteIntent(freshTicket);
    check("actual lifecycle accepts current owned return without real HID access",writes()==1);
    check("write receipt still does not confirm source",!g_msiDinputRouteConfirmed.load()&&!g_msiDinputSourceValid.load());
    g_machineIdentity=machine;g_startEntrySeam=seam;g_inputHostEpochMirror=epoch;
    g_msiModeRecoveryConfirmedEpoch=confirmedEpoch;g_msiModeRecoveryObservedEpoch=observedEpoch;g_msiModeRecoveryObservedTick=observedTick;
    g_msiDinputRouteDinputArmed=armed;g_msiDinputRouteConfirmed=confirmed;g_msiDinputSourceValid=sourceValid;g_msiDinputReturnOwed=owed;
    g_inputHostExitFence=fence;g_realStickTest.enabled=enabled;g_realStickTest.virtualTargetEnabled=target;
    g_powerLifecycle=powerLifecycle;g_hardwareWriteGate=hardwareWriteGate;g_aiFanMockSession.isolated=mockIsolated;
    // A successful seam write still advances production transaction state.
    g_powerGeneration=powerGeneration;g_msiFeedbackModeGeneration=modeGeneration;
    g_msiMode1TerminalAttempted=terminalAttempted;g_msiFeedbackLeaseInvalidated=feedbackInvalidated;
    g_msiLastDisruptiveTx.tick=disruptiveTick;g_msiLastDisruptiveTx.kind=disruptiveKind;
    report<<"cases="<<cases<<" failed="<<failed<<"\n";
    std::ofstream out(fspath::path(g_yemanccDataDir)/L"msi-mode-recovery-native-entry.txt");out<<report.str();return failed==0&&out.good();
}
static bool controllerFreshNeutralNativeRegression(const wchar_t* replayPath) {
    unsigned cases=0,failed=0;std::ostringstream report;
    auto check=[&](const char* name,bool ok){++cases;if(!ok)++failed;report<<(ok?"PASS ":"FAIL ")<<name<<"\n";};
    const auto savedMachine=g_machineIdentity;const bool savedRouteArmed=g_msiDinputRouteDinputArmed.load();
    g_machineIdentity.family=YmccFamily::MsiClaw;g_machineIdentity.manufacturer="MICRO-STAR INTERNATIONAL CO., LTD.";g_machineIdentity.product="MS-1T91";
    g_msiDinputRouteDinputArmed=true;
    const auto process=g_inputHostProcess;const bool prepared=g_inputHostPrepared,neutralized=g_inputHostNeutralized,fault=g_inputHostTransportFault,admitted=g_inputHostFreshNeutralAdmitted;
    const int frames=g_inputHostFreshNeutralFrames;const DWORD mask=g_inputHostFreshNeutralMask;const auto lifecycle=g_inputHostLifecycle;
    // FreshNeutralReady only observes these values; this sentinel is never
    // passed to any Win32 process/pipe API by this test.
    g_inputHostProcess=reinterpret_cast<HANDLE>(uintptr_t(1));g_inputHostPrepared=true;g_inputHostNeutralized=true;g_inputHostTransportFault=false;
    auto reset=[&](){g_inputHostFreshNeutralAdmitted=false;g_inputHostFreshNeutralFrames=0;g_inputHostFreshNeutralMask=0;};
    auto three=[&](const XINPUT_GAMEPAD& raw){reset();const auto before=raw;
        bool a=inputHostFreshNeutralReady(true,raw,0),b=inputHostFreshNeutralReady(true,raw,0),c=inputHostFreshNeutralReady(true,raw,0);
        return !a&&!b&&c&&std::memcmp(&before,&raw,sizeof(raw))==0;};
    reset();
    check("missing physical source cannot publish live data",!inputHostPhysicalPublicationSourceAdmitted(false));
    check("full neutral cleanup can release old report without a source",inputHostPhysicalPublicationSourceAdmitted(false,true));
    check("neutral cleanup does not sign fresh physical evidence",!g_inputHostFreshNeutralAdmitted&&g_inputHostFreshNeutralFrames==0);
    const auto zero=focusIsolationNeutralContent();
    check("neutral content clears buttons back Guide sticks triggers and IMU",zero.buttons==0&&zero.backButtons==0&&zero.lxShort==0&&zero.lyShort==0&&zero.rxShort==0&&zero.ryShort==0&&zero.rawLtByte==0&&zero.rawRtByte==0&&!zero.imuAdmitted);
    const auto savedStage=g_inputHostPublicationStage.load();
    inputHostNotePublicationStage(InputHostPublicationStage::FreshNeutralWait);
    check("actual producer stage is observable without per-frame log I/O",std::string(inputHostPublicationStageName(g_inputHostPublicationStage.load()))=="opening-fresh-neutral");
    inputHostNotePublicationStage(InputHostPublicationStage::NeutralCleanupAccepted);
    check("neutral-only ACK is not labelled live source delivery",std::string(inputHostPublicationStageName(g_inputHostPublicationStage.load()))=="neutral-only-submit-accepted");
    check("unknown stage is honest",std::string(inputHostPublicationStageName(InputHostPublicationStage::Unknown))=="unknown");
    inputHostNotePublicationStage(savedStage);
    XINPUT_GAMEPAD raw{};raw.sThumbLY=-1;raw.sThumbRY=-1;
    check("actual fresh gate admits three real G3E residue frames",three(raw));
    raw.sThumbLX=2048;raw.sThumbRX=-2048;raw.bLeftTrigger=30;check("existing canonical threshold remains admission-only",three(raw));
    raw.sThumbLX=2049;reset();for(int i=0;i<4;i++)inputHostFreshNeutralReady(true,raw,0);check("actual moved stick cannot open first-frame gate",!g_inputHostFreshNeutralAdmitted);
    raw={};raw.wButtons=XINPUT_GAMEPAD_A;reset();for(int i=0;i<4;i++)inputHostFreshNeutralReady(true,raw,0);check("held physical button cannot open gate",!g_inputHostFreshNeutralAdmitted);
    raw={};raw.bRightTrigger=31;reset();for(int i=0;i<4;i++)inputHostFreshNeutralReady(true,raw,0);check("held trigger cannot open gate",!g_inputHostFreshNeutralAdmitted);
    raw={};reset();for(int i=0;i<4;i++)inputHostFreshNeutralReady(false,raw,0);check("source absence cannot manufacture neutral evidence",!g_inputHostFreshNeutralAdmitted);
    reset();inputHostFreshNeutralReady(true,raw,1);inputHostFreshNeutralReady(true,raw,1);check("mask change restarts consecutive evidence",!inputHostFreshNeutralReady(true,raw,2)&&g_inputHostFreshNeutralFrames==1);
    reset();g_inputHostTransportFault=true;check("transport fault still blocks neutral gate",!inputHostFreshNeutralReady(true,raw,0));g_inputHostTransportFault=false;
    const auto msiMachine=g_machineIdentity;const auto priorStage=g_inputHostPublicationStage.load();
    auto protectedBranch=[&](const char* label,bool msiCleanupExpected=false){
        check(label,!msiControllerRepairScope());
        XINPUT_GAMEPAD residue{};residue.sThumbLY=-1;reset();for(int i=0;i<3;++i)inputHostFreshNeutralReady(true,residue,0);
        check("protected branch keeps original raw-zero admission",!g_inputHostFreshNeutralAdmitted&&g_inputHostFreshNeutralFrames==0);
        residue={};check("protected branch retains original exact-neutral admission",three(residue));
        check("protected live source policy unchanged",inputHostPhysicalPublicationSourceAdmitted(true)&&!inputHostPhysicalPublicationSourceAdmitted(false));
        check(msiCleanupExpected?"authorized unarmed MSI keeps merged neutral-only cleanup":"ROG/common no-source neutral exception is NOT introduced",inputHostPhysicalPublicationSourceAdmitted(false,true)==msiCleanupExpected&&msiControllerNeutralCleanupAllowed(true)==msiCleanupExpected);
        json receipt={{"connectedMask",0},{"frames",3},{"status","admitted"}};
        check("protected base receipt has exact original field schema",inputHostFreshNeutralAdmissionReceipt(0)==receipt);
        json idle={{"baseline",true}};const auto before=idle;msiControllerRepairAppendIdleFields(idle);
        check("protected common idle receipt has no new fields",idle==before);
        g_inputHostPublicationStage.store(InputHostPublicationStage::Unknown);inputHostNotePublicationStage(InputHostPublicationStage::Accepted);
        check("protected producer creates no new stage trace",g_inputHostPublicationStage.load()==InputHostPublicationStage::Unknown);
    };
    for(const auto family:{YmccFamily::AsusRogAlly,YmccFamily::AsusRogAllyX,YmccFamily::AsusRogAllyClassic,YmccFamily::AsusRogAllyXClassic,YmccFamily::Unknown}){
        g_machineIdentity.family=family;g_machineIdentity.manufacturer="ASUSTeK COMPUTER INC.";g_machineIdentity.product="protected-common";protectedBranch("ROG/generic cannot enter MSI repair scope");
    }
    g_machineIdentity=msiMachine;g_machineIdentity.product="UNREGISTERED";protectedBranch("unknown MSI board remains outside repair scope");
    g_machineIdentity=msiMachine;g_msiDinputRouteDinputArmed=false;protectedBranch("ordinary unarmed MSI keeps raw-zero and live-source policy",true);
    g_msiDinputRouteDinputArmed=true;g_machineIdentity=msiMachine;g_inputHostPublicationStage.store(priorStage);
    json msiIdle={{"baseline",true}};msiControllerRepairAppendIdleFields(msiIdle);
    check("MSI-only idle fields are present in actual scoped builder",msiIdle.contains("freshNeutralAdmitted")&&msiIdle.contains("latestPublicationStage"));
    check("MSI-only receipt records canonical rule",inputHostFreshNeutralAdmissionReceipt(0).contains("neutralPolicy"));
    check("MSI-only cleanup exception requires semantic neutral",msiControllerNeutralCleanupAllowed(true)&&!msiControllerNeutralCleanupAllowed(false));
    raw.sThumbLY=-1;const auto originalRaw=raw;bool analogAdmitted=false;const auto wire=inputHostStabilizeAnalogPad(raw,analogAdmitted);check("analog wire adapter still returns raw input unchanged",std::memcmp(&wire,&originalRaw,sizeof(raw))==0&&wire.sThumbLY==-1);
    if(replayPath){std::ifstream in{fspath::path(replayPath)};json fixture;in>>fixture;
        check("new real return fixture contains observations",fixture.at("episodes").size()>=2);
        for(const auto& e:fixture.at("episodes")){
            ymcc::msi::ModeRecoveryFacts f{true,true,true,false,true,false,e.at("hostEpoch"),e.at("hostEpoch"),e.at("hostEpoch"),100,100};
            const auto waited=e.at("waitedMs").get<ULONGLONG>();
            check("real observed PID1901 return gets bounded early reassert",e.at("productId")==6401&&ymcc::msi::observedXinputReturnAdmitted(f)&&msiDinputRouteWindowDecide(true,false,false,waited,true)==MsiDinputRouteWindowAction::Resend);
            check("real episode cannot extend retry budget",msiDinputRouteWindowDecide(true,false,true,waited,true)==MsiDinputRouteWindowAction::Poll);
        }
        check("new real fixture contains prepared-zero-submit residue windows",fixture.at("neutralBlockedWindows").size()>0);
        for(const auto& w:fixture.at("neutralBlockedWindows")){
            const auto& p=w.at("physical");XINPUT_GAMEPAD v{};v.wButtons=p.at("buttons").get<WORD>();v.sThumbLX=p.at("sticks").at(0);v.sThumbLY=p.at("sticks").at(1);v.sThumbRX=p.at("sticks").at(2);v.sThumbRY=p.at("sticks").at(3);v.bLeftTrigger=p.at("triggers").at(0);v.bRightTrigger=p.at("triggers").at(1);
            check("actual new-log residue opens production neutral gate unchanged",w.at("attempts")==0&&w.at("accepted")==0&&three(v));
        }
        report<<"realReturnEpisodes="<<fixture.at("episodes").size()<<" realBlockedNeutralWindows="<<fixture.at("neutralBlockedWindows").size()<<"\n";
    }
    g_machineIdentity=savedMachine;g_msiDinputRouteDinputArmed=savedRouteArmed;
    g_inputHostProcess=process;g_inputHostPrepared=prepared;g_inputHostNeutralized=neutralized;g_inputHostTransportFault=fault;g_inputHostFreshNeutralAdmitted=admitted;g_inputHostFreshNeutralFrames=frames;g_inputHostFreshNeutralMask=mask;g_inputHostLifecycle=lifecycle;
    report<<"cases="<<cases<<" failed="<<failed<<"\n";std::ofstream out(fspath::path(g_yemanccDataDir)/L"controller-fresh-neutral-native-entry.txt");out<<report.str();return failed==0&&out.good();
}
static bool msiKeyRepairBoundaryRegression() {
    unsigned cases=0,failed=0;std::ostringstream report;auto check=[&](const char* n,bool ok){++cases;failed+=!ok;report<<(ok?"PASS ":"FAIL ")<<n<<"\n";};
    const auto savedMachine=g_machineIdentity;const bool savedArmed=g_msiDinputRouteDinputArmed.load();const auto savedStage=g_inputHostPublicationStage.load();
    g_msiRearAudit.enable(true);
    for(const auto family:{YmccFamily::AsusRogAlly,YmccFamily::AsusRogAllyX,YmccFamily::AsusRogAllyClassic,YmccFamily::AsusRogAllyXClassic,YmccFamily::Unknown}){
        g_machineIdentity.family=family;g_machineIdentity.manufacturer="ASUSTeK COMPUTER INC.";g_machineIdentity.product="protected-common";g_msiDinputRouteDinputArmed=true;
        check("ROG/generic enters no MSI key class probe scope",!msiDedicatedKeyRepairScope());
        json row={{"baseline",true}};const auto before=row;msiControllerRepairAppendIdleFields(row);check("ROG/generic gets no rear audit fields",row==before);
        g_inputHostPublicationStage.store(InputHostPublicationStage::Unknown);inputHostNotePublicationStage(InputHostPublicationStage::FreshNeutralWait);check("ROG/generic records no new held-rear gate",g_msiRearAudit.take().heldRearStage[unsigned(InputHostPublicationStage::FreshNeutralWait)]==0);
    }
    g_machineIdentity.family=YmccFamily::MsiClaw;g_machineIdentity.manufacturer="MICRO-STAR INTERNATIONAL CO., LTD.";g_machineIdentity.product="UNREGISTERED";
    check("unknown MSI board cannot enter class probe scope",!msiDedicatedKeyRepairScope());
    g_machineIdentity.product="MS-1T91";g_msiDinputRouteDinputArmed=false;check("dedicated MSI keys remain independent of virtual route",msiDedicatedKeyRepairScope()&&!msiControllerRepairScope());
    g_msiDinputRouteDinputArmed=true;g_msiRearAudit.physical(9,0,0,0);g_msiRearAudit.physical(9,1,1,1);g_msiRearAudit.stage(unsigned(InputHostPublicationStage::FreshNeutralWait),1);g_msiRearAudit.submit(0,true);g_msiRearAudit.physical(9,0,0,0);
    json row;msiControllerRepairAppendIdleFields(row);const auto& rear=row.at("msiRearWindow");
    check("actual low-rate receipt preserves short M1 pulse",rear.at("pressEdgesM1M2").at(0)==1&&rear.at("releaseEdgesM1M2").at(0)==1);
    check("actual receipt distinguishes physical pulse from rear ACK",rear.at("physicalDownM1M2").at(0)==1&&rear.at("acceptedRearSlots").at(0)==0&&rear.at("acceptedRearSlots").at(1)==0);
    check("actual receipt names the suppressed gate",rear.at("heldRearDecisions").at("opening-fresh-neutral")==1);
    check("extra raw slots retain unknown semantics",rear.at("unassignedButtonIndices")==json::array({12,13,14}));
    check("MSI class capability is a base MSI event",nativeLifecycleEventIsBase("msi-wmi-source-capability"));
    const auto wg=g_msiWmiActiveGeneration.load();const bool we=g_msiWmiEnabled.load(),ws=g_msiWmiStop.load();const auto failedAt=g_msiWmiFailedAt.load();const auto state=g_msiWmiState.load();
    const auto pulse=g_msiWmiPulseMask.load(),until=g_msiWmiPulseUntil.load(),oem=g_oemKeyMaskMsi.load();
    const auto count=g_msiWmiClickCount,head=g_msiWmiClickHead;const auto clicks=g_msiWmiClicks;
    g_msiWmiEnabled=true;g_msiWmiStop=false;g_msiWmiActiveGeneration=10;g_msiWmiFailedAt=0;g_msiWmiState=2;g_msiWmiPulseMask=kOemBitQs;g_oemKeyMaskMsi=kOemBitQs;g_msiWmiClickCount=1;
    check("retired watcher cannot withdraw newer key source",msiWmiFinishKnownWatcher(9,true)==0&&g_msiWmiActiveGeneration.load()==10&&g_msiWmiState.load()==2&&g_msiWmiFailedAt.load()==0&&g_msiWmiClickCount==1);
    g_msiWmiActiveGeneration=0;g_msiWmiState=0;g_msiWmiClickCount=0;
    msiWmiFinishKnownWatcher(9,true);check("cancelled lookup cannot poison resumed source backoff",g_msiWmiFailedAt.load()==0&&g_msiWmiState.load()==0);
    g_msiWmiActiveGeneration=11;g_msiWmiState=1;g_msiWmiClickCount=1;
    check("current genuine failure clears only owned source",msiWmiFinishKnownWatcher(11,true)==1&&g_msiWmiActiveGeneration.load()==0&&g_msiWmiState.load()==3&&g_msiWmiFailedAt.load()!=0&&g_msiWmiPulseMask.load()==0&&g_oemKeyMaskMsi.load()==0);
    g_msiWmiActiveGeneration=12;g_msiWmiState=2;g_msiWmiFailedAt=0;msiWmiFinishKnownWatcher(12,false);check("normal watcher completion is not a new failure",g_msiWmiFailedAt.load()==0);
    g_msiWmiActiveGeneration=13;g_msiWmiEnabled=false;g_msiWmiState=0;msiWmiFinishKnownWatcher(13,true);check("disabled watcher cannot grant authority or retry debt",g_msiWmiFailedAt.load()==0&&g_msiWmiState.load()==0);
    g_msiWmiActiveGeneration=wg;g_msiWmiEnabled=we;g_msiWmiStop=ws;g_msiWmiFailedAt=failedAt;g_msiWmiState=state;g_msiWmiPulseMask=pulse;g_msiWmiPulseUntil=until;g_oemKeyMaskMsi=oem;g_msiWmiClickCount=count;g_msiWmiClickHead=head;g_msiWmiClicks=clicks;

    g_msiRearAudit.enable(false);g_machineIdentity=savedMachine;g_msiDinputRouteDinputArmed=savedArmed;g_inputHostPublicationStage=savedStage;
    report<<"cases="<<cases<<" failed="<<failed<<"\n";std::ofstream out(fspath::path(g_yemanccDataDir)/L"msi-key-boundary-native-entry.txt");out<<report.str();return failed==0&&out.good();
}
static bool msiRearExportReplayRegression(const wchar_t* replayPath) {
    unsigned cases=0,failed=0;std::ostringstream report;auto check=[&](const char* n,bool ok){++cases;failed+=!ok;report<<(ok?"PASS ":"FAIL ")<<n<<"\n";};
    // Exhaustive preservation of the pre-existing adapter, not a new mapping.
    for(unsigned raw=0;raw<4;++raw)for(unsigned caps=0;caps<4;++caps)for(unsigned armed=0;armed<4;++armed){
        const auto x=msiDinputBuildRearSample(raw&1?128:0,raw&2?128:0,caps&1,caps&2,armed&1,armed&2);
        const unsigned a=armed|(~raw&3u),bits=raw&caps&a;
        check("rear source mapping is unchanged for raw/cap/rearm truth table",x.rawBits==raw&&x.bits==bits&&x.armed15==bool(a&1)&&x.armed16==bool(a&2)&&x.keyMask==((bits&1?kOemBitM1:0)|(bits&2?kOemBitM2:0))&&x.displayBits==int(((bits&1)?2:0)|((bits&2)?1:0)));
    }
    const auto producerStates=g_oemProducerStates;const auto oldProfile=g_oemBackProfileId.load();
    std::array<uint64_t,kOemProducerCount> authorities;for(size_t i=0;i<kOemProducerCount;i++){authorities[i]=g_oemProducerAuthorities[i].load();g_oemProducerAuthorities[i]=0;}
    const auto oldMsiMask=g_oemKeyMaskMsiDinput.load(),oldKbMask=g_oemKeyMask.load();const int oldMsiBack=g_msiDinputBackBits.load(),oldKbBack=g_oemBackStateKb.load(),oldPhysicalBack=g_oemBackState.load();
    const auto oldMapL=g_oemRearMapToL5.load(),oldMapR=g_oemRearMapToR5.load();const bool enabled=g_msiDinput.enabled.load(),valid=g_msiDinputSourceValid.load(),cap15=g_msiDinput.hasBtn15.load(),cap16=g_msiDinput.hasBtn16.load(),mapping=g_oemBackMappingEnabled.load();const auto oldPersona=g_realStickTest.virtualTargetPersona;
    g_oemProducerStates={};g_oemBackProfileId=int(OemChordProfileId::Msi);g_oemBackState=0;g_oemRearMapToL5=g_oemRearMapToR5=0;g_msiDinput.enabled=true;g_msiDinputSourceValid=true;g_msiDinput.hasBtn15=true;g_msiDinput.hasBtn16=true;g_oemBackMappingEnabled=true;g_realStickTest.virtualTargetPersona="steamdeck";
    // Stale keyboard counterparts must not echo a source-qualified rear pulse.
    g_oemKeyMask=kOemBitM1|kOemBitM2;g_oemBackStateKb=3;
    if(replayPath){std::ifstream in{fspath::path(replayPath)};json data;in>>data;
        check("new target fixture contains actual rear frames",data.at("rearSamples").size()>=4);
        for(const auto& r:data.at("rearSamples")){
            const auto x=msiDinputBuildRearSample(r.at("raw15").get<BYTE>(),r.at("raw16").get<BYTE>(),r.at("cap15"),r.at("cap16"),r.at("armed15"),r.at("armed16"));
            check("actual exported raw frame preserves logical and historical display order",x.bits==r.at("publishedBits").get<unsigned>()&&x.displayBits==r.at("displayBits").get<int>());
            g_msiDinputBackBits=x.displayBits;g_oemKeyMaskMsiDinput=x.keyMask;
            check("actual OEM source merge suppresses stale keyboard counterpart",oemBackMergedKeyMask()==x.keyMask&&oemBackMergedDisplayState()==x.displayBits);
            check("actual rear producer feeds unchanged output slots",oemLiveBackBits()==static_cast<unsigned>(x.displayBits)&&msiBackkeyEffectiveBackBits(0,false)==x.displayBits);
            check("actual semantic neutral releases both slots",msiBackkeyEffectiveBackBits(0,true)==0);
            check("actual rear barrier clears both slots",msiBackkeyEffectiveBackBits(3ull<<16,false)==0);
            FocusFrameContent content;content.backButtons=msiBackkeyEffectiveBackBits(0,false);const auto frame=focusIsolationAssembleFrame(content);
            check("actual native frame builder retains replayed rear slots",frame.at("backButtons")==x.displayBits);
        }
        check("fixture includes failing real MSI WMI subscription receipts",!data.at("wmiClassFailures").empty());
        for(const auto& r:data.at("wmiClassFailures")){
            const auto hr=r.at("hresult").get<int32_t>();
            check("real invalid-class code does not become class availability",ymcc::msi::classDefinitelyAbsent(hr)&&!ymcc::msi::classObjectAdmitted(hr,true)&&!ymcc::msi::maySubscribeEventClass(ymcc::msi::EventClassState::Absent));
        }
        report<<"realRearFrames="<<data.at("rearSamples").size()<<" realWmiFailureReceipts="<<data.at("wmiClassFailures").size()<<"\n";
    }
    g_oemProducerStates=producerStates;for(size_t i=0;i<kOemProducerCount;i++)g_oemProducerAuthorities[i]=authorities[i];g_oemBackProfileId=oldProfile;g_oemKeyMaskMsiDinput=oldMsiMask;g_oemKeyMask=oldKbMask;g_msiDinputBackBits=oldMsiBack;g_oemBackStateKb=oldKbBack;g_oemBackState=oldPhysicalBack;g_oemRearMapToL5=oldMapL;g_oemRearMapToR5=oldMapR;g_msiDinput.enabled=enabled;g_msiDinputSourceValid=valid;g_msiDinput.hasBtn15=cap15;g_msiDinput.hasBtn16=cap16;g_oemBackMappingEnabled=mapping;g_realStickTest.virtualTargetPersona=oldPersona;
    report<<"cases="<<cases<<" failed="<<failed<<"\n";std::ofstream out(fspath::path(g_yemanccDataDir)/L"msi-rear-export-replay-native-entry.txt");out<<report.str();return failed==0&&out.good();
}
#include "msi_virtual_wake_regression.h"
#include "msi_mode_identity_regression.h"
#include "msi_controller_transition_logging_regression.h"
#include "msi_controller_cause_diagnostics_regression.h"
#include "msi_persona_mode_handoff_regression.h"
#include "msi_host_liveness_admission_regression.h"
#include "msi_explicit_rearm_regression.h"
#include "msi_late_hide_pending_regression.h"
#include "msi_blocked_start_preparation_regression.h"
#include "msi_visibility_wake_regression.h"
#include "msi_quarantine_cache_regression.h"
#include "msi_fake_sd_fence_protocol_regression.h"
int wmain(int argc, wchar_t** argv) {
    if (argc != 2 && argc != 3 && argc != 4 && argc != 5) return 2;
    // All old/new offline entry tests remain forbidden from metadata hardware I/O.
    g_msiControllerTraceInventoryForTest=[](){return json{{"known",false},{"scope","offline-test; no hardware queried"}};};
    g_msiControllerOfficialStackForTest=[](){return json{{"scope","offline-test; no process/service queried"},{"modeWriterProven",false}};};
    g_yemanccDataDir = argv[1];
    fspath::create_directories(g_yemanccDataDir);
    const bool fakeSdProtocol=msiFakeSdFenceProtocolRegression();
    const bool quarantineCache=msiQuarantineCacheRegression();
    const bool visibilityWake=msiVisibilityWakeDiagnosticsRegression();
    const bool blockedPreparation=msiBlockedStartPreparationRegression();
    const bool lateHidePending=msiLateHidePendingRegression();
    const bool explicitRearm=msiExplicitRearmRegression();
    const bool livenessAdmission=msiHostLivenessAdmissionRegression();
    const bool capability = msiG3eCapabilitySelfTest();
    const bool order = msiStartOrderSelfTest();
    const bool entry = msiStartOrderEntrySelfTest(0);
    const bool mailboxEntry=sensorStaMailboxEntryRegression();
    const bool idleEntry=controllerIdleNativeEntryRegression();
    const bool modeRecovery=msiModeRecoveryNativeRegression();
    const bool freshNeutral=controllerFreshNeutralNativeRegression(argc>=4?argv[3]:nullptr);
    const bool modeIdentity=msiModeIdentityBoundaryRegression();
    const bool transitionLogging=msiControllerTransitionLoggingRegression();
    const bool causeDiagnostics=msiControllerCauseDiagnosticsRegression();
    const bool virtualWake=msiVirtualWakeNativeRegression(argc==5?argv[4]:nullptr);
    const bool keys=msiKeyRepairBoundaryRegression();
    const bool rearReplay=msiRearExportReplayRegression(argc>=4?argv[3]:nullptr);
    const bool replay=argc>=3 ? replayMsiMotionEvidence(argv[2]) : true;
    const bool personaHandoff=msiPersonaModeHandoffRegression();
    std::ofstream result(fspath::path(g_yemanccDataDir) / L"msi-native-regression-result.txt");
    result << "fakeSdProtocol="<<fakeSdProtocol<<"\n" << "quarantineCache="<<quarantineCache<<"\n" << "visibilityWake="<<visibilityWake<<"\n" << "blockedPreparation="<<blockedPreparation<<"\n" << "lateHidePending="<<lateHidePending<<"\n" << "explicitRearm="<<explicitRearm<<"\n" << "livenessAdmission="<<livenessAdmission<<"\n" << "personaHandoff="<<personaHandoff<<"\n" << "capability=" << capability << "\nstartOrder=" << order
           << "\nstartOrderEntry=" << entry << "\nmailboxEntry=" << mailboxEntry << "\nidleEntry=" << idleEntry << "\nrearReplay="<<rearReplay<<"\nkeys="<<keys<<"\nfreshNeutral="<<freshNeutral<<"\ncauseDiagnostics="<<causeDiagnostics<<"\ntransitionLogging="<<transitionLogging<<"\nmodeIdentity="<<modeIdentity<<"\nvirtualWake="<<virtualWake<<"\nmodeRecovery="<<modeRecovery<<"\nreplay=" << replay << "\n";
    return fakeSdProtocol && quarantineCache && visibilityWake && blockedPreparation && lateHidePending && explicitRearm && livenessAdmission && personaHandoff && capability && order && entry && mailboxEntry && idleEntry && modeRecovery && freshNeutral && modeIdentity && transitionLogging && causeDiagnostics && virtualWake && keys && rearReplay && replay && result.good() ? 0 : 3;
}
