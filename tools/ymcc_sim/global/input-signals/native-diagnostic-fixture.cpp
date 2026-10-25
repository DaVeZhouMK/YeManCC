// No OS input, settings, Host or device port. Actual product excerpts are supplied
// through current-native-*.inc, never rewritten to turn a failed assertion green.
#define NOMINMAX
#include <windows.h>
#include <xinput.h>
#include <tuple>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include "deps/json/json.hpp"
#include "native/shortcut_frontend_button.h"
using json=nlohmann::json;
#include "current-native-types.inc"
static json logs=json::array();
static std::string dispatched;
static bool dispatchAccepted=true;
static unsigned forbiddenInput=0,settingsBoundaryReads=0;
static void appendNativeLifecycleLog(const std::string& event,const json& detail){logs.push_back({{"event",event},{"detail",detail}});}
static bool nativeShortcutUiDispatch(const NativeKeyboardShortcutRule& rule,uint64_t,uint64_t,uint64_t,bool,OemProducerTokens){dispatched=rule.uiAction;return dispatchAccepted;}
static bool nativeYmccShortcutEmit(const std::string& action,int,uint64_t,uint64_t,uint64_t,bool,OemProducerTokens){dispatched=action;return dispatchAccepted;}
namespace ymcc {static json inputShortcutContext(const json&,const json&){return json::object();}}
static json ymSettingsSection(const std::string&){++settingsBoundaryReads;return json::object();}
static json gameInputOwnerSession(){return json::object();}
static void nativeInputShortcutToggle(const std::string& action,const std::string&,const json&){dispatched=action;}
enum class SerialJobKind {YmccOwn,ExternalInjection};
static bool nativeYmccShortcutSubmit(std::function<void()> callback,SerialJobKind kind,uint64_t,uint64_t,uint64_t,bool,OemProducerTokens){
 if(kind!=SerialJobKind::YmccOwn){++forbiddenInput;throw std::runtime_error("EXTERNAL_INJECTION_FORBIDDEN");}
 if(dispatchAccepted)callback();return dispatchAccepted;
}
static void nativeKeyboardShortcutSend(WORD,bool,bool){++forbiddenInput;throw std::runtime_error("KEYBOARD_INPUT_FORBIDDEN");}
#include "current-native-dispatch.inc"
#include "current-native-mapping.inc"
namespace ymcc {using InputShortcutJson=json;
#include "current-native-string.inc"
}
#include "current-native-inputs.inc"
static std::vector<NativeKeyboardShortcutRule> g_nativeKeyboardShortcuts;
static uint32_t g_nativeShortcutLegacyOverrideMask=0;
static void runSummary(){
#include "current-native-summary.inc"
}
struct MemoryReceipt{uint64_t code=41,generation=1,sequence=1,ruleEpoch=4,capturedAt=100;};
static void runClick(const NativeKeyboardShortcutRule& rule,bool supported,bool emit,bool accepted){MemoryReceipt receipt;
#include "current-native-click.inc"
}
static void runEdge(const NativeKeyboardShortcutRule& rule,bool oemRule,bool pressed,bool matched,bool emit){const uint64_t currentMask=rule.requiredMask;
#include "current-native-edge.inc"
}
static bool admittedNumpadEnter(const std::string& code){NativeKeyboardShortcutRule rule;json params={{"key",code}};const auto key=params.find("key");
#include "current-native-numpad-enter.inc"
 return rule.forceExtendedKey;
}
struct Checks{
 unsigned checks=0;json failures=json::array(),evidence=json::array();
 void check(bool value,const std::string& label){++checks;if(!value)failures.push_back(label);}
 json result()const{return {{"status",failures.empty()?"PASS":"FAIL"},{"checks",checks},{"failures",failures},{"inputConsumerStateFeedback",evidence}};}
};
int main(int argc,char** argv){
 if(argc!=2)return 2;
 try{
  std::ifstream input(argv[1]);const auto fixture=json::parse(input);Checks diagnostic,keyboard;
  unsigned sequence=0;
  for(const std::string kind:{"ownAction","inputAction","uiAction","mixed-priority"}){
   NativeKeyboardShortcutRule rule;rule.requiredMask=kOemBitClaw;rule.trigger="press";
   if(kind=="ownAction")rule.ownAction="power.tdpUp";
   else if(kind=="inputAction")rule.inputAction="input.gyroToggle";
   else if(kind=="uiAction")rule.uiAction="monitor.toggle";
   else {rule.uiAction="monitor.toggle";rule.ownAction="power.tdpUp";rule.inputAction="input.gyroToggle";}
   const std::string expected=kind=="ownAction"?"power.tdpUp":kind=="inputAction"?"input.gyroToggle":"monitor.toggle";
   for(const bool accepted:{true,false}){
    dispatchAccepted=accepted;dispatched.clear();const bool actual=nativeKeyboardShortcutDispatch(rule,4,1,++sequence);
    diagnostic.check(actual==accepted,"actual dispatch admission: "+kind);
    if(accepted||kind!="inputAction")diagnostic.check(dispatched==expected,"actual dispatch priority: "+kind);
   }
   for(const auto& tuple:std::vector<std::tuple<bool,bool,bool,std::string>>{{true,true,true,"accepted"},{true,true,false,"failed"},{true,false,false,"waiting-double"},{false,false,false,"unconsumed"}}){
    logs.clear();runClick(rule,std::get<0>(tuple),std::get<1>(tuple),std::get<2>(tuple));
    diagnostic.check(logs.size()==1,"one click log: "+kind);
    const auto detail=logs.at(0).at("detail");diagnostic.check(detail.at("actionId")==expected,"oem-shortcut-click identity: "+kind);
    diagnostic.check(detail.at("status")==std::get<3>(tuple),"actual click status branch: "+kind);
    diagnostic.check(!detail.contains("defaultActionSuppressionVerified")&&!detail.contains("hardwareWriteIssued"),"dispatch log has no invented suppression proof");
    diagnostic.evidence.push_back({{"input",{{"sourceKind",kind},{"supported",std::get<0>(tuple)},{"emit",std::get<1>(tuple)},{"accepted",std::get<2>(tuple)}}},{"consumer","actual-click-log-statement"},{"state",detail},{"feedback",{{"event","oem-shortcut-click"},{"physicalSuppressionConfirmed",false}}}});
   }
   logs.clear();g_nativeKeyboardShortcuts={rule};++g_nativeShortcutLegacyOverrideMask;runSummary();
   diagnostic.check(logs.size()==1,"rules summary emits: "+kind);
   const auto summary=logs.at(0).at("detail");diagnostic.check(summary.at("rules").at(0).at("actionId")==expected,"oem-shortcut-rules identity: "+kind);
   diagnostic.evidence.push_back({{"input",{{"sourceKind",kind}}},{"consumer","actual-summary-digest-and-log-block"},{"state",summary},{"feedback",{{"event","oem-shortcut-rules"},{"physicalSuppressionConfirmed",false}}}});
   logs.clear();runSummary();diagnostic.check(logs.empty(),"unchanged summary does not log repeatedly: "+kind);
   const auto checkEdge=[&](bool oem,bool pressed,bool matched,bool emit,bool prior,const std::string& event){
    auto sampled=rule;sampled.matchedLastTick=prior;logs.clear();runEdge(sampled,oem,pressed,matched,emit);
    if(event.empty())diagnostic.check(logs.empty(),"no OEM event for inactive or non-OEM branch: "+kind);
    else {
     diagnostic.check(logs.size()==1&&logs.at(0).at("event")==event,"actual edge/emit condition: "+event+":"+kind);
     const auto detail=logs.at(0).at("detail");diagnostic.check(detail.at("actionId")==expected,event+" identity: "+kind);
     diagnostic.evidence.push_back({{"input",{{"sourceKind",kind},{"pressed",pressed},{"matched",matched},{"emit",emit},{"matchedLastTick",prior}}},{"consumer","actual-oemRule-edge-emit-block"},{"state",detail},{"feedback",{{"event",event},{"physicalSuppressionConfirmed",false}}}});
    }
   };
   checkEdge(true,true,true,true,false,"oem-shortcut-edge");
   checkEdge(true,false,false,false,true,"oem-shortcut-edge");
   checkEdge(true,false,true,true,true,"oem-shortcut-emit");
   checkEdge(true,false,true,false,true,"");checkEdge(false,true,true,true,false,"");
  }
  // Isolated extra defect: uiAction-only change must refresh the real digest.
  NativeKeyboardShortcutRule ui;ui.requiredMask=kOemBitClaw;ui.uiAction="monitor.toggle";g_nativeKeyboardShortcuts={ui};++g_nativeShortcutLegacyOverrideMask;
  logs.clear();runSummary();ui.uiAction="display.frameRateControl";g_nativeKeyboardShortcuts={ui};logs.clear();runSummary();
  diagnostic.check(logs.size()==1,"UI action-only change refreshes OEM rule digest");
  if(!logs.empty())diagnostic.check(logs.at(0).at("detail").at("rules").at(0).at("actionId")==ui.uiAction,"UI changed summary has the new identity");
  for(const auto& item:fixture.at("mappings")){
   const std::string action=item.at("actionId"),code=item.at("code");WORD vk=0;bool shift=false;
   const bool mapped=nativeKeyboardShortcutVirtualKey(action,code,vk,shift);
   const bool extended=((nativeKeyboardShortcutInputFlags(vk)&KEYEVENTF_EXTENDEDKEY)!=0)||admittedNumpadEnter(code);
   keyboard.check(mapped&&vk==item.at("virtualKey").get<WORD>()&&shift==item.at("requiresShift").get<bool>()&&extended==item.at("extended").get<bool>(),action+":"+code+" virtual-key/shift/extended contract");
   keyboard.evidence.push_back({{"input",{{"actionId",action},{"code",code}}},{"consumer","actual-native-key-mapping-and-flags-functions"},{"state",{{"mapped",mapped},{"virtualKey",vk},{"requiresShift",shift},{"extended",extended}}},{"feedback",{{"identityMatched",mapped&&vk==item.at("virtualKey").get<WORD>()},{"osInputPerformed",false}}}});
  }
  for(const auto& item:fixture.at("defaults")){
   WORD vk=0;bool shift=false;const std::string action=item.at("actionId"),code=item.at("code");
   keyboard.check(nativeKeyboardShortcutVirtualKey(action,code,vk,shift),"actual UI default accepted by native: "+action+":"+code);
  }
  for(const auto& action:{"keyboard.numeric","keyboard.symbol","keyboard.letter","keyboard.leftFunction","keyboard.rightFunction"})for(const auto& code:{"InvalidKey","Tilde","F25","Digit10","Numpad10"}){
   WORD vk=0;bool shift=false;keyboard.check(!nativeKeyboardShortcutVirtualKey(action,code,vk,shift),std::string("invalid native code rejected: ")+action+":"+code);
  }
  const json mixedControllerDesktop={{{"source","controller"},{"code","lb"}},{{"source","keyboard"},{"code","ControlLeft"}}};
  const json mixedOemController={{{"source","oem"},{"code","m1"}},{{"source","controller"},{"code","a"}}};
  uint64_t inputMask=0;std::vector<WORD> keys;
  keyboard.check(nativeKeyboardShortcutAllInputs(mixedControllerDesktop,inputMask,keys)&&inputMask==XINPUT_GAMEPAD_LEFT_SHOULDER&&keys==std::vector<WORD>{VK_LCONTROL},"actual mixed controller/keyboard admission preserves identity");
  keyboard.check(nativeKeyboardShortcutAllInputs(mixedOemController,inputMask,keys)&&inputMask==((1ull<<20)|XINPUT_GAMEPAD_A)&&keys.empty(),"actual mixed OEM/controller admission preserves identity");
  keyboard.check(!nativeKeyboardShortcutAllInputs(json::array(),inputMask,keys),"actual empty chord rejected");
  json five=json::array();for(const auto& code:{"a","b","x","y","lb"})five.push_back({{"source","controller"},{"code",code}});
  keyboard.check(!nativeKeyboardShortcutAllInputs(five,inputMask,keys),"actual five-key chord rejected");
  keyboard.check(!nativeKeyboardShortcutAllInputs(json::array({{{"source","controller"},{"code","a"}},{{"source","controller"},{"code","a"}}}),inputMask,keys),"actual duplicate physical key rejected");
  keyboard.check(!nativeKeyboardShortcutAllInputs(json::array({{{"source","oem"},{"code","a"}}}),inputMask,keys),"actual OEM/controller source spoof rejected");
  for(const auto& pair:std::vector<std::pair<std::string,ymcc::FrontendButton>>{{"steam",ymcc::FrontendButton::Steam},{"steamQuickAccess",ymcc::FrontendButton::SteamQuickAccess},{"ps",ymcc::FrontendButton::PS},{"xbox",ymcc::FrontendButton::Xbox},{"gamebar",ymcc::FrontendButton::Gamebar}})keyboard.check(ymcc::frontendButton(pair.first)==pair.second,"real frontend parser: "+pair.first);
  keyboard.check(ymcc::frontendButton("invalid")==ymcc::FrontendButton::Invalid,"invalid frontend button rejected");
  diagnostic.check(forbiddenInput==0,"no external injection or keyboard sender called");
  const bool pass=diagnostic.failures.empty()&&keyboard.failures.empty();
  const json result={{"schemaVersion",1},{"status",pass?"PASS":"FAIL"},{"diagnostic",diagnostic.result()},{"keyboard",keyboard.result()},
   {"boundaries",{{"actualOsInput",false},{"hardwareAccess",false},{"physicalSuppressionConfirmed",false},{"hostStarted",false},{"settingsReads",settingsBoundaryReads},{"settingsReadMeaning","memory-dispatch-context-only"}}}};
  std::cout<<result.dump()<<'\n';return pass?0:1;
 }catch(const std::exception& error){std::cout<<json({{"status","FAIL"},{"error",error.what()}}).dump()<<'\n';return 2;}
}

