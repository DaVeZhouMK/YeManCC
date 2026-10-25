#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "keyboard_owned_pulse.h"
#include "keyboard_modifier_event_evidence.h"
#include "json.hpp"
using json=nlohmann::json;
namespace kp=ymcc::keyboardPulse;namespace ke=ymcc::keyboardEvents;
static std::atomic<kp::Owner*> g_nativeKeyboardPulseOwner{nullptr};
static HWND g_hwnd=reinterpret_cast<HWND>(1);static bool g_webviewReady=true;
inline constexpr int COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC=0;
namespace fake {
struct Plan {bool ready=true,valid=true,visible=true,iconic=false,focusFirst=false,allowAlt=true,hwndPresent=true,controller=true;UINT first=4;DWORD error=8765;std::vector<UINT> extra;};
static Plan plan;static DWORD error=12345;static ULONGLONG tick=1000;static unsigned focusReads=0;
static std::vector<std::vector<INPUT>>sent;static json trace=json::array(),observations=json::array();static std::array<bool,256>held{};
static DWORD getError()noexcept{return error;}static void setError(DWORD v)noexcept{error=v;}
static ULONGLONG getTick()noexcept{error=7001;return tick;}
static void ui(const char* name,int value=0){trace.push_back({{"api",name},{"value",value}});error=4321;}
static BOOL valid(HWND){ui("IsWindow");return plan.valid;}
static BOOL iconic(HWND){ui("IsIconic");return plan.iconic;}
static BOOL visible(HWND){ui("IsWindowVisible");return plan.visible;}
static BOOL show(HWND,int value){ui("ShowWindow",value);return FALSE;}
static BOOL bring(HWND){ui("BringWindowToTop");return TRUE;}
static BOOL foregroundSet(HWND){ui("SetForegroundWindow");return TRUE;}
static HWND active(HWND){ui("SetActiveWindow");return nullptr;}
static HWND focus(HWND){ui("SetFocus");return nullptr;}
static HWND foreground(){ui("GetForegroundWindow");return reinterpret_cast<HWND>((plan.focusFirst||focusReads++)?1:2);}
static HWND ancestor(HWND h,UINT value){ui("GetAncestor",static_cast<int>(value));return h;}
static UINT send(UINT n,LPINPUT inputs,int cb)noexcept{
 if(cb!=sizeof(INPUT)||!inputs)std::terminate();const auto ordinal=sent.size();UINT accepted=ordinal==0?plan.first:ordinal-1<plan.extra.size()?plan.extra[ordinal-1]:n;
 sent.emplace_back(inputs,inputs+n);trace.push_back({{"api","SendInput"},{"requested",n}});error=plan.error+static_cast<DWORD>(ordinal);
 for(UINT i=0;i<(std::min)(accepted,n);++i){if(inputs[i].type!=INPUT_KEYBOARD||inputs[i].ki.wVk>=256)std::terminate();held[inputs[i].ki.wVk]=(inputs[i].ki.dwFlags&KEYEVENTF_KEYUP)==0;}
 return accepted;
}
}
struct Controller {HRESULT MoveFocus(int v){fake::ui("MoveFocus",v);return S_OK;}};
static Controller controller;static Controller* g_ctrl=&controller;
static void hideWindowAnimated(HWND){fake::ui("hideWindowAnimated");}
static void keyboardModifierObserveSelfSend(ke::SourceId source,const INPUT*,UINT requested,UINT inserted)noexcept{
 fake::observations.push_back({{"source",ke::sourceName(source)},{"requested",requested},{"inserted",inserted},{"errorAtObservation",fake::error}});
}
#define GetLastError fake::getError
#define SetLastError fake::setError
#define GetTickCount64 fake::getTick
#define SendInput fake::send
#define IsWindow fake::valid
#define IsIconic fake::iconic
#define IsWindowVisible fake::visible
#define ShowWindow fake::show
#define BringWindowToTop fake::bring
#define SetForegroundWindow fake::foregroundSet
#define SetActiveWindow fake::active
#define SetFocus fake::focus
#define GetForegroundWindow fake::foreground
#define GetAncestor fake::ancestor
#include "actual_pulse.inc"
namespace baseline {
#include "baseline_producers.inc"
}
#undef GetLastError
#undef SetLastError
#undef GetTickCount64
#undef SendInput
#undef IsWindow
#undef IsIconic
#undef IsWindowVisible
#undef ShowWindow
#undef BringWindowToTop
#undef SetForegroundWindow
#undef SetActiveWindow
#undef SetFocus
#undef GetForegroundWindow
#undef GetAncestor
static unsigned checks=0;static json cases=json::array();
static void check(bool b,const char* why){++checks;if(!b)throw std::runtime_error(why);}
static void reset(const fake::Plan& plan){check(!g_nativeKeyboardPulseOwner.load()||!g_nativeKeyboardPulseOwner.load()->pending(),"previous case unresolved");fake::plan=plan;fake::error=12345;fake::tick=1000;fake::focusReads=0;fake::sent.clear();fake::trace=json::array();fake::observations=json::array();fake::held={};g_hwnd=plan.hwndPresent?reinterpret_cast<HWND>(1):nullptr;g_webviewReady=plan.ready;g_ctrl=plan.controller?&controller:nullptr;}
enum class Which {Focus,Shortcut,Shifted,Desktop,Task,Gamebar};
struct Outcome {bool value=false,threw=false;std::string exception;DWORD error=0;json trace,observations;std::vector<std::vector<INPUT>>batches;};
static Outcome run(Which which,const fake::Plan& plan,bool before){reset(plan);Outcome r;
 try{switch(which){case Which::Focus:r.value=before?baseline::refocusWebView(plan.allowAlt):refocusWebView(plan.allowAlt);break;
 case Which::Shortcut:if(before)baseline::nativeKeyboardShortcutSend(VK_LCONTROL,false);else nativeKeyboardShortcutSend(VK_LCONTROL,false);break;
 case Which::Shifted:if(before)baseline::nativeKeyboardShortcutSend('A',true);else nativeKeyboardShortcutSend('A',true);break;
 case Which::Desktop:if(before)baseline::returnToDesktop();else returnToDesktop();break;
 case Which::Task:if(before)baseline::nativeSendTaskView();else nativeSendTaskView();break;
 case Which::Gamebar:r.value=before?baseline::nativeFrontendButtonSendGamebar():nativeFrontendButtonSendGamebar();break;}}
 catch(const std::runtime_error& e){r.threw=true;r.exception=e.what();}
 r.error=fake::error;r.trace=fake::trace;r.observations=fake::observations;r.batches=fake::sent;return r;
}
static bool sameBatch(const std::vector<INPUT>& a,const std::vector<INPUT>& b){return a.size()==b.size()&&(a.empty()||!std::memcmp(a.data(),b.data(),a.size()*sizeof(INPUT)));}
static void normalParity(){unsigned scenarios=0;
 for(auto which:{Which::Focus,Which::Shortcut,Which::Shifted,Which::Desktop,Which::Task,Which::Gamebar}){
  fake::Plan p;p.first=(which==Which::Focus||which==Which::Shortcut)?2:4;auto old=run(which,p,true),now=run(which,p,false);++scenarios;
  check(old.value==now.value&&old.threw==now.threw&&old.exception==now.exception&&old.error==now.error,"full pulse return/exception/error changed");check(old.trace==now.trace&&old.batches.size()==now.batches.size()&&old.batches.size()==1&&sameBatch(old.batches[0],now.batches[0]),"full pulse exact batch or UI sequence changed");check(old.observations==now.observations,"full pulse observation changed");
 }
 for(unsigned variant=0;variant<7;++variant){fake::Plan p;p.first=2;switch(variant){case 0:p.hwndPresent=false;break;case 1:p.ready=false;break;case 2:p.valid=false;break;case 3:p.focusFirst=true;break;case 4:p.allowAlt=false;break;case 5:p.visible=false;break;default:p.controller=false;p.iconic=true;break;}
 auto old=run(Which::Focus,p,true),now=run(Which::Focus,p,false);++scenarios;check(old.trace==now.trace&&old.value==now.value&&old.error==now.error&&old.batches.size()==now.batches.size(),"focus shortcut guard changed");if(!old.batches.empty())check(sameBatch(old.batches[0],now.batches[0]),"guarded focus batch changed");}
 cases.push_back({{"case","actual-source-normal-and-focus-guard-parity"},{"scenarios",scenarios},{"pass",true}});
}
static void partialAndDurable(){unsigned scenarios=0;
 for(auto which:{Which::Focus,Which::Shortcut,Which::Shifted,Which::Desktop,Which::Task,Which::Gamebar}){
  const UINT n=(which==Which::Focus||which==Which::Shortcut)?2:4;
  for(UINT inserted=0;inserted<n;++inserted){fake::Plan p;p.first=inserted;auto old=run(which,p,true),now=run(which,p,false);++scenarios;
   check(old.batches.size()>=1&&now.batches.size()>=1&&sameBatch(old.batches[0],now.batches[0]),"partial initial batch changed");check(now.value==old.value&&now.threw==old.threw&&now.exception==old.exception,"partial original result/exception changed");
   check(!g_nativeKeyboardPulseOwner.load()->pending(),"successful immediate repair retained debt");for(auto held:fake::held)check(!held,"partial pulse left modeled key held");
   check(now.batches.size()<=2,"more than one immediate compensation");for(std::size_t b=1;b<now.batches.size();++b)for(const auto& input:now.batches[b])check((input.ki.dwFlags&KEYEVENTF_KEYUP)!=0,"compensation emitted DOWN");
   if(which==Which::Task)check(now.error==p.error,"compensation replaced original TaskView error");
  }
 }
 fake::Plan p;p.first=1;p.extra={0,0,1,4};auto first=run(Which::Shortcut,p,false);check(fake::held[VK_LCONTROL]&&g_nativeKeyboardPulseOwner.load()->pending(),"persistent Ctrl setup failed");
 fake::tick=1100;const auto calls=fake::sent.size();nativeKeyboardPulseMaintain(false);check(fake::sent.size()==calls,"maintenance ignored backoff");
 bool ok=nativeFrontendButtonSendGamebar();check(!ok&&fake::sent.size()==3&&fake::held[VK_LCONTROL]&&!fake::held[VK_LWIN],"failed old release allowed new Gamebar pulse");
 fake::tick=1200;ok=nativeFrontendButtonSendGamebar();check(ok&&fake::sent.size()==5&&!g_nativeKeyboardPulseOwner.load()->pending(),"old source release before new source pulse failed");check(fake::observations[3].at("source")=="owned-keyboard-pulse-release"&&fake::observations[4].at("source")=="nativeFrontendButtonSendGamebar","repair/source evidence wrong");
 cases.push_back({{"case","actual-source-short-send-repair-and-persistent-admission"},{"scenarios",scenarios},{"pass",true}});
}
static void debtEvidence(){fake::Plan p;p.first=1;p.extra={0,1};run(Which::Shortcut,p,false);auto j=nativeKeyboardPulseEvidence();check(j.at("pendingOwnedReleases")==true&&j.at("releaseCount")==1&&j.at("ownedUps").at(0).at("virtualKey")==VK_LCONTROL,"owned debt evidence missing");check(j.at("externalKeyboardStateProven")==false,"debt evidence claimed OS state");fake::error=321;nativeKeyboardPulseMaintain(true);check(fake::error==321&&!g_nativeKeyboardPulseOwner.load()->pending(),"forced cleanup failed or clobbered caller error");const auto count=fake::sent.size();nativeKeyboardPulseMaintain(true);check(fake::sent.size()==count,"empty owner emitted global releases");cases.push_back({{"case","pending-evidence-forced-cleanup-and-error-preservation"},{"pass",true}});}
int main(){try{normalParity();partialAndDurable();debtEvidence();std::cout<<json({{"pass",true},{"checks",checks},{"cases",cases},{"realInputUsed",false},{"actualProducerFunctionsExtracted",true},{"fieldCtrlAltRootCauseProven",false}}).dump(2)<<'\n';return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
