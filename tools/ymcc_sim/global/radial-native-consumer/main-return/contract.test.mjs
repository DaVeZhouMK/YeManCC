import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import assert from 'node:assert/strict';
import {REPO,MODULE,getRadialNativeConsumerAdapters,inputFiles} from '../adapter.mjs';
import {readJson,sha256} from '../helper.mjs';
import {publicStagesPassed} from '../run.mjs';
import {verifyExecutionCompletion} from '../../execution-completion/index.mjs';
import {assembleMainReturn,extractBlock} from './prepare.mjs';
import {validateMainNative,requireThisMainOriginal} from './run.mjs';
const leaf=path.join(REPO,MODULE),here=leaf+'/main-return',contract=readJson(here+'/contract.json'),plan=readJson(here+'/plan.json');
const sources=Object.fromEntries(contract.productSources.map(r=>[r.path,fs.readFileSync(path.join(REPO,r.path))]));
const templates=Object.fromEntries(contract.templates.map(r=>[r.path,fs.readFileSync(here+'/'+r.path)]));templates['MemoryPorts.hpp']=fs.readFileSync(leaf+'/MemoryPorts.hpp');
const terminal=code=>({pid:123,naturalExit:code,signal:null,timedOut:false,launchError:null,killed:false,exitObserved:true,closed:true,outputComplete:true,outputTruncated:false});
const returned={closed:true,ownerNull:true,window:0,inputWindow:0,viewNull:true,controllerNull:true};
function sample(variant){const fault=plan.sourceFaults.find(f=>f.id===variant),ids=fault?[fault.case]:plan.positiveCases.map(c=>c.id),status=fault?'ASSERTION_FAILED':'PASS';return {status,naturalExit:fault?23:0,actualMainReturnAndAdmission:true,realHostIntentExecuted:false,postCreateSeedOnly:true,cases:ids.map(id=>({id,status,...(fault?{assertion:fault.expectedAssertion}:{}),before:{contractOnly:true},beforeCleanup:{contractOnly:true},afterCleanup:{resources:{windows:0,regions:0,wireAllocations:0,queuedMessages:0},queuedActions:0,active:false,mainOwned:false,states:[returned]},trace:[{op:'validator-contract-only'}]}))};}
function validate(native,variant,overrides={}){return validateMainNative({native,variant,plan,terminal:terminal(native.naturalExit),stdout:native.cases.map(c=>c.id+':'+c.status+'\n').join(''),stderr:'',...overrides});}

test('V2 uses the same unique ID and requires post-create,main and wrapper results',()=>{
 const adapters=getRadialNativeConsumerAdapters();assert.equal(adapters.length,1);const a=adapters[0];assert.equal(a.id,'controller.radial-native-consumer');assert.equal(a.kind,'powershell');assert.equal(a.artifactInputs.length,1191);
 assert.deepEqual(a.resultFiles,['radial-native/result.json','radial-native/post-create-result.json','radial-native/main-return/result.json','radial-native/wrapper-terminal.json']);
 assert.equal(a.completionContract.rules[0].line,'RADIAL_NATIVE_CONSUMER_V2_COMPLETED postCreate=12+2 mainReturn=12+2');assert.equal(a.timeoutMs,480000);
 assert.equal(inputFiles().length,22);for(const r of contract.productSources)assert(inputFiles().includes(r.path));
 assert(!inputFiles().some(r=>r==='src/radial/host.ts'||r==='tools/radial_host_selftest.mjs'));
});

test('new completion rejects old three-group marker even after process exit0',()=>{
 const contract=getRadialNativeConsumerAdapters()[0].completionContract,process={exitCode:0,signal:null,timedOut:false,stdout:'RADIAL_NATIVE_CONSUMER_COMPLETED original=12 sourceFaults=2\n',stderr:''};
 assert.equal(verifyExecutionCompletion(process,contract).status,'FAIL');process.stdout=contract.rules[0].line+'\n';assert.equal(verifyExecutionCompletion(process,contract).status,'PASS');
});

