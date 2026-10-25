/** Executes the production native JSON policy; no services, registry or devices. */
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {outputDirectory,repo} from './boot_modes_fixture/harness.mjs';
const out=outputDirectory(process.argv.slice(2),'native');
const msvc='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207',sdk='C:/Program Files (x86)/Windows Kits/10',version='10.0.26100.0';
const env={...process.env,INCLUDE:[msvc+'/include',sdk+'/Include/'+version+'/um',sdk+'/Include/'+version+'/shared',sdk+'/Include/'+version+'/ucrt',sdk+'/Include/'+version+'/winrt',sdk+'/Include/'+version+'/cppwinrt',repo+'/deps/json',repo+'/native'].join(';'),LIB:[msvc+'/lib/x64',sdk+'/Lib/'+version+'/um/x64',sdk+'/Lib/'+version+'/ucrt/x64'].join(';'),PATH:[msvc+'/bin/Hostx64/x64','C:/Windows/System32',process.env.PATH].join(';')};
const header=fs.readFileSync(path.join(repo,'native/input_startup_policy.h'),'utf8'),cpp=fs.readFileSync(path.join(repo,'tools/input_startup_policy_native_selftest.cpp'),'utf8');
const marker='initializeInputStartup(input, startup, session, !preserve)';assert(header.includes(marker));
const variants=[['current',header,true],['negative-process-replay',header.replace(marker,'initializeInputStartup(input, startup, session, true)'),false]];
const runs=[];
for(const [name,source,pass] of variants){
 const dir=path.join(out,name);fs.mkdirSync(dir,{recursive:true});fs.writeFileSync(path.join(dir,'input_startup_policy.h'),source);const file=path.join(dir,'consumer.cpp'),exe=path.join(dir,'consumer.exe');fs.writeFileSync(file,cpp);
 const compile=spawnSync(msvc+'/bin/Hostx64/x64/cl.exe',['/nologo','/EHsc','/std:c++20','/utf-8','/W4',file,'/Fo'+path.join(dir,'consumer.obj'),'/Fe'+exe],{cwd:dir,env,windowsHide:true,encoding:'utf8',timeout:45000});fs.writeFileSync(path.join(dir,'build.log'),(compile.stdout||'')+(compile.stderr||''));assert.equal(compile.status,0,compile.stdout+compile.stderr);
 const run=spawnSync(exe,[],{cwd:dir,windowsHide:true,encoding:'utf8',timeout:10000});fs.writeFileSync(path.join(dir,'result.log'),(run.stdout||'')+(run.stderr||''));assert.equal(run.status===0,pass,name+' unexpected result '+run.stdout+run.stderr);runs.push({name,pass:true,negative:!pass,...(pass?{consumer:JSON.parse(run.stdout)}:{originalProcessReplayRejected:true})});
}
// Read-only process-boundary proof for the actual Windows identity provider.
const identityDir=path.join(out,'windows-identity');fs.mkdirSync(identityDir,{recursive:true});
const identityCpp=path.join(identityDir,'identity.cpp'),identityExe=path.join(identityDir,'identity.exe');
fs.writeFileSync(identityCpp,'#include "gpu_dedicated_memory_runtime.h"\n#include <iostream>\nint main(){const auto id=ymcc::dedicatedmemory::bootIdentity();std::cout<<id;return id.empty()?2:0;}\n');
const identityBuild=spawnSync(msvc+'/bin/Hostx64/x64/cl.exe',['/nologo','/EHsc','/std:c++20','/utf-8','/DNOMINMAX',identityCpp,'/Fo'+path.join(identityDir,'identity.obj'),'/Fe'+identityExe,'/link','Ole32.lib'],{cwd:identityDir,env,windowsHide:true,encoding:'utf8',timeout:45000});fs.writeFileSync(path.join(identityDir,'build.log'),(identityBuild.stdout||'')+(identityBuild.stderr||''));assert.equal(identityBuild.status,0,identityBuild.stdout+identityBuild.stderr);
const identities=[];for(let n=0;n<2;n++){const result=spawnSync(identityExe,[],{cwd:identityDir,windowsHide:true,encoding:'utf8',timeout:5000});assert.equal(result.status,0,result.stdout+result.stderr);assert.match(result.stdout,/^windows-loader-v1\|/);identities.push(result.stdout);}
assert.equal(identities[0],identities[1]);const readOnlyIdentity={provider:'ymcc::dedicatedmemory::bootIdentity',processes:2,stableAcrossProcesses:true,observedIdentity:identities[0],systemWrites:0};
// Exercise the production volatile-key producer in its own disposable HKCU key.
// Resetting this isolated key models hive lifetime; no production HKLM key, app,
// service or actual reboot is touched. Cross-process reads run the real Win32 API.
const kernelDir=path.join(out,'kernel-session');fs.mkdirSync(kernelDir,{recursive:true});
const kernelCpp=path.join(kernelDir,'kernel.cpp'),kernelExe=path.join(kernelDir,'kernel.exe');
fs.writeFileSync(kernelCpp,String.raw`#include "boot_mode_identity.h"
#include <iostream>
int wmain(int argc,wchar_t** argv){if(argc!=3)return 2;std::wstring key=argv[1];if(key.rfind(L"Software\\YeManCC-Tests\\BootIdentity-",0)!=0)return 3;if(std::wstring(argv[2])==L"drop"){auto e=RegDeleteTreeW(HKEY_CURRENT_USER,key.c_str());return e==ERROR_SUCCESS||e==ERROR_FILE_NOT_FOUND?0:4;}auto v=ymcc::bootModeIdentity::observe(HKEY_CURRENT_USER,key.c_str(),L"Local\\YeManCC-Test-BootIdentity");if(v.identity.empty()){std::cerr<<v.error;return 5;}std::cout<<v.identity;return 0;}
`);
const kernelBuild=spawnSync(msvc+'/bin/Hostx64/x64/cl.exe',['/nologo','/EHsc','/std:c++20','/utf-8','/DNOMINMAX',kernelCpp,'/Fo'+path.join(kernelDir,'kernel.obj'),'/Fe'+kernelExe,'/link','Ole32.lib','Advapi32.lib'],{cwd:kernelDir,env,windowsHide:true,encoding:'utf8',timeout:45000});fs.writeFileSync(path.join(kernelDir,'build.log'),(kernelBuild.stdout||'')+(kernelBuild.stderr||''));assert.equal(kernelBuild.status,0,kernelBuild.stdout+kernelBuild.stderr);
const isolatedKey='Software\\YeManCC-Tests\\BootIdentity-'+process.pid+'-'+Date.now();
const kernel=(op)=>{const r=spawnSync(kernelExe,[isolatedKey,op],{windowsHide:true,encoding:'utf8',timeout:6000});assert.equal(r.status,0,r.stdout+r.stderr);return r.stdout;};
let kernelSession;
try{const before=kernel('observe'),same=kernel('observe');assert.match(before,/^windows-volatile-v2\|\{/);assert.equal(before,same);kernel('drop');const next=kernel('observe');assert.notEqual(next,before);const sameNext=kernel('observe');assert.equal(next,sameNext);kernelSession={pass:true,producer:'ymcc::bootModeIdentity::observe',isolatedHive:'HKCU test key only',observedBefore:before,observedSameBoot:same,afterIsolatedHiveReset:next,stableAcrossFourHelperProcesses:true,changesAfterIsolatedHiveReset:true,physicalReboot:false};}finally{kernel('drop');}
fs.writeFileSync(path.join(kernelDir,'result.json'),JSON.stringify(kernelSession,null,2));
const inputs=['native/main.cpp','native/input_startup_policy.h','native/boot_mode_identity.h','native/controller_persona_settings.h','native/gpu_dedicated_memory_runtime.h','tools/input_startup_policy_native_selftest.cpp','tools/boot_mode_startup_selftest.mjs'];
const result={pass:true,readOnlyIdentity,kernelSession,scope:'Production native input startup consumer with in-memory settings, fixed observed boot/process identities, and a semantic replay negative; read-only Windows boot identity is also checked in two separate helper processes. No installed app, hardware, Windows boot, services or production registry changes; only a disposable HKCU fixture key is created and removed.',runs,sourceHashes:Object.fromEntries(inputs.map(file=>[file,createHash('sha256').update(fs.readFileSync(path.join(repo,file))).digest('hex')]))};fs.writeFileSync(path.join(out,'boot-mode-startup-native.json'),JSON.stringify(result,null,2));console.log(JSON.stringify({pass:true,cases:runs[0].consumer.passed,negativeCases:1}));
