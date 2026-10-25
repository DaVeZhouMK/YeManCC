// Actual physical enumeration/selection/reconnect plus original bounded caller;
// every device/transport/readback API is an inert boundary.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {spawnSync} from 'node:child_process';import {fileURLToPath} from 'node:url';
import {sha,fn,type,names} from './fixtures/physical_visibility_reconnect/source-binding.mjs';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..'),fixtures=path.join(repo,'tools/fixtures/physical_visibility_reconnect');
const options={out:path.resolve(repo,'../../../Build/Validation/VirtualInput-Submission-R113-20261016/ownership-reconnect/formal'),source:path.join(repo,'native/main.cpp'),expectedFailures:0,runtime:path.join(repo,'native/msi_native_foreground_isolation_runtime.h')};
for(let i=2;i<process.argv.length;i++){const key={'--out':'out','--source':'source','--expected-failures':'expectedFailures','--runtime':'runtime'}[process.argv[i]];assert(key&&process.argv[i+1]);const value=process.argv[++i];options[key]=key==='expectedFailures'?Number(value):path.resolve(value);}
assert(Number.isSafeInteger(options.expectedFailures)&&options.expectedFailures>=0);
const run=path.join(options.out,new Date().toISOString().replace(/[:.]/g,'-'));fs.mkdirSync(run,{recursive:true});
const sourcePath=options.source,bytes=fs.readFileSync(sourcePath),text=bytes.toString('utf8');fs.writeFileSync(path.join(run,'main.snapshot.cpp'),bytes);
const runtimeBytes=fs.readFileSync(options.runtime),runtimeText=runtimeBytes.toString('utf8');fs.writeFileSync(path.join(run,'runtime.snapshot.h'),runtimeBytes);
const ownedHelper=text.includes('static void hidHideAppendOwnedReconnectTargets(');
const familyNames=['nativeForegroundAppendVirtualTargets'];if(runtimeText.includes('static bool nativeForegroundVirtualTargetsAllowed()'))familyNames.unshift('nativeForegroundVirtualTargetsAllowed');
const family=Object.fromEntries(familyNames.map(n=>[n,fn(runtimeText,n)]));
const selectedNames=ownedHelper?[...names.slice(0,2),'hidHideAppendOwnedReconnectTargets',...names.slice(2)]:names;
const types=['HidHidePhysicalIdentity','HidHideVisibilitySnapshot','HidHideVisibilityJournal','HidHidePeriodicState','HidHideMsiMainSourceRecord','HidHideInterfaceRecord'];
const helpers=['ascii_lower','trim_ascii','hidHideUpper','hidHideIsVirtualIdentity','hidHideContainsPath','hidHideAppendTarget','hidHidePhysicalTargets','hidHideMsiSiblingSourceAdmitted','hidHidePhysicalIdentityKeys'];
const actual=Object.fromEntries([...helpers,...selectedNames].map(n=>[n,fn(text,n)]));
const caller=fn(text,'inputHostReconcilePhysicalSourceOnLifecycleThread');
const budgetStart=caller.body.indexOf('    const bool sourceOnline =');
const budgetEnd=caller.body.indexOf('    // A visibility drift',budgetStart);
if(budgetStart<0||budgetEnd<0)throw Error('budget block anchors');
const budgetBody=caller.body.slice(budgetStart,budgetEnd);
const constants=['kInputHostReapplyHideCooldownMs','kInputHostReapplyGuardMaxRepairs'].map(n=>{const m=text.match(new RegExp('^static constexpr [^\\n;]*\\b'+n+' = [^;]*;','m'));if(!m)throw Error(n);return m[0];}).join('\n');
const template=fs.readFileSync(path.join(fixtures,'fixture.cpp'),'utf8');
const cpp=template.replace('/*ACTUAL_TYPES*/',type(text,'HidHideIfaceRole','enum class')+'\n'+types.map(n=>type(text,n)).join('\n')+'\n'+type(text,'HidResolveReject','enum class'))
.replace('/*ACTUAL_HELPERS*/',helpers.map(n=>actual[n].body).join('\n')).replace('/*ACTUAL_FUNCTIONS*/',selectedNames.map(n=>actual[n].body).join('\n'))
.replace('/*ACTUAL_CONSTANTS*/',constants)
.replace('/*ACTUAL_FAMILY_PLANNER*/',Object.values(family).map(f=>f.body).join('\n'))
.replace('/*ACTUAL_OWNED_PROGRESS*/',ownedHelper?type(text,'HidHideOwnedGameRepairProgress')+'\nstatic HidHideOwnedGameRepairProgress g_hidHideOwnedGameRepairProgress;\nvoid resetOwnedDiscovery(){g_hidHideOwnedGameRepairProgress={};}':'void resetOwnedDiscovery(){}')
.replace('/*ACTUAL_BUDGET*/','void fixtureReconcileBudget(const HidHideVisibilityJournal& journal,const HidHidePeriodicState& currentVisibility,bool readback=true){\n'+budgetBody+'\n}');
fs.writeFileSync(path.join(run,'fixture.cpp'),cpp);
const vc=process.env.YMCC_VCVARS64??'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';const profiles={};
for(const profile of ['Release','Checked']){
 const out=path.join(run,profile);fs.mkdirSync(out);const flags=profile==='Release'?'/O2 /MT /DNDEBUG':'/Od /MTd /RTC1 /D_DEBUG';
 const cmd=path.join(out,'build.cmd');fs.writeFileSync(cmd,'@echo off\r\ncall "'+vc+'" >nul\r\nif errorlevel 1 exit /b 1\r\nset VSLANG=1033\r\ncl /nologo /std:c++20 /utf-8 /EHsc /W4 /WX /permissive- /sdl '+flags+' /I"'+path.join(repo,'native')+'" /I"'+path.join(repo,'deps/json')+'" "'+path.join(run,'fixture.cpp')+'" /FoFixture.obj /FeFixture.exe\r\nexit /b %errorlevel%\r\n');
 let r=spawnSync('cmd.exe',['/d','/c',cmd],{cwd:out,encoding:'utf8',windowsHide:true,timeout:60000});fs.writeFileSync(path.join(out,'build.log'),(r.stdout??'')+(r.stderr??''));assert.equal(r.status,0,r.stdout+r.stderr);
 r=spawnSync(path.join(out,'Fixture.exe'),[],{cwd:out,encoding:'utf8',windowsHide:true,timeout:15000});fs.writeFileSync(path.join(out,'results.json'),r.stdout??'');const result=JSON.parse(r.stdout);const failed=result.checks.filter(x=>!x.pass);assert.equal(failed.length,options.expectedFailures,JSON.stringify(failed));assert.equal(r.status,options.expectedFailures?1:0);
 profiles[profile]={passed:true,assertions:result.checks.length,expectedFailures:options.expectedFailures,failed,scenarios:result.scenarios,exeSha256:sha(fs.readFileSync(path.join(out,'Fixture.exe')))};console.log(profile+': '+result.checks.length+' checks, '+failed.length+' expected failures');
}
const sourceStable=sha(fs.readFileSync(sourcePath))===sha(bytes);assert(sourceStable,'Source changed while fixture executed');
const result={passed:true,run,runtimeSource:options.runtime,runtimeSha256:sha(runtimeBytes),familyFunctions:Object.values(family).map(f=>({name:f.name,line:f.line,sha256:sha(f.body)})),sourcePath,sourceSha256:sha(bytes),sourceStable,profiles,functions:Object.values(actual).map(f=>({name:f.name,line:f.line,sha256:sha(f.body)})),callerBudget:{name:caller.name,line:caller.line,sha256:sha(budgetBody)},fixtureSha256:sha(cpp),boundaries:'actual enumeration, selection and reapply; identity probe, SetupAPI and driver/transport/store inert; no hardware acceptance',productionWrittenByHarness:false,hardwareOperations:0};fs.writeFileSync(path.join(run,'RESULT.json'),JSON.stringify(result,null,2)+'\n');fs.writeFileSync(path.join(options.out,'LATEST.json'),JSON.stringify(result,null,2)+'\n');console.log('Evidence: '+run);
