import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { fileURLToPath } from 'node:url';
import { sourcePath, validateAdapters, executeAdapter, evaluateResult, verdictFor, compareRuns, runProcess, runAdapters, makeCoverageWorkItems, MAX_ARTIFACT_INPUTS, sha256File } from './runner.mjs';
import {BASELINE_ENVIRONMENT_KEYS,baselineEnvironment} from './decky-persistent-lifecycle/baseline-environment.mjs';
import { parseArgs } from './cli.mjs';
const tempParent = fs.realpathSync(os.tmpdir());
const temp = fs.mkdtempSync(path.join(tempParent, 'ymcc-global-runner-'));
process.on('exit', () => {
  const target = fs.realpathSync(temp);
  assert.equal(path.dirname(target), tempParent);
  assert(path.basename(target).startsWith('ymcc-global-runner-'));
  fs.rmSync(target, { recursive: true, force: true });
});
function fixture(id, body) {
  const file = `${id}.mjs`; fs.writeFileSync(path.join(temp, file), body);
  return { id, domain: 'fixture', title: id, kind: 'node', path: file, args: [], profiles: ['desktop'], scope: 'behavioral-mock', timeoutMs: 1500, evidenceInputs: [] };
}
function options() { return { repoRoot: temp, runDir: fs.mkdtempSync(path.join(temp, 'run-')) }; }
test('nonzero process exit is failure even when stdout says PASS', async () => {
  const item = fixture('false-green', "console.log('PASS');process.exitCode=2;");
  const result = await executeAdapter(item, options());
  assert.equal(result.status, 'FAIL'); assert.equal(result.exitCode, 2);
});
test('successful actual process stores current-run evidence and hashes', async () => {
  const result = await executeAdapter(fixture('normal', "console.log('executed');"), options());
  assert.equal(result.status, 'PASS'); assert.equal(result.sourceInputs.length, 1);
  assert.match(result.sourceInputs[0].sha256, /^[a-f0-9]{64}$/);
  assert.equal(fs.readFileSync(path.join(result.evidenceDir, 'stdout.log'), 'utf8').trim(), 'executed');
  assert.equal(result.claims.physicalHardwareValidated, false);
});
test('missing entry point is retained as BLOCKED', async () => {
  const item = fixture('removed-entry', ''); fs.unlinkSync(path.join(temp, item.path));
  const result = await executeAdapter(item, options());
  assert.equal(result.status, 'BLOCKED'); assert.equal(result.reason, 'REQUIRED_INPUT_MISSING');
});
test('missing covered source is blocked before test runs', async () => {
  const item = fixture('source-missing', 'throw Error("must not run")'); item.evidenceInputs=['missing.ts'];
  assert.equal((await executeAdapter(item, options())).status, 'BLOCKED');
});
test('a live hung child times out and cannot pass', async () => {
  const item = fixture('timeout', 'setInterval(()=>{},500)'); item.timeoutMs=180;
  const result=await executeAdapter(item, options());
  assert.equal(result.status,'TIMEOUT'); assert.equal(result.timedOut,true);
});
test('required result JSON missing or false overrides successful exit', async () => {
  const item=fixture('missing-json', ''); item.resultFiles=['receipt.json'];
  let result=await executeAdapter(item, options());
  assert.equal(result.status,'FAIL'); assert.equal(result.requiredEvidenceMissing,true);
  fs.writeFileSync(path.join(temp,item.path), "import fs from 'node:fs';import path from 'node:path';fs.writeFileSync(path.join(process.env.YMCC_SIM_EVIDENCE_DIR,'receipt.json'),JSON.stringify({ok:false}));");
  result=await executeAdapter(item, options());
  assert.equal(result.status,'FAIL'); assert.equal(result.requiredEvidenceFailed,true);
});
test('a source changed while running invalidates the receipt', async () => {
  const item=fixture('source-drift', "import fs from 'node:fs';fs.appendFileSync(new URL(import.meta.url),'\\n// changed');");
  const result=await executeAdapter(item, options());
  assert.equal(result.sourceChanged,true); assert.equal(result.status,'FAIL');
});
test('truncated stdout cannot silently hide a failure', async () => {
  const result=await runProcess(process.execPath,['-e',"console.log('x'.repeat(2000))"],{timeoutMs:1500,outputLimit:100});
  assert.equal(result.outputTruncated,true); assert.equal(evaluateResult(result),'FAIL');
});
test('duplicate ids, unsafe host args, traversal cannot enter the registry', () => {
  const item=fixture('registry', '');
  assert.throws(()=>validateAdapters([item,item],temp),/DUPLICATE/);
  assert.throws(()=>validateAdapters([{...item,referenceInputs:'not-an-array'}],temp),/REFERENCE_INPUTS_INVALID/);
  assert.throws(()=>sourcePath(temp,'../outside'),/OUTSIDE/);
  assert.throws(()=>validateAdapters([{...item,kind:'powershell',args:['--real-backend']}],temp),/UNSAFE/);
});
test('host mock requires explicit payload and does not spawn when absent', async () => {
  const item=fixture('host-payload', '');item.kind='powershell';item.scope='host-mock';
  const result=await executeAdapter(item,options());
  assert.equal(result.status,'BLOCKED');assert.equal(result.reason,'EXPLICIT_CURRENT_HOST_PAYLOAD_REQUIRED');assert.equal(result.pid,undefined);
});
test('zero tests, omitted result, NOT_RUN or blocked required test never pass', () => {
  assert.equal(verdictFor({results:[],selectedCount:0}).exitCode,10);
  assert.equal(verdictFor({results:[],selectedCount:1}).exitCode,10);
  assert.equal(verdictFor({results:[{status:'NOT_RUN'}],selectedCount:1}).exitCode,10);
  assert.equal(verdictFor({results:[{status:'BLOCKED'}],selectedCount:1}).exitCode,10);
  assert.equal(verdictFor({results:[{status:'FAIL'}],selectedCount:1}).exitCode,2);
});
test('passing selected tests does not satisfy require-all while coverage is missing', () => {
  const input={results:[{status:'PASS'}],selectedCount:1,uncoveredCount:2};
  assert.equal(verdictFor(input).status,'PASS_SELECTED_SCOPE');
  assert.equal(verdictFor({...input,requireAll:true}).status,'INCOMPLETE_COVERAGE');
});
test('diff records regressions, removed tests, source changes and honest timing semantics', () => {
  const before={results:[{id:'a',status:'PASS',source:'a',scope:'source-contract',sourceInputs:['old'],durationMs:10},{id:'removed',status:'PASS'}]};
  const after={results:[{id:'a',status:'FAIL',source:'a',scope:'source-contract',sourceInputs:['new'],durationMs:20}]};
  const diff=compareRuns(before,after);
  assert.equal(diff.regressions,1);assert.equal(diff.scopeGaps,1);assert.equal(diff.comparisonComplete,false);assert.equal(diff.differences[0].sourceChanged,true);assert.equal(diff.performanceImprovementProven,false);
});
test('CLI rejects unknown switches and duplicate or incomplete values', () => {
  assert.throws(()=>parseArgs(['--execute','evil']),/INVALID_ARGUMENT/);
  assert.throws(()=>parseArgs(['--mode']),/INVALID_ARGUMENT/);
  assert.throws(()=>parseArgs(['--mode','run','--mode','plan']),/DUPLICATE/);
  assert.throws(()=>parseArgs(['--profile','anything']),/INVALID_PROFILE/);
  assert.deepEqual(parseArgs(['--domain','fan,input']).domains,['fan','input']);
});
test('empty or malformed comparison cannot report no regressions', () => {
  assert.throws(()=>compareRuns({},{}),/EVIDENCE_REQUIRED/);
  assert.throws(()=>compareRuns({results:[{id:'x',status:'UNKNOWN'}]},{results:[{id:'x',status:'PASS'}]}),/RESULT_INVALID/);
});

