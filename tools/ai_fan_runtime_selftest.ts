import fs from 'node:fs';
import fsp from 'node:fs/promises';
import path from 'node:path';
import http from 'node:http';
import { spawn, execFileSync } from 'node:child_process';
import { randomUUID, createHash } from 'node:crypto';
import assert from 'node:assert/strict';
const out=path.resolve(process.env.CPU_AI_FAN_TEST_OUT ?? 'Build/Validation/CPU-AI-Fan-Integration-20261004/runtime-'+Date.now());
if(fs.existsSync(out) && !(process.env.CPU_AI_FAN_CLIENT_TRANSPORT==='1' && fs.readdirSync(out).every(x=>x==='native-fixture')))throw Error('DO_NOT_OVERWRITE_RUNTIME_EVIDENCE');
fs.mkdirSync(out,{recursive:true});
const clientTransport=process.env.CPU_AI_FAN_CLIENT_TRANSPORT==='1';
if(clientTransport && (!process.execPath.startsWith(out+path.sep)||path.basename(process.execPath)!=='YeManCC.exe'))throw Error('CLIENT_TEST_REQUIRES_ISOLATED_NODE_FIXTURE');
const artifact=path.resolve(process.env.CPU_AI_FAN_HOST_ARTIFACT ?? 'FanLab/real-host/bin/Release/net10.0-windows10.0.19041.0/win-x64');
const hostDir=path.join(clientTransport?path.dirname(path.dirname(process.execPath)):out,'PowerControl','fan-host-v2');fs.mkdirSync(hostDir,{recursive:true});
for(const file of fs.readdirSync(artifact)){if(fs.statSync(path.join(artifact,file)).isFile())fs.copyFileSync(path.join(artifact,file),path.join(hostDir,file));}
const hostExe=path.join(hostDir,'YeManFanHost.exe');
const sessionId=clientTransport?process.env.CPU_AI_FAN_CLIENT_SESSION!:randomUUID();
if(!/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/.test(sessionId))throw Error('INVALID_CLIENT_FIXTURE_SESSION');
const stateDirectory=path.join(clientTransport?path.join(process.env.LOCALAPPDATA!,'YeManCC'):out,'ai-fan-sessions',sessionId,'fan-host');fs.mkdirSync(stateDirectory,{recursive:true});
const clientPath=path.resolve('tools/Send-YMCC-AI-Fan-Mock.ps1');
const queryPath=path.resolve('tools/ai_fan_client_selftest.ps1');
const clientOperations:Record<string,unknown>[]=[];
const queryIdentity=(pid:number)=>JSON.parse(execFileSync('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-File',queryPath,'-Client',clientPath,'-OutDir',path.join(out,'identity-queries'),'-QueryPid',String(pid)],{windowsHide:true,encoding:'utf8'}));
const rootCreationTimeUtc=clientTransport?queryIdentity(process.pid).creationTimeUtc:'';
const session={schemaVersion:1,enabled:true,mode:'mock-handshake',sessionId,parentPid:process.pid,stateDirectory,requestPath:path.join(stateDirectory,'ai-request.json'),responsePath:path.join(stateDirectory,'ai-response.json'),hostPid:0,hostCreationTime100ns:'0'};
let child: ReturnType<typeof spawn>|null=null;let childExited=true;let hostStarts=0;let identityWrites=0;let forcedKills=0;let high=0;const seen=new Set<string>();
let fixtureCreationTime = '0'; const runtimeErrors: string[] = []; const onError = (e: unknown) => { const message = String((e as Error)?.message ?? e).replace(/(token|leaseId|confirm)\s*[:=]\s*[^\s,]+/gi, '$1=<redacted>'); runtimeErrors.push(message); console.error('service-error: '+message); };
let reads=0,writes=0,claims=0,checks=0;const requests:Record<string,unknown>[]=[];const steps:Record<string,unknown>[]=[];
const stdout=fs.createWriteStream(path.join(out,'host-stdout.log'));const stderr=fs.createWriteStream(path.join(out,'host-stderr.log'));
let listener:((e:{data:unknown})=>void)|null=null;
const win=new EventTarget() as EventTarget&Record<string,any>;
win.setTimeout=setTimeout;win.clearTimeout=clearTimeout;
function localPath(p:string){const resolved=path.resolve(p);if(!resolved.startsWith(out+path.sep)&&resolved!==stateDirectory&&!resolved.startsWith(stateDirectory+path.sep))throw Error('FIXTURE_PATH_OUTSIDE_WORKSPACE');return resolved;}
function ownedPid(){return child&&!childExited?(child.pid??0):0;}
function getSession(){return {...session,hostPid:ownedPid(),hostCreationTime100ns:ownedPid()?fixtureCreationTime:'0'};}
async function invoke(cmd:string,a:Record<string,any>){
 switch(cmd){
 case 'app.aiFanMockSession':return getSession();
 case 'app.aiFanMockAcceptRequest':claims++;if(a.sessionId!==sessionId||a.parentPid!==process.pid||a.sequence<=high||seen.has(a.requestId))return false;high=a.sequence;seen.add(a.requestId);return true;
 case 'app.pid':return process.pid;
 case 'app.fanStateDir':return stateDirectory;
 case 'app.fanActivity':return {schemaVersion:1,active:false,recoverable:false,revision:0,curve:[]};
 case 'fs.exists':return fs.existsSync(localPath(a.path));
 case 'fs.mkdir':await fsp.mkdir(localPath(a.path),{recursive:true});return true;
 case 'fs.readTextFile':{reads++;const b=await fsp.readFile(localPath(a.path));if(b.length>a.maxBytes)throw Error('TOO_LARGE');return b.toString('utf8');}
 case 'fs.writeTextFileAtomic':{writes++;const p=localPath(a.path);await fsp.mkdir(path.dirname(p),{recursive:true});await fsp.writeFile(p+'.tmp',a.content);await fsp.rename(p+'.tmp',p);return true;}
 case 'proc.findExact':assert.equal(path.resolve(a.path).toLowerCase(),hostExe.toLowerCase());return {found:ownedPid()>0,pid:ownedPid()};
 case 'process.identity':return a.pid===ownedPid()&&ownedPid()>0?{valid:true,pid:ownedPid(),processCreated:fixtureCreationTime,path:hostExe}:{valid:false,pid:a.pid};
 case 'process.terminateExact':{
  const matches=a.pid===ownedPid()&&a.processCreated===fixtureCreationTime&&path.resolve(a.path).toLowerCase()===hostExe.toLowerCase();
  if(!ownedPid())return {ok:true,matched:false,exited:true,reason:'already-exited'};
  if(!matches)return {ok:false,matched:false,exited:false,reason:'identity-mismatch'};
  forcedKills++;child!.kill();return {ok:true,matched:true,exited:false,reason:'termination-requested'};
 }
 case 'process.terminateTree':if(!ownedPid())return {ok:true,attempted:0,terminated:0};assert.equal(a.pid,ownedPid());forcedKills++;child!.kill();return {ok:true,attempted:1,terminated:1};
 case 'shell.hidden':{
  assert.equal(path.resolve(a.program).toLowerCase(),hostExe.toLowerCase());assert.equal(ownedPid(),0);assert.ok(a.args.includes('--mock-handshake')&&a.args.includes('--mock-zero-hardware-evidence'));assert.ok(!a.args.some((x:string)=>/real-backend|allow-hardware|authorization|confirm/.test(x)));
  hostStarts++;fixtureCreationTime=String(BigInt(Date.now()+11644473600000)*10000n);child=spawn(hostExe,a.args,{windowsHide:true,stdio:['ignore','pipe','pipe']});childExited=false;child.stdout!.pipe(stdout,{end:false});child.stderr!.pipe(stderr,{end:false});child.once('exit',()=>{childExited=true});child.once('error',()=>{childExited=true});// Creation-time identity is a production admission gate, not a guessed fixture timestamp.
  fixtureCreationTime=queryIdentity(child.pid!).creationTime100ns;return {ok:true,pid:child.pid};
 }
 case 'http.request':{
  const url=new URL(a.url);assert.equal(url.hostname,'127.0.0.1');assert.equal(url.port,'8765');const method=a.method??'GET';
  if(method!=='GET'&&method!=='HEAD'&&!ownedPid())throw Error('AI_FAN_OWNED_MOCK_REQUIRED');
  const start=performance.now();
  return await new Promise((resolve,reject)=>{const req=http.request(url,{method,headers:a.headers??{},timeout:a.timeoutMs??5000},res=>{const chunks:Buffer[]=[];res.on('data',x=>chunks.push(x));res.on('end',()=>{requests.push({method,path:url.pathname,status:res.statusCode,durationMs:performance.now()-start});resolve({status:res.statusCode,body:Buffer.concat(chunks).toString('utf8'),headers:Object.entries(res.headers).map(([k,v])=>k+': '+v).join('\r\n')});});});req.on('timeout',()=>req.destroy(Error('timeout')));req.on('error',(e:NodeJS.ErrnoException)=>reject(Error(e.code==='ECONNREFUSED'?'HTTP request failed (WinHTTP 12029, path='+url.pathname+')':'HTTP request failed unknown')));if(a.body)req.write(a.body);req.end();});
 }
 case 'fanLog.write':case 'fanLog.record':return true;
 default:throw Error('unknown: '+cmd);
 }
}
win.chrome={webview:{addEventListener(_type:string,fn:typeof listener){listener=fn},postMessage(msg:any){void invoke(msg.cmd,msg.args??{}).then(result=>listener?.({data:{id:msg.id,result}}),error=>listener?.({data:{id:msg.id,error:String(error.message)}}))}}};
(globalThis as any).window=win;
const {FanHostLifecycle,resolveFanHostConfig}=await import('../src/bridge/fanHost');
const {startAiFanService}=await import('../src/bridge/aiFanService');
const {DEFAULT_FAN_PRESET_CURVES}=await import('../src/bridge/fanFeature');
const wait=async(fn:()=>boolean,ms=15000)=>{const end=Date.now()+ms;while(!fn()){if(Date.now()>end)throw Error('WAIT_TIMEOUT');await new Promise(r=>setTimeout(r,40));}};
let sequence=0;let stop:(()=>void)|null=null;let life:InstanceType<typeof FanHostLifecycle>|null=null;let occupied:http.Server|null=null;
function check(ok:unknown,msg:string){checks++;assert.ok(ok,msg);}
async function request(action:string,preset?:string){
 if(clientTransport){
  const clientOut=path.join(out,'client-actions',String(clientOperations.length+1)+'-'+action);
  const args=['-NoProfile','-ExecutionPolicy','Bypass','-File',clientPath,'-Action',action,'-ExePath',process.execPath,'-ExpectedSha256',createHash('sha256').update(fs.readFileSync(process.execPath)).digest('hex'),'-ParentPid',String(process.pid),'-ParentCreationTimeUtc',rootCreationTimeUtc,'-SessionId',sessionId,'-OutDir',clientOut,'-TimeoutSeconds','30'];
  if(preset)args.push('-Preset',preset);
  const code=await new Promise<number|null>((resolve,reject)=>{const p=spawn('powershell.exe',args,{windowsHide:true,stdio:['ignore','pipe','pipe']});let output='';p.stdout.on('data',x=>output+=x);p.stderr.on('data',x=>output+=x);p.once('error',reject);p.once('exit',c=>{fs.mkdirSync(clientOut,{recursive:true});fs.writeFileSync(path.join(clientOut,'caller-output.log'),output);resolve(c)});});
  const result=JSON.parse(fs.readFileSync(path.join(clientOut,'client-result.json'),'utf8'));
  clientOperations.push({action,exitCode:code,clientResult:result});
  const reply=JSON.parse(fs.readFileSync(session.responsePath,'utf8'));
  check(reply.requestId===result.requestId,'external client response belongs to its request');
  check(code===(reply.ok===true?0:7),'external client exitCode matches actual outcome');
  if(reply.ok===true)check(result.workerIsElevated===true&&result.workerIntegrity==='high','external client auto-elevated worker');
  steps.push(reply);return reply;
 }
 sequence++;const requestId=randomUUID();await fsp.writeFile(session.requestPath,JSON.stringify({schemaVersion:1,sessionId,parentPid:process.pid,requestId,sequence,action,...(preset?{preset}:{})}));await wait(()=>{try{return JSON.parse(fs.readFileSync(session.responsePath,'utf8')).requestId===requestId}catch{return false}},30000);const reply=JSON.parse(fs.readFileSync(session.responsePath,'utf8'));steps.push(reply);check(!JSON.stringify(reply).includes('leaseId')&&!JSON.stringify(reply).includes('confirmationToken'),'no response secrets');return reply;}
