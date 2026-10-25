import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
const runner=fileURLToPath(import.meta.url);
const repo=path.resolve(path.dirname(runner),'..');
const workspace=path.resolve(repo,'../../..');
const root=path.join(workspace,'Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/AutomaticCalibration/SessionMode');
const options={};
for(let i=2;i<process.argv.length;++i){assert(['--out','--vcvars'].includes(process.argv[i])&&process.argv[i+1],`Invalid option ${process.argv[i]}`);options[process.argv[i].slice(2)]=process.argv[++i];}
const out=path.resolve(options.out||path.join(root,`${new Date().toISOString().replace(/[:.]/g,'-')}-${crypto.randomBytes(3).toString('hex')}`));
assert.equal(path.dirname(out).toLowerCase(),root.toLowerCase(),'Evidence requires a fresh direct SessionMode child');
fs.mkdirSync(root,{recursive:true});assert.equal(fs.realpathSync(root).toLowerCase(),path.resolve(root).toLowerCase(),'Redirected evidence root');fs.mkdirSync(out);
const sha=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const relative=file=>path.relative(out,file).replaceAll('\\','/');
const json=(file,data)=>fs.writeFileSync(path.join(out,file),JSON.stringify(data,null,2)+'\n');
const receipt={schema:1,status:'running',startedAt:new Date().toISOString(),scope:'Pure C++17 actual Session/Owner calibration mode API and callback traces',
 inputs:[],configurations:[],mutations:[],hardwareIo:false,dllLoaded:false,devicesChanged:false,actualSleepWake:false,productionEdited:false,
 integrationExecuted:false,exceptionsOrReentrancyTested:false,fullEnvironmentDumped:false,
 callbackContract:'Caller serialized, nonthrowing non-reentrant C ABI callbacks; no additional exception guarantee',
 oracle:'Manual callback order/count/value assertions, explicit dt and first quaternion pins; control Session trajectory is supplementary, no copied Session/Owner implementation',
 boundaries:['No real GamepadMotion.dll or backend behavior, physical stillness/sensor fusion validation, hardware, device or sleep/wake',
 'No main/consumer/automatic-calibration integration or configuration persistence',
 'No callback throws, reentry, concurrent calls or DLL lifetime/unload claims']};
const save=()=>json('summary.json',receipt);
function run(exe,args,log,cwd=out){const result=spawnSync(exe,args,{cwd,windowsHide:true,encoding:'utf8',timeout:45000,maxBuffer:12*1024*1024});
 fs.mkdirSync(path.dirname(path.join(out,log)),{recursive:true});fs.writeFileSync(path.join(out,log),(result.stdout||'')+(result.stderr||''));
 if(result.error)throw result.error;assert.equal(result.signal,null,log);return result;}
