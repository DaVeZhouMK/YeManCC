// Source preparation tests only. No compiler, product, child Host or device IO.
import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {readVirtualControllerSource,snapshotVirtualControllerSource,replaceUniqueSourceText,virtualControllerRuntimeHeaders} from './virtual_controller_runtime_source.mjs';
import {extract} from './fixtures/legion_motion_consumer/extract_main.mjs';

const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const requestedEvidenceRoot=process.env.YMCC_RUNTIME_SOURCE_TEST_OUT;
if(requestedEvidenceRoot)assert(path.isAbsolute(requestedEvidenceRoot),'Explicit source-preparation output must be absolute');
const root=requestedEvidenceRoot?path.resolve(requestedEvidenceRoot):path.resolve(repo,'../../../Build/Validation/VirtualController-Lifecycle-20261019/player-switch-lock12','runtime-source-tools-tests-'+Date.now()+'-'+process.pid);
assert(!fs.existsSync(root),'Source-preparation evidence must be fresh');
fs.mkdirSync(root,{recursive:true});
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const main=path.join(root,'main.cpp'),[capability,start,stop,ui]=virtualControllerRuntimeHeaders;
function memory(files){return readVirtualControllerSource(main,{readFile:file=>{const data=files[path.basename(file)];if(data===undefined)throw Error('MISSING:'+path.basename(file));return Buffer.from(data);}});}

