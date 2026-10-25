// Fixed HC oracle vs verbatim native steering functions/frame fragments.
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';import {fileURLToPath} from 'node:url';
import {sha,acquireOracle,freezeNative,method} from './fixtures/controller_motion_steering/extract_source.mjs';
const runner=fileURLToPath(import.meta.url),repo=path.resolve(path.dirname(runner),'..'),workspace=path.resolve(repo,'../../..');
const root=path.join(workspace,'Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/LegionRuntime/Steering');
const options={};for(let i=2;i<process.argv.length;++i){const a=process.argv[i];assert(['--out','--vcvars','--manifest'].includes(a)&&process.argv[i+1],a);options[a.slice(2)]=process.argv[++i];}
const out=path.resolve(options.out||path.join(root,new Date().toISOString().replace(/[:.]/g,'-')+'-'+crypto.randomBytes(3).toString('hex')));
assert.equal(path.dirname(out).toLowerCase(),root.toLowerCase());fs.mkdirSync(root,{recursive:true});assert.equal(fs.realpathSync(root).toLowerCase(),path.resolve(root).toLowerCase());fs.mkdirSync(out);
const rel=p=>path.relative(out,p).replaceAll('\\','/');const json=(p,v)=>fs.writeFileSync(path.join(out,p),JSON.stringify(v,null,2)+'\n');
const load=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const walk=d=>fs.readdirSync(d,{withFileTypes:true}).sort((a,b)=>a.name.localeCompare(b.name)).flatMap(e=>e.isDirectory()?walk(path.join(d,e.name)):e.isFile()?[path.join(d,e.name)]:[]);
const receipt={schema:1,status:'running',startedAt:new Date().toISOString(),scope:'Isolated fixed-source HC steering decision/plane/local oracle vs exact native functions and frame snippets',actualHardware:false,actualPbt:false,wholeOnGamepadExecuted:false,fullHcActionCurveParity:false,inputs:[],oracleBuilds:[],configurations:[],mutations:[]};
const save=()=>json('summary.json',receipt);save();
const run=(exe,args,log,cwd=out,env=process.env)=>{const r=spawnSync(exe,args,{cwd,env,windowsHide:true,encoding:'utf8',timeout:60000,maxBuffer:24*1024*1024});fs.mkdirSync(path.dirname(path.join(out,log)),{recursive:true});fs.writeFileSync(path.join(out,log),(r.stdout||'')+(r.stderr||''));if(r.error)throw r.error;assert.equal(r.signal,null,log);return r;};
const ok=(r,label)=>{assert.equal(r.status,0,label+'; evidence '+out);return r.stdout;};
const once=(s,a,b)=>{assert.equal(s.split(a).length-1,1,'Missing/ambiguous mutation anchor: '+a);return s.replace(a,b);};
try{
  const fixture=path.join(repo,'tools/fixtures/controller_motion_steering');
  for(const p of [runner,...walk(fixture),path.join(repo,'deps/json/json.hpp')]){const bytes=fs.readFileSync(p),frozen=path.join(out,'source',path.relative(repo,p));fs.mkdirSync(path.dirname(frozen),{recursive:true});fs.writeFileSync(frozen,bytes);receipt.inputs.push({path:path.relative(repo,p).replaceAll('\\','/'),snapshot:rel(frozen),bytes:bytes.length,sha256:sha(bytes)});}
  const frozenFixture=path.join(out,'source/tools/fixtures/controller_motion_steering');
  const manifestPath=path.resolve(options.manifest||path.join(workspace,'Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/LegionFamilies/AUTO-STEERING-OPEN.json'));
  const manifestBytes=fs.readFileSync(manifestPath),manifest=load(manifestPath);fs.writeFileSync(path.join(out,'AUTO-STEERING-OPEN.json'),manifestBytes);
  const reference=acquireOracle(manifest.referencePath,manifest,out);
  receipt.oracleSource={...reference,fragments:reference.fragments.map(({code,...e})=>e),manifestPath,manifestSha256:sha(manifestBytes)};
  json('oracle-source-bindings.json',receipt.oracleSource);
  const native=freezeNative(repo,out);receipt.native=native.binding;
  const spec=load(path.join(frozenFixture,'cases.json'));const cases=[];
  const add=(source,axis,gravity,am,gm)=>{const id='steering-'+String(cases.length).padStart(5,'0');cases.push({id,source,axis,gravity,am,gm,gyro:spec.calibratedGyro});};
  for(const source of spec.sources)for(const axis of spec.axes)for(const gravity of spec.gravity)for(const am of spec.accelerometerMultipliers)for(const gm of spec.gyrometerMultipliers)add(source,axis,gravity,am,gm);
  for(const pin of spec.pinnedDecisions)add(pin.source,pin.axis,pin.gravity,pin.am,1);
  const inputs=path.join(out,'oracle-input.tsv');fs.writeFileSync(inputs,cases.map(c=>[c.id,c.source,c.axis,...c.gravity,c.am,c.gm,...c.gyro].join('\t')).join('\n')+'\n');
  receipt.corpus={rows:cases.length,source:'independent cases.json cartesian inputs plus manual pins',sha256:sha(fs.readFileSync(inputs)),manualPins:spec.pinnedDecisions.length};
  const sdk=ok(run('dotnet',['--version'],'dotnet-version.log'),'dotnet').trim(),major=Number(sdk.split('.')[0]);assert(major>=8);
  const dotnetEnv={...process.env,DOTNET_CLI_HOME:path.join(out,'dotnet-home'),DOTNET_NOLOGO:'1',DOTNET_SKIP_FIRST_TIME_EXPERIENCE:'1',DOTNET_CLI_TELEMETRY_OPTOUT:'1',NUGET_PACKAGES:path.join(out,'nuget-packages')};
  const dir=path.join(out,'oracle');fs.mkdirSync(dir);const sources=[path.join(frozenFixture,'Oracle.cs'),path.join(out,'HC-exact.generated.cs')];
  const project=path.join(dir,'Oracle.csproj'),xml=s=>s.replaceAll('&','&amp;').replaceAll('"','&quot;');
  fs.writeFileSync(path.join(dir,'NuGet.Config'),'<configuration><packageSources><clear /></packageSources></configuration>\n');
  fs.writeFileSync(project,`<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net${major}.0</TargetFramework>`+
    `<EnableDefaultCompileItems>false</EnableDefaultCompileItems><ImplicitUsings>disable</ImplicitUsings><Nullable>disable</Nullable>`+
    `<EnableNETAnalyzers>false</EnableNETAnalyzers><AllowUnsafeBlocks>false</AllowUnsafeBlocks></PropertyGroup><ItemGroup>`+
    sources.map(s=>`<Compile Include="${xml(s)}" />`).join('')+`</ItemGroup><Target Name="RecordActualSources" BeforeTargets="CoreCompile">`+
    `<WriteLinesToFile File="actual-inputs-$(Configuration).txt" Lines="@(Compile-&gt;'%(FullPath)')" Overwrite="true" /></Target></Project>\n`);
  let expected;
  for(const configuration of ['Release','Checked']){
    const output=path.join(dir,configuration),args=['build',project,'--configuration',configuration,'--output',output,'--configfile',path.join(dir,'NuGet.Config'),'--nologo','-p:UseSharedCompilation=false','-p:CheckForOverflowUnderflow=true','-p:ImportDirectoryBuildProps=false','-p:ImportDirectoryBuildTargets=false',`-p:Optimize=${configuration==='Release'}`];
    ok(run('dotnet',args,`oracle/${configuration}-build.log`,dir,dotnetEnv),'C# '+configuration);
    const values=JSON.parse(ok(run('dotnet',[path.join(output,'Oracle.dll'),inputs],`oracle/${configuration}-results.json`,dir,dotnetEnv),'C# oracle '+configuration));
    if(expected)assert.deepEqual(values,expected,'C# configs disagree');expected=values;
    const actual=fs.readFileSync(path.join(dir,`actual-inputs-${configuration}.txt`),'utf8').replace(/^\uFEFF/,'').trim().split(/\r?\n/);
    for(const s of sources)assert(actual.includes(s));assert(actual.every(s=>s.toLowerCase().startsWith((out+path.sep).toLowerCase())));
    receipt.oracleBuilds.push({configuration,sdk,buildArguments:args,actualCompileInputs:actual.map(p=>({path:rel(p),sha256:sha(fs.readFileSync(p))})),assemblySha256:sha(fs.readFileSync(path.join(output,'Oracle.dll')))});save();
  }
  assert.equal(expected.length,cases.length);
  for(let i=0;i<expected.length;++i)assert.equal(expected[i].id,cases[i].id);
  for(let i=0;i<spec.pinnedDecisions.length;++i)assert.equal(expected[expected.length-spec.pinnedDecisions.length+i].yaw,spec.pinnedDecisions[i].yaw,'Independent decision pin');
  const expectedFile=path.join(out,'native-expected.tsv');fs.writeFileSync(expectedFile,expected.map((e,i)=>{const c=cases[i];return [c.id,c.source,c.axis,...c.gravity,c.am,c.gm,...c.gyro,Number(e.yaw),...e.defaultGyro,...e.defaultAccel,...e.shortContribution,Number(e.windowsDefaultYaw),...e.windowsShortContribution].join('\t');}).join('\n')+'\n');
  receipt.expected={sha256:sha(fs.readFileSync(expectedFile)),origin:'Exact fixed-source C# output; Release and Checked identical'};
  let vcvars=options.vcvars;
  if(!vcvars){const vswhere=path.join(process.env['ProgramFiles(x86)']||'C:/Program Files (x86)','Microsoft Visual Studio/Installer/vswhere.exe');const install=ok(run(vswhere,['-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],'toolchain-discovery.log'),'MSVC discovery').trim();assert(install&&!/[\r\n]/.test(install));vcvars=path.join(install,'VC/Auxiliary/Build/vcvars64.bat');}
  assert(fs.existsSync(vcvars)&&!/[\r\n"%&|<>^!]/.test(vcvars));const setup=path.join(out,'compiler-environment.cmd');assert(!/[\r\n"%&|<>^!]/.test(setup));fs.writeFileSync(setup,`@echo off\r\ncall "${vcvars}" >nul\r\nif errorlevel 1 exit /b %errorlevel%\r\nset\r\n`);
  const setupResult=spawnSync(process.env.ComSpec||'cmd.exe',['/d','/c',setup],{encoding:'utf8',windowsHide:true,timeout:30000});if(setupResult.error)throw setupResult.error;assert.equal(setupResult.status,0);
  const env={...process.env};for(const line of setupResult.stdout.split(/\r?\n/)){const at=line.indexOf('=');if(at<=0)continue;const key=line.slice(0,at),old=Object.keys(env).find(k=>k.toLowerCase()===key.toLowerCase());env[old||key]=line.slice(at+1);}for(const k of Object.keys(env))if(['cl','_cl_','link','_link_'].includes(k.toLowerCase()))delete env[k];
  const compiler=path.join(env.VCToolsInstallDir,'bin/Hostx64/x64/cl.exe');receipt.compiler={compiler,sha256:sha(fs.readFileSync(compiler)),vcvars,vcToolsVersion:env.VCToolsVersion,windowsSdkVersion:env.WindowsSDKVersion};
  const configurations=[['Release',['/O2','/DNDEBUG','/MT']],['Checked',['/Od','/RTC1','/Zi','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MTd']]];
  const build=(where,flags,generated)=>{
    fs.mkdirSync(where,{recursive:true});const exe=path.join(where,'steering.exe'),args=['/nologo','/std:c++17','/EHsc','/utf-8','/W4','/WX','/permissive-','/fp:precise','/showIncludes',...flags,`/I${generated}`,`/I${path.join(out,'source/deps/json')}`,path.join(frozenFixture,'harness.cpp'),`/Fo${path.join(where,'harness.obj')}`,`/Fd${path.join(where,'compiler.pdb')}`,`/Fe${exe}`,'/link','/INCREMENTAL:NO'];
    const includes=ok(run(compiler,args,`${rel(where)}/compile.log`,where,env),'C++ '+rel(where));for(const h of ['native-functions.generated.h','native-pipeline.generated.h'])assert(includes.includes(path.join(generated,h)));
    const r=run(exe,[expectedFile],`${rel(where)}/run.log`,where,env);const match=/^SUMMARY groups=(\d+) rows=(\d+) checks=(\d+) failures=(\d+)\r?$/m.exec(r.stdout);assert(match,'No native semantic summary');
    const groups=[...r.stdout.matchAll(/^CASE ([-A-Za-z]+) rows=(\d+) checks=(\d+) failures=(\d+)\r?$/gm)].map(m=>({name:m[1],rows:+m[2],checks:+m[3],failures:+m[4]}));
    const result={exitCode:r.status,groups:+match[1],rows:+match[2],checks:+match[3],failures:+match[4],results:groups,buildArguments:args,executableSha256:sha(fs.readFileSync(exe))};
    assert.equal(groups.length,result.groups);assert.equal(groups.reduce((n,g)=>n+g.rows,0),result.rows);assert.equal(groups.reduce((n,g)=>n+g.checks,0),result.checks);assert.equal(groups.reduce((n,g)=>n+g.failures,0),result.failures);assert.equal(result.rows,cases.length);return result;
  };
  for(const [configuration,flags] of configurations){const r=build(path.join(out,configuration),flags,out);receipt.configurations.push({configuration,...r});save();assert.equal(r.exitCode,0,'Native '+configuration);assert.equal(r.failures,0);console.log(`${configuration}: PASS (${r.rows} rows, ${r.checks} checks)`);}
  const helper=native.methods.find(m=>m.name==='inputCaptureHcSteeringAxisResolvesYaw').code;
  const contribution=native.methods.find(m=>m.name==='inputCaptureProcessMotionContribution').code;
  const faults=[
    {name:'always-yaw',functions:once(native.generated,helper,helper.slice(0,helper.indexOf('{')+1)+'\n    (void)controllerSource; (void)defaultAccelY; (void)defaultAccelZ; return true;\n}'),pipeline:native.pipeline},
    {name:'raw-instead-of-scaled-accel',functions:native.generated,pipeline:once(native.pipeline,'controllerMotionRoute, gravityY * frameAccelMultiplier, gravityZ * frameAccelMultiplier','controllerMotionRoute, gravityY, gravityZ')},
    {name:'post-swap-redecision',functions:once(native.generated,contribution,once(contribution,'if (steeringYaw) {','(void)steeringYaw;\n        if (inputCaptureHcSteeringAxisResolvesYaw(true,sensor.defaultAccelY,sensor.defaultAccelZ)) {')),pipeline:native.pipeline},
  ];
  for(const fault of faults){const dir=path.join(out,'mutations',fault.name);fs.mkdirSync(dir,{recursive:true});fs.writeFileSync(path.join(dir,'native-functions.generated.h'),fault.functions);fs.writeFileSync(path.join(dir,'native-pipeline.generated.h'),fault.pipeline);const row={name:fault.name,functionsSha256:sha(fault.functions),pipelineSha256:sha(fault.pipeline),configurations:[]};receipt.mutations.push(row);
    for(const [configuration,flags] of configurations){const r=build(path.join(dir,configuration),flags,dir);row.configurations.push({configuration,...r});save();assert.equal(r.exitCode,1,'Mutant must fail semantically');assert(r.results.some(g=>g.name==='Controller-auto'&&g.failures>0),'Controller Auto witness missing');}console.log('Compiled semantic mutant killed in both configurations: '+fault.name);}
  const now=fs.readFileSync(path.join(repo,'native/main.cpp'));const nowText=now.toString('utf8');
  const methodsStable=native.methods.every(m=>method(nowText,m.name).sha256===m.sha256),fragmentsStable=[...native.types,native.decision,native.capture,native.diagnostic,native.plane,native.call].every(e=>nowText.includes(e.code));
  const anchorStable=method(nowText,'inputCaptureOnGamepad').sha256===native.gamepad.sha256;
  const ownedStable=receipt.inputs.every(i=>sha(fs.readFileSync(path.join(repo,i.path)))===i.sha256);
  assert(methodsStable&&fragmentsStable&&anchorStable&&ownedStable,'Sources changed during proof');assert.equal(sha(fs.readFileSync(reference.sourcePath)),reference.sha256);
  receipt.stability={methodsStable,fragmentsStable,anchorStable,ownedStable,hcSourceStable:true,currentMainSha256:sha(now),wholeMainStable:sha(now)===native.binding.sourceSha256};
  receipt.status='passed';receipt.completedAt=new Date().toISOString();save();json('CURRENT-SOURCE-STABILITY.json',receipt.stability);
  fs.writeFileSync(path.join(out,'REPORT.md'),`# Controller Auto steering isolated parity proof\n\nStatus: PASSED.\n\n`+
    `Independent oracle: fixed HC commit ${reference.commit}, MotionManager.cs SHA256 ${reference.sha256}, official Git blob ${reference.gitBlob}. Exact DetermineSteeringAxis/SwapYawRoll plus actual Default-plane and LocalSpace snippets execute with value-only C# stubs in Release and Checked. Results are identical; ${spec.pinnedDecisions.length} independent decision pins also agree. All C# build inputs and hashes are retained.\n\n`+
    `Native: five actual functions, two actual structs and actual frame multiplier/decision/default-accel/diagnostic-capture/JSON-lambda/contribution-call snippets compiled and executed. Whole onGamepad is source-only. Expected values come only from C#.\n\n`+
    receipt.configurations.map(c=>`- ${c.configuration}: ${c.rows} rows, ${c.checks} checks, ${c.failures} failures.`).join('\n')+
    `\n- Three required semantic mutants compiled and killed in both configurations: always-Yaw, raw rather than scaled accel, post-swap re-decision. No compile error/crash counts as a kill.\n\n`+
    `Windows/SerialUSBIMU/Controller, roll/yaw/auto/unknown, signed gravity, both Controller Auto orientations, tie/zero, zero/negative accel multipliers and signed gyro multipliers are covered. Actual diagnostics, Default acceleration and neutral-profile LocalSpace short contribution agree with the fixed-source oracle. Windows omitted-argument behavior is preserved and profile state remains unchanged.\n\n`+
    `One decision per frame and same decision for diagnostics/default plane/contribution are source-anchored. Neutral position LocalSpace profile controls aim/trigger/curve/velocity behavior; the proof does not cover all HC actions/curves. Provider admission is controlled; there is no full onGamepad, raw reader, PBT/OS sleep, hardware, Host/Steam runtime or deployment acceptance.\n\n`+
    `Current methods/fragments/anchor/owned inputs and fixed HC source are stable. Current main SHA256 ${receipt.stability.currentMainSha256}; whole-main stable ${receipt.stability.wholeMainStable}. See native-bindings.json, oracle-source-bindings.json and summary.json for exact scopes and hashes.\n`);
  json('FILES.json',{note:'Self inventory excluded',files:walk(out).filter(p=>path.basename(p)!=='FILES.json').map(p=>({path:rel(p),bytes:fs.statSync(p).size,sha256:sha(fs.readFileSync(p))}))});
  console.log(JSON.stringify({status:receipt.status,evidence:out,configurations:receipt.configurations.map(({configuration,rows,checks,failures})=>({configuration,rows,checks,failures})),stability:receipt.stability},null,2));
}catch(e){receipt.status='failed';receipt.error=e.stack||String(e);receipt.finishedAt=new Date().toISOString();save();console.error(receipt.error);console.error('Evidence: '+out);process.exitCode=1;}
