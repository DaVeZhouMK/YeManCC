import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
import { parseArgs, prepareFreshEvidence, resolveInputs, baselineRelativeRoot, baselinePins, writeBaselineRelativeRoot, writeBaselinePins, resolveWriteBaseline, BlockedInputError } from './inputs.mjs';
import { execute } from './run.mjs';
import {toolSourceInputs,fileHash} from './tool-policy-runtime.mjs';
import { getOneXFanCoexistenceAdapters } from './adapter.mjs';
import {createHash} from 'node:crypto';
import {verifyDeclaredHostPayload} from './payload-staging.mjs';
import {sourceContractInputs} from './source-contracts.mjs';

// MODEL_ONLY metadata files, never executable payload approval. The production
// verifier requires every Host member, so this exercises disappearance after its
// last actual read. No verifier result or filesystem observation is fabricated.
function missingCandidateAfterAdmission() {
 const modelRepo=path.join(root,'candidate-boundary-MODEL_ONLY'),payloadRoot=path.join(modelRepo,'PowerControl/fan-host');
 const runtimeId='YMCC-CANDIDATE-BOUNDARY-MODEL_ONLY',runtimeRoot=path.join(modelRepo,'PowerControl/handheldcompanion-runtime',runtimeId),sharedRuntimeRoot=path.join(modelRepo,'PowerControl/ymcc-shared-runtime');
 const sha=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
 const sourceFiles=[...toolSourceInputs(repo).map(row=>row.path),...sourceContractInputs(repo).map(file=>path.join(repo,file)),
  ...['native/ProcessEfficiency.cs','FanLab/DeviceCoreSharedDependencies.cs','FanLab/RuntimeDependencyContract.cs','tools/hc-application-dependency-trim/ApplicationTrim.csproj',
   'tools/fan_control_self_heal_selftest.ts','tools/fan_hc_enable_alignment_selftest.ps1','tools/fan_hc_lifecycle_order_audit.ps1','tools/fan_hc_lifecycle_deep_selftest.ps1'].map(file=>path.join(repo,file)),
  ...['provenance.json',...Object.keys(baselinePins)].map(file=>path.join(repo,baselineRelativeRoot,file))];
 const copies=[];for(const file of [...new Set(sourceFiles)]){const rel=path.relative(repo,file);assert(!path.isAbsolute(rel)&&rel!=='..'&&!rel.startsWith('..'+path.sep));const target=path.join(modelRepo,rel),before=sha(file);fs.mkdirSync(path.dirname(target),{recursive:true});fs.copyFileSync(file,target);assert.equal(sha(target),before);copies.push({relative:rel,sha256:before});}
 const write=(file,value)=>{fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,typeof value==='string'?value:JSON.stringify(value,null,2)+'\n',{flag:'wx'});return sha(file);};
 const hcAssembly=path.join(runtimeRoot,'HandheldCompanion.dll');fs.mkdirSync(runtimeRoot,{recursive:true});fs.copyFileSync(actual.hc.path,hcAssembly);assert.equal(sha(hcAssembly),actual.hc.sha256);
 const sharedJson=path.join(sharedRuntimeRoot,'Newtonsoft.Json.dll');fs.mkdirSync(sharedRuntimeRoot,{recursive:true});fs.copyFileSync(path.join(repo,'PowerControl/ymcc-shared-runtime/Newtonsoft.Json.dll'),sharedJson);
 const sharedRecord=JSON.parse(fs.readFileSync(path.join(repo,'PowerControl/ymcc-shared-runtime/manifest.json'),'utf8')).files.find(row=>row.packagedPath==='Newtonsoft.Json.dll');assert.equal(sha(sharedJson),sharedRecord.sha256);
 const runtimeManifestSha256=write(path.join(runtimeRoot,'HandheldCompanion.runtime.json'),{schemaVersion:1,runtimeId,MODEL_ONLY:true,files:[{path:'HandheldCompanion.dll',sha256:sha(hcAssembly)}]});
 const sharedManifestSha256=write(path.join(sharedRuntimeRoot,'manifest.json'),{schemaVersion:1,MODEL_ONLY:true,files:[sharedRecord]});
 const members={'YeManFanHost.exe':'MODEL_ONLY inert apphost text; never execute.\n','YeManFanHost.dll':'MODEL_ONLY inert Host text; never load.\n','YeManFanBackend.dll':'MODEL_ONLY inert backend text; never load.\n',
  'YeManFanHost.deps.json':{MODEL_ONLY:true,targets:{'MODEL_ONLY/0':{'MODEL_ONLY/0':{runtime:{'YeManFanHost.dll':{}}}}}},'YeManFanHost.runtimeconfig.json':{MODEL_ONLY:true},'YeManFanHost.json':{MODEL_ONLY:true}};
 const files=Object.entries(members).map(([name,body])=>({path:name,sha256:write(path.join(payloadRoot,name),body)}));
 const manifestPath=path.join(payloadRoot,'YeManFanHost.payload.json'),manifestSha256=write(manifestPath,{schemaVersion:2,MODEL_ONLY:true,runtimeId,runtimeManifest:'../handheldcompanion-runtime/'+runtimeId+'/HandheldCompanion.runtime.json',files});
 const binding={manifestPath,manifestSha256,candidateHostSha256:sha(path.join(payloadRoot,'YeManFanHost.dll')),runtimeRoot,sharedRuntimeRoot,runtimeManifestSha256,sharedManifestSha256};
 const declared=verifyDeclaredHostPayload({...binding,payloadRoot});assert.equal(declared.status,'PASS_STRICT_DECLARED_OWNED_PAYLOAD');
 const removedMember=path.join(payloadRoot,'YeManFanHost.exe'),lastRead=path.join(payloadRoot,'YeManFanHost.deps.json');let retractions=0;
 return {options:{repoRoot:modelRepo,candidateHostRoot:payloadRoot,candidatePayloadBinding:binding,hcAssembly,cecilPath:actual.cecil.path},run(fn,outDir){
  const originalRead=fs.readFileSync;
  fs.readFileSync=function(file,...args){const bytes=originalRead.call(fs,file,...args);if(file===lastRead&&args[0]==='utf8'){assert.equal(retractions,0);assert.equal(sha(removedMember),files.find(row=>row.path==='YeManFanHost.exe').sha256);fs.unlinkSync(removedMember);retractions++;}return bytes;};
  try {const result=fn();assert.equal(retractions,1);assert.equal(result.inputEvidence.declaredCandidatePayload.manifestSha256,manifestSha256);assert.equal(result.detail.path,removedMember);assert.equal(fs.existsSync(removedMember),false);assert.equal(fs.existsSync(path.join(outDir,'generated')),false);
   fs.writeFileSync(path.join(root,'candidate-boundary-proof.json'),JSON.stringify({MODEL_ONLY:true,productionPayloadApproval:false,binding,verifiedInputPins:declared.inputPins,copiedSourceFiles:copies,removedMember,retractions,removalBoundary:'after real deps JSON read, following all member hashes in unmodified strictPlan; before original post-verification existence loop',resultCode:result.code},null,2)+'\n');return result;
  } finally {fs.readFileSync=originalRead;}
 }};
}

