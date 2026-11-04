// Offline exact-production OneX shutdown and late-terminal ownership regression.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';
import {fixedOracle} from './fixtures/onex_capture_shutdown/oracle.mjs';
import {extract,sha} from './fixtures/msi_owned_return_retired_io/source-binding.mjs';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const defaults=path.resolve(repo,'../../../Build/Validation/HC-ModelCapabilities-20261017/OneXLifecycle');
const args=process.argv.slice(2),options={};
for(let i=0;i<args.length;++i){if(args[i]==='--mutations')options.mutations=true;else if(['--out','--source'].includes(args[i])&&args[i+1])options[args[i].slice(2)]=args[++i];else throw Error('Unknown argument '+args[i]);}
const out=path.resolve(options.out||path.join(defaults,'selftest'));
const sourcePath=path.resolve(options.source||path.join(repo,'native/main.cpp'));
const relativeOut=path.relative(repo,out);
assert(relativeOut&& (relativeOut.startsWith('..'+path.sep)||path.isAbsolute(relativeOut)),'Evidence must remain outside production checkout');
assert(!fs.existsSync(path.join(out,'fixture.cpp'))&&!fs.existsSync(path.join(out,'RESULT.json')),'Refusing to overwrite an earlier run or FAIL; choose a fresh --out');
fs.mkdirSync(out,{recursive:true});
const main=fs.readFileSync(sourcePath,'utf8'),bindings=[];
const fixtureDir=path.join(repo,'tools/fixtures/onex_capture_shutdown');
const environmentPath=path.join(fixtureDir,'environment.h'),harnessPath=path.join(fixtureDir,'harness.cpp');
const fixedRoot=path.resolve(repo,'../../../Testing/YMCC/Evidence/HC-Code-Capability-Upgrade-20261031/upstream/source/HandheldCompanion');
const oracle=fixedOracle(fixedRoot);
fs.writeFileSync(path.join(out,'ORACLE.json'),JSON.stringify(oracle,null,2)+'\n');
// Copy the actual recursively included quoted-header closure. Mutations alter only
// the private native source; all contract/codec calls still compile the real header.
const headerInputs=[],seen=new Set();
function captureHeader(file){
 const relative=path.relative(repo,file);assert(!relative.startsWith('..')&&!path.isAbsolute(relative),'header escaped repo');
 if(seen.has(relative))return;seen.add(relative);
 const bytes=fs.readFileSync(file),copy=path.join(out,'headers',relative);
 fs.mkdirSync(path.dirname(copy),{recursive:true});fs.writeFileSync(copy,bytes);
 headerInputs.push({file:relative,original:file,copy,sha256:sha(bytes)});
 for(const m of bytes.toString('utf8').matchAll(/^\s*#include\s+"([^"\r\n]+)"/gm))captureHeader(path.resolve(path.dirname(file),m[1]));
}
captureHeader(path.join(repo,'capabilities/HcOemInputCapabilityBinding.h'));
assert(headerInputs.some(x=>x.file.replaceAll('\\','/')==='capabilities/HcOneXVendorInputContract.h'),'OneX contract must be in compiled closure');
const names=['oemProducerName','oemProducerCurrent','oemProducerBegin','oemProducerCancel','oemProducerPublish','onexCapDescriptorFor','onexBackFinalizeClose','onexBackSettleWrite','onexBackRetireStuck','onexBackRetireReadStuck','onexBackServiceWrite','onexBackServiceRead','onexBackIoQuiescent','onexBackServiceIo','onexBackClose','onexBackReadFaultRevoke','oemHidClear','onexBackApplyDescriptor','onexBackSubmitWrite','onexInitStepAt','onexBackInitPump','onexBackRequestReinit','onexBackReadHidCaps','onexBackBind','onexBackParse','onexBackStartRead','onexBackSetEnabled','onexBackPump','oemVendorHidShutdown'];
const fn=name=>extract(main,'function',name,sourcePath,bindings);
const definitions=names.map(fn);
const exact=(pattern,name)=>{const m=main.match(pattern);assert(m,name);bindings.push({symbol:name,kind:'definition',file:sourcePath,startLine:main.slice(0,m.index).split('\n').length,sha256:sha(m[0])});return m[0];};
const enumText=exact(/^enum class OnexInitProfile[^\n]+$/m,'OnexInitProfile');
const submitEnum=exact(/^enum class OnexWriteSubmit[^\n]+$/m,'OnexWriteSubmit');
const descriptor=extract(main,'struct','OnexCapDescriptor',sourcePath,bindings);
const session=extract(main,'struct','OnexBackSession',sourcePath,bindings);
const state=exact(/^static OnexBackSession g_onexBack;$/m,'g_onexBack');
const constants=[...main.matchAll(/^static constexpr [^\n]*\bkOnex\w+[^\n]*;/gm)].map(m=>m[0]).join('\n');
assert(constants.includes('kOnexBackReadDrainLimitMs')&&constants.includes('kOnexBackWriteDrainLimitMs'));
const producerStart=main.indexOf('enum class OemProducerId'),producerStop=main.indexOf('// Only the new OneX sources use this context fence.',producerStart);assert(producerStop>producerStart);
const producerState=main.slice(producerStart,producerStop);bindings.push({symbol:'producer-state-and-contract',kind:'compiled-production-definitions',sha256:sha(producerState)});
const canonicalBits=[...main.matchAll(/^static constexpr uint64_t kOemBit[^;]+;/gm)].map(m=>m[0]).join('\n');
bindings.push({symbol:'canonical-OEM-bits',kind:'compiled-production-definitions',file:sourcePath,sha256:sha(canonicalBits)},{symbol:'OneX-constants',kind:'compiled-production-definitions',file:sourcePath,sha256:sha(constants)});
const declarations=definitions.map(code=>code.slice(0,code.indexOf('{')).trim()+';');
// Default arguments belong on declarations once, never again on definitions.
const bodies=definitions.map(code=>code.replace(/uint64_t observedMask = UINT64_MAX/g,'uint64_t observedMask'));
const cpp=[fs.readFileSync(environmentPath,'utf8'),
 '#include '+JSON.stringify(headerInputs[0].copy.replaceAll('\\','/')),
 'namespace contract=ymcc::capabilities::oem::onex;',
 `static const json fixtureOracle=json::parse(R"ORACLE(${JSON.stringify(oracle.plans)})ORACLE");`,
 canonicalBits,producerState,
 'static bool oemProducerContextCurrent(OemProducerId source){return source!=OemProducerId::OneXX2Rear&&source!=OemProducerId::OneXX2Home;}',
 enumText,submitEnum,descriptor,session,state,constants,...declarations,...bodies,
 'static uint64_t& fixtureProducerGeneration=g_oemProducerStates[static_cast<size_t>(OemProducerId::OneX)].generation;\nstatic uint64_t& fixtureProducerMask=g_oemProducerStates[static_cast<size_t>(OemProducerId::OneX)].mask;',
 fs.readFileSync(harnessPath,'utf8')].join('\n\n');
fs.writeFileSync(path.join(out,'fixture.cpp'),cpp);
const vc='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
assert(fs.existsSync(vc));
// Resolve the existing child-only toolchain once, then pass compiler arguments
// directly. Keep sourceDependencies so every header actually compiled is pinned.
const envCmd=path.join(out,'compiler-environment.cmd');
fs.writeFileSync(envCmd,calibrationToolchainPreamble(vc)+'set PATH\r\nset INCLUDE\r\nset LIB\r\nset VCTools\r\nset WindowsSdk\r\nexit /b 0\r\n');
const prepared=spawnSync(process.env.ComSpec||'cmd.exe',['/d','/c',envCmd],{encoding:'utf8',timeout:30000,windowsHide:true});
fs.writeFileSync(path.join(out,'compiler-environment.log'),prepared.stderr||'');
assert.equal(prepared.status,0,'MSVC environment preparation failed');
const compilerEnv={...process.env,VSLANG:'1033'};
for(const line of (prepared.stdout||'').split(/\r?\n/)){
 const at=line.indexOf('=');if(at<=0)continue;const key=line.slice(0,at),existing=Object.keys(compilerEnv).find(k=>k.toLowerCase()===key.toLowerCase());compilerEnv[existing||key]=line.slice(at+1);
}
const envValue=name=>compilerEnv[Object.keys(compilerEnv).find(k=>k.toLowerCase()===name.toLowerCase())]||'';
const tool=name=>{const file=envValue('PATH').split(';').map(p=>path.join(p,name)).find(p=>fs.existsSync(p));assert(file,'Missing actual '+name);return {file,sha256:sha(fs.readFileSync(file))};};
const compiler=tool('cl.exe'),linker=tool('link.exe');
const evidence={schema:2,headerInputs,oracle,
 toolInputs:[fileURLToPath(import.meta.url),process.execPath,vc,path.join(fixtureDir,'oracle.mjs'),path.join(repo,'tools/fixtures/msi_owned_return_retired_io/source-binding.mjs'),path.join(repo,'tools/fixtures/hc_reviewed_calibration/toolchain.mjs'),path.join(repo,'deps/json/json.hpp')].map(file=>({file,sha256:sha(fs.readFileSync(file))})),
 toolchain:{compiler,linker,pathCharacters:envValue('PATH').length,pathEntries:envValue('PATH').split(';').length,include:envValue('INCLUDE'),lib:envValue('LIB'),vcToolsVersion:envValue('VCToolsVersion'),windowsSdkVersion:envValue('WindowsSDKVersion')},
 source:{path:sourcePath,sha256:sha(main)},bindings,fixtureInputs:{environment:sha(fs.readFileSync(environmentPath)),harness:sha(fs.readFileSync(harnessPath))},fixtureInputPaths:[environmentPath,harnessPath].map(file=>({file,sha256:sha(fs.readFileSync(file))})),generatedCppSha256:sha(cpp),configurations:[],mutations:[],noDeviceIo:true,realWindowsPowerCallbacks:false,realDriverCancellation:false};
const save=()=>fs.writeFileSync(path.join(out,'RESULT.json'),JSON.stringify(evidence,null,2)+'\n');
save();
for(const [name,flags] of [['Release',['/O2','/MT','/DNDEBUG']],['Checked',['/Od','/MTd','/RTC1','/D_DEBUG']]]){
 const dir=path.join(out,name);fs.mkdirSync(dir,{recursive:true});const exe=path.join(dir,'fixture.exe'),dependencyFile=path.join(dir,'dependencies.json');
 const argv=['/nologo','/std:c++20','/utf-8','/EHsc','/W4','/wd4505',...flags,path.join(out,'fixture.cpp'),'/I'+path.join(repo,'deps/json'),'/sourceDependencies',dependencyFile,'/Fo'+path.join(dir,'fixture.obj'),'/Fe'+exe];
 fs.writeFileSync(path.join(dir,'command.json'),JSON.stringify({file:compiler.file,argv},null,2)+'\n');
 let r=spawnSync(compiler.file,argv,{cwd:dir,env:compilerEnv,encoding:'utf8',timeout:60000,windowsHide:true});fs.writeFileSync(path.join(dir,'build.log'),(r.stdout||'')+(r.stderr||''));
 const configuration={name,compileExitCode:r.status,flags};evidence.configurations.push(configuration);save();
 assert.equal(r.status,0,r.stdout+r.stderr);
 const dependencies=JSON.parse(fs.readFileSync(dependencyFile,'utf8').replace(/^\uFEFF/,''));
 assert(Array.isArray(dependencies.Data?.Includes),'Missing actual compiler include closure');
 configuration.compiledHeaders=[...new Set(dependencies.Data.Includes)].map(file=>({file,sha256:sha(fs.readFileSync(file))}));
 r=spawnSync(exe,[],{cwd:dir,encoding:'utf8',timeout:15000,windowsHide:true});fs.writeFileSync(path.join(dir,'run.json'),r.stdout||'');
 const report=JSON.parse(r.stdout);Object.assign(configuration,{exitCode:r.status,exeSha256:sha(fs.readFileSync(exe)),...report});save();
 assert.equal(r.status,0,r.stdout+r.stderr);assert.equal(report.status,'pass');assert.equal(report.cases.length,10,'Original ten cases must execute exactly once');console.log(`${name}: ${report.cases.length} cases / ${report.checks} checks PASS`);
}
if(options.mutations){
 // Each private mutation preserves the native function signature and must
 // compile before a specific original case reports a semantic assertion failure.
 const mutate=(name,before='',after='')=>{
  const original=extract(main,'function',name,sourcePath,[]),open=original.indexOf('{'),body=original.slice(open+1,-1);
  const changed=original.slice(0,open+1)+before+'\n'+(after?'auto fixtureOriginal=[&]()->bool{'+body+'};\nbool fixtureResult=fixtureOriginal();\n'+after+'\nreturn fixtureResult;':body)+'\n}';
  return main.replace(original,changed);
 };
 const cases=[
  ['missing-onex-shutdown','actual-source-ledger-generation-and-queue',main.replace('    g_onexBack.enabled = false; onexBackClose();','')],
  ['retired-read-never-reaped','dual-pending-shutdown-and-restart',main.replace('    // Keep observing the retained event after retirement; never publish a draining read.','    if (g_onexBack.readStuck) return;')],
  ['retired-write-never-reaped','dual-pending-shutdown-and-restart',main.replace('    // Retirement blocks new I/O, but a late terminal event still releases this owned slot.','    if (g_onexBack.writeStuck) return;')],
  ['premature-close-one-side-pending','dual-pending-shutdown-and-restart',main.replace('    if (g_onexBack.readPending || g_onexBack.writeInFlight) return;','    if (g_onexBack.readPending && g_onexBack.writeInFlight) return;')],
  ['legacy-mini-plan','actual-source-ledger-generation-and-queue',mutate('onexInitStepAt','if(p==OnexInitProfile::X1Mini){if(step<2)return onexInitStepAt(OnexInitProfile::X1,step,cmd,payload,n,delayAfterMs);if(step==2){static const BYTE motor[]={1,5,5};cmd=0xB3;payload=motor;n=3;delayAfterMs=0;return true;}}')],
  ['apex-wrong-prefix','actual-source-ledger-generation-and-queue',mutate('onexInitStepAt','','if(fixtureResult&&p==OnexInitProfile::Apex&&step<2){static BYTE wrong[59];std::memcpy(wrong,payload,n);wrong[2]=0x20;payload=wrong;}')],
  ['apex-wrong-delay','actual-source-ledger-generation-and-queue',mutate('onexInitStepAt','','if(fixtureResult&&p==OnexInitProfile::Apex&&step<3)delayAfterMs=100;')],
  ['drop-request-during-init','dual-pending-shutdown-and-restart',mutate('onexBackRequestReinit','if(!g_onexBack.initDone||g_onexBack.writeInFlight)return;')],
  ['wrong-input-offset','active-current-read-control',mutate('onexBackParse','if(len==65)len=64;')],
  ['wrong-collection-admitted','shutdown-completion-order-matrix',main.replace(/!ymcc::capabilities::oem::onex::capsAllowed\(\s*attrs\.VendorID,\s*attrs\.ProductID,\s*caps\.UsagePage,\s*caps\.Usage,\s*caps\.InputReportByteLength,\s*caps\.OutputReportByteLength\)/,'false')],
  ['reopen-drift-admitted','shutdown-completion-order-matrix',main.replace('reopenedInput != inputBytes || reopenedOutput != outputBytes','false')],
  ['read-blocked-during-init','shutdown-completion-order-matrix',main.replace('    // Fixed source reads before and during configuration; retain one independent read slot.','    if (!g_onexBack.initDone) return;')]

 ];
 for(const [name,section,text]of cases){assert.notEqual(text,main,name+' mutation anchor');const dir=path.join(out,'mutations',name);fs.mkdirSync(dir,{recursive:true});const file=path.join(dir,'main.cpp');fs.writeFileSync(file,text);const childOut=path.join(dir,'evidence');const r=spawnSync(process.execPath,[fileURLToPath(import.meta.url),'--source',file,'--out',childOut],{encoding:'utf8',timeout:60000,windowsHide:true});fs.writeFileSync(path.join(dir,'driver.log'),(r.stdout||'')+(r.stderr||''));const run=path.join(childOut,'Release/run.json');assert(fs.existsSync(run),name+' must compile then fail semantically');const child=JSON.parse(fs.readFileSync(path.join(childOut,'RESULT.json'),'utf8'));assert.equal(child.configurations[0].compileExitCode,0,name+' must compile successfully');const failure=JSON.parse(fs.readFileSync(run,'utf8'));assert.notEqual(r.status,0);assert.equal(failure.status,'fail');assert.equal(failure.section,section);evidence.mutations.push({name,section,detected:true,failure});save();}
 console.log(`Mutations: ${evidence.mutations.length}/${cases.length} semantic rejections`);
}
const current=fs.readFileSync(sourcePath,'utf8');evidence.sourceChangedDuringRun=sha(current)!==sha(main);evidence.selectedRangesStable=extract(current,'struct','OnexBackSession',sourcePath,[])===session&&extract(current,'struct','OnexCapDescriptor',sourcePath,[])===descriptor&&headerInputs.every(input=>sha(fs.readFileSync(input.original))===input.sha256)&&names.every(name=>extract(current,'function',name,sourcePath,[])===extract(main,'function',name,sourcePath,[]));assert(evidence.selectedRangesStable,'Selected source changed during run');
assert([...evidence.toolInputs,...evidence.fixtureInputPaths,compiler,linker].every(input=>sha(fs.readFileSync(input.file))===input.sha256),'Tool/fixture input changed during run');
const remainingDefinitionsStable=current.match(/^enum class OnexInitProfile[^\n]+$/m)?.[0]===enumText&&current.match(/^enum class OnexWriteSubmit[^\n]+$/m)?.[0]===submitEnum&&
 [...current.matchAll(/^static constexpr uint64_t kOemBit[^;]+;/gm)].map(m=>m[0]).join('\n')===canonicalBits&&
 [...current.matchAll(/^static constexpr [^\n]*\bkOnex\w+[^\n]*;/gm)].map(m=>m[0]).join('\n')===constants&&
 current.slice(current.indexOf('enum class OemProducerId'),current.indexOf('// Only the new OneX sources use this context fence.'))===producerState;
assert(remainingDefinitionsStable,'Compiled production definitions changed during run');
evidence.passed=true;save();console.log('Evidence: '+out);
