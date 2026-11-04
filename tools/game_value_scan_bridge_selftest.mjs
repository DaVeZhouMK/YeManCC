import assert from 'node:assert/strict';import fs from 'node:fs';import path from 'node:path';import vm from 'node:vm';import{webcrypto,createHash}from'node:crypto';import{build}from'esbuild';
const root=process.cwd(),selected=process.argv.includes('--session-config'),caseAt=process.argv.indexOf('--case-filter'),caseFilter=caseAt<0?null:process.argv[caseAt+1],outRoot=path.resolve(process.env.YMCC_CE_VALIDATION_ROOT||path.resolve(root,'../../Build/Validation/CeEngineBridge-selftest'));if(selected&&!process.env.YMCC_CE_VALIDATION_ROOT)throw Error('SessionConfig selection requires an explicit owned Evidence root');fs.mkdirSync(outRoot,{recursive:true});
const sourceOverride=process.env.YMCC_VALUE_SCAN_SOURCE;if(sourceOverride&&(!selected||!path.resolve(sourceOverride).startsWith(path.dirname(outRoot)+path.sep)))throw Error('Source override must be private Evidence');
const pin=p=>({path:path.resolve(p),sha256:createHash('sha256').update(fs.readFileSync(p)).digest('hex')}),inputPaths=[sourceOverride||'src/bridge/gameValueScan.ts','tools/game_value_scan_bridge_selftest.mjs','PowerControl/MemoryScan/MemoryScanHost.cs','PowerControl/MemoryScan/CeScanEngine.lua','node_modules/esbuild/lib/main.js','node_modules/@esbuild/win32-x64/esbuild.exe'],inputs=inputPaths.map(pin),started=performance.now();
const producerPath=process.env.YMCC_VALUE_SCAN_HOST_STATUS;if(producerPath){inputPaths.push(producerPath);inputs.push(pin(producerPath));}
const mocks={
'api.ts':`const f=globalThis.fx;export const app={powerControlDir:async()=> f.pc||'R:/PowerControl',dataDir:async()=> f.data||'R:/Data',pid:async()=>55,exeDir:async()=> 'R:/YMCC'};export const fs={exists:async p=>f.exists(p),mkdir:async p=>f.mkdir(p),readTextFile:async(p,max)=>{if(f.readHook){const value=await f.readHook(p);if(value!==undefined)return value;}if(!f.files.has(p))throw Error('missing');return f.files.get(p)},writeTextFileAtomic:async(p,s)=>f.write(p,s),remove:async p=>f.files.delete(p)};export const proc={identity:async()=>({valid:true,pid:55,path:'R:/YMCC/YeManCC.exe',processCreated:'134365380584475500'})};export const shell={hidden:async(p,a)=>f.launch(p,a)};`,
'gameQuickSession.ts':`export const getLockedGameTarget=()=>globalThis.fx.target;export const validateLockedGameTarget=async()=>{if(globalThis.fx.validateHook)await globalThis.fx.validateHook();return globalThis.fx.target};`,
'settingsRepository.ts':`export const getSettingsGeneration=()=>globalThis.fx.generation;`,
};
const bundle=await build({entryPoints:['src/bridge/gameValueScan.ts'],bundle:true,format:'cjs',platform:'node',write:false,plugins:[{name:'scan-boundaries',setup(b){if(sourceOverride)b.onLoad({filter:/[\\/]gameValueScan\.ts$/},()=>({contents:fs.readFileSync(sourceOverride,'utf8'),loader:'ts'}));b.onLoad({filter:/[\\/](api|gameQuickSession|settingsRepository)\.ts$/},a=>({contents:mocks[path.basename(a.path)],loader:'ts'}))}}]});
function fixture({disk=false}={}){
 const fx={now:1792070000000,files:new Map(),generation:0,target:{pid:1234,processCreated:'134365380584475513',path:'R:/Game/Game.exe',lockGeneration:1,lockReason:'action'},intervals:new Map(),nextTimer:0,launches:[],requests:[],state:null,root:'',config:null,validateHook:null,receiptHook:null,launchHook:null};
 if(disk){const own=path.join(outRoot,'owned-'+webcrypto.randomUUID()),normal=p=>path.resolve(p.replaceAll('\\',path.sep));fs.mkdirSync(own,{recursive:true});const check=p=>{const n=normal(p);if(!n.startsWith(own+path.sep))throw Error('File IO outside owned Evidence: '+n);return n;};fx.pc=path.join(own,'PowerControl');fx.data=path.join(own,'Data');fx.diskRoot=own;fx.diskIO={reads:0,writes:0};fx.files={has:p=>fs.existsSync(check(p)),get:p=>{fx.diskIO.reads++;return new TextDecoder('utf-8',{fatal:true,ignoreBOM:true}).decode(fs.readFileSync(check(p)));},set:(p,v)=>{fx.diskIO.writes++;const n=check(p);fs.mkdirSync(path.dirname(n),{recursive:true});const temp=n+'.'+webcrypto.randomUUID()+'.tmp';fs.writeFileSync(temp,v);try{fs.renameSync(temp,n);}finally{if(fs.existsSync(temp))fs.unlinkSync(temp);}},delete:p=>{const n=check(p);if(fs.existsSync(n))fs.unlinkSync(n);return true;}};fx.mkdir=p=>{fs.mkdirSync(check(p),{recursive:true});return true;};fx.files.set(fx.pc+'\\MemoryScan\\MemoryScanHost.exe','INERT marker - never executed');}
 fx.mkdir||=()=>true;fx.exists=p=>p.endsWith('MemoryScanHost.exe')?!fx.missingWorker:fx.files.has(p);
 const state=()=>({...fx.state,rows:fx.state.rows.map(r=>({...r}))});
 fx.write=(p,s)=>{if(fx.writeHook)fx.writeHook(p,s);fx.files.set(p,s);if(p.endsWith('config.json'))fx.config=JSON.parse(s);if(p.includes('\\requests\\')){const q=JSON.parse(s);fx.requests.push(q);fx.state={...fx.state,at:fx.now};if(q.action==='first')fx.state={...fx.state,stage:'ready',revision:fx.state.revision+1,count:1,rows:[{address:'0000000000001000',value:q.value}],dataType:q.dataType||'int32'};const r={id:q.id,token:q.token,ok:true,error:'',state:state()};if(fx.receiptHook)fx.receiptHook(r);fx.files.set(p.replace('\\requests\\','\\responses\\'),fx.rawReceiptHook?fx.rawReceiptHook(r):JSON.stringify(r));fx.files.set(fx.root+'\\status.json',JSON.stringify(fx.state));}return true};
 fx.launch=async(p,a)=>{if(a[0]==='--probe'){fx.probes=(fx.probes||0)+1;fx.files.set(a[1],JSON.stringify({engine:'cheat-engine',available:fx.ceAvailable!==false}));return{ok:true,pid:4000}}fx.root=a[0];fx.launches.push({program:p,args:a});fx.state={...fx.config,engine:'cheat-engine',at:fx.now,stage:'idle',message:'ready',revision:0,dataType:'int32',count:0,offset:0,pageSize:40,rows:[],selected:'',selectedValue:null,frozen:false,frozenValue:null,scannedBytes:0,skippedBlocks:0,bulkLimit:32,scanRound:0,searchValue:'',lastWriteCount:0,lastWriteValue:'',temporarilyPaused:false,pauseRestored:true};if(disk&&producerPath){const produced=JSON.parse(fs.readFileSync(producerPath,'utf8'));fx.state={...produced,...fx.config,at:fx.now};}if(fx.launchHook)fx.launchHook();if(!fx.deferStatus)fx.files.set(fx.root+'\\status.json',JSON.stringify(fx.state));return{ok:true,pid:4567}};
 class Clock extends Date{static now(){return fx.now}}
 const module={exports:{}};vm.runInNewContext(bundle.outputFiles[0].text,{module,exports:module.exports,fx,crypto:webcrypto,console,TextEncoder,TextDecoder,Date:Clock,setTimeout:(fn,ms)=>setImmediate(()=>{fx.now+=ms;fn()}),clearTimeout:clearImmediate,setInterval:fn=>{const id=++fx.nextTimer;fx.intervals.set(id,fn);return id},clearInterval:id=>fx.intervals.delete(id)});
 fx.tick=async ms=>{fx.now+=ms;for(const fn of [...fx.intervals.values()])fn();for(let i=0;i<100;i++)await Promise.resolve()};return{fx,scan:module.exports};
}
const checks=[],skipped=[];async function check(name,body){if((selected&&!name.startsWith('session-config:'))||(caseFilter&&!name.includes(caseFilter))){skipped.push(name);return;}try{await body();checks.push({name,ok:true});console.log('PASS '+name)}catch(error){checks.push({name,ok:false,error:error.message});console.log('FAIL '+name+': '+error.stack)}}
await check('missing locked game is rejected before creating a helper',async()=>{const{fx,scan}=fixture(),target=fx.target;fx.target=null;await assert.rejects(scan.startGameValueSession(target),/锁定游戏/);assert.equal(fx.launches.length,0)});
await check('concurrent openings reuse one helper and exact game identity',async()=>{const{fx,scan}=fixture();const[a,b]=await Promise.all([scan.startGameValueSession(fx.target),scan.startGameValueSession({...fx.target})]);assert.equal(a.pid,b.pid);assert.equal(fx.launches.length,1);assert.equal(fx.launches[0].args.length,1);await scan.stopGameValueSession();assert.equal(fx.intervals.size,0)});
await check('owner identity uses the native PID and executable directory without querying self through process.identity',async()=>{const{fx,scan}=fixture();await scan.startGameValueSession(fx.target);assert.equal(fx.config.ownerPid,55);assert.equal(fx.config.ownerDirectory,'R:/YMCC');assert.equal(fx.config.ownerCreated,undefined);assert.equal(fx.config.ownerPath,undefined);await scan.stopGameValueSession()});
await check('first scan keeps input values as strings and publishes only authenticated results',async()=>{const{fx,scan}=fixture();await scan.startGameValueSession(fx.target);const r=await scan.gameValueCommand('first',{value:'99999',dataType:'int32'});assert.equal(fx.requests[0].value,'99999');assert.equal(r.rows[0].value,'99999');r.rows[0].address='wrong';assert.notEqual(scan.getGameValueState().rows[0].address,'wrong');await scan.stopGameValueSession()});
await check('wrong response token cannot be accepted by the UI',async()=>{const{fx,scan}=fixture();await scan.startGameValueSession(fx.target);fx.receiptHook=r=>r.token='wrong';await assert.rejects(scan.gameValueCommand('first',{value:'5000',dataType:'int32'}),/回执不匹配/);assert.equal(scan.getGameValueState().stage,'idle');await scan.stopGameValueSession()});
await check('source switch during validation creates no queued write',async()=>{const{fx,scan}=fixture();await scan.startGameValueSession(fx.target);fx.validateHook=()=>{fx.generation++};await assert.rejects(scan.gameValueCommand('write',{value:'9999'}),/锁定游戏/);assert.equal(fx.requests.length,0);await scan.stopGameValueSession()});
await check('another PID or creation time never inherits an active scan session',async()=>{for(const field of['pid','processCreated']){const{fx,scan}=fixture();await scan.startGameValueSession(fx.target);fx.target={...fx.target,[field]:field==='pid'?2345:'134365380584475514'};await assert.rejects(scan.gameValueCommand('write',{value:'9999'}),/锁定游戏/);assert.equal(fx.requests.length,0);await fx.tick(2000);assert.equal(fx.intervals.size,0);assert.equal(fx.files.get(fx.root+'\\stop'),'stop')}});
await check('stale helper status stops freeze indication and requests bounded session shutdown',async()=>{const{fx,scan}=fixture();await scan.startGameValueSession(fx.target);fx.state.frozen=true;fx.files.set(fx.root+'\\status.json',JSON.stringify(fx.state));await fx.tick(10000);const r=scan.getGameValueState();assert.equal(r.stage,'closed');assert.equal(r.frozen,false);assert.equal(fx.files.get(fx.root+'\\stop'),'stop');assert.equal(fx.intervals.size,0);await scan.stopGameValueSession()});
await check('cancel writes one session marker without starting a replacement helper',async()=>{const{fx,scan}=fixture();await scan.startGameValueSession(fx.target);await scan.cancelGameValueScan();assert.equal(fx.files.get(fx.root+'\\cancel'),'cancel');assert.equal(fx.launches.length,1);await scan.stopGameValueSession()});
await check('missing CE removes capability and cannot start any scanner',async()=>{
 const {fx,scan}=fixture();fx.ceAvailable=false;assert.equal(await scan.isGameValueScanAvailable(),false);await assert.rejects(scan.startGameValueSession(fx.target),/未安装CE/);assert.equal(fx.launches.length,0);assert.equal(fx.requests.length,0);
});
await check('missing adapter never falls back to the old memory scanner',async()=>{
 const {fx,scan}=fixture();fx.missingWorker=true;assert.equal(await scan.isGameValueScanAvailable(),false);assert.equal(fx.launches.length,0);assert.equal(fx.probes,undefined);
});
await check('old custom scanner receipts are rejected even with correct process and token',async()=>{
 const {fx,scan}=fixture();await scan.startGameValueSession(fx.target);fx.receiptHook=r=>{delete r.state.engine};await assert.rejects(scan.gameValueCommand('first',{value:'5000'}),/无效状态/);await scan.stopGameValueSession();
});

