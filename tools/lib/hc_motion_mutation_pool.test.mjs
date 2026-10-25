import test from 'node:test';import assert from 'node:assert/strict';
import {runBoundedTasks,runLoggedChild} from './hc_motion_mutation_pool.mjs';
const immediate=()=>new Promise(resolve=>setImmediate(resolve));
test('independent jobs overlap up to the exact bound and retain input result order',async()=>{
 let active=0,max=0,started=[];const gates=Array.from({length:4},()=>{let resolve;const promise=new Promise(r=>resolve=r);return {promise,resolve};});
 const done=runBoundedTasks([0,1,2,3],{concurrency:2,task:async value=>{started.push(value);max=Math.max(max,++active);await gates[value].promise;active--;return value*2;}});
 await immediate();assert.deepEqual(started,[0,1]);gates[1].resolve();await immediate();assert.deepEqual(started,[0,1,2]);gates[2].resolve();await immediate();assert.deepEqual(started,[0,1,2,3]);gates[3].resolve();gates[0].resolve();assert.deepEqual(await done,[0,2,4,6]);assert.equal(max,2);assert.equal(active,0);
});
test('failure stops queued jobs and awaits the already active child before returning',async()=>{
 let fail,release;const fault=new Promise((_,reject)=>fail=reject),pending=new Promise(resolve=>release=resolve),started=[];let drained=false,settled=false;
 const result=runBoundedTasks([0,1,2,3],{concurrency:2,task:async value=>{started.push(value);if(value===0)return fault;await pending;drained=true;return value;}}).catch(error=>error).then(value=>{settled=true;return value;});
 await immediate();fail(Error('EXPECTED_OWNED_TASK_FAILURE'));await immediate();assert.equal(settled,false);assert.deepEqual(started,[0,1]);release();const error=await result;assert.equal(error.message,'EXPECTED_OWNED_TASK_FAILURE');assert(drained);assert.equal(error.partialResults[1],1);assert.deepEqual(started,[0,1]);
});
test('invalid concurrency cannot silently skip work',async()=>{for(const concurrency of [0,-1,1.5,5,NaN])await assert.rejects(runBoundedTasks([1],{concurrency,task:async value=>value}),/MOTION_MUTATION_POOL_ARGUMENTS/);});
test('natural exit1 remains observable and separate from fault assertion acceptance',async()=>{const child=await runLoggedChild(process.execPath,['-e',"process.stdout.write('controlled assertion failure');process.exitCode=1"],{cwd:process.cwd(),timeoutMs:5000});assert.equal(child.status,1);assert.equal(child.signal,null);assert(child.naturalExitObserved);assert.equal(child.stdout,'controlled assertion failure');assert.equal(child.error,null);});
test('closed stdio is captured completely before the child returns',async()=>{const text='x'.repeat(65536),child=await runLoggedChild(process.execPath,['-e',"process.stdout.write('x'.repeat(65536));process.stderr.write('closed')"],{cwd:process.cwd(),timeoutMs:5000});assert.equal(child.status,0);assert(child.naturalExitObserved);assert.equal(child.stdout,text);assert.equal(child.stderr,'closed');});
test('timeout cannot count as a naturally terminated semantic fault',async()=>{const child=await runLoggedChild(process.execPath,['-e','setInterval(()=>{},1000)'],{cwd:process.cwd(),timeoutMs:250});assert.equal(child.timedOut,true);assert.equal(child.naturalExitObserved,false);assert(child.error);});
test('output truncation rejects instead of signing partial evidence',async()=>{const child=await runLoggedChild(process.execPath,['-e',"process.stdout.write('x'.repeat(10000));setInterval(()=>{},1000)"],{cwd:process.cwd(),timeoutMs:5000,outputLimit:32});assert.equal(child.outputTruncated,true);assert.equal(child.naturalExitObserved,false);assert(child.stdout.length<=32);});
test('launch failure is distinct from an ordinary nonzero exit',async()=>{const child=await runLoggedChild(process.execPath+'-does-not-exist',[],{cwd:process.cwd(),timeoutMs:5000});assert(child.launchError);assert.equal(child.naturalExitObserved,false);assert.equal(child.status,-4058);});

test('supported four-lane ownership retains every fault in source order under out-of-order completion',async()=>{
 let active=0,max=0;const started=[],gates=Array.from({length:7},()=>{let resolve;const promise=new Promise(r=>resolve=r);return {promise,resolve};});
 const done=runBoundedTasks([0,1,2,3,4,5,6],{concurrency:4,task:async value=>{started.push(value);max=Math.max(max,++active);await gates[value].promise;active--;return 'fault-'+value;}});
 await immediate();assert.deepEqual(started,[0,1,2,3]);assert.equal(active,4);
 gates[2].resolve();await immediate();assert.deepEqual(started,[0,1,2,3,4]);assert.equal(active,4);
 gates[0].resolve();gates[4].resolve();await immediate();assert.deepEqual(started,[0,1,2,3,4,5,6]);assert.equal(active,4);
 for(const index of [6,3,5,1])gates[index].resolve();assert.deepEqual(await done,['fault-0','fault-1','fault-2','fault-3','fault-4','fault-5','fault-6']);assert.equal(max,4);assert.equal(active,0);
});
