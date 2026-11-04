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
const args=process.argv.slice(2);
// Fail closed before output creation, source freezing, toolchain discovery or native work.
const valueOptions=new Set(['--case','--profiles','--source-root','--variants','--output','--ui-from-native']);
const switchOptions=new Set(['--fixed-only']);const seenOptions=new Set();
for(let i=0;i<args.length;++i){const flag=args[i];assert(valueOptions.has(flag)||switchOptions.has(flag),'Unknown argument: '+flag);assert(!seenOptions.has(flag),'Duplicate argument: '+flag);seenOptions.add(flag);if(valueOptions.has(flag)){assert(args[i+1]&&!args[i+1].startsWith('--'),flag+' requires a value');++i;}}

function option(flag){const at=args.indexOf(flag);if(at<0)return null;assert(args[at+1]&&!args[at+1].startsWith('--'),flag+' requires a value');return args[at+1];}
const selectedCase=option('--case');
const selectedCases=selectedCase?.split(',')??[];
assert(new Set(selectedCases).size===selectedCases.length&&selectedCases.every(Boolean),'Invalid original case selection');
const savedNativeUi=option('--ui-from-native');
const selectedProfiles=option('--profiles')?.split(',')??['Release','Checked'];
assert(selectedProfiles.length&&new Set(selectedProfiles).size===selectedProfiles.length&&selectedProfiles.every(p=>['Release','Checked'].includes(p)),'Invalid native profiles');
const candidateRoot=option('--source-root');
const originalVariants=['fixed','old-handoff','omit-deferred-pending','ignore-source-window','raw-cycle-clock','omit-early-protection','raw-inspection-clock','raw-publication-equality','omit-ps-return-status'];
const allowedVariants=[...originalVariants,'cache-ignores-source','cache-ignores-protection','retain-rejected-wait','clear-wrong-context','edge-ignores-request','edge-ignores-phase','edge-ignores-lock','inactive-frame-cache-off','inactive-frame-ignore-persona'];
const variants=option('--variants')?.split(',')??(args.includes('--fixed-only')?['fixed']:originalVariants);
assert(variants.length&&new Set(variants).size===variants.length&&variants.every(v=>allowedVariants.includes(v)),'Invalid explicit native variants');
if(savedNativeUi) assert(!selectedCase&&!args.includes('--fixed-only')&&!option('--variants')&&!option('--profiles'),'saved UI bridge does not execute native selections');
const outputArg=process.argv.indexOf('--output');
const out=path.resolve(outputArg>=0?process.argv[outputArg+1]:path.join(repo,'../../../Build/Validation/VirtualController-Lifecycle-20261019/player-switch-lock12/transition-status-'+Date.now()));
assert(!fs.existsSync(out),'Fresh output directory required; preserve prior evidence');
fs.mkdirSync(out,{recursive:true});
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
// The normal native tail and explicit saved-receipt path use the same UI bridge.
// Expectations are the declared source-unconfirmed contract, never the returned view.
function runSourceWindowUi(frozenRoot, nativeReceipt) {
 const require=createRequire(path.join(repo,'package.json'));const esbuild=require('esbuild');
 const uiProbe=`import fs from 'node:fs';import assert from 'node:assert/strict';import {virtualOutputPresentation} from './frozen-source/src/bridge/virtualOutputStatus';const report=JSON.parse(fs.readFileSync(${JSON.stringify(nativeReceipt)},'utf8'));let assertions=0;const views=[];assert.equal(report.presentationReceipts.length,26,'original source-window receipt set');++assertions;for(const item of report.presentationReceipts){const view=virtualOutputPresentation(item.receipt,true,'elite');const confirmedAbsent=item.id==='confirmed-source-absence';const expectedTone=confirmedAbsent?'pending':item.expectedTone;assert.equal(view.tone,expectedTone,item.id);++assertions;if(expectedTone==='pending'){if(confirmedAbsent){assert.equal(item.receipt.reason,'physical-source-unavailable',item.id+' confirmed absence cause');assert.equal(item.receipt.sourceCurrent,true,item.id+' confirmed current window');assert.equal(item.receipt.sourceLive,false,item.id+' actual absence');assertions+=3;}else{assert.equal(item.receipt.reason,'physical-source-unconfirmed',item.id+' declared pending cause');assert.equal(item.receipt.sourceCurrent,false,item.id+' source window remains unconfirmed');assertions+=2;}assert.equal(view.text,'虚拟手柄-等待输入',item.id);++assertions;}views.push({id:item.id,...view});}console.log(JSON.stringify({passed:true,assertions,views},null,2));`;
 assert.equal(frozenRoot,path.join(out,'frozen-source'),'UI bridge consumes captured sources');
 fs.writeFileSync(path.join(out,'source_window_ui_probe.ts'),uiProbe);const uiFile=path.join(out,'source_window_ui_probe.mjs');esbuild.buildSync({entryPoints:[path.join(out,'source_window_ui_probe.ts')],bundle:true,platform:'node',format:'esm',outfile:uiFile,logLevel:'silent'});const ui=command('source-window-ui',process.execPath,[uiFile]);const report=JSON.parse(ui.stdout);fs.writeFileSync(path.join(out,'source-window-ui-results.json'),JSON.stringify(report,null,2)+'\n');return report;
}
if(savedNativeUi){
 const receiptPath=path.resolve(savedNativeUi),receiptBytes=fs.readFileSync(receiptPath),native=JSON.parse(receiptBytes);
 const frozenRoot=path.join(out,'frozen-source'),inputs=[];
 for(const rel of ['src/bridge/virtualOutputStatus.ts','src/bridge/virtualTargetPolicy.ts','tools/virtual_output_transition_status_selftest.mjs']){
  const override=candidateRoot?path.resolve(candidateRoot,rel):null,sourceFile=override&&fs.existsSync(override)?override:path.join(repo,rel),bytes=fs.readFileSync(sourceFile),target=path.join(frozenRoot,rel);fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,bytes);inputs.push({file:rel,sourceFile,sha256:sha(bytes),bytes:bytes.length});
 }
 const result={scope:'original source-window UI bridge only',nativeExecutedThisRun:false,frontend283ExecutedThisRun:false,savedNative:{path:receiptPath,sha256:sha(receiptBytes),passed:native.passed,tests:native.tests,assertions:native.assertions,presentationReceipts:native.presentationReceipts.length,meaning:'Saved input reference only; no fresh native PASS'},inputs,hardwareOperations:0,networkOperations:0};
 fs.writeFileSync(path.join(out,'source-manifest.json'),JSON.stringify({capturedAt:new Date().toISOString(),files:inputs,savedNative:result.savedNative},null,2)+'\n');
 try{result.sourceWindowUi=runSourceWindowUi(frozenRoot,receiptPath);result.passed=true;}catch(error){result.passed=false;result.error=String(error);}
 result.sourceChangedDuringRun=inputs.filter(row=>sha(fs.readFileSync(row.sourceFile))!==row.sha256).map(row=>row.file);if(result.sourceChangedDuringRun.length)result.passed=false;
 result.completedAt=new Date().toISOString();fs.writeFileSync(path.join(out,'RESULT.json'),JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify(result,null,2));process.exit(result.passed?0:1);
}
const runtime=readVirtualControllerSource(path.join(repo,'native/main.cpp')); 
const files=['native/main.cpp','native/input_host_lifecycle_claim.h','native/controller_persona.h','native/virtual_target_policy.h','native/virtual_output_status.h','native/virtual_output_transition_status.h','src/bridge/virtualOutputStatus.ts','src/bridge/virtualTargetPolicy.ts','tools/virtual_output_status_selftest.ts','tools/virtual_output_transition_status_selftest.mjs','deps/json/json.hpp','tools/fixtures/hc_reviewed_calibration/toolchain.mjs','tools/virtual_controller_runtime_source.mjs','native/virtual_start_protection.h','native/virtual_controller_queued_start_runtime.h','native/virtual_controller_runtime_contract.h','native/ps_endpoint_ownership.h','tools/controller_snapshot_wire_selftest.cpp',...runtime.inputs.slice(1).map(row=>path.relative(repo,row.file).replaceAll('\\','/'))];
const frozen=path.join(out,'frozen-source');const manifest=[];
for(const rel of files){const override=candidateRoot?path.resolve(candidateRoot,rel):null;const sourceFile=override&&fs.existsSync(override)?override:path.join(repo,rel);const b=fs.readFileSync(sourceFile);const target=path.join(frozen,rel);fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,b);manifest.push({file:rel,sourceFile,sha256:sha(b),bytes:b.length});}
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
const inactiveFrame=extract('inputCaptureInactiveHostFrame');
const snapshotAt=main.indexOf(snapshot);
const protectionJson=main.includes('static json inputHostStartProtectionJson(')?extract('inputHostStartProtectionJson'):'';
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
 '    facts.personaMatches =',snapshotAt);