await check('session-config: real files reject malformed acknowledgement and permit explicit retry',async()=>{
 for(const kind of ['ok-string','future','duplicate','empty','truncated','array','oversize']){
  const{fx,scan}=fixture({disk:true});await scan.startGameValueSession(fx.target);
  if(kind==='ok-string')fx.receiptHook=r=>r.ok='false';
  if(kind==='future')fx.receiptHook=r=>r.schemaVersion=2;
  if(kind==='duplicate')fx.rawReceiptHook=r=>'{"ok":false,'+JSON.stringify(r).slice(1);
  if(['empty','truncated','array','oversize'].includes(kind))fx.rawReceiptHook=()=>({empty:'',truncated:'{',array:'[]',oversize:' '.repeat(131073)})[kind];
  const begin=fx.now;await assert.rejects(scan.gameValueCommand('first',{value:'5000',dataType:'int32'}),undefined,kind);assert(fx.now-begin<10000,kind+' was repeatedly treated as missing');
  assert.notEqual(scan.getGameValueState()?.stage,'ready',kind);fx.receiptHook=null;fx.rawReceiptHook=null;
  const good=await scan.gameValueCommand('first',{value:'5001',dataType:'int32'});assert.equal(good.rows[0].value,'5001');assert(fx.diskIO.reads>0&&fx.diskIO.writes>0);await scan.stopGameValueSession();assert.equal(fx.intervals.size,0);
 }
});
await check('session-config: config write readback must precede any inert helper launch',async()=>{
 for(const fault of ['write','readback']){
  const{fx,scan}=fixture({disk:true});if(fault==='write')fx.writeHook=p=>{if(p.endsWith('config.json'))throw Error('owned replace refused');};else fx.readHook=p=>p.endsWith('config.json')?'{}':undefined;
  await assert.rejects(scan.startGameValueSession(fx.target));assert.equal(fx.launches.length,0,fault);assert.equal(scan.getGameValueState(),null);assert.equal(fx.intervals.size,0);
  fx.writeHook=null;fx.readHook=null;assert(await scan.startGameValueSession(fx.target));assert.equal(fx.launches.length,1);await scan.stopGameValueSession();
 }
});
await check('session-config: corrupt status stops timer file storms and an explicit new session recovers',async()=>{
 const{fx,scan}=fixture({disk:true});await scan.startGameValueSession(fx.target);const bad='{"schemaVersion":2}';fx.files.set(fx.root+'\\status.json',bad);await fx.tick(1000);
 assert.equal(fx.intervals.size,0);assert.equal(scan.getGameValueState()?.stage,'closed');assert.equal(fx.files.get(fx.root+'\\status.json'),bad);
 const reads=fx.diskIO.reads;for(let i=0;i<12;i++)await fx.tick(1000);assert.equal(fx.diskIO.reads,reads);assert(await scan.startGameValueSession(fx.target));assert.equal(fx.launches.length,2);await scan.stopGameValueSession();
});
await check('session-config: unknown identity and stale revision cannot grant request authority',async()=>{
 const{fx,scan}=fixture({disk:true});await scan.startGameValueSession(fx.target);
 for(const patch of [{pid:'1234'},{message:42},{offset:-1},{pageSize:'40'},{rows:[null]},{frozen:'false'},{schemaVersion:2}]){
  fx.receiptHook=r=>Object.assign(r.state,patch);await assert.rejects(scan.gameValueCommand('first',{value:'5000',dataType:'int32'}));
 }
 fx.receiptHook=null;const good=await scan.gameValueCommand('first',{value:'5000',dataType:'int32'});fx.receiptHook=r=>r.state.revision=good.revision-1;await assert.rejects(scan.gameValueCommand('next',{value:'5001',dataType:'int32'}));
 assert.equal(scan.getGameValueState().revision,good.revision);await scan.stopGameValueSession();
});