const ok=(result,label)=>{assert.equal(result.status,0,`${label} failed; evidence ${out}`);return result.stdout;};
const quote=file=>{assert(!/[\r\n"%&|<>^!]/.test(file),'Unsupported batch path');return `"${file}"`;};
function methodBody(source,name,transform){
 const signature=new RegExp('(?:bool|void|std::optional<int>)\\s+'+name+'\\s*\\(');
 const found=signature.exec(source);assert(found,`Missing actual API ${name}`);
 assert(!signature.test(source.slice(found.index+found[0].length)),`Ambiguous API ${name}`);
 const opening=source.indexOf('{',found.index);let depth=0;
 for(let i=opening;i<source.length;++i){if(source[i]==='{')++depth;if(source[i]==='}'&&--depth===0){
   const before=source.slice(opening+1,i),after=transform(before);assert.notEqual(after,before,`No semantic mutation ${name}`);
   return source.slice(0,opening+1)+after+source.slice(i);}}
 throw Error('Unbalanced method '+name);
}
const controls=[
 {name:'force-manual-intent',target:'controller_motion_session.h',expected:'mode.initial-bool-mapping',change:s=>methodBody(s,'reconcileCalibrationMode',body=>'\n        automatic = false;'+body)},
 {name:'force-automatic-intent',target:'controller_motion_session.h',expected:'mode.initial-bool-mapping',change:s=>methodBody(s,'reconcileCalibrationMode',body=>'\n        automatic = true;'+body)},
 {name:'invalidation-does-not-clear-cache',target:'controller_motion_session.h',expected:'invalidate.cache-unknown',change:s=>methodBody(s,'invalidateCalibrationMode',()=> ' ')},
 {name:'reconcile-forgets-cache-every-frame',target:'controller_motion_session.h',expected:'mode.repeated-no-write',change:s=>methodBody(s,'reconcileCalibrationMode',body=>'\n        invalidateCalibrationMode();'+body)},
 {name:'mode-toggle-resets-quaternion',target:'controller_motion_session.h',expected:'state.mode-preserves-quaternion-at-zero-dt',change:s=>methodBody(s,'reconcileCalibrationMode',body=>'\n        orientation_ = {};'+body)},
 {name:'mode-toggle-resets-time-origin',target:'controller_motion_session.h',expected:'state.mode-preserves-previous-seconds',change:s=>methodBody(s,'reconcileCalibrationMode',body=>'\n        previousSeconds_ = 0;'+body)},
 {name:'invalidation-calls-native',target:'controller_motion_session.h',expected:'invalidate.no-native-callback',change:s=>methodBody(s,'invalidateCalibrationMode',body=>body+'\n        if (currentKey_) { if (void* active = handleFor(*currentKey_)) ops_.setMode(active, 0); }\n    ')},
 {name:'owner-initializes-automatic',target:'controller_motion_processor.h',expected:'select.manual-before-offset',change:s=>{
   const anchor='ops_.setMode(handle_, 0);';assert.equal(s.split(anchor).length,2,'Unique actual Owner Manual initialization required');return s.replace(anchor,'ops_.setMode(handle_, 3);');}},
];
save();
try{
 assert.equal(process.platform,'win32');assert.equal(process.arch,'x64');
 const fixture=path.join(repo,'tools/fixtures/controller_motion_mode');
 const files=[runner,...fs.readdirSync(fixture).sort().map(name=>path.join(fixture,name)),...['controller_motion_session.h','controller_motion_processor.h'].map(name=>path.join(repo,'native',name))];
 const original={};
 for(const file of files){assert(fs.statSync(file).isFile());const bytes=fs.readFileSync(file);const frozen=path.join(out,'source',path.relative(repo,file));
  fs.mkdirSync(path.dirname(frozen),{recursive:true});fs.writeFileSync(frozen,bytes);receipt.inputs.push({path:file,frozen:relative(frozen),bytes:bytes.length,sha256:sha(bytes)});
  if(path.dirname(file)===path.join(repo,'native'))original[path.basename(file)]=bytes.toString('utf8');}
 for(const name of ['reconcileCalibrationMode','invalidateCalibrationMode','calibrationModeFor'])assert(original['controller_motion_session.h'].includes(name),`Production API not ready: ${name}; fixture is preserved, no guessed implementation used`);
 let vcvars=options.vcvars;
 if(!vcvars){const vswhere=path.join(process.env['ProgramFiles(x86)']||'C:/Program Files (x86)','Microsoft Visual Studio/Installer/vswhere.exe');
  const install=ok(run(vswhere,['-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],'compiler-discovery.log'),'MSVC discovery').trim();
  assert(install&&!/[\r\n]/.test(install));vcvars=path.join(install,'VC/Auxiliary/Build/vcvars64.bat');}
 assert(fs.existsSync(vcvars));const setup=`call ${quote(vcvars)} >nul\r\nif errorlevel 1 exit /b %errorlevel%\r\n`;
 const metadataCmd=path.join(out,'compiler-metadata.cmd');
 fs.writeFileSync(metadataCmd,'@echo off\r\n'+setup+'where cl.exe\r\necho VCToolsVersion=%VCToolsVersion%\r\necho WindowsSDKVersion=%WindowsSDKVersion%\r\n');
 const metadata=ok(run(process.env.ComSpec||'cmd.exe',['/d','/c',metadataCmd],'compiler-metadata.log'),'MSVC metadata');
 const compiler=metadata.split(/\r?\n/).find(line=>/\\cl\.exe$/i.test(line.trim()))?.trim();assert(compiler&&fs.existsSync(compiler));
 receipt.compiler={path:compiler,sha256:sha(fs.readFileSync(compiler)),vcToolsVersion:/VCToolsVersion=([^\r\n]+)/.exec(metadata)?.[1],windowsSdkVersion:/WindowsSDKVersion=([^\r\n]+)/.exec(metadata)?.[1],fullEnvironmentCaptured:false};
 const harness=path.join(out,'source/tools/fixtures/controller_motion_mode/harness.cpp');
 const configurations=[['Release',['/O2','/DNDEBUG','/MT']],['Checked',['/Od','/RTC1','/Zi','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MTd']]];
 function buildAndRun(directory,flags,include){
  fs.mkdirSync(directory,{recursive:true});const exe=path.join(directory,'controller_motion_mode.exe');
  const args=['/nologo','/std:c++17','/EHsc','/utf-8','/W4','/WX','/permissive-','/fp:precise','/showIncludes',...flags,
   `/I${include}`,harness,`/Fo${path.join(directory,'harness.obj')}`,`/Fd${path.join(directory,'compiler.pdb')}`,`/Fe${exe}`,'/link','/INCREMENTAL:NO'];
  const response=path.join(directory,'compile.rsp');fs.writeFileSync(response,args.map(arg=>`"${arg}"`).join('\r\n')+'\r\n');
  const batch=path.join(directory,'compile.cmd');fs.writeFileSync(batch,'@echo off\r\n'+setup+'set CL=\r\nset _CL_=\r\nset LINK=\r\nset _LINK_=\r\n'+`${quote(compiler)} @${quote(response)}\r\nexit /b %errorlevel%\r\n`);
  const log=ok(run(process.env.ComSpec||'cmd.exe',['/d','/c',batch],`${relative(directory)}/compile.log`,directory),'C++17 compilation');
  for(const name of Object.keys(original))assert(log.toLowerCase().includes(path.join(include,name).toLowerCase()),`Compiler did not include selected actual/mutant ${name}`);
  const result=run(exe,[],`${relative(directory)}/run.log`,directory);
  const match=/^SUMMARY cases=(\d+) checks=(\d+) failures=(\d+) events=(\d+) failureIds=(\d+)\r?$/m.exec(result.stdout);
  assert(match,'No normal semantic summary; a build error or crash is never a mutant kill');
  const failureIds=[...result.stdout.matchAll(/^FAIL\|([^|\r\n]+)\|/gm)].map(m=>m[1]);
  const callbackEvents=[...result.stdout.matchAll(/^EVENT (.+)\r?$/gm)].map(m=>JSON.parse(m[1]));
  assert.equal(callbackEvents.length,+match[4]);assert.equal(failureIds.length,+match[5]);
  assert([0,1].includes(result.status));assert.equal(result.status,+match[3]?1:0);
  assert.equal(+match[1],49,'All scenarios must complete');
  json(`${relative(directory)}/callbacks.json`,{callbackEvents,failureIds});
  return {exitCode:result.status,cases:+match[1],checks:+match[2],failures:+match[3],events:+match[4],failureIds,buildArguments:args,executableSha256:sha(fs.readFileSync(exe))};
 }
 for(const [name,flags]of configurations){
  for(const input of receipt.inputs.filter(x=>x.path.includes(`${path.sep}native${path.sep}`)))assert.equal(sha(fs.readFileSync(input.path)),input.sha256,'Actual header changed before baseline');
  const result=buildAndRun(path.join(out,name),flags,path.join(repo,'native'));receipt.configurations.push({name,...result});save();
  assert.equal(result.exitCode,0,`${name}: actual Session/Owner failed a manual assertion`);assert.equal(result.failures,0);assert(result.checks>2000);
  console.log(`${name}: ${result.cases} cases, ${result.checks} checks, ${result.events} callback events, 0 failures`);
 }
 for(const control of controls){
  const directory=path.join(out,'mutations',control.name);fs.mkdirSync(directory,{recursive:true});
  for(const [name,text]of Object.entries(original))fs.writeFileSync(path.join(directory,name),name===control.target?control.change(text):text);
  const mutation={name:control.name,target:control.target,expectedAssertion:control.expected,mutatedHeaderSha256:sha(fs.readFileSync(path.join(directory,control.target))),configurations:[]};
  receipt.mutations.push(mutation);save();
  for(const [name,flags]of configurations){const result=buildAndRun(path.join(directory,name),flags,directory);
   assert.equal(result.exitCode,1,`${control.name}: mutant survived or crashed`);assert(result.failures>0);
   assert(result.failureIds.includes(control.expected),`${control.name}: planned assertion ${control.expected} was not hit`);
   mutation.configurations.push({name,compiled:true,status:'semantically-killed',...result});save();}
  mutation.status='compiled-and-semantically-killed';save();console.log(`Mutant ${control.name}: compiled and hit ${control.expected} in Release + Checked`);
 }
 for(const input of receipt.inputs)assert.equal(sha(fs.readFileSync(input.path)),input.sha256,`Input changed during validation: ${input.path}`);
 receipt.sourceHashesRevalidated=true;receipt.status='passed';receipt.completedAt=new Date().toISOString();
 receipt.mutationCoverage={mutants:controls.length,configurations:2,compiledAndSemanticallyKilled:controls.length*2,compileErrorsCountAsKills:false,crashesCountAsKills:false,expectedAssertionRequired:true};save();
 fs.writeFileSync(path.join(out,'REPORT.md'),'# Controller Session calibration mode pure-header verification\n\n'+
  `Status: ${receipt.status}. Actual production Session/Owner headers were included directly in both baseline builds, frozen and rehashed after validation. No production file was edited.\n\n`+
  receipt.configurations.map(c=>`- ${c.name}: ${c.cases} cases, ${c.checks} checks, ${c.events} callback events, ${c.failures} failures.`).join('\n')+'\n'+
  `- ${controls.length} semantic mutants compiled and hit predeclared assertion IDs in both configurations (${controls.length*2} runs). Compile errors/crashes are not kills.\n\n`+
  'Coverage: Manual0 initialization and false/true reconciliation; repeated frames without repeated writes; both toggle directions; invalidation without a native call; unknown/current/wrong-key/retired queries; failed selection/create and no token revival; each Ops/ReadOps callback replaced independently plus all callbacks replaced together; optional ReadOps removed; both changed and reused native pointer addresses; new keys reusing an address; unchanged select preserving known/unknown cache; offset/native axes/dt/quaternion continuity.\n\n'+
  'Callback traces are in each callbacks.json and run.log, with callback bank, slot and owner generation. Direct order/count/value assertions and explicit dt/first-quaternion pins are primary; a second untouched actual Session provides supplementary trajectory comparison. The sample/session implementation is not copied as an oracle.\n\n'+
  'The callbacks follow the existing serialized nonthrowing/non-reentrant C ABI contract. No exception, reentry or concurrency guarantee is added or tested. No DLL/hardware/device/sleep/main integration or physical calibration/sensor-fusion behavior is proved. Compiler setup emits only compiler location/version/SDK metadata, never a full environment SET dump.\n');
 console.log(JSON.stringify({status:receipt.status,evidence:out,configurations:receipt.configurations.map(({name,cases,checks,events,failures})=>({name,cases,checks,events,failures})),mutationCoverage:receipt.mutationCoverage},null,2));
}catch(error){receipt.status='failed';receipt.error=error.stack||String(error);save();console.error(receipt.error);console.error(`Evidence: ${out}`);process.exitCode=1;}