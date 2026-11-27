// Compile verbatim Native trigger/contribution/merge/wire consumers. No installed app/device IO.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {extract} from './fixtures/legion_motion_consumer/extract_main.mjs';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const args=process.argv.slice(2);assert(args.length===2&&args[0]==='--out','Usage: node tools/gyro_steam_handoff_selftest.mjs --out fresh-directory');
const out=path.resolve(args[1]);fs.mkdirSync(out,{recursive:false});
const hash=b=>createHash('sha256').update(b).digest('hex');
const sources=[];
function read(rel){const p=path.join(repo,rel),b=fs.readFileSync(p);sources.push({path:p,sha256:hash(b)});return b.toString('utf8');}
const main=read('native/main.cpp'),virtual=read('native/virtual_controller_ui_output_runtime.h');
const harness=read('tools/fixtures/gyro_steam_handoff/harness.cpp');
read('native/input_shortcut_toggle.h');read('native/input_startup_policy.h');read('tools/gyro_steam_handoff_selftest.mjs');
const receipt={status:'RUNNING',startedAt:new Date().toISOString(),scope:'Verbatim native owner selection, mapping reset, trigger, contribution, blend and frame assembly; source/Host/physical freshness supplied as memory facts; not a full Native build or device qualification',sources,runs:[],deviceIo:false,startedYmcc:false};
function save(){fs.writeFileSync(path.join(out,'RESULT.json'),JSON.stringify(receipt,null,2)+'\n');}save();
function section(text,start,end){const at=text.indexOf(start);assert(at>=0,start);assert.equal(text.indexOf(start,at+start.length),-1,'ambiguous '+start);const last=text.indexOf(end,at);assert(last>at,end);return text.slice(at,last);}
function match(re,text){const found=[...text.matchAll(re)];assert.equal(found.length,1,String(re));return found[0][0];}
const refresh=extract(main,'inputCaptureRefreshRealStickSettings').code;
const readString=section(refresh,'    const auto readString =','    const auto readBool =');
const action=match(/    const bool steamControlled =[^;]+;\s+const bool nextMotionActionEnabled =[^;]+;\s+const bool steamControlChanged =[^;]+;/g,refresh)+'\n'+match(/    const bool nextMotionEnabled =[^;]+;/g,refresh);
const reset=section(refresh,'    if (targetChanged || virtualTargetChanged || targetGyroChanged || motionActionChanged ||','    // ── 背键映射 Phase A');
const imu=match(/ymcc::controllerPersona::motionTouchOutput\(inputHostPersona\(\)\) && gyroSubmitAllowed &&[\s\S]*?std::isfinite\(orientationZ\)\)\)/g,main);
const fields=section(main,'    frameContent.imuAdmitted = imuAdmitted;','    // 背键屏蔽同样经生产函数');
const ack=section(main,'                    const bool sensorCurrent=','                        ((frameSubmission.controllerMotion')+'true;';
const types=['RealStickTestConfig','MotionSensorFrame','MotionContribution','MergedVirtualPadFrame'].map(n=>extract(main,n,'struct'));
const funcs=['ascii_lower','trim_ascii','inputCaptureSteamControlsMotion','inputCaptureMotionTriggerPressed','inputCaptureMotionTriggered','inputHostClampShort','inputCaptureHcSteering','inputCaptureHcSteeringAxisResolvesYaw','inputCaptureApplyCustomSensitivity','inputCaptureProcessMotionContribution','inputHostApplyAxisModifiers','inputHostNormalizeAxis'].map(n=>extract(main,n));
const blends=['FocusStickBlend','FocusFrameContent'].map(n=>extract(virtual,n,'struct')).concat(['focusIsolationBlendStick','focusIsolationAssembleFrame'].map(n=>extract(virtual,n)));
let cpp=harness.replace('// PRODUCTION_CONFIG',types.map(x=>x.code).join('\n')).replace('// PRODUCTION_FUNCTIONS',funcs.map(x=>x.code).join('\n')).replace('// PRODUCTION_BLEND',blends.map(x=>x.code).join('\n')).replace('// PRODUCTION_READ_STRING',readString).replace('// PRODUCTION_ACTION',action).replace('// PRODUCTION_RESET',reset).replace('// PRODUCTION_IMU_ADMISSION',imu).replace('// PRODUCTION_ACK',ack).replace('// PRODUCTION_IMU_FIELDS',fields);
assert(!cpp.includes('// PRODUCTION_'));
receipt.extracted=types.concat(funcs,blends).map(({code,...x})=>x);save();
const envCmd=path.join(out,'compiler-env.cmd');fs.writeFileSync(envCmd,'@call "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat" >nul\r\n@set\r\n');
const setup=spawnSync(process.env.ComSpec||'cmd.exe',['/d','/c',envCmd],{encoding:'utf8',windowsHide:true,timeout:30000});assert.equal(setup.status,0,setup.stderr);
const env={...process.env};for(const row of setup.stdout.split(/\r?\n/)){const i=row.indexOf('=');if(i>0){const key=row.slice(0,i),actual=Object.keys(env).find(k=>k.toLowerCase()===key.toLowerCase())||key;env[actual]=row.slice(i+1);}}
function execute(name,text,expectedFailure=''){
 const dir=path.join(out,name);fs.mkdirSync(dir);const source=path.join(dir,'test.cpp'),exe=path.join(dir,'test.exe');fs.writeFileSync(source,text);
 const built=spawnSync('cl.exe',['/nologo','/std:c++20','/EHsc','/utf-8','/O2','/DNDEBUG','/MD','/permissive-','/I'+path.join(repo,'deps/json'),'/I'+path.join(repo,'native'),source,'/Fo'+path.join(dir,'test.obj'),'/Fe'+exe,'/link','/INCREMENTAL:NO'],{cwd:dir,env,encoding:'utf8',windowsHide:true,timeout:60000});fs.writeFileSync(path.join(dir,'compile.log'),(built.stdout||'')+(built.stderr||''));assert.equal(built.status,0,name+' compile failed '+built.stdout);
 const ran=spawnSync(exe,[],{cwd:dir,env,encoding:'utf8',windowsHide:true,timeout:15000});fs.writeFileSync(path.join(dir,'run.log'),(ran.stdout||'')+(ran.stderr||''));const report=JSON.parse(ran.stdout.trim());const accepted=expectedFailure?ran.status===1&&report.failure===expectedFailure:ran.status===0&&report.status==='pass';receipt.runs.push({name,accepted,report,sourceSha256:hash(text),exeSha256:hash(fs.readFileSync(exe))});save();assert(accepted,name+' '+JSON.stringify(report));console.log(name+': '+(expectedFailure?'EXPECTED FAILURE':'PASS')+' ('+report.checks+' checks)');
}
function once(s,a,b){assert.equal(s.split(a).length,2,'mutation must be unique');return s.replace(a,b);}
try{
 execute('current',cpp);
 execute('legacy-local-trigger',once(cpp,'g_realStickTest.steamControlled || !g_realStickTest.motionActionEnabled','!g_realStickTest.motionActionEnabled'),'Steam bypasses all local trigger modes');
 execute('legacy-local-contribution',once(cpp,'!g_realStickTest.steamControlled && g_realStickTest.motionActionEnabled','g_realStickTest.motionActionEnabled'),'Steam cannot produce a local motion contribution');
 for(const s of sources)assert.equal(hash(fs.readFileSync(s.path)),s.sha256,'source drift: '+s.path);
 receipt.status='PASS';receipt.finishedAt=new Date().toISOString();save();console.log('Evidence: '+out);
}catch(e){receipt.status='FAIL';receipt.error=e.stack||String(e);save();throw e;}
