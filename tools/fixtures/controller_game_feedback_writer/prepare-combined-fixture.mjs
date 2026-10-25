import fs from 'node:fs';import path from 'node:path';
import {closeBrace} from '../hc_remaining_model_capabilities/expectations.mjs';
import {MOTOR_FIXTURE,need,hash,write} from './contract.mjs';
import {preparePhysicalFixture} from './prepare-physical-fixture.mjs';
import {refinePhysicalBridge} from './refine-physical-bridge.mjs';
import {lifecycleContext} from '../controller_game_motor_pipe/lifecycle-context.mjs';
import {targetReturnContext} from '../controller_game_motor_pipe/target-return-context.mjs';
export function prepareCombinedFixture({sourceFile,out,sourceSet}){
 need(!fs.existsSync(out),'FRESH_COMBINED_GENERATION_REQUIRED');fs.mkdirSync(out,{recursive:true});
 const resolveInput=relative=>sourceSet.resolve(relative);
 const physicalBase=preparePhysicalFixture({sourceFile,out:path.join(out,'physical-base'),resolveInput});
 need(physicalBase.sourceSha256===hash(sourceFile),'PHYSICAL_AND_PIPE_FULL_SOURCE_IDENTITY_DIFFER');
 let physical=fs.readFileSync(physicalBase.file,'utf8');
const old=physical.indexOf('int OriginalFeedbackMain(){');if(old<0)throw Error('Original physical test entry');const oldEnd=closeBrace(physical,physical.indexOf('{',old));physical=physical.slice(0,old)+physical.slice(oldEnd+1);
const includes=[...new Set(Array.from(physical.matchAll(/^#include (.+)$/gm),m=>m[1]).filter(x=>x!=='"xinput_feedback_msi_platform.inc"'))];physical=physical.replace(/^#include (?!"xinput_feedback_msi_platform\.inc").+\r?\n/gm,'');physical=physical.replace(/^namespace ymcc::(?:legion_motion|windows_motion) \{struct (?:Sample|Stamp);\}\r?\n/gm,'');
physical=physical.replace('static ULONGLONG GetTickCount64(){return fakeTick.load();}','static ULONGLONG GetTickCount64(){return ::GetTickCount64();}');
const platform=fs.readFileSync(resolveInput('tools/fixtures/xinput_feedback_msi_platform.inc'),'utf8');const macros=[...new Set(['ERROR_SUCCESS','INVALID_HANDLE_VALUE','SUCCEEDED',...Array.from(platform.matchAll(/\b([A-Z][A-Z0-9_]*)\s*=/g),m=>m[1])])];
const prefix='#define WIN32_LEAN_AND_MEAN\n#define NOMINMAX\n#include <windows.h>\n#include <sddl.h>\n#include <wincrypt.h>\n#include <objbase.h>\n'+includes.map(i=>'#include '+i).join('\n')+'\nnamespace ymcc::legion_motion {struct Sample{};}\nnamespace ymcc::windows_motion {struct Stamp{};}\n';
const push=macros.map(m=>'#pragma push_macro("'+m+'")\n#undef '+m).join('\n'),pop=macros.toReversed().map(m=>'#pragma pop_macro("'+m+'")').join('\n');const wrapped=prefix+'\n'+push+'\nnamespace PhysicalMemory {\n'+physical+'\n}\n'+pop+'\n';fs.writeFileSync(path.join(out,'PhysicalMemory.inc'),wrapped);
let pipe=fs.readFileSync(resolveInput(MOTOR_FIXTURE+'/NativeEntry.cpp'),'utf8').replaceAll('\r\n','\n');
function once(a,b){if(pipe.split(a).length!==2)throw Error('Pipe bridge anchor '+a.slice(0,100));pipe=pipe.replace(a,b);}
once('using json=nlohmann::json;','@@PHYSICAL_MEMORY@@\nusing json=nlohmann::json;');
// The maintained request-only fixture declares an empty serial mailbox for its
// real revoke consumer. This combined fixture shares the full actual mailbox
// and actual revoke implementation already extracted in PhysicalMemory.
const emptyMailbox='// Real revoke consumer with an owned empty serial mailbox; no physical writer in this request/receipt fixture.\n'+
 'static std::atomic<unsigned long long> g_inputHostGameMotorBindingGeneration{0};\n'+
 'static std::mutex g_gamepadSerialMx;\nstatic std::condition_variable g_gamepadSerialCv;\n'+
 'static ymcc::game_feedback::Dispatch g_gamepadSerialGameFeedback;\n'+
 'static std::atomic<bool> g_gamepadSerialGameInterested{false};\n@@GAME_REVOKE@@\n';
if(pipe.includes('@@GAME_REVOKE@@'))once(emptyMailbox,'');

once('static ymcc::game_feedback::Inbox g_inputHostGameMotorInbox;','using PhysicalMemory::g_inputHostGameMotorInbox;\nusing PhysicalMemory::g_inputHostGameMotorBindingGeneration;\nusing PhysicalMemory::gamepadSerialRevokeGameFeedback;');
once('static unsigned long long g_inputHostEpoch=17,g_inputHostSequence=0,g_inputHostConfigRevision=1;','using PhysicalMemory::g_inputHostEpoch;\nstatic unsigned long long g_inputHostSequence=0,g_inputHostConfigRevision=1;');
once('static std::atomic<int> g_backendInstallPrompt{0};static unsigned g_xinputConnectedMask=0;','static std::atomic<int> g_backendInstallPrompt{0};using PhysicalMemory::g_xinputConnectedMask;');
once('namespace ymcc::legion_motion {struct Sample{};}\nnamespace ymcc::windows_motion {struct Stamp{};}','');
once('static std::atomic<bool> g_inputReady{true},g_inputHostPowerAdmissionRevoked{false},g_inputHostFaultHoldsStart{false};','using PhysicalMemory::g_inputReady;using PhysicalMemory::g_inputHostPowerAdmissionRevoked;using PhysicalMemory::g_inputHostFaultHoldsStart;');
once('static std::atomic<unsigned long long> g_inputHostVirtualIntentRevision{1},g_msiMainSourceRevision{1},g_msiFeedbackModeGeneration{1};','using PhysicalMemory::g_inputHostVirtualIntentRevision;using PhysicalMemory::g_msiMainSourceRevision;using PhysicalMemory::g_msiFeedbackModeGeneration;');
once('static std::atomic<unsigned> g_xinputTopologyEpoch{1},g_xinputSlotIdentityEpoch{1},g_inputHostVirtualIntentWriters{0};','using PhysicalMemory::g_xinputTopologyEpoch;using PhysicalMemory::g_xinputSlotIdentityEpoch;using PhysicalMemory::g_inputHostVirtualIntentWriters;');
once('static thread_local InputFrameSubmission* t_inputFrameSubmission=nullptr;','using PhysicalMemory::t_inputFrameSubmission;');
once('@@CAPTURE_TYPE@@','using PhysicalMemory::InputFrameSubmission;');
// Actual Current now reads the two settings gates. Supply only this controlled
// environment POD when the shared request fixture has not declared it yet.
// If that independently maintained fixture already supplies it, reuse its state.
const settingsDeclaration='static struct { bool enabled=true,virtualTargetEnabled=true; } g_realStickTest;';
if(pipe.includes(settingsDeclaration))once(settingsDeclaration,'struct FixtureRealStickAdmission {bool enabled=true;bool virtualTargetEnabled=true;std::string virtualTargetPersona;};\nstatic FixtureRealStickAdmission g_realStickTest;');
const fixtureHasSettings= /\bg_realStickTest\b/.test(pipe);
if(!fixtureHasSettings)once('@@FRAME_ADMISSION@@','struct FixtureRealStickAdmission {bool enabled=true;bool virtualTargetEnabled=true;std::string virtualTargetPersona;};\nstatic FixtureRealStickAdmission g_realStickTest;\n@@FRAME_ADMISSION@@');
once('  g_inputHostPrepared=g_inputHostNeutralized=false;','  g_realStickTest.enabled=true;g_realStickTest.virtualTargetEnabled=true;\n  g_inputHostPrepared=g_inputHostNeutralized=false;');
const lifecycle=lifecycleContext({sourceFile,resolveInput,startProtection:true});
once('@@FRAME_ADMISSION@@',lifecycle.code+'\n@@PERSONA_MATCH@@\n@@FRAME_ADMISSION@@');
once('fixturePersona=selected;','fixturePersona=selected;fixtureSetMatchingPersona(selected);');

const targetReturn=targetReturnContext({sourceFile,resolveInput});
once('@@REQUEST@@',targetReturn.code+'\n@@REQUEST@@\n@@DIAGNOSTIC_CONSUMER@@');
once('static void closeCommands(){','static void closeCommands(){\n inputHostLogSubmitStats("combined-writer-end");');
once('static auto currentPowerGeneration(){return 3ULL;}','static auto currentPowerGeneration(){return PhysicalMemory::currentPowerGeneration();}');
once('#include "NativeMotorPipeTests.cpp"','@@COMBINED_TESTS@@');

 fs.writeFileSync(path.join(out,'NativeCombined.template.cpp'),pipe,{flag:'wx'});
 refinePhysicalBridge(path.join(out,'PhysicalMemory.inc'));
 write(path.join(out,'namespace-bridge.json'),{
  status:'SOURCE_EXACT_SHARED_FRAME_PHYSICAL_NAMESPACE_BRIDGE',mode:sourceSet.mode,
  fullMainSha256:hash(sourceFile),sourceFile,targetReturn:{inputs:targetReturn.inputs,bindings:targetReturn.bindings,boundary:targetReturn.boundary},lifecycle:{inputs:lifecycle.inputs,bindings:lifecycle.bindings,boundaries:lifecycle.boundaries},physicalFixture:physicalBase.file,physicalRanges:physicalBase.rangeFile,
  transformations:['Separate inert hardware API symbols from actual Windows pipe API in PhysicalMemory namespace','Alias same frame/Inbox/source/power state into real native request; no second JSON conversion','Actual write/stop RAII and snapshot from physical source; actual low-frequency logger and request wrapper at existing close boundary','Only the declared fake monotonic endpoint uses Windows tick for cross-pipe freshness','ASCII-only controlled physical label converter; native pipe Unicode functions remain actual source'],
  physicalMemorySha256:hash(path.join(out,'PhysicalMemory.inc')),templateSha256:hash(path.join(out,'NativeCombined.template.cpp')),macros,controlledSettings:{enabled:true,virtualTargetEnabled:true,sharedRequestFixtureDeclarationReused:fixtureHasSettings,actualCurrentAndMayIssueNotReplaced:true,personaMatchDerivedByActualRefreshSetClear:true},productionEdited:false
 });
 return{physicalMemory:path.join(out,'PhysicalMemory.inc'),template:path.join(out,'NativeCombined.template.cpp'),bridge:path.join(out,'namespace-bridge.json'),sourceRanges:physicalBase.rangeFile};
}
