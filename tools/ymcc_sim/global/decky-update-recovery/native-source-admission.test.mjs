import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import vm from 'node:vm';
import {fileURLToPath} from 'node:url';
import {prepareNativeSource,prepareSharedPowerProjectionSource,cppBlock} from './native-source-admission.mjs';
const here=path.dirname(fileURLToPath(import.meta.url)),root=path.resolve(here,'../../../..');
const digest=text=>crypto.createHash('sha256').update(text).digest('hex');
const main=fs.readFileSync(path.join(root,'native/main.cpp'),'utf8');
const inputs={main,template:fs.readFileSync(path.join(here,'native-fixture.cpp'),'utf8'),repairHeader:fs.readFileSync(path.join(root,'native/decky_sidebar_manual_repair.h'),'utf8'),automaticHeader:fs.readFileSync(path.join(root,'native/decky_sidebar_automatic_repair.h'),'utf8'),mainSha256:digest(main)};
const modes=['fixed','prewarm-negative','wake-preparation-negative','legacy','retained-negative','repair-uncertain-negative','repair-retained-negative','repair-auto-repeat-negative','repair-auto-ui-negative','repair-stale-mode-negative','wake-power-guard-negative','wake-binding-negative','wake-ready-event-negative'];
test('prepares all original 13 native modes and nine mutant headers without executing them',()=>{
 const result=prepareNativeSource(inputs);
 assert.deepEqual([...result.outputs.keys()].filter(name=>name.endsWith('.cpp')).sort(),modes.map(name=>name+'.cpp').sort());
 assert.equal(result.outputs.size,22);
 for(const text of result.outputs.values())assert.doesNotMatch(text,/\/\*__[A-Z_]+__\*\//);
 assert.equal(result.extraction.mainSha256,inputs.mainSha256);
 assert.equal(result.extraction.committedDelivery.includesSenderAndTimerConsumer,true);
 assert.ok(main.includes('sleepPowerPostResumeCommit(g_hwnd,generation);'));
 const timerBegin=main.indexOf('if(w==kSleepPowerRedeliveryTimer){');
 const timer=cppBlock(main,main.indexOf('{',timerBegin),'committed repair timer').body;
 assert.ok(timer.includes('sleepPowerRedeliverCommitted(h);'));
 for(const [field,anchor] of [['admission','[]() -> ymcc::deckysidebar::LaunchAdmission {'],['modeChanged','modeObserver.changed = []'],['stableBigPicture','modeObserver.stableBigPicture = []']]){
  const body=result.extraction[field],start=main.indexOf(anchor);
  assert.ok(start>=0&&body.begin>=start&&body.end>body.begin);
  assert.equal(digest(main.slice(body.begin+1,body.end-1)),body.sha256);
 }
});
for(const name of ['SESSION_DIAGNOSTIC','PREPARE_ENVIRONMENT','WAKE_READY','ADMISSION','MODE_CHANGED','STABLE_BIGPICTURE','WAKE_COMMIT','WAKE_MESSAGE','WAKE_DELIVERY']){
 const marker='/*__'+name+'__*/';
 test('missing '+name+' template slot rejects',()=>assert.throws(()=>prepareNativeSource({...inputs,template:inputs.template.replace(marker,'')}),/Review unique native fixture template/));
 test('duplicate '+name+' template slot rejects',()=>assert.throws(()=>prepareNativeSource({...inputs,template:inputs.template+'\n'+marker}),/Review unique native fixture template/));
}
for(const anchor of ['[]() -> ymcc::deckysidebar::LaunchAdmission {','modeObserver.changed = []','modeObserver.stableBigPicture = []','static bool deckySidebarResumeAfterIntent(','static std::string deckySidebarPrepareEnvironment(','static void deckySidebarSessionDiagnostic(','static void sleepPowerPostResumeCommit(','static void sleepPowerRedeliverCommitted(']){
 test('missing current callback '+anchor,()=>assert.throws(()=>prepareNativeSource({...inputs,main:main.replace(anchor,'MISSING_NATIVE_ANCHOR')}),/Review unique native/));
 test('duplicate current callback '+anchor,()=>assert.throws(()=>prepareNativeSource({...inputs,main:main+'\n'+anchor}),/Review unique native/));
}
for(const [field,anchor,message,expected=message] of [
 ['repairHeader','dispatchEntered=true;','Uncertain-dispatch mutation anchor missing','Uncertain manual-dispatch mutation anchor must be unique'],
 ['repairHeader','(runtime.at("phase")=="frontend-attached"&&runtime.at("reason")=="retained-frontend-connected")','Retained backend repair mutation anchor missing'],
 ['repairHeader','origin!=Origin::Automatic&&deps.freshMode?deps.freshMode(session):deps.mode(session)','Manual stale-cache mutation anchor missing'],
 ['repairHeader','if(origin==Origin::Automatic)return refuse("steam-ui-refresh-requires-user");','Automatic UI guard anchor missing'],
 ['automaticHeader','if(spent_||','Automatic budget mutation anchor missing']
])test('late original mutation is now admitted early: '+message,()=>{
 assert.ok(inputs[field].includes(anchor));
 assert.throws(()=>prepareNativeSource({...inputs,[field]:inputs[field].replaceAll(anchor,'MISSING_MUTATION_ANCHOR')}),error=>error.message===expected);
});
test('balanced extractor preserves braces inside strings and comments',()=>{
 const source=String.raw`{const char* s="}";char c='{';/* } */ // }
 if(true){ return; }} rest`;
 const block=cppBlock(source,0,'control');
 assert.equal(source.slice(block.end),' rest');
 assert.throws(()=>cppBlock('{R"(raw)";}',0,'control'),/Review raw literal/);
 assert.throws(()=>cppBlock('{',0,'control'),/Unterminated/);
});
test('shared projection binds exact pair Startup selection and final admission before any compiler',()=>{
 const projectionInputs={main,
  template:fs.readFileSync(path.join(here,'shared-power-projection.cpp'),'utf8'),
  msiHeader:fs.readFileSync(path.join(root,'native/msi_native_foreground_isolation_runtime.h'),'utf8'),
  recoveryRuntime:fs.readFileSync(path.join(root,'native/steam_original_recovery_runtime.h'),'utf8'),
  recoveryAuto:fs.readFileSync(path.join(root,'native/sleep_recovery_auto.h'),'utf8'),
  classificationSource:fs.readFileSync(path.join(root,'native/sleep_resume_classification_native.inc'),'utf8')};
 const prepared=prepareSharedPowerProjectionSource(projectionInputs);
 assert.equal(prepared.extraction.recoveryStartupSelector.kind,'actual-pair-ledger-selector-block');
 for(const name of ['recoveryStartupSelector','recoveryFinalAdmission']){
  const row=prepared.extraction[name],body=projectionInputs.recoveryRuntime.slice(row.begin,row.end);
  assert.equal(digest(body),row.sha256);assert.equal(prepared.code.split(body).length,2,'Exact original '+name+' is included once');
 }
 assert.equal(prepared.caseNames.filter(name=>name==='ready-classification-startup-current-consumer').length,1);
 assert.doesNotMatch(prepared.code,/@@[A-Z_]+@@/);
 const pair='// Startup belongs to the exact Steam/game pair in each original ledger.';
 const admission='{std::lock_guard<std::mutex> lock(c->mx);decision=routeAdmission.admit(scope,{pending.kind,pending.generation,pending.sequence},enabled);';
 for(const [name,patch,pattern] of [
  ['missing pair contract',{recoveryRuntime:projectionInputs.recoveryRuntime.replace(pair,'MISSING_PAIR_CONTRACT')},/current pair Startup contract/],
  ['duplicate pair contract',{recoveryRuntime:projectionInputs.recoveryRuntime+'\n'+pair},/current pair Startup contract/],
  ['missing actual preview',{recoveryRuntime:projectionInputs.recoveryRuntime.replace('routeAdmission.preview(scope,startup,enabled).accepted','unreviewedPairAdmission')},/Actual pair Startup admission selector missing/],
  ['missing final admission',{recoveryRuntime:projectionInputs.recoveryRuntime.replace(admission,'MISSING_FINAL_ADMISSION')},/current route final admission/],
  ['duplicate final admission',{recoveryRuntime:projectionInputs.recoveryRuntime+'\n'+admission},/current route final admission/],
  ['missing final slot',{template:projectionInputs.template.replace('@@RECOVERY_FINAL_ADMISSION@@','MISSING_FINAL_SLOT')},/current projection fixture RECOVERY_FINAL_ADMISSION/],
  ['duplicate final slot',{template:projectionInputs.template+'\n@@RECOVERY_FINAL_ADMISSION@@'},/current projection fixture RECOVERY_FINAL_ADMISSION/]
 ])assert.throws(()=>prepareSharedPowerProjectionSource({...projectionInputs,...patch}),pattern,name);
});
const run=fs.readFileSync(path.join(here,'run.mjs'),'utf8');
const workerScope=run.indexOf('let nativeLaunchWorker=null,cssAppearanceWorker=null;');
assert.ok(workerScope>=0);
const start=run.indexOf('try{',workerScope),end=run.indexOf(' report.communityRegressionSources=',start);
assert.ok(start>workerScope&&end>start);
const actualEarlySequence=run.slice(start+4,end);
function actualSequence(overrides={}){
 const calls=[],report={};
 const given={...inputs,...overrides};
 const values=new Map([
  [path.join(root,'native/main.cpp'),given.main],
  [path.join(here,'native-fixture.cpp'),given.template],
  [path.join(root,'native/decky_sidebar_manual_repair.h'),given.repairHeader],
  [path.join(root,'native/decky_sidebar_automatic_repair.h'),given.automaticHeader]
 ]);
 const context={root,here,out:'inert',performance,baselineStartedAt:1,report,path,hash:()=>given.mainSha256,
  fs:{readFileSync(file){assert.ok(values.has(file));return values.get(file);}},
  prepareNativeSource(value){calls.push('prepare');return prepareNativeSource(value);},
  startNativeLaunchWorker(){calls.push('native');return {};},startCssAppearanceWorker(){calls.push('css');return {};}};
 return {calls,report,execute(){vm.runInNewContext(actualEarlySequence,context,{timeout:1000});}};
}
test('actual baseline sequence prepares once before either heavy worker',()=>{
 const observed=actualSequence();observed.execute();
 assert.deepEqual(observed.calls,['prepare','native','css']);
 assert.equal(observed.report.nativeSourceAdmission.preparedFiles,22);
 assert.equal(observed.report.nativeSourceAdmission.businessRuns,0);
});
test('actual baseline sequence launches zero workers on source rejection',()=>{
 const observed=actualSequence({main:main.replace('[]() -> ymcc::deckysidebar::LaunchAdmission {','MISSING')});
 assert.throws(()=>observed.execute(),/Review unique native launch admission anchor/);
 assert.deepEqual(observed.calls,['prepare']);
});
test('actual baseline sequence launches zero workers on formerly late mutation rejection',()=>{
 const observed=actualSequence({repairHeader:inputs.repairHeader.replaceAll('dispatchEntered=true;','MISSING')});
 assert.throws(()=>observed.execute(),/^Error: Uncertain manual-dispatch mutation anchor must be unique$/);
 assert.deepEqual(observed.calls,['prepare']);
});
