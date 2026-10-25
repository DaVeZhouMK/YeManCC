import assert from 'node:assert/strict';
import fs from 'node:fs';
const resultFile=process.argv[2];
if(!resultFile)throw Error('EXPLICIT_RESULT_FILE_REQUIRED');
const cases:any[]=[];const requests:any[]=[];const logs:any[]=[];
let listener:any;let mode='manual';const timers=new Map<number,{fn:()=>void,ms:number}>();let nextTimer=0;
const nativeSetTimeout=globalThis.setTimeout,nativeClearTimeout=globalThis.clearTimeout;
(globalThis as any).setTimeout=(fn:()=>void,ms:number)=>{const id=++nextTimer;timers.set(id,{fn,ms});return id;};
(globalThis as any).clearTimeout=(id:number)=>{timers.delete(id);};
(globalThis as any).window={chrome:{webview:{addEventListener(_event:string,callback:any){listener=callback;},postMessage(request:any){
 if(!['gpu.dedicatedMemory.get','gpu.dedicatedMemory.set'].includes(request.cmd))throw Error('UNREVIEWED_NATIVE_COMMAND');
 requests.push(structuredClone(request));if(mode==='throw')throw Error('fixture post failure');
}}},dispatchEvent(){return true;}};
const {parseDedicatedMemorySnapshot:parse,gpuDedicatedMemory,formatDedicatedCapacity,dedicatedMemoryStatus}=await import('../../../../src/bridge/gpuDedicatedMemory');
const ipc=await import('../../../../src/bridge/ipc');ipc.setLogSink((entry:any)=>logs.push(entry));
const valid=()=>({schema:1,ok:true,available:true,settingRoute:'known-model',reason:'fixture',provider:'rog-vram',adapterName:'offline GPU',driverVersion:'fixture32',pnpId:'PCI\\OFFLINE',luid:'offline-luid',capacityRead:true,dedicatedBytes:2*1024**3,selected:'mib:2048',options:[{id:'auto',raw:0,capacityMiB:null,removeOverride:false},{id:'mib:2048',raw:4,capacityMiB:2048,removeOverride:false},{id:'mib:4096',raw:8,capacityMiB:4096,removeOverride:false}],fence:'fixture-bound-fence',verification:'none'});
async function check(id:string,body:()=>any){const start=requests.length;try{const state=await body();cases.push({id,status:'PASS',state:state??null,requests:requests.slice(start)});}catch(error:any){cases.push({id,status:'FAIL',error:error.stack,requests:requests.slice(start)});}}
const reply=(result:any,id=requests.at(-1).id)=>listener({data:{id,result}});
const expire=()=>{assert.equal(timers.size,1);const item=[...timers][0];timers.delete(item[0]);item[1].fn();};
try{
await check('parse.valid-known-model',()=>{assert(parse(valid()));return {available:true};});
for(const [field,values]of Object.entries({provider:[['rog-vram'],['rog-apu'],{value:'rog-vram'},null,42],verification:[['verified'],['none'],['pending_restart'],{value:'none'},null,42]}))
 for(let index=0;index<values.length;index++)await check(`parse.reject-non-string-${field}-${index}`,()=>{const input={...valid(),[field]:values[index]};assert.equal(parse(input),null,'malformed enum must not activate controls');return {input,visible:false};});
for(const [field,value]of Object.entries({schema:2,ok:false,available:false,settingRoute:'read-only',capacityRead:false,dedicatedBytes:0,pnpId:'',luid:'',fence:'',options:[]}))
 await check(`parse.reject-${field}`,()=>{assert.equal(parse({...valid(),[field]:value}),null);return {field,accepted:false};});
await check('parse.reject-unconfirmed-intel-setting-route',()=>{assert.equal(parse({...valid(),provider:'intel-gmm'}),null);});
await check('parse.reject-duplicate-options',()=>{const input=valid();input.options.push(input.options[0]);assert.equal(parse(input),null);});
await check('parse.failed-write-retains-exact-live-capacity',()=>{const input={...valid(),ok:false,verification:'unconfirmed'};assert.equal(parse(input),null);assert.equal(parse(input,false)?.dedicatedBytes,2*1024**3);});
await check('parse.numeric-capacity-must-be-safe-and-positive',()=>{for(const value of [NaN,Infinity,-1,1.5,'2147483648',Number.MAX_SAFE_INTEGER+1])assert.equal(parse({...valid(),dedicatedBytes:value}),null);});
await check('format.capacity-and-independent-verification',()=>{assert.equal(formatDedicatedCapacity(2*1024**3),'2 GiB');assert.equal(formatDedicatedCapacity(NaN),'—');assert.equal(dedicatedMemoryStatus({...valid(),verification:'pending_restart'} as any),'请重启');assert.equal(dedicatedMemoryStatus({...valid(),verification:'not_effective'} as any),'未生效');});
await check('ipc.read-real-invoke-and-10000-budget',async()=>{const promise=gpuDedicatedMemory.read();assert.deepEqual(requests.at(-1).args,{});assert.equal([...timers.values()][0].ms,10000);reply(valid());const raw=await promise;assert(parse(raw));assert.equal(timers.size,0);return raw;});
await check('ipc.save-real-invoke-confirmed-fence-and-15000-budget',async()=>{const promise=gpuDedicatedMemory.save('fixture-fence','mib:4096');assert.deepEqual(requests.at(-1).args,{fence:'fixture-fence',option:'mib:4096',confirmed:true});assert.equal([...timers.values()][0].ms,15000);const raw={...valid(),selected:'mib:4096',verification:'pending_restart',pendingOption:'mib:4096'};reply(raw);assert.deepEqual(await promise,raw);assert.equal(timers.size,0);return raw;});
await check('ipc.read-timeout-cleans-pending-and-late-reply-is-ignored',async()=>{const promise=gpuDedicatedMemory.read();const request=requests.at(-1);const rejected=assert.rejects(promise,/timed out/);expire();await rejected;reply(valid(),request.id);assert.equal(timers.size,0);});
await check('ipc.write-unknown-result-is-not-replayed',async()=>{const before=requests.length;const promise=gpuDedicatedMemory.save('fixture-fence','mib:4096');const rejected=assert.rejects(promise,/timed out/);expire();await rejected;await Promise.resolve();assert.equal(requests.length,before+1);assert.equal(timers.size,0);return {writes:1,resultKnown:false};});
await check('ipc.wrong-id-cannot-complete-request',async()=>{const promise=gpuDedicatedMemory.read();let settled=false;void promise.then(()=>{settled=true;},()=>{settled=true;});reply(valid(),requests.at(-1).id+100);await Promise.resolve();assert.equal(settled,false);const rejected=assert.rejects(promise,/timed out/);expire();await rejected;});
await check('ipc.native-error-retained-without-auto-write-retry',async()=>{const before=requests.length;const promise=gpuDedicatedMemory.save('fixture-fence','mib:4096');listener({data:{id:requests.at(-1).id,error:'fixture firmware refused'}});await assert.rejects(promise,/firmware refused/);assert.equal(requests.length,before+1);assert.equal(timers.size,0);});
await check('ipc.recovery-rejects-inflight-request',async()=>{const promise=gpuDedicatedMemory.save('fixture-fence','mib:4096');listener({data:{event:'webview.recovering',data:{reason:'fixture recovery'}}});await assert.rejects(promise,/fixture recovery/);assert.equal(timers.size,0);});
await check('ipc.synchronous-send-failure-cleans-timeout',async()=>{mode='throw';try{await assert.rejects(gpuDedicatedMemory.read(),/fixture post failure/);}finally{mode='manual';}assert.equal(timers.size,0);});
await check('ipc.malformed-result-stays-hidden-after-real-transport',async()=>{const promise=gpuDedicatedMemory.read();reply({...valid(),provider:['rog-vram']});assert.equal(parse(await promise),null);});
if(process.argv[3]){
 const native=JSON.parse(fs.readFileSync(process.argv[3],'utf8'));
 await check('native.actual-public-reader-responses-enter-real-bridge-parser',()=>{assert(native.ok&&native.productionReader&&native.productionApply);let writable=0;for(const raw of Object.values(native.responses)as any[]){const expected=raw.ok&&raw.available&&raw.settingRoute==='known-model'&&['rog-apu','rog-vram'].includes(raw.provider);assert.equal(!!parse(raw),!!expected);if(expected)writable++;}assert(writable>0);return {nativeReplies:Object.keys(native.responses).length,writable};});
}
assert.equal(timers.size,0,'all owned synthetic timers must be released');
}finally{globalThis.setTimeout=nativeSetTimeout;globalThis.clearTimeout=nativeClearTimeout;}
const result={schemaVersion:1,status:cases.every(row=>row.status==='PASS')?'PASS':'FAIL',cases,summary:{cases:cases.length,passed:cases.filter(row=>row.status==='PASS').length,failed:cases.filter(row=>row.status!=='PASS').length,requests:requests.length},executedProductModules:['src/bridge/gpuDedicatedMemory.ts','src/bridge/ipc.ts'],trace:requests,feedback:logs,nativeIo:false,physicalHardwareValidated:false,operatingSystemSleepPerformed:false};
fs.writeFileSync(resultFile,JSON.stringify(result,null,2));console.log(JSON.stringify(result.summary));process.exitCode=result.status==='PASS'?0:2;
