import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {selectCases,parseOptions,writeFreshJson} from './cli.mjs';
import {missingCatalogTools} from '../global/cli.mjs';
import {commonHidProducerBudget} from '../../lib/common_hid_runtime_fixture.mjs';

const resumeChoice=['--test','controller.unified-standard-input','--input-resume-only','--input-resume-case','keep-resubscription'];
const win5Choice=['--test','controller.unified-standard-input','--common-hid-runtime-only','--common-hid-runtime-case','win5-kept-wake'];
test('Win5 kept wake selects only the original HID reader and its source closure',async()=>{
 const selected=await selectCases(parseOptions([...win5Choice,'--mode','plan']));
 assert.deepEqual(selected.selectedIds,['controller.unified-standard-input']);assert.equal(selected.adapters.length,1);assert.equal(selected.deferred.length,0);
 const [adapter]=selected.adapters;
 assert.equal(adapter.path,'tools/input_frame_submission_selftest.mjs');assert.equal(adapter.cacheAcceptance,false);
 assert.equal(adapter.inheritExecutionDeadline,true);
 assert.deepEqual(adapter.args.slice(0,5),['--out','{outDir}/controller-input','--common-hid-runtime-only','--common-hid-runtime-case','win5-kept-wake']);
 assert(!adapter.args.includes('--MainFile'));assert(!adapter.evidenceInputs.includes('native/main.cpp'));
 for(const source of ['tools/background_controller_ui_runtime_selftest.cpp','native/controller_isolation_hid_runtime.h','native/controller_isolation_hid_reader.h','native/controller_hid_source_runtime.h','native/sleep_power_dispatch.h'])assert(adapter.evidenceInputs.includes(source));
 assert.equal(selected.commonHidSelection.defaultParentDispatched,false);
 const baseline=await selectCases(parseOptions(['--test','controller.unified-standard-input','--mode','plan']));
 assert.equal(baseline.commonHidSelection,undefined);assert.equal(baseline.adapters.length,0);
});
test('Win5 kept wake rejects an unknown case, an unused MainFile and mixed execution',async()=>{
 for(const [args,error] of [
  [[...win5Choice.slice(0,-1),'win5-kept-wake-unknown'],/COMMON_HID_EXPLICIT_ORIGINAL_CASE_REQUIRED/],
  [[...win5Choice,'--MainFile',path.resolve(import.meta.filename)],/WIN5_KEPT_WAKE_DOES_NOT_CONSUME_MAIN_FILE/],
  [[...win5Choice,'--full-acceptance'],/COMMON_HID_REQUIRES_ONE_EXPLICIT_ORIGINAL_TEST/],
  [[...win5Choice,'--test','fan.curve'],/COMMON_HID_REQUIRES_ONE_EXPLICIT_ORIGINAL_TEST/],
 ])await assert.rejects(async()=>selectCases(parseOptions([...args,'--mode','plan'])),error);
});
test('Win5 kept wake clips both children to one inherited deadline with cleanup reserves',()=>{
 const started=1000,caseDeadline=65000;
 const compile=commonHidProducerBudget(started,2000,'compile',caseDeadline);
 const runtime=commonHidProducerBudget(started,10000,'runtime',caseDeadline);
 assert.deepEqual(compile,{originalBudgetMs:180000,parentBudgetMs:63000,parentRemainingBeforeMs:62000,deadlineEpochMs:64000,cleanupReserveMs:5000});
 assert.deepEqual(runtime,{originalBudgetMs:60000,parentBudgetMs:63000,parentRemainingBeforeMs:54000,deadlineEpochMs:64000,cleanupReserveMs:5000});
 assert.throws(()=>commonHidProducerBudget(started,59000,'runtime',caseDeadline),/PROJECTION_BUDGET_EXHAUSTED/);
 assert.throws(()=>commonHidProducerBudget(started,2000,'compile',NaN),/COMMON_HID_INHERITED_DEADLINE_REQUIRED/);
 for(const deadline of [undefined,started+400000]){
  const standalone=commonHidProducerBudget(started,2000,'compile',deadline);
  assert.equal(standalone.parentBudgetMs,300000);assert.equal(standalone.deadlineEpochMs,182000);
 }
});
test('input resume explicit plan routes only the original consumer and its tools',async()=>{
 const reads=[],original=fs.readFileSync;
 fs.readFileSync=function(file,...args){reads.push(String(file).replaceAll('\\','/'));return original.call(this,file,...args);};
 let selected;try{selected=await selectCases(parseOptions([...resumeChoice,'--mode','plan']));}finally{fs.readFileSync=original;}
 assert.deepEqual(selected.selectedIds,['controller.unified-standard-input']);assert.equal(selected.adapters.length,1);assert.equal(selected.deferred.length,0);
 const adapter=selected.adapters[0];assert.equal(adapter.path,'tools/input_power_gate_r122_selftest.mjs');assert.equal(adapter.kind,'node');
 assert.deepEqual(adapter.args.slice(0,4),['--resume-only','--case','keep-resubscription','--main']);
 assert.equal(adapter.args.filter(value=>value==='--resume-only').length,1);assert.equal(adapter.timeoutMs,180000);
 assert.deepEqual(adapter.args.slice(-2),['--out','{outDir}/input-resume']);assert.deepEqual(adapter.resultFiles,['input-resume/RESULT.json']);
 assert.equal(adapter.originalProjection.defaultParentDispatched,false);assert.equal(adapter.originalProjection.cacheQualified,false);
 assert.equal(selected.inputResumeSelection.entry,adapter.path);assert.deepEqual(selected.inputResumeSelection.arguments,adapter.args);
 for(const source of ['tools/fixtures/input_power_gate_r122/resume.cpp','native/sleep_power_dispatch.h','native/sleep_power_state.h','tools/ymcc_sim/global/decky-persistent-lifecycle/raw-owned-producer.mjs'])assert(adapter.evidenceInputs.includes(source));
 assert(adapter.artifactInputs.some(file=>path.basename(file).toLowerCase()==='cl.exe'));assert(adapter.artifactInputs.some(file=>path.basename(file).toLowerCase()==='vctip.exe'));
 assert(!reads.some(file=>/\/Testing\/YMCC\/Evidence\//i.test(file)||/\/deps\/handheldcompanion-runtime\//i.test(file)));
});
test('input resume reference-only selection keeps the same explicit scope without full parent fallback',async()=>{
 const selected=await selectCases(parseOptions([...resumeChoice,'--mode','run','--reference-only']));
 assert.equal(selected.deferred.length,0);assert.equal(selected.adapters.length,1);assert.equal(selected.adapters[0].path,'tools/input_power_gate_r122_selftest.mjs');
 assert.equal(selected.inputResumeSelection.variant,'input-resume-only');assert.equal(selected.inputResumeSelection.actualOsOrDevices,false);
 const old=await selectCases(parseOptions(['--test','controller.unified-standard-input','--mode','plan']));
 assert.equal(old.adapters.length,0);assert.deepEqual(old.deferred.map(row=>row.id),['controller.unified-standard-input']);assert.equal(old.inputResumeSelection,undefined);
});
test('input resume rejects invalid mixed broad duplicate and unpinned choices before execution',()=>{
 const invalid=[
  ['--test','controller.unified-standard-input','--input-resume-only'],
  ['--test','controller.unified-standard-input','--input-resume-case','keep-resubscription'],
  [...resumeChoice,'--input-resume-only'],[...resumeChoice,'--input-resume-case','keep-resubscription'],
  [...resumeChoice,'--test','fan.curve'],[...resumeChoice,'--module','gamepad'],[...resumeChoice,'--full-acceptance'],
  [...resumeChoice,'--mode','full','--full-acceptance'],[...resumeChoice,'--profile','sleep'],
  [...resumeChoice,'--common-hid-runtime-only','--common-hid-runtime-case','source-binding'],
  [...resumeChoice,'--runtime-root','G:/unrelated'],[...resumeChoice,'--coordinator-case','prehost-writer'],
  [...resumeChoice,'--MainFile',path.resolve(import.meta.filename)],
  [...resumeChoice,'--expected-main-sha256','not-a-sha'],
  [...resumeChoice,'--expected-main-sha256','0'.repeat(64),'--expected-main-sha256','0'.repeat(64)],
  [...resumeChoice,'--HeaderRoot','relative-headers'],
  [...resumeChoice.slice(0,-1),'unknown'],
 ];
 for(const args of invalid)assert.throws(()=>parseOptions(args));
});
test('input resume candidate identity includes all three original headers and rejects a missing wake header',async t=>{
 const main=path.resolve(import.meta.filename),sha=createHash('sha256').update(fs.readFileSync(main)).digest('hex');
 const headerRoot=path.resolve(import.meta.dirname,'../../../native');
 const args=[...resumeChoice,'--MainFile',main,'--expected-main-sha256',sha,'--HeaderRoot',headerRoot];
 const selected=await selectCases(parseOptions(args));
 const adapter=selected.adapters[0];assert.equal(adapter.args[adapter.args.indexOf('--main')+1],main);assert.equal(adapter.args[adapter.args.indexOf('--expected-main-sha256')+1],sha);
 assert.equal(adapter.args[adapter.args.indexOf('--header-root')+1],headerRoot);assert(!adapter.evidenceInputs.includes('native/main.cpp'));
 assert.deepEqual(adapter.evidenceInputs.filter(file=>file.startsWith('native/')).sort(),[
  'native/sleep_power_dispatch.h','native/sleep_power_state.h','native/sleep_wake_admission.h',
 ]);
 await assert.rejects(()=>selectCases(parseOptions([...resumeChoice,'--MainFile',main,'--expected-main-sha256','0'.repeat(64)])),/INPUT_RESUME_MAIN_IDENTITY_MISMATCH/);
 const admissionHeader=path.join(headerRoot,'sleep_wake_admission.h'),stat=fs.statSync;
 const missing=t.mock.method(fs,'statSync',(file,...options)=>{
  if(file===admissionHeader)throw Object.assign(new Error('Missing fixture header'),{code:'ENOENT',path:file});
  return stat(file,...options);
 });
 try{
  await assert.rejects(()=>selectCases(parseOptions(args)),{code:'ENOENT',path:admissionHeader});
 }finally{missing.mock.restore();}
});
test('Host lifecycle references materialize only their selected original descriptors',async()=>{
 const ids=['controller.host-lifecycle-protocol','controller.host-parent-lifetime'];
 for(const id of ids){
  const selected=await selectCases(parseOptions(['--mode','run','--reference-only','--test',id]));
  assert.deepEqual(selected.selectedIds,[id]);assert.equal(selected.adapters.length,1);assert.equal(selected.deferred.length,0);
  assert.equal(selected.adapters[0].path,'tools/ymcc_sim/global/controller-lifecycle-completion/host-run.mjs');
  assert.deepEqual(selected.adapters[0].args,['--case',id,'--evidence-dir','{outDir}/host-lifecycle']);
  assert.throws(()=>parseOptions(['--mode','run','--reference-only','--rerun','--test',id]),/REFERENCE_ONLY_REQUIRES_RUN_WITHOUT_RERUN_OR_FULL/);
 }
 const pair=await selectCases(parseOptions(['--mode','run','--reference-only','--test',ids.join(',')]));
 assert.deepEqual(pair.selectedIds,ids);assert.equal(pair.adapters.length,2);assert.equal(pair.deferred.length,0);
});
test('Host lifecycle cache admission preserves lazy planning and separate original IDs',async()=>{
 for(const id of ['controller.host-parent-lifetime','controller.host-lifecycle-protocol']){
  const selected=await selectCases(parseOptions(['--mode','plan','--test',id]));
  assert.equal(selected.adapters.length,0);assert.deepEqual(selected.deferred.map(row=>row.id),[id]);
 }
});
const repo=path.resolve(import.meta.dirname,'../../..');
function noExternalReads(action,allowed=[]){
 const original=fs.readFileSync,external=[];
 fs.readFileSync=function(file,...args){const full=path.resolve(String(file)),rel=path.relative(repo,full);if((path.isAbsolute(rel)||rel==='..'||rel.startsWith('..'+path.sep))&&!allowed.includes(full)){external.push(full);throw Error('UNRELATED_EXTERNAL_READ:'+full);}return original.call(this,file,...args);};
 return Promise.resolve().then(action).finally(()=>{fs.readFileSync=original;assert.deepEqual(external,[]);});
}
test('fan plan discovers the current catalog without loading unrelated tool or historical evidence closures',async()=>noExternalReads(async()=>{
 const selected=await selectCases(parseOptions(['--module','fan','--mode','plan']));
 assert.deepEqual(selected.selectedIds,['fan.curve']);assert.equal(selected.adapters.length,1);assert.equal(selected.adapters[0].kind,'typescript');
}));
test('explicit source replay materializes its own scope without unrelated music/signal/sleep tool preparation',async()=>noExternalReads(async()=>{
 const selected=await selectCases(parseOptions(['--test','steam.shared-mode-source','--mode','run','--rerun']));
 assert.deepEqual(selected.selectedIds,['steam.shared-mode-source']);assert.equal(selected.adapters[0].blockedReason,undefined);
}));
test('music planning stays identity-only; actual tool verification is not counted as already executed',async()=>noExternalReads(async()=>{
 const selected=await selectCases(parseOptions(['--test','audio.music-player','--mode','plan']));
 assert.equal(selected.adapters[0].registrationIdentityOnly,true);assert.equal(selected.adapters[0].blockedReason,'MUSIC_EXECUTION_IDENTITY_DEFERRED');
}));
test('Decky minimum is the original owned lifecycle and cannot silently choose wallpaper',async()=>noExternalReads(async()=>{
 const selected=await selectCases(parseOptions(['--module','decky','--mode','plan']));
 assert.deepEqual(selected.selectedIds,['plugins.decky-owned-runtime-lifecycle']);assert.equal(selected.deferred.length,1);assert.equal(selected.adapters.length,0);
}));
test('shared lifecycle requests deduplicate original IDs and keep partial coverage',async()=>noExternalReads(async()=>{
 const selected=await selectCases(parseOptions(['--module','sleep,lifecycle','--mode','plan']));
 assert.equal(selected.selectedIds.length,2);assert.equal(selected.duplicateReferences.length,2);
}));
test('unknown IDs and excessive/reset budgets reject rather than silently passing',async()=>{
 await assert.rejects(()=>selectCases(parseOptions(['--test','missing.actual-test'])),/UNKNOWN_TEST/);
 for(const value of ['0','999','1800001','NaN'])assert.throws(()=>parseOptions(['--budget-ms',value]),/WALL_BUDGET/);
 assert.equal(parseOptions([]).budgetMs,1800000);
 for(const flag of ['--module','--test'])for(const value of [',',',,','fan,',',fan','fan,,controller','fan,  ,controller','  '])assert.throws(()=>parseOptions([flag,value]),new RegExp('EMPTY_OPTIMIZATION_SELECTION:'+flag.slice(2)));
 const valid=parseOptions(['--module','fan,gamepad','--test','fan.curve,controller.persona']);
 assert.deepEqual(valid.modules,['fan','controller']);assert.deepEqual(valid.tests,['fan.curve','controller.persona']);
});

const workspace=path.resolve('G:/YeManCC-Work/Testing/YMCC/Workspace');
const controlPrefix='selection-control-';
const samePath=(a,b)=>process.platform==='win32'?a.toLowerCase()===b.toLowerCase():a===b;
function ownedControl(t){
 assert(!fs.lstatSync(workspace).isSymbolicLink()&&samePath(fs.realpathSync(workspace),workspace));
 const root=fs.mkdtempSync(path.join(workspace,controlPrefix)),created=fs.lstatSync(root,{bigint:true});
 t.after(()=>{
  assert.equal(path.dirname(path.resolve(root)),workspace);assert(path.basename(root).startsWith(controlPrefix));
  const current=fs.lstatSync(root,{bigint:true});
  assert(current.isDirectory()&&!current.isSymbolicLink()&&current.dev===created.dev&&current.ino===created.ino);
  assert(samePath(fs.realpathSync(root),root));fs.rmSync(root,{recursive:true,force:false});
 });
 return root;
}
function bugFile(root,name,issues,extra={}){const file=path.resolve(root,name+'.json');fs.writeFileSync(file,JSON.stringify({schemaVersion:1,kind:'ymcc-release-bug-plan',issues,...extra}),{flag:'wx'});return file;}
test('explicit bug plan keeps functional CPU and HC regressions while excluding optimization/upgrades',async t=>{
 const file=bugFile(ownedControl(t),'selected-functional',[{id:'cpu-functional',reason:'Current state publication regression',purpose:'bug-regression',testIds:['performance.cpu-state']},{id:'fan-functional',reason:'Current mock runtime connection regression',purpose:'bug-regression',testIds:['fan.runtime-connection']},{id:'future-upgrade',reason:'Separate future upgrade',purpose:'hc-upgrade',testIds:['future.upgrade-case']}]);
 await noExternalReads(async()=>{const selected=await selectCases(parseOptions(['--bug-plan',file,'--mode','plan']));assert.deepEqual(selected.selectedIds,['performance.cpu-state','fan.runtime-connection']);assert.equal(selected.bugSelection.excludedIssues.length,1);assert.equal(selected.deferred.length,1);},[file]);
});
test('specialized-only and empty bug plans do not expand to the module or full suite',async t=>{
 const file=bugFile(ownedControl(t),'specialized-only',[{id:'power',reason:'Separate CPU optimization',purpose:'cpu-optimization',testIds:['future.power-case']}]);
 await noExternalReads(async()=>{const selected=await selectCases(parseOptions(['--bug-plan',file,'--mode','run']));assert.equal(selected.bugSelection.emptySelection,true);assert.equal(selected.adapters.length,0);assert.equal(selected.deferred.length,0);assert.deepEqual(selected.selectedIds,[]);},[file]);
});
test('release bug selection rejects broadening flags and unknown bug IDs',async t=>{
 assert.throws(()=>parseOptions(['--bug-plan','x.json','--module','fan']),/BUG_PLAN_REQUIRES_EXACT/);
 assert.throws(()=>parseOptions(['--bug-plan','x.json','--mode','full','--full-acceptance']),/BUG_PLAN_REQUIRES_EXACT/);
 const file=bugFile(ownedControl(t),'unknown-bug',[{id:'unknown',reason:'Must not silently skip',purpose:'bug-regression',testIds:['unknown.actual-bug']}]);
 await assert.rejects(()=>selectCases(parseOptions(['--bug-plan',file])),/UNKNOWN_RELEASE_BUG_TEST_ID/);
});

test('catalog package absence is distinct from source syntax/ABI errors and cannot become PASS',()=>{
 const seen=[];assert.deepEqual(missingCatalogTools(name=>{seen.push(name);return 'resolved:'+name;}),[]);assert.deepEqual(seen,['typescript','vue/compiler-sfc']);
 assert.deepEqual(missingCatalogTools(name=>{const error=Error('missing');error.code='ERR_MODULE_NOT_FOUND';throw error;}),['typescript','vue/compiler-sfc']);
 assert.throws(()=>missingCatalogTools(()=>{throw new SyntaxError('broken module');}),/broken module/);
});

test('required release bug profile cannot silently omit one requested original case',async t=>{
 const file=bugFile(ownedControl(t),'profile-mismatch-required',[{id:'mixed-bug',reason:'Both original regressions are required',purpose:'bug-regression',testIds:['steam.shared-mode-source','sleep.host-forms']}]);
 await assert.rejects(()=>selectCases(parseOptions(['--bug-plan',file,'--mode','plan'])),/BUG_PLAN_PROFILE_EXCLUDES_REQUIRED_TEST/);
});
test('bug-plan candidate payload and explicit profile reach the original Host consumer options',async t=>{
 const root=ownedControl(t),payload=path.resolve(root,'candidate-host'),file=bugFile(root,'candidate-host-required',[{id:'host-bug',reason:'Validate the declared current Host payload',purpose:'bug-regression',testIds:['sleep.engine-memory']}],{profile:'full',candidateInputs:{payloadRoot:payload}});
 const o=parseOptions(['--bug-plan',file,'--mode','plan']);const selected=await selectCases(o);
 assert.equal(o.payloadRoot,payload);assert.equal(o.profile,'full');assert.deepEqual(selected.selectedIds,['sleep.engine-memory']);assert.equal(selected.adapters[0].requiresPayloadRoot,true);assert.deepEqual(selected.bugSelection.candidateInputs,{payloadRoot:payload});
});


test('full profile includes all explicitly required desktop and sleep cases without expanding the suite',async t=>{
 const file=bugFile(ownedControl(t),'profile-full-required',[{id:'mixed-bug',reason:'Two required scopes',purpose:'bug-regression',testIds:['steam.shared-mode-source','sleep.host-forms']}],{profile:'full'});
 const selected=await selectCases(parseOptions(['--bug-plan',file,'--mode','plan']));
 assert.deepEqual(selected.selectedIds,['steam.shared-mode-source','sleep.host-forms']);assert.equal(selected.bugSelection.profile,'full');
});
test('explicit contradictory profile and candidate CLI values reject while equal inputs remain usable',async t=>{
 const bugRoot=ownedControl(t),payload=path.resolve(bugRoot,'candidate-host'),file=bugFile(bugRoot,'candidate-conflicts',[{id:'host-bug',reason:'Use exactly this candidate',purpose:'bug-regression',testIds:['sleep.engine-memory']}],{profile:'full',candidateInputs:{payloadRoot:payload}});
 await assert.rejects(()=>selectCases(parseOptions(['--bug-plan',file,'--profile','desktop'])),/BUG_PLAN_PROFILE_CONFLICT/);
 await assert.rejects(()=>selectCases(parseOptions(['--bug-plan',file,'--payload-root',path.resolve(bugRoot,'other-candidate')])),/BUG_PLAN_CANDIDATE_CONFLICT:payloadRoot/);
 const same=await selectCases(parseOptions(['--bug-plan',file,'--profile','full','--payload-root',payload]));assert.deepEqual(same.selectedIds,['sleep.engine-memory']);
});
test('existing candidate pair validation is reused before any selected factory is loaded',async t=>{
 const root=ownedControl(t),file=bugFile(root,'candidate-pin-required',[{id:'host-bug',reason:'Missing reviewed helper pin is not usable',purpose:'bug-regression',testIds:['sleep.engine-memory']}],{profile:'full',candidateInputs:{lightingPayloadRoot:path.resolve(root,'candidate-lighting')}});
 await assert.rejects(()=>selectCases(parseOptions(['--bug-plan',file])),/LIGHTING_CANDIDATE_REQUIRES_ROOT_AND_REVIEWED_PIN/);
});
test('specialized-only plan ignores malformed specialty candidate configuration and cannot execute it',async t=>{
 const file=bugFile(ownedControl(t),'specialized-candidate-no-load',[{id:'future',reason:'Optional upgrade',purpose:'hc-upgrade',testIds:['future.upgrade-case']}],{profile:'future-profile',candidateInputs:{completeHostContract:'missing',args:['not-a-consumer-input']}});
 await noExternalReads(async()=>{const selected=await selectCases(parseOptions(['--bug-plan',file,'--mode','run','--rerun']));assert.equal(selected.bugSelection.emptySelection,true);assert.deepEqual(selected.adapters,[]);assert.deepEqual(selected.bugSelection.candidateInputs,{});},[file]);
});
test('complete Host contract option is forwarded through the existing option vocabulary',()=>{
 const contract=path.resolve(workspace,'candidate-contract.json');assert.equal(parseOptions(['--complete-host-contract',contract]).completeHostContract,contract);
});


test('explicit IDs excluded by profile reject before partial selection or preparation',async t=>noExternalReads(async()=>{
 const expected=/PROFILE_EXCLUDES_EXPLICIT_TEST:sleep\.host-forms/;
 for(const mode of ['plan','run']){
  const args=['--test','fan.curve,sleep.host-forms','--profile','desktop','--mode',mode];
  if(mode==='run')args.push('--rerun');
  let observed;
  const selection=selectCases(parseOptions(args)).then(value=>{observed={mode,requested:value.requested,selectedIds:value.selectedIds};return value;});
  try{await assert.rejects(selection,expected);}finally{if(observed)t.diagnostic(JSON.stringify(observed));}
 }
 await assert.rejects(()=>selectCases(parseOptions(['--test','sleep.host-forms','--profile','desktop','--mode','run','--rerun'])),expected);
 const valid=await selectCases(parseOptions(['--test','fan.curve,sleep.host-forms','--profile','full','--mode','plan']));
 assert.deepEqual(valid.selectedIds,['fan.curve','sleep.host-forms']);
 assert.equal(valid.requested,2);
 assert.equal(valid.adapters.length+valid.deferred.length,2);
}));


test('optimized receipt writer preserves existing bytes and flushes fresh JSON before success',t=>{
 const root=ownedControl(t);
 const old=path.join(root,'existing-result.json'),sentinel=Buffer.from('{"status":"FAIL","evidence":"keep original"}\n');
 fs.writeFileSync(old,sentinel,{flag:'wx'});
 let error;try{writeFreshJson(old,{status:'PASS'});}catch(caught){error=caught;}
 const observed=fs.readFileSync(old);
 t.diagnostic(JSON.stringify({existingBytesPreserved:observed.equals(sentinel),errorCode:error?.code??null}));
 assert.equal(error?.code,'EEXIST');assert.deepEqual(observed,sentinel);
 const sync=fs.fsyncSync,close=fs.closeSync,order=[];
 t.mock.method(fs,'fsyncSync',fd=>{order.push(['sync',fd]);return sync(fd);});
 t.mock.method(fs,'closeSync',fd=>{order.push(['close',fd]);return close(fd);});
 const fresh=path.join(root,'fresh-result.json'),value={status:'SCOPED_CONTROL',executedThisRun:false,rows:[{name:'original'}]};
 writeFreshJson(fresh,value);
 assert.deepEqual(order.map(row=>row[0]),['sync','close']);assert.equal(order[0][1],order[1][1]);
 assert.equal(fs.readFileSync(fresh,'utf8'),JSON.stringify(value,null,2)+'\n');
 const completed=fs.readFileSync(fresh);assert.throws(()=>writeFreshJson(fresh,{status:'PASS'}),{code:'EEXIST'});assert.deepEqual(fs.readFileSync(fresh),completed);
 // A flush error must propagate, close its descriptor and preserve the created evidence.
 const failure=path.join(root,'flush-error.json');order.length=0;
 t.mock.method(fs,'fsyncSync',fd=>{order.push(['failed-sync',fd]);throw Object.assign(Error('fixture flush failure'),{code:'EIO'});});
 assert.throws(()=>writeFreshJson(failure,{status:'UNKNOWN'}),{code:'EIO'});
 assert.deepEqual(order.map(row=>row[0]),['failed-sync','close']);assert.equal(order[0][1],order[1][1]);
 const partial=fs.readFileSync(failure);assert.throws(()=>writeFreshJson(failure,{status:'PASS'}),{code:'EEXIST'});assert.deepEqual(fs.readFileSync(failure),partial);
});

// Existing coordinator cases must stay selectable without the full prerequisite factory.
test('coordinator explicit plans select each original optional case with no external reads',async()=>noExternalReads(async()=>{
 for(const caseName of ['prehost-writer','late-terminal']){
  const selected=await selectCases(parseOptions(['--test','controller.coordinator-lifecycle','--coordinator-case',caseName,'--mode','plan']));
  assert.deepEqual(selected.selectedIds,['controller.coordinator-lifecycle']);assert.equal(selected.adapters.length,1);assert.equal(selected.deferred.length,0);
  const adapter=selected.adapters[0];assert.equal(adapter.id,'controller.coordinator-lifecycle');assert.equal(adapter.timeoutMs,90000);
  assert.equal(adapter.selectedCoordinatorCase,caseName);assert.equal(adapter.args.includes('--plan'),true);
  assert.deepEqual(adapter.args.slice(0,4),['--evidence-dir','{outDir}/coordinator','--case',caseName]);
  assert.deepEqual(selected.coordinatorSelection,{canonicalTestId:adapter.id,caseName,defaultCoordinatorDispatched:false});
 }
}));

test('coordinator explicit run inherits the existing absolute owner cutoff',async()=>noExternalReads(async()=>{
 const deadline=Date.now()+20000,o=parseOptions(['--test','controller.coordinator-lifecycle','--coordinator-case','late-terminal','--mode','run','--rerun']);
 const selected=await selectCases({...o,coordinatorDeadlineEpochMs:deadline});
 assert.equal(selected.adapters.length,1);assert.equal(selected.deferred.length,0);
 assert.deepEqual(selected.adapters[0].args,['--evidence-dir','{outDir}/coordinator','--case','late-terminal','--deadline-epoch-ms',String(deadline)]);
 assert.equal(selected.adapters[0].timeoutMs,90000);
}));

test('coordinator explicit selection rejects broad, duplicate and empty choices',()=>{
 for(const args of [
  ['--coordinator-case','prehost-writer'],
  ['--test','controller.coordinator-lifecycle,fan.curve','--coordinator-case','prehost-writer'],
  ['--test','controller.coordinator-lifecycle','--module','fan','--coordinator-case','prehost-writer'],
  ['--test','controller.coordinator-lifecycle','--coordinator-case','prehost-writer','--full-acceptance'],
  ['--test','controller.coordinator-lifecycle','--coordinator-case','prehost-writer','--mode','full','--full-acceptance'],
  ['--test','controller.coordinator-lifecycle','--coordinator-case',' '],
  ['--test','controller.coordinator-lifecycle','--coordinator-case',''],
  ['--test','controller.coordinator-lifecycle','--coordinator-case','prehost-writer','--coordinator-case','late-terminal'],
 ])assert.throws(()=>parseOptions(args));
});

test('coordinator explicit unknown cases cannot fall back to the default suite',async()=>noExternalReads(async()=>{
 await assert.rejects(()=>selectCases(parseOptions(['--test','controller.coordinator-lifecycle','--coordinator-case','unknown','--mode','plan'])),/UNKNOWN_COORDINATOR_CASE/);
}));

test('coordinator explicit run without a current inherited cutoff is refused',async()=>noExternalReads(async()=>{
 const o=parseOptions(['--test','controller.coordinator-lifecycle','--coordinator-case','prehost-writer','--mode','run','--rerun']);
 await assert.rejects(()=>selectCases(o),/DEADLINE|BUDGET/);
 await assert.rejects(()=>selectCases({...o,coordinatorDeadlineEpochMs:Date.now()-1}),/DEADLINE|BUDGET/);
}));

test('coordinator explicit feature leaves omitted-case default registration deferred',async()=>noExternalReads(async()=>{
 const selected=await selectCases(parseOptions(['--test','controller.coordinator-lifecycle','--mode','plan']));
 assert.equal(selected.adapters.length,0);assert.equal(selected.deferred.length,1);assert.equal(selected.coordinatorSelection,undefined);
}));


test('coordinator OneX plan selects the original scope without reading its explicit tool contract',async()=>noExternalReads(async()=>{
 const toolPlan='G:/YeManCC-Work/Testing/YMCC/Evidence/unused-Onex-tool-contract.json';
 const selected=await selectCases(parseOptions(['--test','controller.coordinator-lifecycle','--coordinator-case','onex-diagnostic-tls-startup','--coordinator-tool-plan',toolPlan,'--mode','plan']));
 assert.deepEqual(selected.selectedIds,['controller.coordinator-lifecycle']);assert.equal(selected.adapters.length,1);assert.equal(selected.deferred.length,0);
 const adapter=selected.adapters[0];assert.equal(adapter.selectedCoordinatorCase,'onex-diagnostic-tls-startup');assert.equal(adapter.timeoutMs,120000);
 assert.deepEqual(adapter.args,['--evidence-dir','{outDir}/coordinator','--case','onex-diagnostic-tls-startup','--tool-plan',toolPlan]);
 assert.deepEqual(adapter.artifactInputs,[toolPlan]);assert.equal(adapter.cacheAcceptance,false);
 assert.deepEqual(adapter.resultFiles,['coordinator/result.json','coordinator/worker-result.json']);
}));

test('coordinator OneX execution forwards its exact tool contract and original owner cutoff',async()=>noExternalReads(async()=>{
 const toolPlan='G:/YeManCC-Work/Testing/YMCC/Evidence/unused-Onex-tool-contract.json',deadline=Date.now()+20000;
 const options=parseOptions(['--test','controller.coordinator-lifecycle','--coordinator-case','onex-diagnostic-tls-startup','--coordinator-tool-plan',toolPlan,'--mode','run','--rerun']);
 const selected=await selectCases({...options,coordinatorDeadlineEpochMs:deadline});
 assert.equal(selected.adapters.length,1);assert.equal(selected.deferred.length,0);
 assert.deepEqual(selected.adapters[0].args,['--evidence-dir','{outDir}/coordinator','--case','onex-diagnostic-tls-startup','--tool-plan',toolPlan,'--deadline-epoch-ms',String(deadline)]);
 assert.equal(selected.adapters[0].timeoutMs,120000);assert.equal(selected.coordinatorSelection.defaultCoordinatorDispatched,false);
}));

test('coordinator OneX tool plans and inherited deadlines cannot leak into other selections',async()=>noExternalReads(async()=>{
 const selection=['--test','controller.coordinator-lifecycle','--coordinator-case','onex-diagnostic-tls-startup'];
 await assert.rejects(()=>selectCases(parseOptions([...selection,'--mode','plan'])),/ONEX_TLS_ABSOLUTE_TOOL_PLAN_REQUIRED/);
 for(const toolPlan of ['relative.json','G:/bad\ncontract.json'])await assert.rejects(()=>selectCases(parseOptions([...selection,'--coordinator-tool-plan',toolPlan,'--mode','plan'])),/ONEX_TLS_ABSOLUTE_TOOL_PLAN_REQUIRED/);
 const toolPlan='G:/YeManCC-Work/Testing/YMCC/Evidence/unused-Onex-tool-contract.json';
 const run=parseOptions([...selection,'--coordinator-tool-plan',toolPlan,'--mode','run','--rerun']);
 await assert.rejects(()=>selectCases(run),/INHERITED_FUTURE_DEADLINE_REQUIRED/);
 await assert.rejects(()=>selectCases({...run,coordinatorDeadlineEpochMs:Date.now()-1}),/INHERITED_FUTURE_DEADLINE_REQUIRED/);
 assert.throws(()=>parseOptions([...selection,'--coordinator-tool-plan',toolPlan,'--coordinator-tool-plan',toolPlan]),/DUPLICATE_COORDINATOR_TOOL_PLAN/);
 assert.throws(()=>parseOptions(['--test','controller.coordinator-lifecycle','--coordinator-case','prehost-writer','--coordinator-tool-plan',toolPlan]),/COORDINATOR_TOOL_PLAN_REQUIRES_ONEX_TLS_CASE/);
 assert.throws(()=>parseOptions(['--test','fan.curve','--coordinator-tool-plan',toolPlan]),/COORDINATOR_TOOL_PLAN_REQUIRES_ONEX_TLS_CASE/);
}));


test('coordinator direct optional descriptors execute the existing selected module rather than the default dispatcher',async()=>noExternalReads(async()=>{
 for(const caseName of ['prehost-writer','late-terminal']){
  const plan=await selectCases(parseOptions(['--test','controller.coordinator-lifecycle','--coordinator-case',caseName,'--mode','plan']));
  const adapter=plan.adapters[0];assert.equal(adapter.path,'tools/ymcc_sim/global/controller-coordinator-lifecycle/supplemental.mjs');
  assert.deepEqual(plan.selectedIds,['controller.coordinator-lifecycle']);assert.equal(plan.deferred.length,0);assert.equal(adapter.timeoutMs,90000);
  assert(!adapter.evidenceInputs.includes('tools/ymcc_sim/global/controller-coordinator-lifecycle/run.mjs'));
  assert.equal(adapter.args[adapter.args.indexOf('--case')+1],caseName);assert.equal(adapter.cacheAcceptance,false);
 }
}));

test('coordinator direct source collection has no unselected legacy loader and still follows actual imports',async()=>noExternalReads(async()=>{
 const {runtimeSources}=await import('../global/controller-coordinator-lifecycle/supplemental.mjs');
 const rows=runtimeSources(Date.now()+20000),relative=rows.map(r=>path.relative(repo,r.path).replaceAll('\\','/')).sort();
 assert.deepEqual(relative,[
  'tools/ymcc_sim/global/controller-coordinator-lifecycle/prehost-writer-fixtures.json',
  'tools/ymcc_sim/global/controller-coordinator-lifecycle/supplemental.mjs',
  'tools/ymcc_sim/global/decky-persistent-lifecycle/raw-owned-producer.mjs',
  'tools/ymcc_sim/global/host-process.ps1',
 ].sort());
 for(const row of rows){assert.equal(row.bytes,fs.statSync(row.path).size);assert.match(row.sha256,/^[a-f0-9]{64}$/);}
 const trace=runtimeSources(Date.now()+20000,path.join(repo,'tools/ymcc_sim/global/controller-coordinator-lifecycle/run.mjs'));
 assert(trace.some(r=>r.path.endsWith('baseline-source-inputs.mjs')));
 assert.throws(()=>runtimeSources(Date.now()+20000,path.join(repo,'tools/ymcc_sim/global/controller-coordinator-lifecycle/unknown.mjs')),/EXACT_OPTIONAL_ENTRY_SOURCE_REQUIRED/);
}));

test('coordinator direct optional routing preserves the original OneX dispatcher and tool contract path',async()=>noExternalReads(async()=>{
 const selection=await selectCases(parseOptions(['--test','controller.coordinator-lifecycle','--coordinator-case','onex-diagnostic-tls-startup','--coordinator-tool-plan','G:/unused/tooling.json','--mode','plan']));
 assert.equal(selection.adapters[0].path,'tools/ymcc_sim/global/controller-coordinator-lifecycle/run.mjs');
 assert.equal(selection.adapters[0].timeoutMs,120000);assert.deepEqual(selection.adapters[0].artifactInputs,['G:/unused/tooling.json']);
}));

const prepareChoice=['--test','controller.coordinator-lifecycle','--coordinator-case','prepare-rejection-cold'];
test('coordinator cold PREPARE rejection plan is exact and does not read managed inputs',async()=>noExternalReads(async()=>{
 const paths=['--coordinator-managed-dll','G:/unused/Fixture.dll','--coordinator-dotnet','G:/unused/dotnet.exe'];
 for(const inputs of [[],paths]){
  const selection=await selectCases(parseOptions([...prepareChoice,...inputs,'--mode','plan']));
  assert.deepEqual(selection.selectedIds,['controller.coordinator-lifecycle']);assert.equal(selection.deferred.length,0);
  const [adapter]=selection.adapters;
  assert.equal(adapter.path,'tools/input_coordinator_lifecycle_selftest.mjs');
  assert.deepEqual(adapter.args,['--out','{outDir}/p','--suite','target-return','--case','prepare-rejection-cold']);
  assert.deepEqual(adapter.artifactInputs,[]);assert.deepEqual(adapter.resultFiles,['p/RESULT.json']);assert.equal(adapter.cacheAcceptance,false);
  assert.deepEqual(selection.coordinatorSelection.expectedCases,['PREPARE-REJECT/ROG/actual-cold-create-exception']);
  assert.equal(selection.coordinatorSelection.defaultCoordinatorDispatched,false);
 }
}));

test('coordinator cold PREPARE rejection run binds prerequisites and rejects ambiguous scope',async t=>noExternalReads(async()=>{
 const dir=ownedControl(t),dll=path.join(dir,'Fixture.dll'),dotnet=path.join(dir,'dotnet.exe');
 const run=['--mode','run','--rerun'], inputs=['--coordinator-managed-dll',dll,'--coordinator-dotnet',dotnet];
 const deadline=Date.now()+20000;
 const select=argv=>selectCases({...parseOptions(argv),coordinatorDeadlineEpochMs:deadline});
 await assert.rejects(()=>select([...prepareChoice,...run]),/PREPARE_REJECTION_EXPLICIT_MANAGED_DLL_AND_DOTNET_REQUIRED/);
 await assert.rejects(()=>select([...prepareChoice,...run,...inputs.slice(0,2)]),/PREPARE_REJECTION_EXPLICIT_MANAGED_DLL_AND_DOTNET_REQUIRED/);
 await assert.rejects(()=>selectCases(parseOptions([...prepareChoice,...run,...inputs])),/INHERITED_FUTURE_DEADLINE_REQUIRED/);
 assert.throws(()=>parseOptions([...prepareChoice,...inputs,...inputs.slice(0,2)]),/DUPLICATE_COORDINATOR_PREREQUISITE/);
 assert.throws(()=>parseOptions(['--test','controller.coordinator-lifecycle','--coordinator-case','prehost-writer',...inputs]),/COORDINATOR_PREREQUISITES_REQUIRE_MANAGED_CASE/);
 const {adapters:[adapter]}=await select([...prepareChoice,...run,...inputs]);
 assert.deepEqual(adapter.args.slice(-4),['--managed-dll',dll,'--dotnet',dotnet]);
 assert.deepEqual(adapter.artifactInputs,[dll,path.join(dir,'fixture-manifest.json'),path.join(dir,'Fixture.deps.json'),path.join(dir,'Fixture.runtimeconfig.json'),dotnet]);
}));

const gpdCycleChoice=['--test','controller.coordinator-lifecycle','--coordinator-case','gpd-expired-first-on-full-cycle'];
test('coordinator GPD late wake routes one explicit managed case without adding a default',async t=>noExternalReads(async()=>{
 const name='wake-kept-source:GPD:expired-then-current';
 const choice=['--test','controller.coordinator-lifecycle','--coordinator-case',name];
 const plan=await selectCases(parseOptions([...choice,'--mode','plan']));
 assert.deepEqual(plan.selectedIds,['controller.coordinator-lifecycle']);assert.equal(plan.adapters.length,1);assert.deepEqual(plan.deferred,[]);
 assert.deepEqual(plan.coordinatorSelection.expectedCases,['GPD/wake-kept-source/expired-then-current']);
 assert.equal(plan.coordinatorSelection.defaultCoordinatorDispatched,false);assert.equal(plan.coordinatorSelection.requiresSdkFreeManagedFixture,true);
 assert.deepEqual(plan.adapters[0].args,['--out','{outDir}/p','--suite','lifecycle-directed','--case',name]);
 assert.deepEqual(plan.adapters[0].artifactInputs,[]);assert.equal(plan.adapters[0].cacheAcceptance,false);
 assert.deepEqual(plan.adapters[0].resultFiles,['p/RESULT.json','p/source-wake-selection-observed.json','p/full-pipe-core/fixture-result.json']);

 const dir=ownedControl(t),dll=path.join(dir,'Fixture.dll'),dotnet=path.join(dir,'dotnet.exe');
 const inputs=['--coordinator-managed-dll',dll,'--coordinator-dotnet',dotnet];
 const run=[...choice,'--mode','run','--rerun'],until=Date.now()+20000;
 await assert.rejects(()=>selectCases({...parseOptions(run),coordinatorDeadlineEpochMs:until}),/GPD_EXPLICIT_MANAGED_DLL_AND_DOTNET_REQUIRED/);
 await assert.rejects(()=>selectCases({...parseOptions([...run,...inputs.slice(0,2)]),coordinatorDeadlineEpochMs:until}),/GPD_EXPLICIT_MANAGED_DLL_AND_DOTNET_REQUIRED/);
 await assert.rejects(()=>selectCases(parseOptions([...run,...inputs])),/INHERITED_FUTURE_DEADLINE_REQUIRED/);
 const selected=await selectCases({...parseOptions([...run,...inputs]),coordinatorDeadlineEpochMs:until});
 assert.deepEqual(selected.adapters[0].args,[...plan.adapters[0].args,'--managed-dll',dll,'--dotnet',dotnet]);
 assert.deepEqual(selected.adapters[0].artifactInputs,[dll,path.join(dir,'fixture-manifest.json'),path.join(dir,'Fixture.deps.json'),path.join(dir,'Fixture.runtimeconfig.json'),dotnet]);
 for(const bad of ['wake-kept-source','wake-kept-source:ROG:expired-then-current','wake-kept-source:GPD:']){
  await assert.rejects(()=>selectCases(parseOptions([...choice.slice(0,-1),bad,'--mode','plan'])),/UNKNOWN_COORDINATOR_CASE/);
 }
 assert.throws(()=>parseOptions([...choice,'--mode','full','--full-acceptance']),/COORDINATOR_CASE_REQUIRES_ONE_EXPLICIT_ORIGINAL_TEST/);
}));
test('coordinator GPD absent source routes only the original native rejection case',async()=>noExternalReads(async()=>{
 const choice=['--test','controller.coordinator-lifecycle','--coordinator-case','gpd-pending-before-hide:absent'];
 const {adapters:[adapter],selectedIds,deferred,coordinatorSelection}=await selectCases(parseOptions([...choice,'--mode','plan']));
 assert.deepEqual(selectedIds,['controller.coordinator-lifecycle']);assert.deepEqual(deferred,[]);
 assert.equal(adapter.path,'tools/input_coordinator_lifecycle_selftest.mjs');
 assert.deepEqual(adapter.args,['--out','{outDir}/p','--suite','family-lifecycle','--case','gpd-pending-before-hide:absent']);
 assert.deepEqual(adapter.artifactInputs,[]);assert.deepEqual(adapter.resultFiles,['p/RESULT.json']);assert.equal(adapter.cacheAcceptance,false);
 assert.deepEqual(coordinatorSelection.expectedCases,['family-lifecycle/GPD/pending-before-hide/absent']);
 assert.equal(coordinatorSelection.nativeOnly,true);assert.equal(coordinatorSelection.defaultCoordinatorDispatched,false);
 await assert.rejects(()=>selectCases(parseOptions([...choice,'--mode','run'])),/INHERITED_FUTURE_DEADLINE_REQUIRED/);
 const run=await selectCases({...parseOptions([...choice,'--mode','run']),coordinatorDeadlineEpochMs:Date.now()+20000});
 assert.deepEqual(run.adapters,[adapter]);
 assert.throws(()=>parseOptions([...choice,'--coordinator-managed-dll','G:/unused/Fixture.dll']),/COORDINATOR_PREREQUISITES_REQUIRE_MANAGED_CASE/);
 assert.throws(()=>parseOptions([...choice,'--test','fan.curve']),/COORDINATOR_CASE_REQUIRES_ONE_EXPLICIT_ORIGINAL_TEST/);
  await assert.rejects(()=>selectCases(parseOptions([...choice.slice(0,-1),'gpd-pending-before-hide:unknown','--mode','plan'])),/GPD_PRE_HIDE_UNKNOWN_PARAMETER/);
}));

test('coordinator GPD pre-hide batch keeps one original invocation and rejects duplicate or invalid parameters',async()=>noExternalReads(async()=>{
 const prefix='gpd-pending-before-hide:', names='hello-return,absent';
 const choice=['--test','controller.coordinator-lifecycle','--coordinator-case',prefix+names,'--mode','plan'];
 const plan=await selectCases(parseOptions(choice));
 assert.deepEqual(plan.selectedIds,['controller.coordinator-lifecycle']);
 assert.equal(plan.adapters.length,1);assert.deepEqual(plan.deferred,[]);
 assert.equal(plan.adapters[0].path,'tools/input_coordinator_lifecycle_selftest.mjs');
 assert.deepEqual(plan.adapters[0].args,['--out','{outDir}/p','--suite','family-lifecycle','--case',prefix+names]);
 assert.equal(plan.adapters[0].timeoutMs,510000);
 assert.equal(plan.adapters[0].cacheAcceptance,false);
 assert.deepEqual(plan.coordinatorSelection.expectedCases,['family-lifecycle/GPD/pending-before-hide/hello-return','family-lifecycle/GPD/pending-before-hide/absent']);
 assert.equal(plan.coordinatorSelection.defaultCoordinatorDispatched,false);
 for(const [bad,issue] of [['absent,absent',/GPD_PRE_HIDE_DUPLICATE_PARAMETER/],['hello-return,unknown',/GPD_PRE_HIDE_UNKNOWN_PARAMETER/],['hello-return,',/GPD_PRE_HIDE_UNKNOWN_PARAMETER/]]){
   const args=[...choice];args[3]=prefix+bad;
   await assert.rejects(()=>selectCases(parseOptions(args)),issue);
 }
}));

test('coordinator GPD pre-hide batch refuses incomplete, reordered, failed or coalesced process receipts',async()=>{
 const {inspectGpdPreHideBatch}=await import('../../fixtures/input_coordinator_lifecycle/gpd_pre_hide_selection.mjs');
 const expectedCases=['family-lifecycle/GPD/pending-before-hide/hello-return','family-lifecycle/GPD/pending-before-hide/absent'];
 const selection={expectedCount:2,expectedCases};
 const [positive,negative]=expectedCases.map(name=>({mode:'family-lifecycle',exitCode:0,result:{status:'PASS',forbiddenIo:0,cases:[{name,passed:true}]}}));
 const check=rows=>inspectGpdPreHideBatch(selection,rows);
 assert.deepEqual(check([positive,negative]),{actualCases:expectedCases,exact:true,semanticPassed:true,notRunCases:[]});
 assert.deepEqual(check([positive]).notRunCases,[expectedCases[1]]);
 assert.equal(check([negative,positive]).exact,false);
 assert.equal(check([positive,positive]).exact,false);
 assert.equal(check([{...positive,result:{...positive.result,cases:[...positive.result.cases,...negative.result.cases]}}]).exact,false);
 assert.equal(check([{...positive,exitCode:1},negative]).semanticPassed,false);
 assert.equal(check([positive,{...negative,timedOut:true}]).semanticPassed,false);
 assert.equal(check([positive,{...negative,result:{...negative.result,forbiddenIo:1}}]).semanticPassed,false);
});

test('coordinator GPD pre-hide plans select each original C++ parameter alone',async()=>noExternalReads(async()=>{
 const tablePath='tools/fixtures/input_coordinator_lifecycle/pre_hide_cases.inc';
 const table=fs.readFileSync(new URL('../../fixtures/input_coordinator_lifecycle/pre_hide_cases.inc',import.meta.url),'utf8');
 const shapes=[...table.split('\n').find(line=>line.includes('supported={')).matchAll(/"([a-z-]+)"/g)].map(row=>row[1]);
 assert.equal(new Set(shapes).size,16);
 assert(shapes.includes('hello-return')&&shapes.includes('absent')&&shapes.includes('capture-stop'));
 for(const shape of shapes){
  const prefix='gpd-pending-before-hide:',name=prefix+shape;
  const choice=['--test','controller.coordinator-lifecycle','--coordinator-case',name];
  const plan=await selectCases(parseOptions([...choice,'--mode','plan']));
  assert.deepEqual(plan.selectedIds,['controller.coordinator-lifecycle']);assert.deepEqual(plan.deferred,[]);
  assert.equal(plan.adapters.length,1);
  const [adapter]=plan.adapters;
  assert.equal(adapter.path,'tools/input_coordinator_lifecycle_selftest.mjs');
  assert.deepEqual(adapter.args,['--out','{outDir}/p','--suite','family-lifecycle','--case',name]);
  assert(adapter.evidenceInputs.includes(tablePath));
  assert(adapter.evidenceInputs.includes('tools/fixtures/input_coordinator_lifecycle/gpd_pre_hide_selection.mjs'));
  assert.equal(adapter.cacheAcceptance,false);
  assert.deepEqual(plan.coordinatorSelection.expectedCases,['family-lifecycle/GPD/pending-before-hide/'+shape]);
  assert.equal(plan.coordinatorSelection.defaultCoordinatorDispatched,false);
 }
 const defaultPlan=await selectCases(parseOptions(['--test','controller.coordinator-lifecycle','--mode','plan']));
 assert.equal(defaultPlan.coordinatorSelection,undefined);
}));

test('coordinator GPD plan selects only actual native-writer OFF and exit without reading external tools',async()=>noExternalReads(async()=>{
 const selection=await selectCases(parseOptions([...gpdCycleChoice,'--mode','plan','--coordinator-managed-dll','G:/unused/Fixture.dll','--coordinator-dotnet','G:/unused/dotnet.exe']));
 assert.deepEqual(selection.selectedIds,['controller.coordinator-lifecycle']);assert.equal(selection.deferred.length,0);
 assert.deepEqual(selection.coordinatorSelection.expectedCases,['GPD/first-on-expired-full-cycle/off','GPD/first-on-expired-full-cycle/exit']);
 assert.equal(selection.coordinatorSelection.nativeWriterReturn,true);assert.equal(selection.coordinatorSelection.defaultCoordinatorDispatched,false);
 const [adapter]=selection.adapters;
 assert.equal(adapter.path,'tools/input_coordinator_lifecycle_selftest.mjs');
 assert.deepEqual(adapter.args,['--out','{outDir}/p','--suite','lifecycle-directed','--case','gpd-expired-first-on-full-cycle','--native-writer-return','actual']);
 assert.deepEqual(adapter.artifactInputs,[]);assert.deepEqual(adapter.resultFiles,['p/RESULT.json']);assert.equal(adapter.cacheAcceptance,false);
}));

test('coordinator GPD run binds managed prerequisites before dispatching either terminal',async t=>noExternalReads(async()=>{
 const dir=ownedControl(t),dll=path.join(dir,'Fixture.dll'),dotnet=path.join(dir,'dotnet.exe');
 const run=[...gpdCycleChoice,'--mode','run','--rerun'];
 const inputs=['--coordinator-managed-dll',dll,'--coordinator-dotnet',dotnet];
 const deadline=Date.now()+20000, select=args=>selectCases({...parseOptions(args),coordinatorDeadlineEpochMs:deadline});
 await assert.rejects(()=>select(run),/GPD_EXPLICIT_MANAGED_DLL_AND_DOTNET_REQUIRED/);
 await assert.rejects(()=>select([...run,...inputs.slice(0,2)]),/GPD_EXPLICIT_MANAGED_DLL_AND_DOTNET_REQUIRED/);
 await assert.rejects(()=>selectCases(parseOptions([...run,...inputs])),/INHERITED_FUTURE_DEADLINE_REQUIRED/);
 assert.throws(()=>parseOptions([...gpdCycleChoice,'--test','fan.curve']),/COORDINATOR_CASE_REQUIRES_ONE_EXPLICIT_ORIGINAL_TEST/);
 const {adapters:[adapter]}=await select([...run,...inputs]);
 assert.deepEqual(adapter.args.slice(-6),['--native-writer-return','actual','--managed-dll',dll,'--dotnet',dotnet]);
 assert.deepEqual(adapter.artifactInputs,[dll,path.join(dir,'fixture-manifest.json'),path.join(dir,'Fixture.deps.json'),path.join(dir,'Fixture.runtimeconfig.json'),dotnet]);
}));
