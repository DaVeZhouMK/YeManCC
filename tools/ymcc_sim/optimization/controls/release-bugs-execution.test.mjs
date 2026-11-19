import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import vm from 'node:vm';
import {performance} from 'node:perf_hooks';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {selectReleaseBugs,RELEASE_CANDIDATE_PATH_KEYS} from '../release-bugs.mjs';
import {simulationCorePath} from '../../global/release-host-environment.mjs';

const entry=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../cli.mjs');
const repo=path.resolve(path.dirname(entry),'../../..');
const ids=['steam.shared-mode-source','steam.contact-source'];
// Complete original optimizer worker and actual release selector; only registry,
// source identity and execution ports are inert. These are not business PASSes.
for(const scenario of ['failed','timeout','cached-failure','identity-drift','all-pass','ordinary-diagnostic','rerun-preparation','rerun-preparation-stops','cache-lock-metadata','cache-lock-foreign','cache-lock-roundtrip']){
 test(`release bug execution ${scenario}`,async(t)=>{
  assert.equal(typeof vm.SourceTextModule,'function','Run with --experimental-vm-modules');
  const evidence=fs.realpathSync(simulationCorePath('Evidence'));
  const base=path.resolve(process.env.YMCC_SIM_RELEASE_BUG_EXECUTION_TEST_ROOT??path.join(evidence,'ReleaseBugExecutionControls'));
  const relative=path.relative(evidence,base);assert(relative!==''&&!path.isAbsolute(relative)&&relative!=='..'&&!relative.startsWith('..'+path.sep));
  fs.mkdirSync(base,{recursive:true});const owner=fs.realpathSync(base);assert.equal(owner.toLowerCase(),base.toLowerCase());
  const root=fs.mkdtempSync(path.join(owner,'required-bug-'));
  let timer;
  const heldLocks=new Set();
  try{
   const lockScenario=scenario.startsWith('cache-lock-');
   const selectedIds=lockScenario?[ids[0]]:ids;
   const runDir=path.join(root,'run');fs.mkdirSync(runDir);
   const planFile=path.join(root,'bug-plan.json');
   fs.writeFileSync(planFile,JSON.stringify({schemaVersion:1,kind:'ymcc-release-bug-plan',issues:[{id:'controlled-required-bugs',reason:'Inert original-worker execution-order control',purpose:'bug-regression',testIds:selectedIds}]}));
   const cacheRoot=path.join(root,'State/ExecutionCache');fs.mkdirSync(cacheRoot,{recursive:true});
   const firstLock=path.join(cacheRoot,ids[0]+'.json.lock'),foreignBytes='foreign lock owner sentinel';
   if(scenario==='cache-lock-foreign')fs.writeFileSync(firstLock,foreignBytes);
   if(scenario==='cached-failure')fs.writeFileSync(path.join(cacheRoot,ids[0]+'.json'),JSON.stringify({result:{path:path.join(root,'prior-failure.json'),sha256:'f'.repeat(64)}}));
   const calls=[],locks=[],fingerprints=new Map(),fingerprintEvents=[],logs=[];
   let metadataFaults=0;
   const fakeConfiguration=JSON.stringify({aliases:{},modules:{}});
   const configuration=path.resolve(repo,'tools/ymcc_sim/optimization/modules.json');
   const registration=path.resolve(repo,'tools/ymcc_sim/global/extra-adapter-index.json');
   const controlledFs=new Proxy(fs,{get(target,key){
    if(key==='readFileSync')return function(file,...args){
     const full=path.resolve(String(file));const text=full===configuration?fakeConfiguration:full===registration?' {"groups":[]}':null;
     if(text!==null)return typeof args[0]==='string'||args[0]?.encoding?text:Buffer.from(text);
     return target.readFileSync(file,...args);
    };
    if(key==='openSync')return function(file,...args){const isLock=String(file).endsWith('.lock');if(isLock)locks.push(path.basename(String(file)));const fd=target.openSync(file,...args);if(isLock)heldLocks.add(fd);return fd;};
    if(key==='closeSync')return function(fd){const result=target.closeSync(fd);heldLocks.delete(fd);return result;};
    if(key==='writeFileSync')return function(file,...args){if(scenario==='cache-lock-metadata'&&heldLocks.has(file)&&metadataFaults===0){metadataFaults++;throw Object.assign(new Error('controlled cache lock metadata write failure'),{code:'ENOSPC'});}return target.writeFileSync(file,...args);};
    return target[key];
   }});
   const hash=file=>crypto.createHash('sha256').update(controlledFs.readFileSync(file)).digest('hex');
   const adapters=ids.map((id,index)=>({id,domain:'steam',kind:'node',path:`inert-${index}.mjs`,args:[],profiles:['desktop'],scope:'source-contract',timeoutMs:1000,evidenceInputs:[]}));
   const executeAdapter=async(adapter)=>{
    calls.push(adapter.id);const first=adapter.id===ids[0];
    if(scenario==='cache-lock-roundtrip'){const metadata=JSON.parse(fs.readFileSync(firstLock,'utf8'));assert.equal(metadata.pid,process.pid);assert.equal(metadata.id,adapter.id);assert.equal(heldLocks.size,1);}
    const status=first&&['failed','ordinary-diagnostic','rerun-preparation-stops'].includes(scenario)?'FAIL':first&&scenario==='timeout'?'TIMEOUT':'PASS';
    const evidenceDir=path.join(runDir,'cases',adapter.id);fs.mkdirSync(evidenceDir,{recursive:true});
    for(const name of ['stdout.log','stderr.log'])fs.writeFileSync(path.join(evidenceDir,name),'inert executor output');
    const result={id:adapter.id,status,pid:10000+calls.length,evidenceDir};fs.writeFileSync(path.join(evidenceDir,'result.json'),JSON.stringify(result));return result;
   };
   const fingerprintCase=({adapter})=>{
    const count=(fingerprints.get(adapter.id)??0)+1;fingerprints.set(adapter.id,count);
    fingerprintEvents.push({id:adapter.id,count,locked:fs.existsSync(path.join(cacheRoot,adapter.id+'.json.lock')),afterExecution:calls.includes(adapter.id)});
    const digest=scenario==='identity-drift'&&adapter.id===ids[0]&&calls.includes(adapter.id)?'2'.repeat(64):'1'.repeat(64);
    return {adapterId:adapter.id,cacheable:true,identity:adapter.id+digest,reasons:[],manifest:{inputs:[{role:'source',path:adapter.path,sha256:digest}],tools:[],options:{}}};
   };
   const inspectCachedResult=({identity})=>scenario==='cached-failure'&&identity.adapterId===ids[0]
    ?{reusable:true,status:'UNCHANGED_FAILURE_REFERENCE',previousStatus:'FAIL',reasons:[]}
    :{reusable:false,status:'NOT_CACHED',reasons:[]};
   const ordinary=scenario==='ordinary-diagnostic';
   const args=ordinary?['--mode','run','--test',ids.join(',')]:['--mode','run','--bug-plan',planFile];
   if(!ordinary&&scenario!=='cached-failure')args.push('--rerun');
   const contextPort={args,runDir,deadlineEpochMs:Date.now()+20000,cleanupReserveMs:0,remainingMs:()=>20000};
   let finish;
   const finished=new Promise(resolve=>{finish=resolve;});
   const processPort={argv:[process.execPath,entry,'--budget-worker','owned-context','owned-nonce'],execPath:process.execPath,execArgv:[],pid:process.pid,platform:process.platform,env:{}};
   Object.defineProperty(processPort,'exitCode',{set(value){finish(value);}});
   const context=vm.createContext({process:processPort,console:{log:value=>logs.push(String(value)),error:value=>logs.push(String(value))},performance,Buffer,URL});
   const module=new vm.SourceTextModule(fs.readFileSync(entry,'utf8'),{context,identifier:entry,initializeImportMeta(meta){meta.url=pathToFileURL(entry).href;meta.dirname=path.dirname(entry);}});
   const unused=()=>{throw new Error('Unselected capability or nested launcher must not run');};
   const bindings={
    'node:fs':{default:controlledFs},'node:path':{default:path},'node:crypto':{default:crypto},'node:url':{fileURLToPath},
    '../global/adapters.mjs':{getAdapters:()=>adapters},'../global/sleep.mjs':{getSleepAdapters:()=>[]},
    '../global/runner.mjs':{executeAdapter,validateAdapters:()=>{},sha256File:hash,makeRunDirectory:unused},
    '../global/candidate-options.mjs':{configureCandidateAdapters:a=>a,validateCommonHidSelection:()=>{}},
    '../global/bundled-node-tool-authority.mjs':{BUNDLED_AUTHORITY_SOURCES:[],lockBundledNodeTools:unused},
    './identity-cache.mjs':{fingerprintCase,inspectCachedResult,deduplicateCases:a=>({canonical:a,deduplicated:[]})},
    '../global/release-host-environment.mjs':{simulationCorePath:()=>root},
    '../global/controller-lifecycle-pipe/wire-contract.mjs':{WIRE_ID:'controller.unselected-wire',wireDescriptorContract:unused},
    '../global/controller-foreground-latency/prehost-cache-contract.mjs':{PREHOST_ID:'controller.unselected-prehost',isPreHostCacheAdapter:()=>false,preHostDescriptorContract:unused,preHostDependencyPaths:()=>[],preHostEvidenceFiles:()=>[]},
    './release-bugs.mjs':{selectReleaseBugs,RELEASE_CANDIDATE_PATH_KEYS},
    './selection-policy.mjs':{validateSelectionPolicy:()=>({scope:'inert-explicit-original-ID-control'})},
    './execution-budget.mjs':{runWithWallBudget:unused,readBudgetWorker:()=>contextPort,validateWallBudget:n=>n,MAX_WALL_BUDGET_MS:1800000}
   };
   await module.link(specifier=>{assert(Object.hasOwn(bindings,specifier),'Unreviewed dependency '+specifier);const values=bindings[specifier];return new vm.SyntheticModule(Object.keys(values),function(){for(const[key,value]of Object.entries(values))this.setExport(key,value);},{context,identifier:specifier});});
   await module.evaluate();
   const exit=await Promise.race([finished,new Promise((_,reject)=>{timer=setTimeout(()=>reject(new Error('Original optimizer worker did not terminate')),5000);})]);
   const result=JSON.parse(fs.readFileSync(path.join(runDir,'result.json'),'utf8'));
   const preparedPlan=JSON.parse(fs.readFileSync(path.join(runDir,'plan.json'),'utf8'));
   const ownedOpenFdsAtWorkerReturn=heldLocks.size,lockExistsAtWorkerReturn=fs.existsSync(firstLock);
   let lockReacquired=null,reacquireError=null;
   if(scenario==='cache-lock-metadata'){
    try{const fd=fs.openSync(firstLock,'wx');try{lockReacquired=true;}finally{fs.closeSync(fd);fs.unlinkSync(firstLock);}}
    catch(error){lockReacquired=false;reacquireError=error.code??error.message;}
   }
   t.diagnostic(JSON.stringify({scenario,exit,executorPortCalls:calls,locks,fingerprints:Object.fromEntries(fingerprints),fingerprintEvents,ownedOpenFdsAtWorkerReturn,lockExistsAtWorkerReturn,lockReacquired,reacquireError,metadataFaults,planned:preparedPlan.tests.map(r=>({id:r.id,identity:r.identity,identityDeferredToLockedExecution:r.identityDeferredToLockedExecution})),rows:result.rows.map(r=>({id:r.id,status:r.status,reason:r.reason,executedThisRun:r.executedThisRun,actualPassedThisRun:r.actualPassedThisRun}))}));
   if(lockScenario){
    assert.equal(result.rows.length,1);assert.equal(ownedOpenFdsAtWorkerReturn,0,'Owned metadata-write failure must close its acquired fd');
    const row=result.rows[0];
    if(scenario==='cache-lock-metadata'){
     assert.equal(exit,10);assert.equal(row.status,'NOT_RUN_LOCK_RECORDING_FAILED');assert.equal(row.reason,'ENOSPC');
     assert.equal(row.executedThisRun,false);assert.equal(row.actualPassedThisRun,false);assert.deepEqual(calls,[]);
     assert.equal(metadataFaults,1);assert.equal(lockExistsAtWorkerReturn,false);assert.equal(lockReacquired,true);
    }else if(scenario==='cache-lock-foreign'){
     assert.equal(exit,10);assert.equal(row.status,'NOT_RUN_BUSY');assert.equal(row.reason,'EEXIST');assert.deepEqual(calls,[]);
     assert.equal(lockExistsAtWorkerReturn,true);assert.equal(fs.readFileSync(firstLock,'utf8'),foreignBytes);assert.equal(metadataFaults,0);
    }else{
     assert.equal(exit,0);assert.equal(row.status,'PASS');assert.deepEqual(calls,[ids[0]]);assert.equal(lockExistsAtWorkerReturn,false);
     assert.equal(fingerprints.get(ids[0]),2);assert(fingerprintEvents.every(event=>event.locked));
    }
    return;
   }
   const stop=['failed','timeout','cached-failure','identity-drift','rerun-preparation-stops'].includes(scenario);
   assert.equal(exit,['all-pass','rerun-preparation'].includes(scenario)?0:scenario==='identity-drift'?10:2);
   assert.equal(result.rows.length,2);assert.deepEqual(result.rows.map(r=>r.id),ids);
   const first=result.rows[0],second=result.rows[1];
   if(stop){
    assert.deepEqual(calls,scenario==='cached-failure'?[]:[ids[0]]);
    assert.equal(second.status,'NOT_RUN_AFTER_FAILURE');assert.equal(second.blockedBy,ids[0]);assert.equal(second.blockingStatus,first.status);
    assert.equal(second.executedThisRun,false);assert.equal(second.actualPassedThisRun,false);assert.equal(second.attemptedThisRun,false);
    assert.equal(locks.includes(ids[1]+'.json.lock'),false);assert.equal(fs.existsSync(path.join(cacheRoot,ids[1]+'.json')),false);
   }else{
    assert.deepEqual(calls,ids);assert.equal(second.status,'PASS');assert.equal(second.actualPassedThisRun,true);
   }
   assert.equal(first.status,scenario==='failed'||ordinary||scenario==='rerun-preparation-stops'?'FAIL':scenario==='timeout'?'TIMEOUT':scenario==='cached-failure'?'UNCHANGED_FAILURE_REFERENCE':scenario==='identity-drift'?'INCOMPLETE_IDENTITY_DRIFT':'PASS');
   if(scenario==='cached-failure'){assert.equal(first.executedThisRun,false);assert.equal(first.actualPassedThisRun,false);}
   if(scenario.startsWith('rerun-preparation')){
    assert.equal(fingerprints.get(ids[0]),2,'Only locked before/after identity scans are needed for an explicit rerun');
    assert.equal(fingerprints.get(ids[1])??0,stop?0:2,'A never-started required case needs no initial identity scan');
    for(const id of stop?[ids[0]]:ids){
     const events=fingerprintEvents.filter(row=>row.id===id);
     assert.deepEqual(events.map(row=>row.locked),[true,true]);assert.deepEqual(events.map(row=>row.afterExecution),[false,true]);
    }
    assert(preparedPlan.tests.every(row=>row.identityDeferredToLockedExecution===true&&row.identity===null&&row.cacheable===null&&row.decision==='RUN_EXPLICIT'));
   }
   assert.equal(fs.readdirSync(cacheRoot).some(name=>name.endsWith('.lock')),false);
  }finally{
   if(timer)clearTimeout(timer);
   for(const fd of heldLocks){fs.closeSync(fd);heldLocks.delete(fd);}
   const actual=fs.realpathSync(root);assert.equal(path.dirname(actual),owner);assert(path.basename(actual).startsWith('required-bug-'));
   fs.rmSync(actual,{recursive:true,force:true});
  }
 });
}
