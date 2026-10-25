import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { fileURLToPath } from 'node:url';
import { sourcePath, validateAdapters, executeAdapter, evaluateResult, verdictFor, compareRuns, runProcess, makeCoverageWorkItems, MAX_ARTIFACT_INPUTS } from './runner.mjs';
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