await check('session-config: late old file completion never stops its replacement session',async()=>{
 const{fx,scan}=fixture({disk:true});await scan.startGameValueSession(fx.target);const oldRoot=fx.root;let release;
 fx.readHook=p=>p===oldRoot+'\\status.json'?new Promise(resolve=>release=resolve):undefined;
 await fx.tick(1000);assert(release);await scan.stopGameValueSession();assert(await scan.startGameValueSession(fx.target));const token=scan.getGameValueState().token;
 release('{');for(let i=0;i<100;i++)await Promise.resolve();assert.equal(scan.getGameValueState().token,token);assert.notEqual(scan.getGameValueState().stage,'closed');assert.equal(fx.intervals.size,1);await scan.stopGameValueSession();
});


await check('session-config: healthy late startup receipt keeps the original bounded wait',async()=>{
 const{fx,scan}=fixture({disk:true});fx.deferStatus=true;const pending=scan.startGameValueSession(fx.target);for(let i=0;i<200;i++)await Promise.resolve();assert.equal(fx.launches.length,1);
 await fx.tick(1500);assert.equal(fx.intervals.size,1);assert.equal(fx.files.has(fx.root+'\\stop'),false);fx.state.at=fx.now;fx.files.set(fx.root+'\\status.json',JSON.stringify(fx.state));
 assert(await pending);assert.equal(fx.launches.length,1);await scan.stopGameValueSession();assert.equal(fx.intervals.size,0);
});


