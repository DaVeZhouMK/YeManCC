import {defaultWindowsSnapshot} from './fixtures/hc_reviewed_calibration/default_windows_snapshot.mjs';
import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';
// Offline execution of the actual Windows sensor sink and BUS snapshot gate.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { extract } from './fixtures/legion_motion_consumer/extract_main.mjs';
import { closeBrace } from './fixtures/hc_remaining_model_capabilities/expectations.mjs';
const runner=fileURLToPath(import.meta.url),repo=path.resolve(path.dirname(runner),'..');
const fixture=path.join(repo,'tools/fixtures/windows_motion_binding');
let workspace=repo;
while(!fs.existsSync(path.join(workspace,'Docs/Research/PcCapabilities-HC-CTW-20261014/upstream/source-manifest.json'))){const p=path.dirname(workspace);if(p===workspace)throw Error('Workspace unavailable');workspace=p;}
const opt={},args=process.argv.slice(2);
for(let i=0;i<args.length;i++){
  if(['--expected-before-failure','--mutation-checks'].includes(args[i]))opt[args[i].slice(2)]=true;
  else if(['--out','--source','--vcvars'].includes(args[i])&&args[i+1])opt[args[i].slice(2)]=args[++i];
  else throw Error(`Unknown/incomplete option ${args[i]}`);
}
if(process.platform!=='win32'||process.arch!=='x64')throw Error('Windows x64 MSVC required');
const root=path.join(workspace,'Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/WindowsBinding');
const out=path.resolve(opt.out||path.join(root,new Date().toISOString().replace(/[:.]/g,'-')));
const rel=path.relative(root,out);
if(!rel||rel==='..'||rel.startsWith(`..${path.sep}`)||path.isAbsolute(rel))throw Error('Use a WindowsBinding child output');
if(fs.existsSync(out)&&fs.readdirSync(out).length)throw Error('Choose a fresh empty output directory');
fs.mkdirSync(out,{recursive:true});
const sha=x=>createHash('sha256').update(x).digest('hex');
const receipt={schema:1,status:'running',startedAt:new Date().toISOString(),configurations:[],mutations:[],
  physicalAcceptance:false,sensorIo:false,dllLoaded:false,hostExecuted:false,entireGoalComplete:false};
const save=()=>fs.writeFileSync(path.join(out,'RESULT.json'),JSON.stringify(receipt,null,2)+'\n');save();
function run(exe,argv,log,env=process.env,cwd=out){
  const r=spawnSync(exe,argv,{cwd,env,windowsHide:true,encoding:'utf8',timeout:60000,maxBuffer:8*1024*1024});
  const file=path.join(out,log);fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,(r.stdout||'')+(r.stderr||'')+(r.error?.message||''));
  if(r.error)throw r.error;return r;
}
function required(r,label){if(r.status!==0)throw Error(`${label}: exit ${r.status}`);return r.stdout;}
function once(text,from,to){if(text.split(from).length!==2)throw Error(`Nonunique anchor ${from}`);return text.replace(from,to);}
try{
  const sourcePath=path.resolve(opt.source||path.join(repo,'native/main.cpp'));
  const bytes=fs.readFileSync(sourcePath),source=bytes.toString('utf8');
  const start=source.indexOf('class InputCaptureSensorSink final {');
  if(start<0||source.indexOf('class InputCaptureSensorSink final {',start+1)>=0)throw Error('Nonunique actual sensor sink');
  const end=closeBrace(source,source.indexOf('{',start))+2;
  const sink=source.slice(start,end);
  if(!sink.endsWith('};'))throw Error('Sink class not complete');
  const viewBoundary=defaultWindowsSnapshot(source,repo);let snapshot,snapshotStart;
  if(viewBoundary){snapshot=viewBoundary.snapshot;snapshotStart=source.indexOf(snapshot);}
  else {
  const gyroMatches=[...source.matchAll(/if \((?:g_inputCaptureSink|windowsMotionSensorOwner)->gyroOk\)/g)];
  if(gyroMatches.length!==1)throw Error('Nonunique actual BUS sensor snapshot');
  snapshotStart=source.includes('    const auto windowsMotionSensorOwner = g_inputCaptureSink;') ? source.indexOf('    const auto windowsMotionSensorOwner = g_inputCaptureSink;') : source.lastIndexOf('    if (g_inputCaptureSink) {',gyroMatches[0].index);
  const snapshotEnd=closeBrace(source,source.indexOf('{',snapshotStart))+1;
  snapshot=source.slice(snapshotStart,snapshotEnd);
  if(!/if \((?:g_inputCaptureSink|windowsMotionSensorOwner)->accelOk\)/.test(snapshot)||!snapshot.includes('->gyroBindingGeneration'))throw Error('Incomplete paired snapshot');
  }
  const functions=['hcSinkTimeOfDayMs','inputCaptureMotionPairAdmitted'].map(s=>extract(source,s));
  const constants=['kInputCaptureHighMagDps','kInputCaptureHighMagQuietWindowMs','kInputCaptureAccelMotionDeltaG'].map(name=>{
    const rows=[...source.matchAll(new RegExp(`^constexpr [^\\n;]*\\b${name}\\s*=.*?;`,'gm'))];
    if(rows.length!==1)throw Error(`Missing constant ${name}`);return rows[0][0];
  });
  const inputs=[runner,path.join(fixture,'harness.cpp'),path.join(fixture,'README.md'),path.join(repo,'tools/fixtures/legion_motion_consumer/extract_main.mjs'),path.join(repo,'tools/fixtures/hc_remaining_model_capabilities/expectations.mjs')]
    .map(p=>({path:p,sha256:sha(fs.readFileSync(p))}));
  for(const p of [...(viewBoundary?.inputs||[]),path.join(repo,'tools/fixtures/hc_reviewed_calibration/default_windows_snapshot.mjs'),path.join(repo,'tools/fixtures/hc_reviewed_calibration/toolchain.mjs')])inputs.push({path:p,sha256:sha(fs.readFileSync(p))});
  const template=fs.readFileSync(path.join(fixture,'harness.cpp'),'utf8');
  const generated=template.replace('// @@CONSTANTS@@',constants.join('\n')).replace('// @@FUNCTIONS@@',functions.map(f=>f.code).join('\n\n'))
    .replace('// @@SINK@@',sink).replace('// @@SNAPSHOT@@',snapshot).replace('static std::shared_ptr<InputCaptureSensorSink> g_inputCaptureSink;', 'static std::shared_ptr<InputCaptureSensorSink> g_inputCaptureSink;'+(viewBoundary?.declarations||''));
  receipt.source={path:sourcePath,sha256:sha(bytes)};receipt.inputs=inputs;
  receipt.bindings=[...functions.map(({code,...row})=>row),{symbol:'InputCaptureSensorSink',sha256:sha(sink),startLine:source.slice(0,start).split('\n').length},
    {symbol:'paired-BUS-snapshot',sha256:sha(snapshot),startLine:source.slice(0,snapshotStart).split('\n').length}];
  receipt.scope='Actual sensor source generation/validity, exact BUS snapshot and final local pair admission; selected provider/selector/live conditions are supplied true. WinRT wrappers compile but are not invoked. No complete BUS, Host, physical sensor or OS queue delivery.';save();
  let vcvars=opt.vcvars;
  if(!vcvars){const vswhere=path.join(process.env['ProgramFiles(x86)']||'C:/Program Files (x86)','Microsoft Visual Studio/Installer/vswhere.exe');
    const install=required(run(vswhere,['-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],'vswhere.log'),'VS discovery').trim();
    vcvars=path.join(install,'VC/Auxiliary/Build/vcvars64.bat');}
  if(!fs.existsSync(vcvars)||/[\r\n"%]/.test(vcvars))throw Error('Invalid vcvars');
  const envFile=path.join(out,'environment.cmd');fs.writeFileSync(envFile,calibrationToolchainPreamble(vcvars)+'set\r\n');
  const prepared=spawnSync(process.env.ComSpec||'cmd.exe',['/d','/c',envFile],{windowsHide:true,encoding:'utf8',timeout:30000});
  if(prepared.status!==0)throw Error('Compiler environment setup failed');
  const env={...process.env};for(const row of prepared.stdout.split(/\r?\n/)){const at=row.indexOf('=');if(at>0){const key=row.slice(0,at),existing=Object.keys(env).find(k=>k.toLowerCase()===key.toLowerCase());env[existing||key]=row.slice(at+1);}}
  const winrt=path.join(env.WindowsSdkDir,'Include',env.WindowsSDKVersion,'cppwinrt');
  fs.writeFileSync(path.join(out,'compiler.json'),JSON.stringify({vcvars,version:env.VCToolsVersion,sdk:env.WindowsSDKVersion,winrt},null,2)+'\n');
  const configs=[['Release',['/O2','/DNDEBUG','/MD']],['Checked',['/Od','/RTC1','/Zi','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MDd']]];
  function compileRun(label,code,expectedFailure=false,mutant=false){
    const folder=path.join(out,label);fs.mkdirSync(folder,{recursive:true});const cpp=path.join(folder,'binding.cpp');fs.writeFileSync(cpp,code);
    const results=[];
    for(const[name,flags]of configs){
      const dir=path.join(folder,name);fs.mkdirSync(dir);const exe=path.join(dir,'binding.exe');
      const argv=['/nologo','/std:c++20','/EHsc','/utf-8','/W4','/permissive-',...flags,`/I${path.join(repo,'deps/json')}`,`/I${winrt}`,cpp,
        `/Fo${path.join(dir,'binding.obj')}`,`/Fd${path.join(dir,'binding.pdb')}`,`/Fe${exe}`,'/link','/INCREMENTAL:NO','WindowsApp.lib'];
      required(run('cl.exe',argv,`${label}/${name}/compile.log`,env,dir),`${label}/${name} compilation`);
      const r=run(exe,[],`${label}/${name}/run.log`,env,dir);let data;
      try{data=JSON.parse(r.stdout.trim());}catch{throw Error(`${label}/${name}: no JSON report, exit ${r.status}`);}
      const knownFailure=data.failureGroups?.['binding-admission']>0&&Object.keys(data.failureGroups).every(k=>k.startsWith('binding-'));
      const accepted=expectedFailure?r.status===1&&data.status==='fail'&&(mutant||knownFailure):r.status===0&&data.status==='pass'&&data.transitionPaths===4096;
      const row={name,exitCode:r.status,accepted,exeSha256:sha(fs.readFileSync(exe)),report:data};results.push(row);
      console.log(`${label}/${name}: ${accepted?(expectedFailure?'EXPECTED FAILURE':'PASS'):'FAIL'} (${data.checks} checks, ${data.failureCount} failures)`);
      if(!accepted)throw Error(`${label}/${name} unexpected result`);
    }
    return results;
  }
  receipt.configurations=compileRun('current',generated,!!opt['expected-before-failure']);save();
  if(opt['mutation-checks']){
    if(opt['expected-before-failure'])throw Error('Do not mutate an intentionally failing baseline');
    const variants=[
      ['no-retirement',()=>once(generated,'if (generation != activeBindingGeneration.load(std::memory_order_relaxed)) {','if (false) {')],
      ['only-one-channel-retired',()=>once(generated,'gyroOk = accelOk = false; // A new binding is not a sensor observation.','gyroOk = false; // A new binding is not a sensor observation.')],
      ['idempotent-bind-resets',()=>once(generated,'if (generation != activeBindingGeneration.load(std::memory_order_relaxed)) {','if (true) {')],
      ['old-gyro-callback-accepted',()=>once(generated,'if (gyroBindingGeneration != generation) return;','if (false) return;')],
    ];
    for(const[name,make]of variants){const configurations=compileRun(`mutations/${name}`,make(),true,true);receipt.mutations.push({name,detected:configurations.every(c=>c.accepted),configurations});save();}
  }
  const finish=fs.readFileSync(sourcePath,'utf8');
  receipt.relevantSourceUnchangedAtFinish=[sink,snapshot,...constants,...functions.map(f=>f.code)].every(code=>finish.includes(code));
  receipt.fixtureInputsUnchangedAtFinish=inputs.every(f=>sha(fs.readFileSync(f.path))===f.sha256);
  if(!receipt.relevantSourceUnchangedAtFinish||!receipt.fixtureInputsUnchangedAtFinish)throw Error('Relevant source changed during verification');
  receipt.status=opt['expected-before-failure']?'expected-before-failure':'pass';receipt.completedAt=new Date().toISOString();save();console.log(`Evidence: ${out}`);
}catch(error){receipt.status='error';receipt.error=error.stack||String(error);save();throw error;}
