"""Bounded offline consumers of actual original-recovery authority sources.
Only compiler/owned fixture executables run. No Windows/Steam/process APIs are
included by the fixture. Native sources are never edited; exact extracted text,
frozen production headers and hashes remain in the explicit output directory.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys


def masked_cpp(text):
    tokens = re.compile(r'//[^\n]*|/\*.*?\*/|(?:u8|u|U|L)?R"(?P<tag>[^ ()\\\t\r\n]{0,16})\(.*?\)(?P=tag)"|"(?:\\.|[^"\\])*"|\x27(?:\\.|[^\x27\\])*\x27', re.S)
    return tokens.sub(lambda m: ''.join('\n' if c == '\n' else ' ' for c in m.group()), text)


def extract(text, pattern, semicolon=False):
    masked = masked_cpp(text)
    matches = list(re.finditer(pattern, masked))
    if len(matches) != 1:
        raise RuntimeError('source extraction is ambiguous/missing: ' + pattern)
    start = matches[0].start()
    opening = masked.index('{', matches[0].start())
    depth, end = 1, opening + 1
    while depth and end < len(masked):
        depth += (masked[end] == '{') - (masked[end] == '}')
        end += 1
    if depth:
        raise RuntimeError('unterminated source definition')
    if semicolon:
        while end < len(masked) and masked[end].isspace():
            end += 1
        if masked[end] != ';':
            raise RuntimeError('source statement lost its semicolon')
        end += 1
    return text[start:end]


def function(text, name):
    return extract(text, r'\bstatic\b[^;{}]*?\b' + re.escape(name) + r'\s*\([^;{}]*\)\s*\{')


def wiring(text):
    """Structural caller check, separately reported from executed consumers."""
    clean = masked_cpp(text)
    hits = list(re.finditer(r'\bconst\s+auto\s+tracker\s*=\s*tracking\s*\(\s*client\s*,\s*game\s*\)\s*;', clean))
    rows = []
    for hit in hits:
        tail = clean[hit.end():]
        connected = re.match(r'\s*steamOriginalRecoveryTrackerObserved\s*\(', tail) is not None
        rows.append({'line': text.count('\n', 0, hit.start()) + 1, 'immediateRevocationCallback': connected})
    return {'passed': len(rows) >= 2 and all(row['immediateRevocationCallback'] for row in rows), 'sites': rows}


CPP = r"""
#include <algorithm>
#include <atomic>
#include <climits>
#include <condition_variable>
#include <cstdint>
#include <cwctype>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "steam_original_recovery_policy.h"
#include "steam_original_recovery_wake_watch.h"
#include "sleep_recovery_diagnostics.h"
#include "sleep_steam_tracking.h"
using json=nlohmann::json;using DWORD=std::uint32_t;using ULONGLONG=unsigned long long;
constexpr DWORD MAXDWORD=UINT32_MAX;
enum class PowerLifecycle {Ready,Resuming,Suspending,Suspended};
std::atomic<bool> g_sleepRecoveryAutoEnabled{true},g_sleepRecoveryAutoPending{false},g_exitRequested{false};
std::atomic<ULONGLONG> g_sleepRecoveryAutoConfigEpoch{5},g_gameRulesEpoch{9};
std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
std::condition_variable g_sgWorkCv;
ULONGLONG generation=7,now=1000000000ULL;
unsigned dispatches=0,targetReads=0,identityReads=0,preferenceWrites=0,facts=0,notifications=0;
bool targetAvailable=true,operationAllowed=true;
ULONGLONG currentPowerGeneration(){return generation;}
ULONGLONG sgNowFileTime(){return now;}
bool ProcessIdToSessionId(DWORD pid,DWORD* value){++identityReads;*value=1;return pid!=0;}
bool ymSettingsPatchSection(const char*,const json&){++preferenceWrites;return true;}
void sgRecordFact(const char*,const json&){++facts;}
namespace SleepRecoveryAuto {
std::mutex mx,setMx;
struct State {ULONGLONG generation=7,configEpoch=5;json wakeAdmission;std::string phase,reason;json steamStatus,inputStatus;} state;
struct KernelWake {ULONGLONG generation=7,configEpoch=5,boundaryFileTime=0,eventFileTime=0;int eventId=507;bool uiWakeRequested=true;ULONGLONG requestGeneration=7;} kernelWake;
struct KernelSleepEntry {ULONGLONG configEpoch=5,generation=7,eventFileTime=0;} kernelSleepEntry;
bool wakeOperationAllows(ULONGLONG,ULONGLONG){return operationAllowed;}
json get(){return {{"enabled",g_sleepRecoveryAutoEnabled.load()}};}
void notify(){++notifications;}
@@SETTING_SET@@
}
struct NativeDetectedGame {DWORD pid=100;ULONGLONG processCreated=1234;std::wstring path=L"D:\\games\\game.exe";} liveGame;
bool nativeValveReadCurrentTarget(NativeDetectedGame* out,unsigned long long*){++targetReads;*out=liveGame;return targetAvailable;}
namespace SleepSteamRecovery {
struct Session {DWORD pid=200;ULONGLONG created=2234;std::wstring root=L"D:\\steam";} live;
Session session(){return live;}
}
namespace SteamOriginalRecovery {
using Trigger=ymcc::steamOriginal::Trigger;using Identity=ymcc::steamOriginal::Identity;using Scope=ymcc::steamOriginal::Scope;
@@PENDING@@
@@CONTROL@@
std::mutex lifecycleMx;std::shared_ptr<Control> control;
@@CURRENT@@
@@IDENTITY@@
@@WAKE_PROOF@@
@@SAME_SCOPE@@
// Forbidden action boundaries: any future callback use is counted, never run.
void enqueue(const std::shared_ptr<Control>&,Trigger,unsigned long long=0){++dispatches;}
}
@@TRACKER_OBSERVED@@
using namespace SteamOriginalRecovery;
unsigned checks=0;
void check(bool value,const char* label){++checks;if(!value)throw std::runtime_error(label);}
const std::string originalId="10307630032478011392";constexpr std::uint32_t originalApp=2399932135u;
Scope scopeValue(){return {{300,1,3234,L"d:\\ymcc\\yemancc.exe"},identity(liveGame.pid,liveGame.processCreated,liveGame.path),identity(SleepSteamRecovery::live.pid,SleepSteamRecovery::live.created,SleepSteamRecovery::live.root+L"\\steam.exe"),9};}
void reset(){
 control=std::make_shared<Control>();liveGame={};SleepSteamRecovery::live={};generation=7;now=1000000000ULL;
 g_sleepRecoveryAutoEnabled=true;g_sleepRecoveryAutoPending=false;g_sleepRecoveryAutoConfigEpoch=5;g_gameRulesEpoch=9;g_exitRequested=false;g_powerLifecycle=PowerLifecycle::Ready;
 targetAvailable=operationAllowed=true;dispatches=targetReads=identityReads=preferenceWrites=facts=notifications=0;
 SleepRecoveryAuto::state={};SleepRecoveryAuto::kernelWake={};SleepRecoveryAuto::kernelSleepEntry={};
 auto& witness=SleepRecoveryAuto::kernelWake;witness.eventFileTime=now;witness.boundaryFileTime=now-1000;
 SleepRecoveryAuto::state.wakeAdmission={{"status","admitted"},{"kernelWakeConfirmed",true},{"proofFresh",true},
  {"proofFollowedPreparedBoundary",false},{"boundaryGeneration",7},{"proofObservedGeneration",7},
  {"eventFileTime",std::to_string(now)},{"boundaryFileTime",std::to_string(now-1000)},{"source","kernel-power-507-reason-1"}};
 const auto s=scopeValue();control->active=true;control->latestPower=7;control->bindingSerial=11;control->bindingQualified=true;
 control->boundGame=s.game;control->boundSteam=s.steam;control->boundGameId=originalId;control->boundAppId=originalApp;control->ownsRunning=true;
}
std::function<bool()> capturedCurrent(Trigger kind=Trigger::Wake,bool recover=true){
 auto c=current();const auto scope=scopeValue();Pending pending{kind,7,1};
 const auto configEpoch=g_sleepRecoveryAutoConfigEpoch.load();const auto bindingSerial=c->bindingSerial;
 const auto proof=wakeProof(pending.generation,configEpoch);
 @@SCOPE_LAMBDA@@
 return [scopeNow,recover]{return scopeNow(recover);};
}
void observed(const std::string& id=originalId,std::uint32_t appid=originalApp,bool exact=true){steamOriginalRecoveryTrackerObserved(100,1234,200,2234,id,appid,exact);}
void publishReadBracket(const std::function<void()>& duringRead={}){
 auto c=current();const auto scope=scopeValue();
 @@BINDING_BEFORE@@
 // This is the sole mocked tracking read boundary, including its possible callback.
 if(duringRead)duringRead();
 struct {std::string gameId;} tracked{originalId};const auto expectedApp=originalApp;
 @@BINDING_PUBLISH@@
}
json fieldReplay;
void consumers(){
 reset();auto proof=wakeProof(7,5);check(proof.valid(7,5,now),"current proof accepted");auto guard=capturedCurrent();check(guard(),"current captured proof accepted");
 using Mutation=std::pair<const char*,std::function<void()>>;
 const std::vector<Mutation> wrong={
  {"wrong observed generation",[]{SleepRecoveryAuto::kernelWake.generation=8;}},
  {"wrong observed epoch",[]{SleepRecoveryAuto::kernelWake.configEpoch=4;}},
  {"wrong state generation",[]{SleepRecoveryAuto::state.generation=8;}},
  {"wrong state epoch",[]{SleepRecoveryAuto::state.configEpoch=4;}},
  {"wrong requested generation",[]{SleepRecoveryAuto::kernelWake.requestGeneration=8;}},
  {"not requested",[]{SleepRecoveryAuto::kernelWake.uiWakeRequested=false;}},
  {"wrong kernel event",[]{SleepRecoveryAuto::kernelWake.eventId=506;}},
  {"wrong source",[]{SleepRecoveryAuto::state.wakeAdmission["source"]="pbt-resume";}},
  {"not admitted",[]{SleepRecoveryAuto::state.wakeAdmission["status"]="waiting";}},
  {"not confirmed",[]{SleepRecoveryAuto::state.wakeAdmission["kernelWakeConfirmed"]=false;}},
  {"not fresh",[]{SleepRecoveryAuto::state.wakeAdmission["proofFresh"]=false;}},
  {"wrong admission generation",[]{SleepRecoveryAuto::state.wakeAdmission["proofObservedGeneration"]=8;}},
  {"wrong event filetime",[]{SleepRecoveryAuto::state.wakeAdmission["eventFileTime"]="1000000001";}},
  {"numeric filetime refused",[]{SleepRecoveryAuto::state.wakeAdmission["eventFileTime"]=1000000000ULL;}},
  {"wrong observed boundary",[]{SleepRecoveryAuto::kernelWake.boundaryFileTime-=1;}},
  {"wrong admission boundary",[]{SleepRecoveryAuto::state.wakeAdmission["boundaryFileTime"]="999998999";}},
  {"zero boundary",[]{SleepRecoveryAuto::state.wakeAdmission["boundaryFileTime"]="0";}},
  {"later sleep supersedes wake",[]{SleepRecoveryAuto::kernelSleepEntry.eventFileTime=now;}},
  {"future wake",[]{--now;}},
  {"expired wake",[]{now+=60000ULL*10000;}}
 };
 for(const auto& test:wrong){reset();const auto captured=capturedCurrent();test.second();check(wakeProof(7,5).generation==0,test.first);check(!captured(),test.first);}
 reset();SleepRecoveryAuto::kernelWake.eventId=107;SleepRecoveryAuto::state.wakeAdmission["source"]="kernel-power-107-target-5";check(wakeProof(7,5).eventId==107,"107 exact source accepted");
 reset();now+=59999ULL*10000;check(wakeProof(7,5).generation==7,"59999ms accepted");
 reset();SleepRecoveryAuto::kernelWake.generation=6;SleepRecoveryAuto::state.wakeAdmission["proofObservedGeneration"]=6;
 SleepRecoveryAuto::state.wakeAdmission["proofFollowedPreparedBoundary"]=true;SleepRecoveryAuto::state.wakeAdmission["boundaryGeneration"]=6;
 check(wakeProof(7,5).generation==7,"exact prepared next generation accepted");
 SleepRecoveryAuto::state.wakeAdmission["boundaryGeneration"]=5;check(wakeProof(7,5).generation==0,"wrong transferred boundary refused");
 reset();guard=capturedCurrent();++SleepRecoveryAuto::kernelWake.eventFileTime;
 SleepRecoveryAuto::state.wakeAdmission["eventFileTime"]=std::to_string(SleepRecoveryAuto::kernelWake.eventFileTime);++now;
 check(wakeProof(7,5).generation==7,"new witness independently valid");check(!guard(),"different valid witness cannot replace captured proof");
 reset();guard=capturedCurrent();auto observation=capturedCurrent(Trigger::TargetChanged,false);
 SleepRecoveryAuto::set({{"enabled",false}});check(!guard()&&!observation(),"actual setting OFF revokes captured authority");
 SleepRecoveryAuto::set({{"enabled",true}});check(g_sleepRecoveryAutoConfigEpoch==7&&preferenceWrites==2,"actual OFF ON advances epoch twice");
 check(!guard()&&!observation(),"actual setting OFF ON cannot revive original epoch");check(wakeProof(7,7).generation==0,"old witness cannot join new setting epoch");
 reset();guard=capturedCurrent();const auto serial=control->bindingSerial;const auto beforeReads=targetReads;
 for(unsigned i=0;i<20;++i)observed();check(control->bindingSerial==serial&&guard(),"identical heartbeat preserves captured serial");
 check(control->admission.preview(scopeValue(),{Trigger::Wake,7,99},true).accepted,"heartbeats do not consume admission");
 check(dispatches==0&&targetReads==beforeReads+1,"heartbeat callback does not elect or dispatch");
 for(int change=0;change<4;++change){reset();guard=capturedCurrent();const auto oldGame=control->boundGame,oldSteam=control->boundSteam;
  const auto oldSerial=control->bindingSerial;const auto reads=targetReads;
  if(change==0)observed("10307630032478011393",originalApp,true);
  if(change==1)observed(originalId,2388972698u,true);
  if(change==2)observed(originalId,originalApp,false);
  if(change==3)observed("",0,false);
  check(!control->bindingQualified&&!control->ownsRunning&&control->bindingSerial==oldSerial+1,"changed or unknown full binding revoked");
  check(!guard(),"captured serial refuses revoked full binding");
  check(control->boundGame==oldGame&&control->boundSteam==oldSteam&&control->boundGameId==originalId&&control->boundAppId==originalApp,"callback cannot select replacement identity");
  check(dispatches==0&&control->pending.empty()&&control->sequence==0&&targetReads==reads,"revocation callback has no dispatch or PID lookup");
 }
 reset();guard=capturedCurrent();control->bindingSerial=ULLONG_MAX;observed("different");check(control->bindingSerial==ULLONG_MAX&&!control->bindingQualified&&!guard(),"serial saturation never wraps authority");
 reset();control->bindingQualified=false;control->bindingSerial=11;publishReadBracket();check(control->bindingQualified&&control->bindingSerial==12,"actual stable tracking bracket publishes binding");
 reset();const auto oldSerial=control->bindingSerial;publishReadBracket([]{observed("changed-during-read");});
 check(!control->bindingQualified&&control->bindingSerial==oldSerial+1,"actual callback during tracking read prevents publication");
 reset();const auto scope=scopeValue();check(control->admission.admit(scope,{Trigger::Wake,7,1},true).accepted,"actual admission spends wake budget");
 observed("changed");check(!control->admission.preview(scope,{Trigger::Wake,7,2},true).accepted,"revocation does not refund wake budget");
 control->admission.invalidate();publishReadBracket();observed();check(!control->admission.admit(scope,{Trigger::Wake,7,3},true).accepted,"rebind heartbeat and invalidate do not refund wake budget");
 SleepRecoveryAuto::set({{"enabled",false}});SleepRecoveryAuto::set({{"enabled",true}});
 check(!control->admission.preview(scope,{Trigger::Wake,7,4},true).accepted,"setting epoch change does not refund wake budget");
 reset();ymcc::steamOriginal::WakeWatch watch;watch.eventFileTime=now;
 check(watch.maximumProbes==320&&watch.maximumFastProbes==240&&watch.fastPollMs==50&&watch.fastWindowMs==2000&&watch.pollMs==10000,"bounded two-rate wake policy");
 check(watch.periodMs(now+1999ULL*10000)==50&&watch.periodMs(now+2000ULL*10000)==10000,"fast cadence ends at physical 2s boundary");
 check(watch.noteHint(now+3000ULL*10000,true)&&watch.periodMs(now+5999ULL*10000)==50&&watch.periodMs(now+6000ULL*10000)==10000,"related event grants only a three second observation burst");
 check(watch.observes(now+54999ULL*10000,true)&&!watch.observes(now+55000ULL*10000,true),"55s read observation boundary");
 check(watch.allows(now+59999ULL*10000,true)&&!watch.allows(now+60000ULL*10000,true),"60s absolute business boundary");
 watch=ymcc::steamOriginal::WakeWatch{};watch.eventFileTime=now;
 for(unsigned i=0;i<watch.maximumProbes;++i)check(watch.reserveProbe(now,true),"bounded probe reservation");
 check(!watch.reserveProbe(now,true)&&watch.probes==watch.maximumProbes,"probe budget never refunds");

 // Field bytes are data, not evidence of a successful live repair. The fixture
 // has admission JSON but no raw kernelWake struct: missing observed ports are
 // explicitly reconstructed here for an offline consistency test only.
 reset();const auto sample=json::parse(R"G3E(@@FIELD_SAMPLE@@)G3E");
 const auto& recorded=sample.at("wakeAdmission");
 const auto fieldGeneration=sample.at("generation").get<ULONGLONG>(),fieldEpoch=sample.at("configEpoch").get<ULONGLONG>();
 const auto eventAt=std::stoull(recorded.at("eventFileTime").get<std::string>());
 const auto boundaryAt=std::stoull(recorded.at("boundaryFileTime").get<std::string>());
 const auto elapsed=sample.at("settling").at("lastElapsedMs").get<ULONGLONG>();
 generation=fieldGeneration;g_sleepRecoveryAutoConfigEpoch=fieldEpoch;
 SleepRecoveryAuto::state.generation=fieldGeneration;SleepRecoveryAuto::state.configEpoch=fieldEpoch;
 SleepRecoveryAuto::state.wakeAdmission=recorded;
 auto& fieldWitness=SleepRecoveryAuto::kernelWake;
 fieldWitness.generation=recorded.at("proofObservedGeneration").get<ULONGLONG>();fieldWitness.configEpoch=fieldEpoch;
 fieldWitness.eventFileTime=eventAt;fieldWitness.boundaryFileTime=boundaryAt;
 fieldWitness.eventId=recorded.at("source")=="kernel-power-507-reason-1"?507:107;
 fieldWitness.requestGeneration=fieldGeneration;fieldWitness.uiWakeRequested=true;
 SleepRecoveryAuto::kernelSleepEntry.configEpoch=fieldEpoch;
 SleepRecoveryAuto::kernelSleepEntry.eventFileTime=std::stoull(recorded.at("latestEntryFileTime").get<std::string>());
 now=eventAt+elapsed*10000ULL;
 const auto fieldProof=wakeProof(fieldGeneration,fieldEpoch);
 check(fieldProof.generation==fieldGeneration&&fieldProof.epoch==fieldEpoch&&fieldProof.boundaryFileTime==boundaryAt,"field admission drives actual wakeProof join");
 check(fieldGeneration==2&&fieldEpoch==1&&elapsed==58547,"field generation epoch and elapsed preserved");
 ymcc::steamOriginal::WakeWatch fieldWatch;fieldWatch.eventFileTime=eventAt;
 check(!fieldWatch.observes(now,true)&&fieldWatch.allows(now,true),"field 58547ms sample is outside 55s reads but inside 60s deadline");
 const auto remainingMs=fieldWatch.budgetMs-elapsed;
 now+=remainingMs*10000ULL;check(wakeProof(fieldGeneration,fieldEpoch).generation==0&&!fieldWatch.allows(now,true),"field remaining 1453ms cannot extend physical wake deadline");
 now=eventAt+elapsed*10000ULL;
 g_sleepRecoveryAutoConfigEpoch=fieldEpoch+1;
 check(wakeProof(fieldGeneration,fieldEpoch).generation==0,"field proof refuses changed global epoch even with original request epoch");
 g_sleepRecoveryAutoConfigEpoch=fieldEpoch;
 SleepRecoveryAuto::set({{"enabled",false}});SleepRecoveryAuto::set({{"enabled",true}});
 check(g_sleepRecoveryAutoConfigEpoch==fieldEpoch+2,"field epoch advances through actual preference OFF ON");
 check(wakeProof(fieldGeneration,fieldEpoch).generation==0&&wakeProof(fieldGeneration,fieldEpoch+2).generation==0,"field old witness cannot authorize either original or reenabled epoch");
 liveGame.pid=sample.at("originalGame").at("pid").get<DWORD>();
 liveGame.processCreated=std::stoull(sample.at("originalGame").at("birthFileTime").get<std::string>());
 const auto originalScope=scopeValue();control->boundGame=originalScope.game;control->boundSteam=originalScope.steam;
 control->boundGameId=sample.at("originalGame").at("gameId").get<std::string>();
 control->boundAppId=sample.at("originalGame").at("appid").get<std::uint32_t>();control->bindingQualified=true;
 const auto extraPid=sample.at("extraProcessPid").get<DWORD>();
 steamOriginalRecoveryTrackerObserved(extraPid,0,originalScope.steam.pid,originalScope.steam.birth,"",0,false);
 check(control->boundGame.pid==15780&&extraPid==2448&&control->boundGameId=="10307630032478011392","field original PID full ID and extra PID are preserved without selecting replacement");
 check(!control->bindingQualified&&dispatches==0,"field extra unknown instance revokes only and never dispatches");
 check(sample.at("nativeOriginalRecoveryStatus")=="disabled"&&sample.at("nativeOriginalRecoveryCallsProven")==0&&sample.at("userInitialSuccessFeedbackWithdrawn")==true&&!sample.at("repairAccepted").get<bool>(),"field disabled native owner and withdrawn feedback are never promoted to success");
 fieldReplay={{"originalPid",liveGame.pid},{"extraPid",extraPid},{"gameId",control->boundGameId},
  {"generation",fieldGeneration},{"configEpoch",fieldEpoch},{"boundaryFileTime",std::to_string(boundaryAt)},
  {"elapsedMs",elapsed},{"remainingMs",remainingMs},{"nativeOriginalStatus",sample.at("nativeOriginalRecoveryStatus")},
  {"nativeCallsProven",sample.at("nativeOriginalRecoveryCallsProven")},{"feedbackWithdrawn",sample.at("userInitialSuccessFeedbackWithdrawn")},
  {"rawObservedWitnessReconstructed",true},{"fieldAcceptance",false},{"repairAccepted",false}};
 check(dispatches==0,"no business dispatch executed");
}
int main(){try{consumers();std::cout<<json({{"status","PASS"},{"checks",checks},{"actualSourceConsumers",true},{"liveProcessPorts",false},{"dispatches",dispatches},{"fieldReplay",fieldReplay}}).dump()<<"\n";return 0;}
 catch(const std::exception& e){std::cerr<<json({{"status","FAIL"},{"checks",checks},{"failure",e.what()}}).dump()<<"\n";return 1;}}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--negative-controls', action='store_true')
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    out = args.output.resolve()
    if out == repo or repo in out.parents or out in repo.parents:
        raise RuntimeError('output must be outside the source repository')
    if out.exists() and any(out.iterdir()):
        raise RuntimeError('fresh output required; earlier evidence is retained')
    out.mkdir(parents=True, exist_ok=True)
    names = ['native/steam_original_recovery_runtime.h', 'native/steam_original_recovery_wake_watch.h',
             'native/steam_original_recovery_policy.h', 'native/sleep_recovery_auto.h',
             'native/sleep_recovery_diagnostics.h', 'native/sleep_steam_tracking.h',
             'native/steam_game_link_runtime.h', 'deps/json/json.hpp',
             'tools/fixtures/steam_original_recovery_g3e_20261021.json']
    sources = {name: (repo / name).read_bytes() for name in names}
    runtime = sources[names[0]].decode('utf-8-sig')
    auto = sources['native/sleep_recovery_auto.h'].decode('utf-8-sig')
    extracted = {key: function(runtime, name) for key, name in {
        'CURRENT':'current', 'IDENTITY':'identity', 'WAKE_PROOF':'wakeProof',
        'SAME_SCOPE':'sameScopeNow', 'TRACKER_OBSERVED':'steamOriginalRecoveryTrackerObserved'}.items()}
    extracted['SETTING_SET'] = function(auto, 'set')
    for key in ('Pending', 'Control'):
        extracted[key.upper()] = extract(runtime, r'\bstruct\s+' + key + r'\s*\{', True)
    run_one = function(runtime, 'runOne')
    extracted['SCOPE_LAMBDA'] = extract(run_one, r'\bconst\s+auto\s+scopeNow\s*=[^;{}]*\{', True)
    prefix = run_one.index('unsigned long long bindingBefore=0;')
    end_prefix = run_one.index('const auto tracked=', prefix)
    extracted['BINDING_BEFORE'] = run_one[prefix:end_prefix]
    start_publish = run_one.index('unsigned long long bindingSerial=0;')
    end_publish = run_one.index('const auto scopeNow=', start_publish)
    extracted['BINDING_PUBLISH'] = run_one[start_publish:end_publish]
    # Verify extraction, including decoys, incomplete definitions and ambiguity.
    decoy = 'static void chosen(); /* static void chosen(){} */ static void chosen(){const char* x=R"tag(} static void chosen(){)tag";}'
    if not function(decoy, 'chosen').endswith('tag";}'):
        raise RuntimeError('extractor decoy control failed')
    for broken in ('static void chosen(){', 'static void chosen(){} static void chosen(){}'):
        try:
            function(broken, 'chosen')
        except RuntimeError:
            pass
        else:
            raise RuntimeError('extractor accepted ambiguous/incomplete code')
    cpp = CPP
    for key, value in extracted.items():
        token = '@@' + key + '@@'
        if cpp.count(token) != 1:
            raise RuntimeError('missing/duplicate consumer slot: ' + key)
        cpp = cpp.replace(token, value)
    sample_path = 'tools/fixtures/steam_original_recovery_g3e_20261021.json'
    sample_text = sources[sample_path].decode('utf-8-sig')
    sample = json.loads(sample_text)
    if sample.get('schema') != 'ymcc.steam-original-recovery.g3e-field-case.v1' or ')G3E\"' in sample_text:
        raise RuntimeError('unexpected field sample schema or raw literal delimiter')
    cpp = cpp.replace('@@FIELD_SAMPLE@@', sample_text)
    if '@@' in cpp:
        raise RuntimeError('unfilled production consumer slot')
    (out / 'field-sample.json').write_bytes(sources[sample_path])
    frozen = out / 'production'
    frozen.mkdir()
    for name, data in sources.items():
        if name.startswith('native/'):
            (frozen / Path(name).name).write_bytes(data)
    provenance = {'sources': [{'path': name, 'sha256': hashlib.sha256(data).hexdigest()} for name, data in sources.items()],
                  'exactFunctionsAndStatements': {key: {'sha256': hashlib.sha256(value.encode()).hexdigest(), 'text': value} for key, value in extracted.items()},
                  'mockedBoundaries': ['clock', 'process/session identity snapshots', 'preference persistence', 'notify/log counters', 'tracking read timing'],
                  'modelOfAuthorityLogic': False, 'wholeRunOneOrTrackerThreadExecuted': False,
                  'fieldSample': {'path': sample_path, 'archiveSha256': sample['archiveSha256'],
                                  'rawKernelWitnessPresent': False, 'missingWitnessPortsMockedFromAdmission': True,
                                  'fieldAcceptance': False}}
    (out / 'source-binding.json').write_text(json.dumps(provenance, indent=2) + '\n', encoding='utf-8')
    vs = Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat')
    sdk = Path(r'C:\Program Files (x86)\Windows Kits\10')
    env = {key: value for key, value in os.environ.items() if key.upper() not in ('CL', '_CL_', 'LINK', '_LINK_')}
    (out / 'temp').mkdir()
    env.update(TEMP=str(out / 'temp'), TMP=str(out / 'temp'), VSCMD_SKIP_SENDTELEMETRY='1')

    def execute(label, text, header_mutation=None):
        case = out / label
        case.mkdir()
        include = frozen
        if header_mutation:
            include = case / 'production'
            include.mkdir()
            for path in frozen.iterdir():
                data = path.read_bytes()
                if path.name == header_mutation[0]:
                    old, new = header_mutation[1:]
                    if data.count(old) != 1:
                        raise RuntimeError('mutation is no longer unique')
                    data = data.replace(old, new, 1)
                (include / path.name).write_bytes(data)
        source, exe = case / 'consumer.cpp', case / 'consumer.exe'
        source.write_text(text, encoding='utf-8')
        batch = case / 'compile.cmd'
        includes = ';'.join(str(sdk / 'Include/10.0.26100.0' / x) for x in ('ucrt', 'shared', 'um'))
        libs = ';'.join(str(sdk / 'Lib/10.0.26100.0' / x / 'x64') for x in ('ucrt', 'um'))
        batch.write_text(f'@echo off\ncall "{vs}" >nul\nset "INCLUDE={includes};%INCLUDE%"\nset "LIB={libs};%LIB%"\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /W3 "{source}" /I"{include}" /I"{repo / "deps/json"}" /Fo"{case / "consumer.obj"}" /Fe"{exe}"\nexit /b %errorlevel%\n', encoding='ascii')
        build = subprocess.run(['cmd.exe', '/d', '/c', str(batch)], capture_output=True, env=env, timeout=60)
        (case / 'compile.log').write_bytes(build.stdout + build.stderr)
        if build.returncode:
            return {'compiled': False, 'compileReturnCode': build.returncode}
        run = subprocess.run([str(exe)], capture_output=True, env=env, timeout=15)
        (case / 'consumer.log').write_bytes(run.stdout + run.stderr)
        rows = [json.loads(line) for line in (run.stdout + run.stderr).decode('utf-8').splitlines() if line.startswith('{')]
        return {'compiled': True, 'returnCode': run.returncode, 'result': rows[-1] if rows else None}

    positive = execute('positive', cpp)
    controls = []
    if args.negative_controls and positive.get('returnCode') == 0:
        cases = [
            ('wrong-observed-epoch', None, ('steam_original_recovery_wake_watch.h', b'observedEpoch==epoch&&', b''), 'wrong observed epoch'),
            ('missing-setting-epoch', ('if(configEpoch&&g_sleepRecoveryAutoConfigEpoch.load()!=configEpoch)return false;', 'if(false)return false;'), None, 'actual setting OFF revokes captured authority'),
            ('ignore-full-id', ('&&c->boundGameId==gameId', ''), None, 'changed or unknown full binding revoked'),
            ('replace-captured-proof', ('wakeProof(pending.generation,configEpoch)==proof', 'true'), None, 'wrong observed generation'),
            ('refund-budget', ('c->bindingQualified=false;c->ownsRunning=false;', 'c->bindingQualified=false;c->ownsRunning=false;c->admission=ymcc::steamOriginal::Admission{};'), None, 'revocation does not refund wake budget')]
        for label, replacement, header, expected in cases:
            text = cpp
            if replacement:
                if text.count(replacement[0]) != 1:
                    raise RuntimeError('control mutation is no longer unique: ' + label)
                text = text.replace(*replacement, 1)
            result = execute(label, text, header)
            result.update(label=label, expectedFailure=expected)
            result['detected'] = result.get('compiled', False) and result.get('returnCode') == 1 and result.get('result', {}).get('failure') == expected
            controls.append(result)
    caller = wiring(sources['native/steam_game_link_runtime.h'].decode('utf-8-sig'))
    # Exercise the real valve reader changed by the guard optimization, with
    # its unchanged predicates and three required-read negative controls.
    valve_run = subprocess.run([sys.executable, str(repo / 'tools/steam_original_recovery_valve_selftest.py'),
        '--output', str(out / 'valve-unused-rules')], capture_output=True)
    (out / 'valve-unused-rules.stdout').write_bytes(valve_run.stdout)
    (out / 'valve-unused-rules.stderr').write_bytes(valve_run.stderr)
    valve_result_path = out / 'valve-unused-rules/result.json'
    valve_result = json.loads(valve_result_path.read_text(encoding='utf-8')) if valve_result_path.exists() else {}
    valve_passed = valve_run.returncode == 0 and valve_result.get('passed') is True
    ready_run = subprocess.run([sys.executable, str(repo / 'tools/steam_original_recovery_identity_ready_selftest.py'),
        '--output', str(out / 'identity-ready')], capture_output=True)
    (out / 'identity-ready.stdout').write_bytes(ready_run.stdout)
    (out / 'identity-ready.stderr').write_bytes(ready_run.stderr)
    ready_path = out / 'identity-ready/result.json'
    ready_result = json.loads(ready_path.read_text(encoding='utf-8')) if ready_path.exists() else {}
    ready_passed = ready_run.returncode == 0 and ready_result.get('passed') is True
    session_run = subprocess.run([sys.executable, str(repo / 'tools/steam_original_recovery_session_selftest.py'),
        '--baseline', str(repo / 'tools/fixtures/steam_original_recovery_session_before.h.in'),
        '--output', str(out / 'session-validation')], capture_output=True)
    (out / 'session-validation.stdout').write_bytes(session_run.stdout)
    (out / 'session-validation.stderr').write_bytes(session_run.stderr)
    session_path = out / 'session-validation/report.json'
    session_result = json.loads(session_path.read_text(encoding='utf-8')) if session_path.exists() else {}
    session_passed = session_run.returncode == 0 and session_result.get('passed') is True
    drift = [name for name, data in sources.items() if (repo / name).read_bytes() != data]
    passed = positive.get('returnCode') == 0 and caller['passed'] and not drift and valve_passed and ready_passed and session_passed and all(row['detected'] for row in controls)
    result = {'status': 'PASS' if passed else 'FAIL', 'positive': positive, 'negativeControls': controls,
              'trackingCallerWiring': caller, 'sourceDrift': drift, 'valveUnusedRuleConsumer': valve_result, 'identityReadyConsumer': ready_result, 'sessionValidationConsumer': session_result,
              'nativeFilesEdited': False, 'liveSteamOrSleep': False, 'globalSuiteRun': False,
              'fieldAcceptance': False, 'fieldSampleArchiveSha256': sample['archiveSha256']}
    (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result, indent=2))
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
