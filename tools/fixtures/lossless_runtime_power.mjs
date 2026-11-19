import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
export async function runtimePowerOnly({arguments_,root,scriptDirectory,calibrationToolchainPreamble}){
 const started=performance.now(),startedUtc=new Date().toISOString();
 const valued=new Set(['--source-root','--candidate-dir','--out','--case']);let legacy=false;
 const values={};
 for(let i=0;i<arguments_.length;i++){
  const arg=arguments_[i];
  if(arg==='--runtime-power'||arg==='--legacy-control'){
   assert.equal(arguments_.filter(x=>x===arg).length,1,'Duplicate '+arg);if(arg==='--legacy-control')legacy=true;continue;
  }
  assert(valued.has(arg),'Unsupported runtime power option '+arg);
  assert(!Object.hasOwn(values,arg),'Duplicate '+arg);const value=arguments_[++i];assert(value&&!value.startsWith('--'),'Missing '+arg+' value');values[arg]=value;
 }
 const cases=['pending-power-never-discovers','unknown-power-then-ready','revoke-during-discovery','off-on-aba-retires-old-discovery','off-during-throttle','power-cancel-returns-watch-and-fresh-ready','discovery-exception-self-heals','subscriber-exception-self-heals','consecutive-failures-retain-original-delay','native-watch-entry-public-source-and-off','actual-common-projection-resumes-native-monitor','concurrent-stop-waits-for-actual-worker-return','ipc-watch-off-after-queued-on','ipc-watch-latest-detail-after-aba','ipc-watch-off-during-blocked-read'];
 const uiaCases=['runtime-uia-transient-init-self-heals','runtime-uia-cancel-during-client-start','runtime-uia-off-returns-client-subscriptions','runtime-uia-monitor-cancel-between-properties'];
 const readCases=['runtime-read-detect-pending','runtime-read-state-pending','runtime-read-normal-publication-and-schema','runtime-read-cancel-between-properties','runtime-read-inflight-return-and-fresh-ready'];
 const watchOrderCases=cases.slice(-3);cases.push(...uiaCases,...readCases);
 const selected=values['--case'],exact=selected?.startsWith('from:')?selected.slice(5):selected;assert(!selected||cases.includes(exact),'Unknown runtime power case');
 assert(!legacy||[cases[0],cases[3]].includes(selected),'Original header control requires an exact pending or ABA case');
 assert(values['--candidate-dir']&&values['--out'],'Explicit candidate and output required');
 const readOnly=!!selected&&readCases.includes(exact);
 const uia=!!selected&&(uiaCases.includes(exact)||readOnly);
 const watchOrder=!!selected&&watchOrderCases.includes(exact);
 const candidate=fs.realpathSync(values['--candidate-dir']),out=path.resolve(values['--out']);fs.mkdirSync(out);
 const pin=p=>{const b=fs.readFileSync(p);return{path:fs.realpathSync(p),bytes:b.length,sha256:createHash('sha256').update(b).digest('hex')}};
 const key=p=>path.resolve(p).replaceAll('\\','/').toLowerCase();
 const inputs=[path.join(scriptDirectory,'lossless_toggle_owner_selftest.mjs'),path.join(scriptDirectory,'fixtures/lossless_runtime_power.mjs'),path.join(scriptDirectory,'lossless_runtime_power_fixture.h'),...(readOnly?[path.join(scriptDirectory,'fixtures/lossless_runtime_read_fixture.h'),path.join(scriptDirectory,'fixtures/lossless_runtime_read_cases.inc'),path.join(root,'tools/ymcc_sim/global/decky-update-recovery/native-source-admission.mjs')]:[]),...(uia?['lossless_uia_ports.h','lossless_uia_client_stubs.inc','lossless_uia_element_stubs.inc','lossless_uia_cases.inc'].map(n=>path.join(scriptDirectory,'fixtures',n)):[]),...(watchOrder?[path.join(scriptDirectory,'fixtures/lossless_runtime_watch_dispatch.inc'),path.join(scriptDirectory,'fixtures/lossless_runtime_watch_cases.inc'),path.join(root,'tools/ymcc_sim/global/decky-update-recovery/native-source-admission.mjs')]:[]),path.join(scriptDirectory,'lossless_launch_selftest.cpp'),path.join(root,'tools/fixtures/hc_reviewed_calibration/toolchain.mjs'),path.join(root,'deps/json/json.hpp'),process.execPath,
  ...(!legacy?[path.join(candidate,'main.cpp')]:[]),...fs.readdirSync(candidate).filter(n=>n.endsWith('.h')).map(n=>path.join(candidate,n))].map(pin);
 const result={pass:false,canonicalId:'controller.lossless-toggle-owner',selection:'native-runtime-power',selectedCase:selected??null,legacyControl:legacy,startedUtc,inputsBefore:inputs,processes:[],fullParent:false,actualLSActions:0};
 const save=()=>fs.writeFileSync(path.join(out,'RESULT.json'),JSON.stringify(result,null,2)+'\n');
 const quote=p=>{assert(!/["%\r\n]/.test(p),'Unsupported build path');return '"'+p.replaceAll('/','\\')+'"';};
 const execute=(label,program,args,timeout)=>{
  const t=performance.now(),run=spawnSync(program,args,{cwd:out,encoding:'utf8',windowsHide:true,timeout,maxBuffer:16*1024*1024});
  result.processes.push({label,program,args,pid:run.pid,status:run.status,signal:run.signal,error:run.error?.message,elapsedMs:performance.now()-t});
  fs.writeFileSync(path.join(out,label+'.stdout.log'),run.stdout||'');fs.writeFileSync(path.join(out,label+'.stderr.log'),run.stderr||'');save();return run;
 };
 try{
  const vcvars='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
  if(!legacy){
   const main=fs.readFileSync(path.join(candidate,'main.cpp'),'utf8'),begin=main.indexOf('    ipc_on("lossless.watch",'),end=main.indexOf('\n    });',begin)+8;
   assert(begin>=0&&end>begin,'Actual watch registration missing');const original=main.slice(begin,end);
   assert.equal(original.split('ymcc::lossless::runtimeMonitor()').length,2,'Watch singleton adapter must be exact');
   const adapted=original.replace('ymcc::lossless::runtimeMonitor()','fixtureMonitor()');fs.writeFileSync(path.join(out,'watch-entry.inc'),adapted);
   result.watchEntry={source:pin(path.join(candidate,'main.cpp')),originalSha256:createHash('sha256').update(original).digest('hex'),adapted:pin(path.join(out,'watch-entry.inc')),adaptations:[{from:'ymcc::lossless::runtimeMonitor()',to:'fixtureMonitor()',count:1}],scope:'Only singleton lookup uses the owned actual RuntimeMonitor with inert discovery. Entire original schema/exit/configure/public-source/receipt body remains verbatim.'};
   const projectionStart=main.indexOf('static void projectSharedPowerLifecycle(HWND window) {'),projectionEnd=main.indexOf('static DWORD WINAPI suspendResumeSubscriptionCallback(',projectionStart);assert(projectionStart>=0&&projectionEnd>projectionStart);
   const projection=main.slice(projectionStart,projectionEnd).trimEnd();assert.equal(projection.split('ymcc::lossless::runtimeMonitor()').length,2,'Common singleton adapter must be exact');
   const projected=projection.replace('ymcc::lossless::runtimeMonitor()','fixtureMonitor()');fs.writeFileSync(path.join(out,'common-projection.inc'),projected);
   result.commonProjection={originalSha256:createHash('sha256').update(projection).digest('hex'),adapted:pin(path.join(out,'common-projection.inc')),adaptations:[{from:'ymcc::lossless::runtimeMonitor()',to:'fixtureMonitor()',count:1}],scope:'Actual complete main projection; original current Reader/Registration and actual RuntimeMonitor worker; peripheral listeners/IPC/timer terminal are inert.'};
  }
  if(watchOrder){
   const {cppBlock}=await import((await import('node:url')).pathToFileURL(path.join(root,'tools/ymcc_sim/global/decky-update-recovery/native-source-admission.mjs')));
   const main=fs.readFileSync(path.join(candidate,'main.cpp'),'utf8');
   const anchor='static bool ipc_cmd_async(const std::string& cmd) {',start=main.indexOf(anchor);assert(start>=0);const end=cppBlock(main,main.indexOf('{',start),'watch async policy').end;
   const dispatchStart=main.indexOf('        // Overwrite renderer data at the sole receiver, before either dispatch.');
   const dispatchEnd=main.indexOf('        ipc_post_json(targetChildId, resp);',main.indexOf('// ── 同步路径',dispatchStart))+'        ipc_post_json(targetChildId, resp);'.length;
   assert(dispatchStart>=0&&dispatchEnd>dispatchStart);const policy=main.slice(start,end),dispatch=main.slice(dispatchStart,dispatchEnd);
   fs.writeFileSync(path.join(out,'watch-async-policy.inc'),policy);fs.writeFileSync(path.join(out,'watch-dispatch-arms.inc'),dispatch);
   result.watchDispatch={policy:{start,end,originalSha256:createHash('sha256').update(policy).digest('hex'),file:pin(path.join(out,'watch-async-policy.inc'))},dispatch:{start:dispatchStart,end:dispatchEnd,originalSha256:createHash('sha256').update(dispatch).digest('hex'),file:pin(path.join(out,'watch-dispatch-arms.inc'))},bodyAdaptations:0,poolAndResponseTransportControlled:true,completeWebViewOrPool:false};
  }
  if(readOnly){
   const {cppBlock}=await import((await import('node:url')).pathToFileURL(path.join(root,'tools/ymcc_sim/global/decky-update-recovery/native-source-admission.mjs')));
   const main=fs.readFileSync(path.join(candidate,'main.cpp'),'utf8'),runtime=fs.readFileSync(path.join(candidate,'lossless_runtime.h'),'utf8');
   const entries=['detect','getState'].map(name=>{const a=main.indexOf('    ipc_on("lossless.'+name+'",'),z=main.indexOf('\n    });',a)+8;assert(a>=0&&z>a);return main.slice(a,z);}).join('\n');
   const original=entries,adapted=entries.replace('ymcc::lossless::inspectRuntime(','fixtureInspectRuntime(');assert.notEqual(adapted,original);
   const a=runtime.indexOf('inline RuntimeReceipt inspectRuntime('),z=cppBlock(runtime,runtime.indexOf('){',a)+1,'inspectRuntime').end,inspect=runtime.slice(a,z);
   const projected=inspect.replace('inspectRuntime(','fixtureInspectRuntime(').replace('discoverRuntime()','fixtureDiscoverRuntime()');
   fs.writeFileSync(path.join(out,'read-entry.inc'),adapted);fs.writeFileSync(path.join(out,'read-inspect.inc'),projected);
   result.readEntry={originalSha256:createHash('sha256').update(original).digest('hex'),adapted:pin(path.join(out,'read-entry.inc')),adaptations:['inspect singleton leaf lookup only']};
   result.readInspect={originalSha256:createHash('sha256').update(inspect).digest('hex'),adapted:pin(path.join(out,'read-inspect.inc')),adaptations:['function name','discovery terminal only'],actualGuardedBodyPreserved:true};
  }
  const exe=path.join(out,'contract.exe');
  const build=calibrationToolchainPreamble(vcvars,{boundInheritedPath:true})+'\r\ncl /nologo /std:c++20 /EHsc /utf-8 /O1 /W4 '+(legacy?'':'/WX ')+
   '/DYMCC_LOSSLESS_RUNTIME_POWER_ONLY=1 '+(readOnly?'/DYMCC_LOSSLESS_RUNTIME_READ_ONLY=1 ':'')+(uia?'/DYMCC_LOSSLESS_RUNTIME_UIA_ONLY=1 ':'')+(watchOrder?'/DYMCC_LOSSLESS_RUNTIME_WATCH_ORDER=1 ':'')+(legacy?'/DYMCC_LOSSLESS_RUNTIME_LEGACY_CONTROL=1 ':'/DYMCC_LOSSLESS_RUNTIME_ENTRY_ONLY=1 ')+
   '/I'+quote(candidate)+' /I'+quote(scriptDirectory)+' /I'+quote(out)+' /I'+quote(path.join(root,'deps/json'))+' '+quote(path.join(scriptDirectory,'lossless_launch_selftest.cpp'))+
   ' /sourceDependencies '+quote(path.join(out,'dependencies.json'))+' /Fe'+quote(exe)+' /Fo'+quote(path.join(out,'contract.obj'))+
   ' /link /VERBOSE:LIB user32.lib shell32.lib advapi32.lib\r\nexit /b %errorlevel%\r\n';
  fs.writeFileSync(path.join(out,'build.cmd'),build);result.build=pin(path.join(out,'build.cmd'));save();
  const compile=execute('compile','cmd.exe',['/d','/c',path.join(out,'build.cmd')],45000);assert.equal(compile.status,0,'Actual Native RuntimeMonitor compile failed');
  const graph=JSON.parse(fs.readFileSync(path.join(out,'dependencies.json'),'utf8').replace(/^\uFEFF/,''));
  result.compilerInputs=[graph.Data.Source,...graph.Data.Includes].map(pin);
  const declared=new Set(inputs.map(v=>key(v.path)));
  if(result.readEntry)declared.add(key(result.readEntry.adapted.path));
  if(result.readInspect)declared.add(key(result.readInspect.adapted.path));
  if(result.watchEntry)declared.add(key(result.watchEntry.adapted.path));
  if(result.commonProjection)declared.add(key(result.commonProjection.adapted.path));
  if(result.watchDispatch)for(const x of [result.watchDispatch.policy.file,result.watchDispatch.dispatch.file])declared.add(key(x.path));
  result.undeclaredProjectInputs=result.compilerInputs.filter(v=>{
   const p=key(v.path);return (p.startsWith(key(candidate)+'/')||p.startsWith(key(scriptDirectory)+'/')||p.startsWith(key(root)+'/'))&&!declared.has(p);
  });
  assert.deepEqual(result.undeclaredProjectInputs,[],'Actual compiler consumed undeclared project input');
  result.libraries=[...new Set([...compile.stdout.matchAll(/Searching\s+(.+?\.lib):?\r?$/gm)].map(m=>m[1].replace(/:$/,'')))].map(pin);
  assert(result.libraries.length>0,'Actual link graph missing');result.executable=pin(exe);save();
  const run=execute('behavior',exe,[path.join(out,'private-profile'),...(selected?[selected]:[])],20000);
  assert.equal(run.status,legacy?1:0,'Actual RuntimeMonitor behavior failed');result.native=JSON.parse(run.stdout.trim());
  const executedDefinitions=readOnly?readCases:uia?uiaCases:watchOrder?cases.slice(0,-(uiaCases.length+readCases.length)):cases.slice(0,-(watchOrderCases.length+uiaCases.length+readCases.length));
  assert.equal(result.native.pass,!legacy);assert.deepEqual(result.native.cases.map(v=>v.name),selected?(selected.startsWith('from:')?executedDefinitions.slice(executedDefinitions.indexOf(exact)):[selected]):executedDefinitions);
  if(legacy)assert(result.native.failures.includes(selected===cases[0]?'pending-public-fact-must-not-discover-publish-or-watch':'same-bool-aba-does-not-revive-old-read'),'Old header did not reproduce selected defect');
  result.inputsAfter=inputs.map(v=>pin(v.path));assert.deepEqual(result.inputsAfter,inputs,'Fixed source changed');
  result.compilerInputsAfter=result.compilerInputs.map(v=>pin(v.path));assert.deepEqual(result.compilerInputsAfter,result.compilerInputs,'Compiler inputs changed');
  result.pass=true;
 }catch(e){result.error={message:e.message,stack:e.stack};process.exitCode=1;}
 finally{result.endedUtc=new Date().toISOString();result.elapsedMs=performance.now()-started;save();}
 console.log(JSON.stringify({pass:result.pass,selection:result.selection,cases:result.native?.cases.length,assertions:result.native?.assertions,error:result.error?.message}));
}