test('an explicitly blocked adapter stays blocked without starting its entry point', async () => {
 const item=fixture('blocked-adapter','throw Error("must not start")');item.blockedReason='DEVICE_LOG_REQUIRED';
 const result=await executeAdapter(item,options());
 assert.equal(result.status,'BLOCKED');assert.equal(result.reason,'DEVICE_LOG_REQUIRED');assert.equal(result.pid,undefined);
});
test('TypeScript receipt fingerprints the exact transitive source used by the bundle', async () => {
 const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../..');
 const prefix='tools/ymcc_sim/global/fixtures/ts-import-graph';
 const item={...fixture('bundled-current-root',''),kind:'typescript',path:prefix+'/entry.ts'};
 const result=await executeAdapter(item,{...options(),repoRoot:repo});
 assert.equal(result.status,'PASS',JSON.stringify(result));
 assert.ok(result.dependencyInputs.some(input=>input.path===prefix+'/transitive.ts'&&/^[a-f0-9]{64}$/.test(input.sha256)));
 assert.equal(result.sourceChanged,false);
 assert.equal(result.bundledToolProof.toolAuthorityVerified,true);
 assert.equal(result.candidateArtifactChanged,false);
});

test('a copied TypeScript entry with forwarded packages and no local tool-authority implementation remains blocked',async()=>{
 const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../..');
 fs.writeFileSync(path.join(temp,'package.json'),'{}');
 const modules=path.join(temp,'node_modules');if(!fs.existsSync(modules))fs.symlinkSync(path.join(repo,'node_modules'),modules,process.platform==='win32'?'junction':'dir');
 fs.writeFileSync(path.join(temp,'transitive.ts'),'export const answer:number=42;');
 fs.writeFileSync(path.join(temp,'bundled.ts'),"import {answer} from './transitive';if(answer!==42)throw Error('incorrect');");
 const result=await executeAdapter({...fixture('bundled-forwarded',''),kind:'typescript',path:'bundled.ts'},options());
 assert.equal(result.status,'BLOCKED');assert.equal(result.reason,'REQUIRED_INPUT_MISSING');assert.equal(result.bundle,undefined);
});

