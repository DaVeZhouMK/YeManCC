import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {spawnSync} from 'node:child_process';import {createHash} from 'node:crypto';import {fileURLToPath} from 'node:url';
import {extract} from '../../../fixtures/legion_motion_consumer/extract_main.mjs';
import {calibrationToolchainPreamble} from '../../../fixtures/hc_reviewed_calibration/toolchain.mjs';
export function runCadenceConsumer({repo,out,beforeHeader=process.env.YMCC_GYRO_BEFORE_HEADER}){
 const here=path.dirname(fileURLToPath(import.meta.url)),mainFile=path.join(repo,'native/main.cpp'),main=fs.readFileSync(mainFile,'utf8'),template=fs.readFileSync(path.join(here,'cadence.cpp.in'),'utf8');
 const sha=b=>createHash('sha256').update(b).digest('hex');const hashes=new Map();
 const bind=f=>{hashes.set(f,sha(fs.readFileSync(f)));return fs.readFileSync(f,'utf8');};bind(mainFile);bind(fileURLToPath(import.meta.url));bind(path.join(here,'run.mjs'));bind(path.join(here,'cadence.cpp.in'));
 for(const name of ['input_shortcut_toggle.h','input_startup_policy.h','controller_persona_settings.h','game_input_override.h','controller_persona.h'])bind(path.join(repo,'native',name));bind(path.join(repo,'deps/json/json.hpp'));bind(path.join(repo,'tools/fixtures/hc_reviewed_calibration/toolchain.mjs'));bind(path.join(repo,'tools/fixtures/legion_motion_consumer/extract_main.mjs'));
 const constants=main.slice(main.indexOf('constexpr double kInputCaptureHighMagDps'),main.indexOf('static std::atomic<bool> g_inputCaptureSawRealGyro{false};')+'static std::atomic<bool> g_inputCaptureSawRealGyro{false};'.length);
 const begin=main.indexOf('class InputCaptureSensorSink final {'),sink=main.slice(begin,main.indexOf('\n};',begin)+4);
 const functions=['hcSinkTimeOfDayMs','inputCapturePnPInstanceIdFromDeviceId','inputCapturePnPReArmSensorDevice','gyroJudgeFinishInconclusive','gyroJudgeFinishSuccess','gyroJudgeFinishFailed','gyroJudgeRequiredSamples','gyroJudgeBeginObservation','inputCaptureSensorHighMagKeepaliveStep'];
 const bindings=functions.map(n=>extract(main,n));
 const statusStart=main.indexOf('appendDomainLog("gyro", "gyro-status-second", {'),statusEnd=main.indexOf('});',statusStart)+3;
 assert(statusStart>0&&statusEnd>statusStart);
 const cpp=template.replace('// PRODUCTION_CONSTANTS',constants+'\n'+main.match(/^static ULONGLONG g_sensorStallLastRebindAt[^\n]+/m)[0]+'\n'+extract(main,'hcSinkTimeOfDayMs').code).replace('// PRODUCTION_SINK',sink).replace('// PRODUCTION_META',extract(main,'InputCaptureSensorMeta','struct').code).replace('// PRODUCTION_JUDGE',bindings.slice(1).map(x=>x.code).join('\n')).replace('// PRODUCTION_TRIGGER',['trim_ascii','inputCaptureMotionTriggerPressed','inputCaptureMotionTriggered'].map(n=>extract(main,n).code).join('\n')).replace('// PRODUCTION_STATUS',main.slice(statusStart,statusEnd));
 assert(cpp.includes('class InputCaptureSensorSink final'));assert(!cpp.includes('// PRODUCTION_'));
 const receipt={schemaVersion:1,canonicalId:'sleep.gyro-power',selection:['gyro-cadence'],status:'RUNNING',sourceHashes:[],runs:[],realDeviceIo:false,normalYmccStarted:false,scope:'Actual native sensor sink, cadence task, PnP scope predicate and runtime linkage; memory device/clock/I/O ports only'};
 const save=()=>{receipt.sourceHashes=[...hashes].map(([path,sha256])=>({path,sha256}));fs.writeFileSync(path.join(out,'cadence-result.json'),JSON.stringify(receipt,null,2)+'\n');};save();
 const vcvars='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat',cmd=path.join(out,'compiler-env.cmd');fs.writeFileSync(cmd,calibrationToolchainPreamble(vcvars)+'set\r\n');
 const setup=spawnSync(process.env.ComSpec||'cmd.exe',['/d','/c',cmd],{encoding:'utf8',windowsHide:true,timeout:30000});assert.equal(setup.status,0,setup.stderr);const env={...process.env};for(const row of setup.stdout.split(/\r?\n/)){const at=row.indexOf('=');if(at>0){const key=row.slice(0,at),actual=Object.keys(env).find(k=>k.toLowerCase()===key.toLowerCase())||key;env[actual]=row.slice(at+1);}}
 function execute(name,source,{define='',expectedFailure='',header=null}={}){
  const dir=path.join(out,name);fs.mkdirSync(dir,{recursive:true});const cppPath=path.join(dir,'consumer.cpp'),exe=path.join(dir,'consumer.exe');fs.writeFileSync(cppPath,source);const includes=[];if(header){bind(header);fs.copyFileSync(header,path.join(dir,'input_shortcut_toggle.h'));includes.push('/I'+dir);}
  const started=Date.now(),args=['/nologo','/std:c++20','/EHsc','/utf-8','/O2','/DNDEBUG','/MD','/permissive-',...define?[define]:[],...includes,'/I'+path.join(repo,'deps/json'),'/I'+path.join(repo,'native'),cppPath,'/Fo'+path.join(dir,'consumer.obj'),'/Fe'+exe,'/link','/INCREMENTAL:NO','WindowsApp.lib'];
  const built=spawnSync('cl.exe',args,{cwd:dir,env,encoding:'utf8',windowsHide:true,timeout:120000});fs.writeFileSync(path.join(dir,'compile.log'),built.stdout+added(built.stderr));assert.equal(built.status,0,name+' compile failed: '+(built.stdout+added(built.stderr)).slice(-4500));
  const ran=spawnSync(exe,[],{cwd:dir,env,encoding:'utf8',windowsHide:true,timeout:30000});fs.writeFileSync(path.join(dir,'run.log'),ran.stdout+added(ran.stderr));let report;try{report=JSON.parse(ran.stdout.trim());}catch{throw Error(name+' no report '+ran.status+' '+ran.stdout);}
  const accepted=expectedFailure?ran.status===1&&report.status==='fail'&&report.failure===expectedFailure:ran.status===0&&report.status==='pass';receipt.runs.push({name,accepted,exitCode:ran.status,report,elapsedMs:Date.now()-started,sourceSha256:sha(source),exeSha256:sha(fs.readFileSync(exe))});save();assert(accepted,name+' unexpected result '+JSON.stringify(report));console.log(name+': '+(expectedFailure?'EXPECTED FAILURE':'PASS')+' ('+report.checks+' assertions)');
 }
 function added(s){return s||'';}
 try{
  execute('current',cpp);
  const oldCap='g_gyroJudgeSegAccum += dSample;';assert.equal(cpp.split(oldCap).length,2);execute('fixed-120Hz-ceiling',cpp.replace(oldCap,'g_gyroJudgeSegAccum += dSample > 120 ? 0 : dSample;'),{define:'/DYMCC_CADENCE_HIGH_RATE_ONLY',expectedFailure:'500Hz source reaches healthy cadence'});
  if(beforeHeader)execute('before-first-enable',cpp,{define:'/DYMCC_CADENCE_ACTIVATION_ONLY',header:beforeHeader,expectedFailure:'first virtual-link enable admits motion without a preset click'});
  for(const [f,hash]of hashes)assert.equal(sha(fs.readFileSync(f)),hash,'source drift '+f);receipt.status='TARGETED_PASS';receipt.finishedAt=new Date().toISOString();save();return receipt;
 }catch(e){receipt.status='FAIL';receipt.error=e.stack||String(e);save();throw e;}
}
