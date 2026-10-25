import{MAX_ARTIFACT_INPUTS}from'./runner.mjs';
import test from 'node:test';import assert from 'node:assert/strict';import fs from 'node:fs';import os from 'node:os';import path from 'node:path';import {createHash}from'node:crypto';import{auditReceipts}from'./receipt-audit.mjs';
const sha=s=>createHash('sha256').update(s).digest('hex');
function fixture(t){const root=fs.mkdtempSync(path.join(os.tmpdir(),'ymcc-audit-'));t.after(()=>{assert(path.basename(root).startsWith('ymcc-audit-'));fs.rmSync(root,{recursive:true,force:true});});const repo=path.join(root,'repo');fs.mkdirSync(repo);fs.writeFileSync(path.join(repo,'a.ts'),'actual');return{repo,root,write(name,rows){const run=path.join(root,name);fs.mkdirSync(run,{recursive:true});const data={schemaVersion:1,tool:'ymcc_sim-global',mode:'run',runId:name,results:rows};const file=path.join(run,'result.json');fs.writeFileSync(file,JSON.stringify(data));return file;}};}
const passed=id=>({id,status:'PASS',exitCode:0,sourceInputs:[{path:'a.ts',sha256:sha('actual')}],structuredEvidence:[]});
test('current receipts are scope validation only and execute no tests',t=>{const f=fixture(t);const r=auditReceipts([f.write('one',[passed('a')])],{repoRoot:f.repo});assert.equal(r.status,'CURRENT_RECEIPTS_VALID');assert.equal(r.executedTests,0);assert.equal(r.fullProductAcceptance,false);});
test('changed source produces a precise dirty list; latest partial run preserves omitted cases',t=>{const f=fixture(t),one=f.write('one',[passed('a'),passed('b')]),two=f.write('two',[passed('b')]);fs.writeFileSync(path.join(f.repo,'a.ts'),'changed');const r=auditReceipts([one,two],{repoRoot:f.repo});assert.equal(r.summary.cases,2);assert.deepEqual(r.needsRerun,['a','b']);assert.equal(r.status,'REVALIDATION_REQUIRED');});
test('blocked latest result cannot fall back to historical pass',t=>{const f=fixture(t);const r=auditReceipts([f.write('one',[passed('a')]),f.write('two',[{id:'a',status:'BLOCKED'}])],{repoRoot:f.repo});assert.equal(r.summary.currentPass,0);assert.equal(r.summary.blocked,1);});
test('empty, invalid, traversal and inventory receipts cannot certify anything',t=>{const f=fixture(t);assert.equal(auditReceipts([],{repoRoot:f.repo}).status,'BLOCKED');assert.equal(auditReceipts([f.write('one',[{...passed('a'),id:'../outside'}])],{repoRoot:f.repo}).status,'BLOCKED');const invalid=f.write('two',[]);assert.equal(auditReceipts([invalid],{repoRoot:f.repo}).status,'BLOCKED');});
test('nested JSON cannot escape its own case and contradicting pass is a failure',t=>{const f=fixture(t);const r=auditReceipts([f.write('one',[{...passed('a'),structuredEvidence:[{path:path.join(f.root,'settings.json')}]},{...passed('b'),exitCode:2}])],{repoRoot:f.repo});assert.equal(r.cases[0].status,'BLOCKED');assert.equal(r.cases[1].status,'FAIL');});

test('explicit Host binary identity is rechecked without pretending to rerun CLR compatibility',t=>{
 const f=fixture(t),payload=path.join(f.root,'payload');fs.mkdirSync(payload);
 const binaries=['YeManFanHost.exe','YeManFanHost.dll'].map(name=>{fs.writeFileSync(path.join(payload,name),'approved');return{name,sha256:sha('approved')};});
 const file=f.write('one',[{...passed('a'),scope:'host-mock'}]);const value=JSON.parse(fs.readFileSync(file,'utf8'));value.hostPayload={root:payload,binaries};fs.writeFileSync(file,JSON.stringify(value));
 assert.equal(auditReceipts([file],{repoRoot:f.repo}).status,'CURRENT_RECEIPTS_VALID');
 fs.writeFileSync(path.join(payload,'YeManFanHost.dll'),'changed');const result=auditReceipts([file],{repoRoot:f.repo});
 assert.equal(result.status,'REVALIDATION_REQUIRED');assert.equal(result.binaryCompatibilityReexecuted,false);assert.equal(result.cases[0].sourceMismatches[0].role,'explicit-host-binary');
});

