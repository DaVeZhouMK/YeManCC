// Current-source input activity adapters and serializer, no live input APIs.
#define NOMINMAX
#define YMCC_SHORTCUT_FIXTURE
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include "input_signal_activity_windows.h"
#include "keyboard_modifier_event_evidence.h"
#include "json.hpp"
using json=nlohmann::json;
namespace ia=ymcc::inputActivity;namespace ke=ymcc::keyboardEvents;
static ia::Recorder g_inputSignalActivity;
static ia::WindowsAdapter g_inputSignalAdapter{g_inputSignalActivity};
static std::atomic<bool> g_inputSignalEventGate{false},g_inputSignalMouseHookInstalled{false};
static std::atomic<std::uint64_t> g_inputSignalLogFailures{0};
static ke::Recorder g_keyboardModifierEvents;
static std::atomic<bool> g_keyboardModifierEventGate{false};
static std::atomic<std::uint64_t> g_keyboardModifierDeviceGeneration{1};
static std::atomic<bool> g_keyboardModifierHookInstalled{false},g_keyboardModifierRawSubscribed{false};
namespace fake {
static DWORD error=12345;static ULONGLONG tick=100;static bool throwLog=false;
static unsigned nexts=0,maintains=0,replays=0,consumes=0;static bool consume=false;static std::vector<json>rows;static std::vector<std::string>domains;
static DWORD getError(){return error;}static void setError(DWORD value){error=value;}
static ULONGLONG getTick(){error=7001;return tick;}static DWORD getThread(){error=7002;return 77;}
static LRESULT next(HHOOK,int,WPARAM,LPARAM){++nexts;error=7003;return 59;}
static unsigned asyncReads=0,queueReads=0,foregroundReads=0;
static std::array<SHORT,256> asyncValues{};
static std::array<BYTE,256> queueKeys{};
static HWND foreground(){++foregroundReads;error=7010;return reinterpret_cast<HWND>(std::uintptr_t(42));}
static DWORD foregroundThread(HWND,LPDWORD pid){error=7011;if(pid)*pid=444;return 77;}
static SHORT asyncKey(int vk){++asyncReads;error=7012;return asyncValues[static_cast<std::size_t>(vk)];}
static BOOL keyboardState(BYTE* keys){++queueReads;error=7013;std::copy(queueKeys.begin(),queueKeys.end(),keys);return TRUE;}

}
static std::uint64_t currentPowerGeneration(){fake::error=7004;return 8;}
static void appendDomainLog(const char* domain,const char* name,const json& value){if(std::string(name)!="input-signal-activity")throw std::runtime_error("unexpected log target");if(fake::throwLog)throw std::runtime_error("synthetic sink failure");fake::domains.emplace_back(domain);fake::rows.push_back(value);}
static bool g_oemMouseLeft=false,g_oemMouseX2=false;
static std::atomic<bool>g_oemKeyHooksActive{false};static std::atomic<uint32_t>g_oemShortcutEdgeEpoch{1},g_gpdBackIfaceGen{2};static std::atomic<uint64_t>g_oemKeyMask{0};
static struct {unsigned replayCount=0;}g_oemMouseConsume;
static void oemMouseConsumeMaintain(ULONGLONG){++fake::maintains;fake::error=8001;}
static void oemBackHookRecompute(){g_oemKeyMask=(g_oemMouseLeft?1:0)|(g_oemMouseX2?2:0);}
static bool oemShortcutPostPhysicalEdge(uint64_t,uint32_t,uint32_t){return true;}
static bool oemMouseConsumeEvent(const MSLLHOOKSTRUCT&,unsigned,bool,ULONGLONG,bool){++fake::consumes;fake::error=8002;return fake::consume;}
static void oemMouseConsumeReplay(ULONGLONG){++fake::replays;fake::error=8003;}
#define GetLastError fake::getError
#define SetLastError fake::setError
#define GetTickCount64 fake::getTick
#define GetCurrentThreadId fake::getThread
#define CallNextHookEx fake::next
#define GetForegroundWindow fake::foreground
#define GetWindowThreadProcessId fake::foregroundThread
#define GetAsyncKeyState fake::asyncKey
#define GetKeyboardState fake::keyboardState
#include "input_signal_key_state_evidence.h"
#include "actual_activity.inc"
#include "before_mouse_hook.inc"
#undef GetLastError
#undef SetLastError
#undef GetTickCount64
#undef GetCurrentThreadId
#undef CallNextHookEx
#undef GetForegroundWindow
#undef GetWindowThreadProcessId
#undef GetAsyncKeyState
#undef GetKeyboardState
namespace ymcc::inputActivity::detail {
struct RecorderTestAccess {
    static void lock(Recorder& recorder){if(recorder.lock_.test_and_set(std::memory_order_acquire))throw std::runtime_error("fixture ledger already locked");}
    static void unlock(Recorder& recorder){recorder.lock_.clear(std::memory_order_release);}
};
}
static unsigned checks=0;static json cases=json::array();
static void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
static void reset(bool enabled=true){fake::asyncReads=fake::queueReads=fake::foregroundReads=0;fake::asyncValues.fill(0);fake::queueKeys.fill(0);g_inputSignalEventGate=false;g_keyboardModifierEventGate=false;check(g_inputSignalActivity.enable(false),"disable ledger");check(g_inputSignalActivity.enable(enabled),"enable ledger");(void)g_inputSignalAdapter.takeCounts();g_inputSignalEventGate=enabled;g_keyboardModifierEvents.enable(false,0);g_keyboardModifierEvents.enable(enabled,100);g_keyboardModifierEventGate=enabled;fake::error=12345;fake::tick=100;fake::throwLog=false;fake::rows.clear();fake::domains.clear();g_inputSignalLogFailures=0;fake::nexts=fake::maintains=fake::replays=fake::consumes=0;fake::consume=false;g_oemMouseLeft=g_oemMouseX2=false;g_oemKeyMask=0;g_oemKeyHooksActive=false;g_oemMouseConsume.replayCount=0;}
static ia::Snapshot snapshot(){ia::Snapshot value;check(g_inputSignalActivity.snapshot(value),"snapshot contention in serial fixture");return value;}
static const ia::Row* find(const ia::Snapshot& s,ia::Channel channel,ia::Kind kind,unsigned code,bool extended=false){for(std::size_t i=0;i<s.count;++i){const auto& r=s.rows[i];if(r.channel==channel&&r.kind==kind&&r.code==code&&r.extended==extended)return &r;}return nullptr;}
static INPUT key(WORD vk,bool up=false){INPUT i{};i.type=INPUT_KEYBOARD;i.ki.wVk=vk;i.ki.dwFlags=up?KEYEVENTF_KEYUP:0;return i;}
static void controlCases(){
 reset();ia::detail::RecorderTestAccess::lock(g_inputSignalActivity);inputSignalUpdateGate(false);
 check(!g_inputSignalEventGate&&g_inputSignalActivity.enabled(),"failed disable must close callback gate but retain retry obligation");
 ia::detail::RecorderTestAccess::unlock(g_inputSignalActivity);inputSignalUpdateGate(false);
 check(!g_inputSignalEventGate&&!g_inputSignalActivity.enabled(),"next control pass failed to retry disable");
 ia::detail::RecorderTestAccess::lock(g_inputSignalActivity);inputSignalUpdateGate(true);
 check(!g_inputSignalEventGate&&!g_inputSignalActivity.enabled(),"failed enable opened callback gate");
 ia::detail::RecorderTestAccess::unlock(g_inputSignalActivity);inputSignalUpdateGate(true);
 check(g_inputSignalEventGate&&g_inputSignalActivity.enabled(),"next control pass failed to enable");
 auto before=snapshot().configEpoch;inputSignalUpdateGate(true);check(snapshot().configEpoch==before,"idempotent gate reset recorder");
 ia::Event held{};held.channel=ia::Channel::LowLevelKeyboard;held.kind=ia::Kind::Keyboard;held.code=VK_LCONTROL;held.down=true;held.tick=100;
 (void)g_inputSignalActivity.record(held);before=snapshot().configEpoch;
 ia::detail::RecorderTestAccess::lock(g_inputSignalActivity);inputSignalUpdateGate(false);ia::detail::RecorderTestAccess::unlock(g_inputSignalActivity);
 inputSignalUpdateGate(true);auto after=snapshot();
 check(g_inputSignalEventGate&&after.configEpoch>before&&after.totalRows==0&&after.discardedHeldOnReset>=1,"rapid re-enable carried stale held state past failed disable");
 cases.push_back({{"case","actual-gate-contention-disable-retry-and-enable"},{"pass",true}});
}
static void keyboardCases(){
 for(DWORD vk:{DWORD('A'),DWORD(VK_F11),DWORD(VK_LWIN),DWORD(VK_LCONTROL),DWORD(VK_RMENU)})for(bool up:{false,true})for(bool suppressed:{false,true}){
   reset();KBDLLHOOKSTRUCT kb{};kb.vkCode=vk;kb.flags=up?LLKHF_UP:0;kb.time=456;ke::HookObservation observation{};observation.decision=suppressed?ke::HookDecision::Suppressed:ke::HookDecision::PassToNextHook;
   keyboardModifierObserveLowLevel(kb,WM_KEYDOWN,observation);check(fake::error==12345,"keyboard observer changed last error");auto s=snapshot();auto r=find(s,ia::Channel::LowLevelKeyboard,ia::Kind::Keyboard,vk);check(r&&r->eventCount==1&&r->observedHeld==!up&&r->lastSourceId==(suppressed?2:1),"ordinary/modifier callback activity wrong");
 }
 reset();KBDLLHOOKSTRUCT kb{};kb.vkCode='Q';kb.flags=LLKHF_INJECTED|LLKHF_EXTENDED;ke::HookObservation bypass{};bypass.decision=ke::HookDecision::PassToNextHook;keyboardModifierObserveLowLevel(kb,WM_KEYDOWN,bypass);auto s=snapshot();auto r=find(s,ia::Channel::LowLevelKeyboard,ia::Kind::Keyboard,'Q',true);check(r&&r->lastSourceId==0x11,"injected route lost");
 reset();RAWINPUT raw{};raw.header.dwType=RIM_TYPEKEYBOARD;raw.header.hDevice=reinterpret_cast<HANDLE>(0x123);raw.data.keyboard.VKey='W';raw.data.keyboard.Flags=RI_KEY_E0;keyboardModifierObserveRaw(raw);raw.data.keyboard.Flags=RI_KEY_E0|RI_KEY_BREAK;keyboardModifierObserveRaw(raw);s=snapshot();r=find(s,ia::Channel::RawKeyboard,ia::Kind::Keyboard,'W',true);check(r&&r->downCount==1&&r->upCount==1&&!r->observedHeld&&r->lastDeviceId==0x123,"ordinary raw short tap lost");check(fake::error==12345,"raw observer changed last error");
 reset(false);keyboardModifierObserveRaw(raw);keyboardModifierObserveLowLevel(kb,WM_KEYDOWN,bypass);check(snapshot().totalRows==0,"closed gate recorded keyboard");
 cases.push_back({{"case","all-key-class-callback-decision-raw-and-gate"},{"pass",true}});
}
static void selfCases(){
 reset();std::array<INPUT,2> tap{};for(auto& i:tap)i.type=INPUT_MOUSE;tap[0].mi.dwFlags=MOUSEEVENTF_LEFTDOWN;tap[1].mi.dwFlags=MOUSEEVENTF_LEFTUP;
 inputSignalObserveScreenTouchpad(tap.data(),2,1,ERROR_ACCESS_DENIED,false);auto s=snapshot();auto requested=find(s,ia::Channel::SelfSendInputRequest,ia::Kind::MouseButton,1);auto returned=find(s,ia::Channel::SelfSendInputReturnedPrefix,ia::Kind::MouseButton,1);
 check(requested&&returned&&!requested->selfHeldAssumption&&returned->selfHeldAssumption&&!returned->observedHeld,"partial tap request/prefix wrongly merged");check(fake::error==12345,"touchpad observer changed last error");
 auto c=g_inputSignalAdapter.takeCounts();auto source=static_cast<unsigned>(ke::SourceId::ScreenTouchpad);check(c.sources[source].batches==1&&c.sources[source].partial==1&&c.sources[source].requested==2&&c.sources[source].returned==1&&c.sources[source].lastErrorHint==ERROR_ACCESS_DENIED,"touchpad partial counts lost");
 inputSignalObserveScreenTouchpad(tap.data()+1,1,1,0,false);s=snapshot();returned=find(s,ia::Channel::SelfSendInputReturnedPrefix,ia::Kind::MouseButton,1);check(returned&&!returned->selfHeldAssumption&&returned->upCount==1,"late touchpad up not visible");
 reset();INPUT scan=key(0);scan.ki.wScan=0x11;scan.ki.dwFlags=KEYEVENTF_SCANCODE;inputSignalObserveScreenTouchpad(&scan,1,1,0,false);s=snapshot();check(find(s,ia::Channel::SelfSendInputReturnedPrefix,ia::Kind::KeyboardScanCode,0x11)!=nullptr&&!find(s,ia::Channel::SelfSendInputReturnedPrefix,ia::Kind::Keyboard,0),"scancode mislabeled VK0");
 reset();inputSignalObserveScreenTouchpad(tap.data(),2,1,5,true);check(snapshot().totalRows==0&&g_inputSignalAdapter.takeCounts().sources[source].batches==0,"dryrun logged as real");
 reset();INPUT unicode=key(0);unicode.ki.dwFlags=KEYEVENTF_UNICODE;unicode.ki.wScan=0x4F60;g_inputSignalAdapter.selfSend(&unicode,1,1,0,100,source);check(snapshot().totalRows==0&&g_inputSignalAdapter.takeCounts().sources[source].unicodeSkipped==1,"Unicode text retained or skip lost");
 reset();std::array<INPUT,64> large{};for(auto& i:large)i=key('X');g_inputSignalAdapter.selfSend(large.data(),65,65,0,100,source);c=g_inputSignalAdapter.takeCounts();check(c.sources[source].inspected==64&&c.sources[source].uninspected==1,"inspection bounds lost");
 reset();INPUT flags{};flags.type=INPUT_MOUSE;flags.mi.dwFlags=MOUSEEVENTF_WHEEL|MOUSEEVENTF_MOVE;flags.mi.mouseData=static_cast<DWORD>(-120);keyboardModifierObserveSelfSend(ke::SourceId::OemMouseReplay,&flags,1,1);s=snapshot();
 auto wheel=find(s,ia::Channel::SelfSendInputReturnedPrefix,ia::Kind::WheelVertical,0);check(wheel&&wheel->deltaSum==-120&&wheel->negativeDelta==120&&find(s,ia::Channel::SelfSendInputReturnedPrefix,ia::Kind::MouseMove,0),"wheel/move self counts lost");
 reset();INPUT ignoredVk=key(VK_LCONTROL);ignoredVk.ki.dwFlags=KEYEVENTF_SCANCODE;ignoredVk.ki.wScan=0x1E;
 keyboardModifierObserveSelfSend(ke::SourceId::ScreenTouchpad,&ignoredVk,1,1);
 auto modifiers=g_keyboardModifierEvents.take(150);check(modifiers.count==0,"ignored wVk from scan input falsely attributed Ctrl");
 s=snapshot();check(find(s,ia::Channel::SelfSendInputReturnedPrefix,ia::Kind::KeyboardScanCode,0x1E)!=nullptr,"ignored-VK scan activity lost");
 reset();std::array<INPUT,2> mixed{key(VK_LCONTROL),ignoredVk};keyboardModifierObserveSelfSend(ke::SourceId::ScreenTouchpad,mixed.data(),2,2);modifiers=g_keyboardModifierEvents.take(150);
 check(modifiers.count==1&&modifiers.events[0].nonVirtualKeyInputs==1&&modifiers.events[0].attemptedModifierDownMask==ke::bit(VK_LCONTROL),"mixed scan/VK mask qualification lost");
 cases.push_back({{"case","touchpad-mouse-partial-scan-unicode-batch-cap-dryrun"},{"pass",true}});
}
struct MouseControl {LRESULT result;DWORD error;unsigned nexts,maintains,replays,consumes;bool left,x2;uint64_t mask;};
static MouseControl runMouse(bool before,WPARAM message,DWORD flags,DWORD data,bool consume,bool active,unsigned pending){reset();fake::consume=consume;g_oemKeyHooksActive=active;g_oemMouseConsume.replayCount=pending;MSLLHOOKSTRUCT ms{};ms.flags=flags;ms.mouseData=data;auto result=before?beforeOemBackMouseHookProc(0,message,reinterpret_cast<LPARAM>(&ms)):oemBackMouseHookProc(0,message,reinterpret_cast<LPARAM>(&ms));return {result,fake::error,fake::nexts,fake::maintains,fake::replays,fake::consumes,g_oemMouseLeft,g_oemMouseX2,g_oemKeyMask.load()};}
static void mouseCases(){
 unsigned combinations=0;
 for(WPARAM msg:{WPARAM(WM_LBUTTONDOWN),WPARAM(WM_LBUTTONUP),WPARAM(WM_RBUTTONDOWN),WPARAM(WM_MBUTTONUP),WPARAM(WM_XBUTTONDOWN),WPARAM(WM_XBUTTONUP),WPARAM(WM_MOUSEWHEEL),WPARAM(WM_MOUSEHWHEEL),WPARAM(WM_MOUSEMOVE)})for(DWORD flags:{DWORD(0),DWORD(LLMHF_INJECTED),DWORD(LLMHF_LOWER_IL_INJECTED)})for(bool consume:{false,true})for(bool active:{false,true})for(unsigned pending:{0u,1u}){
   DWORD data=(msg==WM_MOUSEWHEEL||msg==WM_MOUSEHWHEEL)?static_cast<DWORD>(static_cast<WORD>(-120))<<16:XBUTTON2<<16;const auto old=runMouse(true,msg,flags,data,consume,active,pending),now=runMouse(false,msg,flags,data,consume,active,pending);++combinations;
   check(old.result==now.result&&old.error==now.error&&old.nexts==now.nexts&&old.maintains==now.maintains&&old.replays==now.replays&&old.consumes==now.consumes&&old.left==now.left&&old.x2==now.x2&&old.mask==now.mask,"activity changed mouse hook control/state");
   auto s=snapshot();check(s.totalRows==1,"mouse callback missing/duplicated");check(s.rows[0].lastSourceId%16==(now.result==1?2:1),"mouse consumed/pass attribution wrong");
 }
 cases.push_back({{"case","actual-before-after-mouse-hook"},{"combinations",combinations},{"policySubstituted",true},{"pass",true}});
}
static void serializationCases(){
 reset();json idle={{"msiRearWindow",{{"pressEdgesM1M2",{0,1}},{"releaseEdgesM1M2",{0,0}},{"sourceGenerationFirst",5}}},{"windowStartTick",99000},{"windowEndTick",100000},{"windowMs",1000},{"keyboardModifiers",{{"asyncDownMask",17},{"asyncZeroIsAbsenceProof",false}}},{"hostSnapshotKnown",true},{"hostProcessPresent",false},{"physicalBeforePublication",{{"samples",100}}},{"actualSubmitAttempts",{{"samples",0}}},{"hostAcceptedContent",{{"samples",0}}}};
 fake::asyncValues['W']=static_cast<SHORT>(0x8001u);fake::queueKeys['W']=0x80;
 INPUT a=key('W');inputSignalObserveScreenTouchpad(&a,1,1,0,false);inputSignalFlushEvidence(idle,100000);
 check(fake::rows.size()==2&&fake::rows[0]==fake::rows[1]&&fake::domains==std::vector<std::string>{"virtual","focus"},"activity mirror mismatch");auto row=fake::rows.back();check(row.at("completeInputCoverage")==false&&row.at("osHeldStateProven")==false&&row.at("keyboardModifiers").at("asyncDownMask")==17&&row.at("physicalBeforePublication").at("samples")==100,"correlation/scope missing");
 check(row.at("mouseHookInstalledCached")==false,"mouse coverage incorrectly inferred from other channel");
 const auto keyState=row.at("keyStateEvidence");
 check(keyState.at("sampled")==true&&keyState.at("selectedKeys")==1&&keyState.at("keys").at(0).at("virtualKey")=='W',"candidate state not attached/deduplicated");
 check(keyState.at("keys").at(0).at("asyncDownBit")==true&&keyState.at("keys").at(0).at("callingThreadQueueDown")==true,"actual drain lost synthetic async/queue state");
 check(fake::asyncReads==1&&fake::queueReads==1&&fake::foregroundReads==2,"key-state drain exceeded bounded API calls");

 check(row.at("msiRearWindow").at("pressEdgesM1M2")==json::array({0,1})&&row.at("windowStartTick")==99000&&row.at("windowMs")==1000,"rear-window correlation or latest-one-second boundary lost");
 check(row.at("eventRows").at(0).at("observedHeld").is_null()&&row.at("selfSendBatches").at(0).at("source")=="screen-touchpad-output","self activity mislabeled OS state/source");
 inputSignalFlushEvidence(idle,101000);check(fake::rows.size()==2,"activity flush bypassed five-second gate");check(fake::asyncReads==1&&fake::queueReads==1,"five-second gate permitted additional key-state sampling");inputSignalFlushEvidence(idle,105000);check(fake::rows.size()==4,"held carry not retained");
 a.ki.dwFlags=KEYEVENTF_KEYUP;inputSignalObserveScreenTouchpad(&a,1,1,0,false);inputSignalFlushEvidence(idle,110000);check(fake::rows.size()==6,"release aggregate lost");inputSignalFlushEvidence(idle,115000);check(fake::rows.size()==6,"quiet/no-held emitted repeated rows");
 reset();for(WORD vk=0;vk<100;++vk){KBDLLHOOKSTRUCT kb{};kb.vkCode=vk;ke::HookObservation obs{};obs.decision=ke::HookDecision::PassToNextHook;keyboardModifierObserveLowLevel(kb,WM_KEYDOWN,obs);}inputSignalFlushEvidence(idle,120000);row=fake::rows.back();check(row.at("totalRows")==100&&row.at("omittedRows")==36&&row.at("eventRows").size()==64,"truncation not explicit");
 reset(false);inputSignalFlushEvidence(idle,130000);check(fake::rows.empty(),"disabled signal flush emitted");check(fake::asyncReads==0&&fake::queueReads==0&&fake::foregroundReads==0,"disabled signal flush sampled key state");
 reset();fake::throwLog=true;inputSignalFlushEvidence(idle,140000);check(g_inputSignalLogFailures==1,"signal sink exception escaped");
 reset();KBDLLHOOKSTRUCT enter{};enter.vkCode=VK_RETURN;ke::HookObservation pass{};pass.decision=ke::HookDecision::PassToNextHook;
 keyboardModifierObserveLowLevel(enter,WM_KEYDOWN,pass);
 RAWINPUT raw{};raw.header.dwType=RIM_TYPEKEYBOARD;raw.data.keyboard.VKey=VK_RETURN;raw.data.keyboard.Message=WM_KEYDOWN;
 keyboardModifierObserveRaw(raw);raw.data.keyboard.Flags=RI_KEY_BREAK;raw.data.keyboard.Message=WM_KEYUP;keyboardModifierObserveRaw(raw);
 fake::asyncValues[VK_RETURN]=1;inputSignalFlushEvidence(idle,150000);row=fake::rows.back();
 check(row.at("keyStateEvidence").at("keys").size()==1&&row.at("keyStateEvidence").at("keys").at(0).at("virtualKey")==VK_RETURN,"Enter LL/Raw candidate was not merged");
 check(row.at("keyStateEvidence").at("keys").at(0).at("asyncDownBit")==false&&row.at("keyStateEvidence").at("keys").at(0).at("callingThreadQueueDown")==false,"low bit or callback held was relabelled async down");
 bool retainedCallbackHeld=false;for(const auto& event:row.at("eventRows"))if(event.at("channel")=="existing-low-level-keyboard"&&event.at("code")==VK_RETURN)retainedCallbackHeld=event.at("observedHeld").get<bool>();
 check(retainedCallbackHeld&&row.at("osHeldStateProven")==false,"callback/async mismatch was silently repaired or overclaimed");
 cases.push_back({{"case","actual-serializer-correlation-bounded-mirror-gates-errors"},{"pass",true}});
}
int main(){try{controlCases();keyboardCases();selfCases();mouseCases();serializationCases();std::cout<<json({{"pass",true},{"checks",checks},{"cases",cases},{"realInputUsed",false},{"productionFunctionsExtracted",true},{"sourceAttributionProven",false}}).dump(2)<<'\n';return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
