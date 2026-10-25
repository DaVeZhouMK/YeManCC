// Calls the actual button/touch output functions. UI input APIs are stubs;
// no application window, device, timers or system input is created.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
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
int wmain(int argc,wchar_t** argv){
 if(argc<2)return 2;const bool baseline=argc>2;Json cases=Json::array();bool ok=true;
 auto check=[&](const char* name,bool pass){cases.push_back({{"name",name},{"ok",pass}});ok&=pass;};
 reset();const HWND button=reinterpret_cast<HWND>(uintptr_t{2});
 handleButtonPointer(button,1,WM_POINTERDOWN,41,30,20);
 const auto generationBefore=standaloneGeneration.load();const auto sinkBefore=sinkCalls;
 for(int i=0;i<10000;++i)handleButtonPointer(button,1,WM_POINTERUPDATE,41,30+(i%3),20);
 const auto cancellations=standaloneGeneration.load()-generationBefore;
 check("held-own-ui-moves-do-not-repeat-mapping-cancel",cancellations==(baseline?10000u:0u));
 check("same-held-button-does-not-repeat-haptic-work",sinkCalls==sinkBefore&&pulses==1&&lastContacts==(1u<<3)&&lastStrength==20);
 handleButtonPointer(button,1,WM_POINTERUP,41,31,20);
 check("own-ui-UP-releases-feedback-without-game-input",lastContacts==0&&screenButtonMask.load()==0&&injectedCalls==0);
 reset();fixtureForeground=reinterpret_cast<HWND>(uintptr_t{3});
 handleButtonPointer(button,1,WM_POINTERDOWN,51,30,20);check("game-foreground-press-publishes-mapped-bit",buttonSnapshot()==1&&buttonContacts[1]==51);
 fixtureForeground=owner.load();const auto takeover=standaloneGeneration.load();
 handleButtonPointer(button,1,WM_POINTERUPDATE,51,31,20);
 check("focus-return-revokes-held-mapping-once",standaloneGeneration.load()==takeover+1&&buttonSnapshot()==0&&buttonContacts[1]==0&&buttonFeedbackContacts[1]==51);
 handleButtonPointer(button,1,WM_POINTERUP,51,31,20);
 check("revocation-keeps-physical-feedback-through-UP",pulses==1&&lastContacts==0&&injectedCalls==0);
 for(int state=0;state<4;++state){
  reset();const auto g=standaloneGeneration.load();
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