// A stable Host DLL cannot certify a subsequently replaced Backend/WinRT dependency.
test('captured Backend and platform dependency changes expire the Host receipt',t=>{
 const f=fixture(t),payload=path.join(f.root,'payload');fs.mkdirSync(payload);
 const names=['YeManFanHost.exe','YeManFanHost.dll','YeManFanBackend.dll','WinRT.Runtime.dll'];
 const binaries=names.map(name=>{fs.writeFileSync(path.join(payload,name),'approved');return{name,sha256:sha('approved')};});
 const file=f.write('dependencies',[{...passed('a'),scope:'host-mock'}]);
 const value=JSON.parse(fs.readFileSync(file,'utf8'));value.hostPayload={root:payload,binaries};fs.writeFileSync(file,JSON.stringify(value));
 assert.equal(auditReceipts([file],{repoRoot:f.repo}).status,'CURRENT_RECEIPTS_VALID');
 for(const name of ['YeManFanBackend.dll','WinRT.Runtime.dll']){
  fs.writeFileSync(path.join(payload,name),'changed');const result=auditReceipts([file],{repoRoot:f.repo});
  assert.equal(result.status,'REVALIDATION_REQUIRED');assert(result.cases[0].sourceMismatches.some(x=>x.path===name&&x.role==='explicit-host-binary'));
  fs.writeFileSync(path.join(payload,name),'approved');
 }
});

