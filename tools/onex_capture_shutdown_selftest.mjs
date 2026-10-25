// Offline exact-production OneX shutdown and late-terminal ownership regression.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
import {extract,sha} from './fixtures/msi_owned_return_retired_io/source-binding.mjs';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const defaults=path.resolve(repo,'../../../Build/Validation/HC-ModelCapabilities-20261017/OneXLifecycle');
const args=process.argv.slice(2),options={};
for(let i=0;i<args.length;++i){if(args[i]==='--mutations')options.mutations=true;else if(['--out','--source'].includes(args[i])&&args[i+1])options[args[i].slice(2)]=args[++i];else throw Error('Unknown argument '+args[i]);}
const out=path.resolve(options.out||path.join(defaults,'selftest'));
const sourcePath=path.resolve(options.source||path.join(repo,'native/main.cpp'));
assert(!out.startsWith(repo+path.sep),'Evidence must remain outside production checkout');
fs.mkdirSync(out,{recursive:true});
const main=fs.readFileSync(sourcePath,'utf8'),bindings=[];
const environmentPath=path.join(repo,'tools/fixtures/onex_capture_shutdown/environment.h');
const harnessPath=path.join(repo,'tools/fixtures/onex_capture_shutdown/harness.cpp');
const names=['oemProducerName','oemProducerCurrent','oemProducerCancel','onexBackFinalizeClose','onexBackSettleWrite','onexBackRetireStuck','onexBackRetireReadStuck','onexBackServiceWrite','onexBackServiceRead','onexBackIoQuiescent','onexBackServiceIo','onexBackClose','onexBackReadFaultRevoke','oemHidClear','onexBackSetEnabled','onexBackPump','oemVendorHidShutdown'];
const fn=name=>extract(main,'function',name,sourcePath,bindings);
const definitions=names.map(fn);
const exact=(pattern,name)=>{const m=main.match(pattern);assert(m,name);bindings.push({symbol:name,kind:'definition',file:sourcePath,startLine:main.slice(0,m.index).split('\n').length,sha256:sha(m[0])});return m[0];};
const enumText=exact(/^enum class OnexInitProfile[^\n]+$/m,'OnexInitProfile');
const descriptor=extract(main,'struct','OnexCapDescriptor',sourcePath,bindings);
const session=extract(main,'struct','OnexBackSession',sourcePath,bindings);
const state=exact(/^static OnexBackSession g_onexBack;$/m,'g_onexBack');
const constants=[...main.matchAll(/^static constexpr [^\n]*\bkOnex\w+[^\n]*;/gm)].map(m=>m[0]).join('\n');
assert(constants.includes('kOnexBackReadDrainLimitMs')&&constants.includes('kOnexBackWriteDrainLimitMs'));
const bind=fn('onexBackBind');const start=bind.indexOf('{')+1,end=bind.indexOf('    HDEVINFO set');assert(end>start);
const bindPrefix=bind.slice(start,end);
bindings.push({symbol:'onexBackBind:admission-prefix',kind:'compiled-production-fragment',sha256:sha(bindPrefix)});
const producerStart=main.indexOf('enum class OemProducerId'),producerStop=main.indexOf('static const char* oemProducerName(',producerStart);assert(producerStop>producerStart);
const producerState=main.slice(producerStart,producerStop);bindings.push({symbol:'producer-state-and-contract',kind:'compiled-production-definitions',sha256:sha(producerState)});
const canonicalBits=[...main.matchAll(/^static constexpr uint64_t kOemBit[^;]+;/gm)].map(m=>m[0]).join('\n');
const cpp=[fs.readFileSync(environmentPath,'utf8'),canonicalBits,producerState,enumText,descriptor,session,state,constants,
 'static bool onexBackBind();static void onexBackInitPump();static bool onexBackStartRead();static void onexBackParse(const BYTE*,size_t,uint64_t);',
 ...definitions.map(code=>code.slice(0,code.indexOf('{')).trim()+';'),
 `static bool extractedBindAdmission(){${bindPrefix}\nreturn true;}`,
 ...definitions,
 'static uint64_t& fixtureProducerGeneration=g_oemProducerStates[static_cast<size_t>(OemProducerId::OneX)].generation;\nstatic uint64_t& fixtureProducerMask=g_oemProducerStates[static_cast<size_t>(OemProducerId::OneX)].mask;\nstatic void onexBackParse(const BYTE*,size_t,uint64_t generation){require(oemProducerCurrent(OemProducerId::OneX,generation),"stale parse callback");++fixtureParses;}',
 fs.readFileSync(harnessPath,'utf8')].join('\n\n');
