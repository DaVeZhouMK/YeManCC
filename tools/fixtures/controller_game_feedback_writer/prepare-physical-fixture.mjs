import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import crypto from 'node:crypto';
import {extract as extractMember} from '../legion_motion_consumer/extract_main.mjs';
import {createRequire} from 'node:module';
import {FIXTURE,HEADERS,need,hash,write,once} from './contract.mjs';
const requireContext=createRequire(import.meta.url);
export function preparePhysicalFixture({sourceFile,out,resolveInput}){
 need(!fs.existsSync(out),'FRESH_PHYSICAL_GENERATION_REQUIRED');fs.mkdirSync(out,{recursive:true});
// Load the declared frozen support module; do not capture its live repository copy.
const contextFile=resolveInput('tools/fixtures/xinput_identity_feedback_context.mjs');
const {currentFeedbackSupport,CURRENT_GAME_FUNCTIONS}=requireContext(contextFile);
need(typeof currentFeedbackSupport==='function'&&Array.isArray(CURRENT_GAME_FUNCTIONS),'ACTUAL_GAME_CONTEXT_EXPORTS_REQUIRED');
const sha=b=>crypto.createHash('sha256').update(b).digest('hex'),producer=resolveInput('tools/xinput_identity_feedback_lifecycle_selftest.mjs'),driver=fs.readFileSync(producer,'utf8'),a=driver.indexOf('function extract(text,file){'),b=driver.indexOf('\nconst current=extract(',a);const extract=new Function('assert','sha','currentFeedbackSupport','return ('+driver.slice(a,b).trim()+');')(assert,sha,currentFeedbackSupport),bytes=fs.readFileSync(sourceFile),source=bytes.toString('utf8'),actual=extract(source,sourceFile),extra=[];
const fn=name=>{const r=extractMember(source,name);extra.push(r);return r.code;},type=name=>{const r=extractMember(source,name,'struct');extra.push(r);return r.code;};
// Older captured extractors predate these slots. Fill them with the same actual
// production declarations and functions; never substitute empty context bodies.
if(!actual.parts.GAME_COMMAND&&!actual.parts.GAME_DISPATCH)Object.assign(actual.parts,currentFeedbackSupport(source,fn,actual.record));
need(actual.parts.GAME_COMMAND&&actual.parts.GAME_DISPATCH,'ACTUAL_GAME_CONTEXT_PARTS_REQUIRED');
let template=fs.readFileSync(resolveInput('tools/fixtures/xinput_identity_feedback_lifecycle_fixture.cpp'),'utf8');
const prelude=`
namespace ymcc::legion_motion {struct Sample;}
namespace ymcc::windows_motion {struct Stamp;}
struct XINPUT_GAMEPAD{WORD wButtons=0;BYTE bLeftTrigger=0,bRightTrigger=0;short sThumbLX=0,sThumbLY=0,sThumbRX=0,sThumbRY=0;};
${type('InputCaptureGamepadSnapshot')}
${type('InputFrameSubmission')}
${type('MsiCanonicalReadProof')}
static InputCaptureGamepadSnapshot g_inputCaptureGamepadSnapshot;
static thread_local InputFrameSubmission* t_inputFrameSubmission=nullptr;
static std::atomic<bool> g_inputReady{true},g_inputHostPowerAdmissionRevoked{false},g_inputHostFaultHoldsStart{false};
static std::atomic<unsigned long long> g_inputHostVirtualIntentRevision{1},g_msiMainSourceRevision{11},g_inputHostGameMotorBindingGeneration{1};
static std::atomic<unsigned> g_inputHostVirtualIntentWriters{0};
static unsigned long long g_inputHostEpoch=17;
// The original physical template reset uses this environment name. Share its epoch.
static auto& fixtureInputHostEpoch=g_inputHostEpoch;
static auto inputHostEpochToken(){return g_inputHostEpoch;}
static auto inputHostProtocolPowerGenerationLocked(){return currentPowerGeneration();}
static ymcc::game_feedback::Inbox g_inputHostGameMotorInbox;
static bool nativeGameFeedbackCommandCurrent(const ymcc::game_feedback::SourceBoundCommand&) noexcept;

static void gamepadSerialGameManualStarted(unsigned long long);
static void gamepadSerialGameOwnerFinished(unsigned long long);
static void gamepadSerialRevokeGameFeedback();
static void nativeSubmitGameFeedbackFrame(const InputFrameSubmission&,bool);

`;
// This combined writer owns enabled tickets/Inbox/worker state. Place that same
// environment before the actual command gates, instead of the standalone memory leaf.
template=once(template,'#include "xinput_identity_feedback_game_memory.inc"',prelude,'writer-enabled-game-context');
template=template.replace('static void gamepadSerialStartLocked(){}',`static std::optional<ymcc::touchhaptic::Dispatch::Ticket> g_gamepadSerialHaptic;static std::deque<std::function<void()>> g_gamepadSerialQ;static std::thread g_gamepadSerialThread;static bool g_gamepadSerialStarted=false;
using HRESULT=int;static constexpr int COINIT_MULTITHREADED=0;static int CoInitializeEx(void*,int){return 0;}static bool SUCCEEDED(int value){return value>=0;}static void CoUninitialize(){}static void ymccLogThreadRole(const char*){}
static void nativeRunTouchHapticTicket(const ymcc::touchhaptic::Dispatch::Ticket&,bool){}
static void nativeRunGameFeedbackWork(const ymcc::game_feedback::Dispatch::Work&);
static void physicalFeedbackFinish(unsigned long long);
static void gamepadSerialStartLocked();`);
template=template.replace('static void appendNativeLifecycleLog(const char* event,const json& row){','static void appendNativeLifecycleLog(const char* event,const json& row){if(fixtureThrowMsiFeedbackLog&&std::string(event)=="msi-dinput-feedback")throw std::runtime_error("fixture-msi-log-loss");');
let code='static bool fixtureThrowMsiFeedbackLog=false;\n#define YMCC_UNIFIED_PHYSICAL_FEEDBACK 1\n#define YMCC_CURRENT_GAME_FEEDBACK_CONTEXT 1\n#include <deque>\n#include "game_feedback_diagnostics.h"\n#include "game_feedback_dispatch.h"\n#include "game_feedback_source_ticket.h"\n#include "msi_virtual_report_diagnostic.h"\n#include "HcControllerInputCapabilityCatalog.h"\n#include "HcControllerFeedbackCapabilityCatalog.h"\n'+template;
for(const[n,body]of Object.entries(actual.parts)){const key='@@'+n+'@@';if(!code.includes(key))throw Error('Missing fixture part '+n);code=code.replace(key,()=>body);}if(/@@\w+@@/.test(code))throw Error('Unfilled template: '+[...code.matchAll(/@@(\w+)@@/g)].map(m=>m[1]).join(','));code=code.replace('int main(){','int OriginalFeedbackMain(){');
const helperNames=['inputCaptureStampFeedbackSourceLocked','inputHostFrameSubmissionSample','inputHostConsumeGameMotorReplyLocked','msiDinputApplyCanonicalReadProof','nativeGameFeedbackReadMatchesToken','nativeGameFeedbackFrameCurrent','nativeGameFeedbackCommandCurrent','nativeBindGameFeedbackFrameCommand','nativeWriteBoundGameFeedbackCommand','nativeSubmitGameFeedbackFrame','nativeRunGameFeedbackWork','gamepadSerialRevokeGameFeedback','gamepadSerialGameManualStarted','gamepadSerialGameOwnerFinished','gamepadSerialStartLocked','nativeGameFeedbackDiagnosticsSnapshot'];code+='\n'+type('GameFeedbackFrameDispatchScope')+'\n'+helperNames.filter(name=>!CURRENT_GAME_FUNCTIONS.includes(name)).map(fn).join('\n');
const publishStart=source.indexOf('        g_inputCaptureGamepadSnapshot = {live, pad, g_xinputPrimarySlot, admittedMask, inputSource};'),publishEnd=source.indexOf('        inputCaptureStampFeedbackSourceLocked(g_inputCaptureGamepadSnapshot);',publishStart)+'        inputCaptureStampFeedbackSourceLocked(g_inputCaptureGamepadSnapshot);'.length;if(publishStart<0||publishEnd<publishStart)throw Error('Actual publisher span');const publication=source.slice(publishStart,publishEnd);extra.push({name:'actual gamepad read publication through feedback stamp',code:publication});
const captureStart=source.indexOf('    frameSubmission.sample=inputHostFrameSubmissionSample(t_commonFrameSource);'),captureEnd=source.indexOf('\n',source.indexOf('    frameSubmission.feedbackSource=',captureStart));if(captureStart<0||captureEnd<captureStart)throw Error('Actual frame copy');const capture=source.slice(captureStart,captureEnd);extra.push({name:'actual source snapshot to frame ticket copy',code:capture});
code+=`\nstatic InputCaptureGamepadSnapshot fixturePublishSource(const std::string& inputSource,bool live){\n std::lock_guard<std::mutex> readLock(g_gamepadReadMx);static unsigned long long diagnosticReadSequence=0;const XINPUT_GAMEPAD pad{};const auto admittedMask=g_xinputAdmittedMask;const auto probeNow=GetTickCount64(),diagnosticReadPower=currentPowerGeneration();const auto diagnosticReadTopology=g_xinputTopologyEpoch.load(),diagnosticReadIdentity=g_xinputSlotIdentityEpoch.load();const auto diagnosticReadMsiSourceRevision=g_msiMainSourceRevision.load();const MsiCanonicalReadProof msiCanonicalReadProof{77,probeNow,probeNow,diagnosticReadPower,g_msiFeedbackModeGeneration.load(),diagnosticReadMsiSourceRevision,diagnosticReadTopology};\n${publication}\n return g_inputCaptureGamepadSnapshot;\n}\nstatic void fixtureCopyFrameSource(InputFrameSubmission& frameSubmission,const InputCaptureGamepadSnapshot& sample){const auto* t_commonFrameSource=&sample;\n${capture}\n}\n`;
code+='\n'+fs.readFileSync(resolveInput(FIXTURE+'/PhysicalTestSupport.inc'),'utf8');fs.writeFileSync(out+'/fixture.cpp',code);

 const inputs=[sourceFile,producer,contextFile,resolveInput('tools/fixtures/xinput_identity_feedback_lifecycle_fixture.cpp'),resolveInput(FIXTURE+'/PhysicalTestSupport.inc'),...HEADERS.map(resolveInput)].map(file=>({file,sha256:hash(file)}));
 write(path.join(out,'source-ranges.json'),{inputs,ranges:[...actual.ranges.map(({code,...r})=>r),...extra.map(({code,...r})=>({...r,sha256:sha(code)}))],sourceExactPhysicalConsumers:true,productSourceModified:false,gameFeedbackContext:{sourceFile:contextFile,sha256:hash(contextFile),functions:CURRENT_GAME_FUNCTIONS,slots:['GAME_COMMAND','GAME_DISPATCH'],environmentMemoryLeafUsed:false,writerEnvironment:'existing-enabled-frame-inbox-and-serial-worker'}});
 return{file:path.join(out,'fixture.cpp'),sourceFile,sourceSha256:hash(sourceFile),rangeFile:path.join(out,'source-ranges.json')};
}