const here=path.dirname(fileURLToPath(import.meta.url)),repo=path.resolve(here,'../../../..');
const root=fs.mkdtempSync(path.join(process.env.YMCC_ONEX_TEST_ROOT??os.tmpdir(),'onex-entry-'));
const blocked=code=>error=>error instanceof BlockedInputError&&error.code===code;
function moved(name) {
 const target=path.join(root,name),fixed=path.join(target,baselineRelativeRoot);
 fs.mkdirSync(path.dirname(fixed),{recursive:true});fs.cpSync(path.join(repo,baselineRelativeRoot),fixed,{recursive:true});return target;
}
// These existing input-failure tests must pass the new source admission first.
// All tool artifact pins intentionally name absent self-owned files: if a test
// ever reaches build/launch, it still refuses instead of executing a producer.
function inputFailurePlan(repoRoot=repo){
 const file=path.join(root,'input-only-plan.json'),names={Harness:'OneXFanCoexistence',JoinedFactoryProbe:'JoinedFactoryProbe',BootstrapFixture:'YmccBootstrapFixture',STA:'STA',MTA:'MTA'};
 const pin=name=>({path:path.join(root,'never-built',name),sha256:'0'.repeat(64)});
 const tools=Object.entries(names).map(([kind,name])=>({kind,entry:pin(name+'.dll'),deps:pin(name+'.deps.json'),runtimeconfig:pin(name+'.runtimeconfig.json'),helper:pin('ScopedLoadAuthority.dll')}));
 fs.writeFileSync(file,JSON.stringify({schemaVersion:1,repoRoot,sources:toolSourceInputs(repoRoot),buildInputs:[],tools,sharedAuthority:pin('ScopedLoadAuthority.dll'),calls:[{kind:'Harness',argvSha256:'0'.repeat(64),policy:pin('never-launch.policy.json')}] }));
 return {toolLoadPlan:file,toolLoadPlanSha256:fileHash(file)};
}
const actual=resolveInputs({repoRoot:repo});
test('strict CLI parses all explicit inputs',()=>{
 assert.deepEqual(parseArgs(['--evidence-dir','fresh path','--hc-assembly','hc','--cecil-path','cecil','--legacy-root','old','--candidate-host-root','candidate']),
  {evidenceDir:'fresh path',hcAssembly:'hc',cecilPath:'cecil',legacyRoot:'old',candidateHostRoot:'candidate'});
});
for(const flag of ['--evidence-dir','--hc-assembly','--cecil-path','--legacy-root','--candidate-host-root'])
 test('rejects empty/missing/repeated '+flag,()=>{
  for(const tail of [[],[''],[' '],['--unknown']])assert.throws(()=>parseArgs([flag,...tail]),/Missing value/);
  assert.throws(()=>parseArgs([flag,'first',flag,'second']),/Duplicate argument/);
 });