test('explicit runtime, shared and lighting candidate bytes expire an otherwise passing receipt',t=>{
 const f=fixture(t),candidate=path.join(f.root,'YeManLightSetter.dll');fs.writeFileSync(candidate,'approved');
 const file=f.write('candidate',[{...passed('lighting.signal-consumer'),candidateArtifacts:[{path:candidate,sha256:sha('approved'),status:'OK'}]}]);
 assert.equal(auditReceipts([file],{repoRoot:f.repo}).status,'CURRENT_RECEIPTS_VALID');
 for(const action of ['replace','remove']){
  if(action==='replace')fs.writeFileSync(candidate,'replaced');else fs.rmSync(candidate);
  const result=auditReceipts([file],{repoRoot:f.repo});assert.equal(result.status,'REVALIDATION_REQUIRED');
  assert.equal(result.cases[0].sourceMismatches[0].role,'explicit-candidate-artifact');assert.equal(result.executedTests,0);
 }
});
test('contradictory candidate change and malformed artifact receipts never reuse a PASS',t=>{
 const f=fixture(t);
 for(const [index,candidateArtifacts] of [[0,[{path:'relative.dll',sha256:sha('approved')}]],[1,'invalid']]){
  const report=auditReceipts([f.write(`malformed-${index}`,[{...passed('a'),candidateArtifacts}])],{repoRoot:f.repo});
  assert.notEqual(report.status,'CURRENT_RECEIPTS_VALID');
 }
 const report=auditReceipts([f.write('contradictory',[{...passed('a'),candidateArtifactChanged:true}])],{repoRoot:f.repo});assert.equal(report.cases[0].status,'FAIL');
});
// Execution evidence is immutable even when its replacement still says PASS.
function structuredFixture(f,name='structured'){
 const caseDir=path.join(f.root,name,'cases','a');fs.mkdirSync(caseDir,{recursive:true});
 const proof=path.join(caseDir,'proof.json');fs.writeFileSync(proof,JSON.stringify({passed:true,checks:12}));
 const row={...passed('a'),structuredEvidence:[{path:proof,sha256:sha(fs.readFileSync(proof)),failed:false}]};
 return {proof,row,file:f.write(name,[row])};
}
test('unchanged executed structured bytes pass and successful replacements expire the receipt',t=>{
 const f=fixture(t),s=structuredFixture(f);
 assert.equal(auditReceipts([s.file],{repoRoot:f.repo}).status,'CURRENT_RECEIPTS_VALID');
 fs.writeFileSync(s.proof,JSON.stringify({passed:true,checks:1}));
 const report=auditReceipts([s.file],{repoRoot:f.repo});assert.equal(report.status,'REVALIDATION_REQUIRED');
 assert.equal(report.cases[0].status,'STALE_OR_INVALID_EVIDENCE');assert.deepEqual(report.needsRerun,['a']);
 assert(report.cases[0].diagnostics.some(row=>row.code==='STRUCTURED_EVIDENCE_CHANGED_SINCE_EXECUTION'));
});
test('missing or malformed executed evidence hashes cannot be filled from current successful JSON',t=>{
 const f=fixture(t);
 for(const [index,hash]of [undefined,null,'not-a-hash'].entries()){
  const name=`missing-hash-${index}`,s=structuredFixture(f,name);s.row.structuredEvidence[0].sha256=hash;
  const report=auditReceipts([f.write(name,[s.row])],{repoRoot:f.repo});assert.equal(report.status,'REVALIDATION_REQUIRED');
  assert(report.cases[0].diagnostics.some(row=>row.code==='STRUCTURED_EVIDENCE_SCHEMA_INVALID'));
 }
});
test('missing required Host executables with null identities never match missing files',t=>{
 const f=fixture(t),payload=path.join(f.root,'empty-host');fs.mkdirSync(payload);
 for(const [index,hash]of [null,undefined,'invalid'].entries()){
  const file=f.write(`empty-host-${index}`,[{...passed('a'),scope:'host-mock'}]);const value=JSON.parse(fs.readFileSync(file));
  value.hostPayload={root:payload,binaries:['YeManFanHost.exe','YeManFanHost.dll'].map(name=>({name,sha256:hash}))};fs.writeFileSync(file,JSON.stringify(value));
  const report=auditReceipts([file],{repoRoot:f.repo});assert.equal(report.status,'REVALIDATION_REQUIRED');
  assert(report.cases[0].diagnostics.some(row=>row.code==='HOST_PAYLOAD_IDENTITY_MISSING'));
 }
});
test('an optional Host library captured as absent stays valid until a file appears',t=>{
 const f=fixture(t),payload=path.join(f.root,'optional-host');fs.mkdirSync(payload);
 const binaries=['YeManFanHost.exe','YeManFanHost.dll'].map(name=>{fs.writeFileSync(path.join(payload,name),'approved');return{name,sha256:sha('approved')};});
 binaries.push({name:'WinRT.Runtime.dll',sha256:null});const file=f.write('optional-host',[{...passed('a'),scope:'host-mock'}]);const value=JSON.parse(fs.readFileSync(file));value.hostPayload={root:payload,binaries};fs.writeFileSync(file,JSON.stringify(value));
 assert.equal(auditReceipts([file],{repoRoot:f.repo}).status,'CURRENT_RECEIPTS_VALID');
 fs.writeFileSync(path.join(payload,'WinRT.Runtime.dll'),'new');assert.equal(auditReceipts([file],{repoRoot:f.repo}).status,'REVALIDATION_REQUIRED');
});
test('PASS cannot contradict launch, signal, missing-input or build failures',t=>{
 const f=fixture(t);
 for(const [index,flags]of [{launchError:'ENOENT'},{signal:'SIGTERM'},{missingInputs:true},{buildError:true}].entries()){
  const report=auditReceipts([f.write(`contradiction-${index}`,[{...passed('a'),...flags}])],{repoRoot:f.repo});
  assert.equal(report.status,'FAIL');assert.equal(report.cases[0].status,'FAIL');assert(report.cases[0].diagnostics.some(row=>row.code==='CONTRADICTORY_PASS_RECEIPT'));
 }
});
test('duplicate case ids invalidate the whole run instead of erasing its failure',t=>{
 const f=fixture(t),file=f.write('duplicates',[{...passed('a'),status:'FAIL',exitCode:2},passed('a')]);
 const report=auditReceipts([file],{repoRoot:f.repo});assert.equal(report.status,'BLOCKED');assert.equal(report.summary.currentPass,0);
 assert(report.diagnostics.some(row=>row.code==='RUN_RECEIPT_INVALID'));
 // An invalid later run must not silently fall back to the historical pass.
 const merged=auditReceipts([f.write('earlier',[passed('a')]),file],{repoRoot:f.repo});assert.equal(merged.status,'BLOCKED');
});
test('malformed source rows produce reviewable blocked diagnostics instead of throwing',t=>{
 const f=fixture(t);
 for(const [index,input]of [null,123,{}, {path:123,sha256:sha('actual')}].entries()){
  for(const key of ['sourceInputs','dependencyInputs']){
   const file=f.write(`malformed-source-${key}-${index}`,[{...passed('a'),[key]:[input]}]);let report;
   assert.doesNotThrow(()=>{report=auditReceipts([file],{repoRoot:f.repo});});assert.equal(report.cases[0].status,'BLOCKED');
   assert(report.cases[0].diagnostics.some(row=>row.code==='SOURCE_INPUT_SCHEMA_INVALID'));
  }
 }
});
test('malformed structured evidence rows are blocked without throwing',t=>{
 const f=fixture(t);
 for(const [index,proofs]of [null,'not-an-array',[null],[{}]].entries()){
  const file=f.write(`malformed-proof-${index}`,[{...passed('a'),structuredEvidence:proofs}]);let report;
  assert.doesNotThrow(()=>{report=auditReceipts([file],{repoRoot:f.repo});});assert.equal(report.cases[0].status,'BLOCKED');
  assert(report.cases[0].diagnostics.some(row=>row.code==='STRUCTURED_EVIDENCE_SCHEMA_INVALID'));
 }
});
test('a directory named as structured JSON cannot crash the read-only audit',t=>{
 const f=fixture(t),name='directory-proof',caseDir=path.join(f.root,name,'cases','a');fs.mkdirSync(caseDir,{recursive:true});
 const proof=path.join(caseDir,'proof.json');fs.mkdirSync(proof);
 const file=f.write(name,[{...passed('a'),structuredEvidence:[{path:proof,sha256:sha('actual')}]}]);let report;
 assert.doesNotThrow(()=>{report=auditReceipts([file],{repoRoot:f.repo});});assert.equal(report.cases[0].status,'STALE_OR_INVALID_EVIDENCE');
});

