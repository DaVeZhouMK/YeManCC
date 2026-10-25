import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';
import {snapshotControllerInputFixture,verifyControllerInputFixture} from './lib/hc_controller_input_fixture.mjs';
// HC C# report oracle versus exact YMCC producer/encoder and isolated UDP server.
import fs from 'node:fs';import path from 'node:path';import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';import {spawnSync} from 'node:child_process';
import {extract} from './fixtures/legion_motion_consumer/extract_main.mjs';
import {closeBrace} from './fixtures/hc_remaining_model_capabilities/expectations.mjs';
import {oracle} from './fixtures/hc_model_motion_consumer/oracle.mjs';
const runner=fileURLToPath(import.meta.url),repo=path.resolve(path.dirname(runner),'..'),workspace=path.resolve(repo,'../../..');
const root=path.join(workspace,'Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/DsuMotion');
const out=path.join(root,new Date().toISOString().replace(/[:.]/g,'-'));fs.mkdirSync(out,{recursive:true});
const sha=x=>createHash('sha256').update(x).digest('hex');
const result={schema:1,status:'running',references:[],sourceBindings:[],configurations:[],newProductDlls:[],hardwareIo:false,physicalAcceptance:false,hostExecuted:false};
const save=()=>fs.writeFileSync(path.join(out,'RESULT.json'),JSON.stringify(result,null,2)+'\n');save();
function run(exe,args,log,env=process.env,cwd=out){const r=spawnSync(exe,args,{cwd,env,windowsHide:true,encoding:'utf8',timeout:60000,maxBuffer:8*1024*1024});const p=path.join(out,log);fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,(r.stdout||'')+(r.stderr||'')+(r.error?.stack||''));if(r.error)throw r.error;return r;}
function need(r,what){if(r.status!==0)throw Error(what+' failed, exit '+r.status);return r.stdout;}
function method(text,name){const matches=[...text.matchAll(new RegExp('^\\s*(?:private|public) static [^\\n;{]*\\b'+name+'\\s*\\(','gm'))];if(matches.length!==1)throw Error('C# method ambiguity '+name);const start=matches[0].index,open=text.indexOf('{',start);return text.slice(start,closeBrace(text,open)+1).trim();}
function producer(text){const p=text.indexOf('        TickPreHostSubScope dsuScope(');if(p<0||text.indexOf('        TickPreHostSubScope dsuScope(',p+1)>=0)throw Error('DSU block ambiguity');const start=text.lastIndexOf('    {',p),end=closeBrace(text,start+4)+1;return text.slice(start,end);}
try{
 const plan=JSON.parse(fs.readFileSync(path.join(root,'PLAN.json'),'utf8'));
 const sources={};for(const f of plan.primarySources){const b=fs.readFileSync(f.path);if(sha(b)!==f.sha256)throw Error('Reference pin changed '+f.path);sources[path.basename(f.path)]=b.toString('utf8').replace(/\r+\n/g,'\n');result.references.push(f);}
 const upstream={};for(const f of plan.upstreamReferences||[]){const b=fs.readFileSync(f.path);if(sha(b)!==f.sha256)throw Error('Upstream reference pin changed '+f.path);upstream[path.basename(f.path)]=b.toString('utf8').replace(/\r+\n/g,'\n');result.references.push(f);}
 if(Object.keys(upstream).length!==5)throw Error('Expected five pinned current HC source files');
 const templateRoot=path.join(repo,'tools/fixtures/dsu_motion');
 const csMap={TIME:['TimerManager.cs','GetElapsedSeconds'],NORMALIZE:['InputUtils.cs','NormalizeXboxInput'],REPORT:['DSUServer.cs','ReportToBuffer'],BEGIN:['DSUServer.cs','BeginPacket'],FINISH:['DSUServer.cs','FinishPacket'],SWAP:['MotionManager.cs','SwapYawRoll'],INVERT:['MotionManager.cs','InvertAxis']};
 let cs=fs.readFileSync(path.join(templateRoot,'Oracle.cs'),'utf8');result.currentVsLocalMethods=[];for(const[k,[f,n]]of Object.entries(csMap)){const current=method(upstream[f],n),local=method(sources[f],n);const equal=current===local;result.currentVsLocalMethods.push({symbol:n,normalizedEqual:equal,sha256:sha(current)});if(!equal)throw Error('Current HC method differs; review before updating expectation: '+n);cs=cs.replace('/*@@'+k+'@@*/',current);}
 if(cs.includes('/*@@'))throw Error('Unfilled C# marker');
 const csDir=path.join(out,'oracle');fs.mkdirSync(csDir);fs.writeFileSync(path.join(csDir,'Oracle.cs'),cs);fs.writeFileSync(path.join(csDir,'MessageType.cs'),upstream['MessageType.cs']);
 fs.writeFileSync(path.join(csDir,'NuGet.Config'),'<configuration><packageSources><clear /></packageSources></configuration>');
 fs.writeFileSync(path.join(csDir,'Oracle.csproj'),'<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><Nullable>enable</Nullable><EnableNETAnalyzers>false</EnableNETAnalyzers></PropertyGroup></Project>');
 const exp=oracle(workspace,repo),rows=[];
 function add(name,g,a,extra={}){for(let bits=0;bits<8;bits++)rows.push({name:name+'/profile'+bits,raw:[2,-3,5,11,13,-17],gyro:g,accel:a,yaw:!!(bits&1),invertH:!!(bits&2),invertV:!!(bits&4),buttons:0xf7ff,sticks:[-32768,-1,0,32767],lt:255,rt:123,neutral:false,motion:true,retirePhysical:false,retireMotion:false,...extra});}
 const legacy=exp.legacy.recipes.filter(r=>!exp.legacy.controllerClasses.includes(r.deviceClass)); // Actual admitted Windows recipes only.
 for(const r of legacy){const[g,a]=['gyro','accel'].map(s=>r.matrices.find(m=>m.sensor===s));add(r.selectorId,{id:g.id,axis:g.axis,swap:g.swap},{id:a.id,axis:a.axis,swap:a.swap});}
 for(const m of exp.models.filter(m=>m.admitted)){const[g,a]=['gyro','accel'].map(s=>m.matrices.find(x=>x.sensor===s));const cv=x=>({id:x.id,axis:['X','Y','Z'].map(k=>x.matrix.Axis[k]),swap:['X','Y','Z'].map(k=>['X','Y','Z'].indexOf(x.matrix.AxisSwap[k]))});add(m.name,cv(g),cv(a));}
 const base=rows[0];for(const [name,x]of [['neutral',{neutral:true}],['no-motion',{motion:false}],['retire-physical',{retirePhysical:true}],['retire-motion',{retireMotion:true}],['controller',{controller:true}],['controller-retire',{controller:true,retireMotion:true}],['fractional-axes',{raw:[-7.25,2.5,-0.125,19.5,-23.25,0.0625]}]])rows.push({...base,...x,name});
 // Signed stick endpoints and near-rounding boundaries use the independent HC MathF.Round body.
 for(const n of [-32768,-32640,-327,0,1,127,128,256,32766,32767])rows.push({...base,name:'stick/'+n,sticks:[n,n,n,n],lt:n&255,rt:255});
 const inputFile=path.join(out,'vectors-input.json'),vectorsFile=path.join(out,'vectors.json');fs.writeFileSync(inputFile,JSON.stringify(rows));
 const dotnet=path.join(process.env.ProgramFiles,'dotnet/dotnet.exe');need(run(dotnet,['build',path.join(csDir,'Oracle.csproj'),'-c','Release','--nologo','-v:q'],'oracle/build.log'),'Oracle build');
 need(run(dotnet,[path.join(csDir,'bin/Release/net10.0/Oracle.dll'),inputFile,vectorsFile],'oracle/run.log'),'Oracle run');result.vectorCount=rows.length;
 const mainPath=path.join(repo,'native/main.cpp'),main=fs.readFileSync(mainPath,'utf8').replace(/\r\n/g,'\n'),before=fs.readFileSync(path.join(root,'main.cpp.before'),'utf8').replace(/\r\n/g,'\n');
 const symbols=['ymccParseAxisToken','ymccSplitAxisRecipe','ymccApplyNamedImuMatrix','inputHostPhysicalPublicationSourceAdmitted','inputHostFrameSubmissionSample','legionMotionSubmissionCurrent','inputCaptureDsuFrameCurrent'];
 const blocks=symbols.map(n=>extract(main,n));const snapshot=extract(main,'InputCaptureGamepadSnapshot','struct');result.sourceBindings=[...blocks,snapshot].map(({code,...r})=>r);result.sourceBindings.push({symbol:'dsu-producer',sha256:sha(producer(main))});result.beforeMainSha256=sha(before);result.beforeRuntimeSha256=sha(fs.readFileSync(path.join(root,'dsu_server.cpp.before')));
 const controllerFixture=snapshotControllerInputFixture(repo,path.join(out,'controller-input'));result.controllerInputDependencies=controllerFixture.inputs;
 const template=controllerFixture.preamble+fs.readFileSync(path.join(templateRoot,'native.cpp'),'utf8');
 const generated=template.replace('// @@SNAPSHOT_TYPE@@',snapshot.code).replace('// @@FUNCTIONS@@',blocks.map(b=>b.code).join('\n\n')).replace('// @@PRODUCER@@',producer(main));
 const oldGenerated=template.replace('// @@SNAPSHOT_TYPE@@',snapshot.code).replace('// @@FUNCTIONS@@',blocks.filter(b=>b.symbol!=='inputCaptureDsuFrameCurrent').map(b=>b.code).join('\n\n')).replace('// @@PRODUCER@@',producer(before));
 const files=['dsu_protocol.h','dsu_frame.h','windows_motion_submission.h','legion_motion_source.h','legion_motion_message_clock.h','input_frame_submission.h','virtual_output_status.h','hc_model_motion_catalog.h','hc_legacy_motion_catalog.h','msi_motion_capabilities.h'];
 const allInputs=[runner,...['Oracle.cs','native.cpp','README.md'].map(n=>path.join(templateRoot,n)),...files.map(n=>path.join(repo,'native',n)),path.join(repo,'native/dsu_server.cpp'),path.join(repo,'deps/json/json.hpp')];result.inputs=allInputs.map(p=>({path:p,sha256:sha(fs.readFileSync(p))}));
 const vcvars=path.join(process.env['ProgramFiles(x86)'],'Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat');const cmd=path.join(out,'environment.cmd');fs.writeFileSync(cmd,calibrationToolchainPreamble(vcvars)+'set\r\n');
 const prep=need(run(process.env.ComSpec,['/d','/c',cmd],'environment.log'),'MSVC setup'),env={...process.env};for(const row of prep.split(/\r?\n/)){const i=row.indexOf('=');if(i>0){const key=row.slice(0,i),existing=Object.keys(env).find(k=>k.toLowerCase()===key.toLowerCase());env[existing||key]=row.slice(i+1);}}
 for(const [label,code,old]of [['before',oldGenerated,true],['after',generated,false]]){
  const dir=path.join(out,label);fs.mkdirSync(dir);fs.writeFileSync(path.join(dir,'test.cpp'),code);for(const f of files)fs.copyFileSync(path.join(repo,'native',f),path.join(dir,f));
  fs.copyFileSync(old?path.join(root,'dsu_server.cpp.before'):path.join(repo,'native/dsu_server.cpp'),path.join(dir,'runtime.cpp'));
  fs.writeFileSync(path.join(dir,'runtime_wrapper.cpp'),'#include "runtime.cpp"\nextern "C" void dsu_fixture_init(){'+(old?'crc32Init();':'')+'}\n');
  for(const [configuration,flags]of [['Release',['/O2','/MD','/DNDEBUG']],['Checked',['/Od','/RTC1','/Zi','/MDd','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2']]]){
   const build=path.join(dir,configuration);fs.mkdirSync(build);const exe=path.join(build,'test.exe');
   need(run('cl.exe',['/nologo','/std:c++20','/EHsc','/utf-8','/W4','/permissive-',...flags,'/DYMCC_DSU_TEST',...(old?['/DYMCC_DSU_BEFORE']:[]),`/I${dir}`,`/I${path.join(repo,'deps/json')}`,path.join(dir,'test.cpp'),path.join(dir,'runtime_wrapper.cpp'),`/Fo${build}${path.sep}`,`/Fd${path.join(build,'test.pdb')}`,`/Fe${exe}`,'/link','ws2_32.lib','/INCREMENTAL:NO'],`${label}/${configuration}/compile.log`,env,build),'Native compile '+label+'/'+configuration);
   const r=run(exe,[vectorsFile],`${label}/${configuration}/run.log`,env,build);let report;try{report=JSON.parse(r.stdout.trim());}catch{throw Error('No native report '+label+'/'+configuration);}
   const accepted=old?r.status===1&&report.failureCount>0&&report.failures.every(f=>f.group.startsWith('fixed-hc-report')):r.status===0&&report.failureCount===0;
   result.configurations.push({label,configuration,accepted,exitCode:r.status,report,exeSha256:sha(fs.readFileSync(exe))});save();console.log(label+'/'+configuration+': '+(accepted?(old?'EXPECTED FAILURE':'PASS'):'FAIL')+' '+report.checks+' checks / '+report.failureCount+' failures');if(!accepted)throw Error('Native result '+label+'/'+configuration);
  }
 }
 if(process.argv.includes('--mutation-checks')){
  result.mutations=[];
  const originalHeader=fs.readFileSync(path.join(repo,'native/dsu_protocol.h'),'utf8'),originalFrame=fs.readFileSync(path.join(repo,'native/dsu_frame.h'),'utf8');
  const once=(s,a,b)=>{if(s.split(a).length!==2)throw Error('Mutation ambiguity '+a);return s.replace(a,b);};
  const variants=[
   ['no-final-source-recheck',generated,null,null,once(fs.readFileSync(path.join(repo,'native/dsu_server.cpp'),'utf8'),'ymcc::dsu::admit(snapshot,GetTickCount64())','ymcc::dsu::Admission{snapshot.live,snapshot.motionReady}')],
   ['wrong-motion-offset',generated,once(originalHeader,'putFloat(out.data()+76,-a.ax)','putFloat(out.data()+72,-a.ax)'),null,null],
   ['wrong-yaw-sign',generated,null,once(originalFrame,'a.gy=-a.gz;','a.gy=a.gz;'),null],
   ['bypass-model-matrix',once(generated,'static_cast<float>(mgx)','static_cast<float>(gx)'),null,null,null],
   ['bypass-windows-binding',once(generated,'ymcc::windows_motion::current(frame.source.windows)','true'),null,null,null],
  ];
  for(const [name,code,header,frame,runtime]of variants){
   const dir=path.join(out,'mutations',name);fs.mkdirSync(dir,{recursive:true});for(const f of files)fs.copyFileSync(path.join(repo,'native',f),path.join(dir,f));
   fs.writeFileSync(path.join(dir,'test.cpp'),code);if(header)fs.writeFileSync(path.join(dir,'dsu_protocol.h'),header);if(frame)fs.writeFileSync(path.join(dir,'dsu_frame.h'),frame);
   fs.writeFileSync(path.join(dir,'runtime.cpp'),runtime||fs.readFileSync(path.join(repo,'native/dsu_server.cpp')));fs.writeFileSync(path.join(dir,'runtime_wrapper.cpp'),'#include "runtime.cpp"\nextern "C" void dsu_fixture_init(){}\n');
   const reports=[];for(const [configuration,flags]of [['Release',['/O2','/MD','/DNDEBUG']],['Checked',['/Od','/RTC1','/Zi','/MDd','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2']]]){
    const build=path.join(dir,configuration);fs.mkdirSync(build);const exe=path.join(build,'test.exe');need(run('cl.exe',['/nologo','/std:c++20','/EHsc','/utf-8','/W4','/permissive-',...flags,'/DYMCC_DSU_TEST',`/I${dir}`,`/I${path.join(repo,'deps/json')}`,path.join(dir,'test.cpp'),path.join(dir,'runtime_wrapper.cpp'),`/Fo${build}${path.sep}`,`/Fd${path.join(build,'test.pdb')}`,`/Fe${exe}`,'/link','ws2_32.lib','/INCREMENTAL:NO'],`mutations/${name}/${configuration}/compile.log`,env,build),'Mutant compile '+name);
    const r=run(exe,name==='no-final-source-recheck'?[vectorsFile]:[vectorsFile,'--codec-only'],`mutations/${name}/${configuration}/run.log`,env,build);const d=JSON.parse(r.stdout.trim());const detected=r.status===1&&d.failureCount>0;reports.push({configuration,detected,report:d});if(!detected)throw Error('Undetected mutation '+name+'/'+configuration);
   }result.mutations.push({name,detected:true,reports});save();console.log('Mutation '+name+': detected in Release and Checked');
  }
 }
 result.inputsUnchanged=result.inputs.every(f=>sha(fs.readFileSync(f.path))===f.sha256);const finish=fs.readFileSync(mainPath,'utf8').replace(/\r\n/g,'\n');result.relevantMainUnchanged=[snapshot.code,...blocks.map(b=>b.code),producer(main)].every(s=>finish.includes(s));if(!result.inputsUnchanged||!result.relevantMainUnchanged)throw Error('Relevant source changed during test');
 verifyControllerInputFixture(repo,controllerFixture);result.status='pass';result.completedAt=new Date().toISOString();save();console.log('Evidence: '+out);
}catch(e){result.status='error';result.error=e.stack||String(e);save();throw e;}