test('actual root acceptance cannot pass with only old groups or a missing/failed main stage',()=>{
 const three=[{},{},{}],post={schemaVersion:1,scopeVersion:2,stage:'post-create',status:'PASS',passed:true,groups:three},main={...post,stage:'main-return'},args={error:null,groups:three,postCreateStage:post,mainReturnStage:main,toolPre:{status:'PASS'},toolPost:{status:'PASS'},sourcePost:{status:'PASS'}};
 assert.equal(publicStagesPassed(args),true);for(const changed of [{mainReturnStage:null},{postCreateStage:null},{mainReturnStage:{...main,passed:false}},{mainReturnStage:{...main,scopeVersion:1}},{mainReturnStage:{...main,groups:[]}},{mainReturnStage:{...main,stage:'post-create'}},{toolPost:{status:'FAIL'}},{error:{message:'source drift'}}])assert.equal(publicStagesPassed({...args,...changed}),false);
});

test('current source assembles thirty complete definitions and fifty-four exact declarations',()=>{
 const a=assembleMainReturn(sources,templates,contract);assert.equal(a.definitions.length,30);assert.equal(a.declarations.length,54);assert.equal(sha256(a.variants.original),contract.reviewedSelectedMainSha256);assert.equal(sha256(a.radial),contract.reviewedSelectedRadialSha256);
 for(const d of [...a.definitions,...a.declarations]){assert.equal(sources[d.source].subarray(d.sourceByteStart,d.sourceByteEnd).toString('utf8'),d.exact);assert.equal(sha256(d.exact),d.sha256);}
 for(const name of ['gamepadEval','nativeYmccShortcutSubmit','nativeYmccShortcutEmit','radialInitialize-lambda-declaration'])assert(a.definitions.some(d=>d.name===name));
 assert(a.variants.original.includes('bool allowRadialOwner = false'));assert(a.variants.original.includes('queued-radial-owner-or-return'));assert(a.variants.original.includes('keyboardWmiFallback,producerTokens,true);'));
});

test('complete extraction finds the body after default empty-initializer arguments',()=>{
 const text='prefix\nstatic bool queue(T tokens={},bool allow=false){const char* x="} {";/* } */return allow;}\nsuffix';const d=extractBlock(text,'static bool queue(');assert(d.exact.endsWith('return allow;}'));assert(d.exact.includes('tokens={}'));assert.equal(d.sourceStartLine,2);
 assert.throws(()=>extractBlock(text+text,'static bool queue('),/ANCHOR_NOT_UNIQUE/);
});

test('whole current-source changes reject even when selected functions could remain equal',()=>{
 const changed={...sources,'native/main.cpp':Buffer.concat([sources['native/main.cpp'],Buffer.from('\n// unrelated source drift\n')])};assert.throws(()=>assembleMainReturn(changed,templates,contract),/WHOLE_SOURCE_NEEDS_EXPLICIT_MAINTENANCE:native\/main.cpp/);
});

test('function and declaration changes still reject after only a whole-file hash update',()=>{
 const text=sources['native/main.cpp'].toString('utf8');
 const changedFunction={...sources,'native/main.cpp':Buffer.from(text.replace('return g_radialUiOwnsInput.load(std::memory_order_acquire)||g_radialReturning.load(std::memory_order_acquire);','return false;'))};
 const f=structuredClone(contract);f.productSources[0].sha256=sha256(changedFunction['native/main.cpp']);assert.throws(()=>assembleMainReturn(changedFunction,templates,f),/DEFINITION_NEEDS_EXPLICIT_MAINTENANCE:radialHasInputOwner/);
 const changedDeclaration={...sources,'native/main.cpp':Buffer.from(text.replace('static std::atomic<bool> g_radialReturning{false};','static std::atomic<bool> g_radialReturning{true};'))};const d=structuredClone(contract);d.productSources[0].sha256=sha256(changedDeclaration['native/main.cpp']);assert.throws(()=>assembleMainReturn(changedDeclaration,templates,d),/DECLARATION_NEEDS_EXPLICIT_MAINTENANCE:g_radialReturning/);
});

test('all memory ports and original twelve-case template require explicit byte maintenance',()=>{
 for(const name of ['MainPlatform.hpp','MainEndpoints.hpp','MainCases.hpp','MemoryPorts.hpp']){
  const changed={...templates,[name]:Buffer.concat([templates[name],Buffer.from('\n')])};assert.throws(()=>assembleMainReturn(sources,changed,contract),/PORT_SIGNATURE_OR_CASE_NEEDS_EXPLICIT_MAINTENANCE/);
 }
 const vector=templates['MainCases.hpp'].toString('utf8').match(/inline const std::vector<std::string> ids=\{([^}]+)\}/);assert(vector);assert.deepEqual([...vector[1].matchAll(/"([^"]+)"/g)].map(m=>m[1]),plan.positiveCases.map(r=>r.id));
 assert.equal(plan.positiveNaturalExit,0);assert.equal(plan.positiveCases.length,12);assert.equal(plan.sourceFaults.length,2);
});

