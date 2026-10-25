// Actual existing CPU motion DLL plus verbatim consumers. No HC/device/Host activation.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { extract } from './fixtures/legion_motion_consumer/extract_main.mjs';
import { closeBrace } from './fixtures/hc_remaining_model_capabilities/expectations.mjs';
import { oracle } from './fixtures/hc_model_motion_consumer/oracle.mjs';
const runner=fileURLToPath(import.meta.url),repo=path.resolve(path.dirname(runner),'..');
const fixture=path.join(repo,'tools/fixtures/hc_motion_real_runtime');
let workspace=repo;
while(!fs.existsSync(path.join(workspace,'Docs/Research/PcCapabilities-HC-CTW-20261014/upstream/source-manifest.json'))) {
  const next=path.dirname(workspace);if(next===workspace)throw Error('Shared HC evidence unavailable');workspace=next;
}
const args=process.argv.slice(2),opt={};
for(let i=0;i<args.length;++i){
  if(['--expect-zero-gradient-failure','--mutation-checks'].includes(args[i]))opt[args[i].slice(2)]=true;
  else if(['--out','--source','--vcvars','--python'].includes(args[i])&&args[i+1])opt[args[i].slice(2)]=args[++i];
  else throw Error(`Unknown/incomplete option: ${args[i]}`);
}
if(process.platform!=='win32'||process.arch!=='x64')throw Error('Actual Windows x64 runtime required');
const root=path.join(workspace,'Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/RealRuntime');
const out=path.resolve(opt.out||path.join(root,new Date().toISOString().replace(/[:.]/g,'-')));
const relative=path.relative(root,out);
if(!relative||relative==='..'||relative.startsWith(`..${path.sep}`)||path.isAbsolute(relative))throw Error('Evidence needs a RealRuntime child directory');
if(fs.existsSync(out)&&fs.readdirSync(out).length)throw Error('Choose a fresh empty output directory');
fs.mkdirSync(out,{recursive:true});
const sha=data=>createHash('sha256').update(data).digest('hex');
const report={schema:1,status:'running',out,startedAt:new Date().toISOString(),configurations:[],mutations:[],
  realGamepadMotionLoaded:false,managedHcLoaded:false,newProductDlls:[],deviceIo:false,sensorIo:false,hostExecuted:false,
  physicalAcceptance:false,entireGoalComplete:false,expectZeroGradientFailure:!!opt['expect-zero-gradient-failure']};
