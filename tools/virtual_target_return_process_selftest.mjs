import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';

const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const args=process.argv.slice(2);
assert(args.length===4&&args[0]==='--out'&&args[2]==='--toolchain'&&path.isAbsolute(args[1])&&path.isAbsolute(args[3]),
  'Use --out ABS_FRESH_PRIVATE_DIRECTORY --toolchain ABS_CAPTURED_TOOLCHAIN_DIRECTORY');
const out=path.resolve(args[1]),toolchain=fs.realpathSync(path.resolve(args[3]));
const inside=(p,r)=>{const rel=path.relative(r,p);return rel===''||(!rel.startsWith('..'+path.sep)&&rel!=='..'&&!path.isAbsolute(rel));};
const hash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const save=(p,j)=>fs.writeFileSync(p,JSON.stringify(j,null,2)+'\n');
const identity=p=>({path:p,sha256:hash(p),bytes:fs.statSync(p).size});
const capturePath=path.join(toolchain,'build-environment.json'),manifestPath=path.join(toolchain,'toolchain-manifest.json');
const capture=read(capturePath),manifest=read(manifestPath);
const toolInputs=[identity(capturePath),identity(manifestPath)];
const protectedRoots=[fs.realpathSync(repo),toolchain,...manifest.roots.map(r=>fs.realpathSync(r.frozen)),
  ...manifest.tools.filter(t=>['cl.exe','link.exe','dumpbin.exe'].includes(t.name)).map(t=>path.dirname(fs.realpathSync(t.original)))];
let existingParent=path.dirname(out);while(!fs.existsSync(existingParent))existingParent=path.dirname(existingParent);
const resolvedOut=path.resolve(fs.realpathSync(existingParent),path.relative(existingParent,out));
assert(!fs.existsSync(out)&&out!==path.parse(out).root&&!protectedRoots.some(r=>inside(resolvedOut,r)),
  'Output must be fresh, private and outside product sources, captured toolchain and tool roots');
const env={...capture.environment};
for(const key of Object.keys(env))if(/^(CL|_CL_|LINK|_LINK_)$/i.test(key))delete env[key];
env.INCLUDE=manifest.roots.filter(r=>r.category==='INCLUDE').map(r=>r.frozen).join(';');
env.LIB=manifest.roots.filter(r=>r.category==='LIB').map(r=>r.frozen).join(';');env.LIBPATH=env.LIB;
const cl=capture.tools['cl.exe'].paths[0],dump=capture.tools['dumpbin.exe'].paths[0];
for(const file of[cl,dump,capture.tools['link.exe'].paths[0]]){
  const bound=manifest.tools.find(t=>path.resolve(t.original).toLowerCase()===path.resolve(file).toLowerCase());
  assert(bound&&hash(file)===bound.sha256,'captured compiler/tool identity: '+file);toolInputs.push(identity(file));
}
const byTool=new Map(manifest.files.map(f=>[path.resolve(f.frozen).toLowerCase(),f]));
const relativeSources=['native/virtual_target_return_process.h','native/virtual_target_return_record.h','deps/json/json.hpp',
  'tools/virtual_target_return_process_selftest.mjs','tools/fixtures/virtual_target_return_process/fixture.cpp','tools/fixtures/virtual_target_return_process/README.md'];