test('a syntactically valid but empty receipt cannot certify success', async () => {
 const item=fixture('empty-receipt',"import fs from 'node:fs';import path from 'node:path';fs.writeFileSync(path.join(process.env.YMCC_SIM_EVIDENCE_DIR,'receipt.json'),'{}');");
 item.resultFiles=['receipt.json'];
 const result=await executeAdapter(item,options());
 assert.equal(result.status,'FAIL');assert.equal(result.requiredEvidenceFailed,true);
});
test('coverage queue retains unknown, dynamic, static-only and failing source associations',()=>{
 const catalog={surfaces:[
  {domain:'unclassified',path:'src/new.ts',line:2,coverage:'NOT_COVERED',dynamic:true,adapterIds:[],evidenceScopes:[]},
  {domain:'sleep',path:'native/gate.h',line:1,coverage:'REGISTERED_TEST',dynamic:false,operation:'sleep.resume',adapterIds:['static'],evidenceScopes:['source-contract']},
 ]};
 const queue=makeCoverageWorkItems(catalog,[{id:'static',status:'FAIL'}]);
 assert.equal(queue.fullBehaviorAcceptanceProven,false);
 assert.equal(queue.items.length,2);assert.equal(queue.items[0].failedAdapterIds[0],'static');
 assert.equal(queue.items.find(item=>item.path==='src/new.ts').uncovered,1);
 assert.equal(queue.items.find(item=>item.path==='src/new.ts').dynamic,1);
 assert.equal(queue.items[0].sourceContractOnly,1);
});
test('a descriptive verdict cannot hide valid PASS status or an explicit failure',async()=>{
 const receipts=[{status:'PASS',verdict:'SOFTWARE_CHAIN_PASS_PHYSICAL_GAPS_REMAIN'},
 {status:'FAIL',verdict:'PASS'},{status:'PASS',verdict:'BLOCKED_COMPATIBILITY_DIFFERENCE'},
 {status:'PASS',cases:[{status:'FAIL'}]}];
 for(const[index,receipt]of receipts.entries()){
  const item=fixture('dual-status-'+index,`import fs from 'node:fs';import path from 'node:path';fs.writeFileSync(path.join(process.env.YMCC_SIM_EVIDENCE_DIR,'receipt.json'),${JSON.stringify(JSON.stringify(receipt))});`);
  item.resultFiles=['receipt.json'];const result=await executeAdapter(item,options());assert.equal(result.status,index===0?'PASS':'FAIL');
 }
});

test('structured product-source hashes are verified even when absent from adapter metadata',async()=>{
 const product='indirect-product.txt';fs.writeFileSync(path.join(temp,product),'current-product');
 const {createHash}=await import('node:crypto');const digest=createHash('sha256').update('current-product').digest('hex');
 for(const stale of [false,true]){
  const item=fixture('nested-source-'+stale,`import fs from 'node:fs';import path from 'node:path';fs.writeFileSync(path.join(process.env.YMCC_SIM_EVIDENCE_DIR,'receipt.json'),${JSON.stringify(JSON.stringify({status:'PASS',sourceHashes:[{path:product,sha256:stale?'0'.repeat(64):digest}]}))});`);
  item.resultFiles=['receipt.json'];const result=await executeAdapter(item,options());assert.equal(result.status,stale?'FAIL':'PASS');
  assert.equal(result.structuredEvidence[0].sourceMismatches.length,stale?1:0);
 }
});

test('run-local fixture hashes are bound to this exact case and cannot read unrelated absolute sources',async()=>{
 for(const outside of [false,true]){
  const item=fixture('owned-fixture-'+outside,`import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';const file=path.join(${outside?'process.cwd()':'process.env.YMCC_SIM_EVIDENCE_DIR'},'owned-template.txt');fs.writeFileSync(file,'fixture');const sha256=crypto.createHash('sha256').update('fixture').digest('hex');fs.writeFileSync(path.join(process.env.YMCC_SIM_EVIDENCE_DIR,'receipt.json'),JSON.stringify({status:'PASS',sourceHashes:[{path:file,sha256}]}));`);
  item.resultFiles=['receipt.json'];const result=await executeAdapter(item,options());assert.equal(result.status,outside?'FAIL':'PASS');
 }
});

