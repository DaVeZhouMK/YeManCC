import assert from 'node:assert/strict';
// Additional protocol failures execute the same production hub exported by the
// retained lifecycle test. Only command replies and timer scheduling are inputs.
export async function checkProductReceipts(m,trace){
 const settle=async()=>{for(let i=0;i<80;i++)await Promise.resolve();};
 function make(start,stop=async()=>({ok:true})){
  let now=100000,id=0;const timers=new Map(),calls=[];
  const hub=m.createMonitorDataHub({now:()=>now,setTimer:(fn,ms)=>{timers.set(++id,{fn,at:now+ms});return id},clearTimer:key=>timers.delete(key),
   readTextFile:async()=>{throw Error('Unexpected compatibility-file IO')},invoke:async(command,args)=>{
    calls.push({command,args});if(command==='monitor.start')return start();if(command==='monitor.stop')return stop();throw Error('Unexpected read/takeover IO:'+command);}});
  const state=()=>({desired:hub.desired(),applied:hub.applied(),pending:hub.pending(),attempts:hub.attempts(),timers:timers.size,calls:structuredClone(calls),pendingReads:hub.pendingReads()});
  const exhaust=async()=>{let budget=50;while(timers.size){assert(--budget>0,'retry work must remain bounded');const [key,timer]=[...timers].sort((a,b)=>a[1].at-b[1].at)[0];timers.delete(key);now=timer.at;timer.fn();await settle();}};
  return{hub,state,exhaust};
 }
 for(const [name,reply]of [['missing-ack',async()=>undefined],['refused-ack',async()=>({ok:false})],['timeout',async()=>{throw Error('fixture IPC timeout')} ]]){
  const test=make(reply),release=test.hub.acquire('receipt-negative',{top:true});await settle();
  assert.equal(test.hub.applied().top,false);assert.equal(test.hub.pending().top,true);assert.equal(test.hub.desired().top,true);
  const pending=test.state();await test.exhaust();assert(test.hub.attempts().start>1&&test.hub.attempts().start<=6,'bounded retry budget');
  release();await settle();assert.deepEqual(test.hub.desired(),{top:false,fps:false});assert.equal(test.hub.pending().top,false);assert.equal(test.hub.pendingReads(),0);
  trace.push({id:'product-receipt-'+name,input:{command:'monitor.start',reply:name},consumer:'createMonitorDataHub pump start/stop/retry',state:{pending,afterRelease:test.state()},feedback:'unconfirmed start stays pending, retries bounded, release emits idempotent stop',status:'PASS'});
 }
 {
  const test=make(async()=>({ok:true}),async()=>undefined),release=test.hub.acquire('stop-missing',{top:true});await settle();assert.equal(test.hub.applied().top,true);
  release();await settle();assert.equal(test.hub.desired().top,false);assert.equal(test.hub.applied().top,true);assert.equal(test.hub.pending().top,true);
  await test.exhaust();assert(test.hub.attempts().stop>1&&test.hub.attempts().stop<=6);assert.equal(test.hub.pending().top,true);
  trace.push({id:'product-receipt-stop-missing',input:{start:{ok:true},stop:undefined},consumer:'createMonitorDataHub pending stop/retry budget',state:test.state(),feedback:'missing stop acknowledgement remains unknown; no fabricated stopped receipt',status:'PASS'});
 }
 {
  let finish;const test=make(()=>new Promise(resolve=>finish=resolve));const release=test.hub.acquire('late-start',{top:true});await settle();release();finish({ok:true});await settle();
  assert.deepEqual(test.hub.desired(),{top:false,fps:false});assert.equal(test.hub.applied().top,false);assert.equal(test.hub.pending().top,false);
  assert.deepEqual(test.state().calls.map(item=>item.command),['monitor.start','monitor.stop']);
  trace.push({id:'product-receipt-late-start-after-release',input:{start:'deferred ok:true',release:'before start completes'},consumer:'createMonitorDataHub latest intent pump',state:test.state(),feedback:'late start acknowledgement cannot keep a released consumer running',status:'PASS'});
 }
}
