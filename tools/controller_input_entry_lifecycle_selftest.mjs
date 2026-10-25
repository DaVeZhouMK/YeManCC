import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {compileEntryBundle,createEntryFixture,repoRoot,fileHash,sha} from './fixtures/controller_input_entry_lifecycle_fixture.mjs';
import {verifyEntryToolchain,toolchainManifest} from './fixtures/controller_input_entry_toolchain.mjs';
const toolchainInputs=verifyEntryToolchain();
const args=process.argv.slice(2);assert.equal(args.length,2);assert.equal(args[0],'--out');const out=path.resolve(args[1]);assert(path.isAbsolute(args[1])&&!fs.existsSync(out));assert(!out.toLowerCase().startsWith(repoRoot.toLowerCase()+path.sep));fs.mkdirSync(out,{recursive:true});
const fixtureFile=path.join(repoRoot,'tools/fixtures/controller_input_entry_lifecycle_fixture.mjs'),self=path.join(repoRoot,'tools/controller_input_entry_lifecycle_selftest.mjs');
const harnessInputs=[self,fixtureFile,path.join(repoRoot,'tools/fixtures/controller_input_entry_toolchain.mjs'),toolchainManifest].map(p=>({path:p,sha256:fileHash(p)}));
const game={path:'C:/Games/a.exe',pid:111,processCreated:'born-111'};
async function suite(compiled){const cases=[],failures=[];let checks=0;
 const ck=(v,msg)=>{checks++;assert.ok(v,msg);};
 const check=async(name,run)=>{try{await run();cases.push({name,pass:true});}catch(e){failures.push({name,error:e.stack??String(e)});cases.push({name,pass:false});}};
 async function fixture(run){const f=await createEntryFixture(compiled);try{await run(f);}finally{f.stop();}}
 await check('actual-repository-native-protection-reply-is-not-commit',()=>fixture(async f=>{
  const before=f.disk;f.protectedReply=true;const result=await f.repo.compareAndSwapInputSettings(10,{outputTarget:{persona:'disabled'}});
  ck(result.ok===false&&result.reason==='virtual-start-protection-active','native protected reply preserved');ck(result.startProtection.locked&&result.startProtection.requestId===41,'same native lock receipt retained');ck(f.disk===before&&f.commits.length===0,'rejected native target cannot update durable state');ck((await f.repo.loadSettings()).input.outputTarget.persona==='elite','rejected target cannot enter cached input');
 }));
 for(const persona of ['disabled','dualsense-edge','steamdeck'])await check('mirror-rejected-'+persona+'-does-not-publish-save-or-clear',()=>fixture(async f=>{
  const before=f.disk;f.protectedReply=true;const r=await f.mirror(persona);
  ck(!r.ok,'mirror cannot report saved after native rejection');ck(f.disk===before&&f.commits.length===0,'mirror refusal retains original complete document');ck(f.clearShortcut===0,'rejected mirror request cannot clear current shortcut state');ck(f.events.every(e=>e.type!=='gp:pad-persona'),'no rejected-persona publish');ck(f.leases===0,'mirror ownership released on refusal');
 }));
 for(const persona of ['disabled','steamdeck'])await check('mirror-accepted-'+persona+'-is-pending-not-device-ready',()=>fixture(async f=>{
  const r=await f.mirror(persona);const d=f.document();
  ck(r.ok&&r.result.saved===true&&r.result.pending===true&&r.result.applied===false,'durable mirror save is pending, not Host or device readiness');ck(d.input.outputTarget.persona===persona&&d.input.revision===11,'actual repository commits intended persona once');ck(f.commits.length===1&&f.clearShortcut===1,'only accepted mirror save clears old shortcut once');ck(d.input.appliedRevision===f.initial.input.appliedRevision&&d.input.applyStatus==='saved-pending','durable save cannot advance applied revision');ck(d.futureDocument.retained===true,'unrelated document data retained');
 }));
 await check('mirror-cancelled-at-real-queued-CAS-boundary',()=>fixture(async f=>{
  const before=f.disk;const r=await f.mirror('disabled',{beforeCas:async invalidate=>{invalidate();}});
  ck(!r.ok,'retired mirror source refuses');ck(f.writeAttempts.length===0&&f.disk===before,'cancellation observed before native settings.write');ck(f.clearShortcut===0&&f.leases===0,'cancelled source neither clears shortcut nor retains owner');
 }));
 await check('overlay-protected-retry-retains-base-and-emits-no-success',()=>fixture(async f=>{
  const before=f.disk;f.state.config.entries['a.exe']={enabled:true,padPersona:'steamdeck',gyroOverride:'off'};f.protectedReply=true;f.startOverlay();await f.setGame(game);await f.tick();
  ck(f.disk===before&&f.commits.length===0,'native rejection retains base and overlay');ck(f.events.every(e=>e.type!=='gp:pad-persona'),'rejected overlay must not publish persona success');ck(f.overlay.getGameInputOverrideState().locked,'dedicated game retains its editing lock while retry is pending');ck(f.writeAttempts.length>0&&f.writeAttempts.length<=6,'existing bounded three-attempt loop only');
  f.protectedReply=false;await f.tick();const committed=f.document();ck(f.commits.length===1,'one later accepted overlay save');ck(committed.input.outputTarget.persona==='elite'&&committed.input.gameOverride.outputTarget.persona==='steamdeck','overlay does not overwrite base persona');ck(f.events.filter(e=>e.type==='gp:pad-persona').length===1,'one accepted persona event');
  await f.tick();ck(f.commits.length===1,'stable periodic retry does not re-save accepted overlay');
 }));
 await check('overlay-native-revision-conflict-reloads-current-commit',()=>fixture(async f=>{
  f.state.config.entries['a.exe']={enabled:true,padPersona:'steamdeck',gyroOverride:'off'};f.changeRevisionBeforeWrite=true;f.startOverlay();await f.setGame(game);await f.tick();const d=f.document();
  ck(f.revisionConflicts===1&&f.commits.length===1,'one real repository conflict then one accepted retry');ck(d.input.revision===12&&d.input.futureNativeValue==='retained','native revision and unknown fields never roll back');ck(d.input.outputTarget.persona==='elite'&&d.input.gameOverride.outputTarget.persona==='steamdeck','current document base survives overlay retry');ck(f.events.filter(e=>e.type==='gp:pad-persona').length===1,'only actual accepted overlay publishes');
 }));
 await check('overlay-stop-during-queued-read-cancels-before-native-write',()=>fixture(async f=>{
  let enteredResolve,release;const entered=new Promise(r=>{enteredResolve=r});const held=new Promise(r=>{release=r});let once=true;
  f.beforeReadText=async()=>{if(once){once=false;enteredResolve();await held;}};f.state.config.entries['a.exe']={enabled:true,padPersona:'steamdeck',gyroOverride:'off'};f.startOverlay();f.state.game=game;
  const task=f.tick();await entered;f.overlay.stopGameInputOverrideWatch();release();await task;
  ck(f.commits.length===0&&f.writeAttempts.length===0,'stopped generation cancels at repository commit boundary');ck(f.events.every(e=>e.type!=='gp:pad-persona'),'stopped overlay cannot publish delayed success');ck(f.state.scheduled.size===0,'stop removes the existing retry owner');
 }));
 await check('mirror-and-overlay-serialized-native-rejection-then-one-recovery',()=>fixture(async f=>{
  let enteredResolve,release;const entered=new Promise(r=>{enteredResolve=r}),held=new Promise(r=>{release=r});let once=true;
  f.protectedReply=true;f.beforeWrite=async()=>{if(once){once=false;enteredResolve();await held;}};
  f.state.config.entries['a.exe']={enabled:true,padPersona:'steamdeck',gyroOverride:'off'};f.startOverlay();const before=f.disk;
  const mirror=f.mirror('dualsense-edge');await entered;await f.setGame(game);release();const r=await mirror;await f.flush();
  ck(!r.ok&&f.commits.length===0&&f.disk===before,'queued mirror and overlay cannot commit through protected native replies');ck(f.clearShortcut===0&&f.events.every(e=>e.type!=='gp:pad-persona'),'neither rejected caller publishes success effects');
  f.protectedReply=false;await f.tick();ck(f.commits.length===1,'after release only current game overlay retries successfully');const d=f.document();ck(d.input.outputTarget.persona==='elite'&&d.input.gameOverride.outputTarget.persona==='steamdeck','retired mirror persona never becomes base or overlay');ck(f.leases===0,'no old mirror mutation owner remains');
 }));
 await check('legacy-restore-retained-after-protection-and-cleared-after-commit',()=>fixture(async f=>{
  f.state.config.inputRestore={persona:'steamdeck',buttonMappingEnabled:true,gyroEnabled:false,motionEnabled:false,gyroMotion:structuredClone(f.initial.input.gyroMotion)};f.protectedReply=true;const before=f.disk;f.startOverlay();await f.tick();
  ck(f.disk===before&&f.state.config.inputRestore&&f.state.restoreClears===0,'rejected legacy restore preserves recovery snapshot');f.protectedReply=false;await f.tick();ck(f.commits.length===1&&f.state.restoreClears===1&&!f.state.config.inputRestore,'accepted legacy restore is cleared once after commit');ck(f.document().input.outputTarget.persona==='steamdeck','committed restore is reflected in actual document');await f.tick();ck(f.commits.length===1&&f.state.restoreClears===1,'successful restore is not replayed');
 }));
 await check('write-failure-remains-unsaved-without-shortcut-clear',()=>fixture(async f=>{
  f.writeFailure=true;const before=f.disk;const r=await f.mirror('disabled');ck(!r.ok&&f.disk===before&&f.commits.length===0,'actual repository propagates failed write');ck(f.clearShortcut===0&&f.leases===0,'write failure cannot clear shortcut or leak mutation lease');
 }));
 return{status:failures.length?'FAIL':'PASS',passed:failures.length===0,cases,checks,failures};
}
const runs=[];try{
 for(const mutation of [null,'protected-nack-as-commit','overlay-publishes-rejected-save','mirror-ignores-rejected-save']){
  const name=mutation??'positive',dir=path.join(out,name);fs.mkdirSync(dir);const compiled=await compileEntryBundle(mutation);fs.writeFileSync(path.join(dir,'actual-entry.cjs'),compiled.code);fs.writeFileSync(path.join(dir,'COMPILED-SOURCES.json'),JSON.stringify(compiled,null,2));
  const result=await suite(compiled);fs.writeFileSync(path.join(dir,'RESULT.json'),JSON.stringify(result,null,2));const changed=compiled.sources.filter(r=>fileHash(r.path)!==r.sha256);assert.equal(changed.length,0,'Source changed during run');
  const expected=mutation!==null;assert.equal(result.passed,!expected,'Compiled production control was not caught: '+name);if(expected){const required=mutation==='protected-nack-as-commit'?'actual-repository-native-protection-reply-is-not-commit':mutation==='overlay-publishes-rejected-save'?'overlay-protected-retry-retains-base-and-emits-no-success':'mirror-rejected-disabled-does-not-publish-save-or-clear';assert(result.failures.some(x=>x.name===required),'Control missed designated assertion');}
  runs.push({name,expectedRejection:expected,status:expected?'EXPECTED_PRODUCTION_MUTATION_CAUGHT':result.status,cases:result.cases.length,checks:result.checks,assertionFailures:result.failures.length,sources:compiled.sources,mutations:compiled.modified,bundleSha256:compiled.sha256});console.log(JSON.stringify({name,status:runs.at(-1).status,cases:result.cases.length,checks:result.checks,failures:result.failures.length}));
 }
 assert(harnessInputs.every(r=>fileHash(r.path)===r.sha256),'Harness input drift');
 verifyEntryToolchain();
 const report={status:'PASS',passed:true,createdAt:new Date().toISOString(),positiveCases:runs[0].cases,positiveChecks:runs[0].checks,runs,harnessInputs,toolchainInputs,actualRepositoryApiIpcExecuted:true,actualCallerConsumersExecuted:true,nativeAlgorithmModeled:false,nativeReplyKindsAreInertBoundary:true,normalProductExecuted:false,realDevicesOrDrivers:false,realSettingsWritten:false,physicalAcceptance:false,scope:'Actual frontend caller/repository/api/ipc handling of native protection/conflict/failure replies and queued cancellation; native 30s admission/cleanup remains covered separately by actual full-TU suite.'};
 fs.writeFileSync(path.join(out,'RESULT.json'),JSON.stringify(report,null,2));
}catch(error){fs.writeFileSync(path.join(out,'RESULT.json'),JSON.stringify({status:'ERROR',error:error.stack??String(error),runs},null,2));console.error(error);process.exitCode=1;}