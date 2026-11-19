import assert from 'node:assert/strict';
import test from 'node:test';
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {auditReceipts,auditRegisteredScope} from './receipt-audit.mjs';
import {executeAdapter,MAX_ARTIFACT_INPUTS} from './runner.mjs';
import {simulationCorePath} from './release-host-environment.mjs';
import tsFixture from './registered-artifact-closure.fixture.json' with {type:'json'};

// Only inert owned files execute. The saved TS proof is historical real audit data,
// used solely at the pure registered-scope boundary; no verifier is substituted.
const retainedParent=process.env.YMCC_ARTIFACT_CLOSURE_EVIDENCE;
const prefix='ymcc-artifact-registration-',source='inert-entry.mjs';
let scratch,run,withArtifacts;
function directoryIdentity(dir){
 const stat=fs.lstatSync(dir,{bigint:true});
 assert(stat.isDirectory()&&!stat.isSymbolicLink()&&path.relative(dir,fs.realpathSync.native(dir))==='','Redirected artifact closure directory: '+dir);
 return stat;
}
function scratchRoot(){
 if(!scratch){
  const parent=retainedParent?path.resolve(retainedParent):simulationCorePath('Workspace'),parentIdentity=directoryIdentity(parent);
  const root=fs.mkdtempSync(path.join(parent,prefix));
  scratch={parent,parentIdentity,root,rootIdentity:directoryIdentity(root)};
 }
 return scratch.root;
}
test.after(()=>{
 if(!scratch||retainedParent)return;
 const {parent,parentIdentity,root,rootIdentity}=scratch;
 assert.equal(path.relative(parent,path.dirname(root)),'');assert(path.basename(root).startsWith(prefix));
 for(const [dir,expected] of [[parent,parentIdentity],[root,rootIdentity]]){
  const stat=directoryIdentity(dir);assert(stat.dev===expected.dev&&stat.ino===expected.ino,'Artifact closure directory identity changed: '+dir);
 }
 fs.rmSync(root,{recursive:true});
});
function ownedRun(){
 if(!run){
  const root=scratchRoot(),repo=path.join(root,'repo'),runDir=path.join(root,'run');
  fs.mkdirSync(repo);fs.mkdirSync(runDir);fs.writeFileSync(path.join(repo,source),"export {};\n",{flag:'wx'});
  run={root,repo,runDir};
 }
 return run;
}
const hash=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const descriptorFor=(id,artifactInputs)=>({id,domain:'fixture',profiles:['desktop','full'],kind:'node',path:source,args:[],scope:'behavioral-mock',timeoutMs:1500,evidenceInputs:[],artifactInputs});
async function execute(descriptor){
 const {repo,runDir}=ownedRun(),result=await executeAdapter(descriptor,{repoRoot:repo,runDir});
 assert.equal(result.status,'PASS',JSON.stringify(result));return result;
}
async function recordArtifacts(){
 const {root,repo}=ownedRun(),artifactA=path.join(root,'declared-a.bin'),artifactB=path.join(root,'declared-b.bin'),foreign=path.join(root,'foreign-owner','same-bytes.bin');
 fs.writeFileSync(artifactA,'owned tiny artifact\n',{flag:'wx'});fs.writeFileSync(artifactB,'second tiny artifact\n',{flag:'wx'});
 fs.mkdirSync(path.dirname(foreign));fs.copyFileSync(artifactA,foreign,fs.constants.COPYFILE_EXCL);
 const descriptor=descriptorFor('fixture.node-artifacts',[artifactA,artifactB]),executed=await execute(descriptor);
 return {root,repo,descriptor,executed,artifactA,artifactB,foreign,foreignRow:{...executed.candidateArtifacts[0],path:foreign}};
}
function artifactTest(name,body){
 test(name,async()=>body(await (withArtifacts??=recordArtifacts())));
}
function audit(name,item,adapter){
 const {root,repo,runDir}=ownedRun();
 const receipt=path.join(runDir,name+'.json');fs.writeFileSync(receipt,JSON.stringify({schemaVersion:1,tool:'ymcc_sim-global',mode:'run',runId:name,repoRoot:repo,results:[item]},null,2)+'\n',{flag:'wx'});
 const plain=auditReceipts([receipt],{repoRoot:repo}),before=structuredClone(plain),scoped=auditRegisteredScope(plain,[adapter],{profile:'desktop'});
 assert.deepEqual(plain,before,'registration checking must not mutate the underlying audit');
 fs.writeFileSync(path.join(root,name+'-audit.json'),JSON.stringify({receipt,plain,scoped},null,2)+'\n',{flag:'wx'});
 return {plain,scoped,receipt};
}
const scenarios=[
 {name:'missing-one',change:item=>item.candidateArtifacts.pop(),missing:['artifactB'],extra:[]},
 {name:'extra-same-bytes',change:(item,{foreignRow})=>item.candidateArtifacts.push(foreignRow),missing:[],extra:['foreign']},
 {name:'foreign-source-same-count',change:(item,{foreignRow})=>item.candidateArtifacts[0]=foreignRow,missing:['artifactA'],extra:['foreign']},
 {name:'duplicate-executed-same-count',change:item=>item.candidateArtifacts[1]={...item.candidateArtifacts[0]},missing:['artifactB'],extra:[],duplicateExecuted:'artifactA'},
 {name:'omitted-executed-artifact-list',change:item=>delete item.candidateArtifacts,missing:['artifactA','artifactB'],extra:[]},
 {name:'current-declaration-extra',change:()=>{},declaration:['artifactA','artifactB','foreign'],missing:['foreign'],extra:[]},
 {name:'current-declaration-duplicate',change:()=>{},declaration:['artifactA','artifactB','artifactB'],missing:[],extra:[],duplicateDeclared:'artifactB'},
];
artifactTest('registered artifact closure retains only hash-validated candidate rows for later exact registration comparison',({artifactA,foreign,descriptor,executed})=>{
 const {plain,scoped}=audit('complete-control',executed,descriptor);assert.equal(scoped.status,'CURRENT_RECEIPTS_VALID');assert.equal(scoped.registeredScope.complete,true);
 assert.deepEqual(plain.cases[0].execution.candidateArtifacts,executed.candidateArtifacts.map(({path,sha256})=>({path,sha256})));
 assert.equal(hash(artifactA),hash(foreign));assert.notEqual(artifactA,foreign);
});
for(const scenario of scenarios)artifactTest('registered artifact closure rejects '+scenario.name+' with exact differences',fixture=>{
 const {executed,descriptor}=fixture,member=key=>fixture[key];
 const item=structuredClone(executed);scenario.change(item,fixture);
 const adapter={...descriptor,artifactInputs:scenario.declaration?.map(member)??descriptor.artifactInputs};
 const {plain,scoped}=audit(scenario.name,item,adapter);
 assert.equal(plain.status,'CURRENT_RECEIPTS_VALID','individual hash validity alone does not prove declaration closure');
 assert.equal(scoped.status,'REVALIDATION_REQUIRED');assert.equal(scoped.cases[0].status,'STALE_REGISTRATION');assert.equal(scoped.registeredScope.complete,false);
 assert.equal(scoped.summary.currentPass,0);assert.equal(scoped.summary.stale,1);assert.deepEqual(scoped.needsRerun,[descriptor.id]);
 const difference=scoped.cases[0].registrationMismatches.find(row=>row.code==='REGISTERED_ARTIFACTS_CHANGED');assert(difference);
 assert.deepEqual(difference.missing,scenario.missing.map(member));assert.deepEqual(difference.extra,scenario.extra.map(member));
 if(scenario.duplicateExecuted)assert(difference.duplicateExecuted.some(row=>row.path===member(scenario.duplicateExecuted)));
 if(scenario.duplicateDeclared)assert(difference.duplicateDeclared.some(row=>row.path===member(scenario.duplicateDeclared)));
});
artifactTest('registered artifact closure compares the same PowerShell explicit-artifact receipt semantics without starting PowerShell',({executed,descriptor,artifactB})=>{
 const item=structuredClone(executed);item.candidateArtifacts.pop();const {scoped}=audit('powershell-membership',item,{...descriptor,kind:'powershell'});
 assert.equal(scoped.cases[0].status,'STALE_REGISTRATION');assert.deepEqual(scoped.cases[0].registrationMismatches[0].missing,[artifactB]);
});
artifactTest('registered artifact closure accepts order changes without removing any actual member',({executed,descriptor})=>{
 const item=structuredClone(executed);item.candidateArtifacts.reverse();const {scoped}=audit('permuted-order',item,descriptor);
 assert.equal(scoped.status,'CURRENT_RECEIPTS_VALID');assert.equal(scoped.registeredScope.complete,true);
});
test('registered artifact closure accepts actually empty execution and omitted or empty current declaration',async()=>{
 const emptyDescriptor=descriptorFor('fixture.empty-artifacts',[]);
 const emptyExecuted=await execute(emptyDescriptor);
 for(const [name,inputs]of [['empty-explicit',[]],['empty-omitted',undefined]]){
  const {plain,scoped}=audit(name,emptyExecuted,{...emptyDescriptor,artifactInputs:inputs});
  assert.equal(plain.status,'CURRENT_RECEIPTS_VALID');assert.equal(scoped.registeredScope.complete,true);
 }
});
artifactTest('registered artifact closure rejects relative or over-budget current declarations without relaxing 3072',({executed,descriptor,artifactA})=>{
 assert.equal(MAX_ARTIFACT_INPUTS,3072);
 for(const [name,inputs]of [['relative-declaration',['declared-a.bin']],['over-budget-declaration',Array(MAX_ARTIFACT_INPUTS+1).fill(artifactA)]]){
  const {scoped}=audit(name,executed,{...descriptor,artifactInputs:inputs});assert.equal(scoped.cases[0].status,'STALE_REGISTRATION');
  const difference=scoped.cases[0].registrationMismatches.find(row=>row.code==='REGISTERED_ARTIFACTS_CHANGED');
  assert(difference.invalidDeclared.length||difference.declaredSchemaInvalid);
 }
});
artifactTest('registered artifact closure preserves a hash mismatch as STALE_SOURCE and never exposes unvalidated artifacts',({executed,descriptor})=>{
 const item=structuredClone(executed);item.candidateArtifacts[0].sha256='0'.repeat(64);const {plain,scoped}=audit('artifact-hash-refusal',item,descriptor);
 assert.equal(plain.cases[0].status,'STALE_SOURCE');assert.equal(scoped.cases[0].status,'STALE_SOURCE');
 assert.equal(plain.cases[0].execution.candidateArtifacts,undefined);
});
artifactTest('registered artifact closure preserves the absolute product-source fence for same-byte foreign files',({executed,descriptor,root,repo})=>{
 const item=structuredClone(executed),outside=path.join(root,'outside-source.mjs');fs.copyFileSync(path.join(repo,source),outside,fs.constants.COPYFILE_EXCL);item.sourceInputs[0]={...item.sourceInputs[0],path:outside};
 const {scoped}=audit('absolute-source-refusal',item,descriptor);assert.equal(scoped.cases[0].status,'STALE_SOURCE');assert.equal(scoped.registeredScope.complete,false);
});
artifactTest('registered artifact closure keeps the narrow bundled Node authority refusal intact',({executed,descriptor,root,repo})=>{
 const item=structuredClone(executed);item.bundle={path:path.join(root,'unverified-entry.mjs'),sha256:hash(path.join(repo,source))};
 const {plain,scoped}=audit('bundled-node-no-authority',item,descriptor);assert.equal(plain.cases[0].status,'BLOCKED');assert.equal(scoped.cases[0].status,'BLOCKED');
 assert(plain.cases[0].diagnostics.some(row=>row.code==='BUNDLED_NODE_TOOL_AUTHORITY_INVALID'&&/AUTHORITY_REQUIRED_RERUN/.test(row.reason)));
});
artifactTest('registered artifact closure keeps TypeScript bundle receipts without original authority blocked',({executed,descriptor,root,repo})=>{
 const ts='inert-source.ts';fs.writeFileSync(path.join(repo,ts),'export {};\n',{flag:'wx'});const item={...structuredClone(executed),source:ts,sourceInputs:[{path:ts,sha256:hash(path.join(repo,ts))}],compilerKind:'typescript-esbuild-bundle',bundle:{path:path.join(root,'unverified-ts-entry.mjs')}};
 const {scoped}=audit('typescript-no-authority',item,{...descriptor,kind:'typescript',path:ts});assert.equal(scoped.cases[0].status,'BLOCKED');
 assert(scoped.cases[0].diagnostics.some(row=>row.code==='BUNDLED_NODE_TOOL_AUTHORITY_INVALID'&&/AUTHORITY_REQUIRED_RERUN/.test(row.reason)));
});
function tsReport(row=tsFixture.row){return {status:'CURRENT_RECEIPTS_VALID',cases:[structuredClone(row)],diagnostics:[],needsRerun:[],summary:{cases:1,currentPass:1,stale:0,blocked:0,failed:0},executedTests:0,fullProductAcceptance:false,physicalHardwareAcceptance:false};}
const tsDescriptor={id:tsFixture.row.id,domain:'fixture',kind:'typescript',profiles:['desktop'],path:tsFixture.row.execution.source,scope:tsFixture.row.execution.scope,evidenceInputs:[],artifactInputs:[]};
test('registered artifact closure compares only original explicit TS members after the real saved 26-package/749-file proof',()=>{
 const before=tsReport(),copy=structuredClone(before);assert.equal(before.cases[0].execution.bundledToolProof.packageCount,26);assert.equal(before.cases[0].execution.bundledToolProof.fileCount,749);assert.equal(before.cases[0].execution.candidateArtifacts.length,753);
 const scoped=auditRegisteredScope(before,[tsDescriptor],{profile:'desktop'});assert.equal(scoped.status,'CURRENT_RECEIPTS_VALID');assert.equal(scoped.registeredScope.complete,true);assert.deepEqual(before,copy);
 if(retainedParent)fs.writeFileSync(path.join(scratchRoot(),'real-prevalidated-ts-explicit-only.json'),JSON.stringify({provenance:tsFixture.provenance,scoped},null,2)+'\n',{flag:'wx'});
});
test('registered artifact closure cannot promote an automatic TS tool into an originally explicit declaration',()=>{
 const required=tsFixture.row.execution.candidateArtifacts[0].path;const scoped=auditRegisteredScope(tsReport(),[{...tsDescriptor,artifactInputs:[required]}],{profile:'desktop'});
 assert.equal(scoped.cases[0].status,'STALE_REGISTRATION');const difference=scoped.cases[0].registrationMismatches.find(row=>row.code==='REGISTERED_EXPLICIT_ARTIFACTS_CHANGED');assert.deepEqual(difference.missing,[required]);assert.deepEqual(difference.extra,[]);
});
test('registered artifact closure never accepts a TS scope after its verified authority proof is removed',()=>{
 const before=tsReport();delete before.cases[0].execution.bundledToolProof;const scoped=auditRegisteredScope(before,[tsDescriptor],{profile:'desktop'});
 assert.equal(scoped.cases[0].status,'STALE_REGISTRATION');assert(scoped.cases[0].registrationMismatches.some(row=>row.code==='REGISTERED_BUNDLED_AUTHORITY_NOT_EXECUTED'));
});
