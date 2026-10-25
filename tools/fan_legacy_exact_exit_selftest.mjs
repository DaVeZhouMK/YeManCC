/** Real native launcher and IPC with virtual processes/clock; never starts a real Host. */
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {webcrypto,createHash} from 'node:crypto';
import {build} from 'esbuild';
const root=path.resolve(import.meta.dirname,'..'),out=process.argv[2];if(!out||fs.existsSync(out))throw Error('Use a new evidence directory');fs.mkdirSync(out,{recursive:true});
const originalBridge=process.argv[3],source=fs.readFileSync(originalBridge||path.join(root,'src/bridge/fanHost.ts'),'utf8');let injected=0;
const built=await build({stdin:{contents:"export {NativeFanHostLauncher,resolveFanHostConfig} from './src/bridge/fanHost';export {setLogSink} from './src/bridge/ipc';",resolveDir:root,sourcefile:'legacy-exit-inert-entry.ts'},bundle:true,write:false,format:'iife',globalName:'FanLegacy',platform:'browser',target:'es2022',define:{'import.meta.env':'{}'},plugins:[{name:'exact-frozen-bridge-for-comparison',setup(b){b.onLoad({filter:/fanHost\.ts$/},a=>{if(!a.path.replaceAll('\\','/').endsWith('/src/bridge/fanHost.ts'))return;injected++;return{contents:source,loader:'ts',resolveDir:path.dirname(a.path)};});}}]});assert.equal(injected,1,'comparison must load exactly one actual bridge');
const EXE='C:\inert-fan\YeManFanHost.exe',PID=128801,CREATED='133001111111111111',NEWPID=128802,NEWCREATED='133001222222222222';
const stopped={state:'Stopped',powerState:'On',hardwareWrites:false,hardwareWritesEnabled:false,hardwareWritesObserved:false,unknownState:false,openCalled:false,openEventsCalled:false,closeCalled:true,hcCloseCleanupPending:false,hcVirtualCloseReturned:true,hcDeviceManagerStopCompleted:true,oemRestoreConfirmed:false,oemPhysicalOwnershipConfirmed:false};
function fixture(options={}){
 let now=0,id=0,listener,oldLive=true,oldListening=options.noListener?false:true,newLive=false,shutdown=false;const timers=new Map(),calls=[],files=new Map(),deployment=[],starts=[];let maxLive=1;
 const timer=(fn,ms)=>{const key=++id;timers.set(key,{fn,at:now+Math.max(0,Number(ms))});return key;};const clear=key=>timers.delete(key);const win=new EventTarget();win.setTimeout=timer;win.clearTimeout=clear;
 const reply=(m,result,error)=>listener?.({data:error?{id:m.id,error}:{id:m.id,result}});
 const respond=m=>{
  const {cmd,args={}}=m,route=cmd==='http.request'?new URL(args.url).pathname:null;calls.push({cmd,route,method:args.method,pid:args.pid,oldLive,newLive,at:now});let result;
  if(cmd==='app.aiFanMockSession')result={schemaVersion:1,enabled:false};
  else if(cmd==='app.fanStateDir'||cmd==='app.dataDir')result='C:\inert-state';
  else if(cmd==='app.pid')result=17;
  else if(cmd==='app.fanActivity'||cmd==='app.fanLifecycleEvidence')result=null;
  else if(cmd==='fs.exists')result=String(args.path).toLowerCase()===EXE.toLowerCase();
  else if(cmd==='fs.mkdir')result=true;
  else if(cmd==='fs.readTextFile')result=files.get(args.path)||'';
  else if(cmd==='fs.readTextRange'){const value=files.get(args.path)||'';result={content:value,offset:0,size:Buffer.byteLength(value)};}
  else if(cmd==='fs.writeTextFileAtomic'){files.set(args.path,args.content);result=true;}
  else if(cmd==='proc.findExact')result={found:oldLive||newLive,pid:oldLive?PID:newLive?NEWPID:0};
  else if(cmd==='shell.run')result={exitCode:0,stdout:oldListening&&oldLive?' TCP 127.0.0.1:8765 0.0.0.0:0 LISTENING '+PID:'',stderr:''};
  else if(cmd==='process.identity'){
   if(args.pid===NEWPID&&newLive)result={valid:true,pid:NEWPID,path:EXE,processCreated:NEWCREATED};
   else if(args.pid===PID&&options.pidReused&&shutdown)result={valid:true,pid:PID,path:'C:\inert-other\Other.exe',processCreated:'133009999999999999'};
   else if(args.pid!==PID||!oldLive)result={valid:false,pid:args.pid};
   else if(options.identityUnreadable||shutdown&&options.identityUnreadableAfterShutdown)result={valid:false,pid:PID};
   else result={valid:true,pid:PID,path:options.foreign?'C:\inert-other\Other.exe':EXE,processCreated:options.created??CREATED};
  }
  else if(cmd==='process.terminateExact'){
   assert.equal(args.pid,PID);assert.equal(args.path,EXE);assert.equal(args.processCreated,CREATED,'must retire only the captured old instance');
   if(options.terminate==='timeout')return;
   if(options.terminate==='unconfirmed')result={ok:false,matched:true,exited:false,reason:'still-running'};
   else if(options.terminate==='identity-mismatch')result={ok:false,matched:false,exited:false,reason:'identity-mismatch'};
   else{const wasLive=oldLive;oldLive=false;oldListening=false;result={ok:true,matched:wasLive,exited:true,reason:wasLive?'terminated':'not-found'};}
  }
  else if(cmd==='shell.hidden'){assert.equal(args.program,EXE);starts.push({oldLive,at:now});newLive=true;maxLive=Math.max(maxLive,(oldLive?1:0)+1);result={ok:true,pid:NEWPID};}
  else if(cmd==='http.request'){
   if(route==='/api/close'){result={status:options.closeStatus??200,body:JSON.stringify({ok:true,state:{...stopped,...options.closePatch}}),headers:''};}
   else if(route==='/api/shutdown'){const status=options.shutdownStatus??200;if(status===200||status===404){shutdown=true;oldListening=false;if(options.exitsOnShutdown||options.pidReused)oldLive=false;}result={status,body:'{}',headers:''};}
   else if(route==='/health'){if(!oldListening&&!newLive){reply(m,null,'HTTP request failed (WinHTTP 12029, phase=send)');return;}result={status:200,body:JSON.stringify({host:'YeManFanHost',protocolVersion:2}),headers:''};}
   else if(route==='/api/state'){if(!oldListening&&!newLive){reply(m,null,'HTTP request failed (WinHTTP 12029, phase=send)');return;}result={status:200,body:JSON.stringify({ok:true,state:stopped}),headers:''};}
   else throw Error('unexpected HTTP route '+route);
  }
  else throw Error('unexpected native command '+cmd);
  reply(m,result);
 };
 win.chrome={webview:{postMessage:respond,addEventListener:(_name,fn)=>listener=fn}};
 const context=vm.createContext({window:win,document:Object.assign(new EventTarget(),{hidden:false,visibilityState:'visible',createElement:()=>({style:{},content:{}}),querySelector:()=>null}),console,Event,EventTarget,CustomEvent:class extends Event{constructor(type,init){super(type);this.detail=init?.detail}},setTimeout:timer,clearTimeout:clear,setInterval:timer,clearInterval:clear,TextEncoder,TextDecoder,URL,crypto:webcrypto,performance:{now:()=>now},structuredClone,queueMicrotask});vm.runInContext(built.outputFiles[0].text,context,{timeout:5000});const api=context.FanLegacy;api.setLogSink(()=>{});
 const launcher=new api.NativeFanHostLauncher(),config=api.resolveFanHostConfig('C:\inert-root');config.hostExecutable=EXE;config.hostDirectory='C:\inert-fan';config.sessionToken='';
 launcher.ensureFanHostDeployment=async()=>{deployment.push({oldLive,at:now});}; // Records entry only; no real installer or file operations.
 async function flush(){for(let n=0;n<40;n++)await Promise.resolve();await new Promise(setImmediate);}
 async function finish(p){const state={status:'pending',value:null,error:null};void p.then(v=>{state.status='fulfilled';state.value=v;},e=>{state.status='rejected';state.error=String(e)});for(let n=0;n<1000&&state.status==='pending';n++){await flush();if(state.status!=='pending')break;const next=[...timers.entries()].sort((a,b)=>a[1].at-b[1].at)[0];if(next){assert(next[1].at<=30000,'software episode exceeded test ceiling');now=next[1].at;timers.delete(next[0]);next[1].fn();}}assert.notEqual(state.status,'pending','test did not reach a terminal observation');return state;}
 return{launcher,config,calls,timers,deployment,starts,finish,launch:()=>finish(launcher.start(config)),probe:()=>finish(launcher.recoverLegacyUnauthenticatedHost(config)),counts:()=>({oldLive,newLive,maxLive,deployment:deployment.length,unsafeDeployment:deployment.filter(x=>x.oldLive).length,launches:starts.length,unsafeLaunches:starts.filter(x=>x.oldLive).length,close:calls.filter(c=>c.route==='/api/close').length,shutdown:calls.filter(c=>c.route==='/api/shutdown').length,terminate:calls.filter(c=>c.cmd==='process.terminateExact').length})};
}
const cases=[];async function check(name,options,work){const f=fixture(options);try{const evidence=await work(f);cases.push({name,ok:true,evidence,counts:f.counts()});}catch(error){cases.push({name,ok:false,error:String(error),counts:f.counts(),calls:f.calls});}}
await check('public legacy migration cannot enter deployment until the exact old instance exits',{},async f=>{const p=await f.launch();assert.equal(f.counts().unsafeDeployment,0,'listener disappeared while old Host still lived, but payload deployment was entered');assert.equal(p.status,'fulfilled',p.error??'migration failed');assert.equal(f.counts().terminate,1);assert.equal(f.counts().launches,1);assert.equal(f.counts().maxLive,1);assert.equal(f.counts().close,1);assert.equal(f.counts().shutdown,1);assert.equal(f.timers.size,0);return{adopted:false,oldRetired:true};});
for(const options of [{created:''},{created:'0'},{identityUnreadable:true},{foreign:true}])await check('unproven initial exact instance never receives Close '+JSON.stringify(options),options,async f=>{const p=await f.probe();assert.equal(p.status,'rejected');assert.equal(f.counts().close,0);assert.equal(f.counts().shutdown,0);assert.equal(f.counts().terminate,0);});
await check('graceful exact old exit still permits the same migration request',{exitsOnShutdown:true},async f=>{const p=await f.launch();assert.equal(p.status,'fulfilled',p.error);assert.equal(f.counts().unsafeDeployment,0);assert.equal(f.counts().launches,1);assert.equal(f.counts().maxLive,1);});
await check('PID reuse proves only the old instance exit and never terminates the replacement',{pidReused:true},async f=>{const p=await f.launch();assert.equal(p.status,'fulfilled',p.error);assert.equal(f.counts().terminate,0);assert.equal(f.counts().unsafeDeployment,0);assert.equal(f.counts().maxLive,1);});
for(const terminate of ['unconfirmed','identity-mismatch','timeout'])await check('unconfirmed exact retirement remains finite and never deploys '+terminate,{terminate},async f=>{const p=await f.launch();assert.equal(p.status,'rejected');assert.equal(f.counts().unsafeDeployment,0);assert.equal(f.counts().deployment,0);assert.equal(f.counts().launches,0);assert.equal(f.counts().terminate,1);assert.equal(f.counts().close,1);assert.equal(f.counts().shutdown,1);assert.equal(f.timers.size,0);});
for(const patch of [{openCalled:true},{hardwareWritesEnabled:true},{hcCloseCleanupPending:true},{unknownState:true}])await check('incomplete resource Close never authorizes retirement '+JSON.stringify(patch),{closePatch:patch},async f=>{const p=await f.probe();assert.equal(p.status,'rejected');assert.equal(f.counts().shutdown,0);assert.equal(f.counts().terminate,0);assert.equal(f.counts().close,1);});
for(const options of [{closeStatus:409},{shutdownStatus:401},{noListener:true}])await check('legacy rejection remains write-closed '+JSON.stringify(options),options,async f=>{const p=await f.probe();assert.equal(p.status,'rejected');assert.equal(f.counts().terminate,0);assert.equal(f.counts().deployment,0);});
await check('legacy shutdown 404 still requires exact retirement after safe Close',{shutdownStatus:404},async f=>{const p=await f.launch();assert.equal(p.status,'fulfilled',p.error);assert.equal(f.counts().terminate,1);assert.equal(f.counts().unsafeDeployment,0);assert.equal(f.counts().maxLive,1);});
const report={ok:cases.every(x=>x.ok),checks:cases.length,failed:cases.filter(x=>!x.ok).length,cases,sources:{fanHost:createHash('sha256').update(source).digest('hex'),api:createHash('sha256').update(fs.readFileSync(path.join(root,'src/bridge/api.ts'))).digest('hex'),ipc:createHash('sha256').update(fs.readFileSync(path.join(root,'src/bridge/ipc.ts'))).digest('hex')},hardwareCalls:0,cpuBenchmark:false,scope:'actual NativeFanHostLauncher public legacy migration plus direct legacy boundary; in-memory WebView IPC/processes/credentials and virtual clock; deployment records entry only; no Host Main/HC/network/device execution'};
fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({ok:report.ok,checks:report.checks,failed:report.failed,failures:cases.filter(x=>!x.ok).map(x=>({name:x.name,error:x.error,counts:x.counts}))}));if(!report.ok)process.exitCode=1;