import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';
// Actual controller diagnostics/export functions, frozen and compiled offline.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath, pathToFileURL} from 'node:url';
const runner=fileURLToPath(import.meta.url),repo=path.resolve(path.dirname(runner),'..');
const workspace=path.resolve(repo,'../../..');
const root=path.join(workspace,'Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/CapabilityDiagnostics/Consumer');
const fixture=path.join(repo,'tools/fixtures/controller_motion_evidence_consumer');
const args={};
for(let i=2;i<process.argv.length;++i){const key=process.argv[i];assert(['--out','--vcvars'].includes(key)&&process.argv[i+1],key);args[key.slice(2)]=process.argv[++i];}
function space(){const s=fs.statfsSync(workspace);const available=s.bavail*s.bsize;assert(available>=768*1024*1024,'DISK_STOP: less than 768 MiB available');return available;}
space();
assert(fs.readFileSync(path.join(repo,'native/legion_motion_message_clock.h'),'utf8').includes('receivedAt > epochStart'),'Final strict queue epoch header not ready');
assert(fs.readFileSync(path.join(repo,'native/main.cpp'),'utf8').includes('"queue-time-not-after-epoch"'),'Final diagnostics reason not ready');
const out=path.resolve(args.out||path.join(root,new Date().toISOString().replace(/[:.]/g,'-')+'-'+crypto.randomBytes(3).toString('hex')));
assert.equal(path.dirname(out).toLowerCase(),root.toLowerCase(),'Evidence must be a fresh direct child');
fs.mkdirSync(root,{recursive:true});assert.equal(fs.realpathSync(root).toLowerCase(),path.resolve(root).toLowerCase());fs.mkdirSync(out);
const rel=p=>path.relative(out,p).replaceAll('\\','/');
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const load=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const write=(name,text)=>{space();const p=path.join(out,name);fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,text);};
const json=(name,value)=>write(name,JSON.stringify(value,null,2)+'\n');
const walk=d=>fs.readdirSync(d,{withFileTypes:true}).sort((a,b)=>a.name.localeCompare(b.name)).flatMap(e=>e.isDirectory()?walk(path.join(d,e.name)):e.isFile()?[path.join(d,e.name)]:[]);
const receipt={schema:1,status:'running',startedAt:new Date().toISOString(),initialAvailableBytes:space(),scope:'Seven exact production functions with real Lease inspect/current and production age/queue epoch helpers; controlled identity/clock/file boundary',actualHardware:false,actualDllLoaded:false,actualHidIo:false,actualPbt:false,ipcExecuted:false,logPackageExecuted:false,realWin32AtomicFileExecuted:false,inputs:[],configurations:[],mutations:[]};
const save=()=>json('RESULT.json',receipt);
const run=(exe,argv,log,cwd=out,env=process.env)=>{
 space();const r=spawnSync(exe,argv,{cwd,env,windowsHide:true,encoding:'utf8',timeout:60000,maxBuffer:4*1024*1024});
 write(log,(r.stdout||'')+(r.stderr||''));if(r.error)throw r.error;assert.equal(r.signal,null,log);return r;
};
const ok=(r,label)=>{assert.equal(r.status,0,label+'; evidence '+out);return r.stdout;};
function once(text,from,to){assert.equal(text.split(from).length-1,1,'Mutation anchor must be unique: '+from);return text.replace(from,to);}
try{
 save();
 const inputFiles=[runner,path.join(repo,'tools/fixtures/hc_reviewed_calibration/toolchain.mjs'),path.join(repo,'tools/lib/hc_controller_motion_fixture.mjs'),...walk(fixture),path.join(repo,'tools/fixtures/legion_motion_consumer/extract_main.mjs'),...['legion_motion_source.h','legion_motion_message_clock.h','virtual_output_status.h'].map(n=>path.join(repo,'native',n)),path.join(repo,'deps/json/json.hpp')];
 for(const p of inputFiles){const bytes=fs.readFileSync(p),snapshot='source/'+path.relative(repo,p).replaceAll('\\','/');write(snapshot,bytes);receipt.inputs.push({path:path.relative(repo,p).replaceAll('\\','/'),snapshot,bytes:bytes.length,sha256:sha(bytes)});}
 const frozenFixture=path.join(out,'source/tools/fixtures/controller_motion_evidence_consumer');
 const extraction=await import(pathToFileURL(path.join(frozenFixture,'extract_production.mjs')).href);
 const bindings=path.join(out,'bindings');fs.mkdirSync(bindings);
 const frozen=extraction.freeze(repo,bindings);receipt.bindingFile='bindings/SOURCE-BINDINGS.json';save();
 let vcvars=args.vcvars;
 if(!vcvars){
  const vswhere=path.join(process.env['ProgramFiles(x86)']||'C:/Program Files (x86)','Microsoft Visual Studio/Installer/vswhere.exe');
  const install=ok(run(vswhere,['-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],'toolchain-discovery.log'),'MSVC discovery').trim();
  assert(install&&!/[\r\n]/.test(install));vcvars=path.join(install,'VC/Auxiliary/Build/vcvars64.bat');
 }
 assert(fs.existsSync(vcvars)&&!/[\r\n"%&|<>^!]/.test(vcvars));
 const setup=path.join(out,'compiler-environment.cmd');assert(!/[\r\n"%&|<>^!]/.test(setup));
 const keys=['VCToolsInstallDir','VCToolsVersion','WindowsSDKVersion','INCLUDE','LIB','LIBPATH','PATH'];
 write('compiler-environment.cmd',calibrationToolchainPreamble(vcvars)+keys.map(k=>`echo ${k}=%${k}%\r\n`).join(''));
 // Deliberately never run/persist an unfiltered set/environment dump.
 const setupResult=spawnSync(process.env.ComSpec||'cmd.exe',['/d','/c',setup],{encoding:'utf8',windowsHide:true,timeout:30000});
 if(setupResult.error)throw setupResult.error;assert.equal(setupResult.status,0,'vcvars setup');
 const env={...process.env};for(const line of setupResult.stdout.split(/\r?\n/)){const i=line.indexOf('=');if(i<=0)continue;const key=line.slice(0,i);if(!keys.includes(key))continue;const old=Object.keys(env).find(k=>k.toLowerCase()===key.toLowerCase());env[old||key]=line.slice(i+1);}
 for(const key of Object.keys(env))if(['cl','_cl_','link','_link_'].includes(key.toLowerCase()))delete env[key];
 const compiler=path.join(env.VCToolsInstallDir,'bin/Hostx64/x64/cl.exe');
 receipt.compiler={path:compiler,sha256:sha(fs.readFileSync(compiler)),vcvars,vcToolsVersion:env.VCToolsVersion,windowsSdkVersion:env.WindowsSDKVersion,environmentCapture:'explicit toolchain variables only; environment values not persisted'};save();
 const native=path.join(out,'source/native'),harness=path.join(frozenFixture,'harness.cpp'),jsonDir=path.join(out,'source/deps/json');
 const configurations=[['Release',['/O2','/DNDEBUG','/MT']],['Checked',['/Od','/RTC1','/Zi','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MTd']]];
 function build(where,flags,generated=bindings,headerDir=native){
  space();fs.mkdirSync(where,{recursive:true});const exe=path.join(where,'evidence-consumer.exe'),deps=path.join(where,'dependencies.json');
  const argv=['/nologo','/std:c++17','/EHsc','/utf-8','/W4','/WX','/permissive-','/fp:precise',...flags,'/sourceDependencies',deps,`/I${generated}`,`/I${bindings}`,`/I${headerDir}`,`/I${native}`,`/I${jsonDir}`,harness,`/Fo${path.join(where,'harness.obj')}`,`/Fd${path.join(where,'compiler.pdb')}`,`/Fe${exe}`,'/link','/INCREMENTAL:NO'];
  ok(run(compiler,argv,rel(where)+'/compile.log',where,env),'C++ compile '+rel(where));
  const dep=load(deps).Data;assert.equal(path.resolve(dep.Source).toLowerCase(),harness.toLowerCase());
  const actual=[dep.Source,...dep.Includes];
  for(const expected of [path.join(generated,'production-functions.generated.h'),path.join(bindings,'production-types.generated.h'),path.join(fs.existsSync(path.join(headerDir,'legion_motion_source.h'))?headerDir:native,'legion_motion_source.h'),path.join(fs.existsSync(path.join(headerDir,'legion_motion_message_clock.h'))?headerDir:native,'legion_motion_message_clock.h'),path.join(native,'virtual_output_status.h'),path.join(jsonDir,'json.hpp')])assert(actual.some(p=>path.resolve(p).toLowerCase()===expected.toLowerCase()),'Missing actual compile input '+expected);
  json(rel(where)+'/actual-compile-inputs.json',actual.map(p=>({path:p.toLowerCase().startsWith(out.toLowerCase()+path.sep)?rel(p):p,sha256:sha(fs.readFileSync(p))})));
  const r=run(exe,[],rel(where)+'/run.log',where,env);const match=/^SUMMARY scenarios=(\d+) checks=(\d+) failures=(\d+)\r?$/m.exec(r.stdout);
  assert(match,'Missing semantic summary; build failure/crash/timeout is not a mutant kill');
  const cases=[...r.stdout.matchAll(/^CASE ([a-z-]+) checks=(\d+) failures=(\d+)\r?$/gm)].map(m=>({name:m[1],checks:+m[2],failures:+m[3]}));
  assert.equal(cases.length,+match[1]);assert.equal(cases.reduce((n,c)=>n+c.checks,0),+match[2]);assert.equal(cases.reduce((n,c)=>n+c.failures,0),+match[3]);
  return {exitCode:r.status,scenarios:+match[1],checks:+match[2],failures:+match[3],cases,buildArguments:argv,executableSha256:sha(fs.readFileSync(exe)),actualCompileInputs:rel(where)+'/actual-compile-inputs.json'};
 }
 for(const [configuration,flags]of configurations){const result=build(path.join(out,configuration),flags);receipt.configurations.push({configuration,...result});save();assert.equal(result.exitCode,0,configuration+' failed');assert.equal(result.failures,0);console.log(`${configuration}: PASS (${result.scenarios} scenarios, ${result.checks} checks)`);}
 const clock=fs.readFileSync(path.join(native,'legion_motion_message_clock.h'),'utf8');
 assert(clock.includes('receivedAt > epochStart'),'Final strict queue epoch header required');
 const production=fs.readFileSync(path.join(bindings,'production-functions.generated.h'),'utf8'),lease=fs.readFileSync(path.join(native,'legion_motion_source.h'),'utf8');
 const mutants=[
  {name:'omit-final-context-stability',from:'const bool sampleCurrent=stable && observation.sampleCurrent',to:'const bool sampleCurrent=observation.sampleCurrent',witness:'retirement-and-expiry-during-read'},
  {name:'omit-completion-freshness',from:'stable && observation.sampleCurrent && queueCurrent && freshAtCompletion;',to:'stable && observation.sampleCurrent && queueCurrent;',witness:'retirement-and-expiry-during-read'},
  {name:'unsupported-false-signal-claim',from:'{"sampleObserved",nullptr}',to:'{"sampleObserved",false}',witness:'four-models-and-unsupported'},
  {name:'failed-export-returns-stale-path',function:'exportControllerMotionEvidence',from:'return {};',to:'return L"stale-controller-motion-evidence.json";',witness:'failed-export-never-reuses-old-file'},
  {name:'lose-last-error-restoration',from:'SetLastError(value);',to:'SetLastError(value+1);',witness:'successful-read-only-export'},
  {name:'same-tick-epoch-admitted',clock:true,from:'receivedAt > epochStart',to:'receivedAt >= epochStart',witness:'age-and-epoch-boundaries'},
  {name:'inspect-consumes-lease',lease:true,from:'auto admitted = current(context, source, ready, now, maxAgeMs);',to:'auto admitted = current(context, source, ready, now, maxAgeMs);\n        const_cast<Lease*>(this)->revoke();',witness:'non-consuming-lease-and-snapshot'}
 ];
 for(const mutant of mutants){
  const where=path.join(out,'mutants',mutant.name),generated=path.join(where,'generated'),headerDir=(mutant.lease||mutant.clock)?generated:native;fs.mkdirSync(generated,{recursive:true});
  const selected=mutant.function?frozen.entries.find(row=>row.symbol===mutant.function):null;
  if(mutant.function)assert(selected,'Exact production mutation function missing');
  const altered=(mutant.lease||mutant.clock)?production:selected?once(production,selected.code,once(selected.code,mutant.from,mutant.to)):once(production,mutant.from,mutant.to);
  write(rel(generated)+'/production-functions.generated.h',altered);
  if(mutant.clock)write(rel(generated)+'/legion_motion_message_clock.h',once(clock,mutant.from,mutant.to));
  if(mutant.lease)write(rel(generated)+'/legion_motion_source.h',once(lease,mutant.from,mutant.to));
  const result=build(path.join(where,'build'),configurations[0][1],generated,headerDir);
  const killed=result.exitCode===1&&result.failures>0&&result.cases.some(c=>c.name===mutant.witness&&c.failures>0);
  receipt.mutations.push({...mutant,killed,...result});save();assert(killed,'Mutant survived or failed without semantic witness: '+mutant.name);console.log('Mutant killed: '+mutant.name);
 }
 receipt.stability={...extraction.verify(repo,frozen),inputs:receipt.inputs.map(p=>({path:p.path,sha256:p.sha256,stable:sha(fs.readFileSync(path.join(repo,p.path)))===p.sha256}))};
 assert(receipt.stability.methodsStable&&receipt.stability.typeStable&&receipt.stability.anchorsStable&&receipt.stability.inputs.every(p=>p.stable),'Tested inputs drifted');
 receipt.boundaries={executed:'Actual production status/export/snapshot/source/getter/route/power-name with real Lease inspect/current; memory atomic-write success/failure, controlled queue times and source retirement',staticOnly:'IPC registration/delegation; logs.exportAll successful-current-path vs missing branch; manifest line',notExecuted:'Full IPC/log ZIP package; actual Win32 persistence or HID discovery; raw reader/hardware/firmware sampling; Host/game consumption; physical acceptance',configuration:'No settings globals or setters are provided to this isolated translation unit; query/export can only access declared controlled read state and memory evidence writer',identityInvariant:'Identity replacement must advance capture epoch; race fixture changes identity plus epoch'};
 receipt.status='passed';receipt.finishedAt=new Date().toISOString();receipt.finalAvailableBytes=space();save();
 json('FILES.json',{note:'Self inventory excluded; all failed historical runs retained',files:walk(out).filter(p=>path.basename(p)!=='FILES.json').map(p=>({path:rel(p),bytes:fs.statSync(p).size,sha256:sha(fs.readFileSync(p))}))});
 console.log(JSON.stringify({status:receipt.status,out,stability:receipt.stability.methodsStable,wholeMainDrifted:receipt.stability.wholeMainDrifted}));
}catch(error){
 receipt.status='failed';receipt.error=String(error.stack||error);receipt.finishedAt=new Date().toISOString();
 // On low-space/write failure preserve existing evidence; never attempt cleanup or a new run.
 try{if(!/DISK_STOP|ENOSPC/.test(String(error)))save();}catch{}
 console.error(receipt.error);console.error('Preserved evidence: '+out);process.exitCode=1;
}
