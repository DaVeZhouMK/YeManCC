#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
static ULONGLONG virtualTick=1000;
static unsigned inputCalls=0;
static ULONGLONG WINAPI simulationTick(){return virtualTick;}
static UINT WINAPI forbidInput(UINT,LPINPUT,int){++inputCalls;return 0;}
static BOOL WINAPI simulationCursor(PCURSORINFO c){c->flags=0;c->hCursor=nullptr;c->ptScreenPos={0,0};return TRUE;}
#define GetTickCount64 simulationTick
#define SendInput forbidInput
#define GetCursorInfo simulationCursor
#include "screen_touchpads.h"
#undef GetTickCount64
#undef SendInput
#undef GetCursorInfo
#include <iostream>
using namespace ymcc::screenpads;
static unsigned sinkCalls=0,pulses=0,lastContacts=0;
static int strength=0;
static void sink(unsigned c,bool p,int s){++sinkCalls;lastContacts=c;pulses+=p?1:0;strength=s;}
static bool ownUi=true;
static void reset(TouchFeedbackMode feedback,Mode mode=Mode::Mouse){
 core=Core{};core.config=profileDefault(0);core.config.enabled=true;core.config.left=core.config.right=mode;core.config.touchFeedback=feedback;
 feedbackOnlyContacts.cancel();buttonContacts={};buttonArmed={};buttonFeedbackContacts={};buttonFeedbackPressed={};buttonHapticContacts=0;screenButtonMask=0;
 dryRun=true;feedbackOnlyForegroundFixture=&ownUi;ownUi=true;initialized=false;enabled=true;activeProfile=0;appliedProfile=0;
 owner=nullptr;sizes={SIZE{200,200},SIZE{200,200}};available=true;psAvailable=true;
 sentKeys=0;sentClick=false;virtualTick=1000;setTouchHapticPulseSink(sink);sinkCalls=pulses=lastContacts=0;strength=0;
}
int wmain(int argc,wchar_t** argv){
 if(argc<2)return 2;const bool baseline=argc>2;Json cases=Json::array();bool ok=true;
 auto check=[&](const std::string& name,bool pass){cases.push_back({{"name",name},{"ok",pass}});ok&=pass;};
 reset(TouchFeedbackMode::Off);handlePointer(nullptr,0,WM_POINTERDOWN,1,20,20,0);
 unsigned motionChanges=0;bool visualCurrent=true;
 for(int i=0;i<10000;++i){virtualTick+=50;const float x=i%2?20.f:150.f;const float old=feedbackOnlyContacts.contacts[0].motion.anchorX;
  handlePointer(nullptr,0,WM_POINTERUPDATE,1,x,20,0);const auto& c=feedbackOnlyContacts.contacts[0];motionChanges+=c.motion.anchorX!=old;visualCurrent&=c.active&&c.id==1&&c.x==x&&c.y==20;}
 check("disabled-feedback-skips-motion-step-on-owned-moves",motionChanges==(baseline?10000u:0u));
 check("disabled-feedback-keeps-current-visual-contact",visualCurrent&&surfacePressed(0));
 check("disabled-feedback-never-requests-motor-work",sinkCalls==0&&pulses==0);
 handlePointer(nullptr,0,WM_POINTERUPDATE,99,180,30,0);check("wrong-pointer-still-cannot-move-contact",feedbackOnlyContacts.contacts[0].id==1&&feedbackOnlyContacts.contacts[0].x==20);
 handlePointer(nullptr,0,WM_POINTERUP,1,20,20,0);check("disabled-feedback-UP-clears-contact-and-press",feedbackOnlyContacts.mask()==0&&!surfacePressed(0));
 for(const auto tier:{TouchFeedbackMode::Minimal,TouchFeedbackMode::Medium,TouchFeedbackMode::Strong}){
  reset(tier);handlePointer(nullptr,0,WM_POINTERDOWN,2,20,20,0);check(std::string("enabled-down-pulse-")+touchFeedbackName(tier),pulses==1&&strength==touchFeedbackStrength(tier));
  virtualTick+=50;handlePointer(nullptr,0,WM_POINTERUPDATE,2,40,20,0);check(std::string("enabled-distance-pulse-")+touchFeedbackName(tier),pulses==2&&feedbackOnlyContacts.contacts[0].motion.anchorX==40);
  const auto count=pulses;for(int i=0;i<1000;++i){virtualTick+=50;handlePointer(nullptr,0,WM_POINTERUPDATE,2,40,20,0);}
  check(std::string("stationary-enabled-no-extra-pulses-")+touchFeedbackName(tier),pulses==count);
  handlePointer(nullptr,0,WM_POINTERUP,2,40,20,0);check(std::string("enabled-UP-releases-")+touchFeedbackName(tier),lastContacts==0&&feedbackOnlyContacts.mask()==0);
 }
 reset(TouchFeedbackMode::Off,Mode::Deck);ownUi=false;core.nativeOutputBlocked=true;
 handlePointer(nullptr,0,WM_POINTERDOWN,3,20,20,0);virtualTick+=60;handlePointer(nullptr,0,WM_POINTERUPDATE,3,100,100,0);
 check("blocked-native-output-retains-only-feedback-contact",feedbackOnlyContacts.mask()==1&&!core.pads[0].down&&sinkCalls==0);
 check("blocked-native-output-off-skips-distance-state",feedbackOnlyContacts.contacts[0].motion.anchorX==(baseline?100:20));
 handleCaptureLoss(nullptr,0,3);check("blocked-native-capture-loss-clears-owner",feedbackOnlyContacts.mask()==0);
 reset(TouchFeedbackMode::Off,Mode::DualSense);core.config.layout=Layout::Single;core.config.single=Mode::DualSense;
 handlePointer(nullptr,0,WM_POINTERDOWN,10,20,20,0);handlePointer(nullptr,0,WM_POINTERDOWN,11,80,20,0);
 virtualTick+=100;handlePointer(nullptr,0,WM_POINTERUPDATE,10,100,30,0);handlePointer(nullptr,0,WM_POINTERUPDATE,11,150,40,0);
 check("shared-surface-keeps-two-current-visual-points-while-muted",feedbackOnlyContacts.mask()==3&&feedbackOnlyContacts.contacts[0].x==100&&feedbackOnlyContacts.contacts[1].x==150&&sinkCalls==0);
 handlePointer(nullptr,0,WM_POINTERUP,10,100,30,0);check("one-release-keeps-other-muted-contact",feedbackOnlyContacts.mask()==2&&surfacePressed(0));
 auto next=core.config;next.touchFeedback=TouchFeedbackMode::Medium;configure(next,0);
 check("feedback-setting-change-releases-previous-gesture",feedbackOnlyContacts.mask()==0);
 handlePointer(nullptr,0,WM_POINTERDOWN,12,80,20,0);check("reenabled-new-gesture-starts-fresh-with-one-pulse",pulses==1&&strength==20);
 handlePointer(nullptr,0,WM_POINTERUP,12,80,20,0);check("all-routes-avoid-real-input",inputCalls==0);
 const Json result={{"ok",ok},{"baseline",baseline},{"cases",cases},{"disabledMoves",10000},{"disabledMotionStateChanges",motionChanges},{"realInputInjected",0},{"realWindowsCreated",0},{"processCpuMeasured",false}};
 std::ofstream out(std::filesystem::path(argv[1]),std::ios::binary);out<<result.dump(2);std::cout<<"FEEDBACK_OFF_SIMULATION ok="<<ok<<" checks="<<cases.size()<<" motionSteps="<<motionChanges<<"\n";return ok&&out.good()?0:3;
}