test('compiler dependency receipts verify the last member beyond the old512 bound',t=>{
 const f=fixture(t),common=path.join(f.root,'common.dll'),last=path.join(f.root,'last.dll');
 fs.writeFileSync(common,'approved');fs.writeFileSync(last,'approved');
 const candidateArtifacts=[...Array.from({length:570},()=>({path:common,sha256:sha('approved'),status:'OK'})),{path:last,sha256:sha('approved'),status:'OK'}];
 const file=f.write('large-closure',[{...passed('a'),candidateArtifacts}]);
 assert.equal(auditReceipts([file],{repoRoot:f.repo}).cases[0].status,'PASS');
 fs.writeFileSync(last,'changed');const changed=auditReceipts([file],{repoRoot:f.repo});
 assert(changed.cases[0].sourceMismatches.some(x=>x.path===last&&x.role==='explicit-candidate-artifact'));
 fs.writeFileSync(last,'approved');const tooMany=f.write('over-bound',[{...passed('a'),candidateArtifacts:Array(MAX_ARTIFACT_INPUTS+1).fill(candidateArtifacts[0])}]);
 const rejected=auditReceipts([tooMany],{repoRoot:f.repo});assert.equal(rejected.cases[0].status,'BLOCKED');assert(rejected.cases[0].diagnostics.some(x=>x.code==='CANDIDATE_ARTIFACT_SCHEMA_INVALID'));
});


test('more than32 original receipt roots preserve latest partial results without new containers',t=>{
 const f=fixture(t),files=[];
 for(let i=0;i<40;i++)files.push(f.write('original-'+i,[passed('case.'+i)]));
 const final=f.write('latest-partial',[{...passed('case.0'),status:'FAIL',exitCode:1}]);
 const r=auditReceipts([...files,final],{repoRoot:f.repo});
 assert.equal(r.sourceRuns.length,41);assert.equal(r.summary.cases,40);
 assert.equal(r.summary.currentPass,39);assert.equal(r.summary.failed,1);assert.equal(r.status,'FAIL');
 const first=r.cases.find(row=>row.id==='case.0');assert.equal(first.sourceRunId,'latest-partial');
 assert.equal(first.caseDir,path.join(f.root,'latest-partial','cases','case.0'));
 assert.equal(r.cases.find(row=>row.id==='case.39').sourceRunId,'original-39');
 assert.equal(r.executedTests,0);assert.equal(r.fullProductAcceptance,false);
});


