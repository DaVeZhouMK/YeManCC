import fs from 'node:fs';import path from 'node:path';import test from 'node:test';import assert from 'node:assert/strict';import {createHash} from 'node:crypto';
import {readVirtualControllerSource} from '../../../virtual_controller_runtime_source.mjs';
import {extract} from '../../../fixtures/legion_motion_consumer/extract_main.mjs';
import {transformCurrentEnvironmentRecipe,applyCurrentEnvironmentRecipe} from '../../../input_coordinator_current_environment_recipe.mjs';
import {beginCurrentCompileBatch,requireCurrentCompileBatch,finishCurrentCompileBatch,verifyCurrentManagedSourceRows} from './r3-current-compile-batch.mjs';
import {buildManagedFixture} from './r3-current-managed-builder.mjs';
import {buildCurrentCompileOnly} from '../../../input_coordinator_current_compile_only.mjs';
import {optimizeManagedHashes} from './r3-current-managed-hash.mjs';
const repo=path.resolve(import.meta.dirname,'../../../..'),sha=bytes=>createHash('sha256').update(bytes).digest('hex');
let source,producer,baseline;
const readSource=()=>source??=readVirtualControllerSource(repo+'/native/main.cpp').source;
const readProducer=()=>producer??=fs.readFileSync(repo+'/tools/input_coordinator_lifecycle_selftest.mjs','utf8');
const prepare=(text=readSource(),script=readProducer())=>transformCurrentEnvironmentRecipe(text,{producerSource:script,expectedExpandedSha256:sha(text)});
// Keep the original refusal in each dependent test. Negative mutations must start from an admitted baseline.
function recipeTest(name,run){test(name,()=>{
 if(!baseline){try{baseline={actual:prepare()};}catch(error){baseline={error};}}
 if('error' in baseline)throw baseline.error;
 return run(baseline.actual);
});}
recipeTest('explicit current full-source28 recipe restores every byte and leaves60 business cores unchanged',actual=>{assert.equal(actual.substitutions.length,28);assert.equal(new Set(actual.substitutions.map(r=>r.symbol)).size,28);assert.equal(actual.core.length,60);assert(actual.core.every(r=>r.unchanged&&r.sourceSha256===r.transformedSha256));assert(actual.coldEnvironment.unchanged);assert.equal(actual.restore(actual.inert),source);assert.equal(actual.scope.compilerExecuted,false);assert.equal(actual.scope.fullOriginalConsumerAcceptance,false);});
recipeTest('all seven additional CLI-writer and cold-return forbidden leaves remain throwing',actual=>{const names=['hidHideCliWriterRecordPath','hidHideCliWriterMachineBinding','hidHideCliWriterCapture','hidHideCliWriterProbe','hidHideCliWriterProbeJob','inputHostTargetReturnColdAbsence','inputHostTargetReturnColdHidden'];for(const name of names){assert(actual.substitutions.some(row=>row.symbol===name));const code=extract(actual.inert,name).code;assert(code.includes('++forbiddenIo;throw std::logic_error'));}assert.equal(names.length,7);});
recipeTest('original XInput output refusal preserves its typed device-not-connected error',actual=>{const code=extract(actual.inert,'ymccXInputSetState14').code;assert(code.includes('++forbiddenIo;return ERROR_DEVICE_NOT_CONNECTED;'));});
recipeTest('a changed original source recipe cannot borrow a current source digest',()=>{assert.throws(()=>prepare(source,producer+'\n// unknown producer change'),/PRODUCER_IDENTITY/);});
test('an unknown expanded source cannot borrow another capture digest',()=>{const text=readSource();assert.throws(()=>transformCurrentEnvironmentRecipe(text+'\n// other capture',{producerSource:readProducer(),expectedExpandedSha256:sha(text)}),/EXPLICIT_EXPANDED/);});
recipeTest('one changed business core is rejected even with a matching whole-source digest',()=>{const body=extract(source,'inputHostTargetReturnBeforeCreateLocked').code,changed=body.replace('{','{ /* changed source core */');assert.notEqual(changed,body);assert.throws(()=>prepare(source.replace(body,changed)),/Reviewed original core changed/);});
recipeTest('cold environment type cannot silently change under a new capture',()=>{const body=extract(source,'ColdEnvironment','struct').code,changed=body.replace('{','{ /* changed contract */');assert.throws(()=>prepare(source.replace(body,changed)));});
recipeTest('inverse rejects a changed generated environment body',actual=>{const row=actual.substitutions.find(r=>r.symbol==='hidHideRun'),body=extract(actual.inert,row.symbol).code,changed=body.replace('++forbiddenIo','++forbiddenIo; ++forbiddenIo');assert.throws(()=>actual.restore(actual.inert.replace(body,changed)),/RESTORE/);});
for(const fake of [{},{kind:'CURRENT_CAPTURE_COMPILE_ONLY_IN_PROCESS_CAPABILITY'},JSON.parse('{"kind":"CURRENT_CAPTURE_COMPILE_ONLY_IN_PROCESS_CAPABILITY"}')])test('serialized compile token has no member or finish authority '+JSON.stringify(fake),()=>{assert.throws(()=>requireCurrentCompileBatch(fake),/LIVE_BRANDED_BATCH/);assert.throws(()=>finishCurrentCompileBatch(fake),/LIVE_BRANDED_BATCH/);assert.throws(()=>verifyCurrentManagedSourceRows({inputBindings:{sources:[]}},repo,{batchAuthority:fake}),/LIVE_BRANDED_BATCH/);});
test('environment transform for actual compilation requires live full-source authority',()=>{const text=readSource(),script=readProducer();assert.throws(()=>applyCurrentEnvironmentRecipe(text,{producerSource:script,batchAuthority:{kind:'CURRENT_CAPTURE_COMPILE_ONLY_IN_PROCESS_CAPABILITY'}}),/LIVE_BRANDED_BATCH/);});
test('managed builder rejects forged authority before creating output or running compiler',()=>{assert.throws(()=>buildManagedFixture('D:/not-created-current-maintenance/managed',{sourceRoot:repo,batchAuthority:{}}),/LIVE_BRANDED_BATCH/);assert.equal(fs.existsSync('D:/not-created-current-maintenance/managed'),false);});
test('native producer rejects forged authority before output or compiler execution',()=>{assert.throws(()=>buildCurrentCompileOnly({argv:['--compile-only','--out','D:/not-created-current-maintenance/native','--source-root',repo,'--managed-dll','D:/not-a-current-fixture.dll'],batchAuthority:{}}),/LIVE_BRANDED_BATCH/);assert.equal(fs.existsSync('D:/not-created-current-maintenance/native'),false);});
test('strict Terminal29 parallel source bookends preserve compile authority',async t=>{
 const {createCapturedSourceSnapshot}=await import('../source-snapshot.mjs');
 const {bindSnapshotNodeTools,DEFAULT_SEED_PACKAGES}=await import('../snapshot-node-tools.mjs');
 const {managedCompileContract,TERMINAL_SHARED_INPUT_POWER_MANAGED_CONTRACT}=await import('./r3-managed-input-contract.mjs');
 const workspace=path.resolve(process.env.YMCC_CORE_LAYOUT_TEST_TEMP??path.resolve(repo,'../../../Testing/YMCC/Workspace'));
 assert.equal(path.resolve(fs.realpathSync.native(workspace)).toLowerCase(),workspace.toLowerCase());
 const owner=fs.mkdtempSync(path.join(workspace,'terminal29-bookends-'));
 t.after(()=>{
  assert.equal(path.dirname(owner),workspace);assert.match(path.basename(owner),/^terminal29-bookends-/);
  assert.equal(path.resolve(fs.realpathSync.native(owner)).toLowerCase(),owner.toLowerCase());
  fs.rmSync(owner,{recursive:true});
 });
 const primaryRoot=path.join(owner,'Primary'),sourceRoot=path.join(owner,'Frozen'),authorizedBuildRoot=path.join(owner,'Build/Validation/Batch');
 const tool=path.join(owner,'unexecuted-tool.fixture'),mainText='// synthetic compile-only input\n';
 const fileSha=file=>sha(fs.readFileSync(file));
 function write(root,relative,text){const file=path.join(root,relative);fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,text);return file;}
 const main=write(primaryRoot,'native/main.cpp',mainText),unrelated=write(primaryRoot,'tools/observed.txt','original');
 write(owner,path.basename(tool),'synthetic compiler bytes');
 fs.mkdirSync(authorizedBuildRoot,{recursive:true});
 write(primaryRoot,'package.json','{"name":"bookends-control","version":"1.0.0"}');
 for(const name of DEFAULT_SEED_PACKAGES){
  write(primaryRoot,`node_modules/${name}/package.json`,JSON.stringify({name,version:'1.0.0',main:'index.cjs'}));
  write(primaryRoot,`node_modules/${name}/index.cjs`,'throw Error("package code must not be executed");\n');
 }
 const managedInputContract=TERMINAL_SHARED_INPUT_POWER_MANAGED_CONTRACT;
 const expectedManagedSources=managedCompileContract(managedInputContract).names.map(relative=>({relative,sha256:fileSha(write(primaryRoot,relative,'// '+relative+'\n'))}));
 const capture=createCapturedSourceSnapshot({repoRoot:primaryRoot,outDir:sourceRoot});
 assert.equal(capture.status,'PASS',JSON.stringify(capture.failure));
 const manifestSha256=fileSha(capture.manifestFile);
 const binding=bindSnapshotNodeTools({primaryRepo:primaryRoot,manifestFile:capture.manifestFile,expectedSnapshotManifestSha256:manifestSha256});
 assert.equal(binding.status,'PASS',JSON.stringify(binding.failure));
 const layout={schemaVersion:1,sourceAuthority:{mode:'EXPLICIT_CURRENT_CAPTURE_COMPILE_ONLY'},managedInputContract,
  primaryRoot,sourceRoot,workspaceRoot:owner,authorizedBuildRoot,expectedManagedSources,
  productMainSha256:fileSha(main),manifestSha256,expandedSourceSha256:sha(mainText),
  nodeBindingSha256:fileSha(binding.bindingManifestFile),
  verifierCodeSha256:fileSha(new URL('../source-snapshot.mjs',import.meta.url)),
  expectationsSha256:fileSha(new URL('./r3-current105-expectations.json',import.meta.url)),
  managedContractCodeSha256:fileSha(new URL('./r3-managed-input-contract.mjs',import.meta.url)),dotnet:{path:tool,sha256:fileSha(tool)}};
 const opts=(name,more)=>({layout,actualToolFiles:[tool],receiptDirectory:path.join(authorizedBuildRoot,name),requireCurrentSource:true,parallelSource:true,...more});
 const read=(name,phase)=>JSON.parse(fs.readFileSync(path.join(authorizedBuildRoot,name,`authority-${phase}.json`),'utf8'));
 const parallel='parallel-primary-and-copy';
 await t.test('serial callers keep synchronous token and two complete checks',()=>{
  const token=beginCurrentCompileBatch(opts('serial',{parallelSource:false}));
  assert.equal(requireCurrentCompileBatch(token).sourceRoot,sourceRoot);
  const result=finishCurrentCompileBatch(token);
  assert.equal(result.status,'AUTHORITY_STABLE');assert.equal(result.fullSourceVerificationCalls,2);assert.equal(result.sourceVerificationMode,undefined);
 });
 await t.test('parallel bookends preserve current proof and revoke token before finish yields',async()=>{
  const token=await beginCurrentCompileBatch(opts('parallel'));
  const before=read('parallel','before');assert.equal(before.sourceVerificationMode,parallel);assert.equal(before.source.isCurrent,true);
  const pending=finishCurrentCompileBatch(token);
  assert.throws(()=>requireCurrentCompileBatch(token),/LIVE_BRANDED_BATCH_REQUIRED/);
  assert.throws(()=>finishCurrentCompileBatch(token),/LIVE_BRANDED_BATCH_REQUIRED/);
  const after=await pending;
  assert.equal(after.source.isCurrent,true);assert.equal(after.status,'AUTHORITY_STABLE');assert.equal(after.fullSourceVerificationCalls,2);
  const omitTimestamp=({verifiedAt,...rest})=>rest;
  assert.deepEqual(omitTimestamp(after.source),omitTimestamp(read('serial','after').source));
  assert.deepEqual(read('parallel','after'),after);
 });
 await t.test('parallel mode cannot weaken the strict current requirement',()=>{
  assert.throws(()=>beginCurrentCompileBatch(opts('frozen',{requireCurrentSource:false})),/PARALLEL_SOURCE_REQUIRES_STRICT_TERMINAL29/);
  assert.equal(fs.existsSync(opts('frozen').receiptDirectory),false);
 });
 await t.test('stale primary at open keeps the before failure and cannot issue a token',async()=>{
  fs.writeFileSync(unrelated,'changed');
  try{
   await assert.rejects(beginCurrentCompileBatch(opts('stale-open')),/PRIMARY_DRIFT_BEFORE_COMPILE/);
   const before=read('stale-open','before');assert.equal(before.status,'CURRENT_SOURCE_DRIFT_BEFORE_COMPILE');
   assert.equal(before.source.isCurrent,false);assert.equal(before.sourceVerificationMode,parallel);
   assert.equal(before.source.sourceDiff.changed[0].path,'tools/observed.txt');
  }finally{fs.writeFileSync(unrelated,'original');}
 });
 await t.test('post-open source drift cannot be reported stable',async()=>{
  const token=await beginCurrentCompileBatch(opts('stale-close'));
  fs.writeFileSync(unrelated,'changed');
  try{
   await assert.rejects(finishCurrentCompileBatch(token),/PRIMARY_DRIFT_AFTER_COMPILE/);
   const after=read('stale-close','after');assert.equal(after.status,'CURRENT_SOURCE_DRIFT_AFTER_COMPILE');
   assert.equal(after.source.isCurrent,false);assert.equal(after.fullSourceVerificationCalls,2);
  }finally{fs.writeFileSync(unrelated,'original');}
 });
 await t.test('output created during open is not adopted',async()=>{
  const o=opts('occupied-open'),pending=beginCurrentCompileBatch(o);
  write(o.receiptDirectory,'sentinel.txt','by another caller');
  await assert.rejects(pending,/FRESH_AUTHORITY_RECEIPTS_REQUIRED/);
  assert.deepEqual(fs.readdirSync(o.receiptDirectory),['sentinel.txt']);
 });
 await t.test('tool bytes changed during finish cannot get a success receipt',async()=>{
  const consumed=write(owner,'actual-tool.fixture','original'),o=opts('tool-close',{actualToolFiles:[consumed]});
  const token=await beginCurrentCompileBatch(o),pending=finishCurrentCompileBatch(token);
  fs.writeFileSync(consumed,'changed');
  await assert.rejects(pending,/ACTUAL_TOOL_CHANGED/);
  assert.deepEqual(fs.readdirSync(o.receiptDirectory),['authority-before.json']);
 });
});
test('portable managed hash endpoint preserves inverse and original failure text',()=>{
 const entry=fs.readFileSync(repo+'/tools/fixtures/input_lifecycle_integration/ManagedEntry.cs','utf8'),optimized=optimizeManagedHashes(entry);
 assert.equal(optimized.restore(optimized.text),entry);
 assert(optimized.text.includes('FileShare.Read, 65536, FileOptions.SequentialScan'));
 // ManagedHashOrderedControls.cs tests the worker bound and failure drain at runtime.
 for(const call of [
  /return ManagedOrdered2\(manifest\.GetProperty\(property\)\.EnumerateArray\(\)\.ToArray\(\), row =>/,
  /return ManagedOrdered2\(AppDomain\.CurrentDomain\.GetAssemblies\(\)\.Where\(a => !a\.IsDynamic\)/,
  /var outputChecks = ManagedOrdered2\(build\.RootElement\.GetProperty\("runtimeArtifacts"\)\.EnumerateArray\(\)\.ToArray\(\), row =>/,
 ]) assert.match(optimized.text,call);
 for(const text of ['binding-changed:','runtime-artifact-changed:','loaded-assembly-not-bound:'])assert(optimized.text.includes(text));
 assert.throws(()=>optimized.restore(optimized.text.replace('65536','32768')));
});

recipeTest('new dormant Steam owner gets two explicit throw-only offline entry guards',async actual=>{const {transformCurrentSteamRecoveryGuard}=await import('../../../input_coordinator_current_steam_guard.mjs'),header=fs.readFileSync(repo+'/native/steam_original_recovery_runtime.h','utf8'),guard=transformCurrentSteamRecoveryGuard(actual.inert,{runtimeHeaderSource:header});assert.equal(guard.substitutions.length,2);assert(guard.substitutions.every(row=>row.rejectOnly));assert.equal(guard.restore(guard.inert),actual.inert);for(const symbol of ['steamOriginalRecoveryStart','steamOriginalRecoveryEngineHandle']){const code=extract(guard.inert,symbol).code;assert(code.includes('++forbiddenIo;throw std::logic_error'));assert(!code.includes('std::thread('));}assert(guard.inert.includes('#define steamOriginalRecoveryStart fixtureNeverCallOriginalSteamRecoveryStart'));for(const row of actual.core)assert.equal(sha(extract(guard.inert,row.symbol).code),row.sourceSha256);});
recipeTest('new Steam guard rejects changed real header and tampered inverse',async actual=>{const {transformCurrentSteamRecoveryGuard}=await import('../../../input_coordinator_current_steam_guard.mjs'),header=fs.readFileSync(repo+'/native/steam_original_recovery_runtime.h','utf8');assert.throws(()=>transformCurrentSteamRecoveryGuard(actual.inert,{runtimeHeaderSource:header+'\n// drift'}),/EXACT_REAL_RUNTIME_HEADER/);const guard=transformCurrentSteamRecoveryGuard(actual.inert,{runtimeHeaderSource:header});assert.throws(()=>guard.restore(guard.inert.replace('fixtureNeverCallOriginalSteamRecoveryStart','wrong-original-entry')),/INVERSE/);});
