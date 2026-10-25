/** Actual Fan adapter + shared HTTP wrapper + IPC; native replies are inert fixtures. */
import assert from 'node:assert/strict';
import fs from 'node:fs';
const listeners:Array<(event:{data:unknown})=>void>=[];
const requests:Array<{url:string;timeoutMs:number;method:string;headers?:Record<string,string>}>=[];
const state={state:'Ready',hardwareWrites:false,hardwareWritesEnabled:true,openCalled:true,openEventsCalled:true,leaseGeneration:1};
const webview={addEventListener(_type:string,listener:(event:{data:unknown})=>void){listeners.push(listener);},postMessage(raw:unknown){
 const msg=raw as {id:number;cmd:string;args:typeof requests[number]};
 if(msg.cmd!=='http.request')throw new Error('unexpected IPC '+msg.cmd);
 requests.push(msg.args);
 const handshake=new URL(msg.args.url).pathname==='/api/handshake';
 const reply=handshake?{ok:true,supported:true,protocolVersion:2,hardwareCapable:true,fanRouteWriteReady:true}:{ok:true,state};
 queueMicrotask(()=>{for(const listener of listeners)listener({data:{id:msg.id,result:{status:200,headers:'',body:JSON.stringify(reply),bodyComplete:true}}});});
}};
Object.assign(globalThis,{window:{chrome:{webview},dispatchEvent(){return true;},addEventListener(){},removeEventListener(){}}});
const {HttpFanApiAdapter}=await import('../src/bridge/fanApi');
const {rejectAllPending}=await import('../src/bridge/ipc');
const adapter=new HttpFanApiAdapter('http://127.0.0.1:8765','fixture-budget-token');
const results:Array<{name:string;passed:boolean;expected:number;actual:number}>=[];
async function check(name:string,call:()=>Promise<unknown>,expected:number){
 const before=requests.length;await call();assert.equal(requests.length,before+1,'one IPC request per API call');
 const req=requests.at(-1)!;assert.equal(req.headers?.['X-YeMan-Fan-Session'],'fixture-budget-token');
 const passed=req.timeoutMs===expected;results.push({name,passed,expected,actual:req.timeoutMs});console.log((passed?'PASS ':'FAIL ')+name+' nativeBudget='+req.timeoutMs);
}
try{
 await check('300ms remaining read budget reaches native unchanged',()=>adapter.getState(300),300);
 await check('499ms remaining read budget reaches native unchanged',()=>adapter.getState(499),499);
 await check('250ms transport minimum remains supported',()=>adapter.getState(250),250);
 await check('sub-250ms override preserves existing shared transport floor',()=>adapter.getState(10),250);
 await check('fractional read override is conservatively floored',()=>adapter.getState(300.9),300);
 await check('missing read override preserves 5s default',()=>adapter.getState(),5000);
 await check('zero override preserves default',()=>adapter.getState(0),5000);
 await check('nonfinite override preserves default',()=>adapter.getState(Number.NaN),5000);
 await check('oversized request preserves existing 120s transport cap',()=>adapter.getState(999999),120000);
 await check('handshake can share a bounded 300ms probe budget',()=>adapter.handshake(300),300);
 await check('Open default remains 16s',()=>adapter.open(),16000);
 await check('OpenEvents default remains 16s',()=>adapter.openEvents(),16000);
 await check('Resume default remains 16s',()=>adapter.resume({generation:1,source:'fixture'}),16000);
 await check('Close default remains 45s',()=>adapter.close(),45000);
 await check('Restore default remains 10s',()=>adapter.restoreOem(),10000);
 const report={ok:results.every(r=>r.passed),checks:results.length,hardwareCalls:0,scope:'real HttpFanApiAdapter -> real shared http.request -> real IPC serialization; fake native reply only; no live Host/network/hardware or timing benchmark',results};
 if(process.argv[2])fs.writeFileSync(process.argv[2],JSON.stringify(report,null,2));console.log(JSON.stringify(report));process.exitCode=report.ok?0:1;
}finally{rejectAllPending('budget selftest completed');}