test('unknown and positional arguments fail before evidence creation',()=>{
 const out=path.join(root,'never-created');
 for(const args of [[],['--out-dir',out],['--evidence-dir',out,'unexpected'],['--evidence-dir',out,'--unknown','x'],['--evidence-dir',out,'--evidence-dir','other']]){
  const p=spawnSync(process.execPath,[path.join(here,'run.mjs'),...args],{windowsHide:true,encoding:'utf8'});
  assert.equal(p.status,2);assert.equal(JSON.parse(p.stderr).code,'ONEX_ARGUMENT_ERROR');assert.equal(fs.existsSync(out),false);
 }
});
test('fresh evidence cannot overwrite an existing result',()=>{
 const out=prepareFreshEvidence(path.join(root,'existing'));fs.writeFileSync(path.join(out,'result.json'),'sentinel');
 assert.throws(()=>execute({evidenceDir:out}),/already exists/);assert.equal(fs.readFileSync(path.join(out,'result.json'),'utf8'),'sentinel');
});
test('baseline and exact HC/Cecil remain usable after relocation',()=>{
 const target=moved('relocated with spaces');const d=resolveInputs({repoRoot:target,hcAssembly:actual.hc.path,cecilPath:actual.cecil.path});
 assert.equal(d.baseline.root,path.join(target,baselineRelativeRoot));assert.deepEqual(d.baseline.files.map(x=>x.sha256),Object.values(baselinePins));
});
for(const name of Object.keys(baselinePins))test('tampered '+name+' cannot become the old negative control',()=>{
 const target=moved('tamper-'+name);fs.writeFileSync(path.join(target,baselineRelativeRoot,name),'candidate bytes');
 assert.throws(()=>resolveInputs({repoRoot:target}),e=>e.code==='ONEX_BASELINE_IDENTITY');
});
test('self-consistent forged provenance does not authorize changed baseline',()=>{
 const target=moved('forged'),p=path.join(target,baselineRelativeRoot,'provenance.json');const d=JSON.parse(fs.readFileSync(p,'utf8'));
 d.files[0].sha256='0'.repeat(64);fs.writeFileSync(p,JSON.stringify(d));
 assert.throws(()=>resolveInputs({repoRoot:target}),e=>e.code==='ONEX_BASELINE_IDENTITY');
});
test('missing baseline and explicit HC are BLOCKED, changed HC is FAIL',()=>{
 const target=moved('missing');fs.unlinkSync(path.join(target,baselineRelativeRoot,'YeManFanBackend.dll'));
 assert.throws(()=>resolveInputs({repoRoot:target}),blocked('ONEX_BASELINE_MISSING'));
 assert.throws(()=>resolveInputs({repoRoot:repo,hcAssembly:path.join(root,'missing-hc')}),blocked('ONEX_HC_MISSING'));
 const wrong=path.join(root,'wrong-hc');fs.writeFileSync(wrong,'wrong');assert.throws(()=>resolveInputs({repoRoot:repo,hcAssembly:wrong}),e=>e.code==='ONEX_HC_IDENTITY');
});
for(const [name,extra,code]of [
 ['baseline',{legacyRoot:path.join(root,'absent-baseline')},'ONEX_BASELINE_MISSING'],
 ['HC',{hcAssembly:path.join(root,'absent-hc')},'ONEX_HC_MISSING'],
 ['Cecil',{cecilPath:path.join(root,'absent-cecil')},'FAN_OPEN_CECIL_MISSING'],
 ['candidate',{candidateHostRoot:path.join(root,'absent-candidate')},'ONEX_CANDIDATE_MISSING'],
])test('actual runner writes missing '+name+' BLOCKED without compiling',()=>{
 const fixture=name==='candidate'?missingCandidateAfterAdmission():null,options=fixture?.options??extra;
 const out=path.join(root,'blocked-'+name),invoke=()=>execute({...inputFailurePlan(options.repoRoot??repo),evidenceDir:out,...options}),d=fixture?fixture.run(invoke,out):invoke();
 assert.equal(d.status,'BLOCKED');assert.equal(d.code,code);assert.equal(fs.existsSync(path.join(out,'test-build')),false);
 assert.equal(d.hostMainStarted,false);assert.equal(d.installedApplicationMutated,false);
 const files=d.sourceHashes.map(x=>x.path);
 for(const basename of ['YeManFanHost.dll','YeManFanBackend.dll','YeManLightSetter.dll','provenance.json'])assert.ok(files.includes(baselineRelativeRoot+'/'+basename));
});
test('explicit candidate without binding is BLOCKED before verification, compilation or target launch',()=>{
 const out=path.join(root,'candidate-without-binding'),d=execute({...inputFailurePlan(),evidenceDir:out,candidateHostRoot:path.join(root,'absent-unbound-candidate')});
 assert.equal(d.status,'BLOCKED');assert.equal(d.code,'ONEX_CANDIDATE_BINDING_MISSING');
 assert.equal(fs.existsSync(path.join(out,'test-build')),false);assert.equal(fs.existsSync(path.join(out,'generated')),false);
 assert.equal(d.hostMainStarted,false);assert.equal(d.installedApplicationMutated,false);assert.equal(d.hardwareIo,false);
 assert.equal(d.inputEvidence.declaredCandidatePayload,undefined);assert.deepEqual(fs.readdirSync(out),['result.json']);
});
test('actual CLI exposes dependency BLOCKED exit 3',()=>{
 const out=path.join(root,'cli-blocked'),p=spawnSync(process.execPath,[path.join(here,'run.mjs'),'--evidence-dir',out,'--cecil-path',path.join(root,'none')],{windowsHide:true,encoding:'utf8'});
 assert.equal(p.status,3);assert.equal(JSON.parse(p.stdout).status,'BLOCKED');
});
test('static descriptor opts in to BLOCKED and imports without filesystem effects',()=>{
 const descriptor=getOneXFanCoexistenceAdapters()[0];assert.deepEqual(descriptor.blockedExitCodes,[3]);
 const script=`import fs from 'node:fs';for(const key of ['readFileSync','existsSync','statSync','readdirSync','openSync','writeFileSync','mkdirSync'])fs[key]=()=>{throw Error(key)};const m=await import(${JSON.stringify(new URL('./adapter.mjs',import.meta.url).href)});if(m.getOneXFanCoexistenceAdapters()[0].id!=='fan.onex-lighting-coexistence')throw Error('descriptor');console.log('PASS');`;
 const child=spawnSync(process.execPath,['--input-type=module','-e',script],{windowsHide:true,encoding:'utf8'});assert.equal(child.status,0,child.stderr);assert.equal(child.stdout.trim(),'PASS');
});

