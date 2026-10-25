"""Compiled consumers for the ordinary private-identity-ready forwarding edge.
Exact production predicates, callback, queue, and caller block; no live APIs.
"""
from pathlib import Path
import argparse,hashlib,importlib.util,json,os,subprocess
CPP=r'''
#include <algorithm>
#include <atomic>
#include <climits>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "json.hpp"
#include "steam_game_link_state.h"
#include "steam_original_recovery_policy.h"
#include "steam_native_record_identity.h"
using json=nlohmann::json;using DWORD=std::uint32_t;using ULONGLONG=unsigned long long;
enum class PowerLifecycle{Ready,Resuming,Suspending,Suspended};
std::atomic<bool> g_exitRequested{false},g_sleepRecoveryAutoEnabled{true};
std::atomic<ULONGLONG> g_sleepRecoveryAutoConfigEpoch{5},g_gameRulesEpoch{9};
std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
unsigned eventSignals=0;bool SetEvent(void*){++eventSignals;return true;}
namespace SteamOriginalRecovery{
using Trigger=ymcc::steamOriginal::Trigger;using Identity=ymcc::steamOriginal::Identity;using Scope=ymcc::steamOriginal::Scope;
@@PENDING@@
@@CONTROL@@
std::mutex lifecycleMx;std::shared_ptr<Control> control;
@@CURRENT@@
@@ENQUEUE@@
}
@@OWNER_READY@@
namespace SteamGameLink{using namespace ymcc::steamlink;
@@READY_PREDICATE@@
}
using namespace ymcc::steamlink;using namespace SteamOriginalRecovery;
constexpr DWORD originalApp=2388972698u;const std::string originalId="10260559608980439040";
struct Round{
 Evidence e;ymcc::steamrecordidentity::Read privateRecordIdentity;
 struct Client{DWORD pid=200,account=17;ULONGLONG created=2234;}client;
 struct Game{DWORD pid=100;ULONGLONG created=1234;}game;
 State state;std::uint32_t appid=originalApp;std::string gameId=originalId;std::uint64_t currentRecordRulesEpoch=9;
 json detail=json::object();
 Round(){state.begin(1,5,1);e.enabled=e.transportAvailable=true;e.configEpoch=5;e.game=e.steam=IdentityStatus::Same;e.account=AccountStatus::Same;e.tracker=TrackerStatus::Exact;e.primary=PrimaryStatus::Exact;e.validator=ValidatorStatus::Accepted;e.ui=UiStatus::RunningAndListed;privateRecordIdentity={76561197960265745ULL,10260559608980439040ULL,originalApp,true,"observed"};}
 void forward(){using SteamGameLink::originalRecoveryIdentityReady;
 @@READY_FORWARD@@
 }
};
unsigned checks=0;void check(bool value,const char*message){++checks;if(!value)throw std::runtime_error(message);}
Scope scope(){return{{300,1,3234,L"d:\\ymcc\\yemancc.exe"},{100,1,1234,L"d:\\game\\game.exe"},{200,1,2234,L"d:\\steam\\steam.exe"},9};}
void reset(){control=std::make_shared<Control>();auto c=control;g_exitRequested=false;g_sleepRecoveryAutoEnabled=true;g_sleepRecoveryAutoConfigEpoch=5;g_gameRulesEpoch=9;g_powerLifecycle=PowerLifecycle::Ready;eventSignals=0;c->enabled=true;c->ownsRunning=false;c->active=false;c->bindingQualified=true;c->bindingSerial=11;c->boundGame=scope().game;c->boundSteam=scope().steam;c->boundGameId=originalId;c->boundAppId=originalApp;c->observedTarget=c->boundGame;c->observedSteam=c->boundSteam;c->targetObserved=c->steamObserved=true;c->observedRules=9;}
void direct(DWORD gamePid=100,ULONGLONG born=1234,DWORD steamPid=200,ULONGLONG steamBorn=2234,const std::string&id=originalId,std::uint32_t appid=originalApp,ULONGLONG epoch=5,ULONGLONG rules=9){steamOriginalRecoveryIdentityReady(gamePid,born,steamPid,steamBorn,id,appid,epoch,rules);}
int main(){try{
 reset();Round round;round.privateRecordIdentity.available=false;round.forward();check(control->pending.empty(),"early unavailable identity queues nothing");round.privateRecordIdentity.available=true;round.forward();check(control->pending.size()==1,"ready ordinary read queues qualification");check(control->pending.front().kind==Trigger::TargetChanged&&control->pending.front().generation==0,"ready hint is TargetChanged observation only");check(control->bindingSerial==11&&control->boundGame==scope().game&&control->boundSteam==scope().steam,"ready hint does not replace binding");check(control->startupPending,"ready hint never clears startup pending");for(int i=0;i<20;i++)round.forward();check(control->pending.size()==1&&control->sequence==1&&control->observationSequence==1,"existing queue coalesces repeated ready reads");
 reset();control->active=true;Round{}.forward();check(control->pending.size()==1,"ready signal during finishing qualification is queued not lost");
 reset();control->ownsRunning=true;Round{}.forward();check(control->pending.empty(),"healthy owned instance queues nothing");
 using Bad=std::pair<const char*,std::function<void(Round&)>>;const std::vector<Bad> bad={
 {"missing private",[](auto&r){r.privateRecordIdentity.available=false;}},{"wrong full native id",[](auto&r){++r.privateRecordIdentity.gameId64;}},{"wrong full tracker id",[](auto&r){r.gameId="10260559608980439041";control->boundGameId=r.gameId;}},{"wrong app",[](auto&r){++r.privateRecordIdentity.appid;}},{"account mismatch",[](auto&r){++r.privateRecordIdentity.account64;}},{"zero account",[](auto&r){r.client.account=0;}},{"not enabled",[](auto&r){r.e.enabled=false;}},{"no transport",[](auto&r){r.e.transportAvailable=false;}},{"old read epoch",[](auto&r){r.e.configEpoch=4;}},{"game unknown",[](auto&r){r.e.game=IdentityStatus::Unknown;}},{"steam changed",[](auto&r){r.e.steam=IdentityStatus::Changed;}},{"account changed",[](auto&r){r.e.account=AccountStatus::Changed;}},{"tracker unknown",[](auto&r){r.e.tracker=TrackerStatus::Unknown;}},{"primary differs",[](auto&r){r.e.primary=PrimaryStatus::Different;}},{"validator rejected",[](auto&r){r.e.validator=ValidatorStatus::Rejected;}},{"UI not running",[](auto&r){r.e.ui=UiStatus::NotRunning;}},{"UI missing list",[](auto&r){r.e.ui=UiStatus::RunningMissingList;}},{"global OFF",[](auto&){g_sleepRecoveryAutoEnabled=false;}},{"epoch moved",[](auto&){g_sleepRecoveryAutoConfigEpoch=6;}},{"rules moved",[](auto&){g_gameRulesEpoch=10;}},{"power not ready",[](auto&){g_powerLifecycle=PowerLifecycle::Resuming;}},{"exit pending",[](auto&){g_exitRequested=true;}}
 };for(const auto&b:bad){reset();Round r;b.second(r);r.forward();check(control->pending.empty(),b.first);}
 using OwnerBad=std::pair<const char*,std::function<void()>>;const std::vector<OwnerBad> ownerBad={
 {"owner stopped",[]{control->stop=true;}},{"owner disabled",[]{control->enabled=false;}},{"owner healthy",[]{control->ownsRunning=true;}},{"owner global OFF",[]{g_sleepRecoveryAutoEnabled=false;}},{"owner epoch current",[]{g_sleepRecoveryAutoConfigEpoch=6;}},{"owner rules current",[]{g_gameRulesEpoch=10;}},{"owner power ready",[]{g_powerLifecycle=PowerLifecycle::Suspending;}},{"owner exit",[]{g_exitRequested=true;}},{"owner other game",[]{++control->boundGame.pid;}},{"owner game reuse",[]{++control->boundGame.birth;}},{"owner other steam",[]{++control->boundSteam.pid;}},{"owner steam reuse",[]{++control->boundSteam.birth;}},{"owner full id",[]{control->boundGameId="10260559608980439041";}},{"owner app id",[]{++control->boundAppId;}},{"owner changed observed target",[]{++control->observedTarget.pid;}},{"owner changed observed steam",[]{++control->observedSteam.birth;}},{"owner observed rules",[]{++control->observedRules;}}
 };for(const auto&b:ownerBad){reset();b.second();direct();check(control->pending.empty(),b.first);}
 reset();direct(0);check(control->pending.empty(),"zero game cannot match original");reset();direct(100,1234,200,2234,originalId,originalApp,0,9);check(control->pending.empty(),"zero epoch rejected");reset();direct(100,1234,200,2234,originalId,originalApp,5,0);check(control->pending.empty(),"zero rules rejected");reset();control.reset();direct();check(!control,"missing owner never recreated");
 reset();control->startupPending=false;auto admitted=control->admission.admit(scope(),{Trigger::Wake,7,1},true);check(admitted.accepted&&admitted.recoveryAllowed,"wake admitted before ready hint");Round{}.forward();check(control->pending.size()==1,"ready hint can only queue observation after consumed wake");check(!control->admission.preview(scope(),{Trigger::Wake,7,99},true).accepted,"ready hint never refunds consumed wake");check(!control->startupPending,"ready hint never revives startup");auto observation=control->admission.admit(scope(),{Trigger::TargetChanged,0,control->pending.front().sequence},true);check(observation.accepted&&!observation.recoveryAllowed,"requeued ordinary event cannot mutate");
 std::cout<<json({{"passed",true},{"checks",checks},{"exactProductionConsumers",true},{"liveProcessCalls",0},{"sleepActions",0},{"steamMutations",0}}).dump()<<"\n";return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
'''
def run(output):
 repo=Path(__file__).resolve().parents[1];out=Path(output).resolve()
 if out==repo or repo in out.parents or out in repo.parents:raise RuntimeError('fresh external output required')
 if out.exists() and any(out.iterdir()):raise RuntimeError('fresh output required')
 out.mkdir(parents=True,exist_ok=True)
 spec=importlib.util.spec_from_file_location('ready_source_extractor',repo/'tools/steam_original_recovery_authority_selftest.py');ex=importlib.util.module_from_spec(spec);spec.loader.exec_module(ex)
 paths=['native/steam_original_recovery_runtime.h','native/steam_game_link_runtime.h','native/steam_original_recovery_policy.h','native/steam_game_link_state.h','native/steam_native_record_identity.h','deps/json/json.hpp']
 sources={n:(repo/n).read_bytes() for n in paths};runtime=sources[paths[0]].decode('utf-8-sig');link=sources[paths[1]].decode('utf-8-sig')
 extracted={'PENDING':ex.extract(runtime,r'\bstruct\s+Pending\s*\{',True),'CONTROL':ex.extract(runtime,r'\bstruct\s+Control\s*\{',True),'CURRENT':ex.function(runtime,'current'),'ENQUEUE':ex.function(runtime,'enqueue'),'OWNER_READY':ex.function(runtime,'steamOriginalRecoveryIdentityReady'),'READY_PREDICATE':ex.function(link,'originalRecoveryIdentityReady')}
 body=ex.function(link,'run');extracted['READY_FORWARD']=ex.extract(body,r'\bif\s*\(originalRecoveryIdentityReady\s*\([^;{}]*\)\)\s*\{')
 location=body.index(extracted['READY_FORWARD']);bracket=body.index('const bool originalIdentityBracketMatched=')
 wiring={'afterFinalIdentityBracket':location>bracket,'afterExistingReadIdentityChecks':body.rfind('finishReadIdentity();',0,location)>0,'beforePublishingEvidence':body.index('publish(c,std::move(detail)')>location}
 cpp=CPP
 for k,v in extracted.items():
  token='@@'+k+'@@'
  if cpp.count(token)!=1:raise RuntimeError('missing production token:'+k)
  cpp=cpp.replace(token,v)
 for name,data in sources.items():
  if name not in paths[:2]:(out/Path(name).name).write_bytes(data)
 env={k:v for k,v in os.environ.items() if k.upper() not in ('CL','_CL_','LINK','_LINK_')}
 locked=r'''@echo off
setlocal
set "VCTOOLS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207"
set "SDK=C:\Program Files (x86)\Windows Kits\10"
set "SDKVERSION=10.0.26100.0"
set "INCLUDE=%VCTOOLS%\include;%SDK%\Include\%SDKVERSION%\um;%SDK%\Include\%SDKVERSION%\shared;%SDK%\Include\%SDKVERSION%\ucrt"
set "LIB=%VCTOOLS%\lib\x64;%SDK%\Lib\%SDKVERSION%\um\x64;%SDK%\Lib\%SDKVERSION%\ucrt\x64"
set "PATH=%SystemRoot%\System32;%VCTOOLS%\bin\Hostx64\x64;%PATH%"
'''
 variants=[('positive',None),('old-missing-forward',('steamOriginalRecoveryIdentityReady(game.pid,game.created,client.pid,client.created,gameId,appid,\n                    e.configEpoch,currentRecordRulesEpoch);','(void)game;')),('ignore-account',('&&std::uint32_t(privateIdentity.account64)==account','')),('ignore-full-id',('&&gameId==std::to_string(fullId)','')),('owner-ignore-epoch',('g_sleepRecoveryAutoConfigEpoch.load()!=configEpoch','false')),('owner-ignore-owned',('||c->ownsRunning.load()','')),('refund-consumed',('SteamOriginalRecovery::enqueue(c,ymcc::steamOriginal::Trigger::TargetChanged);','c->admission=ymcc::steamOriginal::Admission{};SteamOriginalRecovery::enqueue(c,ymcc::steamOriginal::Trigger::TargetChanged);'))]
 results=[]
 for name,mutation in variants:
  code=cpp
  if mutation:
   if code.count(mutation[0])!=1:raise RuntimeError('negative mutation not unique:'+name)
   code=code.replace(*mutation)
  case=out/name;case.mkdir();(case/'fixture.cpp').write_text(code,encoding='utf-8');script=case/'Build.cmd';script.write_text(locked+f'cd /d "{case}"\ncl /nologo /std:c++20 /O2 /EHsc /MT /utf-8 fixture.cpp /I"{out}" /Fe:fixture.exe\nexit /b %errorlevel%\n',encoding='ascii')
  built=subprocess.run(['cmd.exe','/d','/c',str(script)],capture_output=True,env=env);(case/'compile.stdout').write_bytes(built.stdout);(case/'compile.stderr').write_bytes(built.stderr)
  item={'name':name,'compileReturnCode':built.returncode,'sourceSha256':hashlib.sha256(code.encode()).hexdigest()}
  if built.returncode==0:
   ran=subprocess.run([str(case/'fixture.exe')],capture_output=True,env=env);(case/'run.stdout').write_bytes(ran.stdout);(case/'run.stderr').write_bytes(ran.stderr);item['returnCode']=ran.returncode;item['passed']=ran.returncode==(0 if name=='positive' else 1)
   if name=='positive' and ran.returncode==0:item['result']=json.loads(ran.stdout)
  else:item['passed']=False
  results.append(item)
 drift=[n for n,b in sources.items() if (repo/n).read_bytes()!=b]
 result={'schema':'ymcc.exact-production-identity-ready-consumer.v1','passed':all(x['passed'] for x in results) and all(wiring.values()) and not drift,'variants':results,'callerWiring':wiring,'productionExtracts':{k:hashlib.sha256(v.encode()).hexdigest() for k,v in extracted.items()},'sourceDrift':drift,'liveProcessCalls':0,'steamMutations':0,'sleepActions':0,'actualPassivePushCoverageProven':False}
 (out/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');return result
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args();r=run(a.output);print(json.dumps(r));raise SystemExit(0 if r['passed'] else 1)
