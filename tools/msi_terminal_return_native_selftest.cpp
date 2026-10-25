// Pure scenario runner around actual production return handlers. All HID open,
// mode writes and feedback-stop are intercepted by YMCC_FAULT_INJECT. No wWinMain.
#include <iostream>
#include "../native/main.cpp"
static unsigned checks=0,failures=0;
static void check(const char* name,bool good){++checks;if(!good)++failures;std::cout<<(good?"PASS ":"FAIL ")<<name<<"\n";}
static int calls(const char* name){return int(std::count(g_startEntrySeam.trace.begin(),g_startEntrySeam.trace.end(),name));}
static void seed(){
    g_machineIdentity.family=YmccFamily::MsiClaw;g_machineIdentity.manufacturer="MICRO-STAR INTERNATIONAL CO., LTD.";
    g_machineIdentity.product="MS-1T91";g_machineIdentity.deviceClass="HandheldCompanion.Devices.ClawCG3EM";
    g_machineIdentity.attempted=true;g_machineIdentity.resolved=true;
    g_inputHostEpochMirror=4;g_inputHostExitFence=false;g_exitRequested=false;
    g_powerGeneration=7;g_powerLifecycle=PowerLifecycle::Ready;g_hardwareWriteGate=true;
    g_realStickTest.enabled=true;g_realStickTest.virtualTargetEnabled=false;
    g_inputHostProcess=nullptr;g_inputHostPrepared=false;
    g_msiDinputRouteDinputArmed=false;g_msiDinputRouteConfirmed=false;g_msiDinputSourceValid=false;
    g_msiDinputReturnOwed=true;g_msiMode1TerminalAttempted=false;g_msiFeedbackModeGeneration=9;
    g_msiDinput.pad=nullptr;g_msiDinput.productId=0;g_msiDinput.mainInterfaceMatched=false;
    g_msiTerminalReturnPlan.cancel();g_msiTerminalReturnPending=false;
    g_startEntrySeam=YmccStartEntrySeam{};g_startEntrySeam.active=true;
    g_startEntrySeam.modeDeviceMissing=true;g_startEntrySeam.modeReturnNow=100000;
}
static void initial(){check("initial missing interface writes nothing",!msiClawRestoreXInputMode("input-host-stop")&&calls("mode-write:1")==0);check("initial no-device schedules owned bounded return",g_msiTerminalReturnPending&&g_msiTerminalReturnPlan.active());}
static void tick(unsigned long long t){g_startEntrySeam.modeReturnNow=t;msiTerminalReturnMaintenance(t);}
int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 2;g_yemanccDataDir=argv[1];fspath::create_directories(g_yemanccDataDir);
    seed();initial();const auto deadline=g_msiTerminalReturnPlan.deadline();
    msiClawRestoreXInputMode("input-host-stop");check("duplicate terminal call cannot extend original deadline",g_msiTerminalReturnPlan.deadline()==deadline&&calls("mode-discovery")==1);
    tick(100499);check("worker before due performs no lookup",calls("mode-discovery")==1);
    for(unsigned n=1;n<=10;++n)tick(100000+500*n);
    check("five seconds of absent transport sends zero mode writes",calls("mode-write:1")==0&&g_msiTerminalReturnPlan.probes()==10);
    g_startEntrySeam.modeDeviceMissing=false;
    // Simulate a physical DInput binding returning during the unhide cycle.
    g_msiDinput.productId=0x1902;g_msiDinput.mainInterfaceMatched=true;g_msiDinputSourceValid=true;
    tick(105500);
    check("late command interface gets one mode1 attempt",calls("mode-write:1")==1);
    check("rebound local source released before restore",msiDinputDeviceTargetAbsent());
    check("write receipt never clears return debt",g_msiDinputReturnOwed);
    check("actual write permanently ends pending plan",!g_msiTerminalReturnPending&&!g_msiTerminalReturnPlan.active());
    tick(106000);msiClawRestoreXInputMode("input-host-stop");check("maintenance and stop cannot duplicate committed write",calls("mode-write:1")==1&&calls("mode-write:2")==0);
    auto cancel=[&](const char* name,const std::function<void()>& change){seed();initial();const auto discovers=calls("mode-discovery");change();tick(100500);check(name,!g_msiTerminalReturnPending&&calls("mode-write:1")==0&&calls("mode-discovery")==discovers);g_inputHostProcess=nullptr;};
    cancel("new virtual target cancels old return",[]{g_realStickTest.virtualTargetEnabled=true;});
    cancel("new route cancels old return",[]{g_msiDinputRouteDinputArmed=true;});
    cancel("power generation changes cancel old return",[]{g_powerGeneration=8;});
    cancel("mode generation changes cancel old return",[]{g_msiFeedbackModeGeneration=10;});
    cancel("exit fence cancels old return",[]{g_inputHostExitFence=true;});
    cancel("suspend cancels old return",[]{g_powerLifecycle=PowerLifecycle::Suspended;});
    cancel("hardware gate cancels old return",[]{g_hardwareWriteGate=false;});
    cancel("actual new Host ownership cancels old return",[]{g_inputHostProcess=reinterpret_cast<HANDLE>(uintptr_t(0x55));});
    cancel("prepared target cancels old return",[]{g_inputHostPrepared=true;});
    cancel("cleared ownership debt cancels old return",[]{g_msiDinputReturnOwed=false;});
    cancel("unknown MSI board cannot use pending return",[]{g_machineIdentity.product="UNKNOWN";});
    cancel("ROG never executes MSI return",[]{g_machineIdentity.family=YmccFamily::AsusRogAlly;});
    cancel("GPD never executes MSI return",[]{g_machineIdentity.family=YmccFamily::Gpd;});
    seed();initial();tick(110001);check("deadline expires without another discovery",!g_msiTerminalReturnPending&&calls("mode-discovery")==1);
    seed();initial();tick(99999);check("clock rollback cancels pending return",!g_msiTerminalReturnPending&&calls("mode-discovery")==1);
    seed();initial();for(unsigned n=1;n<=20;++n)tick(100000+500*n);
    check("twenty absent lookups are a terminal budget",!g_msiTerminalReturnPending&&g_msiTerminalReturnPlan.probes()==20&&calls("mode-write:1")==0);
    g_startEntrySeam.modeDeviceMissing=false;tick(111000);check("late appearance cannot reopen exhausted episode",calls("mode-write:1")==0);
    seed();initial();g_startEntrySeam.modeDeviceMissing=false;g_startEntrySeam.modeWriteOk=false;tick(100500);tick(101000);
    check("failed actual write never retries",calls("mode-write:1")==1&&!g_msiTerminalReturnPending&&g_msiDinputReturnOwed);
    seed();g_startEntrySeam.modeEnumerationFailed=true;msiClawRestoreXInputMode("input-host-stop");check("SetupAPI failure is not proven absent transport",!g_msiTerminalReturnPending&&calls("mode-write:1")==0);
    seed();g_startEntrySeam.modeOpenDenied=true;msiClawRestoreXInputMode("input-host-stop");check("access denied is not missing transport",!g_msiTerminalReturnPending&&calls("mode-write:1")==0);
    seed();initial();g_startEntrySeam.modeOpenDenied=true;tick(100500);check("deferred discovery error ends plan",!g_msiTerminalReturnPending&&calls("mode-write:1")==0);
    seed();msiClawRestoreXInputMode("dinput-disabled");check("non-stop path does not create maintenance plan",!g_msiTerminalReturnPending);
    seed();g_realStickTest.virtualTargetEnabled=true;msiClawRestoreXInputMode("input-host-stop");check("target still wanted cannot schedule return",!g_msiTerminalReturnPending);
    seed();g_startEntrySeam.modeDeviceMissing=false;check("ordinary immediate mode1 unchanged",msiClawRestoreXInputMode("input-host-stop")&&calls("mode-write:1")==1&&!g_msiTerminalReturnPending);
    seed();initial();g_startEntrySeam.modeDeviceMissing=false;g_startEntrySeam.modeDiscoveryHook=[]{g_realStickTest.virtualTargetEnabled=true;};tick(100500);
    check("context changed while opening cannot commit stale mode1",calls("mode-write:1")==0&&!g_msiTerminalReturnPending);
    seed();initial();g_startEntrySeam.modeDeviceMissing=false;g_startEntrySeam.modeDiscoveryHook=[]{g_startEntrySeam.modeReturnNow=111000;};tick(100500);
    check("discovery past deadline cannot write",calls("mode-write:1")==0&&!g_msiTerminalReturnPending);
    seed();initial();g_startEntrySeam.modeDeviceMissing=false;msiClawRequestDInputMode();
    check("explicit new mode2 request cancels old plan",calls("mode-write:2")==1&&calls("mode-write:1")==0&&!g_msiTerminalReturnPending);
    check("return plan reuses always-on MSI lifecycle event",nativeLifecycleEventIsBase("msi-claw-gamepad-mode"));
    std::cout<<"checks="<<checks<<" failures="<<failures<<" hardwareCalls=0\n";return failures?3:0;
}