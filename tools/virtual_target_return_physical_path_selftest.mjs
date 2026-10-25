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
const relativeSources=['native/virtual_target_return_physical_path.h','native/virtual_target_return_record.h','deps/json/json.hpp',
  'tools/virtual_target_return_physical_path_selftest.mjs','tools/fixtures/virtual_target_return_physical_path/fixture.cpp','tools/fixtures/virtual_target_return_physical_path/README.md'];
const sources=relativeSources.map(relative=>({relative,...identity(path.join(repo,relative))}));
fs.mkdirSync(out,{recursive:true});fs.mkdirSync(path.join(out,'sources'));
for(const source of sources){source.snapshot=path.join(out,'sources',path.basename(source.path));fs.copyFileSync(source.path,source.snapshot);}
save(path.join(out,'SOURCES.json'),{sources,tools:toolInputs,toolchainReference:toolchain});
const variants=[
  {name:'Release',flags:['/O2']},
  {name:'Checked',flags:['/Od','/RTC1']},
  {name:'negative-unchecked-ancestor',before:'if(folded==expectedRoot)return {State::Proven,Reason::RootMatched,depth,CR_SUCCESS};',after:'if(true)return {State::Proven,Reason::RootMatched,depth,CR_SUCCESS};',requiredCase:'ancestry/same-vid-different-tree'},
  {name:'negative-read-error-proven',before:'if(read!=CR_SUCCESS)return {State::Unknown,Reason::DeviceIdReadFailed,depth,read};',after:'if(read!=CR_SUCCESS)return {State::Proven,Reason::DeviceIdReadFailed,depth,read};',requiredCase:'read/target-id-failure'},
  {name:'negative-direct-string-shortcut',before:'    // record::instance admits only its ASCII instance-ID alphabet.',after:'    if(rootInstanceId==targetInstanceId)return {State::Proven,Reason::RootMatched,0,CR_SUCCESS};\n    // record::instance admits only its ASCII instance-ID alphabet.',requiredCase:'direct/root-id-read-failure'}
];
const results=[],boundInputs=new Map();
try{
  for(const variant of variants){
    const dir=path.join(out,variant.name);fs.mkdirSync(dir);
    const generated=[];
    for(const source of sources.filter(s=>!s.relative.endsWith('.mjs')&&!s.relative.endsWith('.md'))){
      const file=path.join(dir,path.basename(source.path));fs.copyFileSync(source.snapshot,file);
      if(variant.before&&source.relative==='native/virtual_target_return_physical_path.h'){
        const text=fs.readFileSync(file,'utf8');assert.equal(text.split(variant.before).length,2,'exact semantic fault anchor');fs.writeFileSync(file,text.replace(variant.before,variant.after));
      }
      generated.push({...identity(file),origin:source.path,originalSha256:source.sha256});
    }
    save(path.join(dir,'GENERATED.json'),{inputs:generated,semanticFault:variant.before?variant:null});
    const exe=path.join(dir,'PhysicalPathFixture.exe');
    const importLibrary=path.join(dir,'PhysicalPathFixture.lib');
    const command=['/nologo','/std:c++20','/utf-8','/EHsc','/MT','/W4','/WX','/DUNICODE','/D_UNICODE',...(variant.flags??['/O2']),
      '/sourceDependencies',path.join(dir,'dependencies.json'),path.join(dir,'fixture.cpp'),'/I'+dir,
      '/Fo'+path.join(dir,'fixture.obj'),'/Fe'+exe,'/link','/VERBOSE:LIB','/IMPLIB:'+importLibrary,'cfgmgr32.lib'];
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
    assert(fs.existsSync(importLibrary),'compile-only export produces its named import-library output');
    const actualLibraryInputs=libraryPaths.filter(file=>path.resolve(file).toLowerCase()!==path.resolve(importLibrary).toLowerCase());
    assert(actualLibraryInputs.length>=3,'verbose linker inputs must be observable');
    const libraries=actualLibraryInputs.map(file=>{const binding=byTool.get(path.resolve(file).toLowerCase());assert(binding&&hash(file)===binding.sha256,'unbound or changed linker input: '+file);boundInputs.set(path.resolve(file).toLowerCase(),identity(file));return identity(file);});
    const imports=spawnSync(dump,['/imports',exe],{cwd:dir,env,encoding:'utf8',windowsHide:true,timeout:10000});
    fs.writeFileSync(path.join(dir,'imports.log'),(imports.stdout??'')+(imports.stderr??''));assert.equal(imports.status,0,'actual PE imports inspection');
    const dlls=[...new Set([...imports.stdout.matchAll(/^\s+([A-Za-z0-9_.-]+\.dll)\s*$/gm)].map(m=>m[1].toLowerCase()))];
    assert.deepEqual([...dlls].sort(),['cfgmgr32.dll','kernel32.dll'],'only bounded CM query ABI plus Win32/CRT import; no controller SDK');
    const exports=spawnSync(dump,['/exports',exe],{cwd:dir,env,encoding:'utf8',windowsHide:true,timeout:10000});
    fs.writeFileSync(path.join(dir,'exports.log'),(exports.stdout??'')+(exports.stderr??''));assert.equal(exports.status,0);
    assert(exports.stdout.includes('productionReaderCompileOnly'),'real ProductionReader is actually compiled and retained as an uncalled export');
    const executableIdentity=identity(exe);
    save(path.join(dir,'COMPILE-CLOSURE.json'),{compiler:identity(cl),binary:executableIdentity,generatedImportLibrary:identity(importLibrary),inputs:actualInputs,libraries,staticImportedDlls:dlls,runtimeModuleLoadingTraced:false});
    const ran=spawnSync(exe,[path.join(dir,'RESULT.json')],{cwd:dir,env,encoding:'utf8',windowsHide:true,timeout:45000,maxBuffer:4*1024*1024});
    fs.writeFileSync(path.join(dir,'run.log'),(ran.stdout??'')+(ran.stderr??''));
    assert(fs.existsSync(path.join(dir,'RESULT.json')),'fixture must produce a terminal result');
    assert.equal(hash(exe),executableIdentity.sha256,'executed binary did not change');
    const result=read(path.join(dir,'RESULT.json'));
    if(variant.before){assert.equal(ran.status,1,'semantic fault must compile and fail original assertions without crashing');assert(result.failures.some(f=>f.case===variant.requiredCase),'semantic fault must hit intended original case');}
    else{assert.equal(ran.status,0,'actual fixture assertions failed: '+path.join(dir,'RESULT.json'));assert(result.pass);}
    const receipt={variant:variant.name,positive:!variant.before,pass:variant.before?ran.status===1:result.pass,
      cases:result.caseCount,assertions:result.assertions,failureCount:result.failures.length,requiredCase:variant.requiredCase??null,
      compilerInputs:actualInputs.length,linkerLibraries:libraries.length,binary:executableIdentity,result:identity(path.join(dir,'RESULT.json'))};
    results.push(receipt);console.log(JSON.stringify(receipt));
  }
  const drift=[...sources,...toolInputs,...boundInputs.values()].filter(s=>!fs.existsSync(s.path)||hash(s.path)!==s.sha256);
  assert.equal(drift.length,0,'actual sources/tools/dependencies changed during execution');
  const result={pass:true,status:'PHYSICAL_ANCESTRY_LEAF_VERIFIED_RUNTIME_NOT_INTEGRATED',createdAt:new Date().toISOString(),sources,tools:toolInputs,
    variants:results,capturedToolchain:toolchain,sourceCurrent:true,boundToolchainInputs:[...boundInputs.values()],
    scope:{algorithm:'actual production verify template, only CM leaf reader controlled',deviceApisExecuted:false,
      productionReader:'real CM imports retained by exported compile-only function, never called by fixture',
      maximumNodeReads:16,maximumParentQueries:16,maximumLocateCalls:2,
      noPhysicalHardwareOrInstalledAppdata:true,checkedConfiguration:'/Od /RTC1 /MT; not an arithmetic-overflow sanitizer',
      productOrHostExecuted:false,deviceServiceDriverOrSteamOperations:false,
      runtimeIntegrated:false,physicalHardwareAccepted:false,wholeGoalComplete:false}};
  save(path.join(out,'RESULT.json'),result);console.log(JSON.stringify({pass:true,variants:results.length,positive:2,semanticFaults:variants.length-2,out}));
}catch(error){save(path.join(out,'FAILURE.json'),{pass:false,message:error.message,stack:error.stack,completedVariants:results,wholeGoalComplete:false});throw error;}