test('unexecuted lineage is explicit and cannot masquerade as tested source or cause unrelated behavior invalidation',async()=>{
 const lineage='native-lineage.txt';fs.writeFileSync(path.join(temp,lineage),'before');
 const item=fixture('lineage-only',`import fs from 'node:fs';fs.writeFileSync(${JSON.stringify(path.join(temp,lineage))},'changed-elsewhere');`);
 item.referenceInputs=[lineage];const result=await executeAdapter(item,options());
 assert.equal(result.status,'PASS');assert.equal(result.sourceChanged,false);assert.equal(result.claims.referenceSourcesBehaviorVerified,false);
 assert.equal(result.referenceClosure,'UNVERIFIED_NOT_EXECUTED_BY_THIS_TEST');assert.deepEqual(result.referenceInputChanges,[lineage]);
 assert(!result.sourceInputs.some(row=>row.path===lineage));
});

test('Node preflight blocking requires explicit descriptor opt-in', async () => {
  const item=fixture('node-blocked-opt-in', 'process.exitCode=10;');
  assert.equal((await executeAdapter(item,options())).status,'FAIL');
  item.blockedExitCodes=[10];
  assert.equal((await executeAdapter(item,options())).status,'BLOCKED');
  const ordinary=fixture('node-real-failure', 'process.exitCode=1;');ordinary.blockedExitCodes=[10];
  assert.equal((await executeAdapter(ordinary,options())).status,'FAIL');
});
test('opt-in blocking cannot mask timeout, source drift or truncated output', async () => {
  const drift=fixture('blocked-drift', "import fs from 'node:fs';fs.appendFileSync(new URL(import.meta.url),'\n// changed');process.exitCode=10;");drift.blockedExitCodes=[10];
  assert.equal((await executeAdapter(drift,options())).status,'FAIL');
  const hung=fixture('blocked-timeout', 'process.exitCode=10;setInterval(()=>{},500);');hung.timeoutMs=180;hung.blockedExitCodes=[10];
  assert.equal((await executeAdapter(hung,options())).status,'TIMEOUT');
  const noisy=fixture('blocked-truncated', "console.log('x'.repeat(3000000));process.exitCode=10;");noisy.blockedExitCodes=[10];
  const result=await executeAdapter(noisy,options());assert.equal(result.outputTruncated,true);assert.equal(result.status,'FAIL');
});
test('blocked-exit metadata cannot redefine success or use malformed values', () => {
  const item=fixture('blocked-metadata', '');
  for(const codes of [[0],[-1],['10'],[10,10],null])assert.throws(()=>validateAdapters([{...item,blockedExitCodes:codes}],temp),/BLOCKED_EXITS_INVALID/);
  assert.equal(validateAdapters([{...item,blockedExitCodes:[10]}],temp).length,1);
});


test('a compiler closure keeps absolute file validation and a bounded member count', () => {
  const item=fixture('compiler-closure','');
  const absolute=path.join(temp,item.path), reviewed=Array(572).fill(absolute);
  assert.doesNotThrow(()=>validateAdapters([{...item,artifactInputs:reviewed}],temp));
  for(const entries of [[...reviewed,'relative.dll'],[...reviewed,absolute+'\0bad'],Array(MAX_ARTIFACT_INPUTS+1).fill(absolute)])
    assert.throws(()=>validateAdapters([{...item,artifactInputs:entries}],temp),/ADAPTER_ARTIFACT_INPUTS_INVALID/);
});

test('null completion proof fails without throwing or aborting the batch',()=>{
 assert.doesNotThrow(()=>evaluateResult({exitCode:0,completionProof:null}));
 assert.equal(evaluateResult({exitCode:0,completionProof:null}),'FAIL');
});

