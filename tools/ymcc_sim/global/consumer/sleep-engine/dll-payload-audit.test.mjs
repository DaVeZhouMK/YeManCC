/** Pure read-only audit contracts. Fixtures are D copies of explicitly supplied
 * executed receipts/PE bytes, never a rerun of the engine or product.
 */
import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {auditReceipts} from '../../receipt-audit.mjs';
import {readPeModuleIdentity} from './pe-module-identity.mjs';
const input=process.env.YMCC_SLEEP_DLL_AUDIT_RUN_RECEIPT, repo=process.env.YMCC_SLEEP_DLL_AUDIT_REPO_ROOT, output=process.env.YMCC_SLEEP_DLL_AUDIT_TEST_ROOT;
assert(input && repo && output && path.isAbsolute(input) && path.isAbsolute(repo) && path.isAbsolute(output),'EXPLICIT_EXECUTED_RECEIPT_FIXED_ROOT_AND_PRIVATE_TEST_OUTPUT_REQUIRED');
const original=JSON.parse(fs.readFileSync(input)), originalRoot=path.dirname(input);
const sha = bytes => createHash('sha256').update(bytes).digest('hex');
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const outcomes=[];fs.mkdirSync(output,{recursive:true});
process.on('exit',()=>fs.writeFileSync(path.join(output,'outcomes.json'),JSON.stringify({kind:'pure-sleep-dll-audit-contracts',executedProductTests:0,outcomes},null,2)));
function copyValues(value,mappings){
  if(typeof value==='string') {for(const [from,to] of mappings) for(const prefix of [path.resolve(from),path.resolve(from).replaceAll('\\','/')]) if(value===prefix || value.startsWith(prefix+path.sep) || value.startsWith(prefix+'/')) return to+value.slice(prefix.length).replaceAll('\\',path.sep).replaceAll('/',path.sep); return value;}
  if(Array.isArray(value))return value.map(row=>copyValues(row,mappings));
  if(value && typeof value==='object')return Object.fromEntries(Object.entries(value).map(([key,row])=>[key,copyValues(row,mappings)]));
  return value;
}
function fixture(t,mode='memory') {
  const root=fs.mkdtempSync(path.join(output,'fixture-'));t.after(()=>{assert(path.dirname(root)===path.resolve(output) && path.basename(root).startsWith('fixture-'));fs.rmSync(root,{recursive:true,force:true});});
  const runRoot=path.join(root,'run'), repoRoot=path.join(root,'source'), payloadRoot=path.join(root,'payload'), id=mode==='memory'?'sleep.engine-memory':'sleep.engine-http-all';
  const sourceItem=original.results.find(row=>row.id===id);assert(sourceItem);
  const caseDir=path.join(runRoot,'cases',id), engine=path.join(caseDir,'engine');fs.mkdirSync(engine,{recursive:true});fs.mkdirSync(payloadRoot,{recursive:true});
  const mappings=[[originalRoot,runRoot],[original.hostPayload.root,payloadRoot],[repo,repoRoot]];
  const run=copyValues({...original,results:[sourceItem]},mappings);run.repoRoot=repoRoot;
  const raw=copyValues(read(path.join(originalRoot,'cases',id,'engine/sleep-engine-result.json')),mappings);
  const inner=copyValues(read(path.join(originalRoot,'cases',id,'engine/receipt-audit.json')),mappings);
  const item=run.results[0], rawFile=path.join(engine,'sleep-engine-result.json'), innerFile=path.join(engine,'receipt-audit.json');
  for(const row of item.sourceInputs){const target=path.join(repoRoot,row.path);fs.mkdirSync(path.dirname(target),{recursive:true});fs.copyFileSync(path.join(repo,row.path),target);}
  for(const binary of run.hostPayload.binaries)if(binary.sha256)fs.copyFileSync(path.join(original.hostPayload.root,binary.name),path.join(payloadRoot,binary.name));
  const groups=(raw.groups??[raw]).map(group=>({group,file:group.evidencePath,value:copyValues(read(path.join(originalRoot,'cases',id,'engine/groups',group.mode,'sleep-engine-result.json')),mappings)}));
  const write=(file,value)=>{fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,JSON.stringify(value));};
  const f={root,run,raw,inner,item,groups,repoRoot,payloadRoot,caseDir,rawFile,innerFile,runFile:path.join(runRoot,'result.json'),seal({link=true}={}){
    for(const group of groups)write(group.file,group.value);
    write(rawFile,raw);if(link)inner.inputReceipt={path:rawFile,sha256:sha(fs.readFileSync(rawFile))};write(innerFile,inner);
    for(const proof of item.structuredEvidence??[])if(fs.existsSync(proof.path))proof.sha256=sha(fs.readFileSync(proof.path));
    write(this.runFile,run);return this;
  },audit({currentRepoRoot=repoRoot}={}){return auditReceipts([this.runFile],{repoRoot:currentRepoRoot});}};
  return f;
}
function record(name,report){outcomes.push({name,status:report.status,summary:report.summary,diagnostics:report.cases.map(row=>({id:row.id,status:row.status,diagnostics:row.diagnostics}))});fs.writeFileSync(path.join(output,name+'.json'),JSON.stringify({syntheticAuditFixture:true,productExecuted:false,report},null,2));}
for(const mode of ['memory','all'])test('DLL-only '+mode+' retains exact caller, original inner and loaded PE identity',t=>{
  const f=fixture(t,mode).seal(),r=f.audit();record('positive-'+mode,r);assert.equal(r.status,'CURRENT_RECEIPTS_VALID',JSON.stringify(r.cases));assert.equal(r.executedTests,0);assert.equal(r.cases[0].payloadIdentity.revalidation.groupOutputs.length,mode==='memory'?1:3);assert.equal(r.cases[0].payloadIdentity.originalInnerAudit.status,'PASS');assert.equal(r.cases[0].payloadIdentity.loadedModules.length,2);
});
const mutations=[
 ['backend-declaration-missing',f=>f.run.hostPayload.binaries=f.run.hostPayload.binaries.filter(row=>row.name!=='YeManFanBackend.dll'),'HOST_BACKEND_PAIR_REQUIRED'],
 ['backend-file-missing',f=>fs.unlinkSync(path.join(f.payloadRoot,'YeManFanBackend.dll')),'PAYLOAD_BYTES_CHANGED:YeManFanBackend.dll'],
 ['backend-byte-changed',f=>fs.appendFileSync(path.join(f.payloadRoot,'YeManFanBackend.dll'),'mutation'),'PAYLOAD_BYTES_CHANGED:YeManFanBackend.dll'],
 ['host-byte-changed',f=>fs.appendFileSync(path.join(f.payloadRoot,'YeManFanHost.dll'),'mutation'),'PAYLOAD_BYTES_CHANGED:YeManFanHost.dll'],
 ['optional-dependency-byte-changed',f=>fs.appendFileSync(path.join(f.payloadRoot,'WinRT.Runtime.dll'),'mutation'),'PAYLOAD_BYTES_CHANGED:WinRT.Runtime.dll'],
 ['exe-appears-after-original-absence',f=>fs.writeFileSync(path.join(f.payloadRoot,'YeManFanHost.exe'),'unexpected'),'PAYLOAD_BYTES_CHANGED:YeManFanHost.exe'],
 ['caller-root-missing',f=>delete f.run.candidateInputs.payloadRoot,'EXPLICIT_CALLER_PAYLOAD_ROOT_REQUIRED'],
 ['caller-root-wrong',f=>f.run.candidateInputs.payloadRoot=f.root,'EXPLICIT_CALLER_PAYLOAD_ROOT_REQUIRED'],
 ['report-payload-root-wrong',f=>f.raw.actualHost.payloadRoot=f.root,'INNER_REVALIDATION_FAILED:LOADED_HOST_SOURCE_PATH_DIFFERS'],
 ['wrong-mode',f=>f.raw.mode='all','MODE_OR_RECEIPT_KIND_DIFFERS'],
 ['wrong-entry-source',f=>f.item.source='tools/arbitrary-host-mock.ps1','ENTRY_CONTRACT_DIFFERS'],
 ['wrong-entry-scope',f=>f.item.scope='desktop-source','ENTRY_CONTRACT_DIFFERS'],
 ['loaded-host-sha-wrong',f=>f.raw.actualHost.loaded[0].sha256='f'.repeat(64),'INNER_REVALIDATION_FAILED:ACTUAL_LOADED_HOST_IDENTITY_DIFFERS'],
 ['loaded-host-path-wrong',f=>f.raw.actualHost.loaded[0].path=path.join(f.root,'YeManFanHost.dll'),'INNER_REVALIDATION_FAILED:LOADED_HOST_SOURCE_PATH_DIFFERS'],
 ['loaded-host-mvid-forged',f=>f.raw.actualHost.loaded[0].moduleVersionId='00000000-0000-0000-0000-000000000000','LOADED_METADATA_DIFFERS:YeManFanHost.dll'],
 ['loaded-assembly-identity-forged',f=>f.raw.actualHost.loaded[0].identity='YeManFanHost, Version=99.0.0.0, Culture=neutral, PublicKeyToken=null','LOADED_METADATA_DIFFERS:YeManFanHost.dll'],
 ['loaded-backend-missing',f=>f.raw.actualHost.loaded=f.raw.actualHost.loaded.filter(row=>!row.path.endsWith('YeManFanBackend.dll')),'ORIGINAL_INNER_AUDIT_CONTRADICTORY'],
 ['pinned-host-sha-forged',f=>f.raw.actualHost.pinnedArtifacts[0].sha256='f'.repeat(64),'PINNED_LOADED_IDENTITY_DIFFERS:YeManFanHost.dll'],
 ['pinned-host-loaded-flag-forged',f=>f.raw.actualHost.pinnedArtifacts[0].actuallyLoaded=false,'PINNED_LOADED_IDENTITY_DIFFERS:YeManFanHost.dll'],
 ['fake-main-called',f=>f.raw.process.mainCalled=true,'INNER_REVALIDATION_FAILED:PRODUCTION_MAIN_WAS_NOT_EXCLUDED'],
 ['wrapper-main-started',f=>f.raw.wrapperVerification.realHostMainStarted=true,'WRAPPER_MAIN_BOUNDARY_DIFFERS'],
 ['wrapper-machine-gate-bypassed',f=>f.raw.wrapperVerification.mainMachineGateBypassed=true,'WRAPPER_MAIN_BOUNDARY_DIFFERS'],
 ['safety-main-field-missing',f=>delete f.raw.safety.productionMainInvoked,'INNER_REVALIDATION_FAILED:PRODUCTION_MAIN_WAS_NOT_EXCLUDED'],
 ['safety-hc-loaded',f=>f.raw.safety.hcLoaded=true,'INNER_REVALIDATION_FAILED:HC_OR_TARGET_NATIVE_ATTEMPT'],
 ['safety-target-native-attempt',f=>f.raw.safety.nativeAttempts=['target-native.dll'],'INNER_REVALIDATION_FAILED:HC_OR_TARGET_NATIVE_ATTEMPT'],
 ['safety-physical-promoted',f=>f.raw.safety.physicalHardwareValidated=true,'INNER_REVALIDATION_FAILED:SOFTWARE_SCOPE_PROMOTED_TO_NATIVE_OR_PHYSICAL'],
 ['forced-cleanup',f=>f.raw.process.forcedCleanup=true,'INNER_REVALIDATION_FAILED:NATURAL_EXIT_NOT_CONFIRMED'],
 ['natural-exit-missing',f=>delete f.raw.process.naturalExitObservedByParent,'INNER_REVALIDATION_FAILED:NATURAL_EXIT_NOT_CONFIRMED'],
 ['original-inner-fail',f=>f.inner.status='FAIL','ORIGINAL_INNER_AUDIT_NOT_PASS'],
 ['original-inner-source-verification-missing',f=>delete f.inner.summary.sourceHashesVerified,'ORIGINAL_INNER_AUDIT_CONTRADICTORY'],
 ['original-validator-source-missing',f=>delete f.inner.sourceHashes['tools/ymcc_sim/global/consumer/sleep-engine/receipt-audit.mjs'],'ORIGINAL_VALIDATOR_SOURCE_DIFFERS'],
 ['tool-source-after-missing',f=>f.raw.sourceInputsAfter.pop(),'TOOL_SOURCE_CLOSURE_INCOMPLETE'],
 ['tool-source-after-sha-changed',f=>f.raw.sourceInputsAfter.find(row=>row.path.endsWith('Program.cs')).sha256='f'.repeat(64),'TOOL_SOURCE_IDENTITY_DIFFERS:Program.cs'],
 ['source-stability-missing',f=>delete f.raw.sourceStability.beforeBuildChecked,'SOURCE_STABILITY_UNPROVEN'],
 ['source-declaration-removed',f=>f.item.sourceInputs=f.item.sourceInputs.filter(row=>row.path!=='FanLab/real-host/Program.cs'),'SOURCE_CLOSURE_DIFFERS:FanLab/real-host/Program.cs'],
 ['process-guard-identity-forged',f=>f.item.processGuard.sha256='f'.repeat(64),'PROCESS_GUARD_IDENTITY_DIFFERS'],
 ['raw-group-output-changed',f=>f.groups[0].value.process.pid++,'GROUP_OUTPUT_PROCESS_DIFFERS'],
 ['raw-child-claims-parent-exit',f=>f.groups[0].value.process.naturalExitObservedByParent=true,'GROUP_OUTPUT_PROCESS_DIFFERS'],
 ['machine-gate-observation-missing',f=>delete f.raw.productionMachineGates.after,'MACHINE_GATE_OBSERVATION_INVALID'],
 ['null-backend-proof-removed',f=>f.raw.checks=f.raw.checks.filter(row=>row.name!=='realBackend remains null'),'ORIGINAL_INNER_AUDIT_CONTRADICTORY'],
 ['original-inner-output-undeclared',f=>f.item.structuredEvidence=f.item.structuredEvidence.filter(row=>row.path!==f.innerFile),'OUTPUT_IDENTITY_DIFFERS:receipt-audit.json'],
 ['http-loaded-process-forged',f=>f.raw.actualHost.loadedByProcess[0].processId++,'LOADED_PROCESS_IDENTITY_DIFFERS','all'],
 ['http-group-safety-missing',f=>delete f.raw.groups[0].safety.hardwareWritesPerformed,'SAFETY_MISSING_OR_PROMOTED:hardwareWritesPerformed','all'],
];
for(const [name,mutate,expected,mode] of mutations)test('reject '+name,t=>{
  const f=fixture(t,mode);mutate(f);f.seal();const r=f.audit();record(name,r);assert.equal(r.summary.currentPass,0);assert.equal(r.cases[0].diagnostics[0]?.code,'SLEEP_DLL_PAYLOAD_IDENTITY_INVALID');assert.equal(r.cases[0].diagnostics[0]?.reason,'SLEEP_DLL_'+expected,JSON.stringify(r.cases[0].diagnostics));
});
test('reject bad original inner raw receipt linkage',t=>{const f=fixture(t);f.inner.inputReceipt.sha256='f'.repeat(64);f.seal({link:false});const r=f.audit();record('bad-inner-link',r);assert.equal(r.cases[0].diagnostics[0].reason,'SLEEP_DLL_INNER_INPUT_RECEIPT_DIFFERS');});
test('reject changed original raw output without resigning saved proof',t=>{const f=fixture(t).seal();fs.appendFileSync(f.rawFile,' ');const r=f.audit();record('unresigned-raw-output',r);assert.equal(r.cases[0].diagnostics[0].reason,'SLEEP_DLL_OUTPUT_IDENTITY_DIFFERS:sleep-engine-result.json');});
test('DLL identity success still passes through common nested receipt checks',t=>{const f=fixture(t),file=path.join(f.caseDir,'extra.json');fs.writeFileSync(file,JSON.stringify({status:'FAIL'}));f.item.structuredEvidence.push({path:file,sha256:sha(fs.readFileSync(file)),failed:false});f.seal();const r=f.audit();record('common-output-closure-not-bypassed',r);assert(r.cases[0].payloadIdentity);assert.equal(r.cases[0].status,'STALE_OR_INVALID_EVIDENCE');assert.equal(r.cases[0].diagnostics[0].code,'REQUIRED_RECEIPT_OR_PRODUCT_HASH_INVALID');});
for(const id of ['fan.complete-host-selftest','fan.parent-exit-contract','sleep.exclusive-host','fan.runtime-connection','arbitrary.host-mock'])test('original full Host gate remains mandatory: '+id,t=>{
  const f=fixture(t);f.item.id=id;f.seal();const r=f.audit();record('full-host-denied-'+id,r);assert.equal(r.summary.currentPass,0);assert.equal(r.cases[0].diagnostics[0]?.code,'HOST_PAYLOAD_IDENTITY_MISSING');assert.equal(r.cases[0].payloadIdentity,undefined);
});
test('normal Host contract with missing identity stays blocked even with valid sleep outputs',t=>{const f=fixture(t);f.item.id='host.main';delete f.run.hostPayload;f.seal();const r=f.audit();record('normal-host-missing-identity',r);assert.equal(r.cases[0].diagnostics[0].code,'HOST_PAYLOAD_IDENTITY_MISSING');});
test('PE metadata parser rejects truncation and reads actual pinned MVID without CLR',()=>{
  for(const name of ['YeManFanHost.dll','YeManFanBackend.dll']){
    const bytes=fs.readFileSync(path.join(original.hostPayload.root,name)),meta=readPeModuleIdentity(bytes);
    const actual=read(path.join(originalRoot,'cases/sleep.engine-memory/engine/sleep-engine-result.json')).actualHost.loaded.find(row=>row.path.endsWith(name));
    assert.equal(meta.moduleVersionId,actual.moduleVersionId);assert.equal(meta.identity,actual.identity);assert.equal(meta.moduleName,name);
    for(const length of [0,63,128,1024])assert.throws(()=>readPeModuleIdentity(bytes.subarray(0,length)),/SLEEP_DLL_PE_METADATA_INVALID/);
    const wrong=Buffer.from(bytes);wrong.writeUInt32LE(0xffffffff,0x3c);assert.throws(()=>readPeModuleIdentity(wrong),/SLEEP_DLL_PE_METADATA_INVALID/);
  }
});
function currentCopy(f){const current=path.join(f.root,'current-source');fs.cpSync(f.repoRoot,current,{recursive:true,errorOnExist:true});return current;}
test('different execution/current roots with equal relative bytes preserve original executed paths',t=>{
  const f=fixture(t),current=currentCopy(f);f.seal();const before=f.raw.sourceInputs.map(row=>row.path),guard=f.item.processGuard.helper;
  const r=f.audit({currentRepoRoot:current});record('relocated-current-equal-bytes',r);assert.equal(r.status,'CURRENT_RECEIPTS_VALID',JSON.stringify(r.cases));
  assert.equal(r.cases[0].payloadIdentity.revalidation.executionRepoRoot,f.repoRoot);assert.equal(r.cases[0].payloadIdentity.revalidation.currentRepoRoot,current);
  assert.deepEqual(read(f.rawFile).sourceInputs.map(row=>row.path),before);assert.equal(read(f.runFile).results[0].processGuard.helper,guard);
});
for(const side of ['current','execution'])for(const relative of ['FanLab/real-host/Program.cs','tools/ymcc_sim/global/host-process.ps1'])test(side+' source change expires relocated audit: '+relative,t=>{
  const f=fixture(t),current=currentCopy(f);fs.appendFileSync(path.join(side==='current'?current:f.repoRoot,relative),'\nchanged source');f.seal();const r=f.audit({currentRepoRoot:current});record('relocated-'+side+'-changed-'+path.basename(relative),r);
  assert.equal(r.summary.currentPass,0);assert.equal(r.cases[0].diagnostics[0].reason,'SLEEP_DLL_'+side.toUpperCase()+'_SOURCE_CLOSURE_DIFFERS:'+relative);
});
test('missing original run execution root cannot borrow current audit root',t=>{const f=fixture(t);delete f.run.repoRoot;f.seal();const r=f.audit();record('execution-root-missing',r);assert.equal(r.cases[0].diagnostics[0].reason,'SLEEP_DLL_EXECUTION_REPO_ROOT_REQUIRED');});
test('forged execution root with equal bytes cannot rewrite actual source/guard provenance',t=>{const f=fixture(t),other=currentCopy(f);f.run.repoRoot=other;f.seal();const r=f.audit();record('execution-root-forged-equal-bytes',r);assert.equal(r.cases[0].diagnostics[0].reason,'SLEEP_DLL_PROCESS_GUARD_IDENTITY_DIFFERS');});
test('forged execution-root tool path cannot borrow equivalent current source file',t=>{const f=fixture(t),current=currentCopy(f);f.raw.sourceInputs[0].path=path.join(current,path.relative(f.repoRoot,f.raw.sourceInputs[0].path));f.seal();const r=f.audit({currentRepoRoot:current});record('execution-source-path-forged',r);assert.equal(r.cases[0].diagnostics[0].reason,'SLEEP_DLL_TOOL_SOURCE_IDENTITY_DIFFERS:Invoke-SleepEngine.ps1');});