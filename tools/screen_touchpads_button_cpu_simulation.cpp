// Calls the actual button/touch output functions. UI input APIs are stubs;
// no application window, device, timers or system input is created.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#ifdef YMCC_SCREEN_SNAPSHOT_CPU_FIXTURE
static ULONGLONG snapshotVirtualTick=1000;
static unsigned snapshotClockCalls=0;
static ULONGLONG WINAPI fixtureSnapshotClock(){++snapshotClockCalls;return snapshotVirtualTick;}
#define GetTickCount64 fixtureSnapshotClock
#endif
static HWND fixtureForeground=reinterpret_cast<HWND>(uintptr_t{1});
static unsigned injectedCalls=0,postedMessages=0,scanCalls=0;
static BOOL WINAPI fixtureClientRect(HWND,LPRECT r){*r={0,0,100,60};return TRUE;}
static HWND WINAPI fixtureGetForeground(){return fixtureForeground;}
static UINT WINAPI fixtureSendInput(UINT count,LPINPUT,int){++injectedCalls;return count;}
static UINT WINAPI fixtureMapKey(UINT key,UINT){++scanCalls;return key;}
static BOOL WINAPI fixturePostMessage(HWND,UINT,WPARAM,LPARAM){++postedMessages;return TRUE;}
#define GetClientRect fixtureClientRect
#define GetForegroundWindow fixtureGetForeground
#define SendInput fixtureSendInput
#define MapVirtualKeyW fixtureMapKey
#define PostMessageW fixturePostMessage
#include "screen_touchpads.h"
#undef GetClientRect
#undef GetForegroundWindow
#undef SendInput
#undef MapVirtualKeyW
#undef PostMessageW
#ifdef YMCC_SCREEN_SNAPSHOT_CPU_FIXTURE
#undef GetTickCount64
#endif
#include <iostream>
using namespace ymcc::screenpads;
static unsigned sinkCalls=0,pulses=0,lastContacts=0;static int lastStrength=0;
static void sink(unsigned contacts,bool pulse,int strength){++sinkCalls;pulses+=pulse?1:0;lastContacts=contacts;lastStrength=strength;}
static void reset(){
 core=Core{};core.config.enabled=true;core.config.left=core.config.right=Mode::Deck;core.config.specialMask=3;core.config.rearMask=15;
 dryRun=false;initialized=false;owner=reinterpret_cast<HWND>(uintptr_t{1});fixtureForeground=owner.load();
 activeProfile=1;appliedProfile=1;controlTargetEnabled=true;controlEdgeEnabled=false;available=true;enabled=true;
 buttonContacts={};buttonArmed={};buttonFeedbackContacts={};buttonFeedbackPressed={};buttonHapticContacts=0;screenButtonMask=0;
 standalonePending=0;standaloneStatus=StandaloneStatus::Idle;autoHide={};sentKeys=0;sentClick=false;inputError=0;
 setTouchHapticPulseSink(sink);sinkCalls=pulses=lastContacts=injectedCalls=postedMessages=scanCalls=0;lastStrength=0;
}
#ifdef YMCC_SCREEN_SNAPSHOT_CPU_FIXTURE
static bool snapshotPowerAllowed=true;
static unsigned snapshotPowerReads=0;
static bool snapshotPower(){++snapshotPowerReads;return snapshotPowerAllowed;}
static int snapshotOnly(const wchar_t* report,bool repairOnly=false){
 Json cases=Json::array();bool ok=true;
 auto check=[&](const char* name,bool pass){cases.push_back({{"name",name},{"ok",pass}});ok&=pass;};
 auto prepare=[&]{reset();owner=nullptr;nativeMode=true;psAvailable=false;nativeTransport={};normalizedProjections={};feedbackOnlyContacts.cancel();
  inputPowerAllowed=snapshotPower;snapshotPowerAllowed=true;snapshotPowerReads=0;snapshotVirtualTick=1000;snapshotClockCalls=0;sizes={SIZE{200,200},SIZE{200,200}};};
 auto down=[&](int side,UINT32 id,float x,float y){core.event(side,id,0,x,y,200,200,snapshotVirtualTick,snapshotVirtualTick);};
 auto neutral=[](Snapshot s){return !s.present&&!s.left.down&&!s.right.down&&!s.left.click&&!s.right.click;};
 NativeTransportObservation ready{1,true};
 unsigned idleCalls=0,swipeCalls=0;Snapshot s{};
 if(!repairOnly){
 prepare();snapshot(&ready);const auto readyCalls=snapshotClockCalls;snapshotClockCalls=0;
 bool empty=true;for(int i=0;i<10000;++i)empty&=neutral(snapshot(&ready));idleCalls=snapshotClockCalls;
 check("empty-native-snapshot-needs-no-clock",empty&&idleCalls==0);
 check("empty-read-keeps-current-power-and-transport-observation",snapshotPowerReads==10001&&nativeTransport.observed&&nativeTransport.changes==1&&readyCalls==1&&core.nativeReadyAt==1000);
 // A control build stops at the necessary original assertion, without repeating the remaining groups.
#ifdef YMCC_SCREEN_SNAPSHOT_CPU_CONTROL
 if(!ok){const Json r={{"ok",false},{"cases",cases},{"idleClockCalls",idleCalls},{"actualInputCalls",injectedCalls},{"realInputInjected",injectedCalls},{"realWindowsCreated",0}};std::ofstream out(std::filesystem::path(report),std::ios::binary);out<<r.dump(2);return 3;}
#endif
 down(0,1,40,70);snapshotClockCalls=0;bool moving=true;
 for(int i=0;i<10000;++i){const float x=40.f+(i%100);core.event(0,1,1,x,70,200,200,++snapshotVirtualTick,snapshotVirtualTick);const auto s=snapshot(&ready);const auto expected=normalizeContact(Contact{true,false,(short)x,70,0},200,200);moving&=s.present&&s.left.down&&!s.left.click&&s.left.x==expected.x&&s.left.y==expected.y&&s.left.pressure==0;}
 swipeCalls=snapshotClockCalls;
 check("native-swipe-keeps-coordinates-without-clock",moving&&swipeCalls==0);
 core.event(0,1,2,170,70,200,200,++snapshotVirtualTick,snapshotVirtualTick);check("swipe-release-is-neutral",neutral(snapshot(&ready)));
 prepare();snapshot(&ready);down(0,2,30,40);snapshotVirtualTick=1010;core.event(0,2,2,30,40,200,200,1010,1010);snapshotVirtualTick=5000;
 s=snapshot(&ready);check("delayed-tap-still-publishes-one-pressed-report",s.left.down&&s.left.click&&s.left.pressure==32767&&core.clicks[0].published);
 check("delayed-tap-clears-on-next-read",neutral(snapshot(&ready))&&!core.clicks[0].active);
 prepare();snapshot(&ready);down(0,3,30,40);snapshotVirtualTick=1010;core.event(0,3,2,30,40,200,200,1010,1010);snapshotClockCalls=0;s=snapshot(&ready);
 check("tap-first-read-uses-clock-and-publishes",s.left.click&&snapshotClockCalls==1);snapshotVirtualTick=1069;check("tap-stays-pressed-before-deadline",snapshot(&ready).left.click);
 snapshotVirtualTick=1070;check("tap-exact-deadline-releases",neutral(snapshot(&ready))&&!core.clicks[0].active);snapshotClockCalls=0;snapshot(&ready);check("expired-tap-returns-to-clock-free-idle",snapshotClockCalls==0);
 prepare();snapshot(&ready);down(0,4,30,40);snapshotVirtualTick=1010;core.event(0,4,2,30,40,200,200,1010,1010);down(0,5,100,100);s=snapshot(&ready);check("new-swipe-does-not-inherit-undelivered-tap",s.left.down&&!s.left.click&&!core.clicks[0].active);
 prepare();snapshot(&ready);down(0,6,40,50);down(1,7,80,90);NativeTransportObservation replaced{2,true};snapshotVirtualTick=2000;
 check("replacement-target-discards-two-held-native-contacts",neutral(snapshot(&replaced))&&nativeTransport.discardedGestures==2&&core.nativeReadyAt==2000);
 check("replacement-keeps-physical-feedback-until-release",feedbackOnlyContacts.mask()==3);
 core.event(0,6,1,90,90,200,200,2001,2001);check("late-update-cannot-resurrect-replaced-contact",neutral(snapshot(&replaced)));
 core.event(0,8,0,60,60,200,200,1999,2000);check("pre-ready-down-is-rejected",neutral(snapshot(&replaced)));
 core.event(0,8,0,60,60,200,200,2000,2000);check("fresh-down-on-new-target-is-accepted",snapshot(&replaced).left.down);
 NativeTransportObservation blocked{2,false};snapshotClockCalls=0;check("target-not-ready-clears-output-with-one-withdrawal-clock",neutral(snapshot(&blocked))&&core.nativeOutputBlocked&&!core.pads[0].down&&snapshotClockCalls==1);
 snapshotVirtualTick=3000;check("ready-after-loss-needs-fresh-contact",neutral(snapshot(&replaced))&&!core.nativeOutputBlocked&&core.nativeReadyAt==3000);
 prepare();NativeTransportObservation zeroEpoch{0,true};check("zero-epoch-does-not-grant-input",neutral(snapshot(&zeroEpoch))&&core.nativeOutputBlocked&&!nativeTransport.last.ready);
 }
 prepare();snapshot(&ready);down(0,9,40,50);snapshotPowerAllowed=false;snapshotClockCalls=0;if(!repairOnly)check("closed-common-power-refuses-held-output",neutral(snapshot(&ready))&&snapshotClockCalls==0);snapshotPowerAllowed=true;releaseMappedState();check("power-release-path-clears-pending-state",neutral(snapshot(&ready))&&!core.pads[0].down&&!core.clicks[0].active);
 prepare();snapshot(&ready);down(0,10,40,50);activeProfile=2;if(!repairOnly)check("profile-transition-is-neutral",neutral(snapshot(&ready)));activeProfile=1;releaseMappedState();check("profile-cancel-does-not-leave-a-contact",neutral(snapshot(&ready)));
 if(!repairOnly){
 prepare();snapshot(&ready);down(0,11,40,50);core.config.masterEnabled=false;check("master-off-is-neutral",neutral(snapshot(&ready)));core.config.masterEnabled=true;core.config.enabled=false;check("pad-off-is-neutral",neutral(snapshot(&ready)));
 }
 prepare();snapshot(&ready);core.config.layout=Layout::Single;core.config.single=Mode::DualSense;available=false;psAvailable=true;activeProfile=appliedProfile=2;
 down(0,12,40,50);down(1,13,80,100);snapshotClockCalls=0;s=snapshot(&ready);if(!repairOnly)check("single-ps5-keeps-two-contacts-and-pressure",s.left.down&&s.right.down&&s.left.pressure==8192&&s.right.pressure==8192&&snapshotClockCalls==0);
 core.config.layout=Layout::Dual;core.config.left=core.config.right=Mode::DualSense;s=snapshot(&ready);check("dual-ps5-retains-half-projection",s.left.x<0&&s.right.x>0);
 core.config.left=Mode::Mouse;s=snapshot(&ready);check("mixed-direct-mouse-is-not-native-contact",!s.left.down&&s.right.down);
 releaseMappedState();if(!repairOnly)check("final-native-cancel-is-neutral",neutral(snapshot(&ready)));inputPowerAllowed=nullptr;setTouchHapticPulseSink(nullptr);
 check("no-real-input-window-or-cursor-work",injectedCalls==0&&cursorQueries==0&&cursorSyncs==0&&cursorShows==0);
 const Json r={{"ok",ok},{"selector",repairOnly?"--snapshot-repair-only":"--snapshot-only"},{"cases",cases},{"iterationsPerHotPath",10000},{"idleClockCalls",idleCalls},{"swipeClockCalls",swipeCalls},{"actualInputCalls",injectedCalls},{"realInputInjected",injectedCalls},{"realWindowsCreated",0},{"realWindowsCreated",0},{"cpuPercentMeasured",false}};
 std::ofstream out(std::filesystem::path(report),std::ios::binary);out<<r.dump(2);std::cout<<"SNAPSHOT_CPU checks="<<cases.size()<<" ok="<<ok<<" idleClock="<<idleCalls<<" swipeClock="<<swipeCalls<<"\n";return ok&&out.good()?0:3;
}
#endif
int wmain(int argc,wchar_t** argv){
#ifdef YMCC_SCREEN_SNAPSHOT_CPU_FIXTURE
 if(argc!=3 || (std::wstring(argv[2])!=L"--snapshot-only" && std::wstring(argv[2])!=L"--snapshot-repair-only"))return 2;
 return snapshotOnly(argv[1],std::wstring(argv[2])==L"--snapshot-repair-only");
#endif
 if(argc<2 || argc>3 || (argc==3 && std::wstring(argv[2])!=L"baseline"))return 2;const bool baseline=argc>2;Json cases=Json::array();bool ok=true;
 auto check=[&](const char* name,bool pass){cases.push_back({{"name",name},{"ok",pass}});ok&=pass;};
 reset();activeProfile=appliedProfile=2;controlEdgeEnabled=true;const HWND button=reinterpret_cast<HWND>(uintptr_t{2});
 handleButtonPointer(button,1,WM_POINTERDOWN,41,30,20);
 const auto generationBefore=standaloneGeneration.load();const auto sinkBefore=sinkCalls;
 for(int i=0;i<10000;++i)handleButtonPointer(button,1,WM_POINTERUPDATE,41,30+(i%3),20);
 const auto cancellations=standaloneGeneration.load()-generationBefore;
 check("held-own-ui-moves-do-not-repeat-mapping-cancel",cancellations==(baseline?10000u:0u));
 check("same-held-button-does-not-repeat-haptic-work",sinkCalls==sinkBefore&&pulses==1&&lastContacts==(1u<<3)&&lastStrength==touchFeedbackStrength(core.config.touchFeedback));
 handleButtonPointer(button,1,WM_POINTERUP,41,31,20);
 check("own-ui-UP-releases-feedback-without-game-input",lastContacts==0&&screenButtonMask.load()==0&&injectedCalls==0);
 reset();activeProfile=appliedProfile=2;controlEdgeEnabled=true;fixtureForeground=reinterpret_cast<HWND>(uintptr_t{3});
 handleButtonPointer(button,1,WM_POINTERDOWN,51,30,20);check("game-foreground-press-publishes-mapped-bit",buttonSnapshot()==1&&buttonContacts[1]==51);
 fixtureForeground=owner.load();const auto takeover=standaloneGeneration.load();
 handleButtonPointer(button,1,WM_POINTERUPDATE,51,31,20);
 check("focus-return-revokes-held-mapping-once",standaloneGeneration.load()==takeover+1&&buttonSnapshot()==0&&buttonContacts[1]==0&&buttonFeedbackContacts[1]==51);
 handleButtonPointer(button,1,WM_POINTERUP,51,31,20);
 check("revocation-keeps-physical-feedback-through-UP",pulses==1&&lastContacts==0&&injectedCalls==0);
 for(int state=0;state<4;++state){
  reset();activeProfile=appliedProfile=2;controlEdgeEnabled=true;const auto g=standaloneGeneration.load();
  if(state==0)standalonePending=g;
  if(state==1)standaloneStatus=StandaloneStatus::QueueUnavailable;
  if(state==2){buttonContacts[0]=88;buttonArmed[0]=true;}
  if(state==3)screenButtonMask=4;
  handleButtonPointer(button,1,WM_POINTERUPDATE,66,30,20);
  check("dirty-pending-or-held-state-is-revoked",standaloneGeneration.load()==g+1&&standalonePending.load()==0&&standaloneStatus.load()==StandaloneStatus::Idle&&buttonContacts[0]==0&&screenButtonMask.load()==0);
 }
 reset();buttonContacts[2]=70;buttonArmed[2]=true;screenButtonMask=2;
 handleButtonCaptureLoss(2,71);check("wrong-pointer-cancel-keeps-held-button",buttonContacts[2]==70&&screenButtonMask.load()==2);
 handleButtonCaptureLoss(2,70);check("matching-pointer-cancel-releases-held-button",buttonContacts[2]==0&&screenButtonMask.load()==0);
 reset();Action held;held.hapticContacts=1;held.hapticPulses=1;output(held);held.hapticPulses=0;
 for(int i=0;i<10000;++i)output(held);
 check("native-only-output-skips-system-input-and-scan-mapping",injectedCalls==0&&scanCalls==0&&sinkCalls==1&&pulses==1);
 output({});check("no-input-early-return-still-delivers-haptic-release",sinkCalls==2&&lastContacts==0);
 reset();Action key;key.keys=1;output(key);for(int i=0;i<10000;++i)output(key);output({});
 check("repeated-key-state-emits-only-press-and-release",injectedCalls==2&&scanCalls==2&&sentKeys==0);
 reset();Action mouse;mouse.dx=4;mouse.dy=-2;output(mouse);Action click;click.tap=true;output(click);
 check("mouse-movement-and-tap-still-reach-input-adapter",injectedCalls==2&&scanCalls==0&&!sentClick);
 const Json result={{"ok",ok},{"baseline",baseline},{"cases",cases},{"heldMoveCount",10000},{"mappingCancellationsDuringHeldMoves",cancellations},{"realInputInjected",0},{"realWindowsCreated",0},{"cpuPercentMeasured",false}};
 std::ofstream out(std::filesystem::path(argv[1]),std::ios::binary);out<<result.dump(2);std::cout<<"BUTTON_CPU_SIMULATION ok="<<ok<<" cases="<<cases.size()<<" cancellations="<<cancellations<<"\n";return ok&&out.good()?0:3;
}
