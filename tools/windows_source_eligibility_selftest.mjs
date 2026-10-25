import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {extract} from './fixtures/legion_motion_consumer/extract_main.mjs';
import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';
const runner=fileURLToPath(import.meta.url),repo=path.resolve(path.dirname(runner),'..'),ws=path.resolve(repo,'../../..');
const root=path.join(ws,'Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/WindowsSourceEligibility-20261018/Query');
const control=process.argv[2]||'';
if(!['','--ignore-context','--accept-remapped-frame','--old-data-is-current','--borrow-selected-family','--skip-export-qualification'].includes(control)||process.argv.length>3)throw Error('Unknown control');
const out=path.join(root,new Date().toISOString().replace(/[:.]/g,'-')+(control||'-baseline'));fs.mkdirSync(out,{recursive:true});
const sha=b=>createHash('sha256').update(b).digest('hex');
const result={schema:1,status:'running',control,inputs:[],sourceBindings:[],configurations:[],cacheAndRecipeProducersControlled:true,hardwareIo:false,newProductDlls:[]};
const save=()=>fs.writeFileSync(path.join(out,'RESULT.json'),JSON.stringify(result,null,2)+'\n');save();
const freeze=p=>{const b=fs.readFileSync(p);result.inputs.push({path:p,sha256:sha(b)});return b;};
try{
 const source=freeze(path.join(repo,'native/main.cpp')).toString('utf8');
 let fixture='#define YMCC_FIXTURE_SELECTION_BACKEND_INTEGRATED '+(source.includes('ipc_on("gyro.windowsSourceSelection.request"')?'1':'0')+'\n'+freeze(path.join(repo,'tools/fixtures/windows_source_eligibility/harness.cpp')).toString('utf8');
 const functions=[['PROVIDER','inputCaptureSensorProviderKey'],['SELECTED_META','inputCaptureWindowsProbeSelectedMeta'],['AXES','inputCaptureWindowsSelectionAxes'],['EVIDENCE','inputCaptureWindowsSelectionEvidence'],['QUERY','inputCaptureWindowsSourceEligibilityStatus'],['EXPORT','exportWindowsSensorEvidence']];
 for(const [marker,name] of functions){const entry=extract(source,name);result.sourceBindings.push({...entry,code:undefined});let code=entry.code;
  if(control==='--ignore-context'&&marker==='QUERY'){const start=code.indexOf('const bool stable='),end=code.indexOf(';',start);if(start<0||end<0)throw Error('context anchor');code=code.slice(0,start)+'const bool stable=true'+code.slice(end);}
  if(control==='--borrow-selected-family'&&marker==='QUERY'){const anchor='InputCaptureSensorMeta prospective;prospective.gyroFamily=prospective.accelFamily="winrt";';if(!code.includes(anchor))throw Error('family anchor');code=code.replace(anchor,'InputCaptureSensorMeta prospective=meta;');}
  if(control==='--skip-export-qualification'&&marker==='EXPORT'){const anchor='snapshot["sourceEligibility"]=inputCaptureWindowsSourceEligibilityStatus();';if(!code.includes(anchor))throw Error('export anchor');code=code.replace(anchor,'snapshot["sourceEligibility"]={{"pairs",json::array({{{"canPrepareExplicitModelConfiguration",false}}})}};');}
  fixture=fixture.replace('// @@'+marker+'@@',code);
 }
 const ipc=source.match(/ipc_on\("gyro\.windowsSourceEligibility\.get",\s*\[\]\(const json&\)\s*->\s*json\s*\{\s*return inputCaptureWindowsSourceEligibilityStatus\(\);\s*\}\);/);if(!ipc)throw Error('IPC anchor');result.sourceBindings.push({symbol:'IPC:executedBlock',sha256:sha(ipc[0])});fixture=fixture.replace('// @@IPC@@',ipc[0]);if(fixture.includes('@@'))throw Error('Unresolved fixture marker');
 for(const name of ['windows_source_eligibility.h','windows_sensor_probe.h','sensor_inventory.h','sensor_inventory_cache.h','windows_motion_submission.h'])fs.writeFileSync(path.join(out,name),freeze(path.join(repo,'native',name)));
 fs.writeFileSync(path.join(out,'json.hpp'),freeze(path.join(repo,'deps/json/json.hpp')));
 const mutate=(name,from,to)=>{const p=path.join(out,name),s=fs.readFileSync(p,'utf8');if(!s.includes(from))throw Error('Mutation anchor '+name);fs.writeFileSync(p,s.replace(from,to));};
 if(control==='--accept-remapped-frame')mutate('windows_source_eligibility.h','*gyro.observations.readingTransform==0&&*accel.observations.readingTransform==0','true');
 if(control==='--old-data-is-current')mutate('windows_source_eligibility.h','result.rawCurrent=completeReportedSeries(gyro,evidence.now)&&completeReportedSeries(accel,evidence.now);','result.rawCurrent=result.observedComplete;');
 freeze(runner);freeze(path.join(repo,'tools/fixtures/legion_motion_consumer/extract_main.mjs'));freeze(path.join(repo,'tools/fixtures/hc_reviewed_calibration/toolchain.mjs'));
 fs.writeFileSync(path.join(out,'qualification.cpp'),fixture);
 for(const [name,flags]of [['Release','/O2 /MT /DNDEBUG'],['Checked','/Od /RTC1 /MTd /D_ITERATOR_DEBUG_LEVEL=2']]){
  const dir=path.join(out,name);fs.mkdirSync(dir);const exe=path.join(dir,'qualification.exe'),cmd=path.join(dir,'build.cmd');
  fs.writeFileSync(cmd,calibrationToolchainPreamble('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')+`cl /nologo /std:c++17 /utf-8 /EHsc ${flags} "${path.join(out,'qualification.cpp')}" /Fo"${path.join(dir,'qualification.obj')}" /Fe"${exe}"\r\nexit /b %errorlevel%\r\n`);
  const build=spawnSync('cmd.exe',['/d','/c',cmd],{cwd:dir,windowsHide:true,encoding:'utf8',timeout:60000,maxBuffer:5e6});fs.writeFileSync(path.join(dir,'build.log'),(build.stdout||'')+(build.stderr||''));if(build.status!==0)throw Error('Compile '+name);
  const report=path.join(dir,'REPORT.json'),run=spawnSync(exe,[report],{cwd:dir,windowsHide:true,encoding:'utf8',timeout:30000,maxBuffer:5e6});fs.writeFileSync(path.join(dir,'run.log'),(run.stdout||'')+(run.stderr||'')+(run.error?.stack||''));const data=fs.existsSync(report)?JSON.parse(fs.readFileSync(report,'utf8')):null;result.configurations.push({name,exitCode:run.status,report:data});save();
  if(control){if(run.status!==1||!data?.failures?.length||data.failures.some(x=>!x.startsWith('query/')))throw Error('Control must fail query assertions');}
  else if(run.status!==0||data?.status!=='pass')throw Error('Assertions '+name);
 }
 result.inputIntegrity=result.inputs.map(x=>({...x,unchanged:sha(fs.readFileSync(x.path))===x.sha256}));if(result.inputIntegrity.some(x=>!x.unchanged&&x.path!==path.join(repo,'native/main.cpp')))throw Error('Input drift');
 const latest=fs.readFileSync(path.join(repo,'native/main.cpp'),'utf8');for(const entry of result.sourceBindings.filter(x=>!x.symbol.includes(':')))if(extract(latest,entry.symbol).sha256!==entry.sha256)throw Error('Bound method drift '+entry.symbol);
 result.status=control?'negative-control-detected':'pass';save();console.log(JSON.stringify({status:result.status,out,configurations:result.configurations.map(x=>({name:x.name,checks:x.report?.checks,failures:x.report?.failures}))},null,2));
}catch(e){result.status='fail';result.error=e.stack;save();console.error(e.stack);console.error(out);process.exitCode=1;}
