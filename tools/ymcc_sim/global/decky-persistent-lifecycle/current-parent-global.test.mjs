// Finite receipt/coverage controls only. No native/JS business child is started.
import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {validateCurrentEnvelope,validateBaselineReceipt,aggregateSeven,coverageMap,allocateBudget,sourceMap,naturalExit,verifyCurrentDisk,parentDeclaration,legacyMigration,EXACT_25,REQUIREMENTS,JS_REGRESSIONS,NATIVE_REGRESSIONS,BUDGETS,currentChildToolPins} from './current-parent-global.mjs';
const repo=path.resolve(import.meta.dirname,'../../../..');
const core=path.resolve(repo,'../../../Testing/YMCC');
const evidence=path.join(core,'Evidence/DeckyParentMigration-20261023/c3-e02b');
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const hash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const seal=read(path.join(evidence,'HANDOFF.json'));
assert.equal(hash(seal.actualEvidence.run.path),seal.actualEvidence.run.sha256,'ORIGINAL_R5_RECEIPT_CHANGED');
const original=read(seal.actualEvidence.run.path),clone=()=>structuredClone(original);
const processReceipt=()=>({exitCode:2,elapsedMs:40000,timeoutMs:285000,error:null,signal:null,timedOut:false,outputTruncated:false});
const controls=[];
function check(name,fn){test(name,()=>{fn();controls.push({name,passed:true});});}
check('original immutable r5 25/7 INCOMPLETE envelope is accepted only as selected child',()=>{
 assert.equal(validateCurrentEnvelope(original,processReceipt()),original);assert.equal(original.passed,false);assert.equal(original.status,'INCOMPLETE');assert.equal(EXACT_25.length,25);
});
for(const [name,edit]of [
 ['child exit0 cannot masquerade as expected exit2',(v,p)=>p.exitCode=0],
 ['child exit1 FAIL is retained',(v,p)=>p.exitCode=1],
 ['native access violation exit is rejected',(v,p)=>p.exitCode=3221225477],
 ['signal is never accepted',(v,p)=>p.signal='SIGTERM'],
 ['timeout is never accepted',(v,p)=>p.timedOut=true],
 ['truncated output is never accepted',(v,p)=>p.outputTruncated=true],
 ['scope status PASS cannot replace original INCOMPLETE',(v)=>v.status='PASS'],
 ['partial24 cannot pass with aggregate counts',(v)=>v.cases.pop()],
 ['extra unexpected failed case cannot be masked',(v)=>v.cases.push({id:'extra.failure',passed:false})],
 ['original negative FAIL remains FAIL',(v)=>v.cases[8].passed=false],
 ['duplicated original case cannot replace another',(v)=>v.cases[3]=v.cases[2]],
 ['unreviewed budget expansion is rejected',(v)=>v.originalBudgets.nativeMs=46000],
 ['missing seventh requirement is rejected',(v)=>v.requirements.pop()],
 ['changed requirement mapping is rejected',(v)=>v.requirements[0].caseIds.pop()],
 ['raw observation mutation is rejected',(v)=>v.requirements[0].observations[0].measurements={fake:true}],
 ['failed native subphase is rejected',(v)=>v.executions[2].exitCode=1],
 ['source drift is rejected',(v)=>{v.sourceUnchanged=false;v.sourceChanges=['native/main.cpp'];}],
 ['unexpected top error is not swallowed',(v)=>v.error='unexpected']
])check(name,()=>{const value=clone(),p=processReceipt();edit(value,p);assert.throws(()=>validateCurrentEnvelope(value,p));});
check('seven independent measurement aggregations retain case IDs and evidence scopes',()=>{
 const result=aggregateSeven(original);assert.equal(result.cases.length,7);assert.equal(new Set(result.cases.map(c=>c.id)).size,7);
 assert.deepEqual(result.observations.map(r=>r.requirement),REQUIREMENTS.map(r=>r[0]));
 assert.equal(result.cases[1].measurements.fallbackOwnedOnly,true);assert.equal(result.cases[6].measurements.ownedInertLoaderLaunches,12);
 assert.equal(result.cases[4].measurements.healthObserved,true);assert.equal(result.cases[4].measurements.committedReady.healthObserved,false);
});
for(const [name,id,edit]of [
 ['nonzero resource cannot become measured zero','native.exit-clears-owned-process-job-worker-event',m=>m.atExit.handles=1],
 ['thread count inequality cannot be waived','native.crash-budget-cannot-renew-on-recovery-or-unknown',m=>m.parentThreadsAfter=1],
 ['live sentinel in target Job rejects owned-only fallback','native.job-stop-failure-falls-back-to-owned-job-close',m=>m.sentinelInRuntimeJob=true],
 ['killed descendant by direct terminate is rejected','native.job-stop-failure-falls-back-to-owned-job-close',m=>m.directDescendantTerminateProcessCalls=1],
 ['fake fifth crash count cannot replace real five','native.crash-budget-cannot-renew-on-recovery-or-unknown',m=>m.attempts=4],
 ['actual crash exit-code mutation rejects budget proof','native.crash-budget-cannot-renew-on-recovery-or-unknown',m=>m.ownedInertProcesses[0].exitCode=0],
 ['late process can never become zero restarts','native.late-actual-recovery-callback-after-exit',m=>m.additionalStarts=1],
 ['connected-only cannot replace current health','consumer.sleep-rejects-write-and-automatically-publishes-current-ready',m=>m.healthObserved=false]
])check(name,()=>{const value=clone();edit(value.cases.find(r=>r.id===id).measurements);assert.throws(()=>aggregateSeven(value));});
check('actual existing frozen raw/compile graphs/exes validate; no business reexecution',()=>{
 const toolMap=new Map(original.toolArtifacts.map(r=>[path.resolve(r.path).toLowerCase(),r]));const sources=sourceMap(original.sourceInputs);
 const root=path.dirname(seal.actualEvidence.run.path),work=path.dirname(path.dirname(original.currentProducer.raw.path));
 const proofs=verifyCurrentDisk(original,sources,toolMap,root,work);assert(proofs.length>=5);
});
check('child before-bind cannot be replaced by missing or post-hoc source digest',()=>{
 const map=sourceMap(original.sourceInputs);map.delete('native/decky_sidebar_runtime.h');
 assert.throws(()=>verifyCurrentDisk(original,map,new Map(original.toolArtifacts.map(r=>[path.resolve(r.path).toLowerCase(),r])),path.dirname(seal.actualEvidence.run.path),path.dirname(path.dirname(original.currentProducer.raw.path))));
});
check('single 480s deadline clips remaining allowance without resetting child budget',()=>{
 assert.deepEqual(allocateBudget(1000,2000,240000),{originalMs:240000,timeoutMs:240000,parentRemainingMs:479000,clipped:false});
 assert.equal(allocateBudget(1000,470000,240000).timeoutMs,11000);assert.throws(()=>allocateBudget(1000,481000,60000));
 assert.throws(()=>naturalExit({exitCode:0,elapsedMs:1001,timeoutMs:1000},0));
});
const fakeHash='a'.repeat(64),fakePath='native/baseline-control-only.h';
function baselineControl(){return {raw:{fieldRegression:'decky-update-disconnect-20261020',nativeChecks:1,passed:true,inputsUnchanged:true,sourceInputs:[{path:fakePath,sha256:fakeHash}],realSteamActions:0,userSettingsWrites:0,hardwareWrites:0},process:{id:'original.plugins.decky-update-recovery',exitCode:0,elapsedMs:123,timeoutMs:240000,originalBudgetMs:240000,boundSourceInputs:[{path:fakePath,sha256:fakeHash}]},before:new Map([[fakePath,fakeHash]])};}
check('baseline schema control only accepts exact original successful source-bound budget',()=>{const c=baselineControl();validateBaselineReceipt(c.raw,c.process,c.before,[fakePath]);});
for(const [name,edit]of [
 ['baseline original FAIL cannot be reused',c=>c.raw.passed=false],
 ['baseline old source cannot be reused',c=>c.raw.sourceInputs[0].sha256='b'.repeat(64)],
 ['baseline missing before-binding proof is rejected',c=>c.process.boundSourceInputs=[]],
 ['baseline missing original elapsed/process result is rejected',c=>delete c.process.elapsedMs],
 ['baseline clipped or expanded 240s contract is rejected',c=>c.process.timeoutMs=240001],
 ['baseline top fields cannot hide source-change',c=>c.raw.inputsUnchanged=false],
 ['baseline nonzero exit cannot be reused',c=>c.process.exitCode=1],
 ['baseline undeclared actual source is rejected',c=>c.raw.sourceInputs.push({path:'new.h',sha256:fakeHash})]
])check(name,()=>{const c=baselineControl();edit(c);assert.throws(()=>validateBaselineReceipt(c.raw,c.process,c.before,[fakePath]));});
check('source traversal and conflicting duplicate identity rejected',()=>{
 assert.throws(()=>sourceMap([{path:'../outside',sha256:fakeHash}]));
 assert.throws(()=>sourceMap([{path:'safe.h',sha256:fakeHash},{path:'safe.h',sha256:'b'.repeat(64)}],{duplicates:true}));
});
check('all old47 rows mapped while old three failures stay immutable',()=>{
 const old=read(path.join(core,'Evidence/DeckyParentMigration-20261023/a1-2547/history/result.json'));
 const before=structuredClone(old);const aggregate=aggregateSeven(original);const mapping=coverageMap(old,[...original.cases,...aggregate.cases,{id:'current.native.entry',passed:true}]);
 assert.equal(mapping.length,47);assert.deepEqual(old,before);assert.equal(mapping.filter(r=>r.oldPassed===false).length,3);
 assert(mapping.every(r=>r.originalFailurePromoted===false));assert.equal(mapping[24].relation,'HISTORICAL_CALLBACK_FAIL_RETAINED_TYPED_COUNTERPARTS_SEPARATE');
 assert.equal(mapping[0].currentCases[0].status,'NOT_RUN');const invalid=structuredClone(old);invalid.cases[24].passed=true;assert.throws(()=>coverageMap(invalid,[]));
});
check('candidate declaration binds vendor lock, exact regressions/tools and unchanged adapter budget',()=>{
 const plan=parentDeclaration(repo);assert(plan.evidenceInputs.includes('decky-plugin/vendor/decky-loader.lock.json'));for(const source of ["tools/decky_update_release_gate.ps1", "tools/decky-loader-upgrade.ps1", "decky-plugin/plugin.json", "decky-plugin/vendor/DECKY-API-LOCK.json", "decky-plugin/vendor/decky-api-1.1.3/dist/index.js", "decky-plugin/vendor/decky-api-1.1.3/dist/types.js", "decky-plugin/vendor/decky-api-1.1.3/LICENSE", "decky-plugin/vendor/decky-api-1.1.3/package.json"])assert(plan.evidenceInputs.includes(source),'metadata and complete official SDK closure must be prebound: '+source);assert(plan.evidenceInputs.includes('tools/ymcc_sim/global/capability-lifecycle/index.mjs'));
 const workflows=fs.readdirSync(path.join(repo,'.github/workflows')).filter(name=>/\.ya?ml$/.test(name));assert(workflows.includes('release.yml'));for(const name of workflows)assert(plan.evidenceInputs.includes('.github/workflows/'+name),'actual publication workflow source must be prebound: '+name);
 assert.equal(plan.timeoutMs,480000);assert(plan.artifactInputs.length<=3072);assert.equal(JS_REGRESSIONS.length,6);assert.equal(NATIVE_REGRESSIONS.length,6);
 assert.equal(BUDGETS.baseline,240000);assert.equal(plan.requiredComponentsAreNotMutated,true);assert(plan.resultFiles.includes('decky-persistent-lifecycle/result.json'));
 const gaps=legacyMigration(repo);assert(gaps.length>0&&gaps.every(g=>g.promotionAllowed===false&&g.originalCompileOrExecutionFailureNeverMasked));
});
test.after(()=>{
 if(process.env.YMCC_PARENT_GLOBAL_CONTROL_RESULT){const output=path.resolve(process.env.YMCC_PARENT_GLOBAL_CONTROL_RESULT);assert(output.startsWith(path.join(core,'Evidence/DeckyParentGlobal-20261023')+path.sep));fs.writeFileSync(output,JSON.stringify({schemaVersion:1,scope:'FINITE_RECEIPT_REJECTION_AND_MAPPING_CONTROLS_ONLY',businessExecuted:false,parentExecuted:false,baseline240sExecuted:false,controls,completedControls:controls.length,originalR5Receipt:seal.actualEvidence.run},null,2)+'\n');}
});
check('current child exact tool scope excludes parent-only package compiler inputs',()=>{
 const a=path.join(core,'Workspace','inert-current-compiler.exe'),b=path.join(core,'Workspace','inert-parent-package-compiler.exe');
 const pin={path:a,sha256:'a'.repeat(64)},extra={path:b,sha256:'b'.repeat(64)};
 const all=new Map([[path.resolve(a).toLowerCase(),pin],[path.resolve(b).toLowerCase(),extra]]);
 const selected=currentChildToolPins({currentChildArtifactInputs:[a]},all);assert.equal(selected.size,1);assert.equal(selected.get(path.resolve(a).toLowerCase()),pin);assert(!selected.has(path.resolve(b).toLowerCase()));assert.equal(all.size,2);
});
check('current tool scope cannot admit unknown paths, duplicates or fall back to the union',()=>{
 const p=path.join(core,'Workspace','inert-one.exe'),map=new Map([[path.resolve(p).toLowerCase(),{path:p,sha256:'a'.repeat(64)}]]);
 for(const values of [[],[p,p],[p+'-missing']])assert.throws(()=>currentChildToolPins({currentChildArtifactInputs:values},map));
 assert.throws(()=>currentChildToolPins({},map));
});
check('real parent pins the package compiler while native child keeps its original explicit scope',()=>{
 const d=parentDeclaration(repo),compiler='c:/windows/microsoft.net/framework64/v4.0.30319/csc.exe';
 assert(d.artifactInputs.some(p=>path.resolve(p).replaceAll('\\','/').toLowerCase()===compiler));
 assert(!d.currentChildArtifactInputs.some(p=>path.resolve(p).replaceAll('\\','/').toLowerCase()===compiler));
 assert(d.currentChildArtifactInputs.length>0&&d.artifactInputs.length<=3072);
});
test('private mutation of the compiler manifest is refused before any compilation',async()=>{
 const root=path.join(core,'Workspace/DeckyPackageBudgetIntegration-20261023');fs.mkdirSync(root,{recursive:true});const temp=fs.mkdtempSync(path.join(root,'authority-negative-'));
 const modulePath=path.join(repo,'tools/ymcc_sim/global/decky-update-recovery/package-fixture-tools.mjs'),manifest=path.join(repo,'tools/ymcc_sim/global/decky-update-recovery/package-fixture-tools.json');
 fs.copyFileSync(modulePath,path.join(temp,'package-fixture-tools.mjs'));const value=JSON.parse(fs.readFileSync(manifest));value.scope+=' mutation-control';fs.writeFileSync(path.join(temp,'package-fixture-tools.json'),JSON.stringify(value));
 const {pathToFileURL}=await import('node:url');const copied=await import(pathToFileURL(path.join(temp,'package-fixture-tools.mjs')));assert.throws(()=>copied.verifyPackageFixtureTools(),/PACKAGE_FIXTURE_MANIFEST_CHANGED/);
});
