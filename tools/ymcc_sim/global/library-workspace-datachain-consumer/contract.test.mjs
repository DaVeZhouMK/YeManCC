import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import {pathToFileURL} from 'node:url';
import test from 'node:test';
import assert from 'node:assert/strict';
import {HERE,FILES,MODULE,spec,json,save,fingerprint,semanticVerdict,verifyReceipt} from './contract.mjs';
import {prepareCase,executeCase,evaluateCase,runSuite} from './run.mjs';
import {getAdapter} from './adapter.mjs';
const source=process.env.YMCC_WORKSPACE_TEST_REPO||path.resolve(HERE,'../../../..');
const output=process.env.YMCC_WORKSPACE_TEST_OUTPUT||path.join(os.tmpdir(),'workspace-datachain-maintenance-'+Date.now());
fs.mkdirSync(output,{recursive:false});
const fixture=path.join(output,'finite-source');fs.mkdirSync(fixture);
for(const file of spec.sourceInputs){const dst=path.join(fixture,file);fs.mkdirSync(path.dirname(dst),{recursive:true});fs.copyFileSync(path.join(source,file),dst);}
let sequence=0;
const next=(faultId=null,repo=fixture)=>prepareCase({repoRoot:repo,caseDir:path.join(output,'control-'+(++sequence)),faultId});
const rejectPre=plan=>assert.throws(()=>executeCase(plan),/PRE_EXECUTION_INPUT_CHANGED/);
let baseline;
test('original positive plus exactly two original named semantic failures',()=>{
  baseline=runSuite({repoRoot:fixture,outDir:path.join(output,'suite')});
  assert.equal(baseline.ok,true);assert.equal(baseline.receipts.length,3);
  for(const receipt of baseline.receipts){assert.equal(verifyReceipt(receipt),true);const trace=json(path.join(receipt.caseDir,'trace.json'));assert.equal(trace.vm.length,6);assert.ok(trace.calls.normalizeWorkspaceSnapshot.calls>=1000);assert.ok(trace.calls.artworkEntry.calls>=3000);}
});
test('adapter separates source/artifact/output and preserves 30000ms budget',()=>{
  const adapter=getAdapter(fixture);assert.equal(adapter.timeoutMs,30000);assert.equal(adapter.artifactInputs.length,1);assert.ok(adapter.artifactInputs.length<=3072);
  assert.equal(adapter.path,MODULE+'/run.mjs');assert.equal(adapter.evidenceInputs.length,FILES.length+3);assert.equal(adapter.blockedReason,undefined);
  assert.equal(adapter.artifactInputs[0],process.execPath);assert.ok(!adapter.evidenceInputs.some(f=>path.isAbsolute(f)));assert.deepEqual(adapter.resultFiles,['workspace-datachain/result.json']);
});
test('fresh output directory is mandatory',()=>assert.throws(()=>runSuite({repoRoot:fixture,outDir:path.join(output,'suite')}),/EEXIST/));
test('stale original report cannot be replayed',()=>{const plan=next();fs.mkdirSync(path.dirname(plan.report),{recursive:true});fs.copyFileSync(baseline.receipts[0].caseDir+'/Build/CustomSteamLibrary/ui-datachain-summary.json',plan.report);assert.throws(()=>executeCase(plan),/FRESH_OUTPUT_REQUIRED/);});
test('copied product source tamper is control failure',()=>{const plan=next();fs.appendFileSync(plan.viewInputs[1].path,'\n// tamper');rejectPre(plan);});
test('original producer tamper is control failure',()=>{const plan=next();fs.appendFileSync(plan.viewInputs[0].path,'\n// tamper');rejectPre(plan);});
test('upstream owned source change after capture is rejected',()=>{
  const repo=path.join(output,'upstream-tamper');fs.mkdirSync(repo);for(const file of spec.sourceInputs){const dst=path.join(repo,file);fs.mkdirSync(path.dirname(dst),{recursive:true});fs.copyFileSync(path.join(fixture,file),dst);}
  const plan=next(null,repo);fs.appendFileSync(path.join(repo,spec.sourceInputs[1]),'\n// tamper');rejectPre(plan);
});
test('exact saved VM code input tamper is control failure',()=>{const plan=next();fs.appendFileSync(plan.vmIdentity.path,' ');rejectPre(plan);});
test('observer config tamper is control failure',()=>{const plan=next();fs.appendFileSync(plan.configFile,' ');rejectPre(plan);});
test('actual reviewed helper tool tamper is control failure',async()=>{
  const cloned=path.join(output,'module-clone');fs.mkdirSync(cloned);for(const file of FILES)fs.copyFileSync(path.join(HERE,file),path.join(cloned,file));
  const module=await import(pathToFileURL(path.join(cloned,'run.mjs')).href);
  const plan=module.prepareCase({repoRoot:fixture,caseDir:path.join(output,'helper-tamper')});fs.appendFileSync(path.join(cloned,'observe.cjs'),'\n// tamper');assert.throws(()=>module.executeCase(plan),/PRE_EXECUTION_INPUT_CHANGED/);
});
test('wrong executable identity is rejected before launch',()=>{const plan=next();plan.tools[0].sha256='0'.repeat(64);rejectPre(plan);});
test('actual VM code differs from observer expected input: never semantic detection',()=>{
  const plan=next(spec.faults[0].id),cfg=json(plan.configFile);cfg.vm[0].code+='\n// wrong VM';cfg.vm[0].sha256='0'.repeat(64);
  fs.writeFileSync(plan.configFile,JSON.stringify(cfg));plan.configIdentity=fingerprint(plan.configFile);
  const receipt=executeCase(plan);assert.equal(receipt.accepted,false);assert.equal(receipt.semanticDetected,false);assert.equal(receipt.controlStatus,'FAIL');
});
test('source syntax error after VM entry is not a semantic negative',()=>{
  const repo=path.join(output,'syntax-source');fs.mkdirSync(repo);for(const file of spec.sourceInputs){const dst=path.join(repo,file);fs.mkdirSync(path.dirname(dst),{recursive:true});fs.copyFileSync(path.join(fixture,file),dst);}
  const file=path.join(repo,spec.sourceInputs[1]);fs.writeFileSync(file,fs.readFileSync(file,'utf8').replace('function isWorkspaceRecord(value) {','function isWorkspaceRecord(value) { @'));
  const plan=next(spec.faults[0].id,repo),receipt=executeCase(plan);assert.equal(receipt.semanticDetected,false);assert.equal(receipt.accepted,false);assert.ok(receipt.controlFailures.includes('FRESH_REPORT_OR_TRACE_MISSING'));
});
test('a different semantic fault cannot satisfy the requested fault',()=>{
  const receipt=baseline.receipts[1],report=json(path.join(receipt.caseDir,'Build/CustomSteamLibrary/ui-datachain-summary.json')),trace=json(path.join(receipt.caseDir,'trace.json'));
  assert.equal(semanticVerdict({report,trace,exitCode:1,faultId:spec.faults[1].id,controlsOk:true}),false);
});
test('positive report cannot count as semantic detection',()=>{
  const receipt=baseline.receipts[0];assert.equal(semanticVerdict({report:json(path.join(receipt.caseDir,'Build/CustomSteamLibrary/ui-datachain-summary.json')),trace:json(path.join(receipt.caseDir,'trace.json')),exitCode:1,faultId:spec.faults[0].id,controlsOk:true}),false);
});
test('missing newly produced output is control failure',()=>{
  const plan=next(spec.faults[0].id),receipt=executeCase(plan);assert.equal(receipt.accepted,true);fs.renameSync(plan.report,plan.report+'.original-archived');
  const reevaluated=evaluateCase(plan,receipt.child);assert.equal(reevaluated.semanticDetected,false);assert.ok(reevaluated.controlFailures.includes('FRESH_REPORT_OR_TRACE_MISSING'));assert.equal(verifyReceipt(receipt),false);
});
test('output tamper invalidates the receipt even if stdout is also forged',()=>{
  const plan=next(),receipt=executeCase(plan);assert.equal(receipt.accepted,true);fs.appendFileSync(plan.report,' ');fs.appendFileSync(path.join(plan.caseDir,'stdout.txt'),' ');assert.equal(verifyReceipt(receipt),false);
});
test('incomplete VM and non-assertion failures cannot count as detection',()=>{
  const receipt=baseline.receipts[1],report=json(path.join(receipt.caseDir,'Build/CustomSteamLibrary/ui-datachain-summary.json')),trace=json(path.join(receipt.caseDir,'trace.json'));
  trace.assertions.forEach(a=>a.code='ERR_TOOL_FAILURE');assert.equal(semanticVerdict({report,trace,exitCode:1,faultId:spec.faults[0].id,controlsOk:true}),false);
  trace.vm[0].completed=false;assert.equal(semanticVerdict({report,trace,exitCode:1,faultId:spec.faults[0].id,controlsOk:true}),false);
});
test('existing symlink source escaping the finite repo is rejected',()=>{
  const repo=path.join(output,'escape-source');fs.mkdirSync(repo);fs.symlinkSync(path.join(fixture,'tools'),path.join(repo,'tools'),'junction');assert.throws(()=>next(null,repo),/SOURCE_OUTSIDE_ROOT/);
});
test('forwarded output ancestor is rejected before producer execution',()=>{
  const actual=path.join(output,'actual-owned-output');fs.mkdirSync(actual);const link=path.join(output,'forwarded-output');fs.symlinkSync(actual,link,'junction');
  assert.throws(()=>runSuite({repoRoot:fixture,outDir:path.join(link,'run')}),/OUTPUT_FORWARDING_FORBIDDEN/);assert.deepEqual(fs.readdirSync(actual),[]);
});
test('timeout cannot count as the expected named assertion failure',()=>{
  const plan=next(spec.faults[0].id),receipt=executeCase(plan);assert.equal(receipt.semanticDetected,true);
  const reevaluated=evaluateCase(plan,{...receipt.child,exitCode:null,signal:'SIGTERM',error:{code:'ETIMEDOUT'}});assert.equal(reevaluated.semanticDetected,false);assert.ok(reevaluated.controlFailures.includes('CHILD_EXECUTION_INCOMPLETE'));
});
test('VM source changes after execution invalidate saved receipts',()=>{
  const plan=next(spec.faults[1].id),receipt=executeCase(plan);assert.equal(receipt.semanticDetected,true);fs.appendFileSync(plan.viewInputs[1].path,'\n// post-execution tamper');assert.equal(verifyReceipt(receipt),false);
});