test('bounded128 original receipts are accepted and a129th is refused before reading',t=>{
 const f=fixture(t),files=[];
 for(let i=0;i<128;i++)files.push(f.write('bound-'+i,[passed('bound.'+i)]));
 const r=auditReceipts(files,{repoRoot:f.repo});assert.equal(r.status,'CURRENT_RECEIPTS_VALID');
 assert.equal(r.sourceRuns.length,128);assert.equal(r.summary.currentPass,128);assert.equal(r.receiptBytesRead,files.reduce((n,p)=>n+fs.statSync(p).size,0));
 const blocked=auditReceipts([...files,files[0]],{repoRoot:f.repo});assert.equal(blocked.status,'BLOCKED');
 assert.equal(blocked.sourceRuns.length,0);assert.equal(blocked.cases.length,0);assert.equal(blocked.receiptBytesRead,0);
 assert(blocked.diagnostics.some(row=>row.code==='RECEIPT_LIST_REQUIRED_MAX_128'));
});

test('aggregate bytes use actual receipt sizes and never certify a budget-truncated subset',t=>{
 const f=fixture(t),a=f.write('bytes-a',[passed('a')]),b=f.write('bytes-b',[passed('b')]);
 const aBytes=fs.statSync(a).size,total=aBytes+fs.statSync(b).size;
 const exact=auditReceipts([a,b],{repoRoot:f.repo,maxTotalReceiptBytes:total});assert.equal(exact.status,'CURRENT_RECEIPTS_VALID');assert.equal(exact.receiptBytesRead,total);
 const blocked=auditReceipts([a,b],{repoRoot:f.repo,maxTotalReceiptBytes:aBytes});assert.equal(blocked.status,'BLOCKED');assert.equal(blocked.cases.length,0);assert.equal(blocked.sourceRuns.length,1);assert.equal(blocked.receiptBytesRead,aBytes);
 assert(blocked.diagnostics.some(row=>row.code==='RECEIPT_TOTAL_SIZE_LIMIT'));assert.equal(blocked.executedTests,0);
});

test('malformed bytes count toward the aggregate bound and callers cannot raise the fixed budget',t=>{
 const f=fixture(t),a=f.write('bad-json',[passed('a')]),b=f.write('good-json',[passed('b')]);fs.writeFileSync(a,'not json but still bytes read');const size=fs.statSync(a).size;
 const blocked=auditReceipts([a,b],{repoRoot:f.repo,maxTotalReceiptBytes:size});assert.equal(blocked.status,'BLOCKED');assert.equal(blocked.receiptBytesRead,size);assert.equal(blocked.cases.length,0);assert(blocked.diagnostics.some(row=>row.code==='RUN_RECEIPT_INVALID'));assert(blocked.diagnostics.some(row=>row.code==='RECEIPT_TOTAL_SIZE_LIMIT'));
 for(const limit of [0,-1,1.5,Infinity,NaN,512*1024*1024+1]){const r=auditReceipts([b],{repoRoot:f.repo,maxTotalReceiptBytes:limit});assert.equal(r.status,'BLOCKED');assert.equal(r.receiptBytesRead,0);assert(r.diagnostics.some(row=>row.code==='RECEIPT_TOTAL_SIZE_LIMIT_INVALID'));}
});

test('the original single-file32MiB refusal remains bounded before JSON parsing',t=>{
 const f=fixture(t),file=f.write('oversize',[passed('a')]);fs.truncateSync(file,32*1024*1024+1);
 const r=auditReceipts([file],{repoRoot:f.repo});assert.equal(r.status,'BLOCKED');assert.equal(r.receiptBytesRead,0);assert.equal(r.cases.length,0);assert(r.diagnostics.some(row=>row.code==='RUN_RECEIPT_INVALID'));
});


