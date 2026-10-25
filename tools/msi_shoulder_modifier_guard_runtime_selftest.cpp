// Exact production adapter/global/call-site extracts with memory-only substitutes.
#include <array>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <string>
#include <stdexcept>
#include "json.hpp"
#include "msi_shoulder_modifier_guard.h"
using json=nlohmann::json;
using DWORD=std::uint32_t;
using WPARAM=std::uintptr_t;
using ULONGLONG=std::uint64_t;
constexpr DWORD VK_CONTROL=17,VK_MENU=18,VK_LCONTROL=162,VK_LMENU=164;
constexpr DWORD LLKHF_EXTENDED=1,LLKHF_LOWER_IL_INJECTED=2,LLKHF_INJECTED=16,LLKHF_UP=128;
constexpr WPARAM WM_KEYDOWN=0x100,WM_KEYUP=0x101,WM_SYSKEYDOWN=0x104,WM_SYSKEYUP=0x105;
struct KBDLLHOOKSTRUCT {DWORD vkCode=162,scanCode=0,flags=18,time=0;std::uintptr_t dwExtraInfo=0;};
struct XINPUT_GAMEPAD {std::uint16_t wButtons=0;};
struct Ledger {std::array<bool,512> physicalDown{};std::array<bool,256> seededPassed{};};
static Ledger g_oemKeyboardConsume;
static std::atomic<unsigned long long> g_inputHostEpochMirror{9};
static std::atomic<bool> g_sleepRecoveryOutputWanted{true},g_inputReady{true};
static std::atomic<bool> g_inputHostPowerAdmissionRevoked{false},g_exitRequested{false};
namespace fake {static DWORD error=12345;static std::uint64_t tick=1000,power=3;static bool known=true;}
static DWORD GetLastError() noexcept{return fake::error;}
static void SetLastError(DWORD value) noexcept{fake::error=value;}
static ULONGLONG GetTickCount64() noexcept{fake::error=8001;return fake::tick;}
static std::uint64_t currentPowerGeneration() noexcept{return fake::power;}
static bool msiClawKnownBoardAuthority() noexcept{return fake::known;}
struct StickConfig {bool enabled=true,virtualTargetEnabled=true;};
static StickConfig g_realStickTest;
enum class FocusDecision {Ready,SkipHostUnavailable};
struct FocusPlan {bool steamDeckTarget=true;FocusDecision decision=FocusDecision::Ready;unsigned long long virtualUiHostEpoch=9;};
#include "actual_adapter.inc"
#include "actual_publisher_call.inc"
static unsigned checks=0;
static json samples=json::array();
static void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
static void reset(){
 fake::error=12345;fake::tick=1000;fake::power=3;fake::known=true;
 g_inputHostEpochMirror=9;g_sleepRecoveryOutputWanted=true;g_inputReady=true;
 g_inputHostPowerAdmissionRevoked=false;g_exitRequested=false;g_oemKeyboardConsume={};g_realStickTest={};
 g_msiShoulderModifierSequence=0;g_msiShoulderModifierTick=0;g_msiShoulderModifierHostEpoch=0;
 g_msiShoulderModifierPowerGeneration=0;g_msiShoulderModifierButtons=0;g_msiShoulderModifierEligible=false;
 g_msiShoulderModifierCtrlBlocked=0;g_msiShoulderModifierAltBlocked=0;
}
static void publish(std::uint16_t buttons=0x0100){msiShoulderModifierPublish(true,XINPUT_GAMEPAD{buttons},9);}
static void observe(const char* name,bool expected,KBDLLHOOKSTRUCT key={},WPARAM message=WM_KEYDOWN){
 const auto ledger=g_oemKeyboardConsume;
 const auto beforeCtrl=g_msiShoulderModifierCtrlBlocked.load(),beforeAlt=g_msiShoulderModifierAltBlocked.load();
 fake::error=12345;
 ymcc::msi::shoulderModifiers::Observation observation{};
 const bool actual=msiShoulderModifierSuppress(key,message,&observation);
 const auto afterError=fake::error;
 const auto afterCtrl=g_msiShoulderModifierCtrlBlocked.load(),afterAlt=g_msiShoulderModifierAltBlocked.load();
 samples.push_back({{"case",name},{"expected",expected},{"blocked",actual},{"vk",key.vkCode},{"message",message},
  {"flags",key.flags},{"extraInfo",key.dwExtraInfo},{"now",fake::tick},
  {"sampleTick",g_msiShoulderModifierTick.load()},{"buttons",g_msiShoulderModifierButtons.load()},
  {"eligible",g_msiShoulderModifierEligible.load()},{"sequence",g_msiShoulderModifierSequence.load()},
  {"sampleEpoch",g_msiShoulderModifierHostEpoch.load()},{"currentEpoch",g_inputHostEpochMirror.load()},
  {"samplePower",g_msiShoulderModifierPowerGeneration.load()},{"currentPower",fake::power},
  {"outputWanted",g_sleepRecoveryOutputWanted.load()},{"inputReady",g_inputReady.load()},
  {"revoked",g_inputHostPowerAdmissionRevoked.load()},{"exit",g_exitRequested.load()},
  {"physicalSlots",ledger.physicalDown},{"startupSeed",ledger.seededPassed},
  {"lastErrorBefore",12345},{"lastErrorAfter",afterError},
  {"ctrlCountBefore",beforeCtrl},{"ctrlCountAfter",afterCtrl},{"altCountBefore",beforeAlt},{"altCountAfter",afterAlt}});
 check(actual==expected,name);
 check(afterError==12345,"suppress must preserve LastError");
 check(afterCtrl==beforeCtrl+(actual&&key.vkCode==162?1:0),"Ctrl counter only increments for suppressed Ctrl");
 check(afterAlt==beforeAlt+(actual&&key.vkCode==164?1:0),"Alt counter only increments for suppressed Alt");
 check(ledger.physicalDown==g_oemKeyboardConsume.physicalDown&&ledger.seededPassed==g_oemKeyboardConsume.seededPassed,
       "guard cannot clear physical/startup ledger or manufacture a release");
}
static void adapterCases(){
 reset();publish();observe("eligible-LB-fresh-Ctrl",true);
 KBDLLHOOKSTRUCT alt{};alt.vkCode=164;observe("LB-does-not-block-Alt",false,alt);
 publish(0x0200);observe("eligible-RB-fresh-Alt",true,alt);observe("RB-does-not-block-Ctrl",false);
 publish(0x0300);
 for(auto key:{KBDLLHOOKSTRUCT{},alt})for(auto message:{WM_KEYUP,WM_SYSKEYUP})
  observe("all-ups-pass",false,key,message);
 auto up=KBDLLHOOKSTRUCT{};up.flags|=LLKHF_UP;observe("up-flag-even-with-down-message",false,up);
 observe("syskeydown-Ctrl",true,{},WM_SYSKEYDOWN);observe("unknown-message",false,{},0);
 for(auto age:{0ull,63ull,64ull,65ull,500ull}){
  fake::tick=1000+age;observe("sample-age-boundary",age<=64);
 }
 fake::tick=999;observe("future-sample",false);fake::tick=0;publish();observe("missing-tick",false);
 reset();publish();++g_inputHostEpochMirror;observe("epoch-changed",false);
 reset();publish();++fake::power;observe("power-generation-changed",false);
 reset();publish();msiShoulderModifierPublish(false,XINPUT_GAMEPAD{0x0100},9);observe("source-lost-republished",false);
 reset();msiShoulderModifierPublish(true,XINPUT_GAMEPAD{0x0100},0);observe("missing-epoch",false);
 reset();publish();g_msiShoulderModifierSequence.fetch_add(1);observe("writer-in-progress-odd-sequence",false);
 for(unsigned i=0;i<4;++i){
  reset();publish();
  if(i==0)g_sleepRecoveryOutputWanted=false;if(i==1)g_inputReady=false;
  if(i==2)g_inputHostPowerAdmissionRevoked=true;if(i==3)g_exitRequested=true;
  observe("config-outputWanted-inputReady-revoked-exit",false);
 }
 reset();observe("unpublished-source",false);
 for(DWORD vk:{17u,18u,163u,165u,160u,161u,65u,0u,65535u}){
  reset();publish(0x0300);auto key=KBDLLHOOKSTRUCT{};key.vkCode=vk;observe("ordinary-generic-right-key",false,key);
 }
 for(DWORD flags:{0u,2u,16u}){
  reset();publish();auto key=KBDLLHOOKSTRUCT{};key.flags=flags;observe("native-own-or-non-lower",false,key);
 }
 for(std::uintptr_t tag:{std::uintptr_t{1},std::uintptr_t{0x594d4343},~std::uintptr_t{0}}){
  reset();publish();auto key=KBDLLHOOKSTRUCT{};key.dwExtraInfo=tag;observe("tagged-input",false,key);
 }
}
static void physicalCases(){
 for(DWORD vk:{162u,164u}){
  const DWORD generic=vk==162?17:18;
  for(unsigned slot:{generic,generic+256,vk,vk+256}){
   reset();publish(0x0300);g_oemKeyboardConsume.physicalDown[slot]=true;
   auto key=KBDLLHOOKSTRUCT{};key.vkCode=vk;observe("physical-or-VHF-ledger-hold",false,key);
  }
  for(unsigned slot:{generic,vk}){
   reset();publish(0x0300);g_oemKeyboardConsume.seededPassed[slot]=true;
   auto key=KBDLLHOOKSTRUCT{};key.vkCode=vk;observe("startup-unowned-seed",false,key);
  }
 }
 reset();publish();g_oemKeyboardConsume.physicalDown[164]=true;observe("unrelated-Alt-hold-keeps-Ctrl-scope",true);
}
static void publisherScope(){
 for(unsigned variant=0;variant<9;++variant){
  reset();FocusPlan plan{};bool live=true;std::string source="msi-dinput";
  if(variant==1)fake::known=false;if(variant==2)g_realStickTest.enabled=false;
  if(variant==3)g_realStickTest.virtualTargetEnabled=false;if(variant==4)plan.steamDeckTarget=false;
  if(variant==5)plan.decision=FocusDecision::SkipHostUnavailable;if(variant==6)live=false;
  if(variant==7)source="xinput";if(variant==8)plan.virtualUiHostEpoch=10;
  actualPublisherCall(live,XINPUT_GAMEPAD{0x0100},source,plan);
  observe("actual-call-scope-gates",variant==0);
  samples.back()["publisherScope"]={{"variant",variant},{"knownMsiBoard",fake::known},
   {"enabled",g_realStickTest.enabled},{"targetEnabled",g_realStickTest.virtualTargetEnabled},
   {"steamDeckTarget",plan.steamDeckTarget},{"hostUnavailable",plan.decision==FocusDecision::SkipHostUnavailable},
   {"live",live},{"inputSource",source},{"hostEpoch",plan.virtualUiHostEpoch}};
  check(g_msiShoulderModifierSequence.load()==(variant==1?0u:2u),"known-board outer call scope");
 }
 reset();FocusPlan plan{};actualPublisherCall(true,XINPUT_GAMEPAD{0x0100},"msi-dinput",plan);
 actualPublisherCall(true,XINPUT_GAMEPAD{0x0100},"xinput",plan);observe("source-family-changed-republish",false);
 reset();publish();auto up=KBDLLHOOKSTRUCT{};up.flags|=LLKHF_UP;
 g_inputReady=false;g_exitRequested=true;observe("up-after-disable-and-exit",false,up,WM_KEYUP);
}
int main(){try{
 adapterCases();physicalCases();publisherScope();
 std::cout<<json({{"pass",true},{"checks",checks},{"cases",samples.size()},{"samples",samples},
  {"actualOSInput",false},{"inputEmitterLinked",false},{"emittedKeyUps",0},
  {"evidenceLimit","Exact extracted adapter and publisher call; fake context; no full main/hook build or hardware/concurrency validation"}}).dump(2)<<'\n';
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';std::cout<<json({{"pass",false},{"checks",checks},{"samples",samples}}).dump(2)<<'\n';return 1;}}
