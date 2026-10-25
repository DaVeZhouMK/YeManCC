import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import assert from 'node:assert/strict';
import {REPO,MODULE,CASE_ID,inputFiles,getRadialNativeConsumerAdapters,toolchainSpec} from './adapter.mjs';
import {sha256,readJson,validateToolManifest,exactMembers,evidencePath,naturalTerminal,validateNativeResult,requireCurrentOriginal} from './helper.mjs';
import {assembleSources,extractBlock,verifyTemplates} from './prepare.mjs';
import {validateAdapters} from '../runner.mjs';

const leaf=path.join(REPO,MODULE),contract=readJson(path.join(leaf,'extraction-contract.json')),plan=readJson(path.join(leaf,'plan.json'));
const sources=Object.fromEntries(contract.productSources.map(relative=>[relative,fs.readFileSync(path.join(REPO,relative))]));
const templates=Object.fromEntries(contract.templates.map(row=>[row.path,fs.readFileSync(path.join(leaf,row.path))]));
const terminal=(code=0)=>({pid:123,naturalExit:code,signal:null,timedOut:false,launchError:null,killed:false,exitObserved:true,closed:true,outputComplete:true,outputTruncated:false});
const returnedState={closed:true,ownerNull:true,window:0,inputWindow:0,viewNull:true,controllerNull:true,compositionNull:true,deviceNull:true,targetNull:true,visualNull:true};
function syntheticNative(variant){
 const fault=plan.sourceFaults.find(f=>f.id===variant),ids=fault?[fault.case]:plan.positiveCases.map(c=>c.id),status=fault?'ASSERTION_FAILED':'PASS';
 return {chain:'actual-extracted-native-post-create-consumer',postCreateSeedOnly:true,actualCreateExecuted:false,realOverlayOrWindowCreated:false,status,naturalExit:fault?23:0,cases:ids.map(id=>({id,status,...(fault?{assertion:fault.expectedAssertion}:{}),before:{contractOnly:true},beforeCleanup:{contractOnly:true},afterCleanup:{resources:{windows:0,regions:0,wireAllocations:0,queuedMessages:0},environmentNull:true,currentNull:true,active:false,states:[returnedState]},trace:[{op:'validator-contract-only'}]}))};
}
function validation(native,variant,overrides={}){return validateNativeResult({native,variant,terminal:terminal(native.naturalExit),plan,stdout:native.cases.map(c=>c.id+':'+c.status+'\n').join(''),stderr:'',...overrides});}

test('public descriptor uses the existing powershell Job path and declares only actual source scope',()=>{
 const adapters=getRadialNativeConsumerAdapters();validateAdapters(adapters,REPO);assert.equal(adapters.length,1);const a=adapters[0];
 assert.equal(a.id,CASE_ID);assert.equal(a.kind,'powershell');assert.equal(a.path,MODULE+'/Run-Radial.ps1');assert.equal(a.artifactInputs.length,1191);
 assert.deepEqual(a.resultFiles,['radial-native/result.json','radial-native/post-create-result.json','radial-native/main-return/result.json','radial-native/wrapper-terminal.json']);
 assert.deepEqual(a.completionContract.rules,[{line:'RADIAL_NATIVE_CONSUMER_V2_COMPLETED postCreate=12+2 mainReturn=12+2'}]);
 const inputs=inputFiles();assert.equal(new Set(inputs).size,22);assert.deepEqual(inputs.filter(s=>s.startsWith('native/')||s.startsWith('deps/')).sort(),['native/main.cpp','native/radial_overlay.h','native/radial_input_region.h','deps/json/json.hpp'].sort());
 assert(inputs.includes('tools/ymcc_sim/global/host-process.ps1'));assert(!inputs.some(s=>/ui-navigation|radial_native_selftest|radial_host_selftest|src\/radial\/host|source-original|[dD]:/.test(s)));
});

test('independent declaration preserves all core533 and selected powershell658 and four complete directories',()=>{
 const tool=toolchainSpec();validateToolManifest(tool);assert.equal(tool.files.length,1191);assert.equal(tool.files.filter(f=>f.role==='selected-powershell-complete-runtime').length,658);
 assert.deepEqual(tool.directories.map(d=>d.members),[91,361,66,658]);assert.equal(new Set(tool.files.map(f=>path.resolve(f.path).toLowerCase())).size,1191);
 for(const name of ['node','cl','link','powershell'])assert(tool.files.some(f=>f.path===tool.executables[name].path&&f.sha256===tool.executables[name].sha256));
 const shortened=structuredClone(tool);shortened.files.pop();assert.throws(()=>validateToolManifest(shortened),/RADIAL_TOOL_COMPLETE_COUNTS/);
 const duplicate=structuredClone(tool);duplicate.files[1190]=duplicate.files[1189];assert.throws(()=>validateToolManifest(duplicate),/RADIAL_TOOL_DUPLICATE_MEMBER/);
});

