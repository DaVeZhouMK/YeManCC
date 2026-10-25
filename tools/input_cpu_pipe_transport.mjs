// Freezes the actual header and runs isolated MSVC fixtures. All API substitutions
// and vector growth counters are confined to copied headers below the evidence dir.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
import assert from 'node:assert/strict';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const args=process.argv.slice(2);
function option(name){const i=args.indexOf(name);assert(i>=0&&args[i+1],name+' required');return path.resolve(args[i+1]);}
const baseline=option('--baseline'),out=option('--output');
const candidate=args.includes('--candidate')?option('--candidate'):path.join(repo,'native/input_host_pipe_transport.h');
const run=path.join(out,'run-'+new Date().toISOString().replace(/[:.]/g,'-'));
fs.mkdirSync(run,{recursive:true});
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const inputs=['native/input_host_pipe_transport.h','native/input_callback_cost.h','native/input_frame_submission.h','native/gamepad_backend_cost_json.h','native/gamepad_backend_cost.h','native/virtual_output_status.h','deps/json/json.hpp','tools/input_cpu_pipe_transport.cpp','tools/input_cpu_pipe_transport.mjs','tools/input_pipe_transport_control_selftest.cpp','tools/input_pipe_transport_real_selftest.cpp','tools/input_pipe_test_allocation_fault.h','tools/gamepad_backend_cost_selftest.cpp'];
const frozen=new Map(inputs.map(p=>[p,fs.readFileSync(path.join(repo,p))]));
const main=fs.readFileSync(path.join(repo,'native/main.cpp'));
const old=fs.readFileSync(baseline),proposed=fs.readFileSync(candidate);
fs.writeFileSync(path.join(run,'baseline.h'),old);
for(const [name,bytes] of frozen)fs.writeFileSync(path.join(run,path.basename(name)),bytes);
function instrument(bytes){const text=bytes.toString('utf8');const needle='created->bytes.resize(payloadSize);';assert.equal(text.split(needle).length,2);return text.replace(needle,'{ inputCpuResize(created->bytes.size(), payloadSize); '+needle+' }');}
fs.writeFileSync(path.join(run,'before.h'),'// Frozen baseline copy.\n'+instrument(old));
fs.writeFileSync(path.join(run,'after.h'),'// Frozen comparison copy.\n'+instrument(proposed));
const vc=process.env.YMCC_VCVARS64??'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
assert(fs.existsSync(vc),vc);
const q=s=>{assert(!/["\r\n]/.test(s));return '"'+s+'"';};
const result={run,sourceHashes:Object.fromEntries([...frozen].map(([p,b])=>[p,sha(b)])),baselineSha256:sha(old),comparisonHeaderPath:candidate,comparisonHeaderSha256:sha(proposed),comparisonIsCurrentProduction:sha(proposed)===sha(frozen.get(inputs[0])),mainSha256AtStart:sha(main),profiles:{}};
const jobs=[['Release','fixture-counts','input_cpu_pipe_transport.cpp',true],['Release','fixture','input_cpu_pipe_transport.cpp',true],['Checked','fixture','input_cpu_pipe_transport.cpp',false]];
if(!args.includes('--fixture-only'))for(const profile of ['Release','Checked'])for(const version of ['baseline','candidate'])for(const scope of ['control','real'])jobs.push([profile,version+'-'+scope,'tools/input_pipe_transport_'+scope+'_selftest.cpp',false,version]);
if(!args.includes('--fixture-only'))for(const profile of ['Release','Checked'])jobs.push([profile,'backend','tools/gamepad_backend_cost_selftest.cpp',false]);
for(const [profile,label,source,timed,version] of jobs){
  const dir=path.join(run,profile+'-'+label);fs.mkdirSync(dir);
  for(const [name,bytes] of frozen)fs.writeFileSync(path.join(dir,path.basename(name)),bytes);
  for(const name of ['before.h','after.h'])fs.copyFileSync(path.join(run,name),path.join(dir,name));
  if(version)fs.writeFileSync(path.join(dir,'input_host_pipe_transport.h'),version==='baseline'?old:proposed);
  const src=path.join(dir,path.basename(source)),exe=path.join(dir,'fixture.exe');
  const batch=path.join(dir,'build.cmd');
  const flags=(profile==='Release'?'/O2 /DNDEBUG /MT':'/Od /RTC1 /D_DEBUG /D_ITERATOR_DEBUG_LEVEL=2 /MTd')+(label==='fixture-counts'?' /DINPUT_CPU_COUNT_RESIZE':'')+(args.includes('--expect-growth-eliminated')?' /DINPUT_CPU_EXPECT_GROWTH_ELIMINATED':'');
  fs.writeFileSync(batch,'@echo off\r\ncall '+q(vc)+' >nul\r\nif errorlevel 1 exit /b 1\r\nset VSLANG=1033\r\ncl /nologo /std:c++20 /utf-8 /EHsc /W4 /WX /permissive- /sdl '+flags+' /I'+q(dir)+' '+q(src)+' /Fo'+q(path.join(dir,'fixture.obj'))+' /Fe'+q(exe)+'\r\nexit /b %errorlevel%\r\n');
  let r=spawnSync('cmd.exe',['/d','/c',batch],{cwd:dir,encoding:'utf8',windowsHide:true,timeout:60000});
  fs.writeFileSync(path.join(dir,'build.log'),r.stdout+'\n'+r.stderr);assert.equal(r.status,0,profile+' '+label+' build: '+r.stdout+r.stderr);
  r=spawnSync(exe,timed?['--benchmark']:[],{cwd:dir,encoding:'utf8',windowsHide:true,timeout:60000});
  fs.writeFileSync(path.join(dir,'result.json'),r.stdout);fs.writeFileSync(path.join(dir,'stderr.log'),r.stderr);
  assert.equal(r.status,0,profile+' '+label+' run: '+r.stdout+r.stderr);
  const data=JSON.parse(r.stdout);assert(data.pass===true||data.passed===true,profile+' '+label+' result');
  result.profiles[profile+'-'+label]=data;
  console.log(profile+' '+label+': PASS '+(data.checks??data.assertions??''));
}
result.mainSha256AtEnd=sha(fs.readFileSync(path.join(repo,'native/main.cpp')));
result.mainChangedOutsideFixture=sha(main)!==result.mainSha256AtEnd;
result.sourceChangedDuringRun=inputs.filter(p=>sha(fs.readFileSync(path.join(repo,p)))!==sha(frozen.get(p)));
result.pass=result.sourceChangedDuringRun.length===0;
fs.writeFileSync(path.join(run,'RESULT.json'),JSON.stringify(result,null,2)+'\n');
fs.writeFileSync(path.join(out,'LATEST.json'),JSON.stringify({run,pass:result.pass},null,2)+'\n');
console.log('Evidence: '+run);assert(result.pass,'input drift');
