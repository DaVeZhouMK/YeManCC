import assert from 'node:assert/strict';
const pending: any[]=[]; let listener:any; let mode='success';
(globalThis as any).window={chrome:{webview:{addEventListener(_e:string,fn:any){listener=fn;},postMessage(req:any){pending.push(req);
 if(mode==='timeout') {queueMicrotask(()=>listener({data:{id:req.id,error:'HTTP request deadline exceeded (phase=headers)'}}));return;}
 const denied=mode==='denied';queueMicrotask(()=>listener({data:{id:req.id,result:{status:denied?401:200,body:JSON.stringify(denied?{ok:false,error:{code:'API_SESSION_REQUIRED',message:'denied'}}:{ok:true,state:{state:'Ready'}}),headers:''}}}));}}},dispatchEvent(){}};
// The production HTTP path now imports the UI lifecycle. Supply inert DOM
// event boundaries before importing it; no browser/native transport is opened.
const documentEvents = new EventTarget();
Object.assign(documentEvents, { visibilityState: 'visible', hidden: false,
 createElement: () => ({ content: {}, style: {} }), querySelector: () => null, getElementById: () => null,
});
(globalThis as any).document = documentEvents;
const windowEvents = new EventTarget();
Object.assign((globalThis as any).window, {
 addEventListener: windowEvents.addEventListener.bind(windowEvents),
 removeEventListener: windowEvents.removeEventListener.bind(windowEvents),
 dispatchEvent: windowEvents.dispatchEvent.bind(windowEvents),
 setTimeout, clearTimeout, setInterval, clearInterval,
});
const {http}=await import('../src/bridge/api');
const {HttpFanApiAdapter,FanApiError}=await import('../src/bridge/fanApi');
const {FanHostLifecycle}=await import('../src/bridge/fanHost');
const {NoIoHost}=await import('./fixtures/fan_no_io_host');
let checks=0;
for(const supplied of [undefined,NaN,Infinity,-20,0,250,5000,120001]){
 await http.request('http://127.0.0.1:8765/api/state',{timeoutMs:supplied});
 const actual=pending.at(-1).args.timeoutMs;
 assert.equal(actual,!Number.isFinite(supplied??30000)?30000:Math.max(250,Math.min(120000,Math.round(supplied??30000))));checks++;
}
const adapter=new HttpFanApiAdapter('http://127.0.0.1:8765','fixture-token');
for(const action of ['open','openEvents']){await (adapter as any)[action]();assert.equal(pending.at(-1).args.timeoutMs,16000);checks++;}
mode='denied';await assert.rejects(adapter.enable([{tempC:0,dutyPercent:20}], 'lease'),e=>e instanceof FanApiError&&e.errorCode==='API_SESSION_REQUIRED'&&e.status===401);checks++;
mode='timeout';await assert.rejects(adapter.enable([{tempC:0,dutyPercent:20}], 'lease'),e=>e instanceof FanApiError&&e.errorCode==='FAN_HTTP_RESULT_UNKNOWN');checks++;
await assert.rejects(adapter.getState(),e=>e instanceof Error&&e.message.includes('deadline exceeded')&&!(e instanceof FanApiError));checks++;
const host=new NoIoHost();let count=0;host.enable=async()=>{count++;throw new FanApiError('unknown POST result',0,'FAN_HTTP_RESULT_UNKNOWN');};
const life=new FanHostLifecycle({enabled:true,adapter:host,heartbeatIntervalMs:0,launcher:{start:async()=>({pid:947,executable:'fixture'}),stop:async()=>{}},readNativePowerState:async()=>null});
await life.start();await assert.rejects(life.apply([{tempC:0,dutyPercent:20},{tempC:40,dutyPercent:40},{tempC:70,dutyPercent:60},{tempC:100,dutyPercent:90}]),e=>e instanceof FanApiError&&e.errorCode==='FAN_HTTP_RESULT_UNKNOWN');assert.equal(count,1,'uncertain POST cannot be blindly repeated');checks++;
await life.close();
console.log(JSON.stringify({ok:true,checks,scope:'production IPC/http/FanApi/lifecycle with native boundary fixture; no device writes'}));