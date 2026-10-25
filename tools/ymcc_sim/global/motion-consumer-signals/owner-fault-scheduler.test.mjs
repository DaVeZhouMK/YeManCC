import test from 'node:test';import assert from 'node:assert/strict';
import {runOwnerFaults} from './owner-fault-scheduler.mjs';
const pending=()=>{let resolve,reject;const promise=new Promise((a,b)=>{resolve=a;reject=b;});return {promise,resolve,reject};};
const tick=()=>new Promise(resolve=>setImmediate(resolve));
test('five isolated faults run at most two at once and retain source order after different completion order',async()=>{
 const controls=Array.from({length:5},(_,id)=>({id})),jobs=controls.map(pending),started=[],finished=[],result={faults:[]};let active=0,max=0;
 const run=runOwnerFaults({controls,result,execute:async(c,i)=>{started.push(i);max=Math.max(max,++active);await jobs[i].promise;--active;finished.push(i);return {id:c.id,configurations:['Release','Checked']};}});
 assert.deepEqual(started,[0,1]);jobs[1].resolve();await tick();assert.deepEqual(started,[0,1,2]);jobs[2].resolve();await tick();assert.deepEqual(started,[0,1,2,3]);jobs[0].resolve();await tick();assert.deepEqual(started,[0,1,2,3,4]);jobs[4].resolve();jobs[3].resolve();await run;
 assert.equal(max,2);assert.equal(active,0);assert.deepEqual(finished,[1,2,0,4,3]);assert.deepEqual(result.faults.map(r=>r.id),[0,1,2,3,4]);assert.equal(result.faults.flatMap(r=>r.configurations).length,10);assert.equal(result.faultScheduling.events.length,10);assert(result.faultScheduling.events.every(e=>e.active>=0&&e.active<=2));
});
test('a failed fault stops queued work and rejection waits for the already started sibling to finish',async()=>{
 const jobs=[pending(),pending()],started=[],error=Error('actual fault failure'),result={faults:[]};let settled=false;
 const run=runOwnerFaults({controls:[0,1,2,3,4],result,execute:async(_,i)=>{started.push(i);await jobs[i].promise;return {id:i};}});const terminal=run.then(()=>{settled=true;throw Error('failure hidden');},e=>{settled=true;assert.equal(e,error);});
 jobs[0].reject(error);await tick();assert.equal(settled,false);assert.deepEqual(started,[0,1]);jobs[1].resolve();await terminal;assert.deepEqual(result.faults,[{id:1}]);assert.equal(result.faultScheduling.events.at(-1).active,0);assert.equal(settled,true);
});
test('two in-flight failures preserve the first failure after draining both',async()=>{
 const jobs=[pending(),pending()],first=Error('first actual failure'),second=Error('later failure'),result={faults:[]};let settled=false;
 const run=runOwnerFaults({controls:[0,1,2],result,execute:(_,i)=>jobs[i].promise});const terminal=run.catch(e=>{settled=true;assert.equal(e,first);});jobs[1].reject(first);await tick();assert.equal(settled,false);jobs[0].reject(second);await terminal;assert.equal(result.faultScheduling.events.filter(e=>e.phase==='start').length,2);assert.equal(result.faultScheduling.events.filter(e=>e.status==='FAIL').length,2);assert.equal(result.faultScheduling.events.at(-1).active,0);
});
test('synchronous failure stops dispatch without starting another fault',async()=>{
 const failure=Error('source preparation failed'),started=[],result={faults:[]};await assert.rejects(runOwnerFaults({controls:[0,1,2],result,execute:(_,i)=>{started.push(i);throw failure;}}),e=>e===failure);assert.deepEqual(started,[0]);assert.equal(result.faultScheduling.events.at(-1).active,0);
});
test('completed real fault receipts remain available when a later fault fails',async()=>{
 const jobs=[pending(),pending(),pending()],failure=Error('third fault failed'),result={faults:[]},started=[];
 const run=runOwnerFaults({controls:[0,1,2,3],result,execute:async(_,i)=>{started.push(i);await jobs[i].promise;return {id:i,status:'PASS'};}});const terminal=run.catch(e=>assert.equal(e,failure));jobs[0].resolve();await tick();assert.deepEqual(started,[0,1,2]);jobs[2].reject(failure);await tick();jobs[1].resolve();await terminal;assert.deepEqual(result.faults.map(r=>r.id),[0,1]);assert.deepEqual(started,[0,1,2]);
});
