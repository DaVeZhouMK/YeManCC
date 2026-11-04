// One extension of controller.unified-motion-providers; no new ID or parent.
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {extract} from '../legion_motion_consumer/extract_main.mjs';
import {buildControllerMotionArtifacts} from '../../lib/hc_controller_motion_catalog.mjs';
const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
export const activationInputs=[
 'tools/fixtures/unified_controller_motion/activation.cpp','tools/fixtures/unified_controller_motion/activation.mjs',
 'tools/fixtures/legion_motion_consumer/extract_main.mjs','tools/lib/hc_controller_motion_catalog.mjs',
 'native/legion_motion_activation.h','native/legion_motion_activation_win32.h','native/controller_motion_processor.h',
 'capabilities/HcControllerMotionCapabilityCatalog.h','capabilities/HcControllerMotionCapabilityCatalog.generated.h',
 'capabilities/hc-device-catalog.generated.h','capabilities/hc-device-catalog.json','capabilities/hc-controller-motion-policy.json','deps/json/json.hpp',
 ...['IDevice','Lenovo/LegionGo','Lenovo/LegionGoTablet','Lenovo/LegionGoTablet2','Lenovo/LegionGoSZ1','Lenovo/LegionGoSZ2'].map(name=>'deps/handheldcompanion-runtime/source/Devices/'+name+'.cs'),
];
const functions=['ascii_lower','legionBackIsPid','legionSBackIsPid','legionMotionModelBinding','legionMotionHardwareAdmitted','legionMotionIdentityAdmitted','legionMotionStableIdentity','legionMotionSelectedSensorIndex','inputCaptureControllerCalibrationIntentCurrent','legionMotionActivationStop','legionMotionActivationTick'];
const types=['HidHidePhysicalIdentity','OemHidReadSession','LegionBackSession','LegionSBackSession','ControllerCalibrationRequest'];
export function runLegionActivationConsumer({repo,out,env}){
 const started=performance.now();fs.mkdirSync(out,{recursive:true});
 const source=path.join(out,'source'),inputs=activationInputs.map(file=>{
  const bytes=fs.readFileSync(path.join(repo,file)),copy=path.join(source,file);fs.mkdirSync(path.dirname(copy),{recursive:true});fs.writeFileSync(copy,bytes);return{file,sha256:sha(bytes),bytes:bytes.length};
 });
 const main=fs.readFileSync(path.join(repo,'native/main.cpp'),'utf8');
 const bindings=[...types.map(name=>extract(main,name,'struct')),...functions.map(name=>extract(main,name))];
 const pidNames=['kLegionBackPids','kLegionSPids'],pids=pidNames.map(name=>{
  const all=[...main.matchAll(new RegExp('^static const USHORT '+name+'\\[\\] = \\{[^}]+\\};','gm'))];if(all.length!==1)throw Error('ACTIVATION_PID_DECLARATION:'+name);return all[0][0];
 });
 const anchors=['legionBackBind','legionSBackBind','legionBackPump','legionSBackPump','legionBackClose','legionSBackClose','legionMotionResolveActivationIdentity','inputCaptureRetireResources','exportControllerMotionEvidence'].map(name=>extract(main,name));
 for(const prefix of ['legionBack','legionSBack']){
  if(!anchors.find(x=>x.symbol===prefix+'Bind').code.includes('caps.OutputReportByteLength')||!anchors.find(x=>x.symbol===prefix+'Bind').code.includes('legionMotionResolveActivationIdentity(found, &data, set)')||!anchors.find(x=>x.symbol===prefix+'Pump').code.includes('legionMotionActivationTick(g_'+prefix+')')||!anchors.find(x=>x.symbol===prefix+'Close').code.includes('legionMotionActivationStop(g_'+prefix+')'))throw Error('ACTIVATION_OWNER_WIRE:'+prefix);
 }
 const retirement=anchors.find(x=>x.symbol==='inputCaptureRetireResources').code;
 if(!retirement.includes('legionMotionActivationStop(g_legionBack)')||!retirement.includes('legionMotionActivationStop(g_legionSBack)')||!retirement.includes('g_legionSBack.activationOutstanding.load'))throw Error('ACTIVATION_RETIREMENT_DEBT');
 fs.writeFileSync(path.join(source,'production-types.h'),bindings.filter(x=>x.kind==='struct').map(x=>x.code).join('\n'));
 fs.writeFileSync(path.join(source,'production-functions.h'),pids.join('\n')+'\n'+bindings.filter(x=>x.kind==='function').map(x=>x.code).join('\n'));
 fs.copyFileSync(path.join(source,'tools/fixtures/unified_controller_motion/activation.cpp'),path.join(source,'activation.cpp'));
 const compilerName=Object.keys(env).find(k=>k.toLowerCase()==='vctoolsinstalldir');
 let compiler=compilerName?path.join(env[compilerName],'bin/Hostx64/x64/cl.exe'):null;
 if(!compiler||!fs.existsSync(compiler)){
  const locate=spawnSync(path.join(env.SystemRoot||'C:/Windows','System32/where.exe'),['cl.exe'],{env,windowsHide:true,encoding:'utf8',timeout:10000});
  compiler=String(locate.stdout||'').trim().split(/\r?\n/).find(file=>fs.existsSync(file));
 }
 if(!compiler||!fs.existsSync(compiler))throw Error('ACTIVATION_COMPILER_IDENTITY_MISSING');
 const configurations=[['Release',['/O2','/DNDEBUG','/MT']],['Checked',['/Od','/RTC1','/MTd']]];
 const result={schemaVersion:1,status:'RUNNING',scope:'Actual Lenovo owner initialization functions/types, actual Win32Port and library recipes; controlled OS I/O. Native bind/close/pump/retirement/export remain source-wire checks.',actualHardware:false,models:['LegionGoTablet','LegionGoTablet2','LegionGoSZ1','LegionGoSZ2'],inputs,bindings:bindings.map(({code,...x})=>x),anchors:anchors.map(({code,...x})=>x),compiler:{path:compiler,sha256:sha(fs.readFileSync(compiler))},runs:[],sourceStability:null};
 const save=()=>fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2)+'\n');save();
 const compileRun=(label,root,negative)=>{
  for(const[configuration,flags]of configurations){
   const dir=path.join(out,label,configuration);fs.mkdirSync(dir,{recursive:true});const executable=path.join(dir,'activation.exe'),before=performance.now();
   const c=spawnSync(compiler,['/nologo','/std:c++20','/utf-8','/EHsc','/DUNICODE','/D_UNICODE',...flags,'/I'+root,path.join(root,'activation.cpp'),'/Fe'+executable,'/Fo'+path.join(dir,'activation.obj'),'/Fd'+path.join(dir,'activation.pdb')],{cwd:dir,env,windowsHide:true,encoding:'utf8',timeout:45000,maxBuffer:4e6});
   fs.writeFileSync(path.join(dir,'compile.log'),(c.stdout||'')+(c.stderr||'')+(c.error?.stack||''));
   if(c.error||c.status!==0||c.signal)throw Error('ACTIVATION_COMPILE:'+label+':'+configuration);
   const compileMs=performance.now()-before,runStart=performance.now(),r=spawnSync(executable,[],{cwd:dir,env,windowsHide:true,encoding:'utf8',timeout:10000,maxBuffer:4e6});
   fs.writeFileSync(path.join(dir,'run.log'),(r.stdout||'')+(r.stderr||'')+(r.error?.stack||''));
   const summary=/LEGION_ACTIVATION_COMPLETED models=(\d+) checks=(\d+) failures=(\d+)/.exec(r.stdout||'');
   const cases=[...(r.stdout||'').matchAll(/^CASE family-(\w+) checks=(\d+) failures=(\d+)\r?$/gm)].map(m=>({model:m[1],checks:+m[2],failures:+m[3]}));
   if(r.error||r.signal||!summary||cases.length!==4||JSON.stringify(cases.map(x=>x.model))!==JSON.stringify(result.models)||r.status!==(negative?1:0)||(+summary[3]>0)!==negative)throw Error('ACTIVATION_SEMANTIC_RESULT:'+label+':'+configuration);
   result.runs.push({label,configuration,exitCode:r.status,checks:+summary[2],failures:+summary[3],cases,compileMs,runMs:performance.now()-runStart,executable:{path:executable,sha256:sha(fs.readFileSync(executable))}});save();
  }
 };
 try{
  const catalog=JSON.parse(fs.readFileSync(path.join(repo,'capabilities/hc-device-catalog.json'),'utf8')),built=buildControllerMotionArtifacts({repo,catalog});
  if(JSON.stringify(built.metadata)!==JSON.stringify(catalog.domains.controllerMotion))throw Error('ACTIVATION_METADATA_DRIFT');
  for(const[file,text]of Object.entries(built.artifacts))if(fs.readFileSync(path.join(repo,file),'utf8')!==text)throw Error('ACTIVATION_GENERATED_DRIFT:'+file);
  compileRun('current',source,false);
  const faults=[['omitted-initialization','native/legion_motion_activation.h','const auto result = port.submit(recipe.commands[index_]);','const auto result = Completion::Success;'],
   ['gos-report-id-omitted','capabilities/HcControllerMotionCapabilityCatalog.generated.h','ActivationCommand{{0, 4, 7, 1, 0, 0, 0}, 4}','ActivationCommand{{4, 7, 1, 0, 0, 0, 0}, 4}']];
  for(const[label,file,from,to]of faults){const root=path.join(out,'fault-source',label);fs.cpSync(source,root,{recursive:true});const target=path.join(root,file),text=fs.readFileSync(target,'utf8');if(text.split(from).length!==2)throw Error('ACTIVATION_FAULT_ANCHOR:'+label);fs.writeFileSync(target,text.replace(from,to));compileRun(label,root,true);}
  const current=fs.readFileSync(path.join(repo,'native/main.cpp'),'utf8');
  result.sourceStability={inputs:inputs.every(x=>sha(fs.readFileSync(path.join(repo,x.file)))===x.sha256),methods:bindings.every(x=>extract(current,x.symbol,x.kind).sha256===x.sha256),anchors:anchors.every(x=>extract(current,x.symbol).sha256===x.sha256),fullMainChanged:sha(current)!==sha(main)};
  if(!result.sourceStability.inputs||!result.sourceStability.methods||!result.sourceStability.anchors)throw Error('ACTIVATION_SOURCE_DRIFT');
  result.status='PASS';result.wallMs=performance.now()-started;save();return{path:path.join(out,'result.json'),sha256:sha(fs.readFileSync(path.join(out,'result.json'))),...result};
 }catch(error){result.status='FAIL';result.error=error.stack;result.wallMs=performance.now()-started;save();throw error;}
}
