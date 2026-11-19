import {wireDefaultWindowsMotion,wireWindowsCalibrationContext,copyWindowsCalibrationContextHeaders} from './fixtures/hc_reviewed_calibration/wire_default_windows_motion.mjs';
import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';
import fs from 'node:fs';import path from 'node:path';import {fileURLToPath} from 'node:url';import {createHash} from 'node:crypto';import {spawnSync} from 'node:child_process';import {extract} from './fixtures/legion_motion_consumer/extract_main.mjs';
const options={};let legacyKeyControl=false,preparedConsumerOnly=false;
for(let i=2;i<process.argv.length;++i){const arg=process.argv[i];
 if(arg==='--expect-legacy-key-failure'){if(legacyKeyControl)throw Error('Duplicate option '+arg);legacyKeyControl=true;continue;}
 if(arg==='--prepared-consumer-only'){if(preparedConsumerOnly)throw Error('Duplicate option '+arg);preparedConsumerOnly=true;continue;}
 if(!['--consumer','--source','--out'].includes(arg)||Object.hasOwn(options,arg)||!process.argv[i+1]||process.argv[i+1].startsWith('--'))throw Error('Unknown, duplicate or incomplete option '+arg);
 options[arg]=process.argv[++i];
}
if(preparedConsumerOnly&&['--consumer','--source','--out'].some(arg=>!Object.hasOwn(options,arg)))throw Error('--prepared-consumer-only requires explicit --consumer, --source and --out');
const runner=fileURLToPath(import.meta.url),repo=path.resolve(path.dirname(runner),'..'),workspace=path.resolve(repo,'../../..'),root=path.join(workspace,'Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/ReviewedRuntime');
const consumer=path.resolve(options['--consumer']??path.join(root,'consumer-R3')),nativePath=path.resolve(options['--source']??path.join(repo,'native/main.cpp'));
const out=path.resolve(options['--out']??path.join(root,'Calibration-'+new Date().toISOString().replace(/[:.]/g,'-')+'-'+process.pid));
if(fs.existsSync(out))throw Error('Calibration output must be a fresh directory: '+out);
fs.mkdirSync(path.dirname(out),{recursive:true});fs.mkdirSync(out);
const writeOnce=(file,data)=>fs.writeFileSync(file,data,{flag:'wx'});
const sha=b=>createHash('sha256').update(b).digest('hex'),read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));const inputs=[];const freeze=p=>{const b=fs.readFileSync(p);inputs.push({path:p,sha256:sha(b)});return b;};
const writeMatching=(file,data)=>{if(fs.existsSync(file)){if(sha(fs.readFileSync(file))!==sha(data))throw Error('Copied dependency differs '+file);return;}writeOnce(file,data);};
const result={schema:1,status:'running',legacyKeyControl,preparedConsumerOnly,consumer,source:nativePath,out,successScope:'actual-calibration-assertions-only',consumerFullProducer:{status:preparedConsumerOnly?'NOT_RUN':'PRIOR_PASS_REQUIRED',recordedStatus:null,executedByThisRunner:false,acceptedByThisRunner:false},configurations:[],newProductDlls:[],realCalibrationFileTouched:false,hardwareIo:false,deployed:false,normalAppStarted:false};let progressSequence=0;
const save=()=>writeOnce(path.join(out,result.status==='running'?'PROGRESS-'+String(++progressSequence).padStart(3,'0')+'.json':'RESULT.json'),JSON.stringify(result,null,2)+'\n');save();
function run(exe,args,log,env=process.env,expectedExit=0){const r=spawnSync(exe,args,{cwd:out,env,windowsHide:true,encoding:'utf8',timeout:60000,maxBuffer:16*1024*1024});writeOnce(path.join(out,log),(r.stdout||'')+(r.stderr||'')+(r.error?.stack||''));if(r.status!==expectedExit)throw Error(log+': '+r.status);return r;}
function consumerGeneratedClosure(meta,consumerBytes){
 const rows=meta.generatedInputsAtCreation,files=new Map(),folded=new Set();
 if(!Array.isArray(rows)||!rows.length)throw Error('Actual consumer generated dependency manifest missing');
 for(const row of rows){
  if(!row||typeof row.path!=='string'||!path.isAbsolute(row.path)||!/^([a-f0-9]{64})$/i.test(row.sha256??'')||row.role!=='owned-generated-at-creation')throw Error('Invalid consumer generated dependency identity');
  const absolute=path.resolve(row.path),relative=path.relative(consumer,absolute),name=relative.replaceAll('\\','/');
  if(!relative||path.isAbsolute(relative)||relative==='..'||relative.startsWith('..'+path.sep)||/[\0\r\n:]/.test(name)||folded.has(name.toLowerCase()))throw Error('Consumer generated dependency escapes or duplicates its full name');
  if(fs.realpathSync(absolute).toLowerCase()!==absolute.toLowerCase()||!fs.statSync(absolute).isFile())throw Error('Consumer generated dependency is not an original regular file');
  const bytes=freeze(absolute);if(sha(bytes)!==row.sha256)throw Error('Consumer generated dependency hash mismatch '+name);
  folded.add(name.toLowerCase());files.set(name,{path:absolute,sha256:row.sha256,bytes});
 }
 if(!files.has('motion_consumer.cpp')||files.get('motion_consumer.cpp').sha256!==sha(consumerBytes))throw Error('Consumer generated root does not match its manifest');
 const visited=new Set(),edges=[];
 const visit=name=>{
  if(visited.has(name))return;visited.add(name);
  for(const match of files.get(name).bytes.toString('utf8').matchAll(/^\s*#\s*include\s*"([^"\r\n]+)"/gm)){
   const include=match[1];if(path.isAbsolute(include)||include.includes('\\')||include.split('/').includes('..'))throw Error('Unsupported consumer include path '+include);
   const resolved=path.posix.join(path.posix.dirname(name),include);edges.push({from:name,include,to:resolved});
   if(files.has(resolved))visit(resolved);
   else if(resolved!=='json.hpp')throw Error('Consumer include missing from generated manifest '+resolved);
  }
 };
 visit('motion_consumer.cpp');
 if(visited.size!==files.size)throw Error('Consumer generated manifest contains files outside the actual include closure');
 return {files,edges};
}
try{
 const meta=JSON.parse(freeze(path.join(consumer,'source-bindings.json')).toString('utf8').replace(/^\uFEFF/,''));result.consumerFullProducer.recordedStatus=meta.status;
 if(preparedConsumerOnly?meta.status!=='generated-only':meta.status!=='pass')throw Error(preparedConsumerOnly?'Prepared consumer receipt must have generated-only status; failed/running receipts are not accepted':'Actual consumer fixture must first pass');
 if(!preparedConsumerOnly)result.consumerFullProducer.status='PRIOR_PASS_REFERENCE_ONLY';
 const nativeBytes=freeze(nativePath),native=nativeBytes.toString('utf8');result.sourceSha256=sha(nativeBytes);
 if(preparedConsumerOnly&&meta.source?.sha256!==result.sourceSha256)throw Error('Prepared consumer source identity mismatch');

 const consumerBytes=freeze(path.join(consumer,'motion_consumer.cpp'));if(sha(consumerBytes)!==meta.generatedCppSha256)throw Error('Actual consumer generated source hash mismatch');
 const generatedClosure=consumerGeneratedClosure(meta,consumerBytes);
 result.generatedConsumerClosure={files:[...generatedClosure.files].map(([name,row])=>({name,path:row.path,sha256:row.sha256})),includeEdges:generatedClosure.edges,scope:'exact-created-files-and-actual-quoted-includes;not-full-consumer-execution'};
 for(const [name,row]of generatedClosure.files){const destination=path.join(out,name);fs.mkdirSync(path.dirname(destination),{recursive:true});writeOnce(destination,row.bytes);}
 let generated=consumerBytes.toString('utf8');const pinnedFixtureSource=generated;const wired=wireDefaultWindowsMotion(generated,native);generated=wired.code;const contextWired=wireWindowsCalibrationContext(generated,native);generated=contextWired.code;result.processingContextBindings=contextWired.bindings;result.defaultWiringBindings=wired.bindings;const at=generated.lastIndexOf('int main(');if(at<0)throw Error('Original fixture Main not found');
 for(const name of ['inputCaptureResolveNewHcMotionSelector','inputCaptureNewHcMotionSelectorBound','inputCaptureResolveLegacyHcMotionSelector','inputCaptureApplyHcMatrixCandidate']){const f=extract(native,name),b=meta.bindings.find(b=>b.snapshot==='current'&&b.name===name);const archived=extract(pinnedFixtureSource.slice(pinnedFixtureSource.indexOf('namespace candidate {')),name),rewired=extract(generated.slice(generated.indexOf('namespace candidate {'),at),name);if(!b||archived.sha256!==b.sha256||rewired.code.replace(/\r\n/g,'\n')!==f.code.replace(/\r\n/g,'\n'))throw Error('Consumer content drift '+name);(result.consumerBindings??=[]).push({symbol:name,archivedSha256:archived.sha256,currentSha256:f.sha256,rewiredSha256:rewired.sha256,historicalFragmentVerified:true,byteMatch:rewired.sha256===f.sha256,lfContentMatch:true});}
 const names=['ymSettingsArchiveFile','inputCaptureCalibrationJsonPath','inputCaptureCalibrationKeyUpper','inputCaptureHcWindowsLegacyNeeded','inputCaptureReviewedCalibrationRecipe','inputCaptureExtendedCalibrationRecipe','inputCaptureSourceQualifiedCalibrationRecipe','inputCaptureSourceQualifiedCalibrationBound','inputCaptureExtendedCalibrationSourceIdentity','inputCaptureCalibrationStorageKey','inputCaptureLoadCalibration','inputCaptureWriteReviewedCalibration','inputCaptureStoreCalibration','inputCaptureReviewedCalibrationNoteApplied','inputCaptureExtendedCalibrationInvalidateOnce','inputCaptureReviewedCalibrationRestoreOnce','inputCaptureGyroCalibrationRestoreOnce'];
 for(const name of ['inputCaptureReadCalibrationDocument','inputCaptureWriteCalibrationRecord'])if(native.includes(name+'('))names.splice(names.indexOf('inputCaptureLoadCalibration'),0,name);
 if(wired.bindings.length)writeMatching(path.join(out,'hc_default_motion_catalog.h'),freeze(path.join(repo,'native/hc_default_motion_catalog.h')));
 if(native.includes('static bool inputCaptureWindowsCalibrationFrameCurrent('))names.unshift('inputCaptureWindowsCalibrationFrameCurrent');
 if(native.includes('static std::string inputCapturePrivateCalibrationStorageKey('))names.splice(names.indexOf('inputCaptureExtendedCalibrationSourceIdentity'),0,'inputCapturePrivateCalibrationStorageKey');
 if(native.includes('static std::string inputCapturePrivateCalibrationStorageKey(')){
  if(!names.includes('inputCaptureSensorProviderKey'))names.unshift('inputCaptureSensorProviderKey');
  names.splice(names.indexOf('inputCaptureSensorProviderKey')+1,0,'inputCaptureWindowsSelectionAxes','inputCaptureWindowsSelectionEvidence');
 }
 if(native.includes('static std::string inputCapturePrivateCalibrationRequestedKey('))names.splice(names.indexOf('inputCapturePrivateCalibrationStorageKey'),0,'inputCapturePrivateCalibrationRequestedKey');
 if(native.includes('static const ymcc::hc_model_motion::Recipe* inputCapturePrivateOemCalibrationRecipe('))names.splice(names.indexOf('inputCaptureSourceQualifiedCalibrationRecipe'),0,'inputCapturePrivateOemCalibrationRecipe');
 copyWindowsCalibrationContextHeaders(repo,out,freeze,writeMatching);
 const bindings=names.map(n=>extract(native,n));result.bindings=bindings.map(({code,...b})=>b);const trusted=native.match(/^static constexpr float kCalibTrustedConfidence = [^;]+;/m)?.[0];if(!trusted)throw Error('Missing actual trust constant');
 const fixturePath=path.join(repo,'tools/fixtures/hc_reviewed_calibration/harness.cpp'),fixture=freeze(fixturePath).toString('utf8');freeze(path.join(repo,'tools/fixtures/hc_reviewed_calibration/README.md'));freeze(runner);freeze(path.join(repo,'tools/fixtures/hc_reviewed_calibration/toolchain.mjs'));freeze(path.join(repo,'tools/fixtures/hc_reviewed_calibration/wire_default_windows_motion.mjs'));
 let cpp='#include "settings_baseline.h"\n#include "windows_source_view.h"\n#include "windows_source_eligibility.h"\n#define NOMINMAX\n#include <functional>\n#include <limits>\n#include "reviewed_gyro_calibration.h"\n#include <windows.h>\n#undef HKEY_LOCAL_MACHINE\n#undef near\n#include <chrono>\n#include <filesystem>\n#include <cstdlib>\n'+generated.slice(0,at)+fixture.replace('// @@CALIBRATION_CODE@@',trusted+'\nstatic ymcc::reviewed_calibration::Context g_reviewedCalibrationContext;\n'+bindings.map(b=>b.code).join('\n\n'));
 if(legacyKeyControl){const original=bindings.find(b=>b.symbol==='inputCaptureCalibrationStorageKey').code;if(cpp.split(original).length!==2)throw Error('Control key scope');cpp=cpp.replace(original,'static std::string inputCaptureCalibrationStorageKey(const std::string& deviceId) {return inputCaptureCalibrationKeyUpper(deviceId);}');}
 writeOnce(path.join(out,'calibration.cpp'),cpp);
 for(const name of ['windows_motion_submission.h','reviewed_gyro_calibration.h','settings_baseline.h'])writeMatching(path.join(out,name),freeze(path.join(repo,'native',name)));
 freeze(path.join(repo,'deps/json/json.hpp'));
 result.storageClosure={symbols:bindings.filter(b=>['ymSettingsArchiveFile','inputCaptureReadCalibrationDocument','inputCaptureWriteCalibrationRecord'].includes(b.symbol)).map(({symbol,sha256})=>({symbol,sha256})),header:'settings_baseline.h',scope:'actual-calibration-load-store-restore;damaged-file-and-atomic-fault-scenarios-not-executed'};
 const dll=path.join(repo,'deps/hc-runtime-upstream/HC-CANDIDATE-0.32.4.0-06c0b954-20260902/GamepadMotion.dll'),dllBytes=freeze(dll);if(sha(dllBytes)!=='e11d59f84b4a4a12e99b15ea998e79de06c6dfb8f1e9c637e973abd4d3d088a9')throw Error('Pinned DLL mismatch');result.dll={path:dll,sha256:sha(dllBytes)};
 const vcvars='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';for(const p of [out,repo,vcvars])if(/["%\r\n]/.test(p))throw Error('Unsupported tool path');
 const oracle=path.join(consumer,'expectations.json');if(sha(freeze(oracle))!==meta.oracleSha256)throw Error('Actual consumer oracle hash mismatch');
 for(const [name,flags]of [['Release','/O2 /DNDEBUG /MT'],['Checked','/Od /RTC1 /MTd']]){const dir=path.join(out,name);fs.mkdirSync(dir);const script=path.join(dir,'build.cmd');writeOnce(script,`${calibrationToolchainPreamble(vcvars)}cl /nologo /std:c++20 /utf-8 /EHsc ${flags} "${path.join(out,'calibration.cpp')}" /I"${path.join(repo,'deps/json')}" /Fo"${path.join(dir,'calibration.obj')}" /Fe"${path.join(dir,'calibration.exe')}"\r\nexit /b %errorlevel%\r\n`);run('cmd.exe',['/d','/c',script],name+'-build.log');const env={...process.env,LOCALAPPDATA:path.join(dir,'isolated-appdata')};const reportPath=path.join(dir,'REPORT.json');if(fs.existsSync(reportPath))throw Error('Calibration report already exists');run(path.join(dir,'calibration.exe'),[oracle,dll,reportPath],name+'-run.log',env,legacyKeyControl?1:0);const report=read(reportPath);const allowed=new Set(['recipe-calibration/key','recipe-calibration/save','recipe-calibration/scope','recipe-calibration/unbound']);if(legacyKeyControl?!(report.status==='fail'&&report.failures.length>0&&report.failures.every(f=>allowed.has(f.group))):report.status!=='pass')throw Error('Calibration failure '+name);result.configurations.push({name,report,executableSha256:sha(fs.readFileSync(path.join(dir,'calibration.exe')))});console.log(name+': '+(legacyKeyControl?'EXPECTED KEY FAILURE':'PASS')+' '+report.checks+' calibration checks');save();}
 result.inputs=inputs;result.sourcesUnchanged=inputs.every(x=>sha(fs.readFileSync(x.path))===x.sha256);if(!result.sourcesUnchanged)throw Error('Input drift');result.status=legacyKeyControl?'expected-failure':'pass';result.completedAt=new Date().toISOString();save();console.log('Evidence: '+out);
}catch(e){result.status='fail';result.error=e.stack||String(e);result.inputs=inputs;save();throw e;}
