// Executes real queued MSI handlers with the existing hardware-write seam.
// Never enters product wWinMain, opens a HID, starts InputHost, or changes SCM.
#include <iostream>
#include "../native/main.cpp"
static int countMode2(){return int(std::count(g_startEntrySeam.trace.begin(),g_startEntrySeam.trace.end(),"mode-write:2"));}
static void seed(){
    g_machineIdentity.family=YmccFamily::MsiClaw;g_machineIdentity.manufacturer="MICRO-STAR INTERNATIONAL CO., LTD.";
    g_machineIdentity.product="MS-1T91";g_machineIdentity.deviceClass="HandheldCompanion.Devices.ClawCG3EM";
    g_machineIdentity.attempted=true;g_machineIdentity.resolved=true;
    g_inputHostEpochMirror=4;g_inputHostExitFence=false;g_exitRequested=false;
    g_powerGeneration=7;g_powerLifecycle=PowerLifecycle::Ready;g_hardwareWriteGate=true;
    g_realStickTest.enabled=true;g_realStickTest.virtualTargetEnabled=true;
    g_msiDinputRouteDinputArmed=true;g_msiDinputRouteConfirmed=false;g_msiDinputSourceValid=false;
    g_msiDinputReturnOwed=true;g_msiFeedbackModeGeneration=9;
    {std::lock_guard<std::mutex> l(g_msiSampleGate.mutex);g_msiSampleGate.gen=12;}
    g_startEntrySeam=YmccStartEntrySeam{};g_startEntrySeam.active=true;g_startEntrySeam.modeWriteOk=true;
}
static std::string ticket(const char* why="dinput-route-window-resend"){
    const auto c=msiModeWriteCurrent();
    return ymcc::msi::modeTicket::encode({true,c.host,c.power,c.source,c.mode,why});
}
int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 2;g_yemanccDataDir=argv[1];fspath::create_directories(g_yemanccDataDir);
    unsigned cases=0,failures=0;
    auto check=[&](const char* text,bool good){++cases;if(!good)++failures;std::cout<<(good?"PASS ":"FAIL ")<<text<<"\n";};
    auto refuse=[&](const char* text,const std::function<void()>& change){seed();const auto q=ticket();change();msiDinputHandleModeWriteIntent(q);check(text,countMode2()==0);};
    seed();msiDinputHandleModeWriteIntent(ticket());check("current pending MSI timeout writes once through real handler",countMode2()==1);
    seed();const auto delayed=ticket();g_msiDinputRouteConfirmed=true;msiDinputHandleModeWriteIntent(delayed);
    check("delayed timeout cannot switch an already confirmed source",countMode2()==0);
    refuse("new canonical source cancels queued timeout",[]{g_msiDinputSourceValid=true;});
    refuse("virtual off cancels queued timeout",[]{g_realStickTest.virtualTargetEnabled=false;});
    refuse("disarmed route cancels queued timeout",[]{g_msiDinputRouteDinputArmed=false;});
    refuse("Host change cancels queued timeout",[]{g_inputHostEpochMirror=5;});
    refuse("power change cancels queued timeout with same Host",[]{g_powerGeneration=8;});
    refuse("source-gate close cancels queued timeout",[]{std::lock_guard<std::mutex> l(g_msiSampleGate.mutex);++g_msiSampleGate.gen;});
    refuse("another owned mode transaction cancels queued timeout",[]{g_msiFeedbackModeGeneration=10;});
    refuse("suspend cancels queued timeout",[]{g_powerLifecycle=PowerLifecycle::Suspended;});
    refuse("closed hardware-write gate cancels queued timeout",[]{g_hardwareWriteGate=false;});
    refuse("exit cancels queued timeout",[]{g_inputHostExitFence=true;});
    refuse("unknown MSI board cannot write mode2",[]{g_machineIdentity.product="UNREGISTERED";});
    refuse("ROG cannot enter MSI timeout handler",[]{g_machineIdentity.family=YmccFamily::AsusRogAlly;});
    refuse("GPD cannot enter MSI timeout handler",[]{g_machineIdentity.family=YmccFamily::Gpd;});
    seed();const auto early=ticket("dinput-route-observed-xinput-return");
    g_msiModeRecoveryConfirmedEpoch=4;g_msiModeRecoveryObservedEpoch=4;g_msiModeRecoveryObservedTick=GetTickCount64();
    msiDinputHandleModeWriteIntent(early);check("fresh physical-return evidence still authorizes bounded early repair",countMode2()==1);
    seed();const auto staleEarly=ticket("dinput-route-observed-xinput-return");g_msiModeRecoveryConfirmedEpoch=4;
    g_msiModeRecoveryObservedEpoch=4;g_msiModeRecoveryObservedTick=GetTickCount64()-2100;
    msiDinputHandleModeWriteIntent(staleEarly);check("stale physical-return evidence never writes mode2",countMode2()==0);
    seed();const auto foreignEarly=ticket("dinput-route-observed-xinput-return");g_msiModeRecoveryConfirmedEpoch=4;
    g_msiModeRecoveryObservedEpoch=4;g_msiModeRecoveryObservedTick=GetTickCount64();g_powerGeneration=8;
    msiDinputHandleModeWriteIntent(foreignEarly);check("fresh RawInput cannot validate an old-power ticket",countMode2()==0);
    seed();msiDinputHandleModeWriteIntent("msi-mode-write:dinput:4:dinput-route-window-resend");
    check("unbound legacy ticket rejected not silently migrated",countMode2()==0);
    seed();msiDinputHandleModeWriteIntent("msi-mode-write:v2:dinput:4junk:7:12:9:dinput-route-window-resend");
    check("partially numeric host never passes stoull prefix parsing",countMode2()==0);
    seed();const auto repeated=ticket();msiDinputHandleModeWriteIntent(repeated);msiDinputHandleModeWriteIntent(repeated);
    check("consumed ticket cannot issue second mode write",countMode2()==1);
    check("real handler refusal uses existing always-on mode event",nativeLifecycleEventIsBase("msi-claw-gamepad-mode"));
    std::cout<<"cases="<<cases<<" failures="<<failures<<" liveHardwareCalls=0\n";
    return failures?3:0;
}
