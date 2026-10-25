"""Bounded offline consumer of exact production observation/hint/wait source.
Writes only --out, compiles with pinned MSVC/SDK, and runs its own event/thread
fixture. Never opens Steam/YMCC/game processes or invokes system sleep.
"""
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import os
import re
import subprocess
import sys
import time


def sha(data):
    return hashlib.sha256(data).hexdigest()


def mask(text):
    return re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\x27(?:\\.|[^\x27\\])*\x27',
                  lambda m: ''.join('\n' if c == '\n' else ' ' for c in m[0]), text, flags=re.S)


def block(text, pattern, semicolon=False):
    clean = mask(text)
    matches = list(re.finditer(pattern, clean))
    if len(matches) != 1:
        raise RuntimeError(f'ambiguous/missing extraction: {pattern}: {len(matches)}')
    start = matches[0].start()
    opening = clean.index('{', matches[0].start())
    depth, end = 1, opening + 1
    while depth and end < len(clean):
        depth += (clean[end] == '{') - (clean[end] == '}')
        end += 1
    if depth:
        raise RuntimeError('unbalanced production source')
    if semicolon:
        while clean[end].isspace():
            end += 1
        if clean[end] != ';':
            raise RuntimeError('missing definition semicolon')
        end += 1
    return text[start:end], start, end


def fn(text, name):
    return block(text, r'\b(?:static\s+)?(?:bool|void|DWORD)\s+' + re.escape(name) + r'\s*\([^;{}]*\)\s*(?:const\s*)?\{')


