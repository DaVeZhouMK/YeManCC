// Existing real Vue/API lifecycle regression, promoted into lighting.product-signals.
// Only IPC/settings are memory ports; no installed app, native or device I/O.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
const component='src/components/ControllerFeedbackCapabilitiesCard.vue';
const api='src/bridge/api.ts';
const helper='tools/controller_lighting_lifecycle_browser_selftest.mjs';
const baseline='tools/controller_lighting_operations_browser_selftest.mjs';
const sources=[component,api,helper,baseline,'src/components/Dropdown.vue','src/components/Slider.vue',
 'src/components/Toggle.vue','src/components/InlineIcon.vue','src/styles/tokens.css',
 'tools/ymcc_sim/global/lighting-signals/ui-lifecycle.mjs'];
const sha=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const snapshot=()=>Object.fromEntries(sources.map(p=>[p,sha(fs.readFileSync(path.join(repo,p)))]));
const load=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const save=(file,value)=>fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n');
const names=Object.freeze(['unmount-cancels-queued-write','deactivate-cancels-queued-write',
 'reactivation-allows-new-explicit-write','late-capabilities-after-unmount','late-capabilities-after-deactivation',
 'late-settings-after-unmount','late-settings-after-deactivation','unmount-revokes-second-rumble-pulse',
 'deactivate-revokes-second-rumble-pulse','old-capability-response-cannot-reenable-new-session','active-settings-still-consumed']);