test('complete membership rejects extra, missing and replaced final members',()=>{
 const members=['alpha','beta','tail'].map(x=>path.join(leaf,x));assert.equal(exactMembers([...members].reverse(),members,3),true);
 assert.throws(()=>exactMembers(members.slice(0,2),members,3),/MEMBERSHIP_CHANGED/);
 assert.throws(()=>exactMembers([...members,path.join(leaf,'extra')],members,3),/MEMBERSHIP_CHANGED/);
 assert.throws(()=>exactMembers([...members.slice(0,2),path.join(leaf,'changed-tail')],members,3),/MEMBERSHIP_CHANGED/);
});

test('fresh current buffers yield the same thirteen complete production definitions and reviewed assembly',()=>{
 const result=assembleSources(sources,contract);assert.equal(result.definitions.length,13);assert.equal(sha256(result.variants.original),contract.reviewedAssembledOriginalSha256);
 for(const d of result.definitions){const original=sources[d.source].subarray(d.sourceByteStart,d.sourceByteEnd);assert.equal(original.toString('utf8'),d.exact);assert.equal(sha256(original),d.exactSha256);assert.equal(result.variants.original.split(d.exact).length,2);}
 assert.equal(result.definitions.filter(d=>d.source==='native/radial_overlay.h').length,12);assert.equal(result.definitions.filter(d=>d.source==='native/radial_input_region.h').length,1);
});

test('production definition or native declaration changes require explicit maintenance',()=>{
 const changed={...sources,'native/radial_overlay.h':Buffer.from(sources['native/radial_overlay.h'].toString('utf8').replace('return current==s&&!s->closed;','return static_cast<bool>(s);'))};
 assert.throws(()=>assembleSources(changed,contract),/DEFINITION_NEEDS_EXPLICIT_MAINTENANCE:Overlay::owns/);
 const fields={...sources,'native/radial_overlay.h':Buffer.from(sources['native/radial_overlay.h'].toString('utf8').replace('int nextId=1;','int nextId=9;'))};
 assert.throws(()=>assembleSources(fields,contract),/DECLARATION_NEEDS_EXPLICIT_MAINTENANCE:fields/);
 const changedJson={...sources,'deps/json/json.hpp':Buffer.concat([sources['deps/json/json.hpp'],Buffer.from('\n')])};assert.throws(()=>assembleSources(changedJson,contract),/JSON_SOURCE_NEEDS_EXPLICIT_MAINTENANCE/);
});

test('memory-port signatures and unchanged scenario template are pinned as full bytes',()=>{
 assert.equal(verifyTemplates(templates,contract),true);
 for(const name of ['MemoryPorts.hpp','ConsumerCases.hpp']){
  const changed={...templates,[name]:Buffer.concat([templates[name],Buffer.from('\n// signature maintenance\n')])};
  assert.throws(()=>verifyTemplates(changed,contract),new RegExp('TEMPLATE_NEEDS_EXPLICIT_MAINTENANCE:'+name.replace('.','\\.')));
 }
});

test('fixed twelve cases and two unique semantic source edits remain exact',()=>{
 assert.equal(plan.positiveCases.length,12);assert.equal(plan.positiveNaturalExit,0);assert.equal(plan.sourceFaults.length,2);
 const vector=templates['ConsumerCases.hpp'].toString('utf8').match(/inline const std::vector<std::string> cases=\{([^}]+)\}/);assert(vector);
 assert.deepEqual([...vector[1].matchAll(/"([^"]+)"/g)].map(m=>m[1]),plan.positiveCases.map(c=>c.id));
 const result=assembleSources(sources,contract);
 for(const fault of contract.faults){assert.equal(result.variants.original.split(fault.before).length,2);assert.equal(result.variants[fault.id],result.variants.original.replace(fault.before,fault.after));const expected=plan.sourceFaults.find(f=>f.id===fault.id);assert(expected);assert.equal(expected.expectedNaturalExit,23);assert(templates['ConsumerCases.hpp'].toString('utf8').includes('"'+expected.expectedAssertion+'"'));}
});

