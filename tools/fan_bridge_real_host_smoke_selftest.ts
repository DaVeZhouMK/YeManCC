/** Real Fan bridge + real HTTP adapter + shipped FanHost smoke checks.
 * Only WebView HTTP forwarding, OS power facts and the process launcher are fixtures.
 * Host is explicitly mock-handshake: realBackend is null and no real-backend/write authorization; engine flags describe simulated control.
 */
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import net from 'node:net';
import {spawn, type ChildProcess} from 'node:child_process';
import {randomBytes, randomUUID} from 'node:crypto';
const root = process.cwd();
// Test the exported V2 lane, never the old source-input path.
const exportRoot = path.resolve(process.env.YEMANCC_FAN_TEST_EXPORT_ROOT || path.join(root, '../../Release'));
const payloadRoot = path.join(exportRoot, 'PowerControl', 'fan-host-v2');
const evidence = path.resolve(root, '../../Build/Validation/fan-bridge-real-host-smoke-20261006');
fs.mkdirSync(evidence, {recursive:true});
const transport: {path:string;method:string;status:number;generation:number}[]=[];
let power = {generation:0,phase:'ready',inputReady:true,resumeReady:true,hardwareWritesAllowed:true,hibernateAvailable:true};
let listener: (message:any)=>void = ()=>{};
const win = new EventTarget() as any;
win.chrome={webview:{addEventListener(_type:string, fn:any){listener=fn;},postMessage(message:any){
 if(typeof message.id!=='number') return;
 if(message.cmd==='http.request') {
  void (async()=>{try {
   const a=message.args;
   const response=await fetch(a.url,{method:a.method,headers:a.headers,body:a.body,signal:AbortSignal.timeout(a.timeoutMs)});
   const body=await response.text();
   transport.push({path:new URL(a.url).pathname,method:a.method,status:response.status,generation:power.generation});
   listener({data:{id:message.id,result:{status:response.status,body,headers:[...response.headers].map(([k,v])=>k+': '+v).join('\r\n')}}});
  }catch(e){listener({data:{id:message.id,error:e instanceof Error?e.message:String(e)}});}})();
 }else listener({data:{id:message.id,result:{ok:true}}});
}}};
(globalThis as any).window=win;
const {FanHostLifecycle,resolveFanHostConfig}=await import('../src/bridge/fanHost');
const {HttpFanApiAdapter}=await import('../src/bridge/fanApi');
const curve=[{tempC:0,dutyPercent:0},{tempC:40,dutyPercent:20},{tempC:70,dutyPercent:45},{tempC:100,dutyPercent:90}];
const pause=(n:number)=>new Promise(r=>setTimeout(r,n));
async function waitFor(test:()=>boolean,ms:number,label:string){const start=Date.now();while(!test()&&Date.now()-start<ms)await pause(50);assert(test(),label);}
async function freePort(){const s=net.createServer();await new Promise<void>(r=>s.listen(0,'127.0.0.1',r));const port=(s.address() as net.AddressInfo).port;await new Promise<void>(r=>s.close(()=>r()));return port;}
let child:ChildProcess|null=null;
let lifecycle:InstanceType<typeof FanHostLifecycle>|null=null;
const cases:string[]=[];
try {
 const port=await freePort(), token=randomBytes(32).toString('hex');
 const logRoot=path.join(evidence,'host-logs');fs.mkdirSync(logRoot,{recursive:true});
 fs.writeFileSync(path.join(logRoot,'fan-logging-enabled.flag'),'enabled');
 const tokenFile=path.join(evidence,'test-session.token');fs.writeFileSync(tokenFile,token);
 const exe=path.join(payloadRoot,'YeManFanHost.exe');
 const stdout=fs.openSync(path.join(evidence,'host.stdout.txt'),'w');
 const stderr=fs.openSync(path.join(evidence,'host.stderr.txt'),'w');
 child=spawn(exe,['--port',String(port),'--parent-pid',String(process.pid),'--session-token-file',tokenFile,'--protocol-version','2',
  '--mock-handshake','--mock-resume-rebuild-delay-ms','10500'],
  {windowsHide:true,stdio:['ignore',stdout,stderr],env:{...process.env,YEMANCC_SELFTEST_LOG_ROOT:logRoot}});
 fs.closeSync(stdout);fs.closeSync(stderr);
 const baseUrl=`http://127.0.0.1:${port}`;
 let healthy=false;
 for(let i=0;i<100&&!healthy;i++){try{healthy=(await fetch(baseUrl+'/health',{headers:{'X-YeMan-Fan-Session':token}})).ok;}catch{}if(!healthy)await pause(100);}
 assert(healthy,'real mock Host must expose authenticated health');
 const adapter=new HttpFanApiAdapter(baseUrl,token);
 const config=resolveFanHostConfig(path.join(exportRoot,'PowerControl'));
 config.baseUrl=baseUrl;config.sessionToken=token;config.sessionTokenPath=tokenFile;config.allowHardwareWrites=false;
 lifecycle=new FanHostLifecycle({enabled:true,config,adapter,heartbeatIntervalMs:5000,resumeWaitDeadlineMs:1500,
  launcher:{async start(){return {pid:child!.pid!,executable:exe};},async stop(){}},
  readNativePowerState:async()=>({...power}) as any,readNativeActivity:async()=>null,
  requestManualWake:async(g)=>{if(g!==power.generation||power.phase==='suspending')return {ok:false,generation:power.generation,phase:power.phase} as any;
   power={...power,phase:'ready',resumeReady:true,hardwareWritesAllowed:true};return {ok:true,generation:g,phase:'ready',admitted:true} as any;}});
 const gate=await lifecycle.start();assert(gate.allowed,'real Host handshake must be admissible in AI-only zero-write mode');
 await lifecycle.apply(curve);
 let state=await adapter.getState();assert(state.hardwareWritesEnabled===true,'real Host engine must retain simulated control');
 assert(state.hostMode==='mock-handshake'&&config.allowHardwareWrites===false,'real Host must remain in its explicit mock-handshake mode');
 fs.writeFileSync(path.join(evidence,'state-shape.json'),JSON.stringify({keys:Object.keys(state),hostMode:state.hostMode,hardwareCapable:state.hardwareCapable,mockHandshake:state.mockHandshake},null,2));
 cases.push('real Host authenticated cold Open/OpenEvents/acquire/enable in explicit mock-handshake mode');
 await lifecycle.apply(curve.map(n=>({...n,dutyPercent:Math.min(100,n.dutyPercent+5)})));
 state=await adapter.getState();assert(state.hardwareWritesEnabled===true,'next adjustment reaches the real mock engine');
 cases.push('next manual curve uses the actual HTTP adapter and Host engine');
 adapter.setSessionToken('0'.repeat(64));
 await adapter.getState().then(()=>assert.fail('wrong token must not succeed'),e=>assert(e.status===401,'real Host rejects wrong token with 401'));
 adapter.setSessionToken(token);
 await lifecycle.apply(curve);
 cases.push('real wrong-token 401 preserved; restoring correct credential allows a new manual write');
 await lifecycle.close().then(()=>assert.fail('null-backend mock must not invent a virtual HC Close receipt'),
  e=>assert(/HC 虚拟 Close 尚未确认返回/.test(e.message),'bridge honestly rejects the missing real HC Close receipt'));
 state=await adapter.getState();assert(state.hardwareWritesEnabled===false&&state.state==='Stopped'&&state.closeCalled===true,'actual mock engine stops control without supplying physical HC proof');
 assert(state.hcVirtualCloseReturned===false,'mock Close is not a real HC virtual call');
 assert(config.allowHardwareWrites===false,'no physical write authorization is supplied anywhere in this test');
 cases.push('null-backend mock Close stops its engine, but bridge honestly rejects absent HC virtual Close evidence');
 await adapter.shutdown();
 await waitFor(()=>child!.exitCode!==null,6000,'test Host exits without a leftover process');
 const report={passed:cases.length,cases,wakeCoverage:'not covered by this smoke: shipped null-backend mock F4 fails with system-pending-close-failed; real HC sleep release requires the ROG device window',
  scope:'real FanHostLifecycle + real HttpFanApiAdapter + shipped FanHost engine/process/HTTP; WebView forwarding, OS power and launcher are fixtures; explicit mock-handshake, no real-backend/write authorization; internal write/capability flags represent simulated control; no device proof',
  hostDllSha256:(await import('node:crypto')).createHash('sha256').update(fs.readFileSync(path.join(payloadRoot,'YeManFanHost.dll'))).digest('hex'),
  requests:transport,residualHostProcess:false};
 fs.writeFileSync(path.join(evidence,'results.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({passed:cases.length,cases,residualHostProcess:false},null,2));
 fs.rmSync(tokenFile,{force:true});
} catch(error) {
 fs.writeFileSync(path.join(evidence,'failure.json'),JSON.stringify({message:error instanceof Error?error.message:String(error),transport,state:lifecycle?.state,power},null,2));throw error;
} finally {
 if(lifecycle){
  (lifecycle as unknown as {stopHeartbeat():void}).stopHeartbeat();
  if(child&&child.exitCode===null&&child.signalCode===null)await lifecycle.close().catch(()=>{});
 }
 if(child&&child.exitCode===null&&child.signalCode===null){child.kill();await waitFor(()=>child!.exitCode!==null||child!.signalCode!==null,3000,'isolated test Host exits');}
}