test('two faults are unique edits of actual current functions with exact retained assertions',()=>{
 const a=assembleMainReturn(sources,templates,contract);for(const fault of contract.faults){assert.equal(a.variants.original.split(fault.before).length,2);assert.equal(a.variants[fault.id],a.variants.original.replace(fault.before,fault.after));const p=plan.sourceFaults.find(f=>f.id===fault.id);assert(p);assert.equal(p.expectedNaturalExit,23);assert(templates['MainCases.hpp'].toString('utf8').includes('"'+p.expectedAssertion+'"'));}
});

test('same-run main original12 cannot be borrowed from post-create or old/cross-source receipts',()=>{
 const stageRoot=path.resolve(REPO,'..','main-contract-root','main-return'),ctx={runId:'current-run',stageRoot,sourceFingerprint:'current-sources'},r={schemaVersion:1,scopeVersion:2,stage:'main-return',status:'PASS',variant:'original',runId:ctx.runId,stageRoot,directory:path.join(stageRoot,'runs/original'),sourceFingerprint:ctx.sourceFingerprint,cases:plan.positiveCases.map(p=>({id:p.id,status:'PASS'})),nativeNaturalExit:0};
 assert.equal(requireThisMainOriginal(r,ctx),true);
 for(const change of [{stage:'post-create'},{runId:'previous-run'},{scopeVersion:1},{sourceFingerprint:'old-source'},{stageRoot:path.dirname(stageRoot)},{directory:path.resolve(stageRoot,'../runs/original')},{status:'COMPILE_FAILED'},{cases:r.cases.slice(0,11)}])assert.throws(()=>requireThisMainOriginal({...r,...change},ctx),/THIS_STAGE_THIS_RUN_ORIGINAL12/);
});

test('main validator requires exact semantic23,complete output and all endpoint returns',()=>{
 assert.equal(validate(sample('original'),'original').length,12);
 for(const variant of ['neutral-and-to-or','queued-radial-gate-removed']){const native=sample(variant);assert.equal(validate(native,variant).length,1);const wrong=structuredClone(native);wrong.cases[0].assertion='compiler failed';assert.throws(()=>validate(wrong,variant),/WRONG_SEMANTIC_ASSERTION/);assert.throws(()=>validate(native,variant,{terminal:terminal(2)}),/NATIVE_EXIT_SIGNAL_TIMEOUT_OR_OUTPUT/);assert.throws(()=>validate(native,variant,{terminal:{...terminal(23),timedOut:true}}),/NATIVE_EXIT_SIGNAL_TIMEOUT_OR_OUTPUT/);}
 const positive=sample('original');assert.throws(()=>validate(positive,'original',{stdout:''}),/OUTPUT_INCOMPLETE/);assert.throws(()=>validate(positive,'original',{stderr:'unexpected'}),/OUTPUT_INCOMPLETE/);
 for(const mutate of [n=>n.cases[0].afterCleanup.resources.regions=1,n=>n.cases[0].afterCleanup.queuedActions=1,n=>n.cases[0].afterCleanup.states[0].closed=false]){const changed=structuredClone(positive);mutate(changed);assert.throws(()=>validate(changed,'original'),/NOT_RETURNED/);}
});

test('runtime main leaf has no historical path imports or host/action/TDP execution wiring',()=>{
 for(const name of ['prepare.mjs','run.mjs'])assert(!/[dD]:[\\/]|radial-main-return02|source-original|radial_host_selftest|startRadialHost/.test(fs.readFileSync(here+'/'+name,'utf8')),name);
 const run=fs.readFileSync(here+'/run.mjs','utf8');assert(run.includes('requireThisMainOriginal(readJson(original.receipt.path)'));assert(run.includes('naturalTerminal(compiler,0)'));assert(run.includes('naturalTerminal(terminal,fault?23:0)'));
 assert(!inputFiles().some(r=>/src\/radial\/host\.ts|radial_host_selftest/.test(r)));
});
