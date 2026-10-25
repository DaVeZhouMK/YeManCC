/** Local compiled router regression only. Does not invoke either global producer. */
import fs from 'node:fs';
import path from 'node:path';
import readline from 'node:readline';
import {spawn} from 'node:child_process';
import {pathToFileURL} from 'node:url';
import {REPO,FIXTURE,sourceFiles,toolchain,verifyTools,hash,write,read,need,inside} from './contract.mjs';
import {compileManaged} from './compile-managed.mjs';
import {executor} from './execute.mjs';
const faultDefinitions=[
 {name:'validation-removed',file:'native/BatteryHost/RuntimeContract.cs',before:'throw new IOException("充电组件与主程序版本不一致，请退出 YMCC 后同时更新 YeManCC 和 PowerControl 文件夹");',after:'return; // private fault: invalid envelope accepted',expected:['reject-wrong-protocol-set','reject-wrong-source-set','reject-missing-protocol-get','invalid-envelope-does-not-create-router-domain']},
 {name:'router-set-bypassed',file:'native/BatteryHost/HostDomainRouter.cs',before:'return Program.DispatchBattery(current,request);',after:'return op=="set" ? current.Read() : Program.DispatchBattery(current,request);',expected:['router-set-reaches-production-dispatch','stale-fence-still-rejected','software-policy-applied','explicit-release-restores-software-owned-byte']},
 {name:'dispatch-release-bypassed',file:'native/BatteryHost/Program.cs',before:'"release"=>controller.Release()',after:'"release"=>controller.Read()',expected:['explicit-release-restores-software-owned-byte']},
 {name:'router-shutdown-bypassed',file:'native/BatteryHost/HostDomainRouter.cs',before:'battery?.Shutdown();',after:'/* private fault: router battery shutdown removed */',expected:['shutdown-returns-software-ownership']}
];
function once(source,before,after){need(source.split(before).length===2,'BATTERY_ROUTER_FAULT_NOT_UNIQUE:'+before);return source.replace(before,after);}
export async function probeEndpoint({root,tool,managed}){
 const temp=path.join(root,'endpoint');fs.mkdirSync(temp);for(const d of ['tmp','local','roaming'])fs.mkdirSync(path.join(temp,d));
 const env={...process.env,TEMP:path.join(temp,'tmp'),TMP:path.join(temp,'tmp'),LOCALAPPDATA:path.join(temp,'local'),APPDATA:path.join(temp,'roaming'),YMCC_BATTERY_SIM_RECEIPT:path.join(temp,'native-boundary.json'),YMCC_BATTERY_SIM_BRIDGE:'1',YMCC_BATTERY_SIM_GUARD_NEGATIVE:'0',YMCC_BATTERY_RUNTIME_ALLOWLIST:managed.allowFile,YMCC_BATTERY_RUNTIME_RECEIPT:path.join(temp,'runtime-boundary.json')};
 for(const key of ['NODE_OPTIONS','NODE_PATH','DOTNET_STARTUP_HOOKS','DOTNET_ADDITIONAL_DEPS','DOTNET_SHARED_STORE'])delete env[key];
 const child=spawn(tool.dotnet,[managed.endpointDll],{cwd:temp,env,windowsHide:true,stdio:['pipe','pipe','pipe']});
 const pending=new Map(),transcript=[],checks=[];let sequence=0,stderr='';
 const done=new Promise((resolve,reject)=>{child.once('error',reject);child.once('close',(code,signal)=>{for(const p of pending.values()){clearTimeout(p.timer);p.reject(Error('ENDPOINT_CLOSED'));}pending.clear();resolve({pid:child.pid,code,signal});});});
 child.stderr.on('data',b=>{stderr+=b;});
 readline.createInterface({input:child.stdout}).on('line',line=>{try{const r=JSON.parse(line),p=pending.get(r.id);need(p,'BATTERY_PROBE_RESPONSE_ID');pending.delete(r.id);clearTimeout(p.timer);transcript.push({request:p.request,response:r});p.resolve(r);}catch(e){for(const p of pending.values()){clearTimeout(p.timer);p.reject(e);}pending.clear();}});
 const rpc=(command,extra={})=>new Promise((resolve,reject)=>{const request={id:++sequence,command,...extra};const timer=setTimeout(()=>{pending.delete(request.id);reject(Error('BATTERY_PROBE_TIMEOUT'));},15000);pending.set(request.id,{request,resolve,reject,timer});child.stdin.write(JSON.stringify(request)+'\n');});
 const check=(name,pass,detail)=>checks.push({name,pass:!!pass,detail});
 const fixture=async(command,args)=>{const r=await rpc(command,args?{args}:{});need(r.ok,'BATTERY_PROBE_FIXTURE_REJECTED');return r.reply;};
 let hello;
 const exchange=async(request,extra={})=>{const r=await rpc('pipe.exchange',{raw:JSON.stringify(request)+'\n',...extra});need(r.ok,'BATTERY_PROBE_EXCHANGE_REJECTED');return{...r,value:r.raw?JSON.parse(r.raw):null};};
 const envelope=(op,extra={})=>({protocol:hello.protocol,sourceId:hello.sourceId,op,...extra});
 let terminal;
 try{
  await fixture('fixture.reset');hello=(await exchange({op:'hello'})).value;check('actual-host-hello',hello.ok&&hello.component==='YeManBatteryHost');
  const before=await fixture('fixture.stats');
  let bad=await exchange(envelope('set',{protocol:hello.protocol+1,fence:'bad',target:80}));check('reject-wrong-protocol-set',!!bad.value?.error);
  bad=await exchange(envelope('set',{sourceId:hello.sourceId+'-wrong',fence:'bad',target:80}));check('reject-wrong-source-set',!!bad.value?.error);
  bad=await exchange({op:'get'});check('reject-missing-protocol-get',!!bad.value?.error);
  const after=await fixture('fixture.stats');check('invalid-envelope-does-not-create-router-domain',bad.batteryFactoryCalls===0&&after.opens===before.opens&&after.reads===before.reads&&after.writes===0,{before,after});
  const unknown=await exchange(envelope('not-a-battery-operation'));check('router-rejects-unknown-operation',unknown.value?.error==='未知电池操作');
  const oversized=await rpc('pipe.exchange',{raw:' '.repeat(4096)+'\n'});check('request-length-limit-before-dispatch',JSON.parse(oversized.raw).error==='电池请求过大');
  const malformed=await rpc('pipe.exchange',{raw:'{broken-json\n'});check('parse-error-is-structured-response',!!JSON.parse(malformed.raw).error);
  const cancelled=await exchange(envelope('get'),{cancelRead:true});check('read-cancellation-does-not-dispatch',cancelled.raw===''&&cancelled.stats.opens===after.opens&&cancelled.stats.writes===0);
  const got=await exchange(envelope('get'));check('get-reaches-current-controller',got.value.ok&&got.value.currentLimit===100);
  const applied=await exchange(envelope('set',{fence:got.value.fence,target:80}));check('router-set-reaches-production-dispatch',applied.value.ok&&applied.value.currentLimit===80&&applied.stats.writes===1);
  const stale=await exchange(envelope('set',{fence:'stale',target:100}));check('stale-fence-still-rejected',!stale.value.ok&&stale.stats.writes===applied.stats.writes);
  const refreshed=await exchange(envelope('refresh'));check('refresh-dispatch-preserved',refreshed.value.ok);
  await fixture('fixture.reset',{route:'ayaneo-cec-bypass'});await fixture('fixture.set',{percent:90});
  const software=await exchange(envelope('get'));const owned=await exchange(envelope('set',{fence:software.value.fence,target:80}));check('software-policy-applied',owned.value.ok&&owned.value.managed&&owned.stats.raw===0x55&&owned.stats.writes===1);
  const restored=await exchange(envelope('restore'));check('restore-is-not-replayed-write',restored.value.ok&&restored.stats.writes===owned.stats.writes);
  const released=await exchange(envelope('release'));check('explicit-release-restores-software-owned-byte',released.value.ok&&!released.value.managed&&released.stats.raw===0xaa&&released.stats.writes===2&&released.stats.memoryRecords===1);
  await fixture('fixture.reset');await fixture('fixture.suspend');const suspended=await exchange(envelope('get'));check('router-refuses-lazy-creation-while-suspended',!!suspended.value.error&&suspended.stats.opens===0);
  await fixture('fixture.resume');const resumed=await exchange(envelope('get'));check('router-resumes-after-software-suspend',resumed.value.ok);
  await fixture('fixture.reset',{route:'ayaneo-cec-bypass'});await fixture('fixture.set',{percent:90});
  const shutdownGet=await exchange(envelope('get'));const shutdownSet=await exchange(envelope('set',{fence:shutdownGet.value.fence,target:80}));
  const shutdown=await fixture('fixture.shutdown');check('shutdown-returns-software-ownership',shutdown.raw===0xaa&&shutdown.writes===shutdownSet.stats.writes+(shutdownSet.value.managed?1:0));
  child.stdin.end();terminal=await done;
 }finally{
  child.stdin.end();
  // Only this owned endpoint is eligible for cleanup after a local harness failure.
  if(terminal===undefined){if(child.exitCode===null&&!child.killed)child.kill();terminal=await done;}
  fs.writeFileSync(path.join(temp,'stderr.log'),stderr);write(path.join(temp,'transcript.json'),transcript);write(path.join(temp,'terminal.json'),terminal);
 }
 need(terminal.code===0&&terminal.signal===null,'BATTERY_PROBE_CHILD_NOT_NATURAL_SUCCESS');
 const native=read(path.join(temp,'native-boundary.json')),runtime=read(path.join(temp,'runtime-boundary.json'));
 need(native.status==='PASS'&&runtime.status==='PASS'&&runtime.unknown.length===0,'BATTERY_PROBE_BOUNDARY_FAILURE');
 const result={checks,failed:checks.filter(x=>!x.pass).map(x=>x.name),terminal,nativeBoundary:native.status,runtimeBoundary:runtime.status,realHostMain:false,realHardware:false};write(path.join(root,'probe-result.json'),result);return result;
}
export async function runRouterContractProbe(evidence){
 need(path.isAbsolute(evidence)&&!inside(REPO,evidence)&&!fs.existsSync(evidence),'BATTERY_PROBE_FRESH_EXTERNAL_ROOT_REQUIRED');
 fs.mkdirSync(evidence,{recursive:true});const tool=toolchain();verifyTools(tool);
 const sources=sourceFiles().map(relative=>({relative,path:path.join(REPO,relative),sha256:hash(path.join(REPO,relative))}));write(path.join(evidence,'source-capture.json'),sources);
 const results=[];
 for(const fault of [null,...faultDefinitions]){
  const name=fault?.name??'positive',root=path.join(evidence,name);fs.mkdirSync(root);const inputRoot=path.join(root,'input-sources');
  for(const row of sources){const dst=path.join(inputRoot,row.relative);fs.mkdirSync(path.dirname(dst),{recursive:true});fs.copyFileSync(row.path,dst,fs.constants.COPYFILE_EXCL);need(hash(dst)===row.sha256,'BATTERY_PROBE_SOURCE_CAPTURE_DRIFT:'+row.relative);}
  if(fault){const p=path.join(inputRoot,fault.file),source=fs.readFileSync(p,'utf8'),mutant=once(source,fault.before,fault.after);fs.writeFileSync(p,mutant);write(path.join(root,'fault.json'),{...fault,beforeSha256:hash(path.join(REPO,fault.file)),afterSha256:hash(p)});}
  const ledger=[],execute=executor(root,ledger),managed=compileManaged({inputRoot,root,tool,execute});
  write(path.join(root,'compile-ledger.json'),ledger);const probe=await probeEndpoint({root,tool,managed});
  const expected=fault?.expected??[];need(JSON.stringify(probe.failed)===JSON.stringify(expected),'BATTERY_PROBE_EXACT_SEMANTIC_FAILURES:'+name+':'+JSON.stringify(probe.failed));results.push({name,compiled:true,naturalExit:probe.terminal.code,checks:probe.checks.length,failed:probe.failed,expected});
 }
 for(const row of sources)need(hash(row.path)===row.sha256,'BATTERY_PROBE_SOURCE_CHANGED_DURING_RUN:'+row.relative);
 verifyTools(tool);
 const result={status:'PASS',scope:'local compiled receive/router/DispatchBattery regression; original global leaves not executed',results,sourceCount:sources.length,sourceChanged:false};write(path.join(evidence,'result.json'),result);return result;
}
if(process.argv[1]&&import.meta.url===pathToFileURL(path.resolve(process.argv[1])).href){
 const args=process.argv.slice(2);need(args.length===2&&args[0]==='--out','Usage: router-contract-probe.mjs --out ABS_FRESH_DIRECTORY');
 console.log(JSON.stringify(await runRouterContractProbe(path.resolve(args[1])),null,2));
}