const sources=relativeSources.map(relative=>({relative,...identity(path.join(repo,relative))}));
fs.mkdirSync(out,{recursive:true});fs.mkdirSync(path.join(out,'sources'));
for(const source of sources){source.snapshot=path.join(out,'sources',path.basename(source.path));fs.copyFileSync(source.path,source.snapshot);}
save(path.join(out,'SOURCES.json'),{sources,tools:toolInputs,toolchainReference:toolchain});
// Extend the existing leaf's positive/semantic-fault run; no additional parent gate.
const requiredCensusCases=[
  'controlled/census-open-denied-pid-reused','controlled/census-open-denied-pid-absent',
  'controlled/census-same-birth-alive','controlled/census-truncated-after-target-and-self',
  'controlled/census-missing-self','controlled/census-three-retries-exhausted',
  'controlled/census-returned-size-above-budget','controlled/census-negative-status-with-valid-payload',
  'real/census-open-denied-private-child'
];
const variants=[
  {name:'Release',flags:['/O2']},
  {name:'Checked',flags:['/Od','/RTC1']},
  {name:'negative-access-denial-as-dead',before:'return {State::Unknown, Reason::OpenFailed, failure};',after:'return {State::Dead, Reason::OpenFailed, failure};',requiredCase:'controlled/open-access-denied'},
  {name:'negative-pid-reuse-as-alive',before:'return {State::Dead, Reason::PidReused, ERROR_SUCCESS};',after:'return {State::Alive, Reason::PidReused, ERROR_SUCCESS};',requiredCase:'controlled/pid-reuse'},
  {name:'negative-terminal-as-alive',before:'if(waited == WAIT_OBJECT_0) return {State::Dead, Reason::MatchingExited, ERROR_SUCCESS};',after:'if(waited == WAIT_OBJECT_0) return {State::Alive, Reason::MatchingExited, ERROR_SUCCESS};',requiredCase:'real/probe-exited'},
  {name:'negative-uncertain-wait-as-dead',before:'if(waited == WAIT_FAILED) return {State::Unknown, Reason::WaitFailed, GetLastError()};',after:'if(waited == WAIT_FAILED) return {State::Dead, Reason::WaitFailed, GetLastError()};',requiredCase:'controlled/wait-error'},
  {name:'negative-census-live-as-dead',before:'result.state=targetSeen&&targetMatches?State::Alive:State::Dead;',after:'result.state=State::Dead;',requiredCase:'controlled/census-same-birth-alive'},
  {name:'negative-census-without-self-proof',before:'if(!selfSeen) {result.reason=Reason::CensusSelfUnproven;',after:'if(!selfSeen && self->creationTime.empty()) {result.reason=Reason::CensusSelfUnproven;',requiredCase:'controlled/census-missing-self'},
  {name:'negative-census-short-circuit-scan',before:'if(row.nextEntryOffset==0)break;',after:'if(row.nextEntryOffset==0 || (selfSeen&&targetSeen))break;',requiredCase:'controlled/census-truncated-after-target-and-self'},
  {name:'negative-census-fourth-query',before:'constexpr unsigned maxAttempts=3;',after:'constexpr unsigned maxAttempts=4;',requiredCase:'controlled/census-three-retries-exhausted'},
  {name:'negative-census-parse-failed-query',before:'if(status<0) {result.reason=Reason::CensusQueryFailed;',after:'if(status<0 && returned==0) {result.reason=Reason::CensusQueryFailed;',requiredCase:'controlled/census-negative-status-with-valid-payload'},
  {name:'negative-probe-handle-leak',before:'~OwnedHandle() { if(value) CloseHandle(value); }',after:'~OwnedHandle() { }',requiredCase:'real/probe-handle-count'}
];
const results=[],boundInputs=new Map();
try{
  for(const variant of variants){
    const dir=path.join(out,variant.name);fs.mkdirSync(dir);
    const generated=[];
    for(const source of sources.filter(s=>!s.relative.endsWith('.mjs')&&!s.relative.endsWith('.md'))){
      const file=path.join(dir,path.basename(source.path));fs.copyFileSync(source.snapshot,file);
      if(variant.before&&source.relative==='native/virtual_target_return_process.h'){
        const text=fs.readFileSync(file,'utf8');assert.equal(text.split(variant.before).length,2,'exact semantic fault anchor');fs.writeFileSync(file,text.replace(variant.before,variant.after));
      }
      generated.push({...identity(file),origin:source.path,originalSha256:source.sha256});
    }
    save(path.join(dir,'GENERATED.json'),{inputs:generated,semanticFault:variant.before?variant:null});
    const exe=path.join(dir,'ProcessIdentityFixture.exe');
    const command=['/nologo','/std:c++20','/utf-8','/EHsc','/MT','/W4','/WX','/DUNICODE','/D_UNICODE',...(variant.flags??['/O2']),
      '/sourceDependencies',path.join(dir,'dependencies.json'),path.join(dir,'fixture.cpp'),'/I'+dir,
      '/Fo'+path.join(dir,'fixture.obj'),'/Fe'+exe,'/link','/VERBOSE:LIB'];
    const built=spawnSync(cl,command,{cwd:dir,env,encoding:'utf8',windowsHide:true,timeout:60000,maxBuffer:16*1024*1024});
    const buildLog=(built.stdout??'')+(built.stderr??'');fs.writeFileSync(path.join(dir,'build.log'),buildLog);
    save(path.join(dir,'COMMAND.json'),{executable:cl,arguments:command,exit:built.status,error:built.error?.message??null});
    assert.equal(built.status,0,'actual compilation failed: '+path.join(dir,'build.log'));
    const dependency=read(path.join(dir,'dependencies.json'));
    const byGenerated=new Map(generated.map(f=>[path.resolve(f.path).toLowerCase(),f]));
    const actualInputs=[dependency.Data.Source,...dependency.Data.Includes].map(file=>{
      const key=path.resolve(file).toLowerCase(),binding=byGenerated.get(key)??byTool.get(key);
      assert(binding&&hash(file)===binding.sha256,'unbound or changed actual compiler input: '+file);
      if(byTool.has(key))boundInputs.set(key,identity(file));
      return{path:file,sha256:binding.sha256,scope:byGenerated.has(key)?'frozen actual source':'captured toolchain'};
    });
    const libraryPaths=[...new Set([...buildLog.matchAll(/([A-Za-z]:[\\/][^\r\n]*?\.lib)(?=[:\s(]|$)/gi)].map(m=>m[1].trim()))];
    assert(libraryPaths.length>=3,'verbose linker inputs must be observable');
    const libraries=libraryPaths.map(file=>{const binding=byTool.get(path.resolve(file).toLowerCase());assert(binding&&hash(file)===binding.sha256,'unbound or changed linker input: '+file);boundInputs.set(path.resolve(file).toLowerCase(),identity(file));return identity(file);});
    const imports=spawnSync(dump,['/imports',exe],{cwd:dir,env,encoding:'utf8',windowsHide:true,timeout:10000});
    fs.writeFileSync(path.join(dir,'imports.log'),(imports.stdout??'')+(imports.stderr??''));assert.equal(imports.status,0,'actual PE imports inspection');
    const dlls=[...new Set([...imports.stdout.matchAll(/^\s+([A-Za-z0-9_.-]+\.dll)\s*$/gm)].map(m=>m[1].toLowerCase()))];
    assert.deepEqual(dlls,['kernel32.dll'],'fixture has no statically imported controller or device libraries');
    const executableIdentity=identity(exe);
    save(path.join(dir,'COMPILE-CLOSURE.json'),{compiler:identity(cl),binary:executableIdentity,inputs:actualInputs,libraries,staticImportedDlls:dlls,runtimeModuleLoadingTraced:false});
    const ran=spawnSync(exe,[path.join(dir,'RESULT.json')],{cwd:dir,env,encoding:'utf8',windowsHide:true,timeout:45000,maxBuffer:4*1024*1024});
    fs.writeFileSync(path.join(dir,'run.log'),(ran.stdout??'')+(ran.stderr??''));
    assert(fs.existsSync(path.join(dir,'RESULT.json')),'fixture must produce a terminal result');
    assert.equal(hash(exe),executableIdentity.sha256,'executed binary did not change');
    const result=read(path.join(dir,'RESULT.json'));
    if(variant.before){assert.equal(ran.status,1,'semantic fault must compile and fail original assertions without crashing');assert(result.failures.some(f=>f.case===variant.requiredCase),'semantic fault must hit intended original case');}
    else{
      assert.equal(ran.status,0,'actual fixture assertions failed: '+path.join(dir,'RESULT.json'));assert(result.pass);
      for(const name of requiredCensusCases)assert(result.cases.some(c=>c.name===name&&c.passed),'required actual census case: '+name);
      const realCensus=result.observations.find(o=>o.basis==='real-ntquery-system-process-information');
      assert(realCensus&&realCensus.state==='alive'&&realCensus.censusRows>=2&&realCensus.censusAttempts>=1&&realCensus.censusAttempts<=3,
        'positive must actually delegate NtQuerySystemInformation and parse a real complete snapshot for its private child');
    }
    const receipt={variant:variant.name,positive:!variant.before,pass:variant.before?ran.status===1:result.pass,
      cases:result.caseCount,assertions:result.assertions,
      censusCases:result.cases.filter(c=>c.name.includes('/census-')).length,
      realCensus:result.observations.find(o=>o.basis==='real-ntquery-system-process-information')??null,
      failureCount:result.failures.length,requiredCase:variant.requiredCase??null,
      compilerInputs:actualInputs.length,linkerLibraries:libraries.length,binary:executableIdentity,result:identity(path.join(dir,'RESULT.json'))};
    results.push(receipt);console.log(JSON.stringify(receipt));
  }
  const drift=[...sources,...toolInputs,...boundInputs.values()].filter(s=>!fs.existsSync(s.path)||hash(s.path)!==s.sha256);
  assert.equal(drift.length,0,'actual sources/tools/dependencies changed during execution');
  const result={pass:true,status:'PROCESS_IDENTITY_LEAF_VERIFIED_RUNTIME_NOT_INTEGRATED',createdAt:new Date().toISOString(),sources,tools:toolInputs,
    variants:results,capturedToolchain:toolchain,sourceCurrent:true,boundToolchainInputs:[...boundInputs.values()],
    scope:{realPrivateProcessCaptureAliveExit:true,parentExitDoesNotEndWorker:true,realBorrowedEventRejected:true,realHandleCountChecked:true,
      pidReuse:'controlled Win32 creation-time fault, not observed OS PID reuse',openAccessDenial:'controlled OpenProcess fault; actual restricted-handle capture also exercised',
      realSystemProcessCensusForPrivateChild:true,censusAbiAndMalformedInputs:'controlled independently encoded snapshot bytes',
      boundedCensus:'at most 3 calls, 64KiB initial, 8MiB maximum; exact class 5 and self birth guard',
      ntstatusDiagnostic:'raw last status stored separately from Win32 error and original direct error',
      checkedConfiguration:'/Od /RTC1 /MT; not an arithmetic-overflow sanitizer',productOrHostExecuted:false,deviceServiceDriverOrSteamOperations:false,
      runtimeIntegrated:false,physicalHardwareAccepted:false,wholeGoalComplete:false}};
  save(path.join(out,'RESULT.json'),result);console.log(JSON.stringify({pass:true,variants:results.length,positive:2,semanticFaults:variants.length-2,out}));
}catch(error){save(path.join(out,'FAILURE.json'),{pass:false,message:error.message,stack:error.stack,completedVariants:results,wholeGoalComplete:false});throw error;}