// Historical creation failures retain their status; this consumer proves only its declared source bytes.
function historicalView(){
  const dir=path.join(output,'historical-source-'+(++sequence));fs.mkdirSync(dir);
  for(const file of spec.sourceInputs){const dst=path.join(dir,file);fs.mkdirSync(path.dirname(dst),{recursive:true});fs.copyFileSync(path.join(fixture,file),dst);}
  const metadata=path.join(dir,'.ymcc-source-snapshot');fs.mkdirSync(metadata);
  const manifest={schemaVersion:1,kind:'ymcc-fixed-source-snapshot',status:'FAIL',snapshotRoot:dir,sourceRoot:'C:/historical-unavailable-primary',files:spec.sourceInputs.map(relative=>({path:relative,size:fs.statSync(path.join(dir,relative)).size,sha256:fingerprint(path.join(dir,relative)).sha256}))};
  const file=path.join(metadata,'manifest.json');fs.writeFileSync(file,JSON.stringify(manifest));return {dir,file,manifest};
}
test('historical failed capture remains failed metadata while exact consumed sources pass original cases',()=>{
  const old=historicalView(),result=runSuite({repoRoot:old.dir,outDir:path.join(output,'historical-pass-'+(++sequence))});
  assert.equal(result.ok,true);assert.equal(result.snapshot.status,'FAIL');assert.equal(result.snapshot.wholeSnapshotReverifiedByThisModule,false);assert.equal(result.receipts[0].originalResult.caseCount,40);assert(result.receipts.slice(1).every(row=>row.semanticDetected));assert.equal(json(old.file).status,'FAIL');
});
test('historical wrong repository, unknown capture status and duplicate consumed source refuse before VM',()=>{
  for(const mutate of [row=>row.manifest.snapshotRoot=fixture,row=>row.manifest.status='IN_PROGRESS',row=>row.manifest.files.push({...row.manifest.files[0]})]){
    const old=historicalView();mutate(old);fs.writeFileSync(old.file,JSON.stringify(old.manifest));const out=path.join(output,'historical-reject-'+(++sequence));
    assert.throws(()=>runSuite({repoRoot:old.dir,outDir:out}),/COMPLETED_SNAPSHOT_REQUIRED|SNAPSHOT_SOURCE_IDENTITY_MISMATCH/);assert.equal(fs.existsSync(path.join(out,'positive')),false);
  }
});
test('historical changed consumed source is refused without changing pinned manifest or starting VM',()=>{
  const old=historicalView(),before=fingerprint(old.file);fs.appendFileSync(path.join(old.dir,spec.sourceInputs[2]),'\n<!-- changed after capture -->');const out=path.join(output,'historical-source-reject-'+(++sequence));
  assert.throws(()=>runSuite({repoRoot:old.dir,outDir:out}),/SNAPSHOT_SOURCE_IDENTITY_MISMATCH/);assert.equal(fingerprint(old.file).sha256,before.sha256);assert.equal(fs.existsSync(path.join(out,'positive')),false);
});