test('brace extraction treats comments and quoted brace characters as data',()=>{
 const text='before\n bool sample(){const char* s="} {"; /* } */ if(true){return true;} // }\n return false;}\nafter';
 const result=extractBlock(text,' bool sample(');assert.equal(result.sourceStartLine,2);assert(result.exact.endsWith('return false;}'));assert.equal(text.slice(Buffer.byteLength(text.slice(0,text.indexOf(' bool sample(')))),result.exact+'\nafter');
 assert.throws(()=>extractBlock(text+text,' bool sample('),/ANCHOR_NOT_UNIQUE/);
});

test('natural exits reject signal timeout missing-exit incomplete-output and launch failures',()=>{
 assert.equal(naturalTerminal(terminal(),0),true);assert.equal(naturalTerminal(terminal(23),23),true);assert.equal(naturalTerminal(terminal(2),23),false);
 for(const change of [{pid:null},{signal:'SIGTERM'},{timedOut:true},{launchError:'ENOENT'},{closed:false},{exitObserved:false},{outputComplete:false},{outputTruncated:true},{killed:true},{naturalExit:null}])assert.equal(naturalTerminal({...terminal(),...change},0),false);
});

test('validator contracts reject wrong fault, nonnatural exit, incomplete output and nonzero cleanup',()=>{
 const original=syntheticNative('original');assert.equal(validation(original,'original').length,12);
 for(const variant of ['owner-or-to-and','generation-gate-removed']){
  const fault=syntheticNative(variant);assert.equal(validation(fault,variant).length,1);
  const wrong=structuredClone(fault);wrong.cases[0].assertion='compiler-failed';assert.throws(()=>validation(wrong,variant),/WRONG_FAILURE_ASSERTION/);
  assert.throws(()=>validation(fault,variant,{terminal:terminal(2)}),/NOT_EXPECTED_NATURAL_EXIT/);
 }
 assert.throws(()=>validation(original,'original',{stdout:''}),/OUTPUT_INCOMPLETE/);
 assert.throws(()=>validation(original,'original',{stderr:'unexpected'}),/OUTPUT_INCOMPLETE/);
 const leaked=structuredClone(original);leaked.cases[0].afterCleanup.resources.regions=1;assert.throws(()=>validation(leaked,'original'),/CLEANUP_NOT_ZERO/);
 const empty=structuredClone(original);empty.cases=[];assert.throws(()=>validation(empty,'original'),/CASE_COUNT/);
});

test('source faults require the same current run original12 rather than any historical receipt',()=>{
 const root=path.resolve(REPO,'..','radial-contract-output'),context={runId:'current-run',evidenceRoot:root};
 const receipt={status:'PASS',variant:'original',runId:'current-run',evidenceRoot:root,directory:path.join(root,'runs','original'),cases:plan.positiveCases.map(c=>({id:c.id,status:'PASS'})),nativeNaturalExit:0};
 assert.equal(requireCurrentOriginal(receipt,context),true);
 for(const changed of [{runId:'previous-run'},{evidenceRoot:path.join(root,'other')},{directory:path.resolve(root,'..','historical')},{nativeNaturalExit:23},{status:'COMPILE_FAILED'},{cases:receipt.cases.slice(0,11)}])assert.throws(()=>requireCurrentOriginal({...receipt,...changed},context),/THIS_RUN_ORIGINAL12/);
});

test('owned evidence path requires a fresh external root and cannot target source or its ancestors',()=>{
 assert.equal(evidencePath(path.resolve(REPO,'..','radial-contract-output'),REPO),path.resolve(REPO,'..','radial-contract-output'));
 for(const target of [REPO,path.join(REPO,'temp'),path.dirname(REPO),path.parse(REPO).root,'relative-path'])assert.throws(()=>evidencePath(target,REPO));
});

test('runtime leaves have no historical artifact or UI-manifest imports and no existing native-selftest execution',()=>{
 for(const name of ['adapter.mjs','run.mjs','prepare.mjs','helper.mjs','Run-Radial.ps1']){
  const text=fs.readFileSync(path.join(leaf,name),'utf8');assert(!/[dD]:[\\/]|radial-native-consumer01|Y23|Y24|ui-navigation\/toolchain-inputs|radial_native_selftest/.test(text),name);
 }
 const run=fs.readFileSync(path.join(leaf,'run.mjs'),'utf8');assert(run.includes("requireCurrentOriginal(readJson(original.receipt.path)"));assert(run.includes("naturalTerminal(compiler,0)"));assert(run.includes("sourcePost=verifySources(sourceRows)"));assert(run.includes("toolPost=verifyToolDeclaration(tools)"));
});
