// Offline regression for presentation only. Uses exact main.cpp excerpts and inert OS/Host stand-ins.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';
import {readVirtualControllerSource} from './virtual_controller_runtime_source.mjs';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const outputArg=process.argv.indexOf('--output');
const out=path.resolve(outputArg>=0?process.argv[outputArg+1]:path.join(repo,'../../../Build/Validation/VirtualController-Lifecycle-20261019/player-switch-lock12/transition-status-'+Date.now()));
assert(!fs.existsSync(out),'Fresh output directory required; preserve prior evidence');
fs.mkdirSync(out,{recursive:true});
const runtime=readVirtualControllerSource(path.join(repo,'native/main.cpp')); 
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const files=['native/main.cpp','native/input_host_lifecycle_claim.h','native/controller_persona.h','native/virtual_target_policy.h','native/virtual_output_status.h','native/virtual_output_transition_status.h','src/bridge/virtualOutputStatus.ts','src/bridge/virtualTargetPolicy.ts','tools/virtual_output_status_selftest.ts','tools/virtual_output_transition_status_selftest.mjs','deps/json/json.hpp','tools/fixtures/hc_reviewed_calibration/toolchain.mjs','tools/virtual_controller_runtime_source.mjs','native/virtual_start_protection.h','native/ps_endpoint_ownership.h',...runtime.inputs.slice(1).map(row=>path.relative(repo,row.file).replaceAll('\\','/'))];
const frozen=path.join(out,'frozen-source');const manifest=[];
for(const rel of files){const b=fs.readFileSync(path.join(repo,rel));const target=path.join(frozen,rel);fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,b);manifest.push({file:rel,sha256:sha(b),bytes:b.length});}
fs.writeFileSync(path.join(out,'source-manifest.json'),JSON.stringify({capturedAt:new Date().toISOString(),repo,files:manifest},null,2)+'\n');
const expanded=readVirtualControllerSource(path.join(frozen,'native/main.cpp'));
const main=expanded.source;
fs.writeFileSync(path.join(out,'runtime-expansion.json'),JSON.stringify(expanded.manifest,null,2)+'\n');
fs.writeFileSync(path.join(out,'runtime-expanded.cpp'),main); 
const masked=main.replace(/\/\/[^\r\n]*|\/\*[\s\S]*?\*\/|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'/g,m=>' '.repeat(m.length));
const ranges=[];
function record(name,a,b){assert(a>=0&&b>a,'missing '+name);const code=main.slice(a,b);ranges.push({name,startLine:main.slice(0,a).split('\n').length,endLine:main.slice(0,b).split('\n').length,sha256:sha(code),code});return code;}
function endBrace(a){assert.equal(masked[a],'{');let d=0;for(let i=a;i<masked.length;i++){if(masked[i]==='{')d++;if(masked[i]==='}'&&--d===0)return i+1;}throw Error('unclosed brace');}
function extract(name){for(const m of main.matchAll(new RegExp('^static[^\\r\\n]*\\b'+name+'\\s*\\(','gm'))){const a=masked.indexOf('{',m.index),semi=masked.indexOf(';',m.index);if(semi>=0&&semi<a)continue;return record(name,m.index,endBrace(a));}throw Error('missing '+name);}
function between(name,a,b,from=0){const begin=main.indexOf(a,from),end=main.indexOf(b,begin);return record(name,begin,end);}
const snapshot=extract('virtualOutputRuntimeSnapshot');
// Exact disjoint presentation slices. Independent recovery-health continuity
// producer is executed by its own source-exact verifier, not stubbed here.
const producerAt=main.indexOf('static void gamepadReadStateUnlocked(');
const sourceCapture=between('existing canonical read-window metadata',
 '        g_inputCaptureGamepadSnapshot.readAttemptSequence=++diagnosticReadSequence;',
 '        if(inputSource=="msi-dinput")msiDinputApplyCanonicalReadProof(',producerAt)+
 between('existing presentation read-window copy','        statusSourceWindow =',
 '\n#ifdef YMCC_MSI_REPORT_CHAIN_DIAGNOSTIC',producerAt);
const sourceCheck=between('display-only source confidence assignment',
 '    facts.sourceCurrent = ymcc::virtualOutputSourceCurrent(',
 '    facts.personaMatches =',main.indexOf('static json virtualOutputRuntimeSnapshot('));
// This presentation fixture uses XInput; MSI physical proof has a separate source-exact regression.
const machineBlock=extract('inputHostSteamDeckSelfOutputBlocked');
const machineRejection=extract('inputHostMachineTargetRejectionReason');
const getPersona=extract('inputHostConfiguredPersona');
const refreshMatch=extract('inputHostRefreshPersonaMatchLocked');
const protectionSnapshot=extract('inputHostStartProtectionSnapshot');
const getRequest=extract('inputHostPresentationRequest');
const setDeferred=extract('inputHostPresentationDeferred');
const sourceLess=extract('inputHostCanCreateTargetWithoutPhysicalSource');
const productionPersona=extract('inputHostPersona');
const claim=between('lifecycle handoff','    if (!g_inputHostLifecycleWorkRequested.load(std::memory_order_acquire)) return;','    // GP-XBOX-2（裁决 §4）：生命周期工作执行段',main.indexOf('static void inputHostRunQueuedLifecycleWork() {'));
const setter=between('request publication at config assignment','    g_realStickTest.virtualTargetEnabled = targetEnabled;','    // 批129「请求拍」');
const decisionStart=main.indexOf('            const auto admissionStatusRequest = inputHostPresentationRequest();');
const decisionIf=main.indexOf('            if (!inputHostAdmitSelectedPhysicalSource() &&',decisionStart);
const firstEnd=endBrace(masked.indexOf('{',decisionIf));const elseStart=masked.indexOf('{',firstEnd);assert(/^\s*else\s*$/.test(masked.slice(firstEnd,elseStart)));
const decision=record('source deferred and queue handoff',decisionStart,endBrace(elseStart));
const protectionEdge=between('actual state-feed protection edge','    // Event-only protection edges.','    const bool steamDeckSelfOutputBlocked =',main.indexOf('    const json virtualOutput = virtualOutputRuntimeSnapshot(live, statusSourceWindow);'));
const publicationAt=main.indexOf('    if (g_gamepadStatePublished && externalPriorityRevision == publishedExternalPriorityRevision');
const publicationStart=main.indexOf('(protectionLocked ? ymcc::virtualOutputPublicationEquivalent(',publicationAt),publicationEnd=main.indexOf(' && backendFault == publishedBackendFault',publicationStart);
const publicationCompare=record('actual virtual-output publication comparison',publicationStart,publicationEnd);
fs.writeFileSync(path.join(out,'source-ranges.json'),JSON.stringify(ranges,null,2)+'\n');
const cpp=`#include <atomic>
#include <chrono>
#include <memory>
#include "virtual_start_protection.h"
#include "ps_endpoint_ownership.h"
#include <functional>
#include <mutex>
#include <string>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "json.hpp"
#include "virtual_output_status.h"
#include "virtual_output_transition_status.h"
#include "input_host_lifecycle_claim.h"
#include "controller_persona.h"
#include "virtual_target_policy.h"
using json=nlohmann::json;using ULONGLONG=unsigned long long;
struct Flag {std::atomic<bool> value{false};std::function<void()> afterExchange;unsigned exchanges=0;bool load(std::memory_order o=std::memory_order_seq_cst) const{return value.load(o);}void store(bool b,std::memory_order o=std::memory_order_seq_cst){value.store(b,o);}bool exchange(bool b,std::memory_order o=std::memory_order_seq_cst){++exchanges;bool old=value.exchange(b,o);if(afterExchange)afterExchange();return old;}};
struct InputHostMutex {bool deny=false;unsigned tries=0,blocks=0;std::function<void()> afterTryLock;void lock(){++blocks;}void unlock(){}bool try_lock(){++tries;if(afterTryLock)afterTryLock();return !deny;}};
static InputHostMutex g_inputHostMx;
static std::mutex g_inputHostPersonaMx,g_virtualOutputObservationMx;
static ymcc::VirtualOutputAdmissionStatus g_inputHostAdmissionStatus;
static struct {bool enabled=true,virtualTargetEnabled=true;std::string virtualTargetPersona="elite";} g_realStickTest;
static Flag g_externalPriorityVirtualSuspended,g_inputHostFaultHoldsStart,g_inputHostLifecycleWorkRequested,g_inputHostLifecycleWorkInFlight,g_inputHostRecoveryPending,g_msiOwnedReturnWorking,g_msiOwnedReturnFailed,g_sleepRecoveryOutputWanted;
static std::atomic<unsigned long long> g_inputHostPresentationWorkGeneration{0},g_inputHostEpochMirror{42};
static std::atomic<int> g_inputHostRecoveryFallbackState{0};
static unsigned long long g_inputHostEpoch=42,mockPower=7;
static std::atomic<unsigned> g_xinputTopologyEpoch{9};
static std::atomic<unsigned> g_xinputSlotIdentityEpoch{1};
static std::atomic<unsigned long long> g_msiMainSourceRevision{1};
static ymcc::VirtualOutputSourceWindow mockSourceWindow;
static bool mockAlive=true,g_inputHostPrepared=true,g_inputHostNeutralized=true,g_inputHostActive=true,g_inputHostTransportFault=false,g_inputHostReleaseQuarantined=false,mockMsi=false;
static ULONGLONG mockTick=10000;
static std::string mockBound="elite";
static std::string& g_inputHostBoundPersona=mockBound;
static std::atomic<bool> g_inputHostRequestedPersonaMatchesBound{false};
static ymcc::VirtualStartProtection g_virtualStartProtection;
static ymcc::psEndpointOwnership::State g_psEndpointOwnership;
static std::uint64_t inputHostStartProtectionNow(){return mockTick;}
static struct {unsigned long long hostEpoch=0;ULONGLONG observedAt=0;int verdict=0;} g_virtualOutputSuppression;
enum class PowerLifecycle {Ready,Resuming};static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static struct {int steamPresence(){return 1;}} g_steamSettingsObserver;
static ULONGLONG GetTickCount64(){return mockTick;}
static unsigned long long currentPowerGeneration(){return mockPower;}
static bool inputHostProcessAliveLocked(){return mockAlive;}
static bool msiClawKnownBoardAuthority(){return mockMsi;}
static std::string inputHostBoundPersonaValue(){return mockBound;}
static json externalControllerPrioritySnapshot(){return json::object();}
enum class YmccFamily {Unknown,ValveSteamDeck};
static struct {bool resolved=false;YmccFamily family=YmccFamily::Unknown;} g_machineIdentity;
${machineBlock}
${machineRejection}
${getPersona}
${refreshMatch}
${protectionSnapshot}
${getRequest}
${setDeferred}
static void configureProduction(const std::string& configuredPersona,bool targetEnabled){
${setter}
}
${snapshot}
static struct {unsigned long long readAttemptSequence=0,readPowerGeneration=0,readMsiSourceRevision=0;ULONGLONG readStartedTick=0,readCompletedTick=0;unsigned readTopologyGeneration=0,readIdentityGeneration=0;bool readContextStable=false;} g_inputCaptureGamepadSnapshot;
static ULONGLONG mockReadStarted=0;static unsigned long long mockReadPower=0;static unsigned mockReadTopology=0;
static ymcc::VirtualOutputSourceWindow captureProduction(){
    ymcc::VirtualOutputSourceWindow statusSourceWindow;
    static unsigned long long diagnosticReadSequence=0;
    const auto probeNow=mockReadStarted;
    const auto diagnosticReadPower=mockReadPower;
    const auto diagnosticReadTopology=mockReadTopology;
    const auto diagnosticReadIdentity=g_xinputSlotIdentityEpoch.load();
    const auto diagnosticReadMsiSourceRevision=g_msiMainSourceRevision.load();
    const std::string inputSource="xinput";
${sourceCapture}
    return statusSourceWindow;
}
static bool ownerAtDispatch=false;
static void claimProduction(){
${claim}
    ownerAtDispatch=g_inputHostLifecycleWorkInFlight.load();
}
static bool g_xinputPhysicalSourceLocked=false;static std::atomic<bool> g_msiDinputRouteDinputArmed{false};
${productionPersona}
// Preserve the existing static-literal token comparisons in this unmodified production function.
#pragma warning(push)
#pragma warning(disable:4130)
${sourceLess}
#pragma warning(pop)
static bool mockSourceAdmitted=false,mockQueueAccepted=true,mockPersonaTransition=false;
static unsigned admissionCalls=0,boundaryCalls=0,queueCalls=0,dirtyCalls=0;
static bool g_hidHideStartDeferredLogged=false;static ULONGLONG g_hidHideStartDeferredSinceTick=0,g_hidHideLastStartAttemptTick=0;
static json lifecycleNotes=json::array(),queueReasons=json::array();
static bool inputHostAdmitSelectedPhysicalSource(){++admissionCalls;return mockSourceAdmitted;}
static void xinputSlotsEnsurePreTargetBoundaryDiscovery(){++boundaryCalls;}
static void appendNativeLifecycleLog(const char* event,const json& fields){lifecycleNotes.push_back({{"event",event},{"fields",fields}});}
static void protectionEdgeProduction(const json& virtualOutput){
${protectionEdge}
 (void)protectionLocked; // also consumed by the adjacent real publication gate below
}
static void inputHostSwitchAuditThrottled(const char*,ULONGLONG,ULONGLONG){}
static bool inputHostQueueLifecycleWork(const char* reason){++queueCalls;queueReasons.push_back(reason);if(mockQueueAccepted)g_inputHostLifecycleWorkRequested.store(true,std::memory_order_release);return mockQueueAccepted;}
static void xinputSlotsMarkDiscoveryDirty(){++dirtyCalls;}
static void admissionProduction(){const bool personaTransition=mockPersonaTransition;
${decision}
}
static unsigned assertions=0,failed=0;static json cases=json::array(),presentationReceipts=json::array();
static void require(bool ok,const char* message){++assertions;if(!ok)throw std::runtime_error(message);}
static json snap(bool live=true){return virtualOutputRuntimeSnapshot(live,mockSourceWindow);}
static json statusChurnReceipts=json::array();
static bool publicationEquivalentProduction(const json& virtualOutput,const json& publishedVirtualOutput){
 const bool protectionLocked=virtualOutput.at("startProtection").at("locked").get<bool>();(void)protectionLocked;return ${publicationCompare};
}
static void sourceReceipt(const char* id,const json& receipt,const char* tone){presentationReceipts.push_back({{"id",id},{"receipt",receipt},{"expectedTone",tone}});}
static auto context(){return ymcc::VirtualOutputAdmissionStatus::Context{g_inputHostEpochMirror.load(),currentPowerGeneration(),g_inputHostPresentationWorkGeneration.load()};}
static bool waiting(){const auto r=inputHostPresentationRequest();std::lock_guard<std::mutex> l(g_inputHostPersonaMx);return g_inputHostAdmissionStatus.pending(r,context());}
// Every display case starts a fresh actual R3 object; no algorithm is copied.
static void reset(){g_psEndpointOwnership.clear();std::destroy_at(&g_virtualStartProtection);std::construct_at(&g_virtualStartProtection);ownerAtDispatch=false;g_inputHostLifecycleWorkRequested.afterExchange={};g_inputHostLifecycleWorkInFlight.afterExchange={};g_inputHostLifecycleWorkRequested.store(false);g_inputHostLifecycleWorkInFlight.store(false);g_inputHostPresentationWorkGeneration.store(0);g_inputHostAdmissionStatus=ymcc::VirtualOutputAdmissionStatus{};g_externalPriorityVirtualSuspended.store(false);g_inputHostFaultHoldsStart.store(false);g_inputHostRecoveryPending.store(false);g_inputHostRecoveryFallbackState.store(0);g_msiOwnedReturnWorking.store(false);g_msiOwnedReturnFailed.store(false);g_powerLifecycle.store(PowerLifecycle::Ready);g_inputHostEpoch=42;g_inputHostEpochMirror.store(42);mockPower=7;mockTick+=10000;g_xinputTopologyEpoch.store(9);mockReadStarted=mockTick-1;mockReadPower=mockPower;mockReadTopology=9;mockSourceWindow={1,mockReadStarted,mockTick,mockPower,9,true};g_realStickTest.enabled=true;configureProduction("elite",true);mockBound="elite";mockAlive=g_inputHostPrepared=g_inputHostNeutralized=g_inputHostActive=true;g_machineIdentity.resolved=false;g_machineIdentity.family=YmccFamily::Unknown;g_inputHostTransportFault=g_inputHostReleaseQuarantined=mockMsi=false;g_inputHostMx.deny=false;g_inputHostMx.afterTryLock={};g_virtualOutputSuppression={};g_xinputPhysicalSourceLocked=false;g_msiDinputRouteDinputArmed.store(false);mockSourceAdmitted=false;mockQueueAccepted=true;mockPersonaTransition=false;g_hidHideStartDeferredLogged=false;g_hidHideStartDeferredSinceTick=g_hidHideLastStartAttemptTick=0;admissionCalls=boundaryCalls=queueCalls=dirtyCalls=0;lifecycleNotes=queueReasons=json::array();}
static void dead(){mockAlive=g_inputHostPrepared=g_inputHostNeutralized=g_inputHostActive=false;}
static void test(const char* id,const std::function<void()>& body){reset();const unsigned before=assertions;try{body();cases.push_back({{"id",id},{"passed",true},{"assertions",assertions-before}});}catch(const std::exception& e){++failed;cases.push_back({{"id",id},{"passed",false},{"assertions",assertions-before},{"error",e.what()}});}}
int main(){
 test("claim-handoff",[]{dead();g_inputHostLifecycleWorkRequested.store(true);std::vector<json> observed;g_inputHostLifecycleWorkRequested.afterExchange=[&]{observed.push_back(snap());};g_inputHostLifecycleWorkInFlight.afterExchange=[&]{observed.push_back(snap());};claimProduction();g_inputHostLifecycleWorkRequested.afterExchange={};g_inputHostLifecycleWorkInFlight.afterExchange={};require(!observed.empty(),"handoff interception executed");for(const auto& s:observed)require(s["switchPending"]==true&&s["state"]=="starting","handoff never exposes false terminal");require(ownerAtDispatch,"work ownership retained up to fixture dispatch boundary");require(!g_inputHostLifecycleWorkRequested.load()&&!g_inputHostLifecycleWorkInFlight.load(),"actual claim guard releases owner on fixture scope completion");require(g_inputHostPresentationWorkGeneration.load()==1,"one actual attempt generation");});
 test("nested-claim-retains-owner-and-queued-request",[]{g_inputHostLifecycleWorkRequested.store(true);g_inputHostLifecycleWorkInFlight.store(true);claimProduction();require(g_inputHostLifecycleWorkRequested.load()&&g_inputHostLifecycleWorkInFlight.load(),"nested owner cannot consume work");require(g_inputHostPresentationWorkGeneration.load()==0,"nested claim is no new attempt");});
 test("idle-claim-read-only",[]{const auto r=g_inputHostLifecycleWorkRequested.exchanges,f=g_inputHostLifecycleWorkInFlight.exchanges;for(int i=0;i<1000;++i)claimProduction();require(g_inputHostLifecycleWorkRequested.exchanges==r&&g_inputHostLifecycleWorkInFlight.exchanges==f,"idle has no atomic exchange");require(g_inputHostPresentationWorkGeneration.load()==0,"idle does not rotate work generation");});
 test("purged-request-releases-provisional-owner",[]{g_inputHostLifecycleWorkRequested.store(true);g_inputHostLifecycleWorkInFlight.afterExchange=[]{g_inputHostLifecycleWorkRequested.store(false);};claimProduction();g_inputHostLifecycleWorkInFlight.afterExchange={};require(!g_inputHostLifecycleWorkRequested.load()&&!g_inputHostLifecycleWorkInFlight.load(),"purge cannot leave false busy");require(g_inputHostPresentationWorkGeneration.load()==0,"purged request not an attempt");});
 test("deferred-source",[]{dead();admissionProduction();auto s=snap(false);require(s["state"]=="starting"&&s["switchPending"]==true,"actual missing-source defer remains pending");require(g_hidHideStartDeferredLogged&&lifecycleNotes.size()==1,"existing defer note retained");require(admissionCalls==1&&boundaryCalls==1&&queueCalls==0,"actual admission decision unchanged");admissionProduction();require(lifecycleNotes.size()==1,"no repeated idle log");require(waiting(),"wait remains request bound");});
 test("deferred-old-persona",[]{mockBound="steamdeck";mockPersonaTransition=true;admissionProduction();const auto s=snap(false);require(s["state"]=="starting"&&s["switchPending"]==true,"waiting new persona is not a mismatch failure");require(queueCalls==0,"no admission bypass");});
 test("missing-source-alone-is-not-pending",[]{dead();const auto s=snap(false);require(s["state"]=="suspected-failed"&&s["switchPending"]==false,"saved intent alone cannot invent wait");});
 test("queue-clears-wait-after-requested-published",[]{dead();admissionProduction();require(waiting(),"wait established");mockSourceAdmitted=true;mockTick+=3000;admissionProduction();require(queueCalls==1&&dirtyCalls==1&&admissionCalls==2,"same production admission/queue calls");require(g_inputHostLifecycleWorkRequested.load()&&!waiting(),"queue handoff retires wait after busy publication");require(snap()["switchPending"]==true,"queue handoff remains pending");});
 test("backend-cooldown-retains-real-wait",[]{dead();admissionProduction();mockSourceAdmitted=true;g_hidHideLastStartAttemptTick=mockTick-100;admissionProduction();require(queueCalls==0&&waiting(),"actual admission cooldown still owns current wait");require(snap()["switchPending"]==true,"cooldown does not expose old host failure");});
 test("rejected-queue-does-not-retain-wait",[]{dead();admissionProduction();mockSourceAdmitted=true;mockQueueAccepted=false;mockTick+=3000;admissionProduction();require(!waiting()&&!g_inputHostLifecycleWorkRequested.load(),"denied queue retires old wait");require(snap()["state"]=="suspected-failed","no fake pending after denied queue");});
 test("new-persona-invalidates-old-wait",[]{dead();admissionProduction();const auto old=inputHostPresentationRequest();const auto c=context();configureProduction("dualsense-edge",true);inputHostPresentationDeferred(old,c,true);require(!waiting(),"late old-persona writer ignored");require(snap()["state"]=="suspected-failed","old wait does not mask new failure");configureProduction("elite",true);require(!waiting(),"persona ABA cannot revive wait");});
 test("same-persona-off-on-aba",[]{dead();admissionProduction();const auto old=inputHostPresentationRequest();const auto c=context();configureProduction("elite",false);configureProduction("elite",true);const auto next=inputHostPresentationRequest();require(next.generation!=old.generation,"real config assignment captures unseen OFF/ON");inputHostPresentationDeferred(old,c,true);require(!waiting(),"old same-persona request rejected");require(snap()["state"]=="suspected-failed","new request failure remains visible");admissionProduction();require(snap()["switchPending"]==true,"new actual defer may become pending");inputHostPresentationDeferred(old,c,false);require(waiting(),"old clear cannot erase new request wait");});
 test("off-cancels-deferred-status",[]{dead();admissionProduction();configureProduction("disabled",false);require(!waiting(),"OFF clears pending state");require(snap()["state"]=="disabled","OFF remains OFF");});
 test("host-epoch-invalidates-wait",[]{dead();admissionProduction();++g_inputHostEpoch;g_inputHostEpochMirror.store(g_inputHostEpoch);require(!waiting(),"new host epoch rejects wait");require(snap()["state"]=="suspected-failed","new host failure visible");});
 test("power-generation-invalidates-wait",[]{dead();admissionProduction();++mockPower;require(!waiting(),"new power generation rejects wait");require(snap()["state"]=="suspected-failed","old power wait does not mask current result");});
 test("new-attempt-failure-without-new-host-epoch",[]{dead();admissionProduction();const auto old=inputHostPresentationRequest();const auto c=context();g_inputHostLifecycleWorkRequested.store(true);claimProduction();g_inputHostLifecycleWorkInFlight.store(false);inputHostPresentationDeferred(old,c,true);g_inputHostTransportFault=true;require(!waiting(),"late pre-attempt wait rejected by work generation");const auto s=snap();require(s["state"]=="suspected-failed"&&s["reason"]=="transport-fault","actual new attempt failure remains visible");});
 test("confirmed-target-retires-old-wait",[]{dead();admissionProduction();mockAlive=g_inputHostPrepared=g_inputHostNeutralized=g_inputHostActive=true;require(snap()["state"]=="active","matching admitted target settles presentation");require(!waiting(),"positive target confirmation clears wait");g_inputHostTransportFault=true;require(snap()["reason"]=="transport-fault","later fault not hidden by retired wait");});
 test("terminal-fault-hold-wins",[]{dead();admissionProduction();g_inputHostFaultHoldsStart.store(true);const auto s=snap();require(s["reason"]=="backend-fault-hold"&&s["switchPending"]==false,"explicit terminal fault never hidden");});
 test("terminal-fallback-wins",[]{dead();admissionProduction();g_inputHostRecoveryFallbackState.store(3);const auto s=snap();require(s["reason"]=="physical-fallback"&&s["switchPending"]==false,"terminal fallback never hidden");});
 test("existing-lifecycle-and-release-pending",[]{dead();g_inputHostReleaseQuarantined=true;require(snap()["switchPending"]==true,"release remains pending");g_inputHostReleaseQuarantined=false;g_inputHostRecoveryPending.store(true);require(snap()["state"]=="recovering","recovery remains distinct");});
 test("msi-off-return-unconfirmed-unchanged",[]{configureProduction("disabled",false);mockMsi=true;g_msiOwnedReturnWorking.store(true);g_msiOwnedReturnFailed.store(true);require(snap(false)["reason"]=="physical-return-pending","active return precedes old failure");g_msiOwnedReturnWorking.store(false);require(snap(false)["reason"]=="physical-return-unconfirmed","settled unconfirmed preserved");});
 test("hid-and-locked-xbox-admission-not-changed",[]{configureProduction("steamdeck",true);dead();admissionProduction();require(queueCalls==1&&!waiting(),"HID source-less allowance unchanged");reset();dead();g_xinputPhysicalSourceLocked=true;admissionProduction();require(queueCalls==1&&!waiting(),"locked Xbox source-less allowance unchanged");reset();dead();g_msiDinputRouteDinputArmed.store(true);admissionProduction();require(queueCalls==1&&!waiting(),"managed route allowance unchanged");});
 test("zero-power-cannot-establish-wait",[]{dead();mockPower=0;admissionProduction();require(!waiting(),"unknown power cannot create durable pending");});
 test("steady-status-no-extra-host-blocking-or-publication",[]{const auto first=snap();const auto blocks=g_inputHostMx.blocks;bool same=true;for(int i=0;i<1000;++i)same=same&&(snap()==first);require(same,"steady JSON remains equal for publication dedupe");require(g_inputHostMx.blocks==blocks,"presentation never blocks on Host mutex");require(admissionCalls==0&&queueCalls==0&&dirtyCalls==0,"status reads never perform admission work");});

 test("source-window-crossed-topology",[]{g_xinputTopologyEpoch.fetch_add(1);mockSourceWindow=captureProduction();require(!mockSourceWindow.readContextStable,"real producer records crossed topology");const auto s=snap();require(s["state"]=="unknown"&&s["sourceLive"]==true&&s["sourceCurrent"]==false,"F3 crossed window cannot be green");sourceReceipt("F3-crossed-topology",s,"pending");});
 test("source-window-crossed-power",[]{++mockPower;mockSourceWindow=captureProduction();require(!mockSourceWindow.readContextStable,"real producer records crossed power");const auto s=snap();require(s["state"]=="unknown"&&s["reason"]=="physical-source-unconfirmed","crossed power stays uncertain");sourceReceipt("F3-crossed-power",s,"pending");});
 test("source-window-generation-changed-after-capture",[]{mockSourceWindow=captureProduction();require(mockSourceWindow.readContextStable,"completed read initially stable");g_xinputTopologyEpoch.fetch_add(1);auto s=snap();require(s["state"]=="unknown","new topology rejects previously stable window");sourceReceipt("topology-after-read",s,"pending");reset();mockSourceWindow=captureProduction();++mockPower;s=snap();require(s["state"]=="unknown","new power rejects previously stable window");sourceReceipt("power-after-read",s,"pending");});
 test("source-window-no-attempt-proof",[]{mockSourceWindow.readAttemptSequence=0;const auto s=snap();require(s["state"]=="unknown"&&s["sourceCurrent"]==false,"zero attempt cannot establish active");sourceReceipt("zero-attempt",s,"pending");});
 test("source-window-invalid-time-or-context",[]{const auto good=mockSourceWindow;for(unsigned fault=0;fault<7;++fault){mockSourceWindow=good;if(fault==0)mockSourceWindow.readStartedTick=0;if(fault==1)mockSourceWindow.readCompletedTick=0;if(fault==2)mockSourceWindow.readCompletedTick=mockReadStarted-1;if(fault==3)mockSourceWindow.readCompletedTick=mockTick+1;if(fault==4)mockSourceWindow.readContextStable=false;if(fault==5){mockSourceWindow.readPowerGeneration=0;mockPower=0;}if(fault==6)mockSourceWindow.readStartedTick=mockTick-1501;const auto s=snap();require(s["state"]=="unknown"&&s["sourceCurrent"]==false,"invalid window remains uncertain");sourceReceipt((std::string("invalid-window-")+std::to_string(fault)).c_str(),s,"pending");mockPower=7;}});
 test("source-window-age-bound-and-recovery",[]{mockSourceWindow.readStartedTick=mockTick-ymcc::kVirtualOutputSourceWindowMaxAgeMs;require(snap()["state"]=="active","freshness boundary is inclusive");--mockSourceWindow.readStartedTick;auto s=snap();require(s["state"]=="unknown","overlong read window cannot stay green");sourceReceipt("read-window-too-old",s,"pending");mockReadStarted=mockTick-1;mockSourceWindow=captureProduction();s=snap();require(s["state"]=="active"&&s["sourceCurrent"]==true,"new current canonical read restores active");sourceReceipt("fresh-read-restores-active",s,"on");});
 test("source-window-stationary-real-attempts-valid",[]{const unsigned packetNumber=55;mockSourceWindow=captureProduction();const auto firstSequence=mockSourceWindow.readAttemptSequence;require(snap()["state"]=="active","stationary first canonical read valid");mockTick+=50;mockReadStarted=mockTick-1;mockSourceWindow=captureProduction();const auto s=snap();require(packetNumber==55&&mockSourceWindow.readAttemptSequence!=firstSequence,"real attempts advance independently of constant packet number");require(s["state"]=="active","stationary second read stays valid");sourceReceipt("stationary-read",s,"on");});
 test("source-window-presentation-delayed",[]{g_inputHostMx.afterTryLock=[]{mockTick+=ymcc::kVirtualOutputSourceWindowMaxAgeMs+1;};const auto s=snap();g_inputHostMx.afterTryLock={};require(s["state"]=="unknown","freshness uses decision time after delayed observation");sourceReceipt("delayed-presentation",s,"pending");});
 test("source-window-expired-completion",[]{mockTick+=ymcc::kVirtualOutputSourceWindowMaxAgeMs+1;const auto s=snap();require(s["state"]=="unknown"&&s["sourceCurrent"]==false,"cached completion past freshness bound cannot be green");sourceReceipt("expired-completion",s,"pending");});
 test("source-window-disconnected-distinction",[]{auto s=snap(false);require(s["reason"]=="physical-source-unavailable","current confirmed absence keeps its real meaning");sourceReceipt("confirmed-source-absence",s,"warning");mockSourceWindow.readContextStable=false;s=snap(false);require(s["state"]=="unknown","unstable absence does not invent a terminal failure");sourceReceipt("uncertain-source-absence",s,"pending");});
 test("source-window-explicit-faults-stay-visible",[]{for(unsigned fault=0;fault<7;++fault){reset();mockSourceWindow.readContextStable=false;const char* reason="";if(fault==0){g_inputHostTransportFault=true;reason="transport-fault";}if(fault==1){g_inputHostFaultHoldsStart.store(true);reason="backend-fault-hold";}if(fault==2){g_inputHostRecoveryFallbackState.store(3);reason="physical-fallback";}if(fault==3){mockAlive=false;reason="host-not-alive";}if(fault==4){mockBound="steamdeck";reason="persona-mismatch";}if(fault==5){g_inputHostPrepared=false;reason="target-not-admitted";}if(fault==6){g_virtualOutputSuppression={42,mockTick,2};reason="suppression-degraded";}const auto s=snap();require(s["state"]=="suspected-failed"&&s["reason"]==reason,"explicit fault precedes source uncertainty");sourceReceipt(reason,s,"warning");}});
 test("source-window-metadata-does-not-defeat-json-dedupe",[]{mockSourceWindow=captureProduction();const auto first=snap();const auto oldSequence=mockSourceWindow.readAttemptSequence;mockTick+=50;mockReadStarted=mockTick-1;mockSourceWindow=captureProduction();const auto next=snap();require(mockSourceWindow.readAttemptSequence!=oldSequence,"new canonical attempt captured");require(first==next,"valid sequence/time advances keep identical status JSON");require(!next.contains("readAttemptSequence")&&!next.contains("readStartedTick")&&!next.contains("readCompletedTick")&&!next.contains("sourceWindow"),"per-read metadata never published");});
 test("retired-PS-return-is-visible-without-MSI",[]{configureProduction("disabled",false);dead();g_psEndpointOwnership.hello(json{{"psEndpointOwnershipVersion",1}},ymcc::psEndpointOwnership::Expected{"host","run","target","dualsense-edge",42});g_psEndpointOwnership.captureRetirement(true);const auto pending=snap();require(pending["physicalReturnPending"]==true&&pending["state"]=="recovering","actual non-MSI PS return ownership publishes pending return rather than completed OFF");g_psEndpointOwnership.confirmRetirement("run",42);const auto complete=snap();require(complete["physicalReturnPending"]==false&&complete["state"]=="disabled","completed PS return releases the pending presentation without touching MSI policy");});
 test("external-priority-publishes-protection-contract",[]{g_externalPriorityVirtualSuspended.store(true);const auto value=snap();require(value["state"]=="external-priority","real external-priority verdict remains unchanged");protectionEdgeProduction(value);require(value["startProtection"]["maxMs"]==30000,"actual feed consumes the real protection snapshot during external takeover");});
 test("physical-steamdeck-self-output-publishes-protection-contract",[]{g_machineIdentity.resolved=true;g_machineIdentity.family=YmccFamily::ValveSteamDeck;configureProduction("steamdeck",true);const auto value=snap();require(value["state"]=="disabled","physical SteamDeck self-emulation remains blocked");protectionEdgeProduction(value);require(value["startProtection"]["settleMs"]==10000,"actual feed consumes the real protection snapshot for a blocked persona");});
 for(const char* outcome:{"success","failure"})test((std::string("completed-lifecycle-status-stable-")+outcome).c_str(),[&]{
  const auto request=g_virtualStartProtection.admit("elite",mockTick);require(request.accepted,"actual R3 request admitted");
  require(g_virtualStartProtection.beginVerification(request.requestId,outcome,mockTick),"actual R3 execution candidate starts inspection");mockTick+=10000;
  require(g_virtualStartProtection.finishVerification(request.requestId,outcome,mockTick),"actual R3 verifier finalizes after its unchanged inspection");
  mockReadStarted=mockTick-1;mockSourceWindow=captureProduction();const auto first=snap();unsigned changed=0;auto last=first;
  for(unsigned n=0;n<125;++n){mockTick+=8;mockReadStarted=mockTick-1;mockSourceWindow=captureProduction();const auto current=snap();changed+=current!=last;last=current;}
  statusChurnReceipts.push_back({{"outcome",outcome},{"snapshots",126},{"changedAfterFirst",changed},{"first",first},{"last",last}});
  require(!last["startProtection"]["locked"].get<bool>()&&last["startProtection"]["phase"]==outcome,"display never revives a finalized native cycle");
  require(changed==0,"completed lifecycle telemetry must not defeat otherwise identical status dedupe");
 });
 test("active-lifecycle-countdown-bounded",[]{const auto request=g_virtualStartProtection.admit("elite",mockTick);require(request.accepted,"actual active R3 request admitted");require(g_virtualStartProtection.beginVerification(request.requestId,"success",mockTick),"actual active execution candidate");unsigned changed=0;auto last=inputHostStartProtectionSnapshot()["cycleElapsedMs"];for(unsigned n=0;n<125;++n){mockTick+=8;const auto current=inputHostStartProtectionSnapshot();changed+=current["cycleElapsedMs"]!=last;last=current["cycleElapsedMs"];const auto raw=g_virtualStartProtection.view(mockTick);require(raw.locked&&raw.verificationRemainingMs==10000-(n+1)*8,"display sampling cannot extend or finish native inspection");}statusChurnReceipts.push_back({{"outcome","verifying"},{"snapshots",126},{"cycleCountdownChanges",changed}});require(changed<=1,"active lifecycle display countdown changes at most once per second");});
 for(const char* phase:{"pending","verifying"})test((std::string("active-lifecycle-publication-bounded-")+phase).c_str(),[&]{
  const auto request=g_virtualStartProtection.admit("elite",mockTick);require(request.accepted,"real active request is admitted before publication check");if(std::string(phase)=="verifying")require(g_virtualStartProtection.beginVerification(request.requestId,"success",mockTick),"real execution candidate enters active inspection");
  mockReadStarted=mockTick-1;mockSourceWindow=captureProduction();auto published=snap();unsigned changes=0;for(unsigned n=0;n<125;++n){mockTick+=8;mockReadStarted=mockTick-1;mockSourceWindow=captureProduction();const auto current=snap();if(!publicationEquivalentProduction(current,published)){++changes;published=current;}const auto raw=g_virtualStartProtection.view(mockTick);require(raw.locked&&raw.elapsedMs==(n+1)*8,"presentation comparison preserves exact original monotonic execution period");if(std::string(phase)=="verifying")require(raw.verificationRemainingMs==10000-(n+1)*8,"quantized UI inspection never quantizes actual execution deadline");}
  statusChurnReceipts.push_back({{"outcome",phase},{"snapshots",126},{"publicationChanges",changes}});require(changes==1,"active same-input lifecycle publication changes once per display second");
 });
 test("publication-expiry-only-ignored-with-current-valid-lock",[]{auto left=snap();left["startProtection"]=json{{"locked",true},{"requestId",1ULL},{"expiresAtUnixMs",100000ULL},{"phase","verifying"},{"remainingMs",20000},{"verificationRemainingMs",9000}};auto right=left;right["startProtection"]["expiresAtUnixMs"]=100017ULL;const auto original=right;require(publicationEquivalentProduction(left,right),"actual gate ignores only valid same-request expiry projection");require(right==original,"publication comparison never mutates payload expiry");for(const char* key:{"requestId","remainingMs","verificationRemainingMs"}){right=left;right["startProtection"][key]=2;require(!publicationEquivalentProduction(left,right),"control and countdown changes must remain published");}right=left;right["startProtection"]["locked"]=false;require(!publicationEquivalentProduction(left,right),"native unlock cannot be hidden by expiry comparison");right=left;right["hostAlive"]=false;require(!publicationEquivalentProduction(left,right),"host fact changes still publish");right=left;right["unknown"]=1;require(!publicationEquivalentProduction(left,right),"new unknown top-level member still publishes");right=left;right["startProtection"]["unknown"]=1;require(!publicationEquivalentProduction(left,right),"new unknown protection member still publishes");right=left;right["startProtection"].erase("expiresAtUnixMs");require(!publicationEquivalentProduction(left,right),"missing expiry cannot match");right=left;right["startProtection"]["expiresAtUnixMs"]="100000";require(!publicationEquivalentProduction(left,right),"invalid expiry shape cannot match");left["startProtection"]["locked"]=false;right=left;right["startProtection"]["expiresAtUnixMs"]=100017ULL;require(!publicationEquivalentProduction(left,right),"unlocked fast path retains original full JSON equality");});
 std::cout<<json({{"passed",failed==0},{"cases",cases},{"presentationReceipts",presentationReceipts},{"statusChurnReceipts",statusChurnReceipts},{"tests",cases.size()},{"assertions",assertions},{"failed",failed},{"hardwareOperations",0},{"networkOperations",0}}).dump(2)<<std::endl;return failed?1:0;
}
`;
function command(name,exe,args,expected=0){const r=spawnSync(exe,args,{cwd:out,encoding:'utf8',timeout:60000,windowsHide:true});fs.writeFileSync(path.join(out,name+'.log'),[JSON.stringify({exe,args,status:r.status,error:r.error?.message}),r.stdout||'',r.stderr||''].join('\n'));assert(!r.error,name+' timed out');if(expected!==null)assert.equal(r.status,expected,name+' failed; see log');return r;}
const vcvars=process.env.YMCC_VCVARS64??'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
assert(fs.existsSync(vcvars),'MSVC x64 not found');
const results={profiles:{},mutations:{},hardwareOperations:0,networkOperations:0};
for(const profile of ['Release','Checked']){
 for(const variant of ['fixed','old-handoff','omit-deferred-pending','ignore-source-window','raw-cycle-clock','omit-early-protection','raw-inspection-clock','raw-publication-equality','omit-ps-return-status']){
  let source=cpp;
  if(variant==='old-handoff'){const old=`    if (!g_inputHostLifecycleWorkRequested.exchange(false, std::memory_order_acq_rel)) return;\n    if (g_inputHostLifecycleWorkInFlight.exchange(true, std::memory_order_acq_rel)) {\n        g_inputHostLifecycleWorkRequested.store(true, std::memory_order_release);\n        return;\n    }\n    g_inputHostPresentationWorkGeneration.fetch_add(1, std::memory_order_acq_rel);\n`;assert(source.includes(claim));source=source.replace(claim,old);}
  if(variant==='omit-deferred-pending'){assert(source.includes('|| admissionDeferred)'));source=source.replace('|| admissionDeferred)','|| (admissionDeferred && false))');}
  if(variant==='ignore-source-window'){assert(source.includes(sourceCheck));source=source.replace(sourceCheck,'    facts.sourceCurrent = true; (void)sourceWindow;\n');}
  if(variant==='raw-cycle-clock'){assert(source.includes('{"cycleElapsedMs",view.locked ? (view.elapsedMs/1000)*1000 : 0}'));source=source.replace('{"cycleElapsedMs",view.locked ? (view.elapsedMs/1000)*1000 : 0}','{"cycleElapsedMs",view.elapsedMs}');}
  if(variant==='omit-early-protection'){const line='            {"startProtection",inputHostStartProtectionSnapshot()},';assert(source.split(line).length===3);source=source.replaceAll(line,'');}
  if(variant==='raw-inspection-clock'){const from='{"verificationRemainingMs",view.locked ? ((view.verificationRemainingMs+999)/1000)*1000 : 0}';assert(source.includes(from));source=source.replace(from,'{"verificationRemainingMs",view.verificationRemainingMs}');}
  if(variant==='raw-publication-equality'){assert(source.includes(publicationCompare));source=source.replace(publicationCompare,'(virtualOutput == publishedVirtualOutput)');}
  if(variant==='omit-ps-return-status'){const from='(!facts.enabled && current && previous.psReturnPending)';assert(source.includes(from));source=source.replace(from,'(false && !facts.enabled && current && previous.psReturnPending)');}
  const name=profile+'-'+variant;const sourcePath=path.join(out,name+'.cpp'),exe=path.join(out,name+'.exe');fs.writeFileSync(sourcePath,source);
  const opts=['/nologo','/std:c++20','/utf-8','/EHsc','/W4','/WX','/permissive-','/GF','/MT',...(profile==='Release'?['/O2','/DNDEBUG']:['/Od','/RTC1']),'/I"'+path.join(frozen,'native')+'"','/I"'+path.join(frozen,'deps/json')+'"','"'+sourcePath+'"','/Fo"'+path.join(out,name+'.obj')+'"','/Fe"'+exe+'"'];
  const batch=path.join(out,name+'.cmd');fs.writeFileSync(batch,calibrationToolchainPreamble(vcvars)+'cl '+opts.join(' ')+'\r\nexit /b %errorlevel%\r\n');
  command(name+'-build','cmd.exe',['/d','/c',batch]);const r=command(name+'-execute',exe,[],null);const report=JSON.parse(r.stdout);fs.writeFileSync(path.join(out,name+'-results.json'),JSON.stringify(report,null,2)+'\n');
  if(variant==='fixed'){assert(r.status===0&&report.passed);results.profiles[profile]={passed:true,tests:report.tests,assertions:report.assertions,exeSha256:sha(fs.readFileSync(exe))};}
  else{const requiredCase=variant==='old-handoff'?'claim-handoff':variant==='ignore-source-window'?'source-window-crossed-topology':variant==='raw-cycle-clock'?'completed-lifecycle-status-stable-success':variant==='omit-early-protection'?'external-priority-publishes-protection-contract':variant==='raw-inspection-clock'?'active-lifecycle-publication-bounded-verifying':variant==='raw-publication-equality'?'publication-expiry-only-ignored-with-current-valid-lock':variant==='omit-ps-return-status'?'retired-PS-return-is-visible-without-MSI':'deferred-source';assert(r.status!==0&&report.cases.some(c=>c.id===requiredCase&&!c.passed),'mutation not killed: '+name);results.mutations[name]={killed:true,failedCases:report.cases.filter(c=>!c.passed).map(c=>c.id)};}
  console.log(name+': '+(variant==='fixed'?'PASS '+report.tests+'/'+report.assertions:'MUTATION_KILLED'));
 }
}
const require=createRequire(path.join(repo,'package.json'));const esbuild=require('esbuild');const frontend=path.join(out,'virtual_output_status_selftest.mjs');esbuild.buildSync({entryPoints:[path.join(frozen,'tools/virtual_output_status_selftest.ts')],bundle:true,platform:'node',format:'esm',outfile:frontend,logLevel:'silent'});const front=command('frontend-existing',process.execPath,[frontend]);results.frontend=front.stdout.trim();

const uiProbe=`import fs from 'node:fs';import assert from 'node:assert/strict';import {virtualOutputPresentation} from './frozen-source/src/bridge/virtualOutputStatus';const report=JSON.parse(fs.readFileSync(new URL('./Release-fixed-results.json',import.meta.url),'utf8'));let assertions=0;const views=[];for(const item of report.presentationReceipts){const view=virtualOutputPresentation(item.receipt,true,'elite');assert.equal(view.tone,item.expectedTone,item.id);++assertions;if(item.expectedTone==='pending'){assert.equal(view.text,'虚拟手柄-状态待确认',item.id);++assertions;}views.push({id:item.id,...view});}console.log(JSON.stringify({passed:true,assertions,views},null,2));`;
fs.writeFileSync(path.join(out,'source_window_ui_probe.ts'),uiProbe);const uiFile=path.join(out,'source_window_ui_probe.mjs');esbuild.buildSync({entryPoints:[path.join(out,'source_window_ui_probe.ts')],bundle:true,platform:'node',format:'esm',outfile:uiFile,logLevel:'silent'});const ui=command('source-window-ui',process.execPath,[uiFile]);results.sourceWindowUi=JSON.parse(ui.stdout);fs.writeFileSync(path.join(out,'source-window-ui-results.json'),JSON.stringify(results.sourceWindowUi,null,2)+'\n');
results.sourceChangedDuringRun=manifest.filter(x=>sha(fs.readFileSync(path.join(repo,x.file)))!==x.sha256).map(x=>x.file);results.passed=Object.keys(results.profiles).length===2&&Object.keys(results.mutations).length===16&&results.sourceChangedDuringRun.length===0;results.completedAt=new Date().toISOString();
fs.writeFileSync(path.join(out,'RESULT.json'),JSON.stringify(results,null,2)+'\n');console.log(JSON.stringify({...results,sourceWindowUi:{passed:results.sourceWindowUi.passed,assertions:results.sourceWindowUi.assertions,views:results.sourceWindowUi.views.length}},null,2));process.exitCode=results.passed?0:1;
