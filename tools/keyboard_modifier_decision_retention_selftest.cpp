// Actual production diagnostic functions and before/after hook control-flow
// comparison against deterministic Win32/consumer substitutes. No real input.
#define NOMINMAX
#define YMCC_SHORTCUT_FIXTURE
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
#include <stdexcept>
#include "keyboard_modifier_event_evidence.h"
#include "keyboard_modifier_retention.h"
#include "input_signal_activity_windows.h"
using json=nlohmann::json;
namespace ke=ymcc::keyboardEvents;
static ke::Recorder g_keyboardModifierEvents;
static ymcc::inputActivity::Recorder g_inputSignalActivity;
static ymcc::inputActivity::WindowsAdapter g_inputSignalAdapter{g_inputSignalActivity};
static std::atomic<bool> g_inputSignalEventGate{false};
static std::atomic<bool> g_keyboardModifierEventGate{false};
static std::atomic<std::uint64_t> g_keyboardModifierDeviceGeneration{1};
static std::atomic<bool> g_keyboardModifierHookInstalled{false},g_keyboardModifierRawSubscribed{false};
static std::atomic<ULONGLONG> g_keyboardModifierHookObservedTick{0};
static std::atomic<std::uint64_t> g_keyboardModifierLogFailures{0};
static std::atomic<bool> g_oemHooksWanted{false},g_oemRawPathActive{false},g_oemKeyHooksActive{false};
static std::atomic<uint32_t> g_oemShortcutEdgeEpoch{1},g_gpdBackIfaceGen{2};
static std::atomic<uint64_t> g_oemKeyMask{0};static uint32_t g_oemKbDown=0;
struct ConsumeState {std::array<bool,512>physicalDown{};std::array<bool,256>seededPassed{};std::array<unsigned char,512>visibility{};};
static ConsumeState g_oemKeyboardConsume;
// Baseline has no applicable MSI board/source context: guard is explicitly inactive.
static bool msiShoulderModifierSuppress(const KBDLLHOOKSTRUCT&,WPARAM,ymcc::msi::shoulderModifiers::Observation* = nullptr) noexcept {return false;}