CPP = r'''
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <climits>
#include <cstdint>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "steam_original_recovery_policy.h"
#include "steam_original_recovery_wake_watch.h"
#include "steam_original_recovery_dual_route.h"
#define ymcc cadenceLegacyYmcc
#include "steam_original_recovery_wake_watch_legacy20.h"
#undef ymcc
#include "fixture_json.hpp"
using U64=std::uint64_t;
int failed=0,checks=0;
void check(bool good,const char* name){++checks;if(!good)++failed;std::cout<<"CHECK\t"<<(good?"PASS":"FAIL")<<"\t"<<name<<"\n";}
struct Counters{int disk=0,registry=0,fullValve=0,fullSession=0,cachedSession=0,enqueue=0,admit=0,consume=0,mutate=0,refund=0,signals=0,fullScope=0,recoveryScope=0,proofReads=0,legacyProofReads=0;};
Counters counters;std::vector<HANDLE> signaled;
BOOL trackedSetEvent(HANDLE h){++counters.signals;signaled.push_back(h);return ::SetEvent(h);}
using Identity=ymcc::steamOriginal::Identity;using Scope=ymcc::steamOriginal::Scope;
using Trigger=ymcc::steamOriginal::Trigger;
struct NativeDetectedGame{DWORD pid=0;ULONGLONG processCreated=0;std::wstring path;};
struct NativeGameValveSnapshot{NativeDetectedGame game;U64 rulesEpoch=0,generation=0;};
struct Session{DWORD pid=0;ULONGLONG created=0;std::wstring root;DWORD account=0;};
struct Observer{Session value;Session sessionSnapshot(){++counters.cachedSession;return value;}}g_steamSettingsObserver;
std::mutex g_gameValveMx;NativeGameValveSnapshot g_gameValveTarget;
std::atomic<U64> g_sleepRecoveryAutoConfigEpoch{0},g_gameRulesEpoch{0};
std::atomic<bool> g_exitRequested{false},g_sleepRecoveryAutoEnabled{true};
enum class PowerLifecycle{Ready,Suspending,Resuming};std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
bool nativeValveReadCurrentTarget(NativeDetectedGame*,unsigned long long*){++counters.fullValve;return false;}
namespace SleepSteamRecovery{Session session(){++counters.fullSession;return {};}}
void diskRules(){++counters.disk;}void registryRead(){++counters.registry;}
namespace SteamOriginalRecovery{
struct Pending{Trigger kind;unsigned long long generation=0,sequence=0;};
struct TracedAdmission{
 ymcc::steamOriginal::Admission real;
 auto admit(const Scope& s,ymcc::steamOriginal::Event e,bool enabled){++counters.admit;return real.admit(s,e,enabled);}
 auto preview(const Scope& s,ymcc::steamOriginal::Event e,bool enabled)const{return real.preview(s,e,enabled);}
};
struct Control{
 std::mutex mx;bool stop=false,startupPending=true,bindingQualified=true;
 std::atomic<bool> active{false},ownsRunning{false},enabled{true};
 unsigned long long latestPower=0,bindingSerial=0,observationSequence=0,sequence=0;
 std::deque<Pending> pending;std::shared_ptr<void> observationEvent;TracedAdmission admission;
};
std::shared_ptr<Control> activeControl;
static std::shared_ptr<Control> current(){return activeControl;}
void enqueue(const std::shared_ptr<Control>&,Trigger,unsigned long long=0){++counters.enqueue;}
@@GUARD@@
// Injectable authoritative providers; runtime lambda bodies below are extracted verbatim.
std::function<bool()> authoritativeScope;
ymcc::steamOriginal::WakeProof currentProof;
ymcc::steamOriginal::LegacyWakeProof currentLegacyProof;U64 proofNow=0,proofTick=1000;
bool sameScopeNow(const std::shared_ptr<Control>&,const Scope&,unsigned long long,bool recovery,unsigned long long,unsigned long long){
 ++counters.fullScope;if(recovery)++counters.recoveryScope;return authoritativeScope();
}
ymcc::steamOriginal::WakeProof wakeProof(unsigned long long generation,unsigned long long epoch){
 ++counters.proofReads;return currentProof.valid(generation,epoch,proofNow)?currentProof:ymcc::steamOriginal::WakeProof{};
}
// Explicit power-owner port; the real production proof validates every read.
ymcc::steamOriginal::LegacyWakeProof legacyWakeProof(unsigned long long generation,unsigned long long epoch){
 ++counters.legacyProofReads;return currentLegacyProof.valid(generation,epoch,proofNow,proofTick)?currentLegacyProof:ymcc::steamOriginal::LegacyWakeProof{};
}
}
#define SetEvent trackedSetEvent
@@HINT@@
#undef SetEvent
constexpr std::uint32_t kInitialExpectedAccount=12345678;
Scope baselineScope(){return {{11,2,100,L"c:\\owned\\ymcc.exe"},{22,2,200,L"c:\\owned\\game.exe"},{33,2,300,L"c:\\owned\\steam\\steam.exe"},44};}
std::shared_ptr<SteamOriginalRecovery::Control> reset(){
 auto c=std::make_shared<SteamOriginalRecovery::Control>();c->bindingSerial=55;c->latestPower=66;
 HANDLE h=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!h)throw std::runtime_error("create-hint-event");
 c->observationEvent={h,[](void* p){CloseHandle(p);}};
 SteamOriginalRecovery::activeControl=c;
 g_gameValveTarget={{22,200,L"c:\\owned\\game.exe"},44,1};
 g_steamSettingsObserver.value={33,300,L"c:\\owned\\steam",kInitialExpectedAccount};
 g_sleepRecoveryAutoConfigEpoch=77;g_gameRulesEpoch=44;g_exitRequested=false;
 g_sleepRecoveryAutoEnabled=true;g_powerLifecycle=PowerLifecycle::Ready;
 SteamOriginalRecovery::authoritativeScope=[] {return true;};
 auto& proof=SteamOriginalRecovery::currentProof;proof={};proof.generation=proof.observedGeneration=proof.requestedGeneration=proof.admissionObservedGeneration=66;
 proof.epoch=proof.observedEpoch=77;proof.eventId=507;proof.eventFileTime=proof.admissionFileTime=1000000000;
 proof.boundaryFileTime=proof.observedBoundaryFileTime=proof.eventFileTime-10000;
 proof.admitted=proof.confirmed=proof.fresh=proof.requested=proof.sourceMatched=true;SteamOriginalRecovery::proofNow=proof.eventFileTime+10000;
 auto& legacyProof=SteamOriginalRecovery::currentLegacyProof;legacyProof={};
 legacyProof.generation=66;legacyProof.epoch=77;legacyProof.eventFileTime=proof.eventFileTime;
 legacyProof.boundaryFileTime=proof.boundaryFileTime;legacyProof.boundaryGeneration=65;legacyProof.armedTick=1000;
 legacyProof.admitted=legacyProof.sourceMatched=legacyProof.operationAllowed=true;SteamOriginalRecovery::proofTick=1001;
 counters={};signaled.clear();return c;
}
std::function<bool()> observationPort(const std::shared_ptr<SteamOriginalRecovery::Control>& c){
 using namespace SteamOriginalRecovery;
 const auto scope=baselineScope();const Pending pending{Trigger::Wake,66,2};
 const unsigned long long configEpoch=77,bindingSerial=55;
 const bool legacy=false; // Preserve the original 256 formal assertions on the strict route.
 (void)legacy;
 @@ROUTE_PROOFS@@
 (void)powerEventFileTime;
 const struct {U64 account64;} privateIdentity{(1ULL<<32)|kInitialExpectedAccount};
 @@SCOPE_NOW@@
 (void)scopeNow; // The pre-fix production observation lambda does not capture this provider.
 @@OBSERVATION_GUARD@@
 return observationGuard;
}
bool guard(const std::shared_ptr<SteamOriginalRecovery::Control>& c){return SteamOriginalRecovery::sameObservationScopeNow(c,baselineScope(),66,77,55,kInitialExpectedAccount);}
void guardTests(){
 auto c=reset();check(guard(c),"cached-baseline-accepts");
 struct Case{const char* name;std::function<void(std::shared_ptr<SteamOriginalRecovery::Control>)> change;};
 const std::vector<Case> cases={
 {"valve-pid",[](auto){++g_gameValveTarget.game.pid;}},
 {"valve-birth",[](auto){++g_gameValveTarget.game.processCreated;}},
 {"valve-path",[](auto){g_gameValveTarget.game.path+=L".other";}},
 {"valve-epoch",[](auto){++g_gameValveTarget.rulesEpoch;}},
 {"empty-target",[](auto){g_gameValveTarget.game={};}},
 {"unpublished-target",[](auto){g_gameValveTarget.generation=0;}},
 {"global-rules-epoch",[](auto){++g_gameRulesEpoch;}},
 {"config-epoch",[](auto){++g_sleepRecoveryAutoConfigEpoch;}},
 {"config-disabled",[](auto){g_sleepRecoveryAutoEnabled=false;}},
 {"stop",[](auto p){p->stop=true;}},
 {"not-ready-suspending",[](auto){g_powerLifecycle=PowerLifecycle::Suspending;}},
 {"not-ready-resuming",[](auto){g_powerLifecycle=PowerLifecycle::Resuming;}},
 {"exit-requested",[](auto){g_exitRequested=true;}},
 {"steam-session-pid",[](auto){++g_steamSettingsObserver.value.pid;}},
 {"steam-session-birth",[](auto){++g_steamSettingsObserver.value.created;}},
 {"steam-session-root",[](auto){g_steamSettingsObserver.value.root+=L".other";}},
 {"steam-session-empty",[](auto){g_steamSettingsObserver.value={};}},
 {"steam-session-account-only-must-reject",[](auto){++g_steamSettingsObserver.value.account;}},
 {"steam-session-account-zero-must-reject",[](auto){g_steamSettingsObserver.value.account=0;}},
 {"bindingSerial",[](auto p){++p->bindingSerial;}},
 {"binding-unqualified",[](auto p){p->bindingQualified=false;}},
 {"new-power-generation",[](auto p){++p->latestPower;}}
 };
 for(const auto& test:cases){c=reset();test.change(c);check(!guard(c),test.name);check(counters.fullValve==0&&counters.fullSession==0&&counters.disk==0&&counters.registry==0,"rejection-zero-full-or-disk-reads");}
 c=reset();check(!SteamOriginalRecovery::sameObservationScopeNow(c,baselineScope(),66,77,55,0),"zero-expected-account-rejected");
 c=reset();g_steamSettingsObserver.value.account=0;check(!SteamOriginalRecovery::sameObservationScopeNow(c,baselineScope(),66,77,55,0),"both-cached-and-expected-account-zero-rejected");
 c=reset();g_gameValveTarget.game.path=L"C:\\OWNED\\GAME.EXE";g_steamSettingsObserver.value.root=L"C:\\OWNED\\STEAM";
 check(guard(c),"case-only-path-preserves-scope");
 c=reset();for(int i=0;i<100000;++i)if(!guard(c))throw std::runtime_error("cached-repeat");
 check(counters.cachedSession==100000&&counters.fullValve==0&&counters.fullSession==0&&counters.disk==0&&counters.registry==0,"100000-cached-guards-zero-full-or-disk-reads");
 // Publication generation is a nonzero availability marker, not a scope fence.
 c=reset();++g_gameValveTarget.generation;check(guard(c),"nonzero-publication-generation-change-allowed");
}
void hintTests(){
 auto c=reset();SteamOriginalRecovery::activeControl.reset();steamOriginalRecoveryObserveHint();check(counters.signals==0,"hint-no-control-no-signal");
 SteamOriginalRecovery::activeControl=c;steamOriginalRecoveryObserveHint();check(counters.signals==0&&c->observationSequence==0,"hint-inactive-no-signal");
 c->active=true;c->stop=true;steamOriginalRecoveryObserveHint();check(counters.signals==0&&c->observationSequence==0,"hint-stopped-no-signal");
 c->stop=false;const auto s=baselineScope();using E=ymcc::steamOriginal::Event;
 check(c->admission.admit(s,E{Trigger::Startup,0,1},true).accepted,"seed-startup-consumed");
 check(c->admission.admit(s,E{Trigger::Wake,66,2},true).accepted,"seed-wake-consumed");
 c->pending.push_back({Trigger::TargetChanged,0,3});c->sequence=9;c->ownsRunning=true;
 counters={};for(int i=0;i<10000;++i)steamOriginalRecoveryObserveHint();
 check(counters.signals==10000&&signaled.size()==10000&&std::all_of(signaled.begin(),signaled.end(),[&](HANDLE h){return h==c->observationEvent.get();}),"hint-only-current-active-event");
 check(c->observationSequence==10000&&c->sequence==9&&c->pending.size()==1&&c->pending.front().sequence==3&&c->latestPower==66&&c->bindingSerial==55&&c->bindingQualified&&c->ownsRunning&&c->enabled&&c->startupPending,"hint-only-observation-sequence-changes");
 check(counters.enqueue==0&&counters.admit==0&&counters.mutate==0&&counters.refund==0&&counters.fullValve==0&&counters.fullSession==0&&counters.cachedSession==0&&counters.disk==0&&counters.registry==0,"hint-no-enqueue-admit-mutate-refund-or-reads");
 check(!c->admission.preview(s,E{Trigger::Startup,0,1},true).accepted&&!c->admission.preview(s,E{Trigger::Wake,66,2},true).accepted,"hint-cannot-refund-consumed-production-ledger");
 check(c->admission.preview(s,E{Trigger::Wake,67,4},true).accepted,"hint-does-not-preconsume-next-event");
 auto old=c;c=reset();c->active=true;steamOriginalRecoveryObserveHint();check(counters.signals==1&&signaled.back()==c->observationEvent.get()&&old->observationSequence==10000,"hint-switches-to-current-control");
 c->observationSequence=ULLONG_MAX;steamOriginalRecoveryObserveHint();check(c->observationSequence==ULLONG_MAX,"hint-sequence-saturates");
 c->observationEvent.reset();const auto prior=counters.signals;steamOriginalRecoveryObserveHint();check(counters.signals==prior,"hint-missing-event-no-signal");
}
namespace EngineFixture{
using Current=std::function<bool()>;
thread_local Current gCurrent,gObservationCurrent;thread_local bool gCleanupMode=false;thread_local Current gOriginalSteam;
@@NEED@@
@@EXECUTION_PROBE@@
@@CURRENT@@
@@REQUIRE@@
@@OBSERVATION@@
@@CLEANUP@@
@@MUTATION@@
struct H{HANDLE h=nullptr;};struct Role{H handle;};struct Scope{std::array<Role,4> roles;};
struct Probe{Scope& scope;
@@OWNED@@
};
void mutationTests(){
 check(gExecutionProbe==nullptr,"execution-probe-inactive-before-observation-guard-checks");
 int full=0,cached=0;gCurrent=[&]{++full;return false;};gObservationCurrent=[&]{++cached;return true;};gCleanupMode=false;
 bool rejected=false;try{requireTargetMutation();}catch(...){rejected=true;}
 check(rejected&&full==1&&cached==0,"actual-requireTargetMutation-rejects-full-current-even-cached-true");
 gCurrent=[&]{++full;return true;};requireTargetMutation();check(full==2&&cached==0,"actual-requireTargetMutation-calls-full-current");
 gCleanupMode=true;gOriginalSteam=[] {return false;};rejected=false;try{requireTargetMutation();}catch(...){rejected=true;}check(rejected,"owned-cleanup-rejects-invalid-original");
 gCleanupMode=false;gCurrent={};gObservationCurrent={};gOriginalSteam={};
 check(gExecutionProbe==nullptr,"execution-probe-inactive-after-observation-guard-checks");
}
}
using EngineFixture::need;
U64 nowFileTime(){FILETIME t;GetSystemTimeAsFileTime(&t);ULARGE_INTEGER v;v.LowPart=t.dwLowDateTime;v.HighPart=t.dwHighDateTime;return v.QuadPart;}
U64 cpuTicks(){FILETIME a,b,k,u;if(!GetThreadTimes(GetCurrentThread(),&a,&b,&k,&u))throw std::runtime_error("thread-times");ULARGE_INTEGER x,y;x.LowPart=k.dwLowDateTime;x.HighPart=k.dwHighDateTime;y.LowPart=u.dwLowDateTime;y.HighPart=u.dwHighDateTime;return x.QuadPart+y.QuadPart;}
struct EventOwner{HANDLE h;explicit EventOwner(bool manual=true):h(CreateEventW(nullptr,manual,FALSE,nullptr)){if(!h)throw std::runtime_error("create-own-event");}~EventOwner(){CloseHandle(h);}EventOwner(const EventOwner&)=delete;};
void observationPortTests(){
 using namespace SteamOriginalRecovery;using J=nlohmann::json;using E=ymcc::steamOriginal::Event;
 EventOwner p0,p1,p2,p3;EngineFixture::Scope scope{};
 scope.roles[0].handle.h=p0.h;scope.roles[1].handle.h=p1.h;scope.roles[2].handle.h=p2.h;scope.roles[3].handle.h=p3.h;
 EngineFixture::Probe probe{scope};
 struct Case{const char* name;std::function<void(std::shared_ptr<Control>)> change;bool expected;int full;bool proofFirst=false;};
 const std::vector<Case> cases={
  {"cached-success-no-full-read",[](auto){},true,0},
  {"cached-account0-full-current-continues",[](auto){g_steamSettingsObserver.value.account=0;},true,1},
  {"cached-stale-pid-full-current-continues",[](auto){++g_steamSettingsObserver.value.pid;},true,1},
  {"cached-missing-observer-full-current-continues",[](auto){g_steamSettingsObserver.value={};},true,1},
  {"cached-account-changed-full-current-continues",[](auto){++g_steamSettingsObserver.value.account;},true,1},
  {"full-scope-revoked-no-continuation",[](auto){g_steamSettingsObserver.value.account=0;authoritativeScope=[] {return false;};},false,1},
  {"game-exit-no-continuation",[](auto){g_gameValveTarget.game={};authoritativeScope=[] {return false;};},false,1},
  {"record-invalid-no-continuation",[](auto){g_steamSettingsObserver.value.account=0;authoritativeScope=[] {return false;};},false,1},
  {"new-generation-no-continuation",[](auto c){++c->latestPower;authoritativeScope=[] {return false;};},false,1},
  {"config-off-no-continuation",[](auto){g_sleepRecoveryAutoEnabled=false;authoritativeScope=[] {return false;};},false,1},
  {"stop-no-continuation",[](auto c){c->stop=true;authoritativeScope=[] {return false;};},false,1},
  {"exit-request-no-continuation",[](auto){g_exitRequested=true;authoritativeScope=[] {return false;};},false,1},
  {"proof-revoked-cached-valid-no-continuation",[](auto){currentProof.confirmed=false;},false,0,true},
  {"proof-revoked-cache-miss-no-continuation",[](auto){g_steamSettingsObserver.value.account=0;currentProof.confirmed=false;},false,0,true},
  {"proof-replaced-no-continuation",[](auto){currentProof.eventId=107;},false,0,true},
  {"proof-59999ms-cached-current-continues",[](auto){proofNow=currentProof.eventFileTime+59999ULL*10000;},true,0},
  {"proof-60000ms-no-continuation",[](auto){proofNow=currentProof.eventFileTime+60000ULL*10000;},false,0,true},
  {"proof-revoked-during-full-fallback-no-continuation",[](auto){g_steamSettingsObserver.value.account=0;authoritativeScope=[] {currentProof.confirmed=false;return true;};},false,1}
 };
 for(const auto& item:cases){
  auto c=reset();const auto s=baselineScope();
  check(c->admission.admit(s,E{Trigger::Startup,0,1},true).accepted&&c->admission.admit(s,E{Trigger::Wake,66,2},true).accepted,"observation-seed-consumed-production-ledger");
  auto port=observationPort(c);item.change(c);
  if(item.full)check(!guard(c),"cached-guard-independently-rejects-invalid-cache-or-scope");
  counters={};EngineFixture::gObservationCurrent=port;
  const bool continued=probe.owned();check(continued==item.expected,item.name);
  check(counters.fullScope==item.full&&counters.recoveryScope==0,"observation-only-full-scope-read-fallback-count");
  if(item.proofFirst)check(counters.cachedSession==0&&counters.fullScope==0,"immutable-wake-proof-checked-before-cache-and-fallback");
  check(counters.mutate==0&&counters.admit==0&&counters.consume==0&&counters.refund==0&&counters.enqueue==0,"fallback-no-mutate-admit-consume-refund-or-enqueue");
  check(!c->admission.preview(s,E{Trigger::Startup,0,1},true).accepted&&!c->admission.preview(s,E{Trigger::Wake,66,2},true).accepted&&c->admission.preview(s,E{Trigger::Wake,67,3},true).accepted,"fallback-preserves-consumed-and-next-event-ledger");
  std::cout<<"OBSERVATION\t"<<J{{"name",item.name},{"continued",continued},{"expected",item.expected},{"fullScopeReads",counters.fullScope},{"expectedFullScopeReads",item.full},{"cachedReads",counters.cachedSession},{"proofReads",counters.proofReads},{"mutation",counters.mutate},{"admit",counters.admit},{"consume",counters.consume},{"refund",counters.refund}}.dump()<<"\n";
  if(item.expected&&item.full){
   const auto fullBefore=counters.fullScope,cachedBefore=counters.cachedSession;int mutationChecks=0;bool denied=false;
   EngineFixture::gCurrent=[&]{++mutationChecks;return false;};EngineFixture::gCleanupMode=false;
   try{EngineFixture::requireTargetMutation();++counters.mutate;}catch(...){denied=true;}
   check(denied&&mutationChecks==1&&counters.mutate==0&&counters.fullScope==fullBefore&&counters.cachedSession==cachedBefore,"fallback-observation-cannot-authorize-target-mutation");
  }
 }
 EngineFixture::gCurrent={};EngineFixture::gObservationCurrent={};
}
void waitCase(const char* name,int mode){
 EventOwner p0,p1,p2,p3,hint(false),ready,done;
 EngineFixture::Scope scope{};scope.roles[0].handle.h=p0.h;scope.roles[1].handle.h=p1.h;scope.roles[2].handle.h=p2.h;scope.roles[3].handle.h=p3.h;
 auto control=reset();control->active=true;control->observationEvent={hint.h,[](void*){}};
 if(mode==5)g_steamSettingsObserver.value.account=0;
 if(mode==6)++g_steamSettingsObserver.value.pid;
 SteamOriginalRecovery::authoritativeScope=[control]{std::lock_guard<std::mutex> lock(control->mx);return !control->stop&&!g_exitRequested.load();};
 const auto observationGuard=observationPort(control);
 HANDLE gWakeSignal=hint.h;std::atomic<bool> cancelled{false};std::atomic<U64> waits{0};
 U64 hintSignals=0,processSignals=0,reads=0,cpu=0,wall=0,initialDelay=0,firstHintFileTime=0,lastHintFileTime=0;bool workerOk=true;
 // Modes: 0 process-like, 1 hint, 2 cancel+hint, 3 all-signaled priority, 4 exit+hint.
 if(mode==3){SetEvent(p0.h);SetEvent(p2.h);SetEvent(hint.h);}
 auto worker=std::thread([&]{
  try{
   EngineFixture::gObservationCurrent=[&]{return !cancelled.load()&&observationGuard();};EngineFixture::Probe probe{scope};
   ymcc::steamOriginal::WakeWatch watch;const auto now=nowFileTime();watch.eventFileTime=now-3000ULL*10000;watch.lastActivityFileTime=watch.lastProbeFileTime=now;watch.nextProbeFileTime=now+10000ULL*10000;
   initialDelay=watch.delayMs(now);const auto began=GetTickCount64(),cpuStart=cpuTicks();SetEvent(ready.h);
@@WAIT_PREFIX@@
       ++reads;break; // Fixture tail: no production snapshot, remote read, or mutation.
      }
   cpu=cpuTicks()-cpuStart;wall=GetTickCount64()-began;
  }catch(...){workerOk=false;}
  SetEvent(done.h);
 });
 bool quiet=true;DWORD finished=WAIT_FAILED;U64 latency=0;
 if(WaitForSingleObject(ready.h,2000)!=WAIT_OBJECT_0){cancelled=true;SetEvent(hint.h);worker.join();check(false,"worker-ready");return;}
 if(mode!=3){quiet=WaitForSingleObject(done.h,1250)==WAIT_TIMEOUT&&waits.load()==1;
  if(mode==2){std::lock_guard<std::mutex> lock(control->mx);control->stop=true;}if(mode==4)g_exitRequested=true;
  const auto sent=GetTickCount64();if(mode==0)SetEvent(p2.h);else if(mode==2)SetEvent(hint.h);else steamOriginalRecoveryObserveHint();finished=WaitForSingleObject(done.h,1500);latency=GetTickCount64()-sent;
 }else finished=WaitForSingleObject(done.h,1500);
 if(finished!=WAIT_OBJECT_0){cancelled=true;SetEvent(hint.h);SetEvent(p0.h);}
 worker.join();
 check(workerOk&&finished==WAIT_OBJECT_0,name);
 check(initialDelay==10000,"actual-watch-initial-wait-10-seconds");
 if(mode!=3)check(quiet,"quiet-1250ms-no-self-wake");
 if(mode!=3)check(latency<1000,"signal-releases-10-second-wait-under-1-second");
 check(cpu<1000000ULL,"worker-cpu-under-100ms");
 if(mode==0)check(processSignals==1&&hintSignals==0&&reads==0,"process-exit-priority-over-hint-and-read");
 if(mode==3){check(waits.load()==0&&processSignals==0&&hintSignals==0&&reads==0,"preexisting-exit-blocks-wait-hint-and-read");HANDLE ordered[5]={p0.h,p1.h,p2.h,p3.h,hint.h};check(WaitForMultipleObjects(5,ordered,FALSE,10000)==WAIT_OBJECT_0,"win32-simultaneous-process-priority-by-handle-order");}
 if(mode==1||mode==5||mode==6)check(hintSignals==1&&reads==1&&processSignals==0,"hint-reaches-read-reservation");
 if(mode==5||mode==6)check(counters.fullScope>0&&counters.recoveryScope==0&&counters.mutate==0&&counters.admit==0&&counters.consume==0&&counters.refund==0,"stale-cache-wait-fallback-read-only-no-budget-change");
 if(mode==2||mode==4)check(hintSignals==1&&reads==0&&processSignals==0,"cancel-or-exit-blocks-read-after-hint");
 std::cout<<"METRIC\t"<<name<<"\twallMs="<<wall<<"\tcpu100ns="<<cpu<<"\tsignalLatencyMs="<<latency<<"\twaits="<<waits.load()<<"\treads="<<reads<<"\tprocessSignals="<<processSignals<<"\thintSignals="<<hintSignals<<"\n";
}

void livenessBatchBenchmark(){
 EventOwner p0,p1,p2,p3;EngineFixture::Scope scope{};scope.roles[0].handle.h=p0.h;scope.roles[1].handle.h=p1.h;scope.roles[2].handle.h=p2.h;scope.roles[3].handle.h=p3.h;
 EngineFixture::gObservationCurrent=[] {return true;};EngineFixture::Probe probe{scope};
 const auto singles=[&]{if(!EngineFixture::observationCurrent())return false;for(const auto& r:scope.roles)if(WaitForSingleObject(r.handle.h,0)!=WAIT_TIMEOUT)return false;return true;};
 std::vector<double> oldMs,newMs;LARGE_INTEGER freq{};QueryPerformanceFrequency(&freq);unsigned accepted=0;
 for(int round=0;round<7;++round){
  LARGE_INTEGER a{},b{};QueryPerformanceCounter(&a);for(unsigned i=0;i<50000;++i)accepted+=singles();QueryPerformanceCounter(&b);oldMs.push_back((b.QuadPart-a.QuadPart)*1000.0/freq.QuadPart);
  QueryPerformanceCounter(&a);for(unsigned i=0;i<50000;++i)accepted+=probe.owned();QueryPerformanceCounter(&b);newMs.push_back((b.QuadPart-a.QuadPart)*1000.0/freq.QuadPart);
 }
 check(accepted==700000,"liveness-benchmark-both-accept-all-original-unsignaled-handles");
 for(const auto h:{p0.h,p1.h,p2.h,p3.h}){SetEvent(h);check(!singles()&&!probe.owned(),"batched-and-single-guards-both-reject-each-signaled-role");ResetEvent(h);}
 const auto saved=scope.roles[3].handle.h;scope.roles[3].handle.h=nullptr;check(!singles()&&!probe.owned(),"batched-and-single-guards-both-reject-invalid-handle");scope.roles[3].handle.h=saved;
 EngineFixture::gObservationCurrent=[] {return false;};check(!probe.owned(),"batch-guard-rejects-revoked-cached-authority");EngineFixture::gObservationCurrent={};
 std::sort(oldMs.begin(),oldMs.end());std::sort(newMs.begin(),newMs.end());
 std::cout<<"METRIC\tliveness-guard-batch\titerations=50000\tsingleWaitCalls=4\tbatchWaitCalls=1\tsingleMedianMs="<<oldMs[3]<<"\tbatchMedianMs="<<newMs[3]<<"\tfieldCpuClaim=false\n";
}


namespace CadenceFixture {
using J=nlohmann::json;using Scope=EngineFixture::Scope;
constexpr U64 physicalEvent=134370000000000000ULL;U64 clockMs=0,lossMs=0,lastReadMs=0,qualifications=0;
U64 nowFileTime(){return physicalEvent+clockMs*10000ULL;}
ULONGLONG GetTickCount64(){return clockMs;}
DWORD WaitForMultipleObjects(DWORD count,const HANDLE*,BOOL waitAll,DWORD timeout){
 need(count==4&&!waitAll&&timeout>0&&timeout<=10000,"virtual-no-hint-wait-shape");clockMs+=timeout;return WAIT_TIMEOUT;
}
struct Sample {DWORD flags=8196;};
struct Probe:EngineFixture::Probe {
 explicit Probe(Scope& value):EngineFixture::Probe{value}{}
 DWORD flags(const Sample&)const{lastReadMs=clockMs;return clockMs>=lossMs?4u:8196u;}
};
Sample snapshot(const Scope&,U64 account){need(account==12345678,"fixture-account");++qualifications;return {clockMs>=lossMs?4u:8196u};}
void matchReference(const J&,const Scope&,const Sample& sample,U64 account){need(account==12345678&&sample.flags==4,"fixture-loss-qualification");}
J nativeState(const Scope&,const Sample& sample){return {{"flags",sample.flags},{"running",sample.flags==8196}};}
void publish(const J& row){need(row.at("event")=="late-running-loss-observed","fixture-loss-event");}
struct Result {bool started=false,detected=false;U64 start=0,loss=0,detectedAt=0,endedAt=0,lastRead=0,hints=0;unsigned probes=0,fast=0;};
template<class Watch> Result run(U64 start,U64 loss){
 EventOwner p0,p1,p2,p3;Scope scope{};scope.roles[0].handle.h=p0.h;scope.roles[1].handle.h=p1.h;scope.roles[2].handle.h=p2.h;scope.roles[3].handle.h=p3.h;
 const auto previous=EngineFixture::gObservationCurrent;EngineFixture::gObservationCurrent=[] {return true;};
 struct Restore {EngineFixture::Current previous;~Restore(){EngineFixture::gObservationCurrent=std::move(previous);}} restore{previous};
 clockMs=start;lossMs=loss;lastReadMs=qualifications=0;Probe probe{scope};Watch watch;watch.eventFileTime=physicalEvent;
 Result result;result.start=start;result.loss=loss;
 if(!watch.beginObservation(nowFileTime(),probe.owned())){result.endedAt=clockMs;return result;}
 result.started=true;const auto started=GetTickCount64();const U64 account=12345678;Sample sample;J summary=J::object(),reference=J::object();
 HANDLE gWakeSignal=nullptr;U64 waits=0,hintSignals=0,processSignals=0,lastHealthyProbeFileTime=0,firstHintFileTime=0,lastHintFileTime=0;
@@WATCH_LOOP@@
 result.detected=summary.value("lateLossObserved",false);result.detectedAt=clockMs;result.endedAt=clockMs;
 result.lastRead=lastReadMs;result.hints=hintSignals;result.probes=watch.probes;result.fast=watch.fastProbes;
 need(processSignals==0&&hintSignals==0&&qualifications==(result.detected?1u:0u),"full-loop-no-hint-and-one-qualification");
 need(watch.eventFileTime==physicalEvent&&watch.probes<=320&&watch.fastProbes<=240&&clockMs<=55000&&lastReadMs<55000,
     "full-loop-physical-end-and-budgets-unchanged");
 return result;
}
J row(const Result& result){return {{"started",result.started},{"detected",result.detected},{"startMs",result.start},
 {"lossMs",result.loss},{"detectedAtMs",result.detected?J(result.detectedAt):J(nullptr)},
 {"delayMs",result.detected?J(result.detectedAt-result.loss):J(nullptr)},{"endedAtMs",result.endedAt},
 {"lastReadMs",result.lastRead},{"hints",result.hints},{"probes",result.probes},{"fastProbes",result.fast}};}
void tests(){
 struct Case {const char* name;U64 start,loss;};
 const Case cases[]={{"no-hint-loss-21s",0,21000},{"no-hint-loss-35s",0,35000},{"no-hint-loss-52s",0,52000},
 {"no-hint-loss-54s",0,54000},{"start-4.9s-loss-21s",4900,21000},{"start-4.9s-loss-35s",4900,35000},
 {"start-4.9s-loss-52s",4900,52000},{"start-20s-loss-52s",20000,52000},{"start-48s-loss-52s",48000,52000},
 {"start-52s-loss-54s",52000,54000}};
 unsigned priorViolations=0;
 for(const auto& item:cases){const auto current=run<ymcc::steamOriginal::WakeWatch>(item.start,item.loss);
   const auto legacy=run<cadenceLegacyYmcc::steamOriginal::WakeWatch>(item.start,item.loss);
   const bool legacyFails=!legacy.detected||legacy.detectedAt-legacy.loss>1000;if(legacyFails)++priorViolations;
   check(current.started&&current.detected&&current.detectedAt-current.loss<=1000,"actual-full-loop-no-hint-late-loss-detection-at-most-1s");
   check(current.hints==0&&current.detectedAt<55000,"late-loss-no-artificial-hint-or-extended-window");
   if(item.start==0)check(legacyFails,"old20-policy-exceeds-1s-or-misses-late-loss");
   std::cout<<"CADENCE\t"<<J{{"name",item.name},{"current",row(current)},{"legacy20",row(legacy)},
       {"legacy20ViolatesNewLatencyContract",legacyFails}}.dump()<<"\n";
 }
 check(priorViolations>=6,"old20-policy-negative-has-late-and-delayed-start-regressions");
 const auto boundary=run<ymcc::steamOriginal::WakeWatch>(0,55000);
 check(boundary.started&&!boundary.detected&&boundary.endedAt==55000&&boundary.lastRead<55000,"full-loop-55s-endpoint-exclusive-no-extra-probe");
 const auto expired=run<ymcc::steamOriginal::WakeWatch>(55000,55000);
 check(!expired.started&&expired.probes==0,"full-loop-expired-observation-cannot-start");
 const auto tail=run<ymcc::steamOriginal::WakeWatch>(54999,54999);
 check(tail.started&&!tail.detected&&tail.endedAt==55000&&tail.probes==0,"last-millisecond-start-does-not-extend-physical-window");
}
void negativeContract(){
 for(const U64 loss:{21000ULL,35000ULL,52000ULL}){const auto legacy=run<cadenceLegacyYmcc::steamOriginal::WakeWatch>(0,loss);
  std::cout<<"LEGACY20\t"<<row(legacy).dump()<<"\n";
  check(legacy.detected&&legacy.detectedAt-legacy.loss<=1000,"old20-no-hint-late-loss-must-meet-1s-contract");
 }
}
}

int main(int argc,char** argv){if(argc==2&&std::string(argv[1])=="--legacy20-latency-contract"){CadenceFixture::negativeContract();std::cout<<"TOTAL\tchecks="<<checks<<"\tfailed="<<failed<<"\n";return failed?1:0;}try{guardTests();observationPortTests();hintTests();EngineFixture::mutationTests();livenessBatchBenchmark();CadenceFixture::tests();waitCase("process-like-signal",0);waitCase("hint-signal",1);waitCase("cancel-plus-hint",2);waitCase("simultaneous-process-hint",3);waitCase("exit-plus-hint",4);waitCase("account0-full-current-plus-hint",5);waitCase("stale-pid-full-current-plus-hint",6);}catch(const std::exception& e){++failed;std::cout<<"EXCEPTION\t"<<e.what()<<"\n";}std::cout<<"TOTAL\tchecks="<<checks<<"\tfailed="<<failed<<"\n";return failed?1:0;}
'''


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--repo', type=Path, default=Path(r'G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC'))
    ap.add_argument('--runtime-source', type=Path, help='Read a preserved runtime snapshot for the old-source negative; product files stay untouched.')
    ap.add_argument('--out', type=Path, default=Path(r'D:\Codex\2026-10-21\steam-ai-steam-running-steam-api\work\g3e-event-detection-20261021\observation'))
    args = ap.parse_args()
    started = time.monotonic()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    (out / 'tmp').mkdir(exist_ok=True)
    report = {'started': datetime.datetime.now().astimezone().isoformat(), 'status': 'unknown', 'repo': str(args.repo.resolve()), 'extractions': [], 'boundaries': {}, 'limitations': [
        'Control, kernel globals, valve and Steam cached snapshots are simplified stubs; production function bodies are exact extracted text.',
        'Only own-process events/threads run. Four production process handles are modeled by manual-reset events. No Steam/YMCC/game or suspend operation.',
        'Wait loop prefix through reserveProbe and WakeWatch are exact production source; tail is fixture instrumentation, not full engine execution.',
        'Exit and stop use the extracted production observationGuard and scopeNow lambdas in the observationCurrent callback. Stop notification uses a ported SetEvent; live lifecycle wiring is not executed.',
        'Disk/registry/full-read counters plus allowlist/source checks are bounded direct-call evidence, not OS-wide I/O tracing.',
        'Cached guard does not query Windows session ID; Steam session coverage means cached PID/birth/root. OS session-ID-only changes are unknown.',
        'Nonzero valve publication-generation changes are allowed by production; zero generation rejects. Rules/config epochs are scope fences.',
        'Steam Session includes account in production. An account-only-change negative is enforced and reported as a real guard failure if accepted; end-to-end mutation/account validation is unknown.',
        'sameScopeNow is an injected authoritative result; wakeProof and the explicit legacyWakeProof port validate injected real production proof types; authoritative game/record validity is an input, not a live inspection. Actual scopeNow and observationGuard bodies execute unchanged.',
        'No end-to-end native snapshot/matchReference/consume or real remote mutation is executed; these boundaries are structural checks.',
        'Late-loss scheduling uses the exact complete engine while-loop with virtual time/timeout and stub flags/snapshot/reference; old20 changes only the frozen header constant. No field detection latency is claimed.'
    ]}
    def write_report():
        report['elapsedSeconds'] = round(time.monotonic()-started, 3)
        (out/'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    try:
        files = {
            'runtime': args.runtime_source or args.repo/'native/steam_original_recovery_runtime.h',
            'engine': args.repo/'native/steam_original_recovery_engine.cpp',
            'policy': args.repo/'native/steam_original_recovery_policy.h',
            'watch': args.repo/'native/steam_original_recovery_wake_watch.h',
            'dual_route': args.repo/'native/steam_original_recovery_dual_route.h',
            'observer': args.repo/'native/steam_session_observer.h',
            'test_cmd': args.repo/'native/steam_original_recovery_engine/Test.cmd',
            'json': args.repo/'deps/json/json.hpp',
        }
        frozen = {k: p.read_bytes() for k,p in files.items()}
        texts = {k: v.decode('utf-8-sig') for k,v in frozen.items()}
        report['sources'] = {k:{'path':str(files[k]),'sha256':sha(v),'bytes':len(v)} for k,v in frozen.items()}
        for k,v in frozen.items():
            (out/('source_'+files[k].name)).write_bytes(v)
        for k in ('policy','watch','dual_route'):
            (out/files[k].name).write_bytes(frozen[k])
        (out/'fixture_json.hpp').write_bytes(frozen['json'])
        legacy_watch = frozen['watch'].replace(b'settleWindowMs=budgetMs', b'settleWindowMs=20000')
        if frozen['watch'].count(b'settleWindowMs=budgetMs') != 1 or legacy_watch == frozen['watch']:
            raise RuntimeError('full-window policy/legacy counterfactual anchor changed')
        (out/'steam_original_recovery_wake_watch_legacy20.h').write_bytes(legacy_watch)
        report['legacy20Policy'] = {'onlyChange': 'settleWindowMs=budgetMs -> settleWindowMs=20000', 'sha256': sha(legacy_watch)}
        def keep(name,key,part):
            body,start,end = part
            record={'name':name,'source':key,'startLine':texts[key].count('\n',0,start)+1,'endLine':texts[key].count('\n',0,end-1)+1,'utf8Sha256':sha(body.encode('utf-8')),'file':name+'.inc'}
            report['extractions'].append(record)
            (out/(name+'.inc')).write_bytes(body.encode('utf-8'))
            return body
        replacements={}
        replacements['GUARD']=keep('sameObservationScopeNow','runtime',fn(texts['runtime'],'sameObservationScopeNow'))
        proof_pattern=r'\bconst\s+auto\s+proof=[^;]+;\s*(?:const\s+bool\s+powerOwnerWitness=[^;]+;\s*const\s+auto\s+legacyProof=[^;]+;\s*)?const\s+ULONGLONG\s+powerEventFileTime=[^;]+;'
        proof_sites=list(re.finditer(proof_pattern,mask(texts['runtime'])))
        if len(proof_sites)!=1:raise RuntimeError('ambiguous/missing exact runtime proof selection')
        proof_site=proof_sites[0]
        replacements['ROUTE_PROOFS']=keep('route_proof_selection','runtime',(texts['runtime'][proof_site.start():proof_site.end()],proof_site.start(),proof_site.end()))
        replacements['SCOPE_NOW']=keep('scopeNow','runtime',block(texts['runtime'],r'\bconst\s+auto\s+scopeNow\s*=\s*\[',True))
        replacements['OBSERVATION_GUARD']=keep('observationGuard','runtime',block(texts['runtime'],r'\bconst\s+auto\s+observationGuard\s*=\s*\[',True))
        replacements['HINT']=keep('steamOriginalRecoveryObserveHint','runtime',fn(texts['runtime'],'steamOriginalRecoveryObserveHint'))
        replacements['EXECUTION_PROBE']=keep('ExecutionProbe','engine',block(texts['engine'],r'\bstruct\s+ExecutionProbe\s*\{',True))
        probe_decl=re.search(r'thread_local\s+ExecutionProbe\*\s+gExecutionProbe=nullptr;',texts['engine'])
        if probe_decl is None: raise RuntimeError('execution probe declaration missing')
        replacements['EXECUTION_PROBE']+='\n'+keep('gExecutionProbe','engine',(probe_decl[0],probe_decl.start(),probe_decl.end()))
        for token,name in [('NEED','need'),('CURRENT','current'),('REQUIRE','requireCurrent'),('OBSERVATION','observationCurrent'),('CLEANUP','requireCleanupOwnership'),('MUTATION','requireTargetMutation')]:
            replacements[token]=keep(name,'engine',fn(texts['engine'],name))
        probe,pstart,pend=block(texts['engine'],r'\bstruct\s+WakeProbeReader\s*\{',True)
        owned,ostart,oend=fn(probe,'owned')
        replacements['OWNED']=keep('WakeProbeReader_owned','engine',(owned,pstart+ostart,pstart+oend))
        loop,lstart,lend=block(texts['engine'],r'\bwhile\s*\(watch\.observes\(nowFileTime\(\),probe\.owned\(\)\)\)\s*\{')
        stop = re.search(r'if\s*\(!watch\.reserveProbe\(nowFileTime\(\),probe\.owned\(\)\)\)break;',mask(loop))
        if not stop:
            raise RuntimeError('reserveProbe boundary missing')
        prefix=loop[:stop.end()]
        replacements['WAIT_PREFIX']=keep('event_wait_prefix','engine',(prefix,lstart,lstart+len(prefix)))
        replacements['WATCH_LOOP']=keep('full_event_observation_loop','engine',(loop,lstart,lend))
        guard=mask(replacements['GUARD']);hint=mask(replacements['HINT']);ec=mask(texts['engine']);rc=mask(texts['runtime'])
        flags=fn(probe,'flags')[0]
        report['boundaries']['cached_guard_allowed_calls_only']=sorted(set(re.findall(r'\b(\w+)\s*\(',guard)) - {'sameObservationScopeNow','if','load','c_str','sessionSnapshot','_wcsicmp','lock'}) == []
        report['boundaries']['cached_guard_no_full_valve_registry_disk']=not re.search(r'\b(nativeValveReadCurrentTarget|readJson|saveJson|Reg\w*|SleepSteamRecovery|sameScopeNow|identity)\b',guard)
        observation=mask(replacements['OBSERVATION_GUARD']);scope_now=mask(replacements['SCOPE_NOW'])
        report['boundaries']['runtime_captures_original_account_low32']=bool(re.search(r'expectedAccount=static_cast<std::uint32_t>\(privateIdentity\.account64\)',observation)) and 'sameObservationScopeNow(c,scope,pending.generation,configEpoch,bindingSerial,expectedAccount)' in observation
        report['boundaries']['observation_captures_full_scope_provider']=bool(re.search(r'proof,(?:legacyProof,powerOwnerWitness,)?scopeNow,expectedAccount=',observation))
        report['boundaries']['observation_cache_miss_falls_back_read_only']=bool(re.search(r'return\s+sameObservationScopeNow\([^;]+\)\s*\|\|\s*scopeNow\(false\);',observation))
        strict_recheck=r'wakeProof\(pending.generation,configEpoch\)!=proof'
        dual_recheck=r'\(powerOwnerWitness\?legacyWakeProof\(pending.generation,configEpoch\)!=legacyProof:\s*'+strict_recheck+r'\)'
        report['boundaries']['observation_immutable_proof_checked_first']=bool(re.search(r'if\(pending.kind==Trigger::Wake&&(?:'+strict_recheck+'|'+dual_recheck+r')\)return false;\s*return sameObservationScopeNow',observation))
        report['boundaries']['observation_allowed_calls_only']=not (set(re.findall(r'\b(\w+)\s*\(',observation)) - {'if','wakeProof','legacyWakeProof','sameObservationScopeNow','scopeNow'})
        report['boundaries']['observation_and_full_scope_no_budget_or_mutation_calls']=not re.search(r'\b(admit|consume|refund|enqueue|mutate|recover|preview)\s*\(',observation+'\n'+scope_now)
        report['boundaries']['full_scope_provider_rechecks_immutable_proof']='sameScopeNow(c,scope,pending.generation,recovery,configEpoch,bindingSerial)' in scope_now and 'wakeProof(pending.generation,configEpoch)==proof' in scope_now
        if 'powerOwnerWitness' in scope_now:
            report['boundaries']['full_scope_provider_rechecks_legacy_proof']='legacyWakeProof(pending.generation,configEpoch)==legacyProof' in scope_now
            report['boundaries']['proof_selection_exact_route_and_fallback']='pending.kind==Trigger::Wake&&!legacy?wakeProof(pending.generation,configEpoch)' in mask(replacements['ROUTE_PROOFS']) and 'powerOwnerWitness=legacy||!proof.eventFileTime' in mask(replacements['ROUTE_PROOFS'])
        report['proofPorts']={'strict':'controlled WakeProof validated by real production WakeProof.valid', 'legacy':'explicit controlled LegacyWakeProof validated by real production LegacyWakeProof.valid', 'originalFormalAssertionsRoute':'semi-static strict kernel; shared S3 routes execute separately in exact dual-route self-test'}
        report['boundaries']['hint_allowed_calls_only']=sorted(set(re.findall(r'\b(\w+)\s*\(',hint)) - {'steamOriginalRecoveryObserveHint','current','if','load','SetEvent','get','lock'}) == []
        report['boundaries']['hint_no_admission_queue_budget_calls']=not re.search(r'\b(enqueue|admit|preview|consume|refund|invalidate|reserveProbe|noteHint)\s*\(',hint)
        report['boundaries']['probe_liveness_one_kernel_wait']='WaitForMultipleObjects(4,handles,FALSE,0)' in mask(owned) and 'WaitForSingleObject' not in mask(owned)
        report['boundaries']['probe_one_constructor_OpenProcess']=len(re.findall(r'\bOpenProcess\s*\(',mask(probe)))==1
        report['boundaries']['probe_owned_flags_no_OpenProcess_no_fullcurrent']=not re.search(r'\b(OpenProcess|current|requireCurrent|snapshot|matchReference)\s*\(',mask(owned+'\n'+flags))
        report['boundaries']['requireTargetMutation_uses_full_current']=bool(re.search(r'else\s+requireCurrent\s*\(',mask(replacements['MUTATION'])))
        report['boundaries']['runtime_full_guard_keeps_full_valve_and_session']='nativeValveReadCurrentTarget' in fn(texts['runtime'],'sameScopeNow')[0] and 'SleepSteamRecovery::session' in fn(texts['runtime'],'sameScopeNow')[0]
        report['boundaries']['runtime_stillCurrent_uses_scopeNow']=bool(re.search(r'const\s+auto\s+stillCurrent\s*=\s*\[scopeNow[^\]]*\]\s*\{return\s+scopeNow\(mayRecover\);\}',rc))
        report['boundaries']['engine_receives_separate_full_and_observation_guards']='steamOriginalRecoveryEngineHandle(request,stillCurrent,emit,observationGuard,' in rc
        report['boundaries']['loss_requalifies_snapshot_matchReference']=bool(re.search(r'if\(probe\.flags\(sample\)==8196\)\{lastHealthyProbeFileTime=nowFileTime\(\);continue;\}[\s\S]*?sample=snapshot\(scope,account\);matchReference\(reference,scope,sample,account\);',mask(loop)))
        consume_pos=ec.index('consume(event,scope,*data)')
        loss_pos=ec.index('if(sample.flags==4){',lend)
        qualification=ec[loss_pos:consume_pos]
        report['boundaries']['consume_after_loss_match_and_full_current']='matchReference(reference,scope,sample,account)' in qualification and 'requireCurrent()' in qualification
        report['boundaries']['sessionSnapshot_is_cached_locked_copy']=bool(re.search(r'Session\s+sessionSnapshot\(\)\s+const\s*\{\s*std::lock_guard\s+lock\(sessionMutex\);\s*return\s+cachedSession;',mask(texts['observer'])))
        report['boundaries']['process_signals_before_hint_before_read']=prefix.index('++processSignals;break;')<prefix.index('++hintSignals;')<prefix.index('if(!watch.observes')<prefix.index('reserveProbe')
        fixture=CPP
        for k,v in replacements.items():
            fixture=fixture.replace('@@'+k+'@@',v)
        if '@@' in fixture:
            raise RuntimeError('unfilled fixture marker')
        cpp=out/'observation_fixture.cpp';cpp.write_bytes(fixture.encode('utf-8'))
        vc=Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207')
        sdk=Path(r'C:\Program Files (x86)\Windows Kits\10')
        env=os.environ.copy();env['INCLUDE']=';'.join(map(str,[vc/'include',sdk/'Include/10.0.26100.0/um',sdk/'Include/10.0.26100.0/shared',sdk/'Include/10.0.26100.0/ucrt']))
        env['LIB']=';'.join(map(str,[vc/'lib/x64',sdk/'Lib/10.0.26100.0/um/x64',sdk/'Lib/10.0.26100.0/ucrt/x64']))
        env['PATH']=str(vc/'bin/Hostx64/x64')+os.pathsep+env.get('PATH','');env['TMP']=env['TEMP']=str(out/'tmp')
        exe=out/'observation_fixture.exe'
        command=[str(vc/'bin/Hostx64/x64/cl.exe'),'/nologo','/std:c++20','/utf-8','/EHsc','/MT','/O2','/W4','/WX','/guard:cf',str(cpp),'/Fo'+str(out/'observation_fixture.obj'),'/Fe'+str(exe),'/link','/INCREMENTAL:NO','/DYNAMICBASE','/NXCOMPAT','/GUARD:CF']
        report['compiler']={'command':command,'sdk':'10.0.26100.0','msvc':'14.44.35207'}
        compiled=subprocess.run(command,cwd=out,env=env,capture_output=True,timeout=60)
        (out/'compile.log').write_bytes(compiled.stdout+compiled.stderr);report['compileReturnCode']=compiled.returncode
        if compiled.returncode:
            report['status']='fail';write_report();print(compiled.stdout.decode('utf-8',errors='replace'));return 1
        ran=subprocess.run([str(exe)],cwd=out,env=env,capture_output=True,timeout=30)
        (out/'run.log').write_bytes(ran.stdout+ran.stderr);report['runReturnCode']=ran.returncode
        output=ran.stdout.decode('utf-8',errors='replace');report['metrics']=[line for line in output.splitlines() if line.startswith('METRIC\t')]
        report['failedChecks']=[line for line in output.splitlines() if line.startswith('CHECK\tFAIL') or line.startswith('EXCEPTION')]
        report['checkCount']=sum(line.startswith('CHECK\t') for line in output.splitlines())
        report['boundaries']['original_256_formal_assertions_retained']=report['checkCount']==256
        report['observationCases'] = [json.loads(line.split('\t', 1)[1]) for line in output.splitlines() if line.startswith('OBSERVATION\t')]
        report['boundaries']['observation_port_cases_continue_or_stop_as_required'] = len(report['observationCases']) == 18 and all(case['continued'] == case['expected'] and case['fullScopeReads'] == case['expectedFullScopeReads'] and not any(case[key] for key in ('mutation','admit','consume','refund')) for case in report['observationCases'])
        report['lateLossCases'] = [json.loads(line.split('\t', 1)[1]) for line in output.splitlines() if line.startswith('CADENCE\t')]
        report['boundaries']['late_loss_full_loop_all_within_1s'] = len(report['lateLossCases']) == 10 and all(
            case['current']['detected'] and case['current']['delayMs'] <= 1000 and case['current']['hints'] == 0 and case['current']['detectedAtMs'] < 55000
            for case in report['lateLossCases'])
        negative = subprocess.run([str(exe), '--legacy20-latency-contract'], cwd=out, env=env, capture_output=True, timeout=10)
        (out/'legacy20-negative.log').write_bytes(negative.stdout+negative.stderr)
        negative_output = negative.stdout.decode('utf-8', errors='replace')
        negative_failures = [line for line in negative_output.splitlines() if line.startswith('CHECK\tFAIL')]
        report['legacy20Negative'] = {'returnCode': negative.returncode, 'failedChecks': negative_failures,
            'cases': [json.loads(line.split('\t', 1)[1]) for line in negative_output.splitlines() if line.startswith('LEGACY20\t')],
            'expectedFailureCaught': negative.returncode != 0 and len(negative_failures) == 3}
        report['boundaries']['legacy20_latency_contract_negative_rejected'] = report['legacy20Negative']['expectedFailureCaught']
        report['failureInterpretation'] = {
            'priorWaitFailure': 'Fixture expectation error: pre-signaled process-like event makes real probe.owned() reject before wait; corrected with explicit zero-wait assertion and a separate real Win32 simultaneous-wait assertion.',
            'accountOnly': 'Production guard accepts cached Session.account-only changes. Enforced as failure under the requested all-Steam-session-changes-reject requirement. Scope.steam has no account field; no mutation bypass is claimed.' if any('steam-session-account-only-must-reject' in x for x in report['failedChecks']) else 'Fixed initial expectedAccount is passed independently of cached snapshot. Baseline accepts; changed/zero cached account and zero expected account reject.'
        }
        report['fixedExpectedAccount'] = 12345678
        previous = out/'pre-account-fix'
        if previous.is_dir():
            report['preAccountFix']={'path':str(previous), 'reportSha256':sha((previous/'report.json').read_bytes()), 'status':json.loads((previous/'report.json').read_text(encoding='utf-8'))['status']}
        report['unknown'] = ['Real Steam/game process handles and runtime lifecycle delivery', 'OS-wide disk/registry tracing', 'OS-session-ID-only changes', 'Concurrent active-control replacement races', 'Full engine snapshot/matchReference/consume and account validation before real mutation']
        report['coverage'] = {'fullLoopCadence': 'Exact full engine observation while-loop; synthetic FILETIME/timeout and controlled flags; real WakeWatch policy and owned unsignaled liveness handles; not a physical wake or real remote snapshot', 'executed': ['Exact runtime observationGuard and scopeNow lambdas through engine observationCurrent/Probe.owned with injectable full-scope and proof providers', 'Exact cached guard and hint bodies', 'Real Admission and WakeWatch headers', 'Exact mutation guard helpers', 'Exact WakeProbeReader::owned', 'Exact WaitForMultipleObjects loop prefix through reserveProbe with borrowed HANDLE and instrumented fixture tail'], 'sourceOnly': list(report['boundaries'])}
        report['sourcesUnchanged']={k:sha(files[k].read_bytes())==sha(v) for k,v in frozen.items()}
        report['selftest']={'path':str(Path(__file__).resolve()),'sha256':sha(Path(__file__).read_bytes())}
        report['artifacts']={p.name:sha(p.read_bytes()) for p in (cpp,exe,out/'run.log',out/'compile.log')}
        report['status']='pass' if ran.returncode==0 and all(report['boundaries'].values()) and all(report['sourcesUnchanged'].values()) else 'fail'
        write_report();print(output);print(json.dumps({'status':report['status'],'boundaries':report['boundaries'],'report':str(out/'report.json')},indent=2));return int(report['status']!='pass')
    except Exception as exc:
        report['error']=repr(exc);write_report();print(json.dumps(report,ensure_ascii=False,indent=2));return 2


if __name__=='__main__':
    sys.exit(main())