test('BOM JSON receipts retain exact byte budgeting and original-container identity',t=>{
 const f=fixture(t),file=f.write('utf8-bom',[passed('a')]),json=fs.readFileSync(file);fs.writeFileSync(file,Buffer.concat([Buffer.from([0xef,0xbb,0xbf]),json]));
 const original=sha(fs.readFileSync(file)),bytes=fs.statSync(file).size,r=auditReceipts([file],{repoRoot:f.repo,maxTotalReceiptBytes:bytes});
 assert.equal(r.status,'CURRENT_RECEIPTS_VALID');assert.equal(r.receiptBytesRead,bytes);assert.equal(r.sourceRuns[0].sha256,original);assert.equal(sha(fs.readFileSync(file)),original);
 assert.equal(auditReceipts([file],{repoRoot:f.repo,maxTotalReceiptBytes:bytes-1}).status,'BLOCKED');
});

test('all original1024 distinct compiler artifacts still check925 and1024 with the shared upper bound',t=>{
 const f=fixture(t),artifacts=[];
 for(let n=0;n<1024;n++){const file=path.join(f.root,'compiler-'+n+'.dll');fs.writeFileSync(file,'reviewed-'+n);artifacts.push({path:file,sha256:sha('reviewed-'+n),status:'OK'});}
 const receipt=f.write('actual-combined-tool-unit-fixture',[{...passed('a'),candidateArtifacts:artifacts}]);
 assert.equal(auditReceipts([receipt],{repoRoot:f.repo}).cases[0].status,'PASS');
 for(const index of [924,1023]){
  fs.writeFileSync(artifacts[index].path,'changed-at-'+index);
  const changed=auditReceipts([receipt],{repoRoot:f.repo});
  assert.equal(changed.cases[0].status,'STALE_SOURCE');
  assert(changed.cases[0].sourceMismatches.some(row=>row.path===artifacts[index].path&&row.role==='explicit-candidate-artifact'));
  fs.writeFileSync(artifacts[index].path,'reviewed-'+index);
 }
 assert.equal(auditReceipts([receipt],{repoRoot:f.repo}).cases[0].status,'PASS');
 const over=f.write('over1024-unit-fixture',[{...passed('a'),candidateArtifacts:[...artifacts,...Array(MAX_ARTIFACT_INPUTS+1-artifacts.length).fill(artifacts[0])]}]);
 const refused=auditReceipts([over],{repoRoot:f.repo});assert.equal(refused.cases[0].status,'BLOCKED');
 assert(refused.cases[0].diagnostics.some(row=>row.code==='CANDIDATE_ARTIFACT_SCHEMA_INVALID'));
});

test('mounted native Host UI closures verify every3072 member including actual2375 boundary and reject3073',t=>{
 const f=fixture(t),artifacts=[];
 for(let index=0;index<MAX_ARTIFACT_INPUTS;index++){
  const file=path.join(f.root,'full-ui-tool-'+index+'.dll');fs.writeFileSync(file,'owned-tool-'+index);artifacts.push({path:file,sha256:sha('owned-tool-'+index),status:'OK'});
 }
 const original=f.write('actual3072-owned-tool-unit-fixture',[{...passed('a'),candidateArtifacts:artifacts}]);
 assert.equal(auditReceipts([original],{repoRoot:f.repo}).cases[0].status,'PASS');
 const private2375=f.write('actual2375-count-owned-tool-unit-fixture',[{...passed('a'),candidateArtifacts:artifacts.slice(0,2375)}]);
 assert.equal(auditReceipts([private2375],{repoRoot:f.repo}).cases[0].status,'PASS');
 for(const index of [2374,MAX_ARTIFACT_INPUTS-1]){
  fs.writeFileSync(artifacts[index].path,'source-tool-tampered-'+index);const changed=auditReceipts([original],{repoRoot:f.repo});assert.equal(changed.cases[0].status,'STALE_SOURCE');assert(changed.cases[0].sourceMismatches.some(row=>row.path===artifacts[index].path&&row.role==='explicit-candidate-artifact'));
  fs.writeFileSync(artifacts[index].path,'owned-tool-'+index);
 }
 assert.equal(auditReceipts([original],{repoRoot:f.repo}).cases[0].status,'PASS');
 const over=f.write('over3072-owned-tool-unit-fixture',[{...passed('a'),candidateArtifacts:[...artifacts,artifacts[0]]}]);const denied=auditReceipts([over],{repoRoot:f.repo});assert.equal(denied.cases[0].status,'BLOCKED');assert(denied.cases[0].diagnostics.some(row=>row.code==='CANDIDATE_ARTIFACT_SCHEMA_INVALID'));
});