test('native managed and mounted UI closures accept2375 and the finite3072 bound while refusing the next member',()=>{
 const item=fixture('combined-closure',''),absolute=path.join(temp,item.path);
 for(const count of [640,925,1024,2375,MAX_ARTIFACT_INPUTS])assert.doesNotThrow(()=>validateAdapters([{...item,artifactInputs:Array(count).fill(absolute)}],temp));
 assert.throws(()=>validateAdapters([{...item,artifactInputs:Array(MAX_ARTIFACT_INPUTS+1).fill(absolute)}],temp),/ADAPTER_ARTIFACT_INPUTS_INVALID/);
});
// Inert launch-recorder controls only. The original baseline and its native suite never run here.
const baselineControlPreamble = `
import fs from 'node:fs';
import path from 'node:path';
const out=process.argv[process.argv.indexOf('--evidence-dir')+1];
fs.mkdirSync(out,{recursive:true});
const raw={fixtureKind:'INERT_LAUNCH_PROOF_CONTROL_NOT_BASELINE_ACCEPTANCE',passed:true};
`;
function baselineLaunchFixture(name,body) {
  const root=fs.mkdtempSync(path.join(temp,'baseline-launch-'+name+'-'));
  const entry='tools/ymcc_sim/global/decky-update-recovery/run.mjs';
  fs.mkdirSync(path.dirname(path.join(root,entry)),{recursive:true});
  fs.writeFileSync(path.join(root,entry),baselineControlPreamble+body);
  const tool=path.join(root,'declared-tool.bin');fs.writeFileSync(tool,'INERT declared tool identity; never executed');
  return {root,tool,item:{id:'plugins.decky-update-recovery',domain:'fixture',title:'Inert original launch recorder control',
    kind:'node',path:entry,args:['--evidence-dir','{outDir}/decky-update-recovery'],profiles:['desktop','full'],
    scope:'behavioral-mock',timeoutMs:240000,evidenceInputs:[],artifactInputs:[process.execPath,tool],
    resultFiles:['decky-update-recovery/result.json']},
    options:{repoRoot:root,runDir:path.join(root,'owned-run')}};
}
function readBaselineLaunch(result) {
  assert(result.baselineLaunchReceipt,'launch receipt missing');
  assert.equal(sha256File(result.baselineLaunchReceipt.path),result.baselineLaunchReceipt.sha256);
  const proof=JSON.parse(fs.readFileSync(result.baselineLaunchReceipt.path,'utf8'));
  assert.equal(proof.sharingAuthorized,false);assert.equal(proof.sameInvocationQualified,false);
  assert.equal(proof.parentToolScopeVerified,false);assert.notEqual(proof.status,'PASS');
  return proof;
}
test('baseline launch records actual argv env pid timing and non-UTF8 raw bytes without sharing authority',async()=>{
  const f=baselineLaunchFixture('bytes',`
process.stdout.write(Buffer.from([0,255,195,40,10]));
process.stderr.write(Buffer.from([0,254,226,130,172]));
fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(raw));
`);
  const env={...process.env,YMCC_DECKY_ARTIFACT_ROOT:'INERT-declared-environment',UNRECORDED_LAUNCH_SECRET:'do-not-store-launch-secret'};
  const result=await executeAdapter(f.item,{...f.options,env,remainingMs:()=>1500});
  assert.equal(result.status,'PASS');const proof=readBaselineLaunch(result);
  assert.equal(proof.status,'RECORDED');assert.equal(proof.recordingComplete,true);
  assert.equal(proof.program.path,path.resolve(process.execPath));assert.equal(proof.program.sha256,sha256File(process.execPath));
  assert.deepEqual(proof.programAfter,proof.toolArtifactsAfter.find(row=>row.path===process.execPath));
  assert.deepEqual(proof.arguments,[path.join(f.root,f.item.path),'--evidence-dir',result.evidenceDir+'/decky-update-recovery']);
  assert.equal(proof.cwd,f.root);assert.equal(proof.originalBudgetMs,240000);assert.equal(proof.expectedOriginalBudgetMs,240000);
  assert.equal(proof.originalBudgetMatchesContract,true);assert.equal(proof.timeoutMs,1500);assert.equal(proof.pid,result.pid);assert(proof.pid>0);assert.equal(proof.processStarted,true);
  assert.equal(proof.birthTimeUtc,null);assert.equal(proof.birthTimeStatus,'NOT_OBSERVED_BY_NODE_CHILD_PROCESS');
  assert(Number.isFinite(Date.parse(proof.spawnObservedAt)));assert(Date.parse(proof.spawnObservedAt)>=Date.parse(proof.startedAt));
  assert.equal(Date.parse(proof.finishedAt)-Date.parse(proof.startedAt),proof.elapsedMs);assert.equal(proof.elapsedMs,result.durationMs);
  assert.deepEqual(Object.keys(proof.baselineEnvironment),[...BASELINE_ENVIRONMENT_KEYS]);
  assert.deepEqual(proof.baselineEnvironment,baselineEnvironment(env));
  assert.equal(fs.readFileSync(result.baselineLaunchReceipt.path,'utf8').includes(env.UNRECORDED_LAUNCH_SECRET),false);
  for(const [kind,bytes]of [['stdout',[0,255,195,40,10]],['stderr',[0,254,226,130,172]]]){
    assert.deepEqual(fs.readFileSync(proof[kind].path),Buffer.from(bytes));
    assert.equal(proof[kind].sha256,sha256File(proof[kind].path));
    assert.equal(proof[kind].bytes,bytes.length);assert.equal(proof[kind].observedBytes,bytes.length);assert.equal(proof[kind].complete,true);
  }
  assert.equal(proof.rawOutputComplete,true);assert.equal(proof.sourceUnchanged,true);assert.equal(proof.toolsUnchanged,true);
  assert.deepEqual(proof.boundSourceInputs,proof.sourceInputsAfter);assert.deepEqual(proof.toolArtifacts,proof.toolArtifactsAfter);
  assert.equal(proof.originalResult.sha256,sha256File(proof.originalResult.path));
  assert.equal(JSON.parse(fs.readFileSync(proof.originalResult.path,'utf8')).fixtureKind,'INERT_LAUNCH_PROOF_CONTROL_NOT_BASELINE_ACCEPTANCE');
});
test('baseline launch preserves actual nonzero raw failure and before-after source and tool drift',async()=>{
  const f=baselineLaunchFixture('drift',`
fs.appendFileSync(process.argv[1],'\\n// actual source drift');
fs.appendFileSync(path.join(process.cwd(),'declared-tool.bin'),'actual tool drift');
raw.passed=false;fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(raw));
process.stdout.write(Buffer.from([255,0]));process.exitCode=7;
`);
  const result=await executeAdapter(f.item,{...f.options,remainingMs:()=>1500});
  assert.equal(result.status,'FAIL');const proof=readBaselineLaunch(result);
  assert.equal(proof.exitCode,7);assert.equal(proof.adapterStatus,'FAIL');assert.equal(proof.sourceUnchanged,false);assert.equal(proof.toolsUnchanged,false);
  assert.notDeepEqual(proof.boundSourceInputs,proof.sourceInputsAfter);assert.notDeepEqual(proof.toolArtifacts,proof.toolArtifactsAfter);
  assert.equal(JSON.parse(fs.readFileSync(proof.originalResult.path,'utf8')).passed,false);
  assert.deepEqual(fs.readFileSync(proof.stdout.path),Buffer.from([255,0]));
});
test('baseline launch retains original 240s and records a clipped effective timeout as TIMEOUT',async()=>{
  const f=baselineLaunchFixture('timeout','setInterval(()=>{},1000);');
  const result=await executeAdapter(f.item,{...f.options,remainingMs:()=>150});
  assert.equal(result.status,'TIMEOUT');const proof=readBaselineLaunch(result);
  assert.equal(proof.originalBudgetMs,240000);assert.equal(proof.timeoutMs,150);assert.equal(proof.timedOut,true);
  assert.equal(proof.originalResult,null);assert.equal(proof.adapterStatus,'TIMEOUT');
});
test('baseline launch raw output uses the original finite cap and labels truncation',async()=>{
  const f=baselineLaunchFixture('truncated',`
process.stdout.write(Buffer.alloc(2*1024*1024+512,255));
fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(raw));
`);
  const result=await executeAdapter(f.item,{...f.options,remainingMs:()=>3000});
  assert.equal(result.status,'FAIL');const proof=readBaselineLaunch(result);
  assert.equal(proof.outputTruncated,true);assert.equal(proof.outputCaptureLimitBytes,2*1024*1024);
  assert.equal(proof.stdout.bytes,2*1024*1024);assert.equal(proof.stdout.observedBytes,2*1024*1024+512);
  assert.equal(proof.stdout.complete,false);assert.equal(proof.rawOutputComplete,false);
  assert.equal(proof.stdout.sha256,sha256File(proof.stdout.path));
});
test('baseline launch recording collision preserves historical bytes and cannot claim a complete proof',async()=>{
  const f=baselineLaunchFixture('collision',"fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(raw));");
  const caseDir=path.join(f.options.runDir,'cases',f.item.id);fs.mkdirSync(caseDir,{recursive:true});
  const prior=path.join(caseDir,'original.plugins.decky-update-recovery.stdout.bin');
  fs.writeFileSync(prior,'HISTORICAL RAW MUST REMAIN');
  const result=await executeAdapter(f.item,{...f.options,remainingMs:()=>1500});
  const proof=readBaselineLaunch(result);assert.equal(proof.status,'INCOMPLETE');assert.equal(proof.recordingComplete,false);
  assert(proof.recordingErrors.some(row=>row.stage==='stdout'&&row.code==='EEXIST'));
  assert.equal(fs.readFileSync(prior,'utf8'),'HISTORICAL RAW MUST REMAIN');assert.equal(proof.stdout,undefined);
});
test('other adapters and altered baseline entry retain their original output contract without launch receipts',async()=>{
  for(const altered of ['other-id','other-entry']){
    const f=baselineLaunchFixture(altered,"fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(raw));");
    if(altered==='other-id')f.item.id='fixture.other-original';
    else {fs.copyFileSync(path.join(f.root,f.item.path),path.join(f.root,'altered-entry.mjs'));f.item.path='altered-entry.mjs';}
    const result=await executeAdapter(f.item,{...f.options,remainingMs:()=>1500});
    assert.equal(result.status,'PASS');assert.equal(result.baselineLaunchReceipt,undefined);assert.equal(result.baselineLaunchReceiptError,undefined);
    assert(!fs.existsSync(path.join(result.evidenceDir,'original.plugins.decky-update-recovery.json')));
    assert(!fs.existsSync(path.join(result.evidenceDir,'original.plugins.decky-update-recovery.stdout.bin')));
    assert(fs.existsSync(path.join(result.evidenceDir,'stdout.log')));
  }
});
test('baseline launch without remaining budget creates no process or launch receipt',async()=>{
  const f=baselineLaunchFixture('no-budget','throw Error("must not execute");');
  const result=await executeAdapter(f.item,{...f.options,remainingMs:()=>0});
  assert.equal(result.status,'NOT_RUN_BUDGET');assert.equal(result.pid,undefined);assert.equal(result.baselineLaunchReceipt,undefined);
  assert(!fs.existsSync(path.join(result.evidenceDir,'original.plugins.decky-update-recovery.stdout.bin')));
});


