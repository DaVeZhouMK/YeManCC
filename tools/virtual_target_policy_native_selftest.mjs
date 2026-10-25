// Native policy/source-consumer validation only. The similarly named runner is frontend-owned.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import crypto from 'node:crypto';import {spawnSync} from 'node:child_process';import {fileURLToPath} from 'node:url';
import {extract} from './fixtures/legion_motion_consumer/extract_main.mjs';
import {readVirtualControllerSource,snapshotVirtualControllerSource} from './virtual_controller_runtime_source.mjs';
const runner=fileURLToPath(import.meta.url),repo=path.resolve(path.dirname(runner),'..'),workspace=path.resolve(repo,'../../..');
const out=path.join(workspace,'Build/Validation/LegacyVirtualOutputRemoval-20261018/Native/TargetPolicy',new Date().toISOString().replace(/[:.]/g,'-'));fs.mkdirSync(out,{recursive:true});
const sha=b=>crypto.createHash('sha256').update(b).digest('hex'),write=(rel,bytes)=>{const p=path.join(out,rel);fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,bytes);};
const receipt={schema:1,status:'running',out,runner:'tools/virtual_target_policy_native_selftest.mjs',realDevice:false,normalAppStarted:false,deployed:false,inputs:[],configurations:[],mutations:[]};const save=()=>write('summary.json',JSON.stringify(receipt,null,2)+'\n');save();
const once=(s,a,b)=>{assert.equal(s.split(a).length,2,'Ambiguous/missing anchor: '+a);return s.replace(a,b);};
function run(exe,args,label,env=process.env,retain=true){const r=spawnSync(exe,args,{cwd:out,env,encoding:'utf8',windowsHide:true,timeout:60000,maxBuffer:8*1024*1024});if(retain)write(label+'.log',(r.stdout||'')+(r.stderr||''));if(r.error)throw r.error;assert.equal(r.signal,null,label);return r;}
try{
 const expanded=snapshotVirtualControllerSource(path.join(repo,'native/main.cpp'),path.join(out,'source/native'));
 const mainBytes=expanded.inputs[0].data,main=expanded.source;receipt.sourceExpansion=expanded.manifest;
 for(const row of expanded.inputs.slice(1))receipt.inputs.push({path:path.relative(repo,row.file).replaceAll('\\','/'),sha256:row.sha256});
 const names=['ascii_lower','trim_ascii','ymccDmiNormalize','ymccResolveFamily','inputHostSteamDeckSelfOutputBlocked','inputHostMachineTargetRejectionReason'];
 const pieces=names.map(n=>extract(main,n)),enumCode=/^enum class YmccFamily[^\r\n{]*\{[\s\S]*?^};/m.exec(main);assert(enumCode);
 const model=enumCode[0]+'\n'+extract(main,'MachineIdentity','struct').code+'\nstatic MachineIdentity g_machineIdentity;\n'+pieces.map(e=>e.code).join('\n');
 const start=extract(main,'inputHostStart').code,boundary=start.indexOf('    msiControllerVisibilityObserverStart();');assert(boundary>0);const prefix=start.slice(0,boundary)+'    ++boundaryCalls; return true;\n}';
 const refresh=extract(main,'inputCaptureRefreshRealStickSettings').code;
 const declaration=n=>{const m=new RegExp('    const (?:std::string|char\\*|bool) '+n+'[\\s\\S]*?;').exec(refresh);assert(m,n);return m[0];};
 const lambda=n=>{const m=new RegExp('    const auto '+n+' = \\[\\][\\s\\S]*?\\n    };').exec(refresh);assert(m,n);return m[0];};
 const projection='struct Projection{bool enabled,gyro;};\nstatic Projection projectTarget(const json& outputTarget){\n'+lambda('readString')+'\n'+lambda('readBool')+'\n'+['configuredPersona','targetMachineRejection','targetGyroEnabled','targetRequested','personaAdmitted','targetEnabled'].map(declaration).join('\n')+'\nreturn {targetEnabled,targetGyroEnabled};\n}';
 assert(refresh.includes('g_realStickTest.virtualTargetEnabled = targetEnabled;'));assert(refresh.includes('g_realStickTest.targetGyroEnabled = targetGyroEnabled;'));
 const status=extract(main,'virtualOutputRuntimeSnapshot').code,statusBoundary=status.indexOf('    if (g_externalPriorityVirtualSuspended');assert(statusBoundary>0);
 const statusPrefix=status.slice(0,statusBoundary).replace(/\(bool sourceLive, const ymcc::VirtualOutputSourceWindow& sourceWindow\)/,'(bool sourceLive)').replace('virtualOutputRuntimeSnapshot','policyStatusOnly')+' return nullptr;\n}';
 const protectionNames=['inputHostTargetKey','inputHostStartProtectionSnapshot','inputHostStartProtectionLog','inputHostTargetAdmission'];
 const protectionPieces=protectionNames.map(n=>extract(main,n));
 const consumers=[...protectionPieces.map(r=>r.code),prefix,projection,statusPrefix,extract(main,'inputShortcutRuntimeSnapshot').code,extract(main,'nativeInputShortcutToggle').code].join('\n');
 write('source/main.cpp',mainBytes);write('generated/model.inc',model);write('generated/consumers.inc',consumers);write('production-bindings.json',JSON.stringify({mainSha256:sha(mainBytes),sourceExpansion:expanded.manifest,pieces:[...pieces,...protectionPieces].map(({code,...v})=>v),startPrefixVerbatim:true,firstDeviceBoundary:'msiControllerVisibilityObserverStart',scope:'Actual production declarations/functions; effects past device boundary excluded'},null,2)+'\n');
 for(const rel of ['tools/virtual_target_policy_native_selftest.mjs','tools/virtual_controller_runtime_source.mjs','tools/fixtures/legion_motion_consumer/extract_main.mjs','native/virtual_target_policy.h','native/virtual_start_protection.h','native/controller_persona.h','native/controller_persona_settings.h','native/input_shortcut_toggle.h','native/game_input_override.h','deps/json/json.hpp','tools/fixtures/virtual_target_policy/harness.cpp']){const b=fs.readFileSync(path.join(repo,rel));write('source/'+rel,b);receipt.inputs.push({path:rel,sha256:sha(b)});}
 // Keep the shared harness source read-only; bind its generated expectations
 // to the current-only production admission contract in this native runner.
 let harness=fs.readFileSync(path.join(repo,'tools/fixtures/virtual_target_policy/harness.cpp'),'utf8');
 harness=once(harness,'const char* personas[]={"disabled","dualshock4","xbox360","steamdeck","dualsense","elite","dualsense-edge"};','const char* personas[]={"disabled","steamdeck","dualsense-edge","elite","xbox360","dualshock4","dualsense","unknown"};');
 harness=once(harness,'const bool requested=(bits&3)!=0,external=(bits&4)!=0;','const bool requested=(bits&3)!=0,external=(bits&4)!=0,current=ymcc::controllerPersona::currentOutput(persona);');
 harness=once(harness,'requested&&std::string(persona)!="disabled"&&!blocked&&!external','requested&&current&&!blocked&&!external');
 harness=once(harness,'(bits&2)!=0&&!blocked&&!external','(bits&2)!=0&&current&&!blocked&&!external');
 harness=once(harness,'check(admitted==!blocked,','check(admitted==(ymcc::controllerPersona::currentOutput(persona)&&!blocked),');
 harness=once(harness,'boundaryCalls==(blocked?0:1)','boundaryCalls==(!ymcc::controllerPersona::currentOutput(persona)||blocked?0:1)');
 harness=once(harness,'#include "model.inc"','#include <model.inc>');
 harness=once(harness,'#include "consumers.inc"','#include "virtual_start_protection.h"\n#include <cstring>\nstatic ymcc::VirtualStartProtection g_virtualStartProtection;\nstatic std::uint64_t fixtureProtectionTick=100000;\nstatic std::uint64_t inputHostStartProtectionNow(){return fixtureProtectionTick;}\n// Lifecycle diagnostic sink is outside this target-policy fixture; full pipe tests execute its real consumer.\nstatic std::atomic<std::uint64_t> g_inputHostSwitchVerificationPending{0};\nstatic thread_local std::uint64_t g_inputHostSwitchRecoveryStartDeadline=0;\nstatic void inputHostRecordSwitchTimeout(const ymcc::VirtualStartProtection::View&) noexcept {}\nstatic void nativeShortcutFeedback(const std::string&,const std::string&,const std::string&,const std::string&,const json&){}\n#include <consumers.inc>');
 // ON and OFF are independent policy transactions; the real30s guard must
 // release before a second user request. Keep the original turn-off assertion.
 harness=once(harness,'if(!blocked){nativeInputShortcutToggle("input.virtualGamepadToggle",preset,nullptr);','if(!blocked){fixtureProtectionTick+=31000;nativeInputShortcutToggle("input.virtualGamepadToggle",preset,nullptr);');
 // Each matrix row is a new controlled admission episode. Preserve real gate
 // logic; only its monotonic clock advances between otherwise independent rows.
 harness=once(harness,'g_inputShortcutRuntime.clear();g_externalPriorityVirtualSuspended=false;','g_inputShortcutRuntime.clear();g_externalPriorityVirtualSuspended=false;fixtureProtectionTick+=31000;');
 write('generated/harness.cpp',harness);
 const discovery=run(path.join(process.env['ProgramFiles(x86)']||'C:/Program Files (x86)','Microsoft Visual Studio/Installer/vswhere.exe'),['-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],'toolchain-discovery');assert.equal(discovery.status,0);
 const vcvars=path.join(discovery.stdout.trim(),'VC/Auxiliary/Build/vcvars64.bat');assert(fs.existsSync(vcvars)&&!/[%\r\n"&|<>^!]/.test(vcvars));const envFile=path.join(out,'compiler-environment.cmd');write('compiler-environment.cmd',`@echo off\r\ncall "${vcvars}" >nul\r\nif errorlevel 1 exit /b %errorlevel%\r\nset\r\n`);
 const setup=run(process.env.ComSpec||'cmd.exe',['/d','/c',envFile],'environment',process.env,false);assert.equal(setup.status,0);const env={...process.env};for(const line of setup.stdout.split(/\r?\n/)){const at=line.indexOf('=');if(at>0){const k=line.slice(0,at),old=Object.keys(env).find(x=>x.toLowerCase()===k.toLowerCase());env[old||k]=line.slice(at+1);}}for(const key of Object.keys(env))if(['cl','_cl_','link','_link_'].includes(key.toLowerCase()))delete env[key];
 const sdkRoot=env.WindowsSdkDir||'C:/Program Files (x86)/Windows Kits/10';
 const sdkVersions=fs.readdirSync(path.join(sdkRoot,'Include')).filter(v=>fs.existsSync(path.join(sdkRoot,'Include',v,'ucrt/crtdbg.h'))).sort((a,b)=>a.localeCompare(b,undefined,{numeric:true}));
 assert(sdkVersions.length,'Installed Windows SDK/UCRT required');const sdkVersion=sdkVersions.at(-1);
 const envKey=name=>Object.keys(env).find(k=>k.toLowerCase()===name.toLowerCase())||name;
 env[envKey('INCLUDE')]=['ucrt','shared','um','winrt','cppwinrt'].map(p=>path.join(sdkRoot,'Include',sdkVersion,p)).concat(env[envKey('INCLUDE')]||'').join(';');
 env[envKey('LIB')]=['ucrt','um'].map(p=>path.join(sdkRoot,'Lib',sdkVersion,p,'x64')).concat(env[envKey('LIB')]||'').join(';');
 const compiler=path.join(env.VCToolsInstallDir,'bin/Hostx64/x64/cl.exe');receipt.compiler={path:compiler,sha256:sha(fs.readFileSync(compiler))};
 const header=fs.readFileSync(path.join(out,'source/native/virtual_target_policy.h'),'utf8');
 const mutants=[{name:'start-current-gate-omitted',consumer:once(consumers,'if (!ymcc::controllerPersona::currentOutput(requestedPersona)) return false;','if (false) return false;')},{name:'no-machine-restriction',header:once(header,'identifiedPhysicalSteamDeck && requestedPersona == "steamdeck"','identifiedPhysicalSteamDeck && requestedPersona == "unreachable"')},{name:'start-gate-omitted',consumer:once(consumers,prefix,once(prefix,'if (const char* reason = inputHostMachineTargetRejectionReason(requestedPersona)) {','if (const char* reason = static_cast<const char*>(nullptr)) {'))},{name:'refresh-gates-omitted',consumer:once(once(consumers,'!targetMachineRejection && readBool','readBool'),'personaAdmitted && !targetMachineRejection &&','personaAdmitted &&')},{name:'shortcut-gate-omitted',consumer:once(consumers,'if (result.ok && result.enabled) {','if (result.ok && result.enabled && false) {')}];
 for(const [config,flags] of [['Release',['/O2','/DNDEBUG','/MT']],['Checked',['/Od','/RTC1','/MTd']]])for(const variant of [{name:'baseline'},...mutants]){
  const rel=`${config}/${variant.name}`,dir=path.join(out,rel);fs.mkdirSync(dir,{recursive:true});fs.writeFileSync(path.join(dir,'model.inc'),model);fs.writeFileSync(path.join(dir,'consumers.inc'),variant.consumer||consumers);fs.writeFileSync(path.join(dir,'virtual_target_policy.h'),variant.header||header);
  const exe=path.join(dir,'policy.exe'),compiled=run(compiler,['/nologo','/std:c++17','/EHsc','/utf-8',...flags,`/I${dir}`,`/I${path.join(out,'source/native')}`,`/I${path.join(out,'source/deps/json')}`,path.join(out,'generated/harness.cpp'),`/Fo${path.join(dir,'harness.obj')}`,`/Fe${exe}`],rel+'/compile',env);assert.equal(compiled.status,0,rel+' compile');
  const tested=run(exe,[],rel+'/run',env),m=/SUMMARY checks=(\d+) failures=(\d+)/.exec(tested.stdout);assert(m,rel+' semantic summary');
  if(variant.name==='baseline'){assert.equal(tested.status,0);assert.equal(+m[2],0);receipt.configurations.push({name:config,checks:+m[1],failures:+m[2]});}else{assert.notEqual(tested.status,0,rel+' must be killed');assert(+m[2]>0);receipt.mutations.push({config,name:variant.name,compiled:true,killed:true,failures:+m[2]});}save();
 }
 // This is a shared worktree: bind every tested production function/header,
 // not unrelated monitor code another task may update during compilation.
 const finalExpanded=readVirtualControllerSource(path.join(repo,'native/main.cpp')),finalBytes=finalExpanded.inputs[0].data,finalMain=finalExpanded.source;
 for(const row of expanded.manifest.inputs)assert.equal(sha(fs.readFileSync(row.snapshot)),row.sha256,'Immutable runtime source snapshot:'+row.file);
 assert.equal(sha(fs.readFileSync(expanded.manifest.expandedSnapshot)),expanded.manifest.expandedSha256,'Immutable expanded source');
 const boundNames=[...names,...protectionNames,'inputHostStart','inputCaptureRefreshRealStickSettings','nativeInputShortcutToggle','inputShortcutRuntimeSnapshot','virtualOutputRuntimeSnapshot'];
 receipt.sourceStability={policyFunctions:boundNames.map(name=>({name,identical:extract(main,name).code===extract(finalMain,name).code})),machineIdentityStructIdentical:extract(main,'MachineIdentity','struct').code===extract(finalMain,'MachineIdentity','struct').code,machineFamilyEnumIdentical:enumCode[0]===/^enum class YmccFamily[^\r\n{]*\{[\s\S]*?^};/m.exec(finalMain)?.[0],wholeMainByteIdentical:sha(finalBytes)===sha(mainBytes)};
 assert(receipt.sourceStability.policyFunctions.every(row=>row.identical)&&receipt.sourceStability.machineIdentityStructIdentical&&receipt.sourceStability.machineFamilyEnumIdentical,'Tested policy source changed during run');
 for(const pin of receipt.inputs)assert.equal(sha(fs.readFileSync(path.join(repo,pin.path))),pin.sha256,pin.path);
 receipt.mainSha256=sha(mainBytes);receipt.endingMainSha256=sha(finalBytes);receipt.status='pass';save();console.log(JSON.stringify({status:receipt.status,out,configurations:receipt.configurations,mutants:mutants.length}));
}catch(e){receipt.status='failed';receipt.error=String(e.stack||e);save();console.error(receipt.error);process.exitCode=1;}
