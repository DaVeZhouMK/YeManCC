// Real main Controller steady/manual composition; no production or device writes.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';import {fileURLToPath,pathToFileURL} from 'node:url';
const runner=fileURLToPath(import.meta.url),repo=path.resolve(path.dirname(runner),'..'),workspace=path.resolve(repo,'../../..');
const root=path.join(workspace,'Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/AutomaticCalibration/Consumer');
const fixture=path.join(repo,'tools/fixtures/controller_motion_auto_calibration_consumer');
const args={};for(let i=2;i<process.argv.length;++i){const k=process.argv[i];assert(['--out','--vcvars'].includes(k)&&process.argv[i+1],k);args[k.slice(2)]=process.argv[++i];}
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const space=()=>{const s=fs.statfsSync(workspace),n=s.bavail*s.bsize;assert(n>=768*1024*1024,'DISK_STOP: less than 768 MiB');return n;};
// Readiness failures create no misleading incomplete proof directory.
space();
const basePreference=fs.readFileSync(path.join(repo,'tools/fixtures/legion_motion_consumer/harness.cpp'),'utf8');
assert(/^static std::atomic<bool> g_controllerAutoCalibrationRequested\{true\};\r?$/m.test(basePreference),'Base published atomic/default is not ready');
assert(/struct MotionSettings\s*\{[^}]*std::atomic<bool>& autoCalibrate = g_controllerAutoCalibrationRequested;[^}]*\};/.test(basePreference),'Scenario settings must reference the exact published atomic');
assert(!/\bbool\s+autoCalibrate\s*=/.test(basePreference),'A separate ordinary bool must not shadow the published atomic');
assert(fs.readFileSync(path.join(repo,'native/main.cpp'),'utf8').includes('ops.getOffset(handle,&baseline->x,&baseline->y,&baseline->z);'),'Final live manual baseline is not ready');
assert(/if \(g_controllerCalibrationActiveRequest \|\| request\)\s*g_legionMotionSession->invalidateCalibrationMode\(\);/.test(fs.readFileSync(path.join(repo,'native/main.cpp'),'utf8')),'Final conditional manual invalidation is not ready');
const out=path.resolve(args.out||path.join(root,new Date().toISOString().replace(/[:.]/g,'-')+'-'+crypto.randomBytes(3).toString('hex')));
assert.equal(path.dirname(out).toLowerCase(),root.toLowerCase(),'Fresh direct child of approved evidence root only');
fs.mkdirSync(root,{recursive:true});assert.equal(fs.realpathSync(root).toLowerCase(),root.toLowerCase());fs.mkdirSync(out);
const rel=p=>path.relative(out,p).replaceAll('\\','/');const load=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const write=(p,b)=>{space();const f=path.join(out,p);fs.mkdirSync(path.dirname(f),{recursive:true});fs.writeFileSync(f,b);};
const json=(p,v)=>write(p,JSON.stringify(v,null,2)+'\n');
const walk=d=>fs.readdirSync(d,{withFileTypes:true}).sort((a,b)=>a.name.localeCompare(b.name)).flatMap(e=>e.isDirectory()?walk(path.join(d,e.name)):e.isFile()?[path.join(d,e.name)]:[]);
const receipt={schema:1,status:'running',startedAt:new Date().toISOString(),initialAvailableBytes:space(),scope:'Actual extracted main steady/manual calibration consumer with real Session/Owner/Transaction/Lease; native callback effects and isolated JSON files',actualHardware:false,actualPbt:false,actualDllLoaded:false,rawReaderExecuted:false,windowsOrSerialExecuted:false,hostExecuted:false,actualWin32AtomicReplacement:false,fullSettingsRefreshExecuted:false,crossThreadStressExecuted:false,inputs:[],configurations:[],mutations:[]};
receipt.preferencePublication={sourceOnly:'Atomic default true; unchanged autoCalibrate key/default true -> local -> existing Windows field -> release store; Controller steady/owner use acquire load',executed:'Existing scenarios including the lagging mirror case assign the base MotionSettings atomic reference; extracted Controller functions read that same atomic',notExecuted:'Complete settings refresh, settings/UI IPC and concurrent thread stress'};
const save=()=>json('RESULT.json',receipt);
const run=(exe,argv,log,cwd=out,env=process.env)=>{space();const r=spawnSync(exe,argv,{cwd,env,windowsHide:true,encoding:'utf8',timeout:60000,maxBuffer:5*1024*1024});write(log,(r.stdout||'')+(r.stderr||''));if(r.error)throw r.error;assert.equal(r.signal,null,log);return r;};
const ok=(r,label)=>{assert.equal(r.status,0,label+'; evidence '+out);return r.stdout;};
const once=(s,a,b)=>{assert.equal(s.split(a).length-1,1,'Mutation anchor missing/ambiguous: '+a);return s.replace(a,b);};
try{
 save();const headers=['controller_motion_session.h','controller_motion_processor.h','controller_motion_calibration.h','legion_motion_source.h','legion_motion_message_clock.h','virtual_output_status.h','legion_input_protocol.h','input_frame_submission.h','windows_motion_submission.h'];
 const files=[runner,...walk(fixture),...['harness.cpp','extract_main.mjs','queue_epoch_scenarios.inl'].map(n=>path.join(repo,'tools/fixtures/legion_motion_consumer',n)),...['platform_boundary.h','family-topology.json'].map(n=>path.join(repo,'tools/fixtures/legion_motion_calibration_consumer',n)),...headers.map(n=>path.join(repo,'native',n)),path.join(repo,'deps/json/json.hpp')];
 for(const f of files){const b=fs.readFileSync(f),p=path.relative(repo,f).replaceAll('\\','/'),snapshot='source/'+p;write(snapshot,b);receipt.inputs.push({path:p,snapshot,bytes:b.length,sha256:sha(b)});}
 const frozenFixture=path.join(out,'source/tools/fixtures/controller_motion_auto_calibration_consumer');
 const extraction=await import(pathToFileURL(path.join(frozenFixture,'extract_production.mjs')).href);
 const bindings=path.join(out,'bindings');fs.mkdirSync(bindings);const frozen=extraction.freeze(repo,bindings);receipt.bindingFile='bindings/SOURCE-BINDINGS.json';save();
 let vcvars=args.vcvars;
 if(!vcvars){const vswhere=path.join(process.env['ProgramFiles(x86)']||'C:/Program Files (x86)','Microsoft Visual Studio/Installer/vswhere.exe');const install=ok(run(vswhere,['-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],'toolchain-discovery.log'),'MSVC discovery').trim();assert(install&&!/[\r\n]/.test(install));vcvars=path.join(install,'VC/Auxiliary/Build/vcvars64.bat');}
 assert(fs.existsSync(vcvars)&&!/[\r\n"%&|<>^!]/.test(vcvars));const setup=path.join(out,'compiler-environment.cmd');assert(!/[\r\n"%&|<>^!]/.test(setup));
 const keys=['VCToolsInstallDir','VCToolsVersion','WindowsSDKVersion','INCLUDE','LIB','LIBPATH','PATH'];
 write('compiler-environment.cmd',`@echo off\r\ncall "${vcvars}" >nul\r\nif errorlevel 1 exit /b %errorlevel%\r\n`+keys.map(k=>`echo ${k}=%${k}%\r\n`).join(''));
 const setupResult=spawnSync(process.env.ComSpec||'cmd.exe',['/d','/c',setup],{encoding:'utf8',windowsHide:true,timeout:30000});if(setupResult.error)throw setupResult.error;assert.equal(setupResult.status,0);
 const env={...process.env};for(const line of setupResult.stdout.split(/\r?\n/)){const i=line.indexOf('=');if(i<=0)continue;const k=line.slice(0,i);if(!keys.includes(k))continue;const old=Object.keys(env).find(v=>v.toLowerCase()===k.toLowerCase());env[old||k]=line.slice(i+1);}
 for(const k of Object.keys(env))if(['cl','_cl_','link','_link_'].includes(k.toLowerCase()))delete env[k];
 const compiler=path.join(env.VCToolsInstallDir,'bin/Hostx64/x64/cl.exe');receipt.compiler={path:compiler,sha256:sha(fs.readFileSync(compiler)),vcvars,vcToolsVersion:env.VCToolsVersion,windowsSdkVersion:env.WindowsSDKVersion,environmentCapture:'Only explicitly listed toolchain variables; never an unfiltered SET or saved environment values'};save();
 const native=path.join(out,'source/native'),harness=path.join(frozenFixture,'harness.cpp'),jsonDir=path.join(out,'source/deps/json');
 const configs=[['Release',['/O2','/DNDEBUG','/MT']],['Checked',['/Od','/RTC1','/Zi','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MTd']]];
 function build(where,flags,generated=bindings,headerDir=native){
  space();fs.mkdirSync(where,{recursive:true});const exe=path.join(where,'auto-calibration.exe'),deps=path.join(where,'dependencies.json');
  const argv=['/nologo','/std:c++17','/EHsc','/utf-8','/W4','/WX','/permissive-','/fp:precise',...flags,'/sourceDependencies',deps,`/I${generated}`,`/I${bindings}`,`/I${headerDir}`,`/I${native}`,`/I${jsonDir}`,harness,`/Fo${path.join(where,'harness.obj')}`,`/Fd${path.join(where,'compiler.pdb')}`,`/Fe${exe}`,'/link','/INCREMENTAL:NO'];
  ok(run(compiler,argv,rel(where)+'/compile.log',where,env),'C++ compile '+rel(where));const dep=load(deps).Data;assert.equal(path.resolve(dep.Source).toLowerCase(),harness.toLowerCase());
  const actual=[dep.Source,...dep.Includes];
  for(const name of headers){const p=path.join(fs.existsSync(path.join(headerDir,name))?headerDir:native,name);assert(actual.some(a=>path.resolve(a).toLowerCase()===p.toLowerCase()),'Missing actual native header '+p);}
  assert(actual.some(a=>path.resolve(a).toLowerCase()===path.join(generated,'production_functions.generated.h').toLowerCase()));
  json(rel(where)+'/actual-compile-inputs.json',actual.map(p=>({path:p.toLowerCase().startsWith(out.toLowerCase()+path.sep)?rel(p):p,sha256:sha(fs.readFileSync(p))})));
  const artifacts=path.join(where,'effects');fs.mkdirSync(artifacts);const r=run(exe,[artifacts,path.join(out,'source/tools/fixtures/legion_motion_calibration_consumer/family-topology.json')],rel(where)+'/run.log',where,env);
  const m=/^SUMMARY cases=(\d+) checks=(\d+) failures=(\d+) failedCases=(\d+)\r?$/m.exec(r.stdout);assert(m,'No semantic summary: crash/build failure is not mutant kill');
  const cases=[...r.stdout.matchAll(/^CASE ([-a-z0-9]+) checks=(\d+) failures=(\d+)\r?$/gm)].map(v=>({name:v[1],checks:+v[2],failures:+v[3]}));assert.equal(cases.length,+m[1]);assert.equal(cases.reduce((n,c)=>n+c.checks,0),+m[2]);assert.equal(cases.reduce((n,c)=>n+c.failures,0),+m[3]);
  return {exitCode:r.status,scenarios:+m[1],checks:+m[2],failures:+m[3],failedCases:+m[4],cases,buildArguments:argv,actualCompileInputs:rel(where)+'/actual-compile-inputs.json',executableSha256:sha(fs.readFileSync(exe))};
 }
 for(const [configuration,flags]of configs){const r=build(path.join(out,configuration),flags);receipt.configurations.push({configuration,...r});save();assert.equal(r.exitCode,0,configuration+' failed');assert.equal(r.failures,0);console.log(`${configuration}: PASS (${r.scenarios} scenarios, ${r.checks} checks)`);}
 const body=s=>{const e=frozen.entries.find(e=>e.symbol===s);assert(e,s);return e.code;};
 const session=fs.readFileSync(path.join(native,'controller_motion_session.h'),'utf8'),txn=fs.readFileSync(path.join(native,'controller_motion_calibration.h'),'utf8');
 const invalidation=/if \(g_controllerCalibrationActiveRequest \|\| request\)\s*g_legionMotionSession->invalidateCalibrationMode\(\);/.exec(body('inputCaptureControllerCalibrationAdvance'));
 assert(invalidation,'Final conditional invalidation anchor');
 const mutants=[
  {name:'lagging-flag-invalidates-cache',symbol:'inputCaptureControllerCalibrationAdvance',from:invalidation[0],to:'g_legionMotionSession->invalidateCalibrationMode();',witness:'lagging-request-flag-idempotent'},
  {name:'automatic-preference-inverted',header:'controller_motion_session.h',from:'const int wanted = automatic ? 3 : 0;',to:'const int wanted = automatic ? 0 : 3;',witness:'steady-setting-cache-no-io'},
  {name:'steady-mode-cache-bypassed',header:'controller_motion_session.h',from:'if (calibrationMode_ && *calibrationMode_ == wanted) return true;',to:'if (calibrationMode_ && *calibrationMode_ == wanted && !handle) return true;',witness:'steady-setting-cache-no-io'},
  {name:'manual-training-reconciles-steady',symbol:'inputCaptureControllerCalibrationAdvance',from:'const auto result=process();',to:'const auto result=steadyProcess();',witness:'manual-threshold-success-auto-restore'},
  {name:'abort-keeps-stale-cache',symbol:'inputCaptureControllerCalibrationAbort',from:'if (g_legionMotionSession) g_legionMotionSession->invalidateCalibrationMode();',to:'(void)g_legionMotionSession;',witness:'cancel-withdraw-and-abort'},
  {name:'omit-post-mode-source-check',symbol:'inputCaptureControllerCalibrationAdvance',from:'if (!g_legionMotionSession->reconcileCalibrationMode(key,automatic)) return std::nullopt;\r\n        if (!legionMotionSubmissionCurrent(sample,GetTickCount64())) return std::nullopt;',to:'if (!g_legionMotionSession->reconcileCalibrationMode(key,automatic)) return std::nullopt;',witness:'freshness-and-second-source-check'},
  {name:'rollback-to-persisted-offset',symbol:'inputCaptureControllerCalibrationAdvance',from:'ops.getOffset(handle,&baseline->x,&baseline->y,&baseline->z);',to:'(void)handle;',witness:'live-auto-baseline-rollback'},
  {name:'inclusive-manual-confidence',header:'controller_motion_calibration.h',from:'if (confidence_ > 0.99f)',to:'if (confidence_ >= 0.99f)',witness:'manual-threshold-success-auto-restore'}
 ];
 for(const mutant of mutants){const where=path.join(out,'mutants',mutant.name),generated=path.join(where,'generated');fs.mkdirSync(generated,{recursive:true});
  let code=frozen.generated;
  if(mutant.symbol){const original=body(mutant.symbol);code=once(code,original,once(original,mutant.from,mutant.to));}
  write(rel(generated)+'/production_functions.generated.h',code);
  if(mutant.header)write(rel(generated)+'/'+mutant.header,once(mutant.header==='controller_motion_session.h'?session:txn,mutant.from,mutant.to));
  const r=build(path.join(where,'build'),configs[0][1],generated,mutant.header?generated:native);const killed=r.exitCode===1&&r.failures>0&&r.cases.some(c=>c.name===mutant.witness&&c.failures>0);
  receipt.mutations.push({...mutant,killed,...r});save();assert(killed,'Mutant not semantically killed: '+mutant.name);console.log('Mutant killed: '+mutant.name);
 }
 receipt.stability={...extraction.verify(repo,frozen),inputs:receipt.inputs.map(p=>({path:p.path,sha256:p.sha256,stable:sha(fs.readFileSync(path.join(repo,p.path)))===p.sha256}))};
 assert(receipt.stability.methodsStable&&receipt.stability.constantsStable&&receipt.stability.anchorsStable&&receipt.stability.inputs.every(p=>p.stable),'Relevant tested input drifted; unrelated whole-main drift is informational');
 receipt.boundaries={callbacks:'Controlled nonthrowing/nonreentrant callbacks; memory handles reject Windows sentinel and deleted handles',json:'Actual evidence-private calibration JSON reads/merge/serialization; simulated Win32 file replacement',mode:'Callback-completed intent only, no native mode readback or OEM write',rollback:'Live XYZ sampled once at manual begin; weight from last managed value, not a complete native internal-state snapshot',notExecuted:'Hardware/DLL/raw reader/PBT/Host/game/Windows/Serial/UI or real concurrent scheduling; existing logging exception policy is not expanded'};
 receipt.status='passed';receipt.finishedAt=new Date().toISOString();receipt.finalAvailableBytes=space();save();json('FILES.json',{files:walk(out).filter(p=>path.basename(p)!=='FILES.json').map(p=>({path:rel(p),bytes:fs.statSync(p).size,sha256:sha(fs.readFileSync(p))}))});console.log(JSON.stringify({status:receipt.status,out,wholeMainDrifted:receipt.stability.wholeMainDrifted}));
}catch(error){receipt.status='failed';receipt.error=String(error.stack||error);receipt.finishedAt=new Date().toISOString();try{if(!/DISK_STOP|ENOSPC/.test(String(error)))save();}catch{}console.error(receipt.error);console.error('Preserved evidence: '+out);process.exitCode=1;}
