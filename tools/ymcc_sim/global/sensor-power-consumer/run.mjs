import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {extract} from '../../../fixtures/legion_motion_consumer/extract_main.mjs';
import {calibrationToolchainPreamble} from '../../../fixtures/hc_reviewed_calibration/toolchain.mjs';
const moduleDir=path.dirname(fileURLToPath(import.meta.url)),repo=path.resolve(moduleDir,'../../../..');
const out=path.resolve(process.argv[2]||path.join(repo,'../../../Build/Tasks/GyroSleep-20261021/consumer'));
fs.mkdirSync(out,{recursive:true});
const sha=b=>createHash('sha256').update(b).digest('hex');
const source=fs.readFileSync(path.join(repo,'native/main.cpp'),'utf8');
const symbols=['inputCaptureSensorPowerMayRun','inputCaptureSensorStopRequested','inputCaptureUnsubscribeWinRtSensorsImpl','inputCaptureStopWinRtSensorsImpl','inputCaptureResubscribeWinRtSensorsImpl','inputCaptureSignalSensorPowerPause','inputCaptureSensorThreadProc','inputCaptureSensorThreadPostPower','inputCaptureSuspendSensorsForPower','inputCaptureResumeSensorsForPower','inputCaptureSessionDisplayPowerChanged','suspendResumeSubscriptionCallback'];
const bindings=symbols.map(s=>extract(source,s));
const fixture=fs.readFileSync(path.join(moduleDir,'fixture.cpp.in'),'utf8').replace(/^\uFEFF/,'');
const template=fixture.replace('// PRODUCTION_FUNCTIONS',bindings.map(x=>x.code).join('\n\n'));
fs.writeFileSync(path.join(out,'main.snapshot.cpp'),source);
const files=['native/main.cpp','native/sensor_thread_mailbox.h','deps/json/json.hpp','tools/ymcc_sim/global/sensor-power-consumer/fixture.cpp.in','tools/ymcc_sim/global/sensor-power-consumer/run.mjs','tools/fixtures/legion_motion_consumer/extract_main.mjs','tools/lib/hc_controller_motion_fixture.mjs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs'];
const sourceHashes=files.map(p=>({path:p,sha256:sha(fs.readFileSync(path.join(repo,p)))}));
const result={schemaVersion:1,ok:false,status:'RUNNING',sourceHashes,bindings:bindings.map(({code,...b})=>b),runs:[],hardwareIo:false,normalYmccStarted:false};
const save=()=>fs.writeFileSync(path.join(out,'consumer-result.json'),JSON.stringify(result,null,2)+'\n');save();
try {
 const vcvars='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
 const modes=[['current',s=>s,true],['missing-early-power-pause',s=>s.replace('if (type == PBT_APMSUSPEND) inputCaptureSignalSensorPowerPause();','if (type == PBT_APMSUSPEND) {}'),false],['missing-display-off-pause',s=>s.replace('if (state == 0) inputCaptureSuspendSensorsForPower();','if (state == 0) {}'),false],['resume-while-off',s=>s.replace('display != 0 && (phase == PowerLifecycle::Ready','true && (phase == PowerLifecycle::Ready'),false]];
 for(const [name,mutate,expectedPass] of modes){
  const dir=path.join(out,name);fs.mkdirSync(dir,{recursive:true});const code=mutate(template);
  if(name!=='current'&&code===template)throw Error('Fault anchor absent: '+name);
  fs.writeFileSync(path.join(dir,'fixture.cpp'),code);
  const batch=calibrationToolchainPreamble(vcvars)+`cd /d "${dir}"\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /O1 /DUNICODE /D_UNICODE fixture.cpp /I"${path.join(repo,'deps/json')}" /I"${path.join(repo,'native')}" /Fe:fixture.exe /link user32.lib ole32.lib >compile.log 2>&1\r\nexit /b %errorlevel%\r\n`;
  fs.writeFileSync(path.join(dir,'compile.cmd'),batch);
  const c=spawnSync('cmd.exe',['/d','/c',path.join(dir,'compile.cmd')],{encoding:'utf8',timeout:120000});
  if(c.status!==0)throw Error(name+' compile failed: '+fs.readFileSync(path.join(dir,'compile.log'),'utf8').slice(-6000));
  const run=spawnSync(path.join(dir,'fixture.exe'),[],{encoding:'utf8',timeout:30000});
  fs.writeFileSync(path.join(dir,'stdout.json'),run.stdout||'');fs.writeFileSync(path.join(dir,'stderr.txt'),run.stderr||'');
  const report=JSON.parse((run.stdout||'').trim());
  const passed=expectedPass?run.status===0&&report.ok===true:run.status===1&&report.ok===false&&report.cases.some(c=>c.passed===false);
  result.runs.push({name,expectedPass,exitCode:run.status,passed,report,exeSha256:sha(fs.readFileSync(path.join(dir,'fixture.exe')))});save();
  console.log(name,passed?'PASS':'FAIL',report.cases.length,'checks',report.cases.filter(c=>!c.passed).map(c=>c.name));
  if(!passed)throw Error('Unexpected behavior: '+name);
 }
 for(const x of sourceHashes)if(sha(fs.readFileSync(path.join(repo,x.path)))!==x.sha256)throw Error('Source changed during test: '+x.path);
 result.ok=true;result.status='PASS';save();
}catch(e){result.error=e.stack;result.status='FAIL';save();console.error(e.stack);process.exitCode=1;}
