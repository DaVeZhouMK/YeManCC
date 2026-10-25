import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';
import assert from 'node:assert/strict';
import {extract} from './fixtures/legion_motion_consumer/extract_main.mjs';
import {closeBrace} from './fixtures/hc_remaining_model_capabilities/expectations.mjs';
import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';

const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const root=path.resolve(repo,'../../../Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion');
const snapshot=path.join(root,'GenericWindowsMotion-20261018/2026-10-18T06-31-57-098Z-47204-baseline');
const ingress=path.join(root,'GenericWindowsSdkIngress-20261018/2026-10-18T07-09-42-791Z-29724-baseline');
const control=process.argv[2]||'';
assert.ok(['','--rewrite-sdk-sequence','--skip-owner-process','--skip-owner-retirement'].includes(control));
const out=path.join(root,'WindowsSdkOwner-20261018',new Date().toISOString().replace(/[:.]/g,'-')+'-'+process.pid+'-'+(control||'baseline'));
fs.mkdirSync(out,{recursive:true});
const sha=b=>createHash('sha256').update(b).digest('hex');
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const inputs=[],freeze=p=>{const b=fs.readFileSync(p);inputs.push({path:p,sha256:sha(b)});return b;};
const result={schema:1,status:'running',control,inputs,bindings:[],configurations:[],newProductionDlls:[],productionChanges:[],hardwareIo:false,normalAppStarted:false};
const save=()=>fs.writeFileSync(path.join(out,'RESULT.json'),JSON.stringify(result,null,2)+'\n');
function once(text,from,to){assert.equal(text.split(from).length,2,'Expected one anchor: '+from.slice(0,90));return text.replace(from,()=>to);}
function synthetic(source){
 const pad=extract(source,'inputCaptureOnGamepad').code,normal=pad.replace(/\r\n/g,'\n');
 const start=normal.indexOf('    ymccResolveMachineIdentityOnce();'),end=normal.indexOf('    const auto capabilityPower =',start);
 const meta=normal.indexOf('    InputCaptureSensorMeta sensorMeta;'),metaEnd=normal.indexOf('    const bool sameProvider',meta);
 const pair=pad.indexOf('    const bool pairProven ='),pairEnd=pad.indexOf('    if (g_realStickTest.enabled && !g_realStickTestAdmissionLogged)',pair);
 const proc=pad.indexOf('    if (g_gm && g_gmProcess && g_gmGetCal && gyro && accel &&'),finish='        g_gmProcessedThisTick = windowsFrameProcessed;',procEnd=pad.indexOf(finish,proc);
 const cal=source.indexOf('            const bool reviewed=inputCaptureSourceQualifiedCalibrationRecipe()!=nullptr;'),calStart=source.lastIndexOf('        if (!controllerMotionRoute) {',cal),calOpen=source.indexOf('{',calStart);
 assert.ok([start,end,meta,metaEnd,pair,pairEnd,proc,procEnd,cal,calStart,calOpen].every(x=>x>=0));
 return {
  'actual-bus-source-prefix':normal.slice(start,end),'actual-bus-metadata':normal.slice(meta,metaEnd),
  'actual-bus-admission-and-process':pad.slice(pair,pairEnd)+'\n    if (!controllerMotionRoute && windowsProcessorOwnerReady) inputCaptureGyroCalibrationRestoreOnce();\n    g_gmProcessedThisTick = false;\n'+pad.slice(proc,procEnd+finish.length)+'\n    }\n',
  'actual-bus-calibration-tick':source.slice(calStart,closeBrace(source,calOpen)+1),
  'actual-bus-output-gates':['const bool motionPairProven=','const bool gyroSubmitAllowed ='].map(mark=>{const at=pad.indexOf(mark);assert.ok(at>=0);return pad.slice(at,pad.indexOf(';',at)+1);}).join('\n'),
  'actual-bus-dispatch-condition':extract(source,'inputCaptureBusTick').code.match(/if\s*\(g_inputCaptureReady && !inputHostLifecycleBusy\(\)\)/)?.[0]
 };
}
try{
 freeze(fileURLToPath(import.meta.url));freeze(path.join(repo,'tools/fixtures/hc_generic_windows/sdk_owner_cases.cpp'));
 for(const helper of ['tools/fixtures/legion_motion_consumer/extract_main.mjs','tools/fixtures/hc_remaining_model_capabilities/expectations.mjs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs'])freeze(path.join(repo,helper));
 const source=freeze(path.join(repo,'native/main.cpp')).toString('utf8');
 const prior=JSON.parse(freeze(path.join(snapshot,'RESULT.json')).toString('utf8'));assert.equal(prior.status,'pass');
 const sourcePieces=synthetic(source);
 for(const b of prior.bindings){const code=b.symbol.startsWith('actual-bus-')?sourcePieces[b.symbol]:extract(source,b.symbol).code;assert.equal(sha(code),b.sha256,'Live owner source drift: '+b.symbol);result.bindings.push({symbol:b.symbol,sha256:b.sha256});}
 const t=source.indexOf('// Private typed capture for an explicit-source transaction.'),v=source.indexOf('// A choice guard is present before the first selection.'),e=source.indexOf('static DWORD WINAPI inputCaptureSensorThreadProc',v);
 result.sections=[{name:'native-transport',sha256:sha(source.slice(t,v))},{name:'native-view',sha256:sha(source.slice(v,e))}];
 assert.deepEqual(result.sections,prior.sections);
 let base=freeze(path.join(snapshot,'combined.cpp')).toString('utf8');base=base.slice(0,base.indexOf('int main(int argc,char**argv)'));
 for(const entry of fs.readdirSync(snapshot,{withFileTypes:true}))if(entry.isFile()&&/\.(h|hpp|inc)$/.test(entry.name)){
  const expected=freeze(path.join(snapshot,entry.name));
  const liveName=entry.name.replace('.snapshot.h','.h'),live=path.join(repo,entry.name==='json.hpp'?'deps/json':'native',liveName);
  if(fs.existsSync(live))assert.equal(sha(expected),sha(freeze(live)),'Current header differs: '+entry.name);
  fs.writeFileSync(path.join(out,entry.name),expected);
 }
 // Verify retained source bodies, including the expanded actual native owner include.
 let candidate=base.slice(base.indexOf('namespace candidate {'));
 const expanded=candidate.replace('#include "windows_source_owner_native.inc"',fs.readFileSync(path.join(out,'windows_source_owner_native.inc'),'utf8'));
 for(const b of prior.bindings.filter(b=>!b.symbol.startsWith('actual-bus-'))){
  let retained;
  try{retained=extract(expanded,b.symbol).code;}catch(error){
   if(!['inputCaptureLoadCalibration','inputCaptureReviewedCalibrationFinish'].includes(b.symbol))throw error;
  }
  if(b.symbol==='inputHostLifecycleBusy'){
   assert.equal(retained,'static bool inputHostLifecycleBusy(){return fixtureHostBusy;}');
   result.hostLifecycleStateControlled=true;continue;
  }
  const actual=extract(source,b.symbol).code.replace(/\r\n/g,'\n');
  if(retained?.replace(/\r\n/g,'\n')!==actual){retained=extract(expanded,'fixtureActual_'+b.symbol).code.replace('fixtureActual_'+b.symbol,b.symbol);}
  assert.equal(retained.replace(/\r\n/g,'\n'),actual,'Retained body differs: '+b.symbol);
 }
 const classAt=source.indexOf('class InputCaptureSensorSink final {'),classOpen=source.indexOf('{',classAt),classEnd=closeBrace(source,classOpen)+2;
 assert.ok(classAt>=0);const originalSink=source.slice(classAt,classEnd);let sink=originalSink;
 for(const name of ['OnGyro','OnAccel']){const start=sink.indexOf('    void '+name+'('),open=sink.indexOf('{',start),end=closeBrace(sink,open)+1;assert.ok(start>=0);sink=sink.slice(0,start)+sink.slice(end);}
 sink=sink.replace('class InputCaptureSensorSink final','class MockBindingSink final');
 const constants=['kInputCaptureHighMagDps','kInputCaptureHighMagQuietWindowMs','kInputCaptureAccelMotionDeltaG'].map(name=>{const m=source.match(new RegExp('^constexpr[^\\n;]*\\b'+name+'\\s*=[^;]*;','m'));assert.ok(m,name);return m[0];});
 const helper=extract(source,'hcSinkTimeOfDayMs');
 const oldSink=candidate.match(/^struct MockBindingSink[^\r\n]+/m)?.[0];assert.ok(oldSink);
 candidate=once(candidate,oldSink,constants.join('\n')+'\nstatic double hcSinkTimeOfDayMs(UINT64 ticks100ns) noexcept;\n'+sink);
 candidate=once(candidate,'static double hcSinkTimeOfDayMs(std::uint64_t ts){return double(ts/10000);}',helper.code);
 const at=base.indexOf('namespace candidate {');base=base.slice(0,at)+candidate;
 result.sinkBinding={symbol:'InputCaptureSensorSink',sha256:sha(originalSink),rawProjectionSha256:sha(sink),typedWrappersOmitted:['OnGyro','OnAccel'],constants:constants.map(code=>({code,sha256:sha(code)}))};
 result.bindings.push({symbol:helper.symbol,sha256:helper.sha256});
 let transport=freeze(path.join(repo,'tools/msi_sensor_adapter_selftest.cpp')).toString('utf8');transport=transport.slice(transport.indexOf('using namespace ymcc::msi_sensor;'),transport.indexOf('int main()'));
 transport=once(transport,'if(!IsEqualGUID(key.fmtid,kClawHidMotion)||key.pid<7||key.pid>9||(state->missingAxis&&key.pid==9))return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);','const DWORD first=kind==Kind::Gyro?10:2;if(!IsEqualGUID(key.fmtid,kSdkMotion)||key.pid<first||key.pid>first+2||(state->missingAxis&&key.pid==first+2))return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);');
 transport=once(transport,'std::vector<Sample> received;','std::vector<Sample> received,accepted;');
 transport=once(transport,'out->dblVal=static_cast<double>((kind==Kind::Gyro?10:100)+key.pid);','out->dblVal=kind==Kind::Gyro?static_cast<double>(key.pid-first+1):std::array<double,3>{0.25,-0.5,1}[key.pid-first];');
 transport=once(transport,'*out=GUID{};return S_OK;}// EX-style custom type filtered by WinRT.','*out=kind==Kind::Gyro?kGyroType:kAccelType;return S_OK;}// Standard SDK type.');
 transport=once(transport,'for(DWORD p=7;p<=9;++p)keys->keys.push_back(PROPERTYKEY{kClawHidMotion,p});','const DWORD first=kind==Kind::Gyro?10:2;for(DWORD p=first;p<=first+2;++p)keys->keys.push_back(PROPERTYKEY{kSdkMotion,p});');
 transport=once(transport,'*out=IsEqualGUID(key.fmtid,kClawHidMotion)&&key.pid>=7&&key.pid<=9?VARIANT_TRUE:VARIANT_FALSE;','const DWORD first=kind==Kind::Gyro?10:2;*out=IsEqualGUID(key.fmtid,kSdkMotion)&&key.pid>=first&&key.pid<=first+2?VARIANT_TRUE:VARIANT_FALSE;');
 const fallbackNames=['inputCaptureStopHcWindowsSensorApi','inputCaptureStartHcWindowsSensorApi'];
 const fallbacks=fallbackNames.map(n=>{const b=extract(source,n);result.bindings.push({symbol:b.symbol,sha256:b.sha256});return b.code;}).join('\n\n');
 const oracle=JSON.parse(freeze(path.join(ingress,'expectations.json')).toString('utf8'));
 const eligible=JSON.parse(freeze(path.join(ingress,'Release/REPORT.json')).toString('utf8')).models;
 const rows=[...oracle.identities,...oracle.legacy.cases],cases=[];
 for(const model of eligible){
  const generic=oracle.genericIngress.find(r=>r.expectedGenericClass===model),legacy=oracle.legacy.recipes.find(r=>r.deviceClass===model),newer=oracle.models.find(r=>r.fullName===model);
  const row=generic||rows.find(r=>(r.hcClass||('HandheldCompanion.Devices.'+r.deviceClass))===model)||rows.find(r=>legacy?.identityInputs.includes(r.id));assert.ok(row,'No independent DMI '+model);
  let axes;
  if(generic)axes=model.endsWith('.DefaultDevice')?[1,3,2,0.25,1,-0.5]:[1,2,3,0.25,-0.5,1];
  else if(oracle.reviewedDefaultModelClasses.includes(model))axes=[1,2,3,0.25,-0.5,1];
  else {const recipe=legacy?.selectedRecipe||legacy||newer;assert.ok(recipe?.matrices,model);axes=recipe.matrices.flatMap(m=>oracle.expectedSdkProcess[m.id]);assert.equal(axes.length,6,model);}
  cases.push({model,dmi:row,mapped:axes,generic:!!generic});
 }
 assert.equal(cases.length,56);fs.writeFileSync(path.join(out,'expectations.json'),JSON.stringify({cases},null,2));
 const harness=freeze(path.join(repo,'tools/fixtures/hc_generic_windows/sdk_owner_cases.cpp')).toString('utf8').replace('// @@ACTUAL_FALLBACK@@',fallbacks);
 const preamble='#define _WIN32_WINNT 0x0A00\n#define NOMINMAX\n#define YMCC_FAULT_INJECT\n#include <initguid.h>\n#include "msi_legacy_sensor.h"\n#undef HKEY_LOCAL_MACHINE\n#undef near\n#include <mutex>\n#include <array>\n#include <thread>\n#include <set>\n';
 let code=preamble+transport+'\n'+base+'\n'+harness;
 const mutations={
  '--rewrite-sdk-sequence':[['++gyroSequence;','gyroSequence=1;'],['++accelSequence;','accelSequence=1;']],
  '--skip-owner-process':[['g_gmProcess(g_gm,gx,gy,gz,ax,ay,az,delta);','/* intentional omission of real owner ProcessMotion */']],
  '--skip-owner-retirement':[['owner.lease->store(false,std::memory_order_release);','/* intentional omission of old CPU lease retirement */']]
 };
 for(const [from,to]of mutations[control]||[])code=once(code,from,to);
 fs.writeFileSync(path.join(out,'combined.cpp'),code);fs.writeFileSync(path.join(out,'msi_legacy_sensor.h'),freeze(path.join(repo,'native/msi_legacy_sensor.h')));
 const dll=path.join(repo,'deps/hc-runtime-upstream/HC-CANDIDATE-0.32.4.0-06c0b954-20260902/GamepadMotion.dll');assert.equal(sha(freeze(dll)),'e11d59f84b4a4a12e99b15ea998e79de06c6dfb8f1e9c637e973abd4d3d088a9');
 for(const [name,flags]of [['Release','/O2 /MT /DNDEBUG'],['Checked','/Od /RTC1 /MTd']]){
  const dir=path.join(out,name);fs.mkdirSync(dir);const script=path.join(dir,'build.cmd');
  fs.writeFileSync(script,calibrationToolchainPreamble('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')+`cl /nologo /std:c++20 /utf-8 /EHsc ${flags} "${path.join(out,'combined.cpp')}" /Fo"${path.join(dir,'combined.obj')}" /Fe"${path.join(dir,'combined.exe')}" /link ole32.lib oleaut32.lib\r\nexit /b %errorlevel%\r\n`);
  const build=spawnSync('cmd.exe',['/d','/c',script],{cwd:dir,windowsHide:true,encoding:'utf8',timeout:90000,maxBuffer:8e6});fs.writeFileSync(path.join(dir,'build.log'),(build.stdout||'')+(build.stderr||''));assert.equal(build.status,0,'Build '+name);
  const reportPath=path.join(dir,'REPORT.json'),run=spawnSync(path.join(dir,'combined.exe'),[path.join(out,'expectations.json'),dll,reportPath],{cwd:dir,windowsHide:true,env:{...process.env,LOCALAPPDATA:path.join(dir,'isolated-appdata')},encoding:'utf8',timeout:90000,maxBuffer:8e6});
  fs.writeFileSync(path.join(dir,'run.log'),(run.stdout||'')+(run.stderr||'')+(run.error?.stack||''));const report=fs.existsSync(reportPath)?read(reportPath):null;
  result.configurations.push({name,exitCode:run.status,report});save();assert.equal(run.status,control?1:0,'Run '+name);assert.ok(report);assert.ok(!report.failures.some(f=>String(f.label).startsWith('fixture exception:')));if(control)assert.ok(report.failures.length>0);else assert.equal(report.status,'pass');
  console.log(name+': '+report.status+' '+report.checks+' checks / '+report.failures.length+' failures; '+report.models.length+' models');
 }
 const live=fs.readFileSync(path.join(repo,'native/main.cpp'),'utf8'),pieces=synthetic(live);
 for(const b of result.bindings)assert.equal(sha(b.symbol.startsWith('actual-bus-')?pieces[b.symbol]:extract(live,b.symbol).code),b.sha256,'Bound production drift '+b.symbol);
 const liveAt=live.indexOf('class InputCaptureSensorSink final {'),liveOpen=live.indexOf('{',liveAt);assert.equal(sha(live.slice(liveAt,closeBrace(live,liveOpen)+2)),result.sinkBinding.sha256);
 const changed=inputs.filter(i=>sha(fs.readFileSync(i.path))!==i.sha256);assert.ok(changed.every(i=>i.path===path.join(repo,'native/main.cpp')),'Non-native input changed');
 result.boundProductionCurrent=true;result.wholeMainUnchanged=changed.length===0;result.status=control?'expected-failure':'pass';result.generatedSourceSha256=sha(Buffer.from(code));save();console.log('Evidence: '+out);
}catch(error){result.status='fail';result.error=error.stack;save();console.error(error.stack);console.error(out);process.exitCode=1;}