// This presentation fixture uses XInput; MSI physical proof has a separate source-exact regression.
const machineBlock=extract('inputHostSteamDeckSelfOutputBlocked');
const machineRejection=extract('inputHostMachineTargetRejectionReason');
const getPersona=extract('inputHostConfiguredPersona');
const refreshMatch=extract('inputHostRefreshPersonaMatchLocked');
const protectionSnapshot=extract('inputHostStartProtectionSnapshot');
const getRequest=extract('inputHostPresentationRequest');
const setDeferred=extract('inputHostPresentationDeferred');
const sourceLess=extract('inputHostCanCreateTargetWithoutPhysicalSource');
const queueWork=extract('inputHostQueueLifecycleWorkInternal');
const fenceRaised=extract('inputHostExitFenceRaised');
const cleanupWork=extract('inputHostLifecycleWorkIsCleanup');
const fenceBlocks=extract('inputHostExitFenceBlocks');
const fenceRefusal=extract('inputHostExitFenceLogRefusal');
const productionPersona=extract('inputHostPersona');
const claim=between('lifecycle handoff','    if (!g_inputHostLifecycleWorkRequested.load(std::memory_order_acquire)) return;','    // GP-XBOX-2（裁决 §4）：生命周期工作执行段',main.indexOf('static void inputHostRunQueuedLifecycleWork() {'));
const setter=between('request publication at config assignment','    g_realStickTest.virtualTargetEnabled = targetEnabled;','    // 批129「请求拍」');
const decisionStart=main.indexOf('            const auto admissionStatusRequest = inputHostPresentationRequest();');
const decisionIf=main.indexOf('            if (!inputHostAdmitSelectedPhysicalSource() &&',decisionStart);
const firstEnd=endBrace(masked.indexOf('{',decisionIf));const elseStart=masked.indexOf('{',firstEnd);assert(/^\s*else\s*$/.test(masked.slice(firstEnd,elseStart)));
const decision=record('source deferred and queue handoff',decisionStart,endBrace(elseStart));
const protectionEdge=between('actual state-feed protection edge','    // Event-only protection edges.','    const bool steamDeckSelfOutputBlocked =',main.indexOf('virtualOutputRuntimeSnapshot(live, statusSourceWindow);'));
const publicationAt=main.indexOf('    if (g_gamepadStatePublished && externalPriorityRevision == publishedExternalPriorityRevision');
const publicationStart=main.indexOf('(protectionLocked ? ymcc::virtualOutputPublicationEquivalent(',publicationAt),publicationEnd=main.indexOf(' && backendFault == publishedBackendFault',publicationStart);
const publicationCompare=record('actual virtual-output publication comparison',publicationStart,publicationEnd);
fs.writeFileSync(path.join(out,'source-ranges.json'),JSON.stringify(ranges,null,2)+'\n');
const allocationBody=fs.readFileSync(path.join(frozen,'tools/controller_snapshot_wire_selftest.cpp'),'utf8').split('static thread_local bool trackAlloc=false;')[1]?.split('#include "baseline.inc"')[0];
assert(allocationBody,'Original allocation counter body missing');
const allocationCounter='static thread_local bool trackAlloc=false;'+allocationBody;
const cpp=`#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <malloc.h>
#include <optional>
#include <memory>
#include "virtual_start_protection.h"
#include "ps_endpoint_ownership.h"
#include <functional>
#include <deque>
#include <mutex>
#include <string>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <tuple>
#include "json.hpp"
#include "virtual_output_status.h"
#include "virtual_output_transition_status.h"
#include "input_host_lifecycle_claim.h"
#include "controller_persona.h"
#include "virtual_target_policy.h"
using json=nlohmann::json;using ULONGLONG=unsigned long long;
${allocationCounter}
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
${protectionJson}
${protectionSnapshot}
${getRequest}
${setDeferred}
static void configureProduction(const std::string& configuredPersona,bool targetEnabled){
${setter}
}
${snapshot}
${inactiveFrame}
struct XINPUT_GAMEPAD {std::uint16_t wButtons=0;std::uint8_t bLeftTrigger=0,bRightTrigger=0;std::int16_t sThumbLX=0,sThumbLY=0,sThumbRX=0,sThumbRY=0;};
static struct {unsigned physicalReaderPid=0,physicalReaderPacket=0,physicalReaderActualSlot=4;unsigned long long physicalReaderSequence=0;XINPUT_GAMEPAD physicalReaderRawPad{};unsigned long long readAttemptSequence=0,readPowerGeneration=0,readMsiSourceRevision=0;ULONGLONG readStartedTick=0,readCompletedTick=0;unsigned readTopologyGeneration=0,readIdentityGeneration=0;bool readContextStable=false;} g_inputCaptureGamepadSnapshot;
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
    const bool usedPhysicalReader=false; // This existing fixture exercises only the canonical XInput branch.
    const struct {ULONGLONG tick=0;unsigned readerPid=0,packet=0,actualSlot=4;unsigned long long sequence=0;XINPUT_GAMEPAD pad{};} physicalReaderSample;
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
static bool mockSourceAdmitted=false,mockPersonaTransition=false;
static std::function<void()> beforeQueue;
static unsigned admissionCalls=0,boundaryCalls=0,queueCalls=0,dirtyCalls=0;
static bool g_hidHideStartDeferredLogged=false;static ULONGLONG g_hidHideStartDeferredSinceTick=0,g_hidHideLastStartAttemptTick=0;
static json lifecycleNotes=json::array(),queueReasons=json::array(),protectionEdgeReceipts=json::array();
static bool throwNextLifecycleLog=false;
static bool inputHostAdmitSelectedPhysicalSource(){++admissionCalls;return mockSourceAdmitted;}
static bool inputHostTargetReturnMayQueueMsiColdStart(){return false;} // No cold-return owner/IO is present in this display fixture.
static void xinputSlotsEnsurePreTargetBoundaryDiscovery(){++boundaryCalls;}
static void appendNativeLifecycleLog(const char* event,const json& fields){lifecycleNotes.push_back({{"event",event},{"fields",fields}});if(throwNextLifecycleLog){throwNextLifecycleLog=false;throw std::runtime_error("inert log-port write failure");}}
static void protectionEdgeProduction(const json& virtualOutput){
${protectionEdge}
 (void)protectionLocked; // also consumed by the adjacent real publication gate below
}
static void inputHostSwitchAuditThrottled(const char*,ULONGLONG,ULONGLONG){}
// Exact production bind/queue/fence code. Only unrelated MSI IO and timeout logging are inert ports.
static std::atomic<bool> g_inputHostExitFence{false};
static std::atomic<unsigned long long> g_inputHostExitFenceRefusals{0},g_inputHostExitFenceRefusalRows{0};
static constexpr unsigned long long kInputHostExitFenceRefusalRowMax=8;
static unsigned long long g_exitSessionId=1;
static std::mutex g_inputHostLifecycleWorkMx;
static std::deque<std::string> g_inputHostLifecycleWorkQueue;
static std::uint64_t g_inputHostSwitchRecoveryStartDeadline=0,g_inputHostSwitchRecoveryStartRequestId=0,g_inputHostSwitchRecoveryStartPower=0;
static json timeoutNotes=json::array(),queueBoundaryReceipts=json::array();
static void inputHostRecordSwitchTimeout(const ymcc::VirtualStartProtection::View& view){timeoutNotes.push_back({{"requestId",view.requestId},{"phase",view.phase},{"remainingMs",view.remainingMs}});}
#pragma warning(push)
#pragma warning(disable:4505)
#include "virtual_controller_queued_start_runtime.h"
#pragma warning(pop)
${fenceRaised}
${cleanupWork}
${fenceBlocks}
${fenceRefusal}
struct InputHostLateHidePublication {bool alreadyCompleted=false;};
static bool inputHostLateHideReuseQueuedPublication(InputHostLateHidePublication*,bool){throw std::runtime_error("unmodeled MSI publication port");}
static void inputHostLateHideCommitQueuedPublication(InputHostLateHidePublication*){throw std::runtime_error("unmodeled MSI commit port");}
static bool msiNativeRearIntentCurrent(const std::string&){return false;} // no native rear owner in these cases
static void inputHostSwitchAuditEnqueue(const std::string&,std::size_t){}
${queueWork}
static bool inputHostQueueLifecycleWork(const char* reason){++queueCalls;queueReasons.push_back(reason);if(beforeQueue)beforeQueue();return inputHostQueueLifecycleWorkInternal(reason,nullptr,nullptr);}
static void xinputSlotsMarkDiscoveryDirty(){++dirtyCalls;}
static void admissionProduction(){const bool personaTransition=mockPersonaTransition;
${decision}
}
static unsigned assertions=0,failed=0;static json cases=json::array(),presentationReceipts=json::array();
static void require(bool ok,const char* message){++assertions;if(!ok)throw std::runtime_error(message);}
static json snap(bool live=true){return virtualOutputRuntimeSnapshot(live,mockSourceWindow);}
static json statusChurnReceipts=json::array(),statusConstructionReceipts=json::array(),inactiveFrameConstructionReceipts=json::array();
static bool publicationEquivalentProduction(const json& virtualOutput,const json& publishedVirtualOutput){
 const bool protectionLocked=virtualOutput.at("startProtection").at("locked").get<bool>();(void)protectionLocked;return ${publicationCompare};
}
static void sourceReceipt(const char* id,const json& receipt,const char* tone){presentationReceipts.push_back({{"id",id},{"receipt",receipt},{"expectedTone",tone}});}
static auto context(){return ymcc::VirtualOutputAdmissionStatus::Context{g_inputHostEpochMirror.load(),currentPowerGeneration(),g_inputHostPresentationWorkGeneration.load()};}
static bool waiting(){const auto r=inputHostPresentationRequest();std::lock_guard<std::mutex> l(g_inputHostPersonaMx);return g_inputHostAdmissionStatus.pending(r,context());}
// Every display case starts a fresh actual R3 object; no algorithm is copied.
static void reset(){g_psEndpointOwnership.clear();std::destroy_at(&g_virtualStartProtection);std::construct_at(&g_virtualStartProtection);ownerAtDispatch=false;g_inputHostLifecycleWorkRequested.afterExchange={};g_inputHostLifecycleWorkInFlight.afterExchange={};g_inputHostLifecycleWorkRequested.store(false);g_inputHostLifecycleWorkInFlight.store(false);g_inputHostPresentationWorkGeneration.store(0);g_inputHostAdmissionStatus=ymcc::VirtualOutputAdmissionStatus{};g_externalPriorityVirtualSuspended.store(false);g_inputHostFaultHoldsStart.store(false);g_inputHostRecoveryPending.store(false);g_inputHostRecoveryFallbackState.store(0);g_msiOwnedReturnWorking.store(false);g_msiOwnedReturnFailed.store(false);g_powerLifecycle.store(PowerLifecycle::Ready);g_inputHostEpoch=42;g_inputHostEpochMirror.store(42);mockPower=7;mockTick+=10000;g_xinputTopologyEpoch.store(9);mockReadStarted=mockTick-1;mockReadPower=mockPower;mockReadTopology=9;mockSourceWindow={1,mockReadStarted,mockTick,mockPower,9,true};g_realStickTest.enabled=true;configureProduction("elite",true);mockBound="elite";mockAlive=g_inputHostPrepared=g_inputHostNeutralized=g_inputHostActive=true;g_machineIdentity.resolved=false;g_machineIdentity.family=YmccFamily::Unknown;g_inputHostTransportFault=g_inputHostReleaseQuarantined=mockMsi=false;g_inputHostMx.deny=false;g_inputHostMx.afterTryLock={};g_virtualOutputSuppression={};g_xinputPhysicalSourceLocked=false;g_msiDinputRouteDinputArmed.store(false);mockSourceAdmitted=false;mockPersonaTransition=false;beforeQueue={};g_inputHostExitFence.store(false);g_inputHostExitFenceRefusals.store(0);g_inputHostExitFenceRefusalRows.store(0);g_inputHostLifecycleWorkQueue.clear();g_inputHostSwitchRecoveryStartDeadline=g_inputHostSwitchRecoveryStartRequestId=g_inputHostSwitchRecoveryStartPower=0;ymcc::inputHost::queuedPlayerStart::lastExpiredPlayerWorkNoted.store(0);timeoutNotes=json::array();g_hidHideStartDeferredLogged=false;g_hidHideStartDeferredSinceTick=g_hidHideLastStartAttemptTick=0;admissionCalls=boundaryCalls=queueCalls=dirtyCalls=0;throwNextLifecycleLog=false;lifecycleNotes=queueReasons=json::array();}
static void dead(){mockAlive=g_inputHostPrepared=g_inputHostNeutralized=g_inputHostActive=false;}
static void test(const char* id,const std::function<void()>& body){static const std::vector<std::string> selected=${JSON.stringify(selectedCases).replace('[','{').replace(']','}')};if(!selected.empty()&&std::find(selected.begin(),selected.end(),std::string(id))==selected.end())return;reset();const unsigned before=assertions;try{body();cases.push_back({{"id",id},{"passed",true},{"assertions",assertions-before}});}catch(const std::exception& e){++failed;cases.push_back({{"id",id},{"passed",false},{"assertions",assertions-before},{"error",e.what()}});}}
int main(){
 test("claim-handoff",[]{dead();g_inputHostLifecycleWorkRequested.store(true);std::vector<json> observed;g_inputHostLifecycleWorkRequested.afterExchange=[&]{observed.push_back(snap());};g_inputHostLifecycleWorkInFlight.afterExchange=[&]{observed.push_back(snap());};claimProduction();g_inputHostLifecycleWorkRequested.afterExchange={};g_inputHostLifecycleWorkInFlight.afterExchange={};require(!observed.empty(),"handoff interception executed");for(const auto& s:observed)require(s["switchPending"]==true&&s["state"]=="starting","handoff never exposes false terminal");require(ownerAtDispatch,"work ownership retained up to fixture dispatch boundary");require(!g_inputHostLifecycleWorkRequested.load()&&!g_inputHostLifecycleWorkInFlight.load(),"actual claim guard releases owner on fixture scope completion");require(g_inputHostPresentationWorkGeneration.load()==1,"one actual attempt generation");});
 test("nested-claim-retains-owner-and-queued-request",[]{g_inputHostLifecycleWorkRequested.store(true);g_inputHostLifecycleWorkInFlight.store(true);claimProduction();require(g_inputHostLifecycleWorkRequested.load()&&g_inputHostLifecycleWorkInFlight.load(),"nested owner cannot consume work");require(g_inputHostPresentationWorkGeneration.load()==0,"nested claim is no new attempt");});
 test("idle-claim-read-only",[]{const auto r=g_inputHostLifecycleWorkRequested.exchanges,f=g_inputHostLifecycleWorkInFlight.exchanges;for(int i=0;i<1000;++i)claimProduction();require(g_inputHostLifecycleWorkRequested.exchanges==r&&g_inputHostLifecycleWorkInFlight.exchanges==f,"idle has no atomic exchange");require(g_inputHostPresentationWorkGeneration.load()==0,"idle does not rotate work generation");});
 test("purged-request-releases-provisional-owner",[]{g_inputHostLifecycleWorkRequested.store(true);g_inputHostLifecycleWorkInFlight.afterExchange=[]{g_inputHostLifecycleWorkRequested.store(false);};claimProduction();g_inputHostLifecycleWorkInFlight.afterExchange={};require(!g_inputHostLifecycleWorkRequested.load()&&!g_inputHostLifecycleWorkInFlight.load(),"purge cannot leave false busy");require(g_inputHostPresentationWorkGeneration.load()==0,"purged request not an attempt");});
 test("deferred-source",[]{dead();admissionProduction();auto s=snap(false);require(s["state"]=="starting"&&s["switchPending"]==true,"actual missing-source defer remains pending");require(g_hidHideStartDeferredLogged&&lifecycleNotes.size()==1,"existing defer note retained");require(admissionCalls==1&&boundaryCalls==1&&queueCalls==0,"actual admission decision unchanged");admissionProduction();require(lifecycleNotes.size()==1,"no repeated idle log");require(waiting(),"wait remains request bound");});
 test("deferred-old-persona",[]{mockBound="steamdeck";mockPersonaTransition=true;admissionProduction();const auto s=snap(false);require(s["state"]=="starting"&&s["switchPending"]==true,"waiting new persona is not a mismatch failure");require(queueCalls==0,"no admission bypass");});
 test("missing-source-alone-is-not-pending",[]{dead();const auto s=snap(false);require(s["state"]=="suspected-failed"&&s["switchPending"]==false,"saved intent alone cannot invent wait");});
 test("queue-clears-wait-after-requested-published",[]{dead();admissionProduction();require(waiting(),"wait established");mockSourceAdmitted=true;mockTick+=3000;admissionProduction();require(queueCalls==1&&dirtyCalls==1&&admissionCalls==2,"same production admission/queue calls");require(g_inputHostLifecycleWorkRequested.load()&&!waiting(),"queue handoff retires wait after busy publication");require(snap()["switchPending"]==true,"queue handoff remains pending");const auto depth=g_inputHostLifecycleWorkQueue.size();bool coalesced=false;require(inputHostQueueLifecycleWorkInternal("initial-target-admission",&coalesced,nullptr)&&coalesced,"same-reason production merge remains accepted");require(g_inputHostLifecycleWorkQueue.size()==depth&&g_inputHostLifecycleWorkRequested.load(),"merge retains one queue owner");});
 test("backend-cooldown-retains-real-wait",[]{dead();admissionProduction();mockSourceAdmitted=true;g_hidHideLastStartAttemptTick=mockTick-100;admissionProduction();require(queueCalls==0&&waiting(),"actual admission cooldown still owns current wait");require(snap()["switchPending"]==true,"cooldown does not expose old host failure");});
 test("rejected-queue-does-not-retain-wait",[]{
  dead();const auto t0=mockTick;const auto player=g_virtualStartProtection.admit("elite",t0);require(player.accepted&&player.requestId,"real player request admitted");
  const auto original=g_virtualStartProtection.view(mockTick);const auto deadline=mockTick+original.remainingMs;
  admissionProduction();const auto request=inputHostPresentationRequest();const auto c=context();require(waiting(),"original missing-source wait established");
  mockSourceAdmitted=true;mockTick=deadline;const auto before=snap();admissionProduction();const auto after=snap();const auto terminal=g_virtualStartProtection.view(mockTick);
  queueBoundaryReceipts.push_back({{"scenario","expired-player"},{"playerId",player.requestId},{"originalDeadline",deadline},{"tick",mockTick},{"requestGeneration",request.generation},{"requestEnabled",request.enabled},{"requestPersona",request.persona},{"hostEpoch",c.hostEpoch},{"powerGeneration",c.powerGeneration},{"workGeneration",c.workGeneration},{"before",before},{"after",after},{"timeoutNotes",timeoutNotes},{"queueNotes",lifecycleNotes},{"requested",g_inputHostLifecycleWorkRequested.load()},{"inFlight",g_inputHostLifecycleWorkInFlight.load()},{"queueDepth",g_inputHostLifecycleWorkQueue.size()}});
  require(terminal.requestId==player.requestId&&terminal.phase=="expired"&&!terminal.locked&&terminal.remainingMs==0&&terminal.fallbackAttempts==original.fallbackAttempts,"terminal refusal retains original ticket/deadline/budget");
  require(!timeoutNotes.empty()&&timeoutNotes.back()["requestId"]==player.requestId,"real bind reports same expired player");
  require(std::any_of(lifecycleNotes.begin(),lifecycleNotes.end(),[](const json& n){return n["event"]=="input-host-player-start-work"&&n["fields"].value("reason",std::string{})=="expired-player-start-cannot-become-policy-work";}),"real terminal refusal reason recorded");
  require(!waiting()&&!g_inputHostLifecycleWorkRequested.load()&&g_inputHostLifecycleWorkQueue.empty(),"denied queue retires old wait");require(after["state"]=="suspected-failed"&&!after["switchPending"].get<bool>(),"no fake pending after denied queue");require(!g_inputHostLifecycleWorkInFlight.load()&&dirtyCalls==0&&queueCalls==1,"refusal starts no worker/discovery");
  // Fence can rise after the outer admission guard; independent cleanup/return ownership remains.
  reset();dead();admissionProduction();mockSourceAdmitted=true;g_inputHostReleaseQuarantined=true;g_msiOwnedReturnWorking.store(true);beforeQueue=[] {g_inputHostExitFence.store(true);bool merged=false;if(!inputHostQueueLifecycleWorkInternal("startup-owned-return",&merged,nullptr))throw std::runtime_error("cleanup must survive fence");};mockTick+=3000;admissionProduction();
  const auto fenced=snap();queueBoundaryReceipts.push_back({{"scenario","fence-with-cleanup"},{"after",fenced},{"queueNotes",lifecycleNotes},{"requested",g_inputHostLifecycleWorkRequested.load()},{"queue",g_inputHostLifecycleWorkQueue}});
  require(g_inputHostExitFenceRefusals.load()==1&&!waiting(),"real fence refusal retires matching display wait");require(g_inputHostLifecycleWorkQueue.size()==1&&g_inputHostLifecycleWorkQueue.front()=="startup-owned-return"&&g_inputHostLifecycleWorkRequested.load(),"independent queued cleanup remains owned");require(g_inputHostReleaseQuarantined&&g_msiOwnedReturnWorking.load()&&fenced["switchPending"]==true,"independent release/return debt stays pending");
 });
 test("new-persona-invalidates-old-wait",[]{dead();admissionProduction();const auto old=inputHostPresentationRequest();const auto c=context();configureProduction("dualsense-edge",true);inputHostPresentationDeferred(old,c,true);require(!waiting(),"late old-persona writer ignored");require(snap()["state"]=="suspected-failed","old wait does not mask new failure");configureProduction("elite",true);require(!waiting(),"persona ABA cannot revive wait");});
 test("same-persona-off-on-aba",[]{dead();admissionProduction();const auto old=inputHostPresentationRequest();const auto c=context();configureProduction("elite",false);configureProduction("elite",true);const auto next=inputHostPresentationRequest();require(next.generation!=old.generation,"real config assignment captures unseen OFF/ON");inputHostPresentationDeferred(old,c,true);require(!waiting(),"old same-persona request rejected");require(snap()["state"]=="suspected-failed","new request failure remains visible");admissionProduction();require(snap()["switchPending"]==true,"new actual defer may become pending");inputHostPresentationDeferred(old,c,false);require(waiting(),"old clear cannot erase new request wait");});
 test("off-cancels-deferred-status",[]{dead();admissionProduction();configureProduction("disabled",false);require(!waiting(),"OFF clears pending state");require(snap()["state"]=="disabled","OFF remains OFF");});
 test("host-epoch-invalidates-wait",[]{dead();admissionProduction();++g_inputHostEpoch;g_inputHostEpochMirror.store(g_inputHostEpoch);require(!waiting(),"new host epoch rejects wait");require(snap()["state"]=="suspected-failed","new host failure visible");});
 test("power-generation-invalidates-wait",[]{dead();admissionProduction();++mockPower;require(!waiting(),"new power generation rejects wait");require(snap()["state"]=="suspected-failed","old power wait does not mask current result");});
 test("new-attempt-failure-without-new-host-epoch",[]{dead();admissionProduction();const auto old=inputHostPresentationRequest();const auto c=context();g_inputHostLifecycleWorkRequested.store(true);claimProduction();g_inputHostLifecycleWorkInFlight.store(false);inputHostPresentationDeferred(old,c,true);g_inputHostTransportFault=true;require(!waiting(),"late pre-attempt wait rejected by work generation");const auto s=snap();require(s["state"]=="suspected-failed"&&s["reason"]=="transport-fault","actual new attempt failure remains visible");g_inputHostTransportFault=false;inputHostPresentationDeferred(old,context(),true);require(waiting(),"new work context owns a fresh wait");inputHostPresentationDeferred(old,c,false);require(waiting(),"old-context terminal callback cannot clear newer wait");});
 test("confirmed-target-retires-old-wait",[]{dead();admissionProduction();mockAlive=g_inputHostPrepared=g_inputHostNeutralized=g_inputHostActive=true;require(snap()["state"]=="active","matching admitted target settles presentation");require(!waiting(),"positive target confirmation clears wait");g_inputHostTransportFault=true;require(snap()["reason"]=="transport-fault","later fault not hidden by retired wait");});
 test("terminal-fault-hold-wins",[]{dead();admissionProduction();g_inputHostFaultHoldsStart.store(true);const auto s=snap();require(s["reason"]=="backend-fault-hold"&&s["switchPending"]==false,"explicit terminal fault never hidden");});
 test("terminal-fallback-wins",[]{dead();admissionProduction();g_inputHostRecoveryFallbackState.store(3);const auto s=snap();require(s["reason"]=="physical-fallback"&&s["switchPending"]==false,"terminal fallback never hidden");});
 test("existing-lifecycle-and-release-pending",[]{dead();g_inputHostReleaseQuarantined=true;require(snap()["switchPending"]==true,"release remains pending");g_inputHostReleaseQuarantined=false;g_inputHostRecoveryPending.store(true);require(snap()["state"]=="recovering","recovery remains distinct");});
 test("msi-off-return-unconfirmed-unchanged",[]{configureProduction("disabled",false);mockMsi=true;g_msiOwnedReturnWorking.store(true);g_msiOwnedReturnFailed.store(true);require(snap(false)["reason"]=="physical-return-pending","active return precedes old failure");g_msiOwnedReturnWorking.store(false);require(snap(false)["reason"]=="physical-return-unconfirmed","settled unconfirmed preserved");});
 test("hid-and-locked-xbox-admission-not-changed",[]{configureProduction("steamdeck",true);dead();admissionProduction();require(queueCalls==1&&!waiting(),"HID source-less allowance unchanged");reset();dead();g_xinputPhysicalSourceLocked=true;admissionProduction();require(queueCalls==1&&!waiting(),"locked Xbox source-less allowance unchanged");reset();dead();g_msiDinputRouteDinputArmed.store(true);admissionProduction();require(queueCalls==1&&!waiting(),"managed route allowance unchanged");});
 test("zero-power-cannot-establish-wait",[]{dead();mockPower=0;admissionProduction();require(!waiting(),"unknown power cannot create durable pending");});
 test("steady-status-no-extra-host-blocking-or-publication",[]{
  const auto first=snap();const auto blocks=g_inputHostMx.blocks;bool same=true;
  allocationRequests=allocationBytes=0;trackAlloc=true;const auto began=std::chrono::steady_clock::now();
  for(int i=0;i<1000;++i){const auto& current=virtualOutputRuntimeSnapshot(true,mockSourceWindow);same=same&&(current==first);}
  const auto ended=std::chrono::steady_clock::now();trackAlloc=false;
  statusConstructionReceipts.push_back({{"calls",1000},{"allocationRequests",allocationRequests},{"allocationBytes",allocationBytes},{"elapsedNs",std::chrono::duration_cast<std::chrono::nanoseconds>(ended-began).count()}});
  require(same,"steady JSON remains equal for publication dedupe");require(g_inputHostMx.blocks==blocks,"presentation never blocks on Host mutex");require(admissionCalls==0&&queueCalls==0&&dirtyCalls==0,"status reads never perform admission work");
  require(allocationRequests==0&&allocationBytes==0,"unchanged unlocked presentation performs no heap allocation");
   protectionEdgeProduction(first);const auto logCount=lifecycleNotes.size();allocationRequests=allocationBytes=0;trackAlloc=true;
   for(int i=0;i<1000;++i)protectionEdgeProduction(first);
   trackAlloc=false;protectionEdgeReceipts.push_back({{"calls",1000},{"allocationRequests",allocationRequests},{"allocationBytes",allocationBytes},{"eventsBefore",logCount},{"eventsAfter",lifecycleNotes.size()}});
   require(lifecycleNotes.size()==logCount,"stable protection edge never emits a repeated lifecycle event");require(allocationRequests==0&&allocationBytes==0,"stable protection edge performs no heap allocation");
  // Same original idle-presentation case: the disabled frame has no admission authority.
  const std::vector<std::tuple<bool,std::string,std::string,std::string>> inactiveFacts{
   {false,"none","disabled","left"}, {true,"none","disabled","left"},
   {true,"settled","disabled","left"}, {true,"settled","elite","left"},
   {true,"settled","elite","right"}, {false,"none","disabled","left"},
   {true,std::string(192,'l'),std::string(112,'p'),std::string(80,'s')}
  };
  auto originalDisabledFrame=[](bool active,const std::string& lifecycle,const std::string& persona,const std::string& targetStick){
   return json{{"hostSubmission","publication-suppressed"},{"publicationReason","virtual-target-disabled"},
    {"hostActive",active},{"firstFrame",false},{"lifecycle",lifecycle},{"persona",persona},{"targetStick",targetStick},
    {"motionAdmission","safe-zero"},{"leftStick",{{"x",0.0},{"y",0.0}}},{"rightStick",{{"x",0.0},{"y",0.0}}}};
  };
  for(const auto& [active,lifecycle,persona,targetStick]:inactiveFacts){
   const json expected=originalDisabledFrame(active,lifecycle,persona,targetStick);
   const auto& initial=inputCaptureInactiveHostFrame(active,lifecycle,persona,targetStick);
   require(initial.dump()==expected.dump(),"disabled frame retains exact original wire values for every changed fact");
   allocationRequests=allocationBytes=0;trackAlloc=true;bool equal=true;
   for(int i=0;i<1000;++i){const auto& frame=inputCaptureInactiveHostFrame(active,lifecycle,persona,targetStick);equal=equal&&(frame==expected);}
   trackAlloc=false;const auto cachedRequests=allocationRequests,cachedBytes=allocationBytes;
   require(equal,"disabled frame never hides fresh Host/lifecycle/persona/targetStick facts");
   require(cachedRequests==0&&cachedBytes==0,"unchanged disabled frame performs no heap allocation");
   allocationRequests=allocationBytes=0;trackAlloc=true;bool originalEqual=true;
   for(int i=0;i<1000;++i){const auto frame=originalDisabledFrame(active,lifecycle,persona,targetStick);originalEqual=originalEqual&&(frame==expected);}
   trackAlloc=false;const auto originalRequests=allocationRequests,originalBytes=allocationBytes;
   require(originalEqual&&originalRequests>0&&originalBytes>0,"original disabled-frame construction is a real allocating control");
   inactiveFrameConstructionReceipts.push_back({{"calls",1000},{"hostActive",active},{"lifecycle",lifecycle},
    {"persona",persona},{"targetStick",targetStick},{"cachedAllocationRequests",cachedRequests},
    {"cachedAllocationBytes",cachedBytes},{"originalAllocationRequests",originalRequests},{"originalAllocationBytes",originalBytes}});
  }
  const auto request=g_virtualStartProtection.admit("elite",mockTick);require(request.accepted,"active request is real native object");(void)snap();
  allocationRequests=allocationBytes=0;trackAlloc=true;for(int i=0;i<8;++i)(void)virtualOutputRuntimeSnapshot(true,mockSourceWindow);trackAlloc=false;
  require(allocationRequests>0,"active request keeps fresh uncached transport deadline formatting");
  const auto current=snap();require(current["startProtection"]["locked"]==true&&current["startProtection"]["requestId"]==request.requestId,"cache cannot conceal an active request");
});

 test("source-window-crossed-topology",[]{g_xinputTopologyEpoch.fetch_add(1);mockSourceWindow=captureProduction();require(!mockSourceWindow.readContextStable,"real producer records crossed topology");const auto s=snap();require(s["state"]=="unknown"&&s["sourceLive"]==true&&s["sourceCurrent"]==false,"F3 crossed window cannot be green");sourceReceipt("F3-crossed-topology",s,"pending");});
 test("source-window-crossed-power",[]{++mockPower;mockSourceWindow=captureProduction();require(!mockSourceWindow.readContextStable,"real producer records crossed power");const auto s=snap();require(s["state"]=="unknown"&&s["reason"]=="physical-source-unconfirmed","crossed power stays uncertain");sourceReceipt("F3-crossed-power",s,"pending");});
 test("source-window-generation-changed-after-capture",[]{mockSourceWindow=captureProduction();require(mockSourceWindow.readContextStable,"completed read initially stable");g_xinputTopologyEpoch.fetch_add(1);auto s=snap();require(s["state"]=="unknown","new topology rejects previously stable window");sourceReceipt("topology-after-read",s,"pending");reset();mockSourceWindow=captureProduction();++mockPower;s=snap();require(s["state"]=="unknown","new power rejects previously stable window");sourceReceipt("power-after-read",s,"pending");});
 test("source-window-no-attempt-proof",[]{mockSourceWindow.readAttemptSequence=0;const auto s=snap();require(s["state"]=="unknown"&&s["sourceCurrent"]==false,"zero attempt cannot establish active");sourceReceipt("zero-attempt",s,"pending");});
 test("source-window-invalid-time-or-context",[]{const auto good=mockSourceWindow;for(unsigned fault=0;fault<7;++fault){mockSourceWindow=good;if(fault==0)mockSourceWindow.readStartedTick=0;if(fault==1)mockSourceWindow.readCompletedTick=0;if(fault==2)mockSourceWindow.readCompletedTick=mockReadStarted-1;if(fault==3)mockSourceWindow.readCompletedTick=mockTick+1;if(fault==4)mockSourceWindow.readContextStable=false;if(fault==5){mockSourceWindow.readPowerGeneration=0;mockPower=0;}if(fault==6)mockSourceWindow.readStartedTick=mockTick-1501;const auto s=snap();require(s["state"]=="unknown"&&s["sourceCurrent"]==false,"invalid window remains uncertain");sourceReceipt((std::string("invalid-window-")+std::to_string(fault)).c_str(),s,"pending");mockPower=7;}});
 test("source-window-age-bound-and-recovery",[]{mockSourceWindow.readStartedTick=mockTick-ymcc::kVirtualOutputSourceWindowMaxAgeMs;require(snap()["state"]=="active","freshness boundary is inclusive");--mockSourceWindow.readStartedTick;auto s=snap();require(s["state"]=="unknown","overlong read window cannot stay green");sourceReceipt("read-window-too-old",s,"pending");mockReadStarted=mockTick-1;mockSourceWindow=captureProduction();s=snap();require(s["state"]=="active"&&s["sourceCurrent"]==true,"new current canonical read restores active");sourceReceipt("fresh-read-restores-active",s,"on");});
 test("source-window-stationary-real-attempts-valid",[]{const unsigned packetNumber=55;mockSourceWindow=captureProduction();const auto firstSequence=mockSourceWindow.readAttemptSequence;require(snap()["state"]=="active","stationary first canonical read valid");mockTick+=50;mockReadStarted=mockTick-1;mockSourceWindow=captureProduction();const auto s=snap();require(packetNumber==55&&mockSourceWindow.readAttemptSequence!=firstSequence,"real attempts advance independently of constant packet number");require(s["state"]=="active","stationary second read stays valid");sourceReceipt("stationary-read",s,"on");});
 test("source-window-presentation-delayed",[]{g_inputHostMx.afterTryLock=[]{mockTick+=ymcc::kVirtualOutputSourceWindowMaxAgeMs+1;};const auto s=snap();g_inputHostMx.afterTryLock={};require(s["state"]=="unknown","freshness uses decision time after delayed observation");sourceReceipt("delayed-presentation",s,"pending");});
 test("source-window-expired-completion",[]{mockTick+=ymcc::kVirtualOutputSourceWindowMaxAgeMs+1;const auto s=snap();require(s["state"]=="unknown"&&s["sourceCurrent"]==false,"cached completion past freshness bound cannot be green");sourceReceipt("expired-completion",s,"pending");});
 test("source-window-disconnected-distinction",[]{auto s=snap(false);require(s["reason"]=="physical-source-unavailable","current confirmed absence keeps its real meaning");sourceReceipt("confirmed-source-absence",s,"pending");mockSourceWindow.readContextStable=false;s=snap(false);require(s["state"]=="unknown","unstable absence does not invent a terminal failure");sourceReceipt("uncertain-source-absence",s,"pending");});
 test("source-window-explicit-faults-stay-visible",[]{for(unsigned fault=0;fault<7;++fault){reset();mockSourceWindow.readContextStable=false;const char* reason="";if(fault==0){g_inputHostTransportFault=true;reason="transport-fault";}if(fault==1){g_inputHostFaultHoldsStart.store(true);reason="backend-fault-hold";}if(fault==2){g_inputHostRecoveryFallbackState.store(3);reason="physical-fallback";}if(fault==3){mockAlive=false;reason="host-not-alive";}if(fault==4){mockBound="steamdeck";reason="persona-mismatch";}if(fault==5){g_inputHostPrepared=false;reason="target-not-admitted";}if(fault==6){g_virtualOutputSuppression={42,mockTick,2};reason="suppression-degraded";}const auto s=snap();require(s["state"]=="suspected-failed"&&s["reason"]==reason,"explicit fault precedes source uncertainty");sourceReceipt(reason,s,"warning");}});
 test("source-window-metadata-does-not-defeat-json-dedupe",[]{mockSourceWindow=captureProduction();const auto first=snap();const auto oldSequence=mockSourceWindow.readAttemptSequence;mockTick+=50;mockReadStarted=mockTick-1;mockSourceWindow=captureProduction();const auto next=snap();require(mockSourceWindow.readAttemptSequence!=oldSequence,"new canonical attempt captured");require(first==next,"valid sequence/time advances keep identical status JSON");require(!next.contains("readAttemptSequence")&&!next.contains("readStartedTick")&&!next.contains("readCompletedTick")&&!next.contains("sourceWindow"),"per-read metadata never published");});
 test("retired-PS-return-is-visible-without-MSI",[]{configureProduction("disabled",false);dead();g_psEndpointOwnership.hello(json{{"psEndpointOwnershipVersion",1}},ymcc::psEndpointOwnership::Expected{"host","run","target","dualsense-edge",42});g_psEndpointOwnership.captureRetirement(true);const auto pending=snap();require(pending["physicalReturnPending"]==true&&pending["state"]=="recovering","actual non-MSI PS return ownership publishes pending return rather than completed OFF");g_psEndpointOwnership.confirmRetirement("run",42);const auto complete=snap();require(complete["physicalReturnPending"]==false&&complete["state"]=="disabled","completed PS return releases the pending presentation without touching MSI policy");});
 test("external-priority-publishes-protection-contract",[]{g_externalPriorityVirtualSuspended.store(true);const auto value=snap();require(value["state"]=="external-priority","real external-priority verdict remains unchanged");protectionEdgeProduction(value);require(value["startProtection"]["maxMs"]==30000,"actual feed consumes the real protection snapshot during external takeover");
  // Change the actual producer transport one field at a time: no lifecycle admission is synthesized.
  auto feed=value;auto& protection=feed["startProtection"];auto count=lifecycleNotes.size();
  protection["requestId"]=101ULL;protection["phase"]="pending";protection["locked"]=true;protectionEdgeProduction(feed);require(lifecycleNotes.size()==++count&&lifecycleNotes.back()["fields"]==protection,"first actual edge logs the complete protection payload");
  protection["requestId"]=102ULL;protectionEdgeProduction(feed);require(lifecycleNotes.size()==++count,"request-only change is an edge");
  protection["phase"]="verifying";protectionEdgeProduction(feed);require(lifecycleNotes.size()==++count,"phase-only change is an edge");
  protection["locked"]=false;protectionEdgeProduction(feed);require(lifecycleNotes.size()==++count,"lock-only change is an edge");
  protection["remainingMs"]=11;protection["cycleElapsedMs"]=999;protectionEdgeProduction(feed);require(lifecycleNotes.size()==count,"countdown-only changes never log an edge");
  protection["requestId"]=0ULL;protectionEdgeProduction(feed);protectionEdgeProduction(feed);require(lifecycleNotes.size()==count,"zero request transitions update dedupe without a log");
  protection["requestId"]=102ULL;protectionEdgeProduction(feed);require(lifecycleNotes.size()==++count,"same prior request after zero is a new edge");
  protection["requestId"]=103ULL;throwNextLifecycleLog=true;protectionEdgeProduction(feed);require(lifecycleNotes.size()==++count&&!throwNextLifecycleLog,"log port failure is contained");protectionEdgeProduction(feed);require(lifecycleNotes.size()==count,"failed log edge is not retried on each sample");
 });
 test("physical-steamdeck-self-output-publishes-protection-contract",[]{g_machineIdentity.resolved=true;g_machineIdentity.family=YmccFamily::ValveSteamDeck;configureProduction("steamdeck",true);const auto value=snap();require(value["state"]=="disabled","physical SteamDeck self-emulation remains blocked");protectionEdgeProduction(value);require(value["startProtection"]["settleMs"]==10000,"actual feed consumes the real protection snapshot for a blocked persona");});
 for(const char* outcome:{"success","failure"})test((std::string("completed-lifecycle-status-stable-")+outcome).c_str(),[&]{
  const auto request=g_virtualStartProtection.admit("elite",mockTick);require(request.accepted,"actual R3 request admitted");
  require(g_virtualStartProtection.beginVerification(request.requestId,outcome,mockTick),"actual R3 execution candidate starts inspection");mockTick+=10000;
  require(g_virtualStartProtection.finishVerification(request.requestId,outcome,mockTick),"actual R3 verifier finalizes after its unchanged inspection");
  mockReadStarted=mockTick-1;mockSourceWindow=captureProduction();const auto first=snap();unsigned changed=0;auto last=first;
  require(first["startProtection"]["cycleElapsedMs"]==0&&first["startProtection"]["elapsedMs"]==0&&first["startProtection"]["remainingMs"]==0,"finalized presentation clocks are zero before reuse");
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
 if(!std::string(${JSON.stringify(selectedCase??'')}).empty()&&cases.size()!=${selectedCases.length})++failed;
 std::cout<<json({{"passed",failed==0},{"protectionEdgeReceipts",protectionEdgeReceipts},{"queueBoundaryReceipts",queueBoundaryReceipts},{"cases",cases},{"statusConstructionReceipts",statusConstructionReceipts},{"inactiveFrameConstructionReceipts",inactiveFrameConstructionReceipts},{"presentationReceipts",presentationReceipts},{"statusChurnReceipts",statusChurnReceipts},{"tests",cases.size()},{"assertions",assertions},{"failed",failed},{"hardwareOperations",0},{"networkOperations",0}}).dump(2)<<std::endl;return failed?1:0;
}
`;
function command(name,exe,args,expected=0){const r=spawnSync(exe,args,{cwd:out,encoding:'utf8',timeout:60000,windowsHide:true});fs.writeFileSync(path.join(out,name+'.log'),[JSON.stringify({exe,args,status:r.status,error:r.error?.message}),r.stdout||'',r.stderr||''].join('\n'));assert(!r.error,name+' timed out');if(expected!==null)assert.equal(r.status,expected,name+' failed; see log');return r;}
const vcvars=process.env.YMCC_VCVARS64??'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
assert(fs.existsSync(vcvars),'MSVC x64 not found');
const results={profiles:{},mutations:{},hardwareOperations:0,networkOperations:0,selectedCase,selectedProfiles,selectedVariants:variants,sourceRoot:candidateRoot??repo,fullOriginalMatrix:!selectedCase&&variants.length===originalVariants.length&&variants.every((v,i)=>v===originalVariants[i])};
for(const profile of selectedProfiles){
 for(const variant of variants){
  let source=cpp;
  if(variant==='old-handoff'){const old=`    if (!g_inputHostLifecycleWorkRequested.exchange(false, std::memory_order_acq_rel)) return;\n    if (g_inputHostLifecycleWorkInFlight.exchange(true, std::memory_order_acq_rel)) {\n        g_inputHostLifecycleWorkRequested.store(true, std::memory_order_release);\n        return;\n    }\n    g_inputHostPresentationWorkGeneration.fetch_add(1, std::memory_order_acq_rel);\n`;assert(source.includes(claim));source=source.replace(claim,old);}
  if(variant==='omit-deferred-pending'){assert(source.includes('|| admissionDeferred)'));source=source.replace('|| admissionDeferred)','|| (admissionDeferred && false))');}
  if(variant==='ignore-source-window'){assert(source.includes(sourceCheck));source=source.replace(sourceCheck,'    facts.sourceCurrent = true; (void)sourceWindow;\n');}
  if(variant==='raw-cycle-clock'){assert(source.includes('{"cycleElapsedMs",view.locked ? (view.elapsedMs/1000)*1000 : 0}'));source=source.replace('{"cycleElapsedMs",view.locked ? (view.elapsedMs/1000)*1000 : 0}','{"cycleElapsedMs",view.elapsedMs}');}
  if(variant==='omit-early-protection'){const line='            {"startProtection",inputHostStartProtectionSnapshot()},';assert(source.split(line).length===3);source=source.replaceAll(line,'');}
  if(variant==='raw-inspection-clock'){const from='{"verificationRemainingMs",view.locked ? ((view.verificationRemainingMs+999)/1000)*1000 : 0}';assert(source.includes(from));source=source.replace(from,'{"verificationRemainingMs",view.verificationRemainingMs}');}
  if(variant==='raw-publication-equality'){assert(source.includes(publicationCompare));source=source.replace(publicationCompare,'(virtualOutput == publishedVirtualOutput)');}
  if(variant==='omit-ps-return-status'){const from='(!facts.enabled && current && previous.psReturnPending)';assert(source.includes(from));source=source.replace(from,'(false && !facts.enabled && current && previous.psReturnPending)');}
  if(variant==='cache-ignores-source'){const from='    if (!protectionView.locked && previousKey && *previousKey == key) return snapshot;';assert(source.includes(from));source=source.replace(from,'    key.facts.sourceCurrent = true;\n'+from);}
  if(variant==='cache-ignores-protection'){const from='    if (!protectionView.locked && previousKey && *previousKey == key) return snapshot;';assert(source.includes(from));source=source.replace(from,'    key.protection = ymcc::VirtualStartProtection::View{};\n'+from);}
   if(variant==='retain-rejected-wait'){const from='                    inputHostPresentationDeferred(admissionStatusRequest, admissionStatusContext, false);';assert(source.includes(from));source=source.replace(from,'                    if(queuedStart)inputHostPresentationDeferred(admissionStatusRequest, admissionStatusContext, false);');}
   if(variant==='clear-wrong-context'){const from='else g_inputHostAdmissionStatus.clear(request, context);';assert(source.includes(from));source=source.replace(from,'else g_inputHostAdmissionStatus.clear(request);');}
  if(variant.startsWith('edge-ignores-')){const field=variant==='edge-ignores-request'?'0':variant==='edge-ignores-phase'?'1':'2';const names={'0':'protectionRequest','1':'protectionPhase','2':'protectionLock'};const term='publishedStartProtectionEdge['+field+'] != '+names[field];assert(source.includes(term));source=source.replace(term,'((void)'+names[field]+', false)');}
  if(variant==='inactive-frame-cache-off'){
   const signature='static const json& inputCaptureInactiveHostFrame(';
   const condition='if (!cache.ready || cache.key.hostActive != hostActive ||';
   assert(source.includes(signature)&&source.includes(condition));
   source=source.replace(signature,'static bool mockDisableInactiveFrameReuse=true;\n'+signature);
   source=source.replace(condition,'if (mockDisableInactiveFrameReuse || !cache.ready || cache.key.hostActive != hostActive ||');
  }
  if(variant==='inactive-frame-ignore-persona'){const term='cache.key.persona != persona';assert(source.includes(term));source=source.replace(term,'false');}
  const name=profile+'-'+variant;const sourcePath=path.join(out,name+'.cpp'),exe=path.join(out,name+'.exe');fs.writeFileSync(sourcePath,source);
  const opts=['/nologo','/std:c++20','/utf-8','/EHsc','/W4','/WX','/permissive-','/GF','/MT',...(profile==='Release'?['/O2','/DNDEBUG']:['/Od','/RTC1']),'/I"'+path.join(frozen,'native')+'"','/I"'+path.join(frozen,'deps/json')+'"','"'+sourcePath+'"','/Fo"'+path.join(out,name+'.obj')+'"','/Fe"'+exe+'"'];
  const batch=path.join(out,name+'.cmd');fs.writeFileSync(batch,calibrationToolchainPreamble(vcvars)+'cl '+opts.join(' ')+'\r\nexit /b %errorlevel%\r\n');
  command(name+'-build','cmd.exe',['/d','/c',batch]);const r=command(name+'-execute',exe,[],null);const report=JSON.parse(r.stdout);fs.writeFileSync(path.join(out,name+'-results.json'),JSON.stringify(report,null,2)+'\n');
  if(variant==='fixed'){results.profiles[profile]={passed:r.status===0&&report.passed,failedCases:report.cases.filter(c=>!c.passed),tests:report.tests,assertions:report.assertions,exeSha256:sha(fs.readFileSync(exe)),statusConstructionReceipts:report.statusConstructionReceipts,inactiveFrameConstructionReceipts:report.inactiveFrameConstructionReceipts};}
  else{const requiredCase=variant==='old-handoff'?'claim-handoff':variant==='ignore-source-window'?'source-window-crossed-topology':variant==='raw-cycle-clock'?'completed-lifecycle-status-stable-success':variant==='omit-early-protection'?'external-priority-publishes-protection-contract':variant==='raw-inspection-clock'?'active-lifecycle-publication-bounded-verifying':variant==='raw-publication-equality'?'publication-expiry-only-ignored-with-current-valid-lock':variant==='omit-ps-return-status'?'retired-PS-return-is-visible-without-MSI':variant==='cache-ignores-source'?'source-window-age-bound-and-recovery':variant==='cache-ignores-protection'?'completed-lifecycle-status-stable-failure':variant==='retain-rejected-wait'?'rejected-queue-does-not-retain-wait':variant==='clear-wrong-context'?'new-attempt-failure-without-new-host-epoch':variant.startsWith('inactive-frame-')?'steady-status-no-extra-host-blocking-or-publication':variant.startsWith('edge-ignores-')?'external-priority-publishes-protection-contract':'deferred-source';assert(r.status!==0&&report.cases.some(c=>c.id===requiredCase&&!c.passed),'mutation not killed: '+name);results.mutations[name]={killed:true,failedCases:report.cases.filter(c=>!c.passed).map(c=>c.id)};}
  console.log(name+': '+(variant==='fixed'?(report.passed?'PASS ':'FAIL ')+report.tests+'/'+report.assertions:'MUTATION_KILLED'));
 }
}
if(!selectedCase&&variants.includes('fixed')&&selectedProfiles.includes('Release')){
const require=createRequire(path.join(repo,'package.json'));const esbuild=require('esbuild');const frontend=path.join(out,'virtual_output_status_selftest.mjs');esbuild.buildSync({entryPoints:[path.join(frozen,'tools/virtual_output_status_selftest.ts')],bundle:true,platform:'node',format:'esm',outfile:frontend,logLevel:'silent'});const front=command('frontend-existing',process.execPath,[frontend]);results.frontend=front.stdout.trim();

results.sourceWindowUi=runSourceWindowUi(frozen,path.join(out,'Release-fixed-results.json'));
}else{results.frontend='NOT_RUN_EXPLICIT_SELECTION';results.sourceWindowUi={passed:null,assertions:0,views:[],status:'NOT_RUN_EXPLICIT_SELECTION'};}
results.sourceChangedDuringRun=manifest.filter(x=>sha(fs.readFileSync(x.sourceFile??path.join(repo,x.file)))!==x.sha256).map(x=>x.file);results.passed=Object.keys(results.profiles).length===(variants.includes('fixed')?selectedProfiles.length:0)&&Object.values(results.profiles).every(p=>p.passed)&&Object.keys(results.mutations).length===variants.filter(v=>v!=='fixed').length*selectedProfiles.length&&results.sourceChangedDuringRun.length===0;results.completedAt=new Date().toISOString();
fs.writeFileSync(path.join(out,'RESULT.json'),JSON.stringify(results,null,2)+'\n');console.log(JSON.stringify({...results,sourceWindowUi:{passed:results.sourceWindowUi.passed,assertions:results.sourceWindowUi.assertions,views:results.sourceWindowUi.views.length}},null,2));process.exitCode=results.passed?0:1;