namespace fake {
static DWORD error=12345;static ULONGLONG tick=120;static bool consumed=false,post=true,throwLog=false;
static unsigned maintained=0,consumes=0,nexts=0,posted=0,recomputed=0;static unsigned char after=0;
static std::vector<json> rows;static std::vector<std::string> domains;
static DWORD errorGet(){return error;}static void errorSet(DWORD e){error=e;}
static ULONGLONG tickGet(){error=8001;return tick;}static DWORD threadGet(){error=8002;return 7;}
static HWND foreground(){return nullptr;}
static DWORD windowProcess(HWND,LPDWORD pid){if(pid)*pid=42;return 7;}
static LRESULT next(HHOOK,int,WPARAM,LPARAM){++nexts;error=8003;return 37;}
}
static std::uint64_t currentPowerGeneration(){fake::error=8004;return 3;}
static void oemKeyboardConsumeMaintain(ULONGLONG){++fake::maintained;fake::error=8101;}
static unsigned oemKeyboardConsumeSlot(DWORD vk,DWORD flags){return vk+((flags&LLKHF_EXTENDED)?256:0);}
static uint32_t oemKeyboardPhysicalMask(){uint32_t n=0;for(auto b:g_oemKeyboardConsume.physicalDown)if(b)++n;return n;}
static void oemBackHookRecompute(){++fake::recomputed;g_oemKeyMask=g_oemKbDown;}
static bool oemShortcutPostPhysicalEdge(uint64_t,uint32_t,uint32_t){++fake::posted;return fake::post;}
static bool oemKeyboardConsumeEvent(const KBDLLHOOKSTRUCT& kb,ULONGLONG,bool,bool){++fake::consumes;g_oemKeyboardConsume.visibility[oemKeyboardConsumeSlot(kb.vkCode,kb.flags)]=fake::after;fake::error=8102;return fake::consumed;}
static void appendDomainLog(const char* domain,const char* name,const json& row){if(std::string(name)!="keyboard-modifier-events")throw std::runtime_error("unexpected log kind");if(fake::throwLog)throw std::runtime_error("synthetic sink failure");fake::domains.emplace_back(domain);fake::rows.push_back(row);}
static json keyboardModifierRawNameAtDrain(const ke::Event&,unsigned& budget){if(budget)--budget;return {{"fixture",true},{"eventPathIdentityProven",false}};}
#define GetLastError fake::errorGet
#define SetLastError fake::errorSet
#define GetTickCount64 fake::tickGet
#define GetCurrentThreadId fake::threadGet
#define GetForegroundWindow fake::foreground
#define GetWindowThreadProcessId fake::windowProcess
#define CallNextHookEx fake::next
#include "actual_diagnostics.inc"
#include "before_hook.inc"
#undef GetLastError
#undef SetLastError
#undef GetTickCount64
#undef GetCurrentThreadId
#undef GetForegroundWindow
#undef GetWindowThreadProcessId
#undef CallNextHookEx
static unsigned checks=0;static json cases=json::array();
static void check(bool b,const char* why){++checks;if(!b)throw std::runtime_error(why);}
static void reset(bool enabled=true){g_keyboardModifierEventGate=false;g_keyboardModifierEvents.enable(false,10);g_keyboardModifierEvents.enable(enabled,100);g_keyboardModifierEventGate=enabled;g_keyboardModifierHookInstalled=false;g_keyboardModifierRawSubscribed=false;g_keyboardModifierHookObservedTick=0;g_oemHooksWanted=false;g_oemRawPathActive=false;g_keyboardModifierLogFailures=0;g_oemKeyboardConsume={};g_oemKbDown=0;g_oemKeyMask=0;g_oemKeyHooksActive=false;fake::error=12345;fake::tick=120;fake::consumed=false;fake::post=true;fake::after=0;fake::maintained=fake::consumes=fake::nexts=fake::posted=fake::recomputed=0;fake::throwLog=false;fake::rows.clear();fake::domains.clear();}
static json idle(int async,int queue=0,bool queueOk=true){return {{"keyboardModifiers",{{"asyncDownMask",async},{"callingThreadQueueReadOk",queueOk},{"callingThreadQueueDownMask",queueOk?json(queue):json(nullptr)},{"asyncZeroIsAbsenceProof",false},{"gameOrSteamKeyboardReceipt","unobserved"}}},{"hostSnapshotKnown",true},{"hostProcessPresent",false},{"hostEpoch",9},{"hostLifecycle","none"}};}
static void retentionCases(){
 reset();keyboardModifierFlushEvidence(idle(0),1000);check(fake::rows.size()==2&&fake::domains[0]=="virtual"&&fake::domains[1]=="focus","initial snapshot not mirrored");check(fake::rows[0]==fake::rows[1]&&fake::rows.back().at("modifierSnapshotChanged")==true,"mirror identity/initial state");
 keyboardModifierFlushEvidence(idle(0),2000);check(fake::rows.size()==2,"unchanged zero snapshot emitted");
 keyboardModifierFlushEvidence(idle(17),3000);check(fake::rows.size()==4&&fake::rows.back().at("eventCount")==0&&fake::rows.back().at("keyboardModifiers").at("asyncDownMask")==17,"mask onset without event lost");
 for(ULONGLONG tick=4000;tick<33000;tick+=1000)keyboardModifierFlushEvidence(idle(17),tick);check(fake::rows.size()==4,"persistent mask caused 1Hz focus spam");
 keyboardModifierFlushEvidence(idle(17),33000);check(fake::rows.size()==6&&fake::rows.back().at("nonzeroModifierHeartbeat")==true,"30s nonzero heartbeat lost");
 keyboardModifierFlushEvidence(idle(0),34000);check(fake::rows.size()==8&&fake::rows.back().at("keyboardModifiers").at("asyncZeroIsAbsenceProof")==false,"release snapshot or zero caveat lost");
 keyboardModifierFlushEvidence(idle(0,0,false),35000);check(fake::rows.size()==10,"queue unavailable transition lost");
 keyboardModifierFlushEvidence(json::object(),36000);check(fake::rows.size()==12&&fake::rows.back().at("modifierSnapshotPresent")==false&&!fake::rows.back().contains("keyboardModifiers"),"missing snapshot fabricated zeros");
 keyboardModifierFlushEvidence(json::object(),37000);check(fake::rows.size()==12,"unchanged missing snapshot spam");
 reset(false);keyboardModifierFlushEvidence(idle(17),50000);check(fake::rows.empty(),"disabled recorder emitted snapshot");
 reset();fake::throwLog=true;keyboardModifierFlushEvidence(idle(17),1000);check(g_keyboardModifierLogFailures==1,"diagnostic exception escaped or lost");fake::throwLog=false;keyboardModifierFlushEvidence(idle(17),2000);check(fake::rows.size()==2&&fake::rows.back().at("modifierSnapshotChanged")==true,"failed append advanced retention state");
 cases.push_back({{"case","snapshot-change-heartbeat-gate-and-log-failure"},{"pass",true}});
}
static void policyEdges(){
 ke::ModifierRetention policy;auto a=ke::modifierStateAtDrain(idle(17));auto d=policy.decide(a,1,1000,false,false);check(d.emit&&d.stateChanged&&!d.nonzeroHeartbeat,"policy initial");policy.attempted(a,1,1000);
 check(policy.decide(a,1,999,false,false).nonzeroHeartbeat,"backward clock safety");check(policy.decide(a,2,1001,false,false).stateChanged,"config epoch not reset");check(policy.decide(a,1,1001,true,false).emit&&policy.decide(a,1,1001,false,true).emit,"events/coverage ignored");
 for(auto bad:std::vector<json>{-1,256,1.2,"17",nullptr,true}){auto j=idle(0);j["keyboardModifiers"]["asyncDownMask"]=bad;check(!ke::modifierStateAtDrain(j).asyncKnown,"invalid mask accepted");}
 auto j=idle(0);j["keyboardModifiers"]["asyncDownMask"]=UINT64_MAX;check(!ke::modifierStateAtDrain(j).asyncKnown,"large unsigned mask truncated");
 cases.push_back({{"case","retention-policy-missing-invalid-config-and-clock"},{"pass",true}});
}
struct ControlSnapshot {LRESULT result;DWORD error;unsigned maintained,consumes,nexts,posted,recomputed;ConsumeState state;uint32_t kbMask;uint64_t keyMask;};
static ControlSnapshot runHook(bool before,KBDLLHOOKSTRUCT* kb,int code,DWORD flags,bool active,bool consume,bool seeded){
 reset();fake::consumed=consume;fake::after=consume?3:1;g_oemKeyHooksActive=active;
 if(kb){kb->flags=flags;if(kb->vkCode<256){g_oemKeyboardConsume.seededPassed[kb->vkCode]=seeded;g_oemKeyboardConsume.visibility[oemKeyboardConsumeSlot(kb->vkCode,flags)]=2;}}
 auto result=before?beforeOemBackHookProc(code,WM_KEYUP,reinterpret_cast<LPARAM>(kb)):oemBackHookProc(code,WM_KEYUP,reinterpret_cast<LPARAM>(kb));
 return {result,fake::error,fake::maintained,fake::consumes,fake::nexts,fake::posted,fake::recomputed,g_oemKeyboardConsume,g_oemKbDown,g_oemKeyMask.load()};
}
static void hookCases(){
 unsigned combinations=0;
 for(DWORD vk:{DWORD(VK_LCONTROL),DWORD(VK_LMENU),DWORD('A'),DWORD(300)})for(DWORD flags:{DWORD(0),DWORD(LLKHF_UP),DWORD(LLKHF_EXTENDED|LLKHF_UP),DWORD(LLKHF_INJECTED),DWORD(LLKHF_LOWER_IL_INJECTED|LLKHF_UP)})for(bool active:{false,true})for(bool consume:{false,true})for(bool seeded:{false,true}){
    KBDLLHOOKSTRUCT kb{};kb.vkCode=vk;kb.scanCode=0x1D;kb.time=456;
    const auto old=runHook(true,&kb,HC_ACTION,flags,active,consume,seeded);const auto current=runHook(false,&kb,HC_ACTION,flags,active,consume,seeded);++combinations;
    check(old.result==current.result&&old.error==current.error&&old.maintained==current.maintained&&old.consumes==current.consumes&&old.nexts==current.nexts&&old.posted==current.posted&&old.recomputed==current.recomputed&&old.state.physicalDown==current.state.physicalDown&&old.state.seededPassed==current.state.seededPassed&&old.state.visibility==current.state.visibility&&old.kbMask==current.kbMask&&old.keyMask==current.keyMask,"diagnostics altered hook control/state");
    const auto snapshot=g_keyboardModifierEvents.take(150);if(ke::bit(vk)){
        check(snapshot.count==1,"modifier decision duplicated/missing");const auto& event=snapshot.events[0];const bool bypass=(flags&(LLKHF_INJECTED|LLKHF_LOWER_IL_INJECTED))!=0;
        check(event.lowLevelEventTime==456,"hook raw event time lost");check(event.hook.decision==(consume&&!bypass?ke::HookDecision::Suppressed:ke::HookDecision::PassToNextHook),"hook final decision wrong");check(event.hook.ledgerObserved==!bypass,"injected event invented ledger decision");
    }else check(snapshot.count==0,"modifier recorder unexpectedly retained ordinary key");
 }
 for(int code:{-1,1}){KBDLLHOOKSTRUCT kb{};kb.vkCode=VK_LCONTROL;const auto old=runHook(true,&kb,code,0,false,false,false),now=runHook(false,&kb,code,0,false,false,false);check(old.result==now.result&&g_keyboardModifierEvents.take(150).count==0,"non-action hook changed");}
 auto old=runHook(true,nullptr,HC_ACTION,0,false,false,false),now=runHook(false,nullptr,HC_ACTION,0,false,false,false);check(old.result==now.result,"null hook changed");
 cases.push_back({{"case","actual-before-after-hook-control-flow"},{"combinations",combinations},{"consumerPolicy", "deterministic substitute; exact hook bodies"},{"pass",true}});
}
static void decisionSerialization(){
 reset();KBDLLHOOKSTRUCT kb{};kb.vkCode=VK_LCONTROL;kb.time=123;kb.flags=LLKHF_UP;fake::consumed=true;fake::after=0;g_oemKeyboardConsume.visibility[VK_LCONTROL]=3;
 check(oemBackHookProc(HC_ACTION,WM_KEYUP,reinterpret_cast<LPARAM>(&kb))==1,"setup consume");keyboardModifierFlushEvidence(idle(17),1000);const auto& row=fake::rows.back();const auto& e=row.at("events").at(0);
 check(e.at("hookDecision")=="suppressed-by-YMCC"&&e.at("visibilityBeforeConsume")==3&&e.at("visibilityAfterConsume")==0,"consumed up evidence not serialized");check(e.at("lowLevelEventTimeCaptured")==true&&e.at("lowLevelEventTime")==123&&e.at("downstreamDeliveryProven")==false,"decision timestamp or scope wrong");
 reset();kb.flags=LLKHF_INJECTED|LLKHF_UP;check(oemBackHookProc(HC_ACTION,WM_KEYUP,reinterpret_cast<LPARAM>(&kb))==37,"next-hook return changed");keyboardModifierFlushEvidence(idle(17),2000);const auto& injected=fake::rows.back().at("events").at(0);check(injected.at("hookPath")=="injected-bypass"&&injected.at("hookDecision")=="passed-to-next-hook"&&injected.at("visibilityBeforeConsume").is_null(),"injected bypass misattributed");
 cases.push_back({{"case","final-decision-serialization-and-downstream-limit"},{"pass",true}});
}
int main(){try{retentionCases();policyEdges();hookCases();decisionSerialization();std::cout<<json({{"pass",true},{"checks",checks},{"cases",cases},{"realInputOrProductsUsed",false},{"sourceExtracted",true},{"consumerPolicyReplacedByDeterministicStub",true},{"CtrlAltRootCauseProven",false}}).dump(2)<<'\n';return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}


