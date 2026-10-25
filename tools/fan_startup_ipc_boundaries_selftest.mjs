import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {webcrypto,createHash} from 'node:crypto';
import {build} from 'esbuild';
const project=path.resolve(import.meta.dirname,'..'),out=process.argv[2];
if(!out)throw new Error('Evidence directory required');
fs.mkdirSync(out,{recursive:true});
const bundle=await build({stdin:{contents:"export {fs,shell,app} from './src/bridge/api';export {setLogSink} from './src/bridge/ipc';export {NativeFanHostLauncher,resolveFanHostConfig,fanStartupIpcForSelfTest} from './src/bridge/fanHost';",resolveDir:project,sourcefile:'fan-startup-ipc-fixture-entry.ts'},bundle:true,write:false,format:'iife',globalName:'FanBoundary',platform:'browser',target:'es2022',define:{'import.meta.env':'{}'},plugins:[{name:'expose-private-startup-facade-only-in-test',setup(b){b.onLoad({filter:/[\\/]src[\\/]bridge[\\/](?:fanHost|api)\.ts$/},args=>{const base=process.argv[3];const source=base?path.join(base,path.basename(args.path)):args.path;let contents=fs.readFileSync(source,'utf8');if(path.basename(args.path)==='fanHost.ts')contents+='\nexport const fanStartupIpcForSelfTest={fs,app,shell,proc};\n';return {contents,loader:'ts',resolveDir:path.dirname(args.path)};});}}]});
const code=bundle.outputFiles[0].text;
const TOKEN='0123456789abcdef'.repeat(4),EXE='C:\\fixture\\fan-host\\YeManFanHost.exe',PID=10571;
function fixture(options={}){
 let now=0,id=0,listener;const timers=new Map(),calls=[],logs=[];let drop=options.drop??null;let dropsRemaining=options.dropCount??Infinity,dropSeen=0;let authStatus=options.authStatus??200;let resident=options.resident!==false;
 const timer=(fn,delay)=>{const key=++id;timers.set(key,{fn,at:now+Math.max(0,Number(delay))});return key;};
 const clear=key=>timers.delete(key);
 const content=options.content??TOKEN;
 const win=new EventTarget();win.setTimeout=timer;win.clearTimeout=clear;
 const deliver=(message,result,error)=>listener?.({data:error?{id:message.id,error}:{id:message.id,result}});
 const invoke=message=>{
  const {cmd,args={}}=message;calls.push({cmd,args});if(cmd==='shell.hidden'&&options.launchBeforeDrop)resident=true;if(cmd===drop&&dropSeen++>=(options.dropAfter??0)&&dropsRemaining>0){dropsRemaining--;if(options.authStatusAfterDrop!==undefined)authStatus=options.authStatusAfterDrop;return;}
  if(cmd===options.rejectCommand){deliver(message,null,options.nativeError??'inert native permission failure');return;}
  let result;
  if(cmd==='app.aiFanMockSession')result={schemaVersion:1,enabled:false};
  else if(cmd==='app.fanStateDir'||cmd==='app.dataDir')result='C:\\fixture-state';
  else if(cmd==='app.pid')result=1;
  else if(cmd==='fs.exists')result=String(args.path).toLowerCase().endsWith('yemanfanhost.exe')||args.path==='fixture';
  else if(cmd==='fs.mkdir'||cmd==='fs.writeTextFileAtomic')result=options.atomicFalse?false:true;
  else if(cmd==='fs.readTextFile')result=content; // exact current native semantics: ignores maxBytes.
  else if(cmd==='fs.readTextRange')result=options.rangeReply??{content:Buffer.from(content).subarray(0,args.maxBytes).toString('utf8'),offset:0,size:Buffer.byteLength(content)};
  else if(cmd==='proc.findExact')result={found:resident,pid:resident?PID:0};
  else if(cmd==='process.identity')result={valid:true,pid:PID,path:options.foreign?'C:\\\\foreign\\\\Other.exe':EXE,processCreated:'133001111111111111'};
  else if(cmd==='http.request'&&!resident){deliver(message,null,'connect ECONNREFUSED');return;}
  else if(cmd==='http.request')result={status:authStatus,body:JSON.stringify({ok:true,state:{state:'Ready'}}),headers:''};
  else if(cmd==='shell.run')result={exitCode:0,stdout:' TCP 127.0.0.1:8765 0.0.0.0:0 LISTENING '+PID,stderr:''};
  else if(cmd==='shell.hidden')result={ok:true,pid:PID};
  else if(cmd==='app.fanActivity'||cmd==='app.fanLifecycleEvidence')result=null;
  else{deliver(message,null,'unexpected command: '+cmd);return;}
  deliver(message,result);
 };
 win.chrome={webview:{postMessage:invoke,addEventListener:(_type,fn)=>{listener=fn;}}};
 const context=vm.createContext({window:win,document:Object.assign(new EventTarget(),{hidden:false,visibilityState:"visible",createElement:()=>({style:{},content:{}}),querySelector:()=>null}),console,Event,EventTarget,CustomEvent:class extends Event{constructor(type,init){super(type);this.detail=init?.detail}},setTimeout:timer,clearTimeout:clear,setInterval:timer,clearInterval:clear,TextEncoder,TextDecoder,URL,crypto:webcrypto,performance:{now:()=>now},structuredClone,queueMicrotask});
 vm.runInContext(code,context,{timeout:5000});const api=context.FanBoundary;api.setLogSink(e=>logs.push(e));
 async function flush(){for(let n=0;n<24;n++)await Promise.resolve();await new Promise(setImmediate);}
 async function advance(ms){const target=now+ms;await flush();for(let n=0;n<100;n++){const next=[...timers.entries()].filter(([,r])=>r.at<=target).sort((a,b)=>a[1].at-b[1].at)[0];if(!next)break;now=next[1].at;timers.delete(next[0]);next[1].fn();await flush();}now=target;await flush();}
 function observe(promise){const state={status:'pending',value:null,error:null};void promise.then(value=>{state.status='fulfilled';state.value=value;},error=>{state.status='rejected';state.error=String(error)});return state;}
 return {api,calls,logs,timers,advance,flush,observe,setDrop:value=>drop=value,setResident:value=>resident=value};
}
const results=[];
async function check(name,run){try{await run();results.push({name,ok:true});console.log('PASS '+name)}catch(error){results.push({name,ok:false,error:String(error)});console.log('FAIL '+name+': '+error)}}
for(const [command,deadline] of [['fs.exists',5000],['fs.mkdir',15000]])await check('actual launcher retires missing '+command+' reply and next authenticated resident retry succeeds',async()=>{
 const f=fixture({drop:command,resident:false});const launcher=new f.api.NativeFanHostLauncher(),config=f.api.resolveFanHostConfig('C:\\fixture');config.hostExecutable=EXE;config.hostDirectory='C:\\fixture\\fan-host';config.sessionToken=TOKEN;
 const pending=f.observe(launcher.start(config));await f.flush();assert(f.calls.some(c=>c.cmd===command),'fault injection reached actual launcher');await f.advance(deadline+1);assert.equal(pending.status,'rejected','startup remained pending after its deadline');assert.match(pending.error,/IPC command timed out/);assert.equal(f.timers.size,0);assert(!f.calls.some(c=>c.cmd==='shell.hidden'),'missing prerequisite cannot launch');
 f.setDrop(null);f.setResident(true);const next=f.observe(launcher.start(config));for(let n=0;n<200&&next.status==='pending';n++)await f.flush();assert.equal(next.status,'fulfilled',next.error??'manual retry pending');assert.equal(next.value.adopted,true);assert.equal(next.value.pid,PID);assert(!f.calls.some(c=>c.cmd==='shell.hidden'),'authenticated resident is adopted once without duplicate launch');
});
for(const [command,deadline] of [['fs.exists',5000],['fs.mkdir',15000]])await check('same click adopts authenticated exact-image resident after missing '+command+' acknowledgement',async()=>{
 const f=fixture({drop:command}),launcher=new f.api.NativeFanHostLauncher(),config=f.api.resolveFanHostConfig('C:\\fixture');config.hostExecutable=EXE;config.hostDirectory='C:\\fixture\\fan-host';config.sessionToken=TOKEN;
 const p=f.observe(launcher.start(config));await f.advance(deadline+1);for(let n=0;n<200&&p.status==='pending';n++)await f.flush();assert.equal(p.status,'fulfilled',p.error??'original click still pending');assert.equal(p.value.adopted,true);assert.equal(p.value.pid,PID);assert(!f.calls.some(c=>c.cmd==='shell.hidden'||c.cmd==='fs.writeTextFileAtomic'));assert(f.calls.filter(c=>c.cmd==='http.request').every(c=>(c.args.method??'GET')==='GET'),'rescue performs read-only identity/session checks, no control write');assert.equal(f.timers.size,0);
});
await check('lost launch acknowledgement adopts the one already-created exact-image owner in the same click',async()=>{
 const f=fixture({resident:false,drop:'shell.hidden',launchBeforeDrop:true}),launcher=new f.api.NativeFanHostLauncher(),config=f.api.resolveFanHostConfig('C:\\fixture');config.hostExecutable=EXE;config.hostDirectory='C:\\fixture\\fan-host';config.sessionToken=TOKEN;
 let deploymentGateCalls=0;launcher.ensureFanHostDeployment=async()=>{deploymentGateCalls++}; // Only deployment metadata is a declared substitute in this launch-handoff case.
 const p=f.observe(launcher.start(config));for(let n=0;n<200&&!f.calls.some(c=>c.cmd==='shell.hidden');n++)await f.flush();assert.equal(f.calls.filter(c=>c.cmd==='shell.hidden').length,1,'actual launch point reached');await f.advance(15001);for(let n=0;n<200&&p.status==='pending';n++)await f.flush();assert.equal(p.status,'fulfilled',p.error??'lost launch result stranded current owner');assert.equal(p.value.adopted,true);assert.equal(deploymentGateCalls,1);assert.equal(f.calls.filter(c=>c.cmd==='shell.hidden').length,1,'never replay unknown launch');assert(!f.calls.some(c=>c.cmd.startsWith('process.terminate')));
});
for(const [dropCount,deadline,expected] of [[1,5000,'fulfilled'],[Infinity,15000,'rejected']])await check('cold launch identity timeout count='+String(dropCount)+' reconciles one current owner without another click or launch',async()=>{
 const f=fixture({resident:false,drop:'process.identity',dropCount,launchBeforeDrop:true}),launcher=new f.api.NativeFanHostLauncher(),config=f.api.resolveFanHostConfig('C:\\fixture');config.hostExecutable=EXE;config.hostDirectory='C:\\fixture\\fan-host';config.sessionToken=TOKEN;
 let deploymentGateCalls=0;launcher.ensureFanHostDeployment=async()=>{deploymentGateCalls++}; // Same declared metadata-only substitute as the lost-launch case.
 const p=f.observe(launcher.start(config));for(let n=0;n<200&&!f.calls.some(c=>c.cmd==='process.identity');n++)await f.flush();assert.equal(f.calls.filter(c=>c.cmd==='shell.hidden').length,1);assert(f.calls.some(c=>c.cmd==='process.identity'),'capture fault reached');await f.advance(deadline+1);for(let n=0;n<200&&p.status==='pending';n++)await f.flush();assert.equal(p.status,expected,p.error??'identity observation never settled');assert.equal(deploymentGateCalls,1);assert.equal(f.calls.filter(c=>c.cmd==='shell.hidden').length,1,'identity timeout cannot duplicate launch');assert(!f.calls.some(c=>c.cmd.startsWith('process.terminate')));assert.equal(f.timers.size,0);
 if(expected==='fulfilled'){assert.equal(p.value.adopted,true);assert.equal(p.value.pid,PID);assert.equal(p.value.processCreated,'133001111111111111');assert(f.calls.filter(c=>c.cmd==='http.request').every(c=>(c.args.method??'GET')==='GET'),'reconciliation stays read-only');}
 else {assert.match(p.error,/IPC command timed out: process.identity/);assert.equal(f.calls.filter(c=>c.cmd==='process.identity').length,3,'one initial capture and at most the two existing adoption identity reads');}
});
await check('small UTF8 file is complete and uses the bounded native range command once',async()=>{const text='风扇ROG🌬️';const f=fixture({content:text});const p=f.observe(f.api.fanStartupIpcForSelfTest.fs.readTextFile('fixture',4096,{timeoutMs:5000,bounded:true}));await f.flush();assert.equal(p.status,'fulfilled');assert.equal(p.value,text);assert.equal(f.calls.length,1);assert.equal(f.calls[0].cmd,'fs.readTextRange');assert.equal(f.calls[0].args.maxBytes,4096);assert.equal(f.timers.size,0);});
await check('oversized sidecar is rejected instead of transferring the full file or consuming a prefix',async()=>{const f=fixture({content:'x'.repeat(8192)});const p=f.observe(f.api.fanStartupIpcForSelfTest.fs.readTextFile('fixture',4096,{timeoutMs:5000,bounded:true}));await f.flush();assert.equal(p.status,'rejected');assert.match(p.error,/FILE_READ_LIMIT_EXCEEDED/);assert.equal(f.calls.length,1);assert.equal(f.calls[0].cmd,'fs.readTextRange');});
for(const reply of [{content:'x',offset:0,size:2},{content:'x',offset:1,size:1},{content:'x',offset:0,size:-1},{content:7,offset:0,size:1}])await check('incomplete/malformed range receipt cannot become a whole file '+JSON.stringify(reply),async()=>{const f=fixture({rangeReply:reply});const p=f.observe(f.api.fanStartupIpcForSelfTest.fs.readTextFile('fixture',4096,{timeoutMs:5000,bounded:true}));await f.flush();assert.equal(p.status,'rejected');assert.match(p.error,/FILE_READ_RECEIPT_INVALID/);});
for(const [command,run] of [['fs.readTextRange',f=>f.api.fanStartupIpcForSelfTest.fs.readTextFile('fixture',4096,{timeoutMs:5000,bounded:true})],['app.dataDir',f=>f.api.fanStartupIpcForSelfTest.app.dataDir(5000)],['app.pid',f=>f.api.fanStartupIpcForSelfTest.app.pid(5000)]])await check('read-only '+command+' expires without false success or implicit retry',async()=>{const f=fixture({drop:command});const p=f.observe(run(f));await f.advance(5001);assert.equal(p.status,'rejected');assert.match(p.error,/IPC command timed out/);assert.equal(f.calls.length,1);assert.equal(f.timers.size,0);});
for(const [command,run] of [['fs.writeTextFileAtomic',f=>f.api.fanStartupIpcForSelfTest.fs.writeTextFileAtomic('fixture',TOKEN,15000)],['shell.hidden',f=>f.api.fanStartupIpcForSelfTest.shell.hidden(EXE,[],15000)]])await check('unknown '+command+' outcome expires once without replaying mutation',async()=>{const f=fixture({drop:command});const p=f.observe(run(f));await f.advance(15001);assert.equal(p.status,'rejected');assert.match(p.error,/IPC command timed out/);assert.equal(f.calls.length,1);assert.equal(f.timers.size,0);});
for(const [command,deadline,run] of [['process.identity',5000,f=>f.api.fanStartupIpcForSelfTest.proc.identity(PID)],['process.terminateExact',15000,f=>f.api.fanStartupIpcForSelfTest.proc.terminateExact(PID,'133001111111111111',EXE)],['process.terminateTree',15000,f=>f.api.fanStartupIpcForSelfTest.proc.terminateTree(PID)]])await check('missing '+command+' receipt is bounded, never proof of exit or identity',async()=>{const f=fixture({drop:command}),p=f.observe(run(f));await f.advance(deadline+1);assert.equal(p.status,'rejected');assert.match(p.error,/IPC command timed out/);assert.equal(f.calls.length,1);assert.equal(f.timers.size,0);});
for(const options of [{authStatus:401},{authStatus:403},{foreign:true}])await check('timeout rescue refuses unauthenticated or foreign resident '+JSON.stringify(options),async()=>{const f=fixture({drop:'fs.exists',...options}),launcher=new f.api.NativeFanHostLauncher(),config=f.api.resolveFanHostConfig('C:\\fixture');config.hostExecutable=EXE;config.hostDirectory='C:\\fixture\\fan-host';config.sessionToken=TOKEN;const p=f.observe(launcher.start(config));await f.advance(5001);for(let n=0;n<200&&p.status==='pending';n++)await f.flush();assert.equal(p.status,'rejected');assert(!f.calls.some(c=>c.cmd==='shell.hidden'||c.cmd==='fs.writeTextFileAtomic'||c.cmd.startsWith('process.terminate')));});
// FAN1285: the dedicated Fan state-directory lookup has its own 5s IPC budget.
await check('Fan state-directory timeout adopts one authenticated exact-image owner without a second click',async()=>{
 const f=fixture({drop:'app.fanStateDir'}),launcher=new f.api.NativeFanHostLauncher(),config=f.api.resolveFanHostConfig('C:\\fixture');config.hostExecutable=EXE;config.hostDirectory='C:\\fixture\\fan-host';config.sessionToken=TOKEN;
 const p=f.observe(launcher.start(config));await f.advance(5001);for(let n=0;n<200&&p.status==='pending';n++)await f.flush();
 assert.equal(p.status,'fulfilled',p.error??'no authenticated adoption');assert.equal(p.value.pid,PID);assert.equal(p.value.adopted,true);assert.equal(config.confirmationToken,TOKEN);assert.equal(f.calls.filter(c=>c.cmd==='app.fanStateDir').length,1);
 assert(f.calls.some(c=>c.cmd==='http.request'&&c.args.url.endsWith('/api/state')));assert(f.calls.some(c=>c.cmd==='process.identity'));assert(!f.calls.some(c=>c.cmd==='fs.mkdir'||c.cmd==='fs.writeTextFileAtomic'||c.cmd==='shell.hidden'||c.cmd.startsWith('process.terminate')));assert.equal(f.timers.size,0);
});
for(const options of [{authStatus:401},{authStatus:403},{foreign:true},{resident:false}])await check('Fan directory timeout cannot adopt unverified owner '+JSON.stringify(options),async()=>{
 const f=fixture({drop:'app.fanStateDir',...options}),launcher=new f.api.NativeFanHostLauncher(),config=f.api.resolveFanHostConfig('C:\\fixture');config.hostExecutable=EXE;config.hostDirectory='C:\\fixture\\fan-host';config.sessionToken=TOKEN;const p=f.observe(launcher.start(config));await f.advance(5001);for(let n=0;n<200&&p.status==='pending';n++)await f.flush();
 assert.equal(p.status,'rejected');assert.notEqual(config.confirmationToken,TOKEN);assert(!f.calls.some(c=>c.cmd==='shell.hidden'||c.cmd==='fs.writeTextFileAtomic'||c.cmd.startsWith('process.terminate')));assert.equal(f.timers.size,0);
});
for(const token of [undefined,'invalid-session-token'])await check('Fan directory timeout with '+(token?'invalid':'missing')+' credential performs no recovery request',async()=>{
 const f=fixture({drop:'app.fanStateDir'}),launcher=new f.api.NativeFanHostLauncher(),config=f.api.resolveFanHostConfig('C:\\fixture');config.hostExecutable=EXE;config.hostDirectory='C:\\fixture\\fan-host';config.sessionToken=token;const p=f.observe(launcher.start(config));await f.advance(5001);await f.flush();assert.equal(p.status,'rejected');assert(!f.calls.some(c=>c.cmd==='http.request'||c.cmd==='shell.hidden'||c.cmd==='fs.writeTextFileAtomic'||c.cmd.startsWith('process.terminate')));assert.equal(f.timers.size,0);
});
await check('explicit Fan state-directory error stays rejected without treating it as a lost reply',async()=>{
 const f=fixture({rejectCommand:'app.fanStateDir',nativeError:'inert-directory-access-denied'}),launcher=new f.api.NativeFanHostLauncher(),config=f.api.resolveFanHostConfig('C:\\fixture');config.hostExecutable=EXE;config.hostDirectory='C:\\fixture\\fan-host';config.sessionToken=TOKEN;const p=f.observe(launcher.start(config));await f.flush();assert.equal(p.status,'rejected');assert.match(p.error,/inert-directory-access-denied/);assert(!f.calls.some(c=>c.cmd==='http.request'||c.cmd==='shell.hidden'||c.cmd==='fs.writeTextFileAtomic'));assert.equal(f.timers.size,0);
});
await check('unknown AI-mode capability cannot use the real Fan directory timeout rescue',async()=>{
 const f=fixture({drop:'app.aiFanMockSession'}),launcher=new f.api.NativeFanHostLauncher(),config=f.api.resolveFanHostConfig('C:\\fixture');config.hostExecutable=EXE;config.hostDirectory='C:\\fixture\\fan-host';config.sessionToken=TOKEN;const p=f.observe(launcher.start(config));await f.advance(5001);await f.flush();assert.equal(p.status,'rejected');assert(!f.calls.some(c=>c.cmd==='app.fanStateDir'||c.cmd==='http.request'||c.cmd==='shell.hidden'||c.cmd==='fs.writeTextFileAtomic'));assert.equal(f.timers.size,0);
});
// FAN1286: no-reply process discovery cannot authorize payload mutation or token rotation.
async function reachProcessLookup(f,count=1){for(let n=0;n<200&&f.calls.filter(c=>c.cmd==='proc.findExact').length<count;n++)await f.flush();assert(f.calls.filter(c=>c.cmd==='proc.findExact').length>=count,'fixture did not reach its process-query fault before advancing time');}
await check('unknown exact-process query blocks deployment before any absent-owner inference',async()=>{
 const f=fixture({drop:'proc.findExact',authStatus:503}),launcher=new f.api.NativeFanHostLauncher(),config=f.api.resolveFanHostConfig('C:\\fixture');config.hostExecutable=EXE;config.hostDirectory='C:\\fixture\\fan-host';config.sessionToken=TOKEN;
 let deployment=0;launcher.ensureFanHostDeployment=async()=>{deployment++}; // Only observe entry to deployment; never execute installer I/O.
 const p=f.observe(launcher.start(config));await reachProcessLookup(f);await f.advance(5001);for(let n=0;n<200&&p.status==='pending';n++)await f.flush();
 assert.equal(deployment,0,'unknown resident lookup reached payload deployment');assert.equal(p.status,'rejected');assert(!f.calls.some(c=>c.cmd==='shell.hidden'||c.cmd==='fs.writeTextFileAtomic'||c.cmd.startsWith('process.terminate')));assert.equal(f.timers.size,0);
});
await check('unknown process query still permits same-click authenticated exact-image reattachment',async()=>{
 const f=fixture({drop:'proc.findExact',authStatus:503,authStatusAfterDrop:200}),launcher=new f.api.NativeFanHostLauncher(),config=f.api.resolveFanHostConfig('C:\\fixture');config.hostExecutable=EXE;config.hostDirectory='C:\\fixture\\fan-host';config.sessionToken=TOKEN;
 let deployment=0;launcher.ensureFanHostDeployment=async()=>{deployment++};const p=f.observe(launcher.start(config));await reachProcessLookup(f);await f.advance(5001);for(let n=0;n<200&&p.status==='pending';n++)await f.flush();
 assert.equal(p.status,'fulfilled',p.error??'lost authenticated resident');assert.equal(p.value.pid,PID);assert.equal(p.value.adopted,true);assert.equal(deployment,0);assert(f.calls.some(c=>c.cmd==='process.identity'));assert(!f.calls.some(c=>c.cmd==='shell.hidden'||c.cmd==='fs.writeTextFileAtomic'||c.cmd.startsWith('process.terminate')));assert.equal(f.timers.size,0);
});
for(const options of [{authStatus:401},{foreign:true,authStatus:503,authStatusAfterDrop:200},{resident:false,authStatus:503}])await check('unknown process query never reattaches an unverified owner '+JSON.stringify(options),async()=>{
 const f=fixture({drop:'proc.findExact',...options}),launcher=new f.api.NativeFanHostLauncher(),config=f.api.resolveFanHostConfig('C:\\fixture');config.hostExecutable=EXE;config.hostDirectory='C:\\fixture\\fan-host';config.sessionToken=TOKEN;
 let deployment=0;launcher.ensureFanHostDeployment=async()=>{deployment++};const p=f.observe(launcher.start(config));await reachProcessLookup(f);await f.advance(16001);for(let n=0;n<200&&p.status==='pending';n++)await f.flush();
 assert.equal(deployment,0,'unresolved query reached deployment');assert.equal(p.status,'rejected');assert(!f.calls.some(c=>c.cmd==='shell.hidden'||c.cmd==='fs.writeTextFileAtomic'||c.cmd.startsWith('process.terminate')));if(options.foreign)assert.equal(f.calls.filter(c=>c.cmd==='proc.findExact').length,1,'definite foreign identity must not request another process lookup');assert.equal(f.timers.size,0);
});
await check('unknown second resident query cannot rotate a previously missing credential',async()=>{
 const f=fixture({drop:'proc.findExact',dropAfter:1,resident:false,content:''}),launcher=new f.api.NativeFanHostLauncher(),config=f.api.resolveFanHostConfig('C:\\fixture');config.hostExecutable=EXE;config.hostDirectory='C:\\fixture\\fan-host';config.sessionToken=undefined;
 let deployment=0;launcher.ensureFanHostDeployment=async()=>{deployment++};const p=f.observe(launcher.start(config));await reachProcessLookup(f,2);await f.advance(5001);for(let n=0;n<200&&p.status==='pending';n++)await f.flush();
 assert.equal(p.status,'rejected');assert.equal(deployment,1,'first explicit absent reply may enter deployment');assert.equal(f.calls.filter(c=>c.cmd==='proc.findExact').length,2);assert.equal(typeof config.sessionToken,'undefined','unknown process query minted a new token');assert(!f.calls.some(c=>c.cmd==='fs.writeTextFileAtomic'||c.cmd==='shell.hidden'||c.cmd.startsWith('process.terminate')));assert.equal(f.timers.size,0);
});
await check('explicit process-query error stays unknown rather than absent and never deploys',async()=>{
 const f=fixture({rejectCommand:'proc.findExact',nativeError:'inert-process-query-access-denied',authStatus:503}),launcher=new f.api.NativeFanHostLauncher(),config=f.api.resolveFanHostConfig('C:\\fixture');config.hostExecutable=EXE;config.hostDirectory='C:\\fixture\\fan-host';config.sessionToken=TOKEN;
 let deployment=0;launcher.ensureFanHostDeployment=async()=>{deployment++};const p=f.observe(launcher.start(config));await reachProcessLookup(f);await f.flush();assert.equal(deployment,0);assert.equal(p.status,'rejected');assert(!f.calls.some(c=>c.cmd==='shell.hidden'||c.cmd==='fs.writeTextFileAtomic'||c.cmd.startsWith('process.terminate')));assert.equal(f.timers.size,0);
});
await check('atomic false acknowledgement still rejects and is never accepted as a completed write',async()=>{const f=fixture({atomicFalse:true});const p=f.observe(f.api.fanStartupIpcForSelfTest.fs.writeTextFileAtomic('fixture',TOKEN,15000));await f.flush();assert.equal(p.status,'rejected');assert.match(p.error,/原子写入失败/);});
await check('unrelated callers keep the existing default wait and whole-file command',async()=>{const f=fixture({drop:'fs.exists'});const p=f.observe(f.api.fs.exists('fixture'));await f.advance(50001);assert.equal(p.status,'pending');assert.equal(f.timers.size,0);const other=fixture({content:'abc'}),q=other.observe(other.api.fs.readTextFile('fixture',4096));await other.flush();assert.equal(q.status,'fulfilled');assert.equal(other.calls[0].cmd,'fs.readTextFile');});
const hash=file=>createHash('sha256').update(fs.readFileSync(process.argv[3]&&/\/(api|fanHost)\.ts$/.test(file)?path.join(process.argv[3],path.basename(file)):path.join(project,file))).digest('hex');
const report={ok:results.every(r=>r.ok),checks:results.length,failed:results.filter(r=>!r.ok).length,results,source:{api:hash('src/bridge/api.ts'),fanHost:hash('src/bridge/fanHost.ts'),ipc:hash('src/bridge/ipc.ts')},scope:'Actual bundled API/native launcher/IPC, scratch-only exposure of private Fan facade, virtual timer and selective missing native replies; native port is replaced; the lost-launch case substitutes only deployment metadata; no real file/process/device writes or CPU timing',hardwareCalls:0,cpuBenchmark:false};
fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify({ok:report.ok,checks:report.checks,failed:report.failed}));if(!report.ok)process.exitCode=1;