function parentFirstFixture(name,{parentExit=7}={}){
 const f=baselineLaunchFixture('parent-first-'+name,
  "fs.appendFileSync(path.join(process.cwd(),'launch-counts.txt'),'baseline;');fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(raw));");
 const entry='tools/ymcc_sim/global/decky-persistent-lifecycle/current-parent-global.mjs';
 fs.mkdirSync(path.dirname(path.join(f.root,entry)),{recursive:true});
 fs.writeFileSync(path.join(f.root,entry),"import fs from 'node:fs';import path from 'node:path';fs.appendFileSync(path.join(process.cwd(),'launch-counts.txt'),'parent;');process.exitCode="+parentExit+";");
 f.parent={id:'plugins.decky-persistent-lifecycle',domain:'fixture',title:'Inert parent-first scheduler control',kind:'node',path:entry,
  args:['--mode','run','--evidence-dir','{outDir}/decky-persistent-lifecycle'],profiles:['desktop','full'],scope:'behavioral-mock',
  timeoutMs:480000,evidenceInputs:[],artifactInputs:[process.execPath,f.tool],resultFiles:[]};
 f.events=()=>{const file=path.join(f.root,'launch-counts.txt');return fs.existsSync(file)?fs.readFileSync(file,'utf8').split(';').filter(Boolean):[];};
 return f;
}
test('parent-first baseline skips the independent producer after actual parent failure and preserves selected result order',async()=>{
 const f=parentFirstFixture('baseline-first');
 const unrelated={id:'fixture.parent-first-unrelated',domain:'fixture',title:'Inert independent consumer',kind:'node',path:'unrelated.mjs',
  args:[],profiles:['desktop'],scope:'behavioral-mock',timeoutMs:1500,evidenceInputs:[]};
 fs.writeFileSync(path.join(f.root,unrelated.path),"import fs from 'node:fs';import path from 'node:path';fs.appendFileSync(path.join(process.cwd(),'launch-counts.txt'),'unrelated;');");
 const progress=[],results=await runAdapters([f.item,unrelated,f.parent],{...f.options,onProgress:row=>progress.push(row)});
 assert.deepEqual(f.events(),['unrelated','parent']);assert.deepEqual(results.map(row=>row.id),[f.item.id,unrelated.id,f.parent.id]);
 assert.deepEqual(results.map(row=>row.status),['BLOCKED','PASS','FAIL']);
 assert.equal(results[0].reason,'BASELINE_NOT_RUN_PARENT_NOT_PASS');assert.equal(results[0].executedThisRun,false);
 assert.equal(results[0].actualPassedThisRun,false);assert.equal(results[0].verificationExecuted,false);assert.equal(results[0].originalProducerExecutionsByThisReference,0);
 assert.equal(progress.some(row=>row.id===f.item.id&&row.phase==='start'),false);
 assert.equal(verdictFor({results,selectedCount:3}).exitCode,2);
});
test('parent-first baseline also avoids a producer when the parent occurs first in the selection',async()=>{
 const f=parentFirstFixture('parent-first');const results=await runAdapters([f.parent,f.item],f.options);
 assert.deepEqual(f.events(),['parent']);assert.deepEqual(results.map(row=>row.id),[f.parent.id,f.item.id]);
 assert.deepEqual(results.map(row=>row.status),['FAIL','BLOCKED']);assert.equal(results[1].pid,undefined);
});
test('parent-first baseline selection alone still executes its original producer exactly once',async()=>{
 const f=parentFirstFixture('standalone');const results=await runAdapters([f.item],f.options);
 assert.deepEqual(f.events(),['baseline']);assert.equal(results[0].status,'PASS');assert(results[0].pid>0);
 assert.equal(results[0].sharedReference,undefined);assert.equal(readBaselineLaunch(results[0]).sharingAuthorized,false);
});
test('parent-first baseline preserves explicit blocked descriptors and does not reinterpret altered parent arguments',async()=>{
 for(const variant of ['blocked','altered-arguments']){
  const f=parentFirstFixture(variant,{parentExit:0});
  if(variant==='blocked')f.parent.blockedReason='ORIGINAL_DECLARED_PREREQUISITE_MISSING';else f.parent.args.push('--fixture-marker','inert');
  const results=await runAdapters([f.item,f.parent],f.options);
  assert.equal(results[0].status,'PASS');assert.equal(results[0].sharedReference,undefined);
  assert.deepEqual(f.events(),variant==='blocked'?['baseline']:['baseline','parent']);
  assert.equal(results[1].status,variant==='blocked'?'BLOCKED':'PASS');
 }
});
test('parent-first baseline exhausted shared budget starts neither producer nor parent',async()=>{
 assert.deepEqual(await runAdapters([],{}),[]);
 const f=parentFirstFixture('no-budget');const results=await runAdapters([f.item,f.parent],{...f.options,remainingMs:()=>0});
 assert.deepEqual(f.events(),[]);assert.deepEqual(results.map(row=>row.status),['NOT_RUN_BUDGET','NOT_RUN_BUDGET']);
 assert(results.every(row=>row.pid===undefined));assert.equal(verdictFor({results,selectedCount:2}).exitCode,10);
});
test('parent-first baseline typed reference completes evidence without inventing a new business pass',()=>{
 const reference={id:'plugins.decky-update-recovery',source:'tools/ymcc_sim/global/decky-update-recovery/run.mjs',status:'SHARED_REFERENCE',
  executedThisRun:false,actualPassedThisRun:false,verificationExecuted:true,requiredEvidenceMissing:false,requiredEvidenceFailed:false,
  referenceRequiredEvidenceCount:1,structuredEvidence:[{path:'inert-original-result.json'}],sharedReference:{status:'SHARED_REFERENCE',
   canonicalTestId:'plugins.decky-update-recovery',parentId:'plugins.decky-persistent-lifecycle',originalProducerExecutionsWithinParent:1,
   originalProducerExecutionsByThisReference:0,verificationExecuted:true,cached:false,originalResult:{path:'inert-original-result.json',sha256:'a'.repeat(64)},
   parentResult:{path:'inert-parent-result.json',sha256:'b'.repeat(64)}}};
 assert.equal(verdictFor({results:[{status:'PASS'},reference],selectedCount:2}).status,'PASS_SELECTED_SCOPE');
 assert.equal(reference.status,'SHARED_REFERENCE');assert.equal(reference.actualPassedThisRun,false);assert.equal(reference.executedThisRun,false);
 for(const alter of [row=>delete row.sharedReference,row=>row.sharedReference.cached=true,row=>row.actualPassedThisRun=true,
   row=>row.sharedReference.originalProducerExecutionsByThisReference=1,row=>row.requiredEvidenceMissing=true,row=>row.structuredEvidence=[]]){
  const bad=structuredClone(reference);alter(bad);assert.equal(verdictFor({results:[{status:'PASS'},bad],selectedCount:2}).exitCode,10);
 }
});
test('parent-first baseline passes an actual parent observation to the real verifier and rejects a missing producer proof without fallback',async()=>{
 const f=parentFirstFixture('missing-parent-proof',{parentExit:0});const results=await runAdapters([f.item,f.parent],f.options);
 assert.deepEqual(f.events(),['parent']);assert.equal(results[1].status,'PASS');assert.equal(results[0].status,'FAIL');
 assert.equal(results[0].executedThisRun,false);assert.equal(results[0].actualPassedThisRun,false);
 assert.match(results[0].reason,/BASELINE_PARENT_REFERENCE_REJECTED:.*CURRENT_RUN_BASELINE_REFERENCE_PARENT_PIN_MISSING/);
 assert.equal(results[0].sharedReference,undefined);assert.equal(verdictFor({results,selectedCount:2}).exitCode,2);
});