test('allowlist is exactly four execution modules plus target, CLI writer and cold bridges',()=>{
 assert.deepEqual(virtualControllerRuntimeHeaders,[
  'virtual_controller_capability_runtime.h','virtual_controller_start_runtime.h',
  'virtual_controller_stop_runtime.h','virtual_controller_ui_output_runtime.h',
  'virtual_target_return_runtime.h','hidhide_cli_writer_runtime.h','virtual_target_return_cold_runtime.h']);
});
test('legacy main remains byte-identical without moved includes',()=>{
 const source='static bool oldMain() { return true; }\r\n';
 const result=memory({'main.cpp':source});assert.equal(result.source,source);assert.equal(result.inputs.length,1);
});
test('recursive finite expansion preserves bodies and real non-allowlisted includes',()=>{
 const fn='static bool realStart() {\r\n    return true;\r\n}';
 const result=memory({'main.cpp':`// before\r\n#include "${start}"\r\n// after\r\n`,[start]:`#pragma once\n#include "${stop}"\n#include "virtual_switch_execution_return_runtime.h"\n${fn}\n`,[stop]:'#pragma once\n#include "msi_persona_mode_handoff_runtime.h"\nstatic void realStop() {}\n'});
 assert.equal(extract(result.source,'realStart').code,fn);
 assert(result.source.includes('#include "virtual_switch_execution_return_runtime.h"'));
 assert(result.source.includes('#include "msi_persona_mode_handoff_runtime.h"'));
 assert.equal(result.inputs.length,3);assert.equal(result.manifest.includes.length,2);
 assert(!/^#pragma once/m.test(result.source));
 assert.equal(result.manifest.expandedSha256,sha(result.source));
 for(const row of result.inputs)assert.equal(row.sha256,sha(row.data));
});
test('target duty expansion keeps real storage and process headers as compiler inputs',()=>{
 const adapter='virtual_target_return_runtime.h';
 const result=memory({'main.cpp':`#include "${adapter}"\n`,[adapter]:'#pragma once\n#include "virtual_target_return_session.h"\n#include "virtual_target_return_process.h"\nstatic bool targetDuty() {return true;}\n'});
 assert.equal(result.inputs.length,2);
 assert(result.source.includes('#include "virtual_target_return_session.h"'));
 assert(result.source.includes('#include "virtual_target_return_process.h"'));
 assert(extract(result.source,'targetDuty').code);
});

test('nested cold bridge expands while algorithm, PnP reader and Session stay real includes',()=>{
 const target='virtual_target_return_runtime.h',cold='virtual_target_return_cold_runtime.h';
 const env='struct ColdEnvironment { bool current() const { return true; } };';
 const leaf='static bool coldLeaf() { return true; }';
 const result=memory({'main.cpp':'#include "'+target+'"\n',[target]:'#pragma once\n#include "virtual_target_return_session.h"\n#include "'+cold+'"\n',[cold]:'#pragma once\n#include "virtual_target_return_cold_execution.h"\n#include "virtual_target_return_pnp_absence.h"\n'+env+'\n'+leaf+'\n'});
 assert.equal(result.inputs.length,3);assert.equal(extract(result.source,'ColdEnvironment','struct').code,env);assert.equal(extract(result.source,'coldLeaf').code,leaf);
 for(const name of ['virtual_target_return_session.h','virtual_target_return_cold_execution.h','virtual_target_return_pnp_absence.h'])assert(result.source.includes('#include "'+name+'"'));
 assert.equal(result.manifest.includes.length,2);assert.equal(result.manifest.expandedSha256,sha(result.source));
});
test('comments and raw literals cannot fabricate include edges',()=>{
 const source=`/*\n#include "${start}"\n*/\nstatic auto text=R"x(\n#include "${stop}"\n)x";\n// #include "${ui}"\n`;
 assert.equal(memory({'main.cpp':source}).source,source);
});
test('missing allowed include fails closed',()=>assert.throws(()=>memory({'main.cpp':`#include "${start}"\n`}),/MISSING/));
test('path traversal cannot expand a lookalike adjacent module',()=>assert.throws(()=>memory({'main.cpp':`#include "../${start}"\n`}),/NOT_ADJACENT/));
test('include cycles and repeated sources fail instead of hiding definitions',()=>{
 assert.throws(()=>memory({'main.cpp':`#include "${start}"\n`,[start]:`#pragma once\n#include "${start}"\n`}),/CYCLE/);
 assert.throws(()=>memory({'main.cpp':`#include "${start}"\n#include "${start}"\n`,[start]:'#pragma once\n'}),/REPEATED/);
});
test('duplicate function definitions retain the original strict extractor failure',()=>{
 const result=memory({'main.cpp':`static bool same() { return true; }\n#include "${start}"\n`,[start]:'#pragma once\nstatic bool same() { return true; }\n'});
 assert.throws(()=>extract(result.source,'same'),/Expected one function definition/);
});
test('multiline semantic anchors preserve LF or CRLF and reject ambiguous matches',()=>{
 const before='a\nb\n',after='x\ny\n';
 assert.equal(replaceUniqueSourceText('pre\r\n'+before,before,after,'LF'),'pre\r\n'+after);
 assert.equal(replaceUniqueSourceText(before.replaceAll('\n','\r\n'),before,after,'CRLF'),after.replaceAll('\n','\r\n'));
 assert.throws(()=>replaceUniqueSourceText(before+before.replaceAll('\n','\r\n'),before,after,'duplicate'),/NONUNIQUE_SOURCE_ANCHOR/);
 assert.throws(()=>replaceUniqueSourceText('absent',before,after,'missing'),/NONUNIQUE_SOURCE_ANCHOR/);
});
test('snapshot archives every contributor plus expanded identity without overwriting',()=>{
 const result=snapshotVirtualControllerSource(main,path.join(root,'tiny-snapshot'),{readFile:file=>Buffer.from(path.basename(file)==='main.cpp'?`#include "${capability}"\n`:'#pragma once\nstatic bool policy() {return true;}\n')});
 assert.equal(result.manifest.inputs.length,2);
 for(const row of result.manifest.inputs)assert.equal(sha(fs.readFileSync(row.snapshot)),row.sha256);
 assert.equal(sha(fs.readFileSync(result.manifest.expandedSnapshot)),result.manifest.expandedSha256);
 assert.throws(()=>snapshotVirtualControllerSource(main,path.join(root,'tiny-snapshot'),{readFile:()=>Buffer.from('')}),/EEXIST/);
});

// Execute the existing preparation statements verbatim. Stop before any compile
// or fixture run: these checks cannot claim behavioral negative-control kills.
const productionRoot=path.join(root,'production');
const production=snapshotVirtualControllerSource(path.join(repo,'native/main.cpp'),productionRoot);
const compilerHeaderInputs=['native/virtual_target_return_cold_execution.h','native/virtual_target_return_pnp_absence.h','native/virtual_target_return_record.h','native/virtual_target_return_session.h','native/virtual_target_return_store.h','native/virtual_switch_execution_return_runtime.h'].map(relative=>{
 const original=path.join(repo,relative),snapshot=path.join(productionRoot,relative),bytes=fs.readFileSync(original);fs.mkdirSync(path.dirname(snapshot),{recursive:true});fs.writeFileSync(snapshot,bytes,{flag:'wx'});return {relative,original,snapshot,sha256:sha(bytes)};
});
const preparedTools=['tools/virtual_controller_runtime_source.mjs','tools/input_coordinator_lifecycle_selftest.mjs','tools/virtual_controller_runtime_source.test.mjs'].map(relative=>{
 const original=path.join(repo,relative),snapshot=path.join(root,'prepared-tools',relative),bytes=fs.readFileSync(original);fs.mkdirSync(path.dirname(snapshot),{recursive:true});fs.writeFileSync(snapshot,bytes,{flag:'wx'});return {relative,original,snapshot,sha256:sha(bytes)};
});
fs.writeFileSync(path.join(root,'SOURCE-PREP-INPUTS.json'),JSON.stringify({sourceExpansion:production.manifest,compilerHeaderInputs,tools:preparedTools},null,2)+'\n');
test('actual production modules all contribute; old nested includes stay compiler inputs',()=>{
 assert.equal(production.inputs.length,8);
 for(const name of virtualControllerRuntimeHeaders)assert(production.inputs.some(row=>path.basename(row.file)===name));
 assert(production.source.includes('#include "virtual_switch_execution_return_runtime.h"'));
 assert(production.source.includes('#include "msi_persona_mode_handoff_runtime.h"'));
 for(const symbol of ['inputHostStart','inputHostStopLocked','inputHostStop','inputHostSteamDeckSelfOutputBlocked','inputHostMachineTargetRejectionReason','msiUiBoundTargetAdmitted','virtualUiBoundPersona','focusIsolationPlanTick'])assert(extract(production.source,symbol).code);
});
test('actual driver prepares the same TU, existing mutations and three target duty mutations',async t=>{
 const text=fs.readFileSync(path.join(repo,'tools/input_coordinator_lifecycle_selftest.mjs'),'utf8');
 const begin=text.indexOf('let inert=source;const substitutions=[];'),end=text.indexOf("fs.writeFileSync(path.join(snapshot,'native/main.inert.cpp'),inert)");
 assert(begin>=0&&end>begin);
 const prepare=new Function('source','mutation','out','fs','path','sha','extract','replaceUniqueSourceText','snapshot',text.slice(begin,end)+';return {inert,core,structCore,compilerHeaderBindings,substitutions,fragments};');
 const modes=[null,'bypass-admission','rollback-recovery-inheritance','rollback-cancellation-guards','rollback-request-ownership','rollback-ps-endpoint-ownership','rollback-ps-binding-revision','rollback-switch-return-rearm','rollback-switch-fault-owner','rollback-pipe-stage-budget','rollback-initial-start-budget','rollback-wake-return-continuation','rollback-target-reservation','rollback-target-pin-store','rollback-target-settlement'];
 for(const mutation of modes)await t.test(mutation??'positive',()=>{
  const result=prepare(production.source,mutation,root,fs,path,sha,extract,replaceUniqueSourceText,productionRoot);
  const continuation=fs.readFileSync(path.join(repo,'native/virtual_switch_execution_return_runtime.h'),'utf8');
  const continuationNames=[...continuation.matchAll(/^static[^\n;{]*?\b([A-Za-z_]\w*)\s*\(/gm)].map(m=>m[1]);
  assert.equal(result.core.length,60+continuationNames.length,'60 expanded real functions plus each actual continuation function');
  assert.equal(new Set(result.core.map(row=>row.symbol)).size,result.core.length,'No synthetic or duplicate core entry');
  for(const symbol of continuationNames){const row=result.core.find(x=>x.symbol===symbol);assert(row?.compilerObservedHeader&&row.unchanged);assert.equal(row.sha256,sha(extract(continuation,symbol).code));}
  const recreateCore=result.core.find(row=>row.symbol==='inputHostTargetReturnAcceptedRecreateLocked');
  assert(recreateCore?.unchanged);assert.equal(recreateCore.sha256,sha(extract(production.source,recreateCore.symbol).code));
  assert.equal(extract(result.inert,recreateCore.symbol).code,extract(production.source,recreateCore.symbol).code);
  const quarantineCore=result.core.find(row=>row.symbol==='inputHostTargetReturnAfterPsQuarantineLocked');
  assert(quarantineCore?.unchanged);assert.equal(quarantineCore.sha256,sha(extract(production.source,quarantineCore.symbol).code));
  assert.equal(extract(result.inert,quarantineCore.symbol).code,extract(production.source,quarantineCore.symbol).code);
  assert.equal(result.substitutions.filter(row=>row.negativeControl).length,mutation?1:0);
  const originalStart=extract(production.source,'inputHostStart').code;
  if(mutation==='rollback-initial-start-budget'||mutation==='rollback-recovery-inheritance')assert.notEqual(extract(result.inert,'inputHostStart').code,originalStart);
  else assert.equal(extract(result.inert,'inputHostStart').code,originalStart);
  const targetHooks=['inputHostTargetReturnBeforeCreateLocked','inputHostTargetReturnAcceptPsLocked','inputHostTargetReturnTargetExitedLocked','inputHostTargetReturnBeforeHideLocked','inputHostTargetReturnAfterPhysicalRestoreLocked'];
  assert.equal(result.substitutions.filter(row=>['inputHostTargetReturnRecordPath','inputHostTargetReturnMachineBinding','inputHostTargetReturnCaptureOwner'].includes(row.symbol)).length,3);
  const changedHook=mutation==='rollback-target-reservation'?targetHooks[0]:mutation==='rollback-target-pin-store'?targetHooks[1]:mutation==='rollback-target-settlement'?targetHooks[2]:null;
  for(const symbol of targetHooks){
   const original=extract(production.source,symbol).code,prepared=extract(result.inert,symbol).code;
   if(symbol===changedHook)assert.notEqual(prepared,original);
   else assert.equal(prepared,original);
  }
  const cmLeaves=result.substitutions.filter(row=>row.symbol==='inputHostTargetReturnVerifyPhysicalPath');
  assert.equal(cmLeaves.length,1,'Exactly one explicit physical CM verification leaf');
  const originalCmLeaf=extract(production.source,'inputHostTargetReturnVerifyPhysicalPath').code;
  const inertCmLeaf=extract(result.inert,'inputHostTargetReturnVerifyPhysicalPath').code;
  assert.equal(cmLeaves[0].beforeSha256,sha(originalCmLeaf));
  assert.equal(cmLeaves[0].afterSha256,sha(inertCmLeaf));
  assert.match(inertCmLeaf,/\+\+forbiddenIo;throw std::logic_error\("unexpected-physical-return-CM-verification"\);/);
  assert(!inertCmLeaf.includes('physicalPath::verify('),'Prepared TU cannot invoke the real CM ancestry verifier');
  const processLeaves=result.substitutions.filter(row=>row.symbol==='inputHostTargetReturnProbePhysicalWriter');
  assert.equal(processLeaves.length,1,'Exactly one explicit physical-writer process-probe OS leaf');
  const originalProcessLeaf=extract(production.source,'inputHostTargetReturnProbePhysicalWriter').code;
  const inertProcessLeaf=extract(result.inert,'inputHostTargetReturnProbePhysicalWriter').code;
  assert.equal(processLeaves[0].beforeSha256,sha(originalProcessLeaf));
  assert.equal(processLeaves[0].afterSha256,sha(inertProcessLeaf));
  assert.match(inertProcessLeaf,/\+\+forbiddenIo;throw std::logic_error\("unexpected-physical-return-process-probe"\);/);
  assert(!inertProcessLeaf.includes('probeRecordedOwner('),'Prepared TU cannot probe a real recorded writer PID');
  assert.equal(extract(result.inert,'physicalWritersQuiescent').code,extract(production.source,'physicalWritersQuiescent').code,'Physical writer quiescence algorithm remains verbatim');
  const physicalCore=result.core.filter(row=>targetHooks.slice(3).includes(row.symbol));
  assert.equal(physicalCore.length,2,'Both physical hooks remain in the actual core function list');
  assert(physicalCore.every(row=>row.unchanged),'BeforeHide and AfterRestore algorithms remain verbatim for all existing mutants');

  const cliLeaves=['hidHideCliWriterRecordPath','hidHideCliWriterMachineBinding','hidHideCliWriterCapture','hidHideCliWriterProbe','hidHideCliWriterProbeJob'];
  assert.equal(result.substitutions.filter(row=>cliLeaves.includes(row.symbol)).length,5,'Exactly five new CLI environment/OS leaves');
  for(const symbol of cliLeaves){
   const original=extract(production.source,symbol).code,prepared=extract(result.inert,symbol).code;
   const row=result.substitutions.find(x=>x.symbol===symbol);
   assert.equal(row.beforeSha256,sha(original));assert.equal(row.afterSha256,sha(prepared));
   assert.match(prepared,/\+\+forbiddenIo;throw std::logic_error\("unexpected-CLI-writer-/);
   assert(!/yemancc_data_dir|OpenJobObjectW|probeRecordedOwner|process::capture/.test(prepared),'No real CLI environment leaf remains reachable');
  }
  const writer=fs.readFileSync(path.join(repo,'native/hidhide_cli_writer_runtime.h'),'utf8');
  for(const symbol of ['open','clearSettled','settleOwned','quiescent'])assert(result.inert.includes(extract(writer,symbol).code),'Entire writer algorithm remains verbatim: '+symbol);
  const reaper=extract(production.source,'hidHideCliReapPendingLocked').code;
  assert.equal(extract(result.inert,'hidHideCliReapPendingLocked').code,reaper);
  assert(result.core.some(row=>row.symbol==='hidHideCliReapPendingLocked'&&row.unchanged),'actual reaper remains unmodified core');
  for(const header of ['hidhide_cli_job_guard.h','hidhide_cli_writer_store.h'])assert(result.inert.includes('#include "'+header+'"'),'real data/Job classes stay compiler inputs');


  const coldLeaves=['inputHostTargetReturnColdAbsence','inputHostTargetReturnColdHidden'];
  assert.equal(result.substitutions.filter(row=>coldLeaves.includes(row.symbol)).length,2,'Only the two explicit cold read leaves are adapted');
  for(const symbol of coldLeaves){
   const original=extract(production.source,symbol).code,prepared=extract(result.inert,symbol).code,row=result.substitutions.find(x=>x.symbol===symbol);
   assert.equal(row.beforeSha256,sha(original));assert.equal(row.afterSha256,sha(prepared));
   assert.match(prepared,/\+\+forbiddenIo;throw std::logic_error\("unexpected-cold-return-/);
   assert(!/pnpAbsence::verify|hidHideRun/.test(prepared),'No production census or driver query can run through a prepared cold read leaf');
  }
  for(const symbol of ['inputHostTargetReturnHistorySettledLocked','inputHostTargetReturnColdPassLocked','inputHostTargetReturnQueueColdStartup']){
   assert.equal(extract(result.inert,symbol).code,extract(production.source,symbol).code,'Cold algorithm/queue boundary remains actual: '+symbol);
   assert(result.core.some(row=>row.symbol===symbol&&row.unchanged));
  }
  const environment=extract(production.source,'ColdEnvironment','struct').code;
  assert.equal(extract(result.inert,'ColdEnvironment','struct').code,environment,'Entire Env including current/dead/writer/store behavior stays verbatim');
  assert.equal(result.structCore.length,1);assert.equal(result.structCore[0].sha256,sha(environment));
  assert.equal(result.compilerHeaderBindings.length,6);
  const compilerTexts=[result.inert,...result.compilerHeaderBindings.map(row=>fs.readFileSync(path.join(repo,row.source),'utf8'))].join('\n');
  for(const row of result.compilerHeaderBindings){assert(row.compilerObservationRequired);assert.equal(row.sha256,sha(fs.readFileSync(path.join(repo,row.source))));assert(compilerTexts.includes('#include "'+path.basename(row.source)+'"'),'Real direct/transitive algorithm/data/continuation header stays compiler input: '+row.source);}

  assert.equal(result.fragments.length,2);
 });
});
test('actual focus fixture preparation retains four real gates and all source snapshots',()=>{
 const file=path.join(repo,'tools/custom_steam_library_virtual_input_selftest.mjs'),text=fs.readFileSync(file,'utf8');
 const begin=text.indexOf('const mainFile ='),end=text.indexOf('function run(name, mutant = false)');assert(begin>0&&end>begin);
 const prepare=new Function('repo','root','fs','path','assert','snapshotVirtualControllerSource',text.slice(begin,end)+';return {generated:generate(),expanded,outputGates,replacements};');
 const out=path.join(root,'focus');fs.mkdirSync(out);
 const result=prepare(repo,out,fs,path,assert,snapshotVirtualControllerSource);
 assert.equal(result.outputGates.match(/return gamepadUiInputEligible\(\);/g)?.length,4);
 assert(!result.generated.includes('//__PRODUCTION_'));
 assert.equal(result.expanded.inputs.length,8);
 for(const row of result.expanded.manifest.inputs)assert.equal(sha(fs.readFileSync(row.snapshot)),row.sha256);
 fs.writeFileSync(path.join(out,'prepared-fixture.cpp'),result.generated);
});
test('actual target policy preparation extracts Start and binds moved raw headers',()=>{
 const text=fs.readFileSync(path.join(repo,'tools/virtual_target_policy_native_selftest.mjs'),'utf8');
 const begin=text.indexOf(' const expanded='),end=text.indexOf(" // Keep the shared harness source read-only;");assert(begin>0&&end>begin);
 const out=path.join(root,'policy');fs.mkdirSync(out);const receipt={inputs:[]};
 const write=(rel,bytes)=>{const dest=path.join(out,rel);fs.mkdirSync(path.dirname(dest),{recursive:true});fs.writeFileSync(dest,bytes);};
 const prepare=new Function('repo','out','fs','path','assert','sha','extract','snapshotVirtualControllerSource','receipt','write',text.slice(begin,end)+';return {prefix,consumers,expanded};');
 const result=prepare(repo,out,fs,path,assert,sha,extract,snapshotVirtualControllerSource,receipt,write);
 assert(result.prefix.includes('if (!ymcc::controllerPersona::currentOutput(requestedPersona)) return false;'));
 const fullStart=extract(result.expanded.source,'inputHostStart').code;
 assert.equal(result.prefix,fullStart.slice(0,fullStart.indexOf('    msiControllerVisibilityObserverStart();'))+'    ++boundaryCalls; return true;\n}');
 assert.equal(result.expanded.inputs.length,8);
 for(const name of virtualControllerRuntimeHeaders)assert(receipt.inputs.some(row=>row.path==='native/'+name));
});
process.on('exit',()=>console.log('SOURCE_PREPARATION_EVIDENCE='+root));