test('prior a678 write-failure baseline is portable with its actual 6d2b backend',()=>{
 const target=path.join(root,'write-baseline-moved'),dest=path.join(target,writeBaselineRelativeRoot);fs.mkdirSync(path.dirname(dest),{recursive:true});
 fs.cpSync(path.join(repo,writeBaselineRelativeRoot),dest,{recursive:true});
 assert.deepEqual(resolveWriteBaseline(target).files.map(x=>x.sha256),Object.values(writeBaselinePins));
});
for(const name of Object.keys(writeBaselinePins))test('prior write baseline rejects changed '+name,()=>{
 const target=path.join(root,'write-tamper-'+name),dest=path.join(target,writeBaselineRelativeRoot);fs.mkdirSync(path.dirname(dest),{recursive:true});
 fs.cpSync(path.join(repo,writeBaselineRelativeRoot),dest,{recursive:true});fs.writeFileSync(path.join(dest,name),'new candidate bytes');
 assert.throws(()=>resolveWriteBaseline(target),e=>e.code==='ONEX_WRITE_BASELINE_IDENTITY');
});
test('missing prior write baseline is BLOCKED rather than skipping original failures',()=>{
 assert.throws(()=>resolveWriteBaseline(path.join(root,'no-write-baseline')),blocked('ONEX_WRITE_BASELINE_MISSING'));
 const target=path.join(root,'write-provenance-tamper'),dest=path.join(target,writeBaselineRelativeRoot);fs.mkdirSync(path.dirname(dest),{recursive:true});
 fs.cpSync(path.join(repo,writeBaselineRelativeRoot),dest,{recursive:true});const p=path.join(dest,'provenance.json');const d=JSON.parse(fs.readFileSync(p,'utf8'));d.files[0].sha256='f'.repeat(64);fs.writeFileSync(p,JSON.stringify(d));
 assert.throws(()=>resolveWriteBaseline(target),e=>e.code==='ONEX_WRITE_BASELINE_IDENTITY');
});