let status='FAIL';let failure:string|null=null;
try{
 let externalPosts=0;
 occupied=http.createServer((req,res)=>{if(req.method!=='GET')externalPosts++;res.writeHead(401);res.end('{"ok":false}');});
 await new Promise<void>((resolve,reject)=>{occupied!.once('error',reject);occupied!.listen(8765,'127.0.0.1',()=>resolve())});
 life=new FanHostLifecycle({enabled:true,config:resolveFanHostConfig(path.dirname(hostDir)),savedIdentity:{manufacturer:'ASUS',model:'real-rog'},onDeviceIdentity:()=>identityWrites++,readNativeActivity:async()=>null});
 stop=await startAiFanService({lifecycle:life,preset:n=>DEFAULT_FAN_PRESET_CURVES[n],onError});
 const refused=await request('on','balanced');check(refused.ok===false&&hostStarts===0&&externalPosts===0,'occupied external listener never receives control and no second host');stop();stop=null;
 await new Promise<void>(resolve=>occupied!.close(()=>resolve()));occupied=null;
 life=new FanHostLifecycle({enabled:true,config:resolveFanHostConfig(path.dirname(hostDir)),savedIdentity:{manufacturer:'ASUS',model:'real-rog'},onDeviceIdentity:()=>identityWrites++,readNativeActivity:async()=>null});
 stop=await startAiFanService({lifecycle:life,preset:n=>DEFAULT_FAN_PRESET_CURVES[n],onError});
 const probe=await request('probe');check(probe.ok===true&&probe.evidence.runtimeVerified===true&&probe.evidence.mockControlEnabled===false,'actual mock probe no simulated control');
 const on=await request('on','balanced');check(on.ok===true&&on.evidence.mockControlEnabled===true&&on.evidence.leaseHeld===true,'actual mock control active with lease');
 check(on.evidence.hardwareWritesEnabled===false&&on.evidence.hardwareWritesObserved===false&&on.evidence.mockControlSequence>=1,'physical vs simulated evidence split');
 for(let i=0;i<4;i++){const reply=await request('on',i%2?'soft':'aggressive');check(reply.ok===true&&reply.evidence.mockControlEnabled===true&&reply.evidence.hardwareWritesObserved===false,'real host curve replay '+i);}
 // Repeated AI handshake must neither expose a stale safe-build snapshot nor
 // revoke logical mock control; this uses the actual C# endpoint, not a stub.
 const token=fs.readFileSync(path.join(stateDirectory,'YeManFanHost.session'),'utf8').trim();
 const repeatHandshake=await invoke('http.request',{url:'http://127.0.0.1:8765/api/handshake',method:'POST',headers:{'X-YeMan-Fan-Session':token},body:'{}',timeoutMs:2000}) as any;
 check(repeatHandshake.status===200,'actual repeated AI handshake accepted');
 const afterHandshake=await life.getState();
 check(afterHandshake.hostMode==='mock-handshake'&&afterHandshake.mockControlEnabled===true&&afterHandshake.hardwareWritesObserved===false,'handshake publishes readback without revoking simulated control');
 const oldStarts=hostStarts;
 await new Promise(r=>setTimeout(r,650));check(hostStarts===oldStarts,'no duplicate spawn');
 const sameBefore=requests.filter(r=>r.path==='/api/enable').length;stop();stop=null;stop=await startAiFanService({lifecycle:life,preset:n=>DEFAULT_FAN_PRESET_CURVES[n],onError});await new Promise(r=>setTimeout(r,650));check(requests.filter(r=>r.path==='/api/enable').length===sameBefore,'service recreation cannot replay accepted request');
 const off=await request('off');check(off.ok===true&&off.evidence.mockControlEnabled===false&&off.evidence.hardwareWritesEnabled===false,'actual disable zero hardware');
 const reon=await request('on','balanced');check(reon.ok===true&&reon.evidence.mockControlEnabled===true,'actual reenable');
 const close=await request('close');await wait(()=>!ownedPid());check(close.ok===true&&close.evidence.hostPid===0&&close.evidence.runtimeVerified===false&&close.evidence.closeReceipt.mockCloseCompleted===true&&close.evidence.closeReceipt.hardwareWritesObserved===false&&close.evidence.childExited===true,'actual lifecycle close receipt and exited owned child');
 const newProbe=await request('probe');check(newProbe.ok===true&&hostStarts===2,'new mock lifecycle can start after close');
 const finalClose=await request('close');await wait(()=>!ownedPid());check(finalClose.ok===true,'second close complete');
 check(identityWrites===0,'mock identity never persists into real settings');
 check(forcedKills===0,'host clean shutdown, no terminate fallback');
 const logs=path.join(stateDirectory,'logs');check(fs.existsSync(logs),'host logs in isolated session');
 status='PASS';
}catch(e){failure=(e as Error).message;console.error('runtime selftest failed: '+failure);}
finally{
 stop?.();if(occupied)await new Promise<void>(resolve=>occupied!.close(()=>resolve()));
 if(ownedPid()){
  try{await life?.close();}catch{}
  try{await wait(()=>!ownedPid(),5000);}catch{if(child){forcedKills++;child.kill();await wait(()=>!ownedPid(),5000).catch(()=>{});}}
 }
 stdout.end();stderr.end();
 const result={status,capturedAtUtc:new Date().toISOString(),scope:'real NativeFanHostLauncher/FanHostLifecycle/file service + actual C# mock Host; native IPC transport is a constrained Node fixture, NOT full YMCC/WebView2 integrated CPU or ROG acceptance',sessionId,hostExe,hostExeSha256:createHash('sha256').update(fs.readFileSync(hostExe)).digest('hex'),hostDllSha256:createHash('sha256').update(fs.readFileSync(path.join(hostDir,'YeManFanHost.dll'))).digest('hex'),checks,hostStarts,forcedKills,identityWrites,childExited,reads,writes,claims,requests,steps,runtimeErrors,nativeIdentityIsFixture:true,clientTransport,clientOperations,failure};
 fs.writeFileSync(path.join(out,'runtime-results.json'),JSON.stringify(result,null,2));console.log(JSON.stringify({status,checks,hostStarts,forcedKills,childExited,evidence:path.join(out,'runtime-results.json'),failure},null,2));
}
if(status!=='PASS')process.exitCode=1;