await check('session-config: parser-boundary UTF8 BOM reaches the actual parser and preserves unknowns',async()=>{
 const{fx,scan}=fixture({disk:true});await scan.startGameValueSession(fx.target);let sawBOM=false;
 fx.rawReceiptHook=r=>'\uFEFF'+JSON.stringify({...r,state:{...r.state,unknownParserField:{keep:'BOM'}}});
 fx.readHook=p=>{if(!p.includes('\\responses\\'))return undefined;const raw=fx.files.get(p);assert.equal(raw.charCodeAt(0),0xfeff);sawBOM=true;return raw;};
 const good=await scan.gameValueCommand('first',{value:'5000',dataType:'int32'});assert(sawBOM);assert.equal(good.unknownParserField.keep,'BOM');assert.equal(good.stage,'ready');await scan.stopGameValueSession();
});
await check('session-config: parser-boundary escaped duplicate keys reject without granting acknowledgement',async()=>{
 for(const location of ['root','state']){
  const{fx,scan}=fixture({disk:true});await scan.startGameValueSession(fx.target);let raw;
  fx.rawReceiptHook=r=>raw=location==='root'?'{"o\\u006b":false,'+JSON.stringify(r).slice(1):JSON.stringify(r).replace('"message":','"mess\\u0061ge":"spoof","message":');
  await assert.rejects(scan.gameValueCommand('first',{value:'5000',dataType:'int32'}));const response=fx.root+'\\responses\\'+fx.requests.at(-1).id+'.json';assert.equal(fx.files.get(response),raw);assert.equal(scan.getGameValueState().stage,'idle');
  fx.rawReceiptHook=null;assert.equal((await scan.gameValueCommand('first',{value:'5001',dataType:'int32'})).stage,'ready');await scan.stopGameValueSession();
 }
});
await check('session-config: parser-boundary containers at depth16 accept and depth17 reject',async()=>{
 for(const shape of ['array','object'])for(const levels of [15,16]){
  const{fx,scan}=fixture({disk:true});await scan.startGameValueSession(fx.target);let raw;
  fx.rawReceiptHook=r=>{let nested='keep';for(let n=0;n<levels;n++)nested=shape==='array'?[nested]:{next:nested};return raw=JSON.stringify({...r,state:{...r.state,unknownParserField:nested}});};
  if(levels===15){const good=await scan.gameValueCommand('first',{value:'5000',dataType:'int32'});let nested=good.unknownParserField;for(let n=0;n<levels;n++)nested=shape==='array'?nested[0]:nested.next;assert.equal(nested,'keep');}
  else {await assert.rejects(scan.gameValueCommand('first',{value:'5000',dataType:'int32'}));assert.equal(scan.getGameValueState().stage,'idle');assert.equal(fx.files.get(fx.root+'\\responses\\'+fx.requests.at(-1).id+'.json'),raw);fx.rawReceiptHook=null;assert.equal((await scan.gameValueCommand('first',{value:'5001',dataType:'int32'})).stage,'ready');}
  await scan.stopGameValueSession();
 }
});

const report={passed:checks.filter(x=>x.ok).length,failed:checks.filter(x=>!x.ok).length,checks,realGameMemoryWrites:0,boundary:'Production bridge; file/process IPC and time are isolated fixtures.'};report.selected=selected?'session-config':null;report.caseFilter=caseFilter;report.producerArtifact=producerPath?{...pin(producerPath),binding:'Actual published host fields retained; token/PID/birth/path rebound only to newly persisted fixture config, and at to the fixture clock; no actual scan/memory result'}:null;report.skipped=skipped;report.inputs=inputs;report.sourceDrift=inputPaths.map(pin).filter((v,i)=>v.sha256!==inputs[i].sha256);report.elapsedMs=performance.now()-started;report.node={...pin(process.execPath),version:process.version};report.fileBoundary=selected?'Real UTF8 files and atomic replacement only in owned Evidence directories; process/helper/memory ports inert':'Original Map boundary';const out=path.join(outRoot,'bridge-tests.json');fs.mkdirSync(path.dirname(out),{recursive:true});fs.writeFileSync(out,JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));if(report.failed)process.exitCode=1;