const save=()=>fs.writeFileSync(path.join(out,'RESULT.json'),JSON.stringify(report,null,2)+'\n');save();
function run(exe,argv,log,env=process.env,cwd=out){
  const result=spawnSync(exe,argv,{cwd,env,windowsHide:true,encoding:'utf8',timeout:60000,maxBuffer:16*1024*1024});
  const file=path.join(out,log);fs.mkdirSync(path.dirname(file),{recursive:true});
  fs.writeFileSync(file,(result.stdout||'')+(result.stderr||'')+(result.error?.message||''));
  if(result.error)throw result.error;return result;
}
function required(result,description){if(result.status!==0)throw Error(`${description}: exit ${result.status}`);return result.stdout;}
const inputs=[];
function freeze(file){const bytes=fs.readFileSync(file);inputs.push({path:file,sha256:sha(bytes)});return bytes;}
function once(text,from,to){if(text.split(from).length!==2)throw Error(`Mutation not unique: ${from}`);return text.replace(from,to);}
try {
  const sourcePath=path.resolve(opt.source||path.join(repo,'native/main.cpp'));
  const source=freeze(sourcePath).toString('utf8');
  const names=['ymccParseAxisToken','ymccSplitAxisRecipe','ymccApplyNamedImuMatrix','ymccMadgwickResetOrientation','ymccMadgwickUpdate'];
  const entries=names.map(name=>extract(source,name));
  const matches=[...source.matchAll(/^    g_gmProcessedThisTick = false;\r?\n    if \(g_gm && g_gmProcess && g_gmGetCal && gyro && accel &&/gm)];
  if(matches.length!==1)throw Error('Actual Windows ProcessMotion block not uniquely found');
  const start=matches[0].index,open=source.indexOf('{',start),end=closeBrace(source,open)+1;
  const block=source.slice(start,end);
  entries.push({symbol:'Windows-ProcessMotion-readback-block',code:block,startLine:source.slice(0,start).split('\n').length,
    endLine:source.slice(0,end).split('\n').length,sha256:sha(block)});
  report.extracted=entries.map(({code,...e})=>e);report.mainSource={path:sourcePath,sha256:sha(source)};
  const template=freeze(path.join(fixture,'harness.cpp')).toString('utf8');
  freeze(path.join(fixture,'read_logs.py'));freeze(path.join(fixture,'README.md'));freeze(runner);
  const headers=['controller_motion_processor.h','controller_motion_session.h'];
  const headerText=Object.fromEntries(headers.map(name=>[name,freeze(path.join(repo,'native',name)).toString('utf8')]));
  // Pin against the independently fixed local HC source index, not a command-line DLL path.
  const index=freeze(path.join(repo,'deps/handheldcompanion-runtime/source-files.sha256')).toString('utf8');
  const line=index.split(/\r?\n/).find(row=>/\sGamepadMotion\.dll$/.test(row));
  if(!line)throw Error('GamepadMotion is absent from the fixed HC source index');
  const pinned=line.trim().split(/\s+/)[0].toLowerCase();
  const dll=path.join(repo,'deps/hc-runtime-upstream/HC-CANDIDATE-0.32.4.0-06c0b954-20260902/GamepadMotion.dll');
  if(sha(freeze(dll))!==pinned)throw Error('Actual runtime DLL differs from fixed HC GamepadMotion');
  report.dll={path:dll,sha256:pinned,sourceIndex:'deps/handheldcompanion-runtime/source-files.sha256'};
  for(const name of ['oracle.mjs','reviewed_resources.mjs'])freeze(path.join(repo,'tools/fixtures/hc_model_motion_consumer',name));
  freeze(path.join(repo,'tools/fixtures/hc_remaining_model_capabilities/expectations.mjs'));freeze(path.join(repo,'tools/lib/hc_motion_matrix_guard.mjs'));
  const expected=oracle(workspace,repo);
  fs.writeFileSync(path.join(out,'hc-oracle.json'),JSON.stringify(expected,null,2)+'\n');
  const modelRows=[...expected.legacy.recipes.filter(r=>!r.controllerPreferred).map(r=>{const selected=r.selectedRecipe||r;return {id:selected.selectorId,kind:'legacy-windows',matrices:selected.matrices};}),
    ...expected.models.filter(r=>r.admitted).map(r=>({id:r.selectorId,kind:'new-windows',matrices:r.matrices}))];
  const normalize=m=>{
    if(m.axis)return {id:m.id,axis:m.axis,swap:m.swap};
    const axes=['X','Y','Z'];return {id:m.id,axis:axes.map(a=>m.matrix.Axis[a]),swap:axes.map(a=>axes.indexOf(m.matrix.AxisSwap[a]))};
  };
  const recipes=modelRows.map(row=>({id:row.id,kind:row.kind,gyro:normalize(row.matrices.find(m=>m.sensor==='gyro')),
    accel:normalize(row.matrices.find(m=>m.sensor==='accel'))}));
  const python=opt.python||path.join(process.env.USERPROFILE,'.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe');
  const logs=JSON.parse(required(run(python,[path.join(fixture,'read_logs.py'),workspace],'read-logs.log',{...process.env,PYTHONIOENCODING:'utf-8'}),'Read supplied logs'));
  fs.writeFileSync(path.join(out,'fixture.json'),JSON.stringify({recipes,logs})+'\n');
  report.coverage={reviewedResourceTuples:expected.reviewed.length,legacyWindowsTuples:recipes.filter(r=>r.kind==='legacy-windows').length,newWindowsTuples:recipes.filter(r=>r.kind==='new-windows').length,
    excludedControllerPreferredWindowsTuples:expected.legacy.recipes.filter(r=>r.controllerPreferred).map(r=>r.deviceClass),
    logs:logs.map(({frames,...row})=>({...row,replayedVectors:frames.length}))};
  report.boundary='Actual extracted Windows processing block/matrix/overlay and Controller Session/Owner through real GamepadMotion ABI. Host/admission producers/physical axes and full BUS are not executed. Recorded raw vectors are numeric regressions; their historical filter/calibration state is not reconstructed.';
  report.inputs=inputs;save();
  let vcvars=opt.vcvars;
  if(!vcvars){const vswhere=path.join(process.env['ProgramFiles(x86)']||'C:/Program Files (x86)','Microsoft Visual Studio/Installer/vswhere.exe');
    const install=required(run(vswhere,['-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],'vswhere.log'),'VS discovery').trim();
    vcvars=path.join(install,'VC/Auxiliary/Build/vcvars64.bat');}
  if(!fs.existsSync(vcvars)||/[\r\n"%]/.test(vcvars))throw Error('Invalid vcvars path');
  const envFile=path.join(out,'compiler-environment.cmd');
  fs.writeFileSync(envFile,`@echo off\r\ncall "${vcvars}" >nul\r\nif errorlevel 1 exit /b %errorlevel%\r\nset\r\n`);
  // Parse environment in memory. Do not write inherited credentials to logs.
  const setup=spawnSync(process.env.ComSpec||'cmd.exe',['/d','/c',envFile],{encoding:'utf8',windowsHide:true,timeout:30000});
  if(setup.status!==0)throw Error('Compiler environment failed');
  const env={...process.env};for(const row of setup.stdout.split(/\r?\n/)){const at=row.indexOf('=');if(at>0){const key=row.slice(0,at),existing=Object.keys(env).find(k=>k.toLowerCase()===key.toLowerCase());env[existing||key]=row.slice(at+1);}}
  fs.writeFileSync(path.join(out,'compiler-toolchain.json'),JSON.stringify({vcvars,version:env.VCToolsVersion,sdk:env.WindowsSDKVersion},null,2)+'\n');
  const generated=template.replace('// @@VERBATIM_FUNCTIONS@@',entries.slice(0,-1).map(e=>e.code).join('\n\n'))
    .replace('// @@VERBATIM_WINDOWS_CONSUMER@@',block);
  const configs=[['Release',['/O2','/DNDEBUG','/MD']],['Checked',['/Od','/RTC1','/Zi','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MDd']]];
  function compileRun(label,code,headersForRun,expectedFailure=false,mutant=false){
    const folder=path.join(out,label);fs.mkdirSync(folder,{recursive:true});
    const cpp=path.join(folder,'runtime.cpp');fs.writeFileSync(cpp,code);
    for(const [name,text]of Object.entries(headersForRun))fs.writeFileSync(path.join(folder,name),text);
    const results=[];
    for(const [name,flags] of configs){
      const dir=path.join(folder,name);fs.mkdirSync(dir);const exe=path.join(dir,'runtime.exe');
      const argv=['/nologo','/std:c++20','/EHsc','/utf-8','/W4','/permissive-',...flags,`/I${folder}`,`/I${path.join(repo,'deps/json')}`,cpp,
        `/Fo${path.join(dir,'runtime.obj')}`,`/Fd${path.join(dir,'runtime.pdb')}`,`/Fe${exe}`,'/link','/INCREMENTAL:NO'];
      required(run('cl.exe',argv,`${label}/${name}/compile.log`,env,dir),`${label}/${name} compilation`);
      if(sha(fs.readFileSync(dll))!==pinned)throw Error('Pinned DLL changed before child execution');
      const result=run(exe,[dll,path.join(out,'fixture.json')],`${label}/${name}/run.log`,env,dir);
      let parsed;try{parsed=JSON.parse(result.stdout.trim());}catch{throw Error(`${label}/${name}: missing JSON report (exit ${result.status})`);}
      const failures=parsed.failures||[];
      const zeroFailure=parsed.failureGroups?.['orientation-stationary']>0&&Object.keys(parsed.failureGroups).every(g=>['orientation-runtime','orientation-stationary'].includes(g));
      const accepted=expectedFailure ? result.status===1&&parsed.status==='fail'&&(mutant||zeroFailure) : result.status===0&&parsed.status==='pass';
      results.push({name,exitCode:result.status,accepted,sha256:sha(fs.readFileSync(exe)),report:parsed});
      console.log(`${label}/${name}: ${accepted?(expectedFailure?'EXPECTED FAILURE':'PASS'):'FAIL'} (${parsed.checks} checks, ${failures.length} recorded failures)`);
      if(!accepted)throw Error(`${label}/${name}: unexpected semantic result`);
    }
    return results;
  }
  report.configurations=compileRun('current',generated,headerText,!!opt['expect-zero-gradient-failure']);
  report.realGamepadMotionLoaded=report.configurations.every(c=>c.report.dllLoaded===true);save();
  if(opt['mutation-checks']){
    if(opt['expect-zero-gradient-failure'])throw Error('Do not mutate the intentionally failing baseline');
    const variants=[
      ['zero-gradient-division',()=>({code:once(generated,'if (gradientSquaredNorm == 0.0f) {','if (false && gradientSquaredNorm == 0.0f) {'),headers:headerText})],
      ['windows-gyro-axis-swap',()=>({code:once(generated,'(float)(calibrationOnlyProcess ? hcCandidateGx : mgx)','(float)(calibrationOnlyProcess ? hcCandidateGx : mgy)'),headers:headerText})],
      ['controller-offset-omitted',()=>({code:generated,headers:{...headerText,'controller_motion_processor.h':once(headerText['controller_motion_processor.h'],
        'ops_.setOffset(handle_, calibration.x, calibration.y, calibration.z, calibration.weight);','ops_.setOffset(handle_, 0, 0, 0, calibration.weight);')}})],
    ];
    for(const [name,make]of variants){const v=make();const configurations=compileRun(`mutations/${name}`,v.code,v.headers,true,true);
      report.mutations.push({name,detected:configurations.every(c=>c.accepted),configurations});save();}
  }
  const finish=fs.readFileSync(sourcePath).toString('utf8');
  report.verbatimDefinitionsUnchangedAtFinish=entries.every(e=>finish.includes(e.code));
  report.allFrozenInputsUnchangedAtFinish=inputs.filter(i=>i.path!==sourcePath).every(i=>sha(fs.readFileSync(i.path))===i.sha256);
  if(!report.verbatimDefinitionsUnchangedAtFinish||!report.allFrozenInputsUnchangedAtFinish)throw Error('A relevant source changed during verification');
  report.status=opt['expect-zero-gradient-failure']?'expected-zero-gradient-failure':'pass';report.completedAt=new Date().toISOString();save();
  console.log(`Evidence: ${out}`);
}catch(error){report.status='error';report.error=error.stack||String(error);save();throw error;}
