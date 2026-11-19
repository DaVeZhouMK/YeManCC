import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';
import {controllerInputFixtureInputs,snapshotControllerInputFixture} from './lib/hc_controller_input_fixture.mjs';
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {spawnSync} from 'node:child_process';import {fileURLToPath} from 'node:url';
import {extract,sha} from './fixtures/msi_owned_return_retired_io/source-binding.mjs';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');let out=path.resolve(repo,'../../../Build/Validation/VirtualInput-CaptureProof-R122-20261016/power-gate');
let resumeOnly=false,selectedCase=null,outSupplied=false;
let sharedPowerOnly=false,prepareOnly=false,sharedMain=null,sharedHeaders=null,expectedMainSha256=null;
for(let i=2;i<process.argv.length;i++){
 const arg=process.argv[i];
 if(arg==='--resume-only'){assert(!resumeOnly);resumeOnly=true;}
 else if(arg==='--shared-power-only'){assert(!sharedPowerOnly);sharedPowerOnly=true;}
 else if(arg==='--prepare-only'){assert(!prepareOnly);prepareOnly=true;}
 else if(arg==='--main'){assert(!sharedMain&&process.argv[i+1]&&!process.argv[i+1].startsWith('--'));sharedMain=path.resolve(process.argv[++i]);}
 else if(arg==='--header-root'){assert(!sharedHeaders&&process.argv[i+1]&&!process.argv[i+1].startsWith('--'));sharedHeaders=path.resolve(process.argv[++i]);}
 else if(arg==='--expected-main-sha256'){assert(!expectedMainSha256&&/^[a-f0-9]{64}$/.test(process.argv[i+1]??''));expectedMainSha256=process.argv[++i];}
 else if(arg==='--out'){assert(process.argv[i+1]);out=path.resolve(process.argv[++i]);outSupplied=true;}
 else if(arg==='--case'){assert(process.argv[i+1]&&!process.argv[i+1].startsWith('--')&&!selectedCase);selectedCase=process.argv[++i];}
 else throw Error('UNKNOWN_POWER_GATE_ARGUMENT: '+arg);
}
assert(!(resumeOnly&&sharedPowerOnly),'POWER_GATE_MODES_ARE_EXCLUSIVE');
assert(!selectedCase||resumeOnly||sharedPowerOnly,'CASE_REQUIRES_EXPLICIT_MODE');
assert(!prepareOnly||sharedPowerOnly||resumeOnly,'PREPARE_REQUIRES_EXPLICIT_POWER_MODE');
assert((!sharedMain&&!sharedHeaders&&!expectedMainSha256)||sharedPowerOnly||resumeOnly,'SOURCE_OVERRIDE_REQUIRES_EXPLICIT_MODE');
assert(!resumeOnly||!sharedMain||expectedMainSha256,'INPUT_RESUME_MAIN_OVERRIDE_REQUIRES_SHA256');
if(sharedPowerOnly){
 assert(prepareOnly&&outSupplied,'SHARED_POWER_REQUIRES_PREPARE_ONLY_AND_EXPLICIT_OUTPUT');
 const {prepareSharedPowerFixture}=await import('./fixtures/input_power_gate_r122/shared-power-prepare.mjs');
 const prepared=await prepareSharedPowerFixture({repo,mainFile:sharedMain??path.join(repo,'native/main.cpp'),
  headerRoot:sharedHeaders??path.join(repo,'native'),out,selectedCase,expectedMainSha256});
 console.log(JSON.stringify({status:prepared.status,out,main:prepared.mainObserved,
  preparedRecipes:prepared.actualPreparedRecipeCount,compiles:0,executions:0,fullParentExecuted:false}));
 process.exit(0);
}
if(resumeOnly){
 assert(outSupplied,'INPUT_RESUME_EXPLICIT_OUTPUT_REQUIRED');
 const {runInputResumeFixture}=await import('./fixtures/input_power_gate_r122/resume.mjs');
 await runInputResumeFixture({repo,out,selectedCase,mainFile:sharedMain??path.join(repo,'native/main.cpp'),headerRoot:sharedHeaders??path.join(repo,'native'),expectedMainSha256,prepareOnly});
 process.exit(process.exitCode??0);
}
const run=path.join(out,new Date().toISOString().replace(/[:.]/g,'-')),inc=path.join(run,'source');fs.mkdirSync(inc,{recursive:true});
const files=[...controllerInputFixtureInputs,'native/main.cpp','native/input_frame_submission.h','native/touch_haptic_dispatch.h','native/game_feedback_receipt.h','native/legion_motion_source.h','native/windows_motion_submission.h','native/virtual_output_status.h','native/msi_virtual_report_diagnostic.h','native/input_host_shared_power.h','native/sleep_power_state.h','native/sleep_power_observation.h','native/sleep_power_revocation.h','deps/json/json.hpp','tools/fixtures/input_power_gate_r122/power.cpp'];
const bytes=new Map(files.map(n=>[n,fs.readFileSync(path.join(repo,n))]));const main=bytes.get('native/main.cpp').toString('utf8'),bindings=[];
const fn=name=>extract(main,'function',name,'native/main.cpp',bindings),struct=name=>extract(main,'struct',name,'native/main.cpp',bindings);
const powerEnum=main.match(/^enum class PowerLifecycle\s*:\s*uint8_t\s*\{[^\n]+\};\r?$/m);assert(powerEnum);bindings.push({symbol:'PowerLifecycle',kind:'enum',file:'native/main.cpp',startLine:main.slice(0,powerEnum.index).split('\n').length,endLine:main.slice(0,powerEnum.index).split('\n').length,sha256:sha(powerEnum[0])});
const familyEnum=main.match(/^enum class YmccFamily\s*:\s*int\s*\{[\s\S]*?^\};/m);assert(familyEnum,'production family enum missing');bindings.push({symbol:'YmccFamily',kind:'enum',file:'native/main.cpp',sha256:sha(familyEnum[0])});
const tlsScope=main.match(/^static thread_local std::shared_ptr<ymcc::inputHost::sharedPower::Scope> t_inputHostRequestPower;\r?$/m);assert(tlsScope,'production TLS public scope missing');
bindings.push({symbol:'t_inputHostRequestPower',kind:'declaration',file:'native/main.cpp',sha256:sha(tlsScope[0])});
const parts={TLS_SCOPE:tlsScope[0],FAMILY_ENUM:familyEnum[0],POWER_ENUM:powerEnum[0],KEEP:fn('sgShouldKeepInputDuringStandby'),WRITER:fn('sgVirtualWriterAllowedNow'),SNAPSHOT:struct('InputCaptureGamepadSnapshot'),FRAME:struct('InputFrameSubmission'),REQUEST_SCOPE:struct('InputHostRequestPowerScope'),BIND_STANDARD_INPUT:fn('inputCaptureBindStandardInput'),SOURCE_ADMITTED:fn('inputHostPhysicalPublicationSourceAdmitted'),SAMPLE:fn('inputHostFrameSubmissionSample'),CURRENT:fn('inputHostFrameSubmissionCurrent'),WINDOWS_CURRENT:fn('inputHostWindowsMotionSubmissionCurrent'),SHARED_CURRENT:fn('inputHostFrameSharedPowerCurrent'),MAY_ISSUE:fn('inputHostFrameSubmissionMayIssue')};
let fixture=bytes.get('tools/fixtures/input_power_gate_r122/power.cpp').toString('utf8');snapshotControllerInputFixture(repo,path.join(run,'controller-input'));for(const[k,v]of Object.entries(parts)){assert.equal(fixture.split('@@'+k+'@@').length,2);fixture=fixture.replace('@@'+k+'@@',()=>v);}assert(!/@@\w+@@/.test(fixture));
fs.writeFileSync(path.join(run,'fixture.cpp'),fixture);for(const n of files.filter(n=>n.endsWith('.h')||n.endsWith('json.hpp')))fs.writeFileSync(path.join(inc,path.basename(n)),bytes.get(n));
const save=(name,data)=>fs.writeFileSync(path.join(run,name),JSON.stringify(data,null,2)+'\n');save('source-binding.json',{repo,sourceHashes:Object.fromEntries([...bytes].map(([n,b])=>[n,sha(b)])),extracted:bindings,compiledParts:Object.entries(parts).map(([symbol,code])=>({symbol,sha256:sha(code)})),runnerSha256:sha(fs.readFileSync(fileURLToPath(import.meta.url))),noDeviceIo:true,limits:'actual extracted public power predicates/frame current/sample/issue; controlled Reader plus actual shared Scope/RevocationMirrors Windows events; production motion types and Windows guard; all motion pointers null, Legion predicate fail-closed; no Native power callbacks, physical devices, pipe or Host'});
const vc=process.env.YMCC_VCVARS64??'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';assert(fs.existsSync(vc));const q=s=>{assert(!/["\r\n]/.test(s));return '"'+s+'"';};const results={};
for(const profile of ['Release','Checked'])for(const diag of [false,true]){const label=profile+(diag?'-diagnostic-on':'-diagnostic-off'),dir=path.join(run,label);fs.mkdirSync(dir);const exe=path.join(dir,'fixture.exe'),cmd=path.join(dir,'build.cmd'),flags=profile==='Release'?'/O2 /MT /DNDEBUG':'/Od /MTd /RTC1 /D_DEBUG';fs.writeFileSync(cmd,calibrationToolchainPreamble(vc)+'set VSLANG=1033\r\ncl /nologo /std:c++20 /utf-8 /EHsc /W4 /WX /permissive- /sdl '+flags+(diag?' /DYMCC_MSI_REPORT_CHAIN_DIAGNOSTIC':'')+' /I'+q(inc)+' '+q(path.join(run,'fixture.cpp'))+' /Fo'+q(path.join(dir,'fixture.obj'))+' /Fe'+q(exe)+'\r\nexit /b %errorlevel%\r\n');let r=spawnSync('cmd.exe',['/d','/c',cmd],{cwd:dir,encoding:'utf8',timeout:60000});fs.writeFileSync(path.join(dir,'build.log'),r.stdout+r.stderr);assert.equal(r.status,0,r.stdout+r.stderr);r=spawnSync(exe,[],{cwd:dir,encoding:'utf8',timeout:15000});fs.writeFileSync(path.join(dir,'results.json'),r.stdout);assert.equal(r.status,0,r.stdout+r.stderr);const j=JSON.parse(r.stdout);assert.equal(j.failures.length,0);results[label]={passed:true,cases:j.cases.length,assertions:j.assertions,exeSha256:sha(fs.readFileSync(exe))};console.log(label,JSON.stringify(results[label]));}
const mainAfter=fs.readFileSync(path.join(repo,'native/main.cpp'));
const changedParts=Object.entries(parts).filter(([,code])=>code&&!mainAfter.toString('utf8').includes(code)).map(([name])=>name);
assert.deepEqual(changedParts,[],'actual tested source fragments changed');
const changed=files.filter(n=>n!=='native/main.cpp'&&sha(fs.readFileSync(path.join(repo,n)))!==sha(bytes.get(n)));assert.equal(changed.length,0,'relevant dependency changed during test');
const sourceStability={relatedPartsStable:true,changedParts,dependenciesStable:true,wholeFileChanged:sha(mainAfter)!==sha(bytes.get('native/main.cpp')),sha256AtEnd:sha(mainAfter)};const result={passed:true,run,results,sourceChangedDuringRun:changed,sourceStability,productionChanges:false,noDeviceIo:true,actualPowerCallbacksExecuted:false,fieldAcceptance:false};save('RESULT.json',result);fs.writeFileSync(path.join(out,'LATEST.json'),JSON.stringify(result,null,2)+'\n');console.log('Evidence: '+run);