const expectedIds=[720,800].flatMap(height=>names.map(name=>name+'-'+height));
const expectedFaultIds=expectedIds.filter(id=>!id.startsWith('reactivation-allows-new-explicit-write-')&&!id.startsWith('active-settings-still-consumed-'));
function validateCases(result,fault){
 assert.equal(result.hardwareIo,false,'Lifecycle fixtures must not claim hardware I/O');
 assert.equal(result.caseCount,22,'Exact lifecycle case count');
 assert.equal(result.cases.length,22,'Exact lifecycle rows');
 assert.deepEqual(result.cases.map(c=>c.id),expectedIds,'Required lifecycle cases and order');
 for(const c of result.cases){
  const height=Number(c.id.slice(c.id.lastIndexOf('-')+1));
  assert.deepEqual(c.viewport,{width:1280,height},'Actual required viewport');
  assert.equal(c.hardwareIo,false);
  assert.ok(c.actual&&c.expected,'Actual and expected business observations are required');
  assert.equal(c.status,JSON.stringify(c.actual)===JSON.stringify(c.expected)?'PASS':'FAIL','Case status must match its observations');
 }
 const failed=result.cases.filter(c=>c.status==='FAIL').map(c=>c.id);
 assert.deepEqual(failed,fault?expectedFaultIds:[],'Exact business failures from detached lifecycle hooks');
 assert.equal(result.failedCount,fault?18:0);
 assert.equal(result.status,fault?'FAIL':'PASS');
}
function validateLedger(ledger){
 assert.equal(ledger.actualProductionApi,true,'Actual production API wrapper must execute');
 assert.equal(ledger.nativeIo,false);
 for(const [cmd,timeoutMs] of [['gamepad.feedbackCapabilities',2000],['gamepad.setLed',35000],['gamepad.vibrate',3000]]){
  const calls=ledger.calls.filter(c=>c.cmd===cmd);
  assert.ok(calls.length>0,'Missing actual API command: '+cmd);
  assert.ok(calls.every(c=>c.options.timeoutMs===timeoutMs),'Actual API timeout changed: '+cmd);
 }
}
export function runUiLifecycle(outDir){
 const out=path.resolve(outDir);
 const relative=path.relative(repo,out);
 assert.ok(relative&&(relative==='..'||relative.startsWith('..'+path.sep)||path.isAbsolute(relative)),'Use evidence outside the source tree');
 assert.ok(!fs.existsSync(out),'Use fresh lifecycle evidence');
 fs.mkdirSync(out,{recursive:true});
 const before=snapshot();const executions=[];
 try{
  const original=fs.readFileSync(path.join(repo,component));
  fs.writeFileSync(path.join(out,'component-current.vue'),original);
  let fault=original.toString('utf8');
  for(const hook of ['onDeactivated(deactivateFeedback);','onBeforeUnmount(deactivateFeedback);']){
   assert.equal(fault.split(hook).length,2,'Lifecycle hook mutation must be exact: '+hook);
   fault=fault.replace(hook,'');
  }
  const faultFile=path.join(out,'component-without-exit-hooks.vue');fs.writeFileSync(faultFile,fault);
  const receipts=[];
  for(const isFault of [false,true]){
   const dir=path.join(out,isFault?'fault':'positive');
   const args=[path.join(repo,helper),dir,...(isFault?['--component-source',faultFile]:[])];
   const started=Date.now();
   const child=spawnSync(process.execPath,args,{cwd:repo,windowsHide:true,encoding:'utf8',timeout:105000,maxBuffer:4<<20});
   const command={exe:process.execPath,args,cwd:repo,pid:child.pid,exitCode:child.status,signal:child.signal,
    error:child.error?.message??null,elapsedMs:Date.now()-started};
   executions.push(command);
   fs.writeFileSync(path.join(out,isFault?'fault-launch.log':'positive-launch.log'),(child.stdout??'')+'\n'+(child.stderr??''));
   assert.equal(child.error,undefined,'Lifecycle launch/timeout is not business fault detection');
   assert.equal(child.signal,null,'Lifecycle child must terminate normally');
   assert.equal(child.status,isFault?1:0,'Lifecycle wrapper exit');
   const summary=load(path.join(dir,'summary.json'));
   assert.equal(summary.exit,isFault?1:0,'Inner browser probe exit');
   assert.equal(summary.signal,null);
   assert.ok(!summary.error,'Browser launch/timeout must not count as a caught fault');
   assert.equal(summary.sourceStable,true,'Executed source must remain stable');
   assert.equal(summary.hardwareIo,false);assert.equal(summary.installedApplicationStarted,false);
   assert.equal(summary.allPassed,!isFault);
   assert.equal(summary.sourceSha256,isFault?sha(Buffer.from(fault)):before[component]);
   assert.equal(summary.apiSourceSha256,before[api]);
   assert.equal(summary.fixtureBaselineSha256,before[baseline]);
   const cases=load(path.join(dir,'lifecycle-cases.json'));validateCases(cases,isFault);
   const ledger=load(path.join(dir,'ipc-ledger.json'));validateLedger(ledger);
   const stderr=fs.readFileSync(path.join(dir,'stderr.log'),'utf8');
   if(isFault){
    assert.match(stderr,/AssertionError \[ERR_ASSERTION\]: leaving lighting controls must revoke queued and pending initialization writes/);
    assert.match(stderr,/18 !== 0/);
   }
   receipts.push({dir,summary,cases,ledger,command});
  }
  const normalize=(dir,source)=>fs.readFileSync(path.join(dir,'browser-probe.mjs'),'utf8').replaceAll(JSON.stringify(source),'"<COMPONENT_SOURCE>"');
  assert.equal(normalize(receipts[0].dir,path.join(repo,component)),normalize(receipts[1].dir,faultFile),'Positive and fault probes may differ only in explicit component path');
  const after=snapshot();assert.deepEqual(after,before,'Lifecycle source closure changed during proof');
  const cases=[...receipts[0].cases.cases.map(c=>({...c,id:'ui-lifecycle-'+c.id,consumer:component,apiConsumer:api})),
   {id:'ui-lifecycle-private-exit-hooks-fault-detected',status:'PASS',actual:{exitCode:1,failedIds:receipts[1].cases.cases.filter(c=>c.status==='FAIL').map(c=>c.id)},
    expected:{exitCode:1,failedIds:expectedFaultIds},hardwareIo:false}];
  const result={status:'PASS',scope:'Actual Vue and production API lifecycle; IPC/settings are memory ports',
   sourceHashes:after,cases,positiveCaseCount:22,expectedFaultFailureCount:18,
   negative:{source:faultFile,sourceSha256:sha(fs.readFileSync(faultFile)),disconnectedHooks:['onDeactivated(deactivateFeedback);','onBeforeUnmount(deactivateFeedback);'],
    exitCode:1,failedIds:expectedFaultIds,sameProbeExceptComponentPath:true,evidenceDir:receipts[1].dir},
   executions,executedProductModules:[component,api],hardwareIo:false,physicalEffectVerified:false,
   boundary:'Does not cancel an already issued native command; native ownership/stop and physical LED/motor acceptance remain separate.'};
  save(path.join(out,'result.json'),result);return result;
 }catch(error){save(path.join(out,'result.json'),{status:'FAIL',sourceHashes:before,executions,error:String(error),hardwareIo:false});throw error;}
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 const result=runUiLifecycle(process.argv[2]);
 console.log(JSON.stringify({status:result.status,positiveCaseCount:result.positiveCaseCount,expectedFaultFailureCount:result.expectedFaultFailureCount,evidenceDir:path.resolve(process.argv[2])}));
}
