import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import assert from 'node:assert/strict';
import {spawn,spawnSync} from 'node:child_process';
import {repo,fixture,require,hash,fingerprints} from './harness.mjs';
const tracked=['native/gpu_schedule_first_run.h','native/gpu_schedule_first_run_native.inc','native/main.cpp','src/bridge/yeman.ts','src/views/PowerView.vue','src/components/Toggle.vue','src/components/SwitchControl.vue','tools/boot_modes_selftest.mjs','tools/boot_modes_fixture/gpuScheduleNative.cpp','tools/boot_modes_fixture/gpuScheduleFirstRun.mjs'];
function processRun(command,args,cwd,timeout=45000){return new Promise(resolve=>{const started=Date.now(),child=spawn(command,args,{cwd,windowsHide:true});let stdout='',stderr='',timedOut=false;child.stdout.on('data',b=>stdout+=b);child.stderr.on('data',b=>stderr+=b);const timer=setTimeout(()=>{timedOut=true;if(process.platform==='win32')spawnSync('taskkill.exe',['/PID',String(child.pid),'/T','/F'],{windowsHide:true});else child.kill('SIGKILL');},timeout);child.on('error',error=>{clearTimeout(timer);resolve({exitCode:null,error:String(error),stdout,stderr,timedOut,elapsedMs:Date.now()-started});});child.on('close',code=>{clearTimeout(timer);resolve({exitCode:code,stdout,stderr,timedOut,elapsedMs:Date.now()-started});});});}
async function nativeRun(out,mutate,selected){
 fs.mkdirSync(out,{recursive:true});
 const sources={'gpu_schedule_first_run.h':'native/gpu_schedule_first_run.h','gpu_schedule_first_run_native.inc':'native/gpu_schedule_first_run_native.inc','fixture.cpp':'tools/boot_modes_fixture/gpuScheduleNative.cpp','json.hpp':'deps/json/json.hpp'};
 for(const [dest,src]of Object.entries(sources)){let bytes=fs.readFileSync(path.join(repo,src));if(mutate&&dest==='gpu_schedule_first_run.h')bytes=Buffer.from(mutate.header?.(bytes.toString())??bytes);if(mutate&&dest==='gpu_schedule_first_run_native.inc')bytes=Buffer.from(mutate.native?.(bytes.toString())??bytes);fs.writeFileSync(path.join(out,dest),bytes);}
 let env;
 if(process.env.YMCC_GPU_TOOLCHAIN_CMD)env=fs.readFileSync(process.env.YMCC_GPU_TOOLCHAIN_CMD,'utf8');
 else env='@echo off\r\ncall "'+(process.env['ProgramFiles(x86)']||'C:\\Program Files (x86)')+'\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\r\nif errorlevel 1 exit /b 1\r\n';
 const compile=env+'\r\ncl /nologo /std:c++20 /utf-8 /EHsc /W4 /WX /MT /I"." "fixture.cpp" /Fo"fixture.obj" /Fe"fixture.exe"\r\n';
 fs.writeFileSync(path.join(out,'compile.cmd'),compile);
 const built=await processRun(process.env.ComSpec||'cmd.exe',['/d','/c','compile.cmd'],out);
 fs.writeFileSync(path.join(out,'compile-result.json'),JSON.stringify(built,null,2));
 assert.equal(built.exitCode,0,'native compilation failed: '+built.stdout+built.stderr);
 const execution=await processRun(path.join(out,'fixture.exe'),selected?[selected]:[],out,10000);
 fs.writeFileSync(path.join(out,'process-result.json'),JSON.stringify(execution,null,2));
 assert(!execution.timedOut,'native execution timed out');
 const result=JSON.parse(execution.stdout);assert(result.cases.length>0,'no scenario ran');
 fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2));
 return {...result,exitCode:execution.exitCode,executableSha256:hash(path.join(out,'fixture.exe')),sourceHashes:Object.fromEntries(Object.keys(sources).map(file=>[file,hash(path.join(out,file))]))};
}
export function actualGpuBridge(){const s=fs.readFileSync(path.join(repo,'src/bridge/yeman.ts'),'utf8'),ts=require('typescript'),ast=ts.createSourceFile('yeman.ts',s,ts.ScriptTarget.Latest,true),names=new Set(['GPU_SCHED_KEY','GPU_SCHED_VALUE','readHardwareGpuSchedule','writeHardwareGpuSchedule']);const nodes=ast.statements.filter(node=>ts.isFunctionDeclaration(node)?names.has(node.name?.text):ts.isVariableStatement(node)&&node.declarationList.declarations.some(d=>names.has(d.name.getText(ast))));assert.equal(nodes.length,4,'GPU bridge source declarations missing');return nodes.map(node=>node.getText(ast)).join('\n');}
export async function bridgeRun(source=actualGpuBridge()){
 const cases=[];let value=2,ack=true,writes=[],reads=0,wrong=false,readError=false,writeError=false,osState,osQueryError=false,osQueries=[];
 const reset=()=>{value=2;ack=true;writes=[];reads=0;wrong=readError=writeError=osQueryError=false;osState={ok:true,enabled:false};osQueries=[];};
 const invoke=async(op,args)=>{assert.equal(op,'gpuSchedule.getState');assert.equal(JSON.stringify(args),'{}');osQueries.push({op,args});if(osQueryError)throw Error('fixture GPU state query denied');return osState;};
 const registry={async read(root,key,name){assert.equal(root,'HKLM');assert.equal(key,String.raw`SYSTEM\CurrentControlSet\Control\GraphicsDrivers`);assert.equal(name,'HwSchMode');reads++;if(readError)throw Error('read denied');return value;},async write(root,key,name,requested){assert.equal(root,'HKLM');assert.equal(key,String.raw`SYSTEM\CurrentControlSet\Control\GraphicsDrivers`);assert.equal(name,'HwSchMode');writes.push(requested);if(writeError)throw Error('write denied');if(ack&&!wrong)value=requested;return ack;}};
 const compiled=require('esbuild').transformSync(source.replaceAll('export async','async')+'\nglobalThis.gpu={readHardwareGpuSchedule,writeHardwareGpuSchedule}',{loader:'ts',target:'es2022'}).code;
 const context=vm.createContext({registry,invoke});vm.runInContext(compiled,context);const b=context.gpu;
 const check=async(name,run)=>{reset();try{await run();cases.push({name,ok:true});}catch(error){cases.push({name,ok:false,error:String(error)});}};
 for(const [v,expected]of [[2,true],[1,false],[0,false],[null,false],[undefined,false],[3,false]])await check('read '+String(v)+' is '+String(expected)+' without write',async()=>{value=v;assert.equal(await b.readHardwareGpuSchedule(),expected);assert.equal(reads,1);assert.equal(osQueries.length,v===2||v===1?0:1);assert.equal(writes.length,0);});
 await check('unset override reads Windows ON once without write',async()=>{value=null;osState={ok:true,enabled:true};assert.equal(await b.readHardwareGpuSchedule(),true);assert.equal(reads,1);assert.equal(osQueries.length,1);assert.equal(writes.length,0);});
 await check('invalid or failed Windows state query rejects without write',async()=>{
  value=null;const invalid=[null,undefined,{}, {enabled:true},{ok:false,enabled:true},{ok:true},{ok:true,enabled:1},{ok:true,enabled:'false'}];
  for(const response of invalid){osState=response;const before=reads;await assert.rejects(b.readHardwareGpuSchedule());assert.equal(reads,before+1);assert.equal(osQueries.length,reads);assert.equal(writes.length,0);}
  osQueryError=true;await assert.rejects(b.readHardwareGpuSchedule(),/fixture GPU state query denied/);assert.equal(reads,invalid.length+1);assert.equal(osQueries.length,reads);assert.equal(writes.length,0);
 });
 await check('read failure remains distinguishable from OFF',async()=>{readError=true;await assert.rejects(b.readHardwareGpuSchedule());assert.equal(reads,1);assert.equal(osQueries.length,0);assert.equal(writes.length,0);});
 for(const on of [false,true])await check('manual '+on+' exact DWORD and readback',async()=>{value=on?1:2;assert.equal(await b.writeHardwareGpuSchedule(on),true);assert.deepEqual(writes,[on?2:1]);assert.equal(reads,1);assert.equal(value,on?2:1);assert.equal(osQueries.length,0);});
 await check('false ACK rejected without success or readback',async()=>{ack=false;assert.equal(await b.writeHardwareGpuSchedule(false),false);assert.equal(reads,0);assert.equal(value,2);assert.equal(osQueries.length,0);});
 await check('write ACK with wrong readback rejected',async()=>{wrong=true;assert.equal(await b.writeHardwareGpuSchedule(false),false);assert.equal(reads,1);assert.equal(osQueries.length,0);});
 await check('write permission rejection propagates',async()=>{writeError=true;await assert.rejects(b.writeHardwareGpuSchedule(false));assert.equal(reads,0);assert.equal(osQueries.length,0);});
 await check('readback permission rejection propagates',async()=>{readError=true;await assert.rejects(b.writeHardwareGpuSchedule(false));assert.equal(reads,1);assert.equal(osQueries.length,0);});
 return {cases,passed:cases.filter(c=>c.ok).length,failed:cases.filter(c=>!c.ok).length,actualProductionBridgeExtracted:true,realRegistryWrites:0};
}
export async function runGpuScheduleFirstRun(out,args=[]){
 fs.mkdirSync(out,{recursive:true});const before=fingerprints(tracked);let report={suite:'boot-modes/gpu-schedule',pass:false,startedAt:new Date().toISOString(),sourceHashes:before,realRegistryWrites:0,physicalGpuRebootAcceptance:false,errors:[]};
 try{
  report.native=await nativeRun(path.join(out,'native'));
  if(!args.includes('--native-only')){report.bridge=await bridgeRun();fs.writeFileSync(path.join(out,'actual-gpu-bridge.ts'),actualGpuBridge());}
  if(report.native.failed||(report.bridge?.failed||0))throw Error('positive scenarios failed');
  if(!args.includes('--native-only')&&!args.includes('--no-negative')){
   report.negative={};
   const disabled=await nativeRun(path.join(out,'negative-remove388'),{header:s=>s.replace('sku=="388"','false')},'CPU identity: AMD Ryzen AI Max+ 388 w/ Radeon 8060S');assert.equal(disabled.failed,1,'removed 388 was not detected');report.negative.removed388=disabled;
   const guard=await nativeRun(path.join(out,'negative-existing-guard'),{native:s=>s.replace('mainExists||backupExists','mainExists&&backupExists')},'388 existing backup error isolation guard 1');assert.equal(guard.failed,1,'existing backup overwrite was not detected');report.negative.existingBackupOverwrite=guard;
   const unchecked=await bridgeRun(actualGpuBridge().replace(/return Number\(await registry\.read\('HKLM', GPU_SCHED_KEY, GPU_SCHED_VALUE\)\) === requested;/,'return true;'));
   assert(unchecked.cases.some(c=>c.name==='write ACK with wrong readback rejected'&&!c.ok),'omitted readback was not detected');report.negative.missingManualReadback=unchecked;
  }
  if(!args.includes('--native-only')&&!args.includes('--no-ui')){const {runGpuScheduleBrowser}=await import('./gpuScheduleBrowser.mjs');report.ui=await runGpuScheduleBrowser(path.join(out,'ui'));}
  assert.deepEqual(fingerprints(tracked),before,'production source changed during run');report.pass=true;
 }catch(error){report.errors.push(String(error.stack||error));process.exitCode=1;}
 report.finishedAt=new Date().toISOString();fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({pass:report.pass,nativePassed:report.native?.passed,nativeFailed:report.native?.failed,bridgePassed:report.bridge?.passed,uiCases:report.ui?.cases?.length,errors:report.errors,out},null,2));return report;
}