fs.writeFileSync(path.join(out,'fixture.cpp'),cpp);
const vc='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
assert(fs.existsSync(vc));
const q=s=>{assert(!/["\r\n]/.test(s));return '"'+s+'"';};
const evidence={schema:1,source:{path:sourcePath,sha256:sha(main)},bindings,fixtureInputs:{environment:sha(fs.readFileSync(environmentPath)),harness:sha(fs.readFileSync(harnessPath))},configurations:[],mutations:[],noDeviceIo:true,realWindowsPowerCallbacks:false,realDriverCancellation:false};
const save=()=>fs.writeFileSync(path.join(out,'RESULT.json'),JSON.stringify(evidence,null,2)+'\n');
for(const [name,flags] of [['Release','/O2 /MT /DNDEBUG'],['Checked','/Od /MTd /RTC1 /D_DEBUG']]){
 const dir=path.join(out,name);fs.mkdirSync(dir,{recursive:true});const exe=path.join(dir,'fixture.exe');
 const command='@echo off\r\ncall '+q(vc)+' >nul\r\nif errorlevel 1 exit /b 1\r\nset VSLANG=1033\r\ncl /nologo /std:c++20 /utf-8 /EHsc /W4 /wd4505 '+flags+' '+q(path.join(out,'fixture.cpp'))+' /I'+q(path.join(repo,'deps/json'))+' /Fo'+q(path.join(dir,'fixture.obj'))+' /Fe'+q(exe)+'\r\nexit /b %errorlevel%\r\n';
 const cmd=path.join(dir,'build.cmd');fs.writeFileSync(cmd,command);
 let r=spawnSync('cmd.exe',['/d','/c',cmd],{cwd:dir,encoding:'utf8',timeout:60000,windowsHide:true});fs.writeFileSync(path.join(dir,'build.log'),(r.stdout||'')+(r.stderr||''));assert.equal(r.status,0,r.stdout+r.stderr);
 r=spawnSync(exe,[],{cwd:dir,encoding:'utf8',timeout:15000,windowsHide:true});fs.writeFileSync(path.join(dir,'run.json'),r.stdout||'');const report=JSON.parse(r.stdout);evidence.configurations.push({name,exitCode:r.status,exeSha256:sha(fs.readFileSync(exe)),...report});save();
 assert.equal(r.status,0,r.stdout+r.stderr);assert.equal(report.status,'pass');console.log(`${name}: ${report.cases.length} cases / ${report.checks} checks PASS`);
}
if(options.mutations){
 const cases=[
  ['missing-onex-shutdown','actual-source-ledger-generation-and-queue',main.replace('    g_onexBack.enabled = false; onexBackClose();','')],
  ['retired-read-never-reaped','dual-pending-shutdown-and-restart',main.replace('    // Keep observing the retained event after retirement; never publish a draining read.','    if (g_onexBack.readStuck) return;')],
  ['retired-write-never-reaped','dual-pending-shutdown-and-restart',main.replace('    // Retirement blocks new I/O, but a late terminal event still releases this owned slot.','    if (g_onexBack.writeStuck) return;')],
  ['premature-close-one-side-pending','dual-pending-shutdown-and-restart',main.replace('    if (g_onexBack.readPending || g_onexBack.writeInFlight) return;','    if (g_onexBack.readPending && g_onexBack.writeInFlight) return;')]
 ];
 for(const [name,section,text]of cases){assert.notEqual(text,main,name+' mutation anchor');const dir=path.join(out,'mutations',name);fs.mkdirSync(dir,{recursive:true});const file=path.join(dir,'main.cpp');fs.writeFileSync(file,text);const childOut=path.join(dir,'evidence');const r=spawnSync(process.execPath,[fileURLToPath(import.meta.url),'--source',file,'--out',childOut],{encoding:'utf8',timeout:60000,windowsHide:true});fs.writeFileSync(path.join(dir,'driver.log'),(r.stdout||'')+(r.stderr||''));const run=path.join(childOut,'Release/run.json');assert(fs.existsSync(run),name+' must compile then fail semantically');const failure=JSON.parse(fs.readFileSync(run,'utf8'));assert.notEqual(r.status,0);assert.equal(failure.status,'fail');assert.equal(failure.section,section);evidence.mutations.push({name,section,detected:true,failure});save();}
 console.log(`Mutations: ${evidence.mutations.length}/4 semantic rejections`);
}
const current=fs.readFileSync(sourcePath,'utf8');evidence.sourceChangedDuringRun=sha(current)!==sha(main);evidence.selectedRangesStable=names.every(name=>extract(current,'function',name,sourcePath,[])===extract(main,'function',name,sourcePath,[]));assert(evidence.selectedRangesStable,'Selected source changed during run');
evidence.passed=true;save();console.log('Evidence: '+out);
