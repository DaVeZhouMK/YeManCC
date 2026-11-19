#!/usr/bin/env python3
"""Extracted TaskView producer/budget/async policy; in-memory OS/dispatcher stubs only."""
from pathlib import Path
import argparse
import ast,datetime,hashlib,json,os,re,subprocess,sys
sys.dont_write_bytecode=True
REPO=Path(__file__).resolve().parents[1]
OUT=REPO.parents[2]/'Build/Validation/TaskView-Api-20261015/wiring-tests'
VC=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
def sha(b):return hashlib.sha256(b).hexdigest()
def main():
 parser=argparse.ArgumentParser();parser.add_argument('--out',type=Path,default=OUT);parser.add_argument('--touch-native',action='store_true');options=parser.parse_args()
 if options.touch_native:
  # Mutually exclusive selection under the existing TaskView test identity.
  # The sibling runner extracts the actual endpoint and Windows adapter.
  import runpy
  runner=REPO/'tools/task_view_touch_native_selftest.py'
  sys.argv=[str(runner),'--out',str(options.out)]
  runpy.run_path(str(runner),run_name='__main__')
  return
 output=options.out;output.mkdir(parents=True,exist_ok=True)
 run=output/('run-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+str(os.getpid()));run.mkdir(parents=True)
 raw=(REPO/'native/main.cpp').read_bytes();text=raw.decode('utf-8-sig').replace('\r\n','\n')
 parser_source=(REPO/'tools/virtual_sample_tree_perf_selftest.py').read_text(encoding='utf-8-sig');module=ast.parse(parser_source)
 selected=[n for n in module.body if isinstance(n,ast.FunctionDef) and n.name in ['clean','match_brace','source_function']]
 scope={'re':re};exec(compile(ast.Module(body=selected,type_ignores=[]),'<source-parser>','exec'),scope);extract=scope['source_function']
 signatures=['static bool taskViewContextCurrent(','static bool taskViewFocusHandoffPaused(','static void taskViewInvalidateHandover(','static void taskViewRememberHandover(','static bool taskViewHandoverCurrent(','static bool taskViewQueueHandover(','static bool taskViewConsumeHandover(','static bool taskViewWaitForPriorFocus(','static json taskViewForegroundContextEvidence(','static json taskViewInputContextEvidence(','static std::uint64_t nativeSendTaskView(','static ULONGLONG taskViewRequestDeadline(','static bool ipc_cmd_async(']
 blocks={sig:extract(text,sig) for sig in signatures}
 assert not any(name in text for name in ['TaskViewKeyboardOps','g_taskViewKeyboardFallback','#include "task_view_keyboard_fallback.h"','#include "task_view_keyboard_windows.h"'])
 assert not any(name in '\n'.join(blocks.values()) for name in ['SendInput','GetAsyncKeyState','ymcc::taskView::keyboard::'])
 receipt_header=(REPO/'native/task_view_foreground_grant.h').read_bytes();rt=receipt_header.decode('utf-8-sig');receipt_types=rt[rt.index('enum class DeliveryStage'):rt.index('// YMCC') ]
 header=(REPO/'native/task_view_dispatch.h').read_bytes();ht=header.decode('utf-8-sig');status=ht[ht.index('enum class Status'):ht.index('struct WindowsOps')]
 handover_types=text[text.index('struct TaskViewHandoverReceipt {'):text.index('static std::atomic<std::shared_ptr<const TaskViewHandoverReceipt>>')]
 for sig in ['static void focusClearSession(', 'static bool focusPrepareSummonSession(']:
  assert 'taskViewInvalidateHandover();' in extract(text,sig),sig+' must cancel old request context'
 assert 'case WM_TASK_VIEW_HANDOVER:' in text and 'taskViewConsumeHandover(static_cast<std::uint64_t>(w))' in text,'posted minimize must consume exact receipt'
 fixture=r"""
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <cwchar>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>
#include "json.hpp"
using json=nlohmann::json;
namespace fake {
static unsigned checks=0,calls=0,captures=0;static bool detail=false,foregroundThrows=false;static DWORD error=0;static ULONGLONG tick=100;
static std::wstring module=L"G:\\fixture folder\\YeManCC.exe",helper,command;
static bool powerReady=true,priorFocusReady=true;
static unsigned inputCalls=0;
static std::array<SHORT,256> keyStates{};static unsigned keyReads=0;
static bool explicitGrantRejectedReceipt=false;
static SHORT asyncKey(int key){++keyReads;return keyStates.at(static_cast<std::size_t>(key));}
static UINT sendInput(UINT,INPUT*,int){++inputCalls;return 0;}
static std::function<void()> onPause,onDispatch;
static void pause(DWORD ms){tick+=ms;if(onPause)onPause();}
static int moduleMode=0;static ULONGLONG deadline=0;static bool logThrows=false;
static json lastLog;
static unsigned posts=0;static WPARAM postedSerial=0;static bool postAccepted=true;
BOOL post(HWND,UINT message,WPARAM serial,LPARAM){if(message!=WM_APP+830)throw std::runtime_error("wrong handover message");++posts;postedSerial=serial;return postAccepted;}

void check(bool x,const char* label){++checks;if(!x)throw std::runtime_error(label);}
DWORD moduleName(HMODULE,LPWSTR out,DWORD capacity){if(moduleMode==1)return 0;if(moduleMode==2)return capacity;std::wmemcpy(out,module.data(),module.size());return static_cast<DWORD>(module.size());}
ULONGLONG now(){return tick;}void setError(DWORD e){error=e;}
}
#define GetModuleFileNameW fake::moduleName
#define GetTickCount64 fake::now
#define GetCurrentThreadId() DWORD(77)
#define SetLastError fake::setError
#define GetAsyncKeyState fake::asyncKey
#define SendInput fake::sendInput
#define GetLastError() fake::error
#define Sleep fake::pause
#define PostMessageW fake::post
static HWND g_hwnd=reinterpret_cast<HWND>(17);
static struct {int family=0;} g_machineIdentity;
static const char* ymccFamilyName(int){return "fixture";}
static std::atomic<std::uint64_t> g_taskViewFocusToken{0};
static ULONGLONG currentPowerGeneration(){return fake::powerReady?1:2;}
enum class PowerLifecycle {Ready,Suspended};
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static unsigned long long g_taskViewFocusSession=0,g_focusSessionSequence=0;
static ULONGLONG g_taskViewFocusUntil=0;
__HANDOVER_TYPES__
static std::atomic<std::shared_ptr<const TaskViewHandoverReceipt>> g_taskViewHandoverReceipt;
static std::atomic<std::uint64_t> g_taskViewHandoverSerial{0};
static constexpr UINT WM_TASK_VIEW_HANDOVER=WM_APP+830;
static std::atomic<bool> g_exitRequested{false};
struct FocusLightHandoffState {std::atomic<bool> completed{false};struct {bool reclaimed=false;} result;};
static std::atomic<std::shared_ptr<FocusLightHandoffState>> g_taskViewPriorFocusHandoff;
static std::string fixturePersona="disabled";
static unsigned fixturePersonaReads=0;
static std::atomic<unsigned long long> g_inputHostEpochMirror{88};
static std::string inputHostConfiguredPersona(){++fixturePersonaReads;return fixturePersona;}
namespace ymcc::taskView::foreground {
__RECEIPT__
}

static bool sensorDetailLoggingGate(){return fake::detail;}
namespace ymcc::taskViewForeground {
struct Snapshot{};
Snapshot capture(HWND){++fake::captures;fake::error=999;return {};}
json toJson(Snapshot){if(fake::foregroundThrows)throw std::runtime_error("diagnostic");return {{"sampled",true}};}
}
namespace ymcc::taskView {
inline constexpr ULONGLONG requestBudgetMs=4500;
__STATUS__
static Result planned;
namespace worker {inline constexpr wchar_t mode[]=L"--ymcc-task-view-worker";}
struct WindowsOps{HWND foregroundOwner=nullptr;foreground::DeliveryReceipt foregroundReceipt;};
class Dispatcher {public:Result request(WindowsOps& ops,const wchar_t* h,wchar_t* c,ULONGLONG d,const std::function<bool()>& current={}){fake::check(!current||current(),"stale producer reached API");fake::check(ops.foregroundOwner==g_hwnd,"TaskView must marshal foreground grant to its owner window");ops.foregroundReceipt={planned.status==Status::Requested&&!fake::explicitGrantRejectedReceipt ? foreground::DeliveryStage::ApiAccepted : foreground::DeliveryStage::ApiRejected,planned.status==Status::Requested&&!fake::explicitGrantRejectedReceipt ? 0u : 5u};++fake::calls;fake::helper=h;fake::command=c;fake::deadline=d;if(fake::onDispatch)fake::onDispatch();return planned;}};
}
static int g_asyncMode=0;
static void appendNativeLifecycleLog(const char*,json row){if(fake::logThrows)throw std::runtime_error("sink allocation");fake::lastLog=std::move(row);}
__FUNCTIONS__
int main(){try{
 using namespace ymcc::taskView;using fake::check;
 for(int mode:{-1,0,1,2}){g_asyncMode=mode;check(ipc_cmd_async("shell.taskView"),"target must stay off UI in every async mode");check(!ipc_cmd_async("window.minimize"),"window control affinity changed");check(ipc_cmd_async("shell.run")==bool(mode>0),"unrelated shell async rule changed");}
 for(unsigned age=0;age<5000;++age){const auto args=json{{"requestedAtUnixMs",ULONGLONG(10000-age)}};const auto expected=age>=4500?ULONGLONG(200):ULONGLONG(200+4500-age);check(taskViewRequestDeadline(args,200,10000)==expected,"admission age budget mismatch");}
 for(unsigned ahead:{0,1,999,1000,1001,5000})check(taskViewRequestDeadline(json{{"requestedAtUnixMs",ULONGLONG(10000+ahead)}},200,10000)==(ahead>1000?ULONGLONG(200):ULONGLONG(4700)),"future-clock budget mismatch");
 for(const auto& args:std::vector<json>{nullptr,json::array(),json::object(),json{{"requestedAtUnixMs","bad"}},json{{"requestedAtUnixMs",-1}},json{{"requestedAtUnixMs",true}}})check(taskViewRequestDeadline(args,200,10000)==4700,"invalid/missing caller time changed budget");
 check(taskViewRequestDeadline(json::object(),MAXULONGLONG-2,10000)==MAXULONGLONG,"deadline overflow was not saturated");
 planned={};planned.status=Status::Requested;nativeSendTaskView(777,37);
 check(fake::calls==1&&fake::deadline==777,"explicit deadline was lost");
 check(fake::lastLog["nativeRequestSequence"]==37,"API result lost action sequence");
 check(fake::captures==0&&fake::lastLog["foregroundBeforeDispatch"].is_null(),"disabled diagnostics sampled foreground");
 check(fixturePersonaReads==0&&fake::lastLog["virtualInputBeforeDispatch"].is_null(),"disabled diagnostics read virtual input cache");
 check(fake::helper==L"G:\\fixture folder\\YeManCC.exe","worker must use the same exact executable");
 check(fake::command==L"\"G:\\fixture folder\\YeManCC.exe\" --ymcc-task-view-worker 777","helper quoting or original absolute deadline changed");
 check(fake::lastLog["apiRequestAccepted"]==true&&fake::lastLog["taskViewVisibleProven"]==false,"request status overclaims UI acceptance");
 check(fake::lastLog["keyboardKeyStateChecks"]==0&&fake::lastLog["keyboardBlockingVirtualKey"].is_null(),"API-only result invented key-state evidence");
 fake::detail=true;fake::error=555;auto snapshot=taskViewForegroundContextEvidence();check(snapshot["sampled"]==true&&fake::error==555,"foreground helper did not preserve error");
 fake::foregroundThrows=true;snapshot=taskViewForegroundContextEvidence();check(snapshot.is_null()&&fake::error==555,"throwing diagnostics altered request state");fake::foregroundThrows=false;
 planned={};planned.status=Status::Requested;nativeSendTaskView(777,38);check(fake::lastLog["foregroundBeforeDispatch"]["sampled"]==true&&fake::lastLog["foregroundAfterDispatch"]["sampled"]==true,"foreground snapshots lost");fake::detail=false;
 nativeSendTaskView();check(fake::deadline==fake::tick+4500,"default deadline missing");
 const Status failures[]={Status::Busy,Status::Expired,Status::MissingHelper,Status::JobFailed,Status::StartFailed,Status::AssignFailed,Status::ForegroundGrantFailed,Status::ResumeFailed,Status::ReadyFailed,Status::ReleaseFailed,Status::WaitFailed,Status::TimedOut,Status::ExitQueryFailed,Status::ApiFailed,Status::CleanupUnproven,Status::Busy};
 for(const auto& persona:std::vector<std::string>{"disabled","xbox360","dualsense-edge","steamdeck"}) {
  fixturePersona=persona;fake::detail=true;
  for(const auto resultStatus:{Status::Requested,Status::ForegroundGrantFailed}) {
   planned={};planned.status=resultStatus;planned.errorHint=resultStatus==Status::Requested?0u:5u;
   const auto beforeCalls=fake::calls;bool failed=false;
   try{nativeSendTaskView(777,55);}catch(const std::runtime_error&){failed=true;}
   check(fake::calls==beforeCalls+1,"persona bypassed actual TaskView dispatcher");
   check(failed==(resultStatus!=Status::Requested),"persona changed actual grant outcome");
   check(fake::lastLog["virtualInputBeforeDispatch"]["configuredPersonaSnapshot"]==persona,"cached persona was lost");
   check(fake::lastLog["virtualInputBeforeDispatch"]["hostEpochSnapshot"]==88,"cached epoch was lost");
   check(fake::lastLog["foregroundDeliveryApiAttempted"]==true,"actual API attempt receipt lost");
   check(fake::lastLog["foregroundDeliveryApiAccepted"]==(resultStatus==Status::Requested),"receipt acceptance was invented");
  }
 }
 fake::detail=false;
 for(int blocker:{VK_LCONTROL,VK_RCONTROL,VK_LSHIFT,VK_RSHIFT,VK_LMENU,VK_RMENU,VK_LWIN,VK_RWIN,VK_TAB}){
  fake::keyStates.fill(0);fake::keyStates[blocker]=SHORT(-32768);
  for(const auto status:{Status::Requested,Status::ForegroundGrantFailed}){
   planned={};planned.status=status;planned.errorHint=status==Status::Requested?0u:5u;
   const auto calls=fake::calls;std::string message;
   try{nativeSendTaskView(777);}catch(const std::runtime_error& e){message=e.what();}
   check(fake::calls==calls+1,"held key blocked the system route");
   check(message.empty()==(status==Status::Requested),"held key replaced the system result");
   check(fake::keyReads==0&&fake::inputCalls==0,"TaskView queried or injected keyboard input");
   check(fake::lastLog["keyboardFallbackEnabled"]==false&&fake::lastLog["keyboardFallbackStatus"]=="disabled","fallback policy not reported");
   check(fake::lastLog["keyboardFallbackAttempted"]==false&&fake::lastLog["keyboardFallbackAccepted"]==false&&fake::lastLog["keyboardFallbackAttempts"]==0,"disabled fallback invented an attempt");
   check(fake::lastLog["keyboardKeyStateChecks"]==0&&fake::lastLog["keyboardBlockingVirtualKey"].is_null()&&fake::lastLog["keyboardInsertedInputs"]==0,"disabled fallback invented key evidence");
   check(fake::lastLog["requestAccepted"]==(status==Status::Requested)&&fake::error==planned.errorHint,"held key altered acceptance or error");
  }
 }
 fake::keyStates.fill(0);
 for(const auto shellStatus:{Status::Requested,Status::ApiFailed,Status::TimedOut}){
  planned={};planned.status=shellStatus;planned.foregroundGrantAttempted=true;planned.foregroundGrantAccepted=false;planned.foregroundGrantError=5;planned.executionReleased=true;
  fake::explicitGrantRejectedReceipt=true;const auto beforeCalls=fake::calls;std::string message;
  try{nativeSendTaskView(777);}catch(const std::runtime_error& e){message=e.what();}
  check(message.empty()==(shellStatus==Status::Requested),"known rejection overrode actual Shell result");
  check(fake::lastLog["shellExecutionAfterGrantRejected"]==true&&fake::lastLog["foregroundGrantAccepted"]==false,"best-effort execution invented foreground authority");
  check(fake::lastLog["requestAccepted"]==(shellStatus==Status::Requested),"best-effort execution invented Shell acceptance");
  check(fake::calls==beforeCalls+1&&fake::inputCalls==0&&fake::keyReads==0&&fake::lastLog["keyboardInsertedInputs"]==0,"released system request was replayed");
 }
 fake::explicitGrantRejectedReceipt=false;
 for(auto status:failures){planned={};planned.status=status;planned.errorHint=123;bool threw=false;std::string message;try{nativeSendTaskView(777);}catch(const std::runtime_error& e){threw=true;message=e.what();}check(threw&&!message.empty(),"failure was reported as success");check(fake::lastLog["status"]==statusName(status),"exact dispatcher status lost");check(fake::error==123,"request error hint lost");}
 for(auto status:{Status::Requested,Status::TimedOut}){planned={};planned.status=status;fake::logThrows=true;bool threw=false;std::string message;try{nativeSendTaskView(777);}catch(const std::runtime_error& e){threw=true;message=e.what();}check(threw==(status!=Status::Requested),"logging failure replaced producer outcome");check(message!="sink allocation","logging error escaped producer");fake::logThrows=false;}
 for(int mode:{1,2,3}){fake::moduleMode=mode;auto old=fake::module;if(mode==3)fake::module=L"YeManCC.exe";const auto count=fake::calls;bool threw=false;try{nativeSendTaskView(777);}catch(const std::runtime_error&){threw=true;}check(threw&&fake::calls==count,"invalid executable path reached dispatcher");fake::module=old;fake::moduleMode=0;}
 planned={};planned.status=Status::ForegroundGrantFailed;planned.helperStarted=planned.helperReady=planned.cleanupVerified=true;planned.errorHint=5;
 auto before=fake::calls;bool denied=false;std::string rejected;
 try{nativeSendTaskView(777,83);}catch(const std::runtime_error& e){denied=true;rejected=e.what();}
 check(denied&&fake::calls==before+1&&fake::error==5,"known grant denial gained fallback acceptance");
 check(rejected.find("Tab")==std::string::npos&&rejected.find("Ctrl")==std::string::npos,"grant failure still blames held keys");
 check(fake::lastLog["keyboardFallbackAccepted"]==false&&fake::lastLog["apiRequestAccepted"]==false&&fake::lastLog["requestAccepted"]==false,"grant denial inflated system receipt");
 planned.status=Status::TimedOut;planned.executionReleased=true;denied=false;before=fake::calls;
 try{nativeSendTaskView(777);}catch(const std::runtime_error&){denied=true;}
 check(denied&&fake::calls==before+1&&fake::inputCalls==0,"unknown execution was replayed");
 planned={};planned.status=Status::Requested;
 bool nestedRejected=false;before=fake::calls;
 fake::onDispatch=[&]{try{nativeSendTaskView(777);}catch(const std::runtime_error&){nestedRejected=true;}};
 nativeSendTaskView(777);fake::onDispatch={};
 check(nestedRejected&&fake::calls==before+1,"overlap escaped one action lease");
 nativeSendTaskView(777);check(fake::calls==before+2,"action lease was not returned");
 for(int stop:{0,1}){before=fake::calls;if(stop==0)g_exitRequested=true;else g_powerLifecycle=PowerLifecycle::Suspended;denied=false;try{nativeSendTaskView(777);}catch(const std::runtime_error&){denied=true;}check(denied&&fake::calls==before,"shutdown or suspended lifecycle reached system dispatcher");g_exitRequested=false;g_powerLifecycle=PowerLifecycle::Ready;}
 for(int block:{1,2,3}){const auto before=fake::calls;fake::powerReady=block!=1;if(block==2)g_taskViewPriorFocusHandoff.store(std::make_shared<FocusLightHandoffState>());g_taskViewFocusToken=block==3?7:0;denied=false;try{nativeSendTaskView(777,0,1,block==3?6:0);}catch(const std::runtime_error&){denied=true;}check(denied&&fake::calls==before,"stale power/focus/prior helper reached API");fake::powerReady=fake::priorFocusReady=true;g_taskViewFocusToken=0;g_taskViewPriorFocusHandoff.store({});}
 planned={};planned.status=Status::Requested;g_taskViewFocusToken=9;nativeSendTaskView(777,0,1,9);check(g_taskViewFocusToken==9,"success resumed old focus before minimize");
 planned.status=Status::TimedOut;denied=false;try{nativeSendTaskView(777,0,1,9);}catch(const std::runtime_error&){denied=true;}check(denied&&g_taskViewFocusToken==0,"failed request retained focus pause");
 g_taskViewFocusToken=19;g_taskViewFocusSession=g_focusSessionSequence=6;g_taskViewFocusUntil=fake::tick+500;check(taskViewFocusHandoffPaused(),"pending old focus not paused");++g_focusSessionSequence;check(!taskViewFocusHandoffPaused(),"new explicit summon inherited old pause");g_focusSessionSequence=6;const auto saveTick=fake::tick;fake::tick+=500;check(!taskViewFocusHandoffPaused(),"pause exceeded bounded deadline");fake::tick=saveTick;
 auto prior=std::make_shared<FocusLightHandoffState>();g_taskViewPriorFocusHandoff.store(prior);fake::onPause=[&]{prior->result.reclaimed=true;prior->completed.store(true);};check(taskViewWaitForPriorFocus(fake::tick+500,1,19),"canceled prior helper did not settle");check(prior->completed&&prior->result.reclaimed,"cancel flag substituted for actual reclamation");fake::onPause={};prior->result.reclaimed=false;check(!taskViewWaitForPriorFocus(fake::tick+500,1,19),"unreclaimed helper authorized switch");g_taskViewPriorFocusHandoff.store({});g_taskViewFocusToken=0;
 for(int change:{1,2}){planned={};planned.status=Status::Requested;g_taskViewFocusToken=71;fake::powerReady=true;fake::onDispatch=[&]{if(change==1)fake::powerReady=false;else g_taskViewFocusToken=72;};bool staleAccepted=true;const auto beforeCalls=fake::calls;try{nativeSendTaskView(777,0,1,71);}catch(const std::runtime_error&){staleAccepted=false;}fake::onDispatch={};check(!staleAccepted,"late success crossed its original power/focus context");check(fake::calls==beforeCalls+1&&fake::inputCalls==0,"late success replayed system request");fake::powerReady=true;g_taskViewFocusToken=0;}
 const auto handoverArgs=json{{"diagnosticAction","game-recognition-switch-program"},{"diagnosticActionId","switch-program:fixture"}};
 const auto prepareHandover=[&]{fake::powerReady=true;g_exitRequested=false;g_taskViewFocusToken=81;g_taskViewFocusSession=g_focusSessionSequence=7;g_taskViewFocusUntil=fake::tick+2000;taskViewRememberHandover(handoverArgs,fake::tick+500,1,81,g_taskViewHandoverSerial.fetch_add(1)+1);return g_taskViewHandoverReceipt.load();};
 auto receipt=prepareHandover();check(receipt&&taskViewHandoverCurrent(receipt),"current accepted action lost handover");check(taskViewQueueHandover(handoverArgs)&&fake::postedSerial==receipt->serial,"accepted minimize did not post exact serial");check(taskViewConsumeHandover(receipt->serial),"current posted handover rejected");check(!taskViewConsumeHandover(receipt->serial)&&g_taskViewFocusToken==0,"handover was not one-use");
 for(int boundary:{0,1,2,3,4}){receipt=prepareHandover();const auto tick=fake::tick;if(boundary==0)fake::powerReady=false;if(boundary==1)g_taskViewFocusToken=82;if(boundary==2)++g_focusSessionSequence;if(boundary==3)fake::tick=receipt->expires;if(boundary==4)g_exitRequested=true;const auto posts=fake::posts;check(!taskViewQueueHandover(handoverArgs)&&fake::posts==posts,"stale handover was posted");check(!taskViewConsumeHandover(receipt->serial),"queued stale handover consumed new UI");fake::tick=tick;}
 receipt=prepareHandover();auto otherArgs=handoverArgs;otherArgs["diagnosticActionId"]="switch-program:new";check(!taskViewQueueHandover(otherArgs),"another action stole accepted receipt");check(!taskViewConsumeHandover(receipt->serial+1)&&g_taskViewHandoverReceipt.load()==receipt,"forged serial consumed receipt");taskViewRememberHandover(otherArgs,fake::tick+500,1,81,g_taskViewHandoverSerial.fetch_add(1)+1);const auto newer=g_taskViewHandoverReceipt.load();check(newer->serial!=receipt->serial&&!taskViewConsumeHandover(receipt->serial),"old queued message consumed newer receipt");check(!taskViewQueueHandover(handoverArgs)&&taskViewQueueHandover(otherArgs),"old action matched new return receipt");check(taskViewConsumeHandover(newer->serial),"new accepted action failed consumption");
 receipt=prepareHandover();const auto oldInvocation=receipt->serial;const auto laterInvocation=g_taskViewHandoverSerial.fetch_add(1)+1;taskViewRememberHandover(otherArgs,fake::tick+500,1,81,laterInvocation);const auto later=g_taskViewHandoverReceipt.load();taskViewRememberHandover(handoverArgs,fake::tick+500,1,81,oldInvocation);check(g_taskViewHandoverReceipt.load()==later&&!taskViewQueueHandover(handoverArgs),"late native completion replaced newer accepted action");
 receipt=prepareHandover();taskViewInvalidateHandover();check(!g_taskViewHandoverReceipt.load()&&g_taskViewFocusToken==0&&!taskViewConsumeHandover(receipt->serial),"session invalidation retained old handover");
 for(const auto& bad:std::vector<json>{nullptr,json::object(),json{{"diagnosticAction","other"}},json{{"diagnosticAction","game-recognition-switch-program"},{"diagnosticActionId",123}},json{{"diagnosticAction","game-recognition-switch-program"},{"diagnosticActionId",""}}}){taskViewRememberHandover(bad,777,1,0,0);check(!g_taskViewHandoverReceipt.load(),"malformed handover created a receipt");}
 fake::powerReady=true;g_exitRequested=false;g_taskViewFocusToken=0;
 check(fake::inputCalls==0&&fake::keyReads==0,"producer performed keyboard operations");
 std::cout<<json{{"pass",true},{"checks",fake::checks},{"realOSInput",false},{"realProcessOperations",false},{"scope","actual producer/queue-budget/async code with fake dispatcher and OS; not dispatcher or COM acceptance"}}.dump()<<'\n';return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
""".replace('__HANDOVER_TYPES__',handover_types).replace('__RECEIPT__',receipt_types).replace('__STATUS__',status).replace('__FUNCTIONS__','\n\n'.join(blocks.values()))
 sdk=Path('C:/Program Files (x86)/Windows Kits/10')
 versions=sorted((sdk/'Include').iterdir(),key=lambda x:tuple(int(v) for v in x.name.split('.')),reverse=True)
 sdk_version=next((v.name for v in versions if all((sdk/sub/v.name/part/file).exists() for sub,part,file in [('Include','um','Windows.h'),('Include','ucrt','crtdbg.h'),('Lib','um/x64','kernel32.lib'),('Lib','ucrt/x64','ucrt.lib')])),None)
 assert sdk_version,'A complete x64 Windows SDK is required'
 includes=';'.join(str(sdk/'Include'/sdk_version/x) for x in ['ucrt','shared','um','winrt','cppwinrt'])
 libs=';'.join(str(sdk/'Lib'/sdk_version/x/'x64') for x in ['ucrt','um'])
 preamble='@echo off\ncall "'+str(VC)+'" >nul\nif errorlevel 1 exit /b 1\nset "INCLUDE='+includes+';%INCLUDE%"\nset "LIB='+libs+';%LIB%"\n'
 (run/'fixture.cpp').write_text(fixture,encoding='utf-8');(run/'json.hpp').write_bytes((REPO/'deps/json/json.hpp').read_bytes());profiles=[]
 for profile,flags in [('Release',['/O2','/DNDEBUG','/MT']),('Checked',['/Od','/RTC1','/D_DEBUG','/MTd'])]:
  command=['cl','/nologo','/std:c++20','/utf-8','/EHsc','/W4',*flags,str(run/'fixture.cpp'),'/Fo'+str(run/(profile+'.obj')),'/Fe'+str(run/(profile+'.exe'))]
  batch=run/(profile+'.cmd');batch.write_text(preamble+subprocess.list2cmdline(command)+'\nif errorlevel 1 exit /b %errorlevel%\ndumpbin /imports "'+str(run/(profile+'.exe'))+'" > "'+str(run/(profile+'-imports.txt'))+'"\nexit /b %errorlevel%\n',encoding='utf-8')
  p=subprocess.run(['cmd.exe','/d','/c',str(batch)],cwd=run,capture_output=True,encoding='utf-8',errors='replace',timeout=120);(run/(profile+'-compile.log')).write_text(p.stdout+p.stderr,encoding='utf-8');assert p.returncode==0,p.stdout+p.stderr
  imports=(run/(profile+'-imports.txt')).read_text(encoding='utf-8',errors='replace').lower();assert all(k not in imports for k in ['user32.dll','ole32.dll','shell32.dll','createprocessw','sendinput'])
  p=subprocess.run([str(run/(profile+'.exe'))],cwd=run,capture_output=True,encoding='utf-8',errors='replace',timeout=30);(run/(profile+'-result.json')).write_text(p.stdout,encoding='utf-8');(run/(profile+'-stderr.txt')).write_text(p.stderr,encoding='utf-8');assert p.returncode==0,p.stderr;result=json.loads(p.stdout);assert result['pass'];profiles.append({'profile':profile,**result})
 current=(REPO/'native/main.cpp').read_text(encoding='utf-8-sig');assert all(extract(current,sig)==body for sig,body in blocks.items());assert (REPO/'native/task_view_dispatch.h').read_bytes()==header;assert (REPO/'native/task_view_foreground_grant.h').read_bytes()==receipt_header
 result={'pass':True,'profiles':profiles,'run':str(run),'functionHashes':{sig:sha(body.encode()) for sig,body in blocks.items()},'dispatcherHeaderSha256':sha(header),'receiptHeaderSha256':sha(receipt_header),'realInputOrProductExecuted':False}
 (run/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');(output/'latest.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print(json.dumps(result,ensure_ascii=False))
if __name__=='__main__':main()
