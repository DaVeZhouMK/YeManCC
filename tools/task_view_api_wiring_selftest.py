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
 parser=argparse.ArgumentParser();parser.add_argument('--out',type=Path,default=OUT);options=parser.parse_args()
 output=options.out;output.mkdir(parents=True,exist_ok=True)
 run=output/('run-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+str(os.getpid()));run.mkdir(parents=True)
 raw=(REPO/'native/main.cpp').read_bytes();text=raw.decode('utf-8-sig').replace('\r\n','\n')
 parser_source=(REPO/'tools/virtual_sample_tree_perf_selftest.py').read_text(encoding='utf-8-sig');module=ast.parse(parser_source)
 selected=[n for n in module.body if isinstance(n,ast.FunctionDef) and n.name in ['clean','match_brace','source_function']]
 scope={'re':re};exec(compile(ast.Module(body=selected,type_ignores=[]),'<source-parser>','exec'),scope);extract=scope['source_function']
 signatures=['static json taskViewForegroundContextEvidence(','static json taskViewInputContextEvidence(','static void nativeSendTaskView(','static ULONGLONG taskViewRequestDeadline(','static bool ipc_cmd_async(']
 blocks={sig:extract(text,sig) for sig in signatures}
 receipt_header=(REPO/'native/task_view_foreground_grant.h').read_bytes();rt=receipt_header.decode('utf-8-sig');receipt_types=rt[rt.index('enum class DeliveryStage'):rt.index('// YMCC') ]
 header=(REPO/'native/task_view_dispatch.h').read_bytes();ht=header.decode('utf-8-sig');status=ht[ht.index('enum class Status'):ht.index('struct WindowsOps')]
 fixture=r"""
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <array>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>
#include "json.hpp"
using json=nlohmann::json;
namespace fake {
static unsigned checks=0,calls=0,captures=0;static bool detail=false,foregroundThrows=false;static DWORD error=0;static ULONGLONG tick=100;
static std::wstring module=L"G:\\fixture folder\\YeManCC.exe",helper,command;
static int moduleMode=0;static ULONGLONG deadline=0;static bool logThrows=false;
static json lastLog;
void check(bool x,const char* label){++checks;if(!x)throw std::runtime_error(label);}
DWORD moduleName(HMODULE,LPWSTR out,DWORD capacity){if(moduleMode==1)return 0;if(moduleMode==2)return capacity;std::wmemcpy(out,module.data(),module.size());return static_cast<DWORD>(module.size());}
ULONGLONG now(){return tick;}void setError(DWORD e){error=e;}
}
#define GetModuleFileNameW fake::moduleName
#define GetTickCount64 fake::now
#define GetCurrentThreadId() DWORD(77)
#define SetLastError fake::setError
#define GetLastError() fake::error
static HWND g_hwnd=reinterpret_cast<HWND>(17);
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
class Dispatcher {public:Result request(WindowsOps& ops,const wchar_t* h,wchar_t* c,ULONGLONG d){fake::check(ops.foregroundOwner==g_hwnd,"TaskView must marshal foreground grant to its owner window");ops.foregroundReceipt={planned.status==Status::Requested ? foreground::DeliveryStage::ApiAccepted : foreground::DeliveryStage::ApiRejected,planned.status==Status::Requested ? 0u : 5u};++fake::calls;fake::helper=h;fake::command=c;fake::deadline=d;return planned;}};
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
 check(fake::command==L"\"G:\\fixture folder\\YeManCC.exe\" --ymcc-task-view-worker","helper command quoting changed");
 check(fake::lastLog["apiRequestAccepted"]==true&&fake::lastLog["taskViewVisibleProven"]==false,"request status overclaims UI acceptance");
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
 for(auto status:failures){planned={};planned.status=status;planned.errorHint=123;bool threw=false;std::string message;try{nativeSendTaskView(777);}catch(const std::runtime_error& e){threw=true;message=e.what();}check(threw&&!message.empty(),"failure was reported as success");check(fake::lastLog["status"]==statusName(status),"exact dispatcher status lost");check(fake::error==123,"request error hint lost");}
 for(auto status:{Status::Requested,Status::TimedOut}){planned={};planned.status=status;fake::logThrows=true;bool threw=false;std::string message;try{nativeSendTaskView(777);}catch(const std::runtime_error& e){threw=true;message=e.what();}check(threw==(status!=Status::Requested),"logging failure replaced producer outcome");check(message!="sink allocation","logging error escaped producer");fake::logThrows=false;}
 for(int mode:{1,2,3}){fake::moduleMode=mode;auto old=fake::module;if(mode==3)fake::module=L"YeManCC.exe";const auto count=fake::calls;bool threw=false;try{nativeSendTaskView(777);}catch(const std::runtime_error&){threw=true;}check(threw&&fake::calls==count,"invalid executable path reached dispatcher");fake::module=old;fake::moduleMode=0;}
 std::cout<<json{{"pass",true},{"checks",fake::checks},{"realOSInput",false},{"realProcessOperations",false},{"scope","actual producer/queue-budget/async code with fake dispatcher and OS; not dispatcher or COM acceptance"}}.dump()<<'\n';return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
""".replace('__RECEIPT__',receipt_types).replace('__STATUS__',status).replace('__FUNCTIONS__','\n\n'.join(blocks.values()))
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
