// Maintenance checks for existing public-run receipts; never compiles or substitutes a producer.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import test from 'node:test';
import{createRequire}from'node:module';import{fileURLToPath,pathToFileURL}from'node:url';import crypto from'node:crypto';
import{getAdapters}from'../adapters.mjs';
import{BUNDLED_AUTHORITY_SOURCES,lockBundledNodeTools,verifyBundledImplementation,verifyBundledToolScope,verifyBundledNodeReceipt}from'../bundled-node-tool-authority.mjs';
import{verifySavedExecutionCompletion}from'../execution-completion/index.mjs';
const repoRoot=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
assert.equal(process.argv[2],'--evidence-root');const evidenceRoot=path.resolve(process.argv[3]);
const deliveryRoot=path.dirname(evidenceRoot),read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const sha=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const scope=lockBundledNodeTools(repoRoot);verifyBundledImplementation(repoRoot);
const ts=createRequire(path.join(repoRoot,'package.json'))('typescript');
const sourceIdentity=read(path.join(deliveryRoot,'SOURCE-IDENTITY.json')),faultManifest=read(path.join(deliveryRoot,'FAULT-MANIFEST.json'));
const producer='tools/gyro_game_haptics_selftest.ts',product='src/gyroSimulation/haptics.ts',source=fs.readFileSync(path.join(repoRoot,producer),'utf8');
function tree(text,name){const t=ts.createSourceFile(name,text,ts.ScriptTarget.Latest,true,ts.ScriptKind.TS);assert.equal(t.parseDiagnostics.length,0);return t;}
function findCalls(t,re){const rows=[];function visit(n){if(ts.isCallExpression(n)&&re.test(n.expression.getText(t)))rows.push(n);ts.forEachChild(n,visit);}visit(t);return rows;}
const originalTree=tree(source,producer),originalCases=findCalls(originalTree,/^test$/).filter(n=>ts.isStringLiteral(n.arguments[0])),names=originalCases.map(n=>n.arguments[0].text);
const selected=getAdapters(repoRoot).filter(a=>a.id==='gyro.game-haptics');assert.equal(selected.length,1);const descriptor=selected[0];
const checks=[],faultEvidence=[];async function check(name,fn){await test(name,async()=>{try{await fn();checks.push({name,status:'PASS'});}catch(error){checks.push({name,status:'FAIL',error:error.stack});throw error;}});}
function result(id){const out=path.join(evidenceRoot,id),report=read(path.join(out,'result.json')),item=report.results[0],caseDir=path.join(out,'cases/gyro.game-haptics');return{out,report,item,caseDir,stdout:fs.readFileSync(path.join(caseDir,'stdout.log'),'utf8'),stderr:fs.readFileSync(path.join(caseDir,'stderr.log'),'utf8')};}
function checkIdentity(run){
 const{item,report,caseDir}=run;assert.equal(report.producer.sha256,sourceIdentity.files.find(r=>r.path===producer).sha256);
 assert.equal(item.sourceChanged,false);assert.equal(item.candidateArtifactChanged,false);assert.equal(item.timedOut,false);assert.equal(item.signal,null);assert.equal(item.launchError,null);assert(!item.buildError&&!item.missingInputs);assert(item.bundle);
 const proof=verifyBundledNodeReceipt(item,report.repoRoot,caseDir);assert.equal(proof.toolAuthorityVerified,true);assert.equal(proof.packageCount,26);assert.equal(proof.fileCount,749);assert.equal(proof.artifactCount,753);
 assert.equal(BUNDLED_AUTHORITY_SOURCES.length,6);for(const p of BUNDLED_AUTHORITY_SOURCES)assert(item.sourceInputs.some(r=>r.path===p&&r.sha256===sha(path.join(report.repoRoot,p))));
 for(const row of [...item.sourceInputs,...item.dependencyInputs]){assert(!path.isAbsolute(row.path)&&!row.path.split(/[\\/]/).includes('node_modules'));assert.equal(row.status,'OK');assert.equal(row.sha256,sha(path.join(report.repoRoot,row.path)));}
 assert.equal(item.bundledToolAuthority.nodeModulesRealRoot,fs.realpathSync('G:/YeManCC-Work/Mainline/YeManCC-source/YeManCC/node_modules'));
 assert(item.candidateArtifacts.every(r=>path.isAbsolute(r.path)&&r.status==='OK'));
 assert.deepEqual(item.dependencyInputs.map(r=>r.path).sort(),['src/bridge/ipc.ts',product,producer].sort());return proof;
}
await check('one exact descriptor added; other registrations and original 30s budget remain unchanged',async()=>{
 const base=await import(pathToFileURL(path.join(deliveryRoot,'capture/repo/tools/ymcc_sim/global/adapters.mjs')).href);
 assert.deepEqual(getAdapters(repoRoot).filter(a=>a.id!=='gyro.game-haptics'),base.getAdapters(repoRoot));
 assert.equal(descriptor.kind,'typescript');assert.equal(descriptor.domain,'gyro');assert.equal(descriptor.path,producer);assert.equal(descriptor.timeoutMs,30000);assert.equal(descriptor.scope,'behavioral-mock');assert.deepEqual(descriptor.args,[]);assert.deepEqual(descriptor.profiles,['desktop','full']);
 assert.deepEqual(descriptor.evidenceInputs,['package.json','src/bridge/ipc.ts',product,'tsconfig.json']);assert.equal(descriptor.completionContract.rules[0].line,'Game haptics selftest: 8/8 passed');
 assert.deepEqual(getAdapters('__missing__'),getAdapters(repoRoot));
});
await check('original eight names/order and all producer assertions remain byte-identical in all views',()=>{
 assert.equal(names.length,8);const pin=sourceIdentity.files.find(r=>r.path===producer).sha256;
 for(const view of [repoRoot,...faultManifest.faults.map(f=>f.view)])assert.equal(sha(path.join(view,producer)),pin);
 assert(findCalls(originalTree,/^assert(?:\.\w+)?$/).length>0);
});
await check('positive public API result executes the original eight cases in order with complete tool/output proof',()=>{
 const run=result('positive');checkIdentity(run);assert.equal(run.item.status,'PASS');assert.equal(run.item.exitCode,0);assert.equal(run.report.verdict.status,'PASS_SELECTED_SCOPE');
 assert.deepEqual(run.stdout.split(/\r?\n/).filter(l=>l.startsWith('PASS ')),names.map(n=>'PASS '+n));assert(run.stdout.includes('Game haptics selftest: 8/8 passed'));assert.equal(run.stderr,'');assert.equal(verifySavedExecutionCompletion(run.item,run.caseDir,descriptor.completionContract).status,'PASS');
});
for(const fault of faultManifest.faults)await check(fault.id+' reaches the original named business assertion after a successful compile',()=>{
 const run=result(fault.id),toolProof=checkIdentity(run);assert.equal(run.item.status,'FAIL');assert.equal(run.item.exitCode,1);assert.equal(run.report.verdict.exitCode,2);
 assert.match(run.stderr,/AssertionError \[ERR_ASSERTION\]/);assert(run.stderr.includes(fault.expectedActual+' !== '+fault.expectedExpected));
 assert.deepEqual(run.stdout.split(/\r?\n/).filter(l=>l.startsWith('PASS ')),names.slice(0,fault.caseIndex-1).map(n=>'PASS '+n));assert(!run.stdout.includes('Game haptics selftest: 8/8 passed'));
 const baseBytes=fs.readFileSync(path.join(repoRoot,product)),changedBytes=fs.readFileSync(path.join(run.report.repoRoot,product));assert.equal(baseBytes.toString().split(fault.from).length,2);assert.deepEqual(changedBytes,Buffer.from(baseBytes.toString().replace(fault.from,fault.to)));
 const changed=run.item.sourceInputs.filter(row=>row.sha256!==result('positive').item.sourceInputs.find(p=>p.path===row.path)?.sha256).map(r=>r.path);assert.deepEqual(changed,[product]);
 const bundle=fs.readFileSync(run.item.bundle.path,'utf8'),bt=tree(bundle,'entry.mjs'),location=run.stderr.match(/entry\.mjs:(\d+):(\d+)/);assert(location);const line=Number(location[1]),column=Number(location[2]),pos=bt.getPositionOfLineAndCharacter(line-1,column-1);
 const called=findCalls(bt,/^test$/).find(n=>n.getStart(bt)<=pos&&pos<n.end);assert(called&&ts.isStringLiteral(called.arguments[0]));assert.equal(called.arguments[0].text,fault.case);
 const actualAssert=findCalls(bt,/^assert(?:\.\w+)?$/).filter(n=>n.getStart(bt)<=pos&&pos<n.end).sort((a,b)=>(a.end-a.pos)-(b.end-b.pos))[0];assert(actualAssert);
 const norm=s=>s.replace(/\s/g,'').replace(/!/g,'');assert.equal(norm(actualAssert.getText(bt)),norm(fault.assertion));
 const original=originalCases[fault.caseIndex-1],assertions=findCalls(originalTree,/^assert(?:\.\w+)?$/).filter(n=>n.getStart(originalTree)>=original.getStart(originalTree)&&n.end<=original.end&&norm(n.getText(originalTree))===norm(fault.assertion));assert(assertions.length);
 faultEvidence.push({id:fault.id,status:'EXPECTED_BUSINESS_ASSERTION_REJECTED',caseIndex:fault.caseIndex,caseName:called.arguments[0].text,originalAssertion:fault.assertion,bundleAssertion:actualAssert.getText(bt),stackLocation:{path:run.item.bundle.path,line,column},actual:fault.expectedActual,expected:fault.expectedExpected,precedingOriginalCasesPassed:fault.caseIndex-1,producerUnmodified:true,singleChangedSource:product,compileSucceeded:true,timeout:false,environmentFailure:false,producerExit:run.item.exitCode,runnerExit:run.report.verdict.exitCode,toolProof,stdout:path.join(run.caseDir,'stdout.log'),stderr:path.join(run.caseDir,'stderr.log')});
});
await check('six mandatory implementation helpers and original G complete Node tool graph remain verified',()=>{verifyBundledToolScope(scope,repoRoot);assert.equal(scope.packageCount,26);assert.equal(scope.fileCount,749);for(const relative of BUNDLED_AUTHORITY_SOURCES)assert.equal(sha(path.join(repoRoot,relative)),sourceIdentity.files.find(r=>r.path===relative).sha256);});
const report={schemaVersion:1,sourceApproval:false,status:checks.every(c=>c.status==='PASS')?'PASS':'FAIL',checks,originalCases:names,originalAssertionCalls:findCalls(originalTree,/^assert(?:\.\w+)?$/).length,faults:faultEvidence,producerSha256:sha(path.join(repoRoot,producer)),scope:'haptics producer only; no real SFC teardown or downstream hardware neutral claim'};
fs.writeFileSync(path.join(evidenceRoot,'contract-validation.json'),JSON.stringify(report,null,2)+'\n',{flag:'wx'});
