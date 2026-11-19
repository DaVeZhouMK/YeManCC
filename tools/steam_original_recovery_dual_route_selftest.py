"""Offline execution of exact production dual-route dispatch and wake guards.
Only this driver/fixture and --out are written. No live Steam/game/sleep calls.
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
sys.dont_write_bytecode = True


def sha(data):
    return hashlib.sha256(data).hexdigest()


def masked(text):
    pattern = r'//[^\n]*|/\*.*?\*/|(?:u8|u|U|L)?R"(?P<tag>[^ ()\\\t\r\n]{0,16})\(.*?\)(?P=tag)"|"(?:\\.|[^"\\])*"|\x27(?:\\.|[^\x27\\])*\x27'
    return re.sub(pattern, lambda m: ''.join('\n' if c == '\n' else ' ' for c in m[0]), text, flags=re.S)


def extract(text, pattern, semicolon=False):
    clean = masked(text)
    hits = list(re.finditer(pattern, clean, re.M))
    if len(hits) != 1:
        raise RuntimeError(f'ambiguous/missing production definition: {pattern}: {len(hits)}')
    start = hits[0].start()
    opening = clean.index('{', start)
    depth, end = 1, opening + 1
    while depth and end < len(clean):
        depth += (clean[end] == '{') - (clean[end] == '}')
        end += 1
    if depth:
        raise RuntimeError('unbalanced production definition')
    if semicolon:
        while end < len(clean) and clean[end].isspace():
            end += 1
        if clean[end:end+1] != ';':
            raise RuntimeError('missing definition semicolon')
        end += 1
    return text[start:end], start, end



def adapt_native_read_scope_fixture(cpp, interface, directed):
    def replace_once(old, new):
        nonlocal cpp
        if cpp.count(old) != 1:
            raise RuntimeError('nonunique ReadScope fixture anchor: ' + old[:100])
        cpp = cpp.replace(old, new)
    replace_once('#include "steam_native_record_identity.h"',
                 '#include "steam_native_record_identity.h"\n#include "sleep_power_state.h"')
    replace_once('#include "sleep_power_state.h"', '#include "sleep_power_state.h"\n#include "steam_original_recovery_trace.h"')
    replace_once('static void sleepPowerPostResumeCommit(',
                 'using HWND=std::uintptr_t;using LPARAM=std::intptr_t;\n'
                 'std::atomic<unsigned long long> g_powerResumeCommitRedelivery{0};\n'
                 'void sleepPowerArmRedelivery(HWND){} // deterministic delivery-timer port\n'
                 'static void sleepPowerPostResumeCommit(')
    ports = r'''
using HANDLE=void*;
namespace ymcc::steamnative { @@READ_SCOPE_INTERFACE@@ }
// Deterministic interface consumer port. The real factory is verified separately.
struct FixtureNativeReadScope final:ymcc::steamnative::ReadScope {
    ymcc::steamsession::IdentitySnapshot receipt;
    ymcc::sleeppower::SharedPowerV1 captured;
    bool identityValid=true,powerValid=true,revoked=false;
    bool identityEvent=false,powerEvent=false;
    explicit FixtureNativeReadScope(const SleepSteamRecovery::Session& client){
        receipt.session={client.root,client.pid,client.account,client.created};
        receipt.presence=1;receipt.windowsSessionKnown=true;receipt.windowsSession=sceneSession;
        receipt.processEpoch=1;receipt.accountEpoch=1;
        captured.generation=fixtureGeneration;captured.publicationRevision=1;
        captured.flags=ymcc::sleeppower::kKnown|ymcc::sleeppower::kHardwareAllowed;
    }
    bool current()noexcept override {
        if(!identityValid||!powerValid)cancel();
        return !revoked;
    }
    const ymcc::steamsession::IdentitySnapshot& expected()const noexcept override{return receipt;}
    const ymcc::sleeppower::SharedPowerV1& power()const noexcept override{return captured;}
    HANDLE identityCancellationEvent()const noexcept override{return const_cast<bool*>(&identityEvent);}
    HANDLE powerCancellationEvent()const noexcept override{return const_cast<bool*>(&powerEvent);}
    void cancel()noexcept override{revoked=true;identityEvent=powerEvent=true;}
};
bool fixtureScopeAvailable=true,fixtureScopeAdmitted=true,retainScopeCallbacks=false;
unsigned fixtureScopeAcquisitions=0;
std::weak_ptr<FixtureNativeReadScope> firstNativeReadScope,lastNativeReadScope;
std::function<bool()> retainedScopeCurrent,retainedScopeObservation;
static std::shared_ptr<ymcc::steamnative::ReadScope> nativeSteamNativeReadScope(const SleepSteamRecovery::Session& client){
    ++fixtureScopeAcquisitions;
    if(!fixtureScopeAvailable)return {};
    auto scope=std::make_shared<FixtureNativeReadScope>(client);
    scope->powerValid=fixtureScopeAdmitted;if(firstNativeReadScope.expired())firstNativeReadScope=scope;lastNativeReadScope=scope;return scope;
}
'''.replace('@@READ_SCOPE_INTERFACE@@', interface)
    replace_once('namespace SteamGameLink {', ports + '\nnamespace SteamGameLink {')
    replace_once('    ++engineCalls;requests.push_back(request);if(onEngineEnter)onEngineEnter();',
                 '    ++engineCalls;requests.push_back(request);\n'
                 '    if(retainScopeCallbacks){retainedScopeCurrent=current;retainedScopeObservation=observation;}\n'
                 '    if(onEngineEnter)onEngineEnter();')
    replace_once('std::shared_ptr<Control> reset(bool kernel){',
                 'std::shared_ptr<Control> reset(bool kernel){\n'
                 '    retainedScopeCurrent={};retainedScopeObservation={};firstNativeReadScope.reset();lastNativeReadScope.reset();\n'
                 '    fixtureScopeAvailable=fixtureScopeAdmitted=true;retainScopeCallbacks=false;fixtureScopeAcquisitions=0;')
    if directed:
        # Only the explicit branch changes the original default case selection.
        replace_once('int main(){', 'int main(){\nif(true){\n' + r'''
    test("native-read-scope-acquisition-precedes-qualification",[]{
        for(const auto r:{Route::LegacyDynamic,Route::SemiStatic}){
            auto c=reset(true);fixtureScopeAvailable=false;route(c,r);
            check(fixtureScopeAcquisitions==1&&engineCalls==0&&privateReads==0,"missing-scope-entered-native-reader");
        }
    });
    test("native-read-scope-revoked-acquisition-refuses",[]{
        for(const auto r:{Route::LegacyDynamic,Route::SemiStatic}){
            auto c=reset(true);fixtureScopeAdmitted=false;route(c,r);
            check(engineCalls==0&&privateReads==0,"revoked-scope-entered-qualification");
        }
    });
    test("native-read-scope-identity-revokes-cached-and-fallback",[]{
        for(const auto r:{Route::LegacyDynamic,Route::SemiStatic})for(const bool cached:{false,true}){
            auto c=reset(true);
            onEngineEnter=[cached]{
                if(!cached)SleepSteamRecovery::live.account=0;
                auto held=lastNativeReadScope.lock();held->identityValid=false;
            };
            route(c,r);
            check(engineCalls==1&&!engineGuards[0]["current"].get<bool>()&&!engineGuards[0]["observation"].get<bool>(),"identity-revocation-bypassed");
            check(!c->ownsRunning,"revoked-identity-retained-running");
        }
    });
    test("native-read-scope-power-revokes-cached-and-fallback",[]{
        for(const auto r:{Route::LegacyDynamic,Route::SemiStatic})for(const bool cached:{false,true}){
            auto c=reset(true);
            onEngineEnter=[cached]{
                if(!cached)SleepSteamRecovery::live.account=0;
                auto held=lastNativeReadScope.lock();held->powerValid=false;
            };
            route(c,r);
            check(engineCalls==1&&!engineGuards[0]["current"].get<bool>()&&!engineGuards[0]["observation"].get<bool>(),"power-revocation-bypassed");
            check(g_powerLifecycle==PowerLifecycle::Ready&&operationAllowed,"fixture-revoked-old-power-gate");
        }
    });
    test("native-read-scope-unknown-candidate-keeps-ui-qualification",[]{
        auto c=reset(true);SleepSteamRecovery::live.account=0;registryCandidate=0;registryReadable=false;privateRequireCandidate=true;
        route(c,Route::SemiStatic);
        check(privateReads==2&&engineCalls==1,"unknown-candidate-blocked-independent-ui-reader");
        check(requests[0]["account64"]==std::to_string(76561197960265728ULL+456),"ui-account-qualification-changed");
        check(engineGuards[0]["current"]==true&&engineGuards[0]["observation"]==true,"unknown-candidate-revoked-process-scope");
    });
    test("native-read-scope-dual-route-final-aba-cannot-resurrect",[]{
        auto c=reset(true);unsigned tracking=0;
        onTracking=[&]{if(++tracking==2){auto operation=firstNativeReadScope.lock();check(bool(operation),"operation-scope-not-retained");operation->identityValid=false;}};
        engineReply=[](const json&){return result("healthy");};run(c);
        check(tracking==2&&engineCalls==1,"revoked-second-route-entered-engine");
        check(!c->ownsRunning&&!c->snapshot.value("currentBindingValid",true)&&c->snapshot.value("lastResultHistorical",false),"earlier-healthy-resurrected-after-operation-aba");
    });
    test("native-read-scope-callbacks-retain-until-return",[]{
        for(const auto r:{Route::LegacyDynamic,Route::SemiStatic}){
            auto c=reset(true);retainScopeCallbacks=true;route(c,r);
            check(!lastNativeReadScope.expired(),"scope-released-before-actor-return");
            {
                auto held=lastNativeReadScope.lock();
                check(held->expected().session.pid==63648&&held->power().generation==7,"scope-capture-changed");
                held->powerValid=false;
            }
            check(!retainedScopeCurrent()&&!retainedScopeObservation(),"retained-callback-ignored-revocation");
            {auto held=lastNativeReadScope.lock();held->powerValid=true;}
            check(!retainedScopeCurrent()&&!retainedScopeObservation(),"ready-revived-old-scope");
            retainedScopeCurrent={};check(!lastNativeReadScope.expired(),"observation-lost-shared-ownership");
            retainedScopeObservation={};check(lastNativeReadScope.expired(),"scope-survived-all-callback-returns");
        }
    });
''' + '\n}else{')
        replace_once('    unsigned failed=0;for(const auto& value:cases)',
                     '    }\n    unsigned failed=0;for(const auto& value:cases)')
    return cpp


def write_exclusive(target, data):
    """Immutable prepared inputs/results: CreateNew, flush and fsync."""
    if isinstance(data, str):
        data = data.encode('utf-8')
    with open(target, 'xb') as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())


def adapt_native_read_purpose_fixture(cpp, interface, purpose, old_counterexample, directed=False):
    # Preserve the original adapter/factory verbatim for the old-source control.
    cpp = adapt_native_read_scope_fixture(cpp, interface, directed)
    # Existing Native commit dependencies are ports in this original fixture.
    # This directed scope does not execute commitPowerResume or claim post-gate
    # redelivery. The actual Native/sleep owner qualifies that separately.
    native_ports = r'''
struct FixtureNativeDispatchObservation {
    std::uint64_t observedSequence()const{return 0;}
    bool pendingSuspend()const{return false;}
} g_sleepPowerDispatch;
std::atomic<unsigned long long> g_sharedPowerSuspendSerial{0};
struct FixtureRecoveryBoundary {void revoke(){}} g_recoveryPowerBoundary;
static bool captureSharedPowerLifecycle(ymcc::sleeppower::SharedPowerV1& value)noexcept{
    value={};value.generation=fixtureGeneration;value.publicationRevision=1;
    value.phase=g_powerLifecycle==PowerLifecycle::Ready?0:g_powerLifecycle==PowerLifecycle::Suspending?1:g_powerLifecycle==PowerLifecycle::Suspended?2:3;
    value.flags=ymcc::sleeppower::kKnown;
    if(g_powerLifecycle==PowerLifecycle::Ready&&gateOpenCalls)value.flags|=ymcc::sleeppower::kHardwareAllowed;
    return ymcc::sleeppower::powerAdmitted(value);
}
'''
    marker = 'static bool commitPowerResume('
    if cpp.count(marker) != 1:
        raise RuntimeError('nonunique original Native commit dependency boundary')
    cpp = cpp.replace(marker, native_ports + marker)
    if cpp.count('static void sleepRecoveryAutoPowerReady(') != 1:
        raise RuntimeError('nonunique unused original power-ready producer')
    cpp = cpp.replace('static void sleepRecoveryAutoPowerReady(', '[[maybe_unused]] static void sleepRecoveryAutoPowerReady(')


    def change(old, new):
        nonlocal cpp
        if cpp.count(old) != 1:
            raise RuntimeError('nonunique observation port anchor: ' + old[:100])
        cpp = cpp.replace(old, new)
    change('namespace ymcc::steamnative { ' + interface + ' }',
           'namespace ymcc::steamnative { ' + purpose + '\n' + interface + ' }')
    if not old_counterexample:
        change('bool identityValid=true,powerValid=true,revoked=false;',
               'ymcc::steamnative::ReadPurpose purpose=ymcc::steamnative::ReadPurpose::PrepareMutation;\n    bool identityValid=true,powerValid=true,revoked=false;')
        change('if(!identityValid||!powerValid)cancel();',
               'if(!identityValid||(purpose==ymcc::steamnative::ReadPurpose::PrepareMutation&&!powerValid))cancel();')
        change('unsigned fixtureScopeAcquisitions=0;',
               'unsigned fixtureScopeAcquisitions=0;\nstd::vector<ymcc::steamnative::ReadPurpose> fixturePurposes;\nunsigned observationTrackingCalls=0,observationTrackingAccepted=0;')
        change('nativeSteamNativeReadScope(const SleepSteamRecovery::Session& client){',
               'nativeSteamNativeReadScope(const SleepSteamRecovery::Session& client,ymcc::steamnative::ReadPurpose purpose=ymcc::steamnative::ReadPurpose::PrepareMutation){')
        change('++fixtureScopeAcquisitions;', '++fixtureScopeAcquisitions;fixturePurposes.push_back(purpose);')
        change('scope->powerValid=fixtureScopeAdmitted;',
               'scope->purpose=purpose;scope->powerValid=fixtureScopeAdmitted;\n    if(purpose==ymcc::steamnative::ReadPurpose::ObserveOriginal)scope->captured={};\n    ')
        change('fixtureScopeAvailable=fixtureScopeAdmitted=true;retainScopeCallbacks=false;fixtureScopeAcquisitions=0;',
               'fixtureScopeAvailable=fixtureScopeAdmitted=true;retainScopeCallbacks=false;fixtureScopeAcquisitions=0;\n    fixturePurposes.clear();observationTrackingCalls=observationTrackingAccepted=0;')
        marker='static json steamOriginalRecoveryEngineHandle(const json&,'
        port=r'''
namespace SleepSteamRecovery {struct Game{DWORD pid;ULONGLONG created;std::wstring path;};}
namespace SteamGameLink {using TrackerRead=Tracking;}
// Explicit port only. Actual readLogSource/ProcessLease/Binding/PrimaryJoin is
// compiled by the producer owner, never attested by this tracking mock.
static SteamGameLink::TrackerRead nativeSteamNativeObservationTracking(
    const SleepSteamRecovery::Session& client,const SleepSteamRecovery::Game& game,const std::function<bool()>& active){
    ++observationTrackingCalls;
    const auto unknown=[](){SteamGameLink::TrackerRead value;value.complete=false;value.gameId.clear();return value;};
    if(!active||!active())return unknown();
    auto result=SteamGameLink::tracking(client,{game.pid,game.created,game.path});
    if(!active())return unknown();
    ++observationTrackingAccepted;return result;
}
'''
        change(marker, port+'\n'+marker)
    return cpp


def adapt_native_read_observation_fixture(cpp, interface, purpose, cases, old_counterexample):
    cpp = adapt_native_read_purpose_fixture(cpp, interface, purpose, old_counterexample)
    def change(old, new):
        nonlocal cpp
        if cpp.count(old) != 1:
            raise RuntimeError('nonunique observation case anchor: ' + old[:100])
        cpp = cpp.replace(old, new)
    prefix = '#define YMCC_OBSERVATION_OLD_COUNTEREXAMPLE 1\n' if old_counterexample else ''
    change('int main(){', prefix+'int main(){\nif(true){\n'+cases+'\n}else{')
    change('    unsigned failed=0;for(const auto& value:cases)',
           '    }\n    unsigned failed=0;for(const auto& value:cases)')
    return cpp



def prepare_public_wake_parent(repo,out,prepare_only):
    mapping = [{'token': 'R_Pending', 'path': 'native/steam_original_recovery_runtime.h', 'name': 'Pending', 'kind': 'struct'}, {'token': 'R_StartupFreshRequest', 'path': 'native/steam_original_recovery_runtime.h', 'name': 'StartupFreshRequest', 'kind': 'struct'}, {'token': 'R_StartupFreshPhase', 'path': 'native/steam_original_recovery_runtime.h', 'name': 'StartupFreshPhase', 'kind': 'enum class'}, {'token': 'R_StartupFreshWait', 'path': 'native/steam_original_recovery_runtime.h', 'name': 'StartupFreshWait', 'kind': 'struct'}, {'token': 'R_GroupRequestTarget', 'path': 'native/steam_original_recovery_runtime.h', 'name': 'GroupRequestTarget', 'kind': 'enum class'}, {'token': 'R_ActiveGroupRequest', 'path': 'native/steam_original_recovery_runtime.h', 'name': 'ActiveGroupRequest', 'kind': 'struct'}, {'token': 'R_Control', 'path': 'native/steam_original_recovery_runtime.h', 'name': 'Control', 'kind': 'struct'}, {'token': 'R_cancelGroupRequestLocked', 'path': 'native/steam_original_recovery_runtime.h', 'name': 'cancelGroupRequestLocked', 'kind': 'function'}, {'token': 'R_GroupRequestRegistration', 'path': 'native/steam_original_recovery_runtime.h', 'name': 'GroupRequestRegistration', 'kind': 'struct'}, {'token': 'R_identity', 'path': 'native/steam_original_recovery_runtime.h', 'name': 'identity', 'kind': 'function'}, {'token': 'R_asJson', 'path': 'native/steam_original_recovery_runtime.h', 'name': 'asJson', 'kind': 'function'}, {'token': 'R_sameScopeNow', 'path': 'native/steam_original_recovery_runtime.h', 'name': 'sameScopeNow', 'kind': 'function'}, {'token': 'R_sameObservationScopeNow', 'path': 'native/steam_original_recovery_runtime.h', 'name': 'sameObservationScopeNow', 'kind': 'function'}, {'token': 'R_accountCandidate', 'path': 'native/steam_original_recovery_runtime.h', 'name': 'accountCandidate', 'kind': 'function'}, {'token': 'R_startupFreshRouteIndex', 'path': 'native/steam_original_recovery_runtime.h', 'name': 'startupFreshRouteIndex', 'kind': 'function'}, {'token': 'R_cancelStartupFreshLocked', 'path': 'native/steam_original_recovery_runtime.h', 'name': 'cancelStartupFreshLocked', 'kind': 'function'}, {'token': 'R_RetiredStartupFresh', 'path': 'native/steam_original_recovery_runtime.h', 'name': 'RetiredStartupFresh', 'kind': 'struct'}, {'token': 'R_retireStartupFreshLocked', 'path': 'native/steam_original_recovery_runtime.h', 'name': 'retireStartupFreshLocked', 'kind': 'function'}, {'token': 'R_StartupFreshRunReturn', 'path': 'native/steam_original_recovery_runtime.h', 'name': 'StartupFreshRunReturn', 'kind': 'struct'}, {'token': 'R_beginStartupFreshRequest', 'path': 'native/steam_original_recovery_runtime.h', 'name': 'beginStartupFreshRequest', 'kind': 'function'}, {'token': 'R_retainedRouteTerminal', 'path': 'native/steam_original_recovery_runtime.h', 'name': 'retainedRouteTerminal', 'kind': 'function'}, {'token': 'R_recordRouteTerminal', 'path': 'native/steam_original_recovery_runtime.h', 'name': 'recordRouteTerminal', 'kind': 'function'}, {'token': 'R_enqueue', 'path': 'native/steam_original_recovery_runtime.h', 'name': 'enqueue', 'kind': 'function'}, {'token': 'R_runRoute', 'path': 'native/steam_original_recovery_runtime.h', 'name': 'runRoute', 'kind': 'function'}, {'token': 'R_runOne', 'path': 'native/steam_original_recovery_runtime.h', 'name': 'runOne', 'kind': 'function'}, {'token': 'R_steamOriginalRecoveryWake', 'path': 'native/steam_original_recovery_runtime.h', 'name': 'steamOriginalRecoveryWake', 'kind': 'function'}, {'token': 'R_steamOriginalRecoveryPowerReady', 'path': 'native/steam_original_recovery_runtime.h', 'name': 'steamOriginalRecoveryPowerReady', 'kind': 'function'}, {'token': 'A_State', 'path': 'native/sleep_recovery_auto.h', 'name': 'State', 'kind': 'struct'}, {'token': 'A_sharedWakeFactsLocked', 'path': 'native/sleep_recovery_auto.h', 'name': 'sharedWakeFactsLocked', 'kind': 'function'}, {'token': 'A_followsWakeBoundary', 'path': 'native/sleep_recovery_auto.h', 'name': 'followsWakeBoundary', 'kind': 'function'}, {'token': 'A_WakeOperationObservation', 'path': 'native/sleep_recovery_auto.h', 'name': 'WakeOperationObservation', 'kind': 'struct'}, {'token': 'A_completedInteractiveResumeLocked', 'path': 'native/sleep_recovery_auto.h', 'name': 'completedInteractiveResumeLocked', 'kind': 'function'}, {'token': 'A_recordInteractiveResume', 'path': 'native/sleep_recovery_auto.h', 'name': 'recordInteractiveResume', 'kind': 'function'}, {'token': 'A_recordCompletedResume', 'path': 'native/sleep_recovery_auto.h', 'name': 'recordCompletedResume', 'kind': 'function'}, {'token': 'A_proofSupersededLocked', 'path': 'native/sleep_recovery_auto.h', 'name': 'proofSupersededLocked', 'kind': 'function'}, {'token': 'A_wakeOperationAllowedLocked', 'path': 'native/sleep_recovery_auto.h', 'name': 'wakeOperationAllowedLocked', 'kind': 'function'}, {'token': 'A_wakeOperationAllows', 'path': 'native/sleep_recovery_auto.h', 'name': 'wakeOperationAllows', 'kind': 'function'}, {'token': 'A_wakeReadinessLocked', 'path': 'native/sleep_recovery_auto.h', 'name': 'wakeReadinessLocked', 'kind': 'function'}, {'token': 'A_recordKernelWake', 'path': 'native/sleep_recovery_auto.h', 'name': 'recordKernelWake', 'kind': 'function'}, {'token': 'A_arm', 'path': 'native/sleep_recovery_auto.h', 'name': 'arm', 'kind': 'function'}, {'token': 'A_captureReady', 'path': 'native/sleep_recovery_auto.h', 'name': 'captureReady', 'kind': 'function'}, {'token': 'A_powerReady', 'path': 'native/sleep_recovery_auto.h', 'name': 'powerReady', 'kind': 'function'}, {'token': 'A_tick', 'path': 'native/sleep_recovery_auto.h', 'name': 'tick', 'kind': 'function'}, {'token': 'A_originalWakeIdentity', 'path': 'native/sleep_recovery_auto.h', 'name': 'originalWakeIdentity', 'kind': 'function'}, {'token': 'A_zeroWakeActionsLocked', 'path': 'native/sleep_recovery_auto.h', 'name': 'zeroWakeActionsLocked', 'kind': 'function'}, {'token': 'A_zeroActionOriginalCurrentLocked', 'path': 'native/sleep_recovery_auto.h', 'name': 'zeroActionOriginalCurrentLocked', 'kind': 'function'}, {'token': 'P_wakeProof', 'path': 'native/steam_original_recovery_runtime.h', 'name': 'wakeProof', 'kind': 'function'}, {'token': 'P_legacyWakeProof', 'path': 'native/steam_original_recovery_runtime.h', 'name': 'legacyWakeProof', 'kind': 'function'}, {'token': 'S_originalTargetCurrent', 'path': 'native/sleep_steam_recovery.h', 'name': 'originalTargetCurrent', 'kind': 'function'}, {'token': 'V_gameIdMatches', 'path': 'native/steam_native_app_state_win32.h', 'name': 'gameIdMatches', 'kind': 'function'}]
    template_path='tools/steam_original_recovery_dual_route_fixture.cpp.in'
    required=list(dict.fromkeys([r['path']for r in mapping]+[template_path,'tools/steam_original_recovery_dual_route_selftest.py','native/sleep_wake_admission.h','tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json','tools/fixtures/recovery_startup_handoff_unsupported.h']))
    inputs={rel:(repo/rel).read_bytes()for rel in required}
    text=inputs[template_path].decode('utf-8-sig').replace('\r\n','\n')
    if text.count('// YMCC_PUBLIC_WAKE_PARENT_BEGIN')!=1 or text.count('// YMCC_PUBLIC_WAKE_PARENT_END')!=1:raise RuntimeError('public parent section missing/ambiguous')
    cpp=text.split('// YMCC_PUBLIC_WAKE_PARENT_BEGIN',1)[1].split('// YMCC_PUBLIC_WAKE_PARENT_END',1)[0].strip()+'\n'
    fragments=[]
    for row in mapping:
        source=inputs[row['path']].decode('utf-8-sig').replace('\r\n','\n');clean=masked(source);name=re.escape(row['name'])
        pattern=r'^\s*(?:static|inline)\s+[^;{}]*?\b'+name+r'\s*\((?:[^;{}]|\{\})*\)\s*(?:noexcept\s*)?\{'if row['kind']=='function'else r'\b'+row['kind']+r'\s+'+name+r'\s*(?:final\s*)?\{'
        hits=list(re.finditer(pattern,clean,re.M))
        if len(hits)!=1:raise RuntimeError('public parent definition missing/ambiguous: '+row['name'])
        start=hits[0].start()
        while clean[start].isspace():start+=1
        end=hits[0].end();depth=1
        while depth:depth+=(clean[end]=='{')-(clean[end]=='}');end+=1
        if row['kind']!='function':
            while clean[end].isspace():end+=1
            if clean[end]!=';':raise RuntimeError('public parent type semicolon missing')
            end+=1
        body=source[start:end];marker='@@PW_'+row['token']+'@@'
        if cpp.count(marker)!=1:raise RuntimeError('public parent marker missing/ambiguous: '+marker)
        cpp=cpp.replace(marker,body,1);fragments.append({**row,'sha256':sha(body.encode()),'bytes':len(body.encode()),'complete':True})
    if '@@'in cpp:raise RuntimeError('public parent marker remained')
    # Neutral private compilation attributes; the exact inverse is recorded.
    cpp=re.sub(r'\bconst auto&(\s+\w+\s*=)',r'[[maybe_unused]] const auto&\1',cpp)
    clean=masked(cpp);starts=[m.start()for m in re.finditer(r'^static\s+[^;{}]*?\b[A-Za-z_]\w*\s*\((?:[^;{}]|\{\})*\)\s*(?:noexcept\s*)?\{',clean,re.M)]
    for pos in reversed(starts):cpp=cpp[:pos]+'[[maybe_unused]] '+cpp[pos:]
    write_exclusive(out/'recovery_startup_handoff_unsupported.h',inputs['tools/fixtures/recovery_startup_handoff_unsupported.h'])
    source=out/'Fixture.cpp';write_exclusive(source,cpp)
    receipt={'schema':1,'status':'PREPARED_NOT_RUN','scope':'public-wake-parent','fragments':fragments,'functionFragments':sum(x['kind']=='function'for x in mapping),'typeFragments':sum(x['kind']!='function'for x in mapping),'inputs':[{'path':rel,'bytes':len(b),'sha256':sha(b)}for rel,b in inputs.items()],'fixtureOnlyAnnotations':'[[maybe_unused]] only; remove to reconstruct complete production definitions','runtimeParentComplete':True,'enginePublicationBoundaryPort':True,'nonWakeStartupNotAccepted':True,'processGroupIOAndReadScopePorts':True,'sceneActions':0,'physicalAcceptance':False}
    if not prepare_only:
        tools=json.loads(inputs['tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json'].decode('utf-8-sig'))['native'];env={k.upper():v for k,v in os.environ.items()};env['PATH']=';'.join([str(Path(tools['compiler']).parent),'C:/Windows/System32','C:/Windows']);env['INCLUDE']=';'.join(tools['include']);env['LIB']=';'.join(tools['lib'])
        for key in ['CL','_CL_','LINK','_LINK_']:env[key]=''
        exe=out/'Fixture.exe';built=subprocess.run([tools['compiler'],'/nologo','/std:c++20','/utf-8','/EHsc','/MT','/W4','/WX','/I'+str(repo/'native'),'/I'+str(repo/'deps/json'),'/sourceDependencies',str(out/'dependencies.json'),str(source),'/Fo'+str(out/'Fixture.obj'),'/Fe'+str(exe),'/link','/INCREMENTAL:NO','/VERBOSE:LIB','advapi32.lib'],env=env,cwd=out,capture_output=True,timeout=90);write_exclusive(out/'compile.log',built.stdout+built.stderr);receipt['compileExit']=built.returncode
        if built.returncode:write_exclusive(out/'PUBLIC-WAKE-PARENT.json',json.dumps(receipt,indent=2)+'\n');return 1
        done=subprocess.run([str(exe)],cwd=out,env=env,capture_output=True,timeout=45);write_exclusive(out/'native.stdout',done.stdout);write_exclusive(out/'native.stderr',done.stderr);receipt['nativeExit']=done.returncode;receipt['status']='PASS'if done.returncode==0 else'FAIL'
    write_exclusive(out/'PUBLIC-WAKE-PARENT.json',json.dumps(receipt,indent=2)+'\n');print(json.dumps({'status':receipt['status'],'scope':receipt['scope'],'fragments':len(mapping),'functions':receipt['functionFragments'],'types':receipt['typeFragments'],'prepareOnly':prepare_only}));return 0 if prepare_only or receipt.get('nativeExit')==0 else 1

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', '--output', type=Path, required=True)
    parser.add_argument('--prepare-only', action='store_true')
    parser.add_argument('--production-control-only', action='store_true', help='Prepare only eight explicit actual production Control/RecoveryWorker selectors')
    parser.add_argument('--command-pending-only', action='store_true', help='Generate only the complete original Startup command-pending composition')
    parser.add_argument('--command-pending-reuse', type=Path, help='Reuse only a verified prior command-pending compile directory for one explicit case')
    parser.add_argument('--command-pending-case', choices=["command-pending-ready-invalidations-no-admit","command-pending-ordinary-off-exit-no-debt-wait","command-pending-tracker-and-command-suffix-no-replay","command-pending-consumed-receipt-no-refund","command-pending-start-signal-allocation-failure-worker-call"]+['command-pending-current-identity-advisory', 'command-pending-queued-binding-advisory', 'command-pending-current-ordinary-revoke', 'command-pending-stale-account-advisory', 'command-pending-rejected-completion-advisory', 'command-pending-deferred-wake-advisory', 'command-pending-current-binding-advisory', 'command-pending-arming-command-advisory', 'command-pending-running-fresh-advisory'], help='Exact existing Startup parameter; preparation or bounded single-case execution')
    parser.add_argument('--startup-wake-only', action='store_true', help='Only incremental no-Startup-replay-after-Wake boundaries in startup-pair')
    parser.add_argument('--route-terminal-case', choices=['all','retained-after-preflight'], default='all')
    parser.add_argument('--route-terminal-only', action='store_true', help='Only current-attempt and retained per-route terminal evidence boundaries')
    parser.add_argument('--negative-controls', action='store_true')
    parser.add_argument('--scope', choices=['native-read-scope', 'native-read-observation', 'startup-pair', 'ordinary-notifications', 'public-wake-parent'], default=None,
                        help='Only the directed ReadScope consumer boundary cases; default cases stay unchanged')
    parser.add_argument('--repo', type=Path, help='Explicit immutable product/dependency overlay prepared by the owner')
    parser.add_argument('--read-purpose-header', type=Path, help='Actual public candidate header for enum extraction; required for old control')
    parser.add_argument('--counterexample-original-root', type=Path, help='Original snapshot; choose only original SuspendCapture boundary, expected FAIL')
    parser.add_argument('--notification-case', default='all')
    parser.add_argument('--runtime-source', type=Path)
    parser.add_argument('--notification-control', choices=['none','delay-urgent','refresh-deadline'], default='none')
    args = parser.parse_args()
    if args.production_control_only and (not args.command_pending_only or args.command_pending_case is not None):
        parser.error('production-control-only requires exclusive command-pending-only without old case selection')
    if args.command_pending_case is not None and not args.command_pending_only:
        parser.error('command-pending-case requires command-pending-only')
    if args.command_pending_reuse and (not args.command_pending_only or args.prepare_only or args.command_pending_case is None or args.production_control_only):
        parser.error('command-pending-reuse requires exclusive single-case command-pending execution')
    if args.command_pending_only:
        if args.scope != 'startup-pair' or args.negative_controls or args.counterexample_original_root or args.startup_wake_only or args.route_terminal_only or args.route_terminal_case != 'all' or args.notification_case != 'all' or args.runtime_source or args.notification_control != 'none':
            parser.error('command-pending-only requires exclusive --scope startup-pair --prepare-only')
        if not args.prepare_only:
            if args.production_control_only or args.command_pending_case is None:
                parser.error('command-pending execution requires one explicit --command-pending-case')
            from fixtures.recovery_command_pending import execute
            return execute(args)
        from fixtures.recovery_command_pending import prepare
        return prepare(args, extract)
    if args.scope == 'ordinary-notifications':
        if args.negative_controls or args.counterexample_original_root or (args.prepare_only and args.notification_case != 'command-ready-registration') or args.startup_wake_only or args.route_terminal_only or args.route_terminal_case != 'all':
            parser.error('ordinary-notifications is an exclusive directed scope')
        from fixtures.recovery_hint_coalesce import run
        return run(args, extract)
    if args.notification_case != 'all' or args.runtime_source or args.notification_control != 'none':
        parser.error('notification options require ordinary-notifications scope')
    if args.route_terminal_case != 'all' and not args.route_terminal_only:
        parser.error('--route-terminal-case requires --route-terminal-only')
    if args.route_terminal_only and (args.scope != 'startup-pair' or args.startup_wake_only):
        parser.error('--route-terminal-only requires only --scope startup-pair')
    if args.startup_wake_only and args.scope != 'startup-pair':
        parser.error('--startup-wake-only requires --scope startup-pair')
    if args.counterexample_original_root and (args.scope not in ['native-read-observation', 'startup-pair'] or not args.read_purpose_header):
        parser.error('old counterexample requires native-read-observation and actual --read-purpose-header')
    if args.scope in ['native-read-observation', 'startup-pair'] and args.negative_controls:
        parser.error('observation control uses --counterexample-original-root; old negative groups are not selected')
    repo = (args.repo or Path(__file__).resolve().parent.parent).resolve()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    if args.scope == 'public-wake-parent':
        return prepare_public_wake_parent(repo,out,args.prepare_only)
    files = ['native/steam_original_recovery_runtime.h', 'native/steam_original_recovery_wake_watch.h',
             'native/steam_original_recovery_policy.h', 'native/sleep_recovery_diagnostics.h',
             'native/sleep_steam_tracking.h', 'native/steam_native_record_identity.h',
             'native/steam_native_app_state_win32.h', 'deps/json/json.hpp',
             'tools/steam_original_recovery_dual_route_fixture.cpp.in',
             'tools/steam_original_recovery_dual_route_selftest.py',
             'tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json']
    files.extend(['native/sleep_recovery_auto.h', 'native/main.cpp', 'native/sleep_steam_settle.h', 'native/sleep_steam_input_context.h', 'native/steam_session_observer.h', 'native/sleep_resume_classification_native.inc'])
    files.extend(['native/steam_native_read_scope.h', 'native/sleep_power_state.h', 'native/steam_original_recovery_trace.h', 'tools/fixtures/recovery_startup_handoff_unsupported.h'])
    runtime = (repo / files[0]).read_text(encoding='utf-8-sig')
    for match in re.finditer(r'^#include "(steam_original_recovery_[^"/]*(?:route|proof)[^"/]*)"', runtime, re.M):
        files.append('native/' + match[1])
    inputs = {name: (Path(__file__).resolve() if name == 'tools/steam_original_recovery_dual_route_selftest.py' else repo / name).read_bytes() for name in dict.fromkeys(files)}
    original_pins = {}
    if args.counterexample_original_root:
        for name in ['native/steam_original_recovery_runtime.h', 'tools/steam_original_recovery_dual_route_fixture.cpp.in']:
            inputs[name] = (args.counterexample_original_root / name).read_bytes()
            original_pins[name] = {'path': str(args.counterexample_original_root / name), 'sha256': sha(inputs[name]), 'bytes': len(inputs[name])}
        original_driver = (args.counterexample_original_root / 'tools/steam_original_recovery_dual_route_selftest.py').read_text(encoding='utf-8-sig').replace('\r\n', '\n')
        import ast
        def adapter_body(source):
            tree = ast.parse(source)
            node = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'adapt_native_read_scope_fixture')
            return '\n'.join(source.splitlines()[node.lineno-1:node.end_lineno])
        if adapter_body(original_driver) != adapter_body(Path(__file__).read_text(encoding='utf-8-sig')):
            raise RuntimeError('old factory/adapter bytes changed')
        original_pins['oldAdapter'] = {'sha256': sha(adapter_body(original_driver).encode()), 'unchanged': True}
    if args.scope in ['native-read-observation', 'startup-pair']:
        case_name = ('tools/steam_original_recovery_dual_route_startup_pair_cases.inc' if args.scope == 'startup-pair' else 'tools/steam_original_recovery_dual_route_native_read_observation_cases.inc')
        if args.startup_wake_only: case_name = 'tools/steam_original_recovery_dual_route_startup_pair_wake_cases.inc'
        if args.route_terminal_only: case_name = 'tools/steam_original_recovery_dual_route_terminal_cases.inc'
        inputs[case_name] = Path(__file__).with_name(Path(case_name).name).read_bytes()
        if args.read_purpose_header:
            inputs['read-purpose-public-header.h'] = args.read_purpose_header.read_bytes()
    frozen = out / 'production'
    for name, data in inputs.items():
        target = frozen / name
        target.parent.mkdir(parents=True, exist_ok=True)
        write_exclusive(target, data)
    # Re-read only the captured snapshot for generated code; detect edits at end.
    text = inputs[files[0]].decode('utf-8-sig').replace('\r\n', '\n')
    extracts = []
    def take(name, pattern=None, semicolon=False, source=files[0]):
        content = inputs[source].decode('utf-8-sig').replace('\r\n', '\n')
        body, start, end = extract(content, pattern or r'^static\s+[^;{}]*?\b' + re.escape(name) + r'\s*\([^;{}]*\)\s*\{', semicolon)
        extracts.append({'name': name, 'source': source, 'startLine': content.count('\n', 0, start)+1,
                         'endLine': content.count('\n', 0, end)+1, 'sha256': sha(body.encode()), 'bytes': len(body.encode())})
        write_exclusive(out / (name + '.inc'), body)
        return body
    parts = {'PENDING': take('Pending', r'\bstruct\s+Pending\s*\{', True),
             'CONTROL': take('Control', r'\bstruct\s+Control\s*\{', True)}
    helpers = ['identity', 'asJson', 'wakeProof', 'sameScopeNow', 'sameObservationScopeNow', 'accountCandidate', 'enqueue']
    if re.search(r'^static\s+[^;{}]*?\blegacyWakeProof\s*\(', masked(text), re.M):
        helpers.insert(3, 'legacyWakeProof')
    for diagnostic in ['retainedRouteTerminal','recordRouteTerminal']:
        if re.search(r'^static\s+[^;{}]*?\b'+diagnostic+r'\s*\(', masked(text), re.M):
            helpers.append(diagnostic)
    parts['HELPERS'] = '\n'.join(take(name) for name in helpers)
    parts['SHARED_IDENTITY'] = '\n'.join(take(name, r'\bstruct\s+'+name+r'\s*\{', True,
        source='native/steam_session_observer.h') for name in ['Session', 'IdentitySnapshot'])
    parts['RUN_ONE'] = take('runRoute') + '\n' + take('runOne')
    event_entrypoints = ['steamOriginalRecoverySuspend', 'steamOriginalRecoveryWake', 'steamOriginalRecoveryPowerReady', 'steamOriginalRecoveryTargetChanged', 'steamOriginalRecoveryBindingReady', 'steamOriginalRecoveryRequestStop']
    if args.scope == 'startup-pair':
        event_entrypoints.append('steamOriginalRecoverySteamChanged')
    parts['EVENT_ENTRYPOINTS'] = '\n'.join(take(name) for name in event_entrypoints)
    auto_producers = ['sleepRecoveryAutoArm', 'sleepRecoveryAutoCaptureSuspend', 'sleepRecoveryAutoCloseSuspendEpisode', 'sleepRecoveryAutoPowerReady']
    parts['AUTO_PRODUCERS'] = '\n'.join(take(name, source='native/sleep_recovery_auto.h') for name in auto_producers)
    parts['COMPLETION_PROOF'] = '\n'.join(take(name, r'\bstruct\s+'+name+r'\s*\{', True, source='native/sleep_recovery_auto.h') for name in ['InteractiveResume', 'CompletedResume', 'ReadyCommit']) + '\nInteractiveResume interactiveResume; CompletedResume completedResume; ReadyCommit readyCommit;\n' + '\n'.join(take(name, source='native/sleep_recovery_auto.h') for name in ['followsWakeBoundary', 'completedInteractiveResumeLocked'])
    if 'struct WakeOperationObservation {' in inputs['native/sleep_recovery_auto.h'].decode('utf-8-sig'):
        parts['COMPLETION_PROOF'] = take('WakeOperationObservation', r'\bstruct\s+WakeOperationObservation\s*\{', True, source='native/sleep_recovery_auto.h')+'\n'+parts['COMPLETION_PROOF']
    parts['SHARED_WAKE_CLASSIFICATION'] = take('SgResumeClassificationState', r'\bstruct\s+SgResumeClassificationState\s*\{', True, source='native/sleep_resume_classification_native.inc') + '\nstatic SgResumeClassificationState g_sgResumeClassification;\nstatic std::atomic<unsigned long long> g_sgWakeClassificationBlockedGeneration{0};\n' + take('sharedSleepWakeClassified', r'^static\s+bool\s+sharedSleepWakeClassified\s*\([^;{}]*\)\s+noexcept\s*\{', source='native/sleep_resume_classification_native.inc')
    parts['WAKE_OPERATION'] = '\n'.join(take(name, source='native/sleep_recovery_auto.h') for name in ['proofSupersededLocked', 'wakeOperationAllowedLocked', 'wakeOperationAllows'])
    parts['POWER_COMMIT'] = take('sleepPowerPostResumeCommit', source='native/main.cpp') + '\n' + take('commitPowerResume', source='native/main.cpp')
    app_body = take('gameIdMatches', r'\binline\s+bool\s+gameIdMatches\s*\([^;{}]*\)\s*\{', source='native/steam_native_app_state_win32.h')
    parts['GAME_ID_MATCHES'] = app_body
    route_headers = [name for name in inputs if re.search(r'(?:route|proof)', Path(name).name) and name.startswith('native/')]
    parts['ROUTE_INCLUDES'] = '\n'.join('#include "' + Path(name).name + '"' for name in route_headers)
    cpp = inputs['tools/steam_original_recovery_dual_route_fixture.cpp.in'].decode('utf-8-sig').replace('\r\n', '\n').split('// YMCC_PUBLIC_WAKE_PARENT_BEGIN',1)[0]
    for token, body in parts.items():
        marker = '@@' + token + '@@'
        if cpp.count(marker) != 1:
            raise RuntimeError('missing/ambiguous fixture token: ' + marker)
        cpp = cpp.replace(marker, body)
    if '@@' in cpp:
        raise RuntimeError('unreplaced fixture token')
    scope_interface = take('ReadScope', r'\bclass\s+ReadScope\s*\{', True,
                           source='native/steam_native_read_scope.h')
    if args.scope in ['native-read-observation', 'startup-pair']:
        purpose_source = 'read-purpose-public-header.h' if args.read_purpose_header else 'native/steam_native_read_scope.h'
        purpose_enum = take('ReadPurpose', r'\benum\s+class\s+ReadPurpose\s*\{', True, source=purpose_source)
        cpp = adapt_native_read_observation_fixture(cpp, scope_interface, purpose_enum,
            inputs[case_name].decode('utf-8-sig'), bool(args.counterexample_original_root) and args.scope != 'startup-pair')
        if args.scope == 'startup-pair' and args.counterexample_original_root:
            cpp = '#define YMCC_OBSERVATION_OLD_COUNTEREXAMPLE 1\n' + cpp
    else:
        purpose_enum = take('ReadPurpose', r'\benum\s+class\s+ReadPurpose\s*\{', True, source='native/steam_native_read_scope.h')
        cpp = adapt_native_read_purpose_fixture(cpp, scope_interface, purpose_enum, False, args.scope == 'native-read-scope')
    if args.scope == 'startup-pair':
        anchor = 'static void steamOriginalRecoverySteamChanged()'
        if cpp.count(anchor) != 1: raise RuntimeError('nonunique startup-pair Steam callback')
        cpp = cpp.replace(anchor, '[[maybe_unused]] ' + anchor)
    for name, data in inputs.items():
        if name.startswith('native/') and name != files[0]:
            write_exclusive(out / Path(name).name, data)
    write_exclusive(out / 'recovery_startup_handoff_unsupported.h', inputs['tools/fixtures/recovery_startup_handoff_unsupported.h'])
    write_exclusive(out / 'json.hpp', inputs['deps/json/json.hpp'])
    tools = json.loads(inputs[files[10]].decode('utf-8-sig'))['native']
    env = {k.upper(): v for k, v in os.environ.items()}
    env['PATH'] = ';'.join([str(Path(tools['compiler']).parent), 'C:/Windows/System32', 'C:/Windows'])
    env['INCLUDE'] = ';'.join(tools['include'])
    env['LIB'] = ';'.join(tools['lib'])
    for key in ['CL', '_CL_', 'LINK', '_LINK_']:
        env[key] = ''
    report = {'schema': 'ymcc.steam-original-dual-route-selftest.v1',
              'startedAtUtc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'repo': str(repo), 'extractions': extracts,
              'inputs': [{'path': name, 'sha256': sha(data), 'bytes': len(data)} for name, data in inputs.items()],
              'productionMethodsReimplemented': False,
              'realConsumers': ['complete runOne', 'complete runRoute', 'wakeProof', 'sameScopeNow', 'sameObservationScopeNow', 'Admission', 'WakeProof.valid', 'accountCandidate', 'LegacyWakeProof.valid', 'RecoveryRoute.routeName'],
              'ports': ['virtual clock and power owner state', 'current game/Steam/parent identities',
                        'tracking and private read replies', 'shared immutable Observer identity receipt; registry candidate remains untrusted; forbidden direct registry-read counter', 'strict proof call counter invokes exact wakeProof', 'engine boundary records guard decisions', 'in-memory logs/evidence'],
              'sceneProcessOpens': 0, 'steamMutations': 0, 'sleepActions': 0,
              'fieldAcceptance': False, 'candidateIsNativeAccountProof': False, 'compilerSha256': sha(Path(tools['compiler']).read_bytes()),
              'limits': ['Engine execution/publisher/shared durable consume are ports; parent success-chain tests own those consumers', 'Queue drain and current owner are bounded ports; real enqueue and wake/Ready/stop entrypoints execute without OS sleep or live worker', 'No real wake, native Steam attachment, UI/input or physical Running validation'], 'runs': []}
    report['directedScope'] = args.scope
    report['expectedOldCounterexampleFailure'] = bool(args.counterexample_original_root)
    report['oldSourcePins'] = original_pins
    if args.scope in ['native-read-observation', 'startup-pair']:
        report['ports'].append('nativeSteamNativeObservationTracking: active checked before/after original tracking mock; no real producer attestation')
        report['publicReadPurposeReimplemented'] = False
        report['expectedCases'] = re.findall(r'\btest\("([^"]+)"', inputs[case_name].decode('utf-8-sig'))
        if args.counterexample_original_root:
            report['expectedCases'] = report['expectedCases'][:1]
    report['readScopeProducer'] = 'MOCK_INTERFACE_ONLY_REAL_FACTORY_VALIDATED_SEPARATELY'
    report['readScopeLifecycle'] = 'callback retention port only; actual QueryActor/debug/remote lifetime is source-reviewed'
    report['ports'].append('deterministic ReadScope subclass and formal nativeSteamNativeReadScope facade port')
    report['realConsumers'].extend(['enqueue', 'commitPowerResume'] + event_entrypoints + auto_producers)
    report['ports'].extend(['original power owner admission and arm/capture side effects', 'hardware write gate/message/log counters; no devices or live UI'])
    report['ports'].extend(['current owner pointer', 'SetEvent signal count', 'bounded single-event dequeue'])
    if 'legacyWakeProof' in helpers:
        report['realConsumers'].append('legacyWakeProof')
    if args.route_terminal_case == 'retained-after-preflight':
        cpp='#define YMCC_ROUTE_TERMINAL_FIRST_ONLY 1\n'+cpp
        report['expectedCases']=report['expectedCases'][:1]
    variants = [('current', cpp, False)]
    if args.negative_controls and args.scope == 'native-read-scope':
        controls = [
            ('ignore-native-read-scope', 'nativeReadScope->current()', 'true'),
            ('ignore-observation-read-scope',
             'if(!nativeReadScope->current()||(operationReadScope&&!operationReadScope->current()))return false;', 'if(false)return false;'),
        ]
        for name, old, new in controls:
            if name == 'ignore-native-read-scope':
                if cpp.count(old) < 4:
                    raise RuntimeError('missing native ReadScope guards')
                changed = cpp.replace(old, new)
            else:
                if cpp.count(old) != 1:
                    raise RuntimeError('nonunique observation ReadScope guard')
                changed = cpp.replace(old, new).replace('scopeNow(false))&&nativeReadScope->current()&&(!operationReadScope||operationReadScope->current());', 'scopeNow(false));')
            variants.append((name, changed, True))
    if args.negative_controls and args.scope is None:
        controls = [
            ('ignore-game-identity',
             'if(!(identity(target.pid,target.processCreated,target.path)==scope.game))return false;',
             'if(false)return false;'),
            ('ignore-steam-identity',
             r'return identity(steam.pid,steam.created,steam.root+L"\\steam.exe")==scope.steam;',
             r'return identity(steam.pid,steam.created,steam.root+L"\\steam.exe").valid();'),
            ('do-not-consume-wake',
             'decision=routeAdmission.admit(scope,{pending.kind,pending.generation,pending.sequence},enabled);',
             'decision=routeAdmission.preview(scope,{pending.kind,pending.generation,pending.sequence},enabled);'),
            ('ignore-kernel-confirmation',
             'proof.confirmed=ymcc::sleeprecovery::flag(admission,"kernelWakeConfirmed");',
             'proof.confirmed=true;'),
            ('static-gate-blocks-legacy',
             'const ULONGLONG powerEventFileTime=powerOwnerWitness?legacyProof.eventFileTime:proof.eventFileTime;',
             'const ULONGLONG powerEventFileTime=legacy?0:(powerOwnerWitness?legacyProof.eventFileTime:proof.eventFileTime);'),
            ('legacy-calls-strict-proof',
             'pending.kind==Trigger::Wake&&!legacy?wakeProof(pending.generation,configEpoch)',
             'pending.kind==Trigger::Wake?wakeProof(pending.generation,configEpoch)'),
            ('shared-route-admission',
             'auto& routeAdmission=legacy?c->legacyAdmission:c->admission;',
             'auto& routeAdmission=c->admission;'),
            ('ignore-registry-candidate',
             'return facts&&facts->registryAccountCandidateKnown()?facts->session.account:0;',
             'return DWORD{};'),
            ('ignore-final-candidate-change',
             'accountCandidate(finalSteam.account)==startingAccount',
             'accountCandidate(finalSteam.account)!=0'),
            ('ignore-final-binding',
             'const bool finalCurrent=bestOwns&&finalBinding&&',
             'const bool finalCurrent=bestOwns&&(finalBinding||true)&&'),
            ('second-route-can-publish-after-terminal',
             'runRoute(c,pending,config,route,!terminal,operationReadScope);',
             'runRoute(c,pending,config,route,true,operationReadScope);'),
            ('licensed-type-check-removed',
             '((std::stoull(tracked.gameId)>>24)&255)!=2',
             'false'),
            ('event-ignore-ready-gate',
             'if(g_powerLifecycle.load()==PowerLifecycle::Ready&&sharedSleepWakeClassified(generation))SteamOriginalRecovery::enqueue(c,ymcc::steamOriginal::Trigger::Wake,generation);',
             'if(sharedSleepWakeClassified(generation))SteamOriginalRecovery::enqueue(c,ymcc::steamOriginal::Trigger::Wake,generation);'),
            ('event-drop-deferred-wake',
             'c->latestPower=generation;c->deferredWake=generation;',
             'c->latestPower=generation;'),
            ('producer-drop-commit-ready-notification',
             'g_powerLifecycle.store(PowerLifecycle::Ready, std::memory_order_release);\n    sleepRecoveryAutoPowerReady(generation);',
             'g_powerLifecycle.store(PowerLifecycle::Ready, std::memory_order_release);'),
            ('producer-drop-arm-wake-notification',
             'SleepRecoveryAuto::arm(generation);steamOriginalRecoveryWake(generation);',
             'SleepRecoveryAuto::arm(generation);'),
        ]
        for name, old, new in controls:
            if cpp.count(old) != 1:
                raise RuntimeError(f'nonunique negative control {name}: {cpp.count(old)}')
            variants.append((name, cpp.replace(old, new), True))
    expected_negative_cases = {
        'ignore-native-read-scope': ('native-read-scope-revoked-acquisition-refuses', 'revoked-scope-entered-qualification'),
        'ignore-observation-read-scope': ('native-read-scope-identity-revokes-cached-and-fallback', 'identity-revocation-bypassed'),
        'ignore-game-identity': ('game-changes-during-private-read', 'changed-game-dispatched'),
        'ignore-steam-identity': ('steam-changes-during-private-read', 'changed-steam-dispatched'),
        'do-not-consume-wake': ('same-wake-never-refunds', 'wake-budget-refunded'),
        'ignore-kernel-confirmation': ('actual-wake-proof-refuses-unconfirmed', 'unconfirmed-static-witness-accepted'),
        'static-gate-blocks-legacy': ('s3-ready-no-kernel', 's3-routes-not-dispatched'),
        'legacy-calls-strict-proof': ('s3-legacy-does-not-read-ui-gate', 'legacy-read-strict-kernel-proof'),
        'shared-route-admission': ('independent-route-admission', 'legacy-admission-consumed-static'),
        'ignore-registry-candidate': ('stale-registry-pid-valid-candidate-legacy-dispatches', 'candidate-legacy-not-dispatched'),
        'ignore-final-candidate-change': ('candidate-change-revokes-final-ownership', 'changed-candidate-resurrected-ownership'),
        'ignore-final-binding': ('same-pid-different-full-gameid-cannot-resurrect', 'migrated-gameid-resurrected-ownership'),
        'second-route-can-publish-after-terminal': ('legacy-success-static-only-observes', 'static-can-double-publish'),
        'licensed-type-check-removed': ('licensed-among-us-type0-excluded-LegacyDynamic-Startup', 'licensed-entered-engine'),
        'event-ignore-ready-gate': ('event-wake-before-ready-enters-both-routes', 'wake-queued-before-ready'),
        'event-drop-deferred-wake': ('event-wake-before-ready-enters-both-routes', 'deferred-wake-lost'),
        'producer-drop-commit-ready-notification': ('producer-commit-ready-releases-deferred-wake', 'formal-ready-commit-lost-wake'),
        'producer-drop-arm-wake-notification': ('producer-arm-preserves-wake-before-ready', 'formal-arm-lost-wake-notification'),
    }
    for name, code, negative in variants:
        target = out / name
        target.mkdir()
        source = target / 'fixture.cpp'
        write_exclusive(source, code)
        row = {'name': name, 'negative': negative, 'generatedSourceSha256': sha(source.read_bytes())}
        if not args.prepare_only:
            exe = target / 'fixture.exe'
            command = [tools['compiler'], '/nologo', '/std:c++20', '/utf-8', '/EHsc', '/MT', '/O1', '/W4', '/WX', '/bigobj',
                       '/I'+str(out), '/I'+str(frozen/'native'), str(source), '/Fo'+str(target/'fixture.obj'), '/Fe'+str(exe)]
            built = subprocess.run(command, cwd=out, env=env, capture_output=True, timeout=60)
            write_exclusive(target/'compile.log', built.stdout+built.stderr)
            row['compileExit'] = built.returncode
            if built.returncode == 0:
                ran = subprocess.run([str(exe)], cwd=out, env=env, capture_output=True, timeout=25)
                write_exclusive(target/'run.log', ran.stdout+ran.stderr)
                row['runExit'] = ran.returncode
                try:
                    detail = json.loads(ran.stdout.decode('utf-8-sig'))
                    write_exclusive(target/'result.json', json.dumps(detail, indent=2)+'\n')
                    row.update(detail)
                    if args.scope == 'native-read-observation':
                        actual = [case.get('name') for case in detail.get('cases', [])]
                        if actual != report['expectedCases'] or len(actual) != len(set(actual)):
                            raise RuntimeError('exact directed case selection mismatch')
                    if negative:
                        expected_case, expected_reason = expected_negative_cases[name]
                        row['expectedFailure'] = {'case': expected_case, 'reason': expected_reason}
                        row['detected'] = ran.returncode == 1 and any(case.get('name') == expected_case and case.get('reason') == expected_reason and case.get('passed') is False for case in detail.get('cases', []))
                    else:
                        row['detected'] = ran.returncode == 0 and detail.get('passed') is True
                except Exception as exc:
                    row['parseError'] = str(exc)
        report['runs'].append(row)
    report['sourceDrift'] = []
    for name, data in inputs.items():
        origin = (Path(__file__).resolve() if name == 'tools/steam_original_recovery_dual_route_selftest.py' else
                  Path(__file__).with_name(Path(case_name).name) if args.scope in ['native-read-observation', 'startup-pair'] and name == case_name else
                  args.read_purpose_header if name == 'read-purpose-public-header.h' else
                  args.counterexample_original_root / name if args.counterexample_original_root and name in original_pins else repo / name)
        if origin.read_bytes() != data:
            report['sourceDrift'].append(name)
    report['preparedOnly'] = args.prepare_only
    report['success'] = not args.prepare_only and not report['sourceDrift'] and all(row.get('compileExit') == 0 and row.get('detected') is True for row in report['runs'])
    write_exclusive(out/'report.json', json.dumps(report, indent=2)+'\n')
    summary = [{k: v for k, v in row.items() if k != 'cases'} | {'failedCases': [case for case in row.get('cases', []) if case.get('passed') is False] if not row['negative'] else []} for row in report['runs']]
    print(json.dumps({'success': report['success'], 'preparedOnly': args.prepare_only, 'sourceDrift': report['sourceDrift'], 'runs': summary, 'report': str(out/'report.json')}, indent=2))
    return 0 if report['success'] or args.prepare_only else 1


if __name__ == '__main__':
    raise SystemExit(main())
