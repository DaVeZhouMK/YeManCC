import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath,pathToFileURL} from 'node:url';

// Selected actual native gates/frame/envelope/dispatch blocks, inert transport.
const started=performance.now();
let repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
let out=path.resolve(repo,'../../../Build/Validation/VirtualInput-Submission-R113-20261016/submission-final');
let mainFile=null,headerRoot=null,prepareOnly=false,commonHidOnly=false,commonHidRuntimeOnly=false,commonHidRuntimeCase=null;const seenArguments=new Set();
for(let i=2;i<process.argv.length;i++){
 const arg=process.argv[i];assert(!seenArguments.has(arg),'DUPLICATE_ARGUMENT '+arg);seenArguments.add(arg);
 if(arg==='--prepare-only'){prepareOnly=true;continue;}
 if(arg==='--common-hid-only'){commonHidOnly=true;continue;}
 if(arg==='--common-hid-runtime-only'){commonHidRuntimeOnly=true;continue;}
 assert(['--out','--repo','--MainFile','--HeaderRoot','--common-hid-runtime-case'].includes(arg),'UNKNOWN_FRAME_ARGUMENT '+arg);
 const value=process.argv[++i];assert(value&&!value.startsWith('--'),'EMPTY_FRAME_ARGUMENT '+arg);
 if(arg==='--out')out=path.resolve(value);else if(arg==='--repo')repo=path.resolve(value);
 else if(arg==='--MainFile')mainFile=path.resolve(value);else if(arg==='--common-hid-runtime-case')commonHidRuntimeCase=value;else headerRoot=path.resolve(value);
}
mainFile??=path.join(repo,'native/main.cpp');headerRoot??=path.join(repo,'native');
assert(!(commonHidOnly&&commonHidRuntimeOnly),'MUTUALLY_EXCLUSIVE_COMMON_HID_PROJECTIONS');
assert(!commonHidRuntimeCase||commonHidRuntimeOnly,'RUNTIME_CASE_REQUIRES_RUNTIME_PROJECTION');
if(commonHidRuntimeOnly){
 const {runCommonHidRuntime,win5KeptWakeCase}=await import(new URL('./lib/common_hid_runtime_fixture.mjs',import.meta.url).href);
 assert(commonHidRuntimeCase!==win5KeptWakeCase||!seenArguments.has('--MainFile'),'WIN5_KEPT_WAKE_DOES_NOT_CONSUME_MAIN_FILE');
 const inherited=commonHidRuntimeCase===win5KeptWakeCase&&process.env.YMCC_SIM_GLOBAL_RUN==='1';
 const inheritedDeadlineEpochMs=inherited?Number(process.env.YMCC_SIM_CASE_DEADLINE_EPOCH_MS):undefined;
 const result=await runCommonHidRuntime({repo,out,mainFile,headerRoot,prepareOnly,runtimeCase:commonHidRuntimeCase??'all',inheritedDeadlineEpochMs});
 console.log(JSON.stringify({status:result.status,run:result.run}));process.exit(process.exitCode??0);
}
const candidateFixture=path.join(path.dirname(fileURLToPath(import.meta.url)),'fixtures/input_frame_submission_fixture.cpp');
const sourcePath=name=>name==='tools/input_frame_submission_selftest.mjs'?fileURLToPath(import.meta.url):name==='tools/fixtures/input_frame_submission_fixture.cpp'?path.join(path.dirname(fileURLToPath(import.meta.url)),'fixtures/input_frame_submission_fixture.cpp'):name==='native/main.cpp'?mainFile:
 name==='tools/input_frame_submission_selftest.mjs'?fileURLToPath(import.meta.url):
 name==='tools/fixtures/input_frame_submission_fixture.cpp'?candidateFixture:
 name.startsWith('native/')&&fs.existsSync(path.join(headerRoot,path.basename(name)))?path.join(headerRoot,path.basename(name)):path.join(repo,name);
const {calibrationToolchainPreamble}=await import(pathToFileURL(path.join(repo,'tools/fixtures/hc_reviewed_calibration/toolchain.mjs')).href);
const {controllerInputFixtureInputs,snapshotControllerInputFixture}=await import(pathToFileURL(path.join(repo,'tools/lib/hc_controller_input_fixture.mjs')).href);
const {extract}=await import(pathToFileURL(path.join(repo,'tools/fixtures/msi_owned_return_retired_io/source-binding.mjs')).href);
const run=path.join(out,new Date().toISOString().replace(/[:.]/g,'-'));
fs.mkdirSync(run,{recursive:true});
const inc=path.join(run,'source');fs.mkdirSync(inc);
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const save=(n,j)=>fs.writeFileSync(path.join(run,n),JSON.stringify(j,null,2)+'\n');
const identity=p=>({path:fs.realpathSync(p),bytes:fs.statSync(p).size,sha256:sha(fs.readFileSync(p))});
let phase='freeze';
process.on('uncaughtException',error=>{
  const receipt={passed:false,status:'FAIL',run,phase,error:String(error.stack??error),elapsedMs:performance.now()-started};
  save('FAILURE.json',receipt);fs.writeFileSync(path.join(out,'LATEST.json'),JSON.stringify(receipt,null,2)+'\n');
  console.error(receipt.error);process.exitCode=1;
});
const headerNames=['native/input_frame_submission.h','native/touch_haptic_dispatch.h','native/game_feedback_receipt.h',
  'native/windows_motion_submission.h','native/legion_motion_source.h','native/virtual_output_status.h',
  'native/controller_persona.h','native/canonical_frame_fingerprint_memo.h','native/msi_virtual_report_diagnostic.h','native/input_host_shared_power.h','native/input_host_compact_frame_reply.h','native/sleep_power_state.h','native/sleep_power_observation.h','native/sleep_power_revocation.h','deps/json/json.hpp'];
const names=['tools/fixtures/unified_controller_input/before.json','tools/fixtures/unified_controller_input/fixture.inc',
  ...controllerInputFixtureInputs,...headerNames,'native/screen_touchpads.h','native/main.cpp','tools/fixtures/input_frame_submission_fixture.cpp',
  'tools/input_frame_submission_selftest.mjs','tools/fixtures/msi_owned_return_retired_io/source-binding.mjs'];
const bytes=new Map(names.map(n=>[n,fs.readFileSync(sourcePath(n))]));
const main=bytes.get('native/main.cpp').toString('utf8').replace(/\r\n/g,'\n');
const bindings=[];
const uiPath='native/virtual_controller_ui_output_runtime.h';bytes.set(uiPath,fs.readFileSync(sourcePath(uiPath)));
const uiSource=bytes.get(uiPath).toString('utf8').replace(/\r\n/g,'\n');
const uiSymbols=new Set(['FocusFrameContent','focusIsolationNeutralContent','focusIsolationAssembleFrame','focusIsolationBuildTypedSubmitText']);
const fn=n=>extract(uiSymbols.has(n)?uiSource:main,'function',n,uiSymbols.has(n)?uiPath:'native/main.cpp',bindings);
const struct=n=>extract(uiSymbols.has(n)?uiSource:main,'struct',n,uiSymbols.has(n)?uiPath:'native/main.cpp',bindings);
function block(text,label,a,b){
  const i=text.indexOf(a),j=text.indexOf(b,i);assert(i>=0&&j>i,label);
  const s=text.slice(i,j);bindings.push({symbol:label,sha256:sha(s),exact:true});return s;
}
const submit=fn('inputHostSubmitPad'),request=fn('inputHostRequestPreparedLocked');
const readState=fn('gamepadReadStateUnlocked');
const evaluate=fn(main.includes('static void gamepadEvalImpl(')?'gamepadEvalImpl':'gamepadEval');
const context=struct('InputFrameSubmission')+'\nstatic thread_local InputFrameSubmission* t_inputFrameSubmission=nullptr;\n'+
  struct('InputFrameSubmissionScope')+'\n'+struct('GameFeedbackFrameDispatchScope')+'\n'+struct('InputHostRequestPowerScope');
const current=fn('inputHostFrameSubmissionCurrent');
const beforeClassifier=JSON.parse(bytes.get('tools/fixtures/unified_controller_input/before.json').toString('utf8'));
assert.equal(sha(beforeClassifier.code),beforeClassifier.codeSha256,'fixed before-classifier bytes');
const screenSource=bytes.get('native/screen_touchpads.h').toString('utf8').split(String.fromCharCode(13)).join('');
const screenContact=extract(screenSource,'struct','Contact','native/screen_touchpads.h',bindings);
const screenSnapshot=extract(screenSource,'struct','Snapshot','native/screen_touchpads.h',bindings);
const screenJson=extract(screenSource.replace('inline Json snapshotJson(', 'static Json snapshotJson('),
 'function','snapshotJson','native/screen_touchpads.h',[]).replace('static Json snapshotJson(', 'inline Json snapshotJson(');
assert(screenSource.includes(screenJson),'EXACT_ACTUAL_SCREEN_JSON');
bindings.push({symbol:'actual inline snapshotJson',file:'native/screen_touchpads.h',sha256:sha(screenJson),exact:true});
const compactControlDeclarations=['static std::string g_inputHostFrameReplyBinding;','static bool g_inputHostCompactFrameRequest = false;'];
assert(compactControlDeclarations.every(value=>main.includes(value)),'ACTUAL_COMPACT_CONTROL_DECLARATIONS');
const parts={
  SCREENPAD_TYPES:[screenContact,screenSnapshot,screenJson].join(String.fromCharCode(10)),
  FAST_RECEIPT:struct('InputHostFastReceipt'),
  COMPACT_GLOBALS:compactControlDeclarations.join(String.fromCharCode(10)),
  UNIFIED_CONTROLLER_INPUT:bytes.get('tools/fixtures/unified_controller_input/fixture.inc').toString('utf8'),
  HARDWARE_TLS:main.match(/^static thread_local bool t_inputHostRequestHardwareRequired=false;\r?$/m)?.[0],CONTEXT:context,SNAPSHOT_TYPE:struct('InputCaptureGamepadSnapshot')+'\n'+struct('CommonControllerHidRead'),POWER_WRITER:fn('sgVirtualWriterAllowedNow'),
  COMMON_SOURCE_SCOPE:struct('CommonFrameSourceScope'),
  ADAPTERS:[fn('inputCaptureBindStandardInput'),fn('inputHostPhysicalPublicationSourceAdmitted'),fn('inputHostFrameSubmissionSample'),current,
    fn('inputHostFrameSubmissionNote'),fn('inputHostWindowsMotionSubmissionCurrent'),fn('inputHostFrameSharedPowerCurrent'),fn('inputHostFrameSubmissionMayIssue'),fn('inputHostFrameSubmissionRequestCurrent'),
    beforeClassifier.code.replace('inputHostFrameSubmissionSample(', 'inputHostFrameSubmissionSampleBeforeUnified(')].join('\n'),
  PERSONA:[fn('inputHostConfiguredPersona'),fn('inputHostPersona'),fn('inputHostProfileIdentityOf'),fn('inputHostProfileIdentity'),
    fn('inputHostBoundOrCurrentPersona'),fn('inputHostBoundOrCurrentProfileIdentity'),fn('inputHostProtocolPowerGenerationLocked')].join('\n'),
  ENVELOPE:[block(main,'actual protocol constants','static constexpr const char* kInputHostProtocolConfigHash =','// Approved upstream HIDMaestro'),
    fn('inputHostEnvelopeLocked')].join('\n'),
  SERIALIZERS:[block(main,'actual number cache and formatting','class C6aNumberTextCache {','static void c6aAppendEscaped'),
    block(main,'actual escaping overloads','static void c6aAppendEscaped(std::string& out, const std::string& value)','static void c6aAppendUInt'),
    fn('c6aAppendUInt'),fn('c6aAppendInt'),fn('c6aAppendJsonValue'),fn('focusIsolationBuildTypedSubmitText')].join('\n'),
  HASH:[fn('inputHostCryptoProvider'),fn('inputHostSha256'),fn('inputHostCanonicalFrameHash')].join('\n'),
  NORMALIZE_AXIS:fn('inputHostNormalizeAxis'),
  SNAPSHOT_GUIDE:block(readState,'actual physical snapshot and Guide capture','        g_inputCaptureGamepadSnapshot = {live, pad, g_xinputPrimarySlot, admittedMask, inputSource};','        g_inputCaptureGamepadSnapshot.readAttemptSequence='),
  GUIDE_ASSEMBLY:block(submit,'actual Guide composition before final frame','    XINPUT_GAMEPAD outputPad = pad;','    // G19/HC parity:'),
  UI_SOURCE_FIELD:block(evaluate,'actual neutral-admission source diagnostic','            {"source",','            {"buttonNeutral",'),
  FRAME:struct('FocusFrameContent')+'\n'+fn('focusIsolationNeutralContent')+'\n'+fn('focusIsolationAssembleFrame'),
  FRAME_CONTENT:block(submit,'actual standard frame content assignments','    FocusFrameContent frameContent;','    // 背键屏蔽同样经生产函数'),
  SETUP:block(submit,'normal frame original ticket scope','    // Bind normal BUS input','    // OFF revokes normal BUS'),
  POWER:block(submit,'initial normal-frame power admission','    const bool powerAdmitted =','    if (!powerAdmitted)'),
  NORMALIZE:block(submit,'stale source normalization and exact source-neutral publication bypass','        frameSubmission.powerScope=','        inputHostRecoveryHealthObserveLocked'),
  FRESH_WAIT:block(submit,'opening wait preserves explicit stale-source neutral cleanup','    if (!msiNeutralCleanup && !frameSubmission.sourceNeutralized','        ymcc::screenpads::observeNativeTransport'),
  ACCEPTED_NOTE:block(submit,'log recovery only after accepted current frame','                if(t_inputFrameSubmission && !frameSubmission.sourceNeutralized)','                firstFrame = fastReceipt.used'),
  HOST_READY:block(submit,'final Host readiness','        const bool hostFrameReady =','        const ymcc::screenpads::NativeTransportObservation'),
  ASSEMBLY:block(submit,'effective full neutral assembly','    FocusFrameContent effectiveContent =','    // B1：hash / motionProof'),
  LEGACY_PAYLOAD:block(submit,'actual legacy frame metadata and request dispatch','            const auto submitFrameLegacy =','            bool frameTextReady = false;'),
  LEGACY_ENVELOPE:block(request,'actual legacy envelope merge and serialization','        json request = inputHostEnvelopeLocked(command, false);','        if (ymcc::inputChainCommand::supported(command))'),
  PREPARED_ENVELOPE:block(request,'actual prepared text selection','        requestText = *preparedText;','    } else {'),
  DISPATCH:block(submit,'actual prepared/legacy dispatch','            accepted = frameTextReady','\n#ifdef YMCC_MSI_REPORT_CHAIN_DIAGNOSTIC'),
  RESULT_NEUTRAL:submit.match(/^    result\["focusIsolationSemanticNeutral"\] = semanticNeutral;$/m)?.[0],
  REPORT_NEUTRAL:main.match(/^    publishedSemanticNeutral=hostFrame\.value\("focusIsolationSemanticNeutral",submitNeutral\);$/m)?.[0],
  NOFAULT:block(submit,'context retirement response branch','                // Context retirement is not a Host fault.','            } else {\n                // 跳过原因计数'),
  PRE_ENVELOPE:request.match(/^    if\(!inputHostFrameSubmissionRequestCurrent\(command,responseOut,"before-envelope"\)\)return false;$/m)?.[0],
  PRE_PIPE:request.match(/^    if\(connected&&!inputHostFrameSubmissionRequestCurrent\(command,responseOut,"before-pipe-write"\)\)return false;$/m)?.[0]
};
assert(parts.PRE_ENVELOPE&&parts.PRE_PIPE);
const actualWrite=/const bool written = connected &&(?: \(!ioDeadline\|\|writeBudget\) &&)? inputHostPipeIo\(/.exec(request);
assert(actualWrite,'actual bounded pipe write required');
assert(request.indexOf(parts.PRE_PIPE)>request.indexOf('inputHostWaitClientLocked(')&&request.indexOf(parts.PRE_PIPE)<actualWrite.index);
assert(!/lock_guard|unique_lock|Snapshot\(/.test(current),'Current frame gates must not nest a new mutex');
for(const n of headerNames)fs.writeFileSync(path.join(inc,path.basename(n)),bytes.get(n));
let fixture=bytes.get('tools/fixtures/input_frame_submission_fixture.cpp').toString('utf8');
const controllerFixture=snapshotControllerInputFixture(repo,path.join(run,'controller-input'));fixture=controllerFixture.preamble+fixture;
for(const[k,v]of Object.entries(parts)){assert(v,'Missing '+k);assert.equal(fixture.split('@@'+k+'@@').length,2,k);fixture=fixture.replace('@@'+k+'@@',()=>v);}
assert(!/@@\w+@@/.test(fixture));fs.writeFileSync(path.join(run,'fixture.cpp'),fixture);
save('source-binding.json',{repo,sourceHashes:Object.fromEntries([...bytes].map(([n,b])=>[n,sha(b)])),extracted:bindings,
  transport:'Inert sink after real pre-envelope/pre-write guards and real dispatch expression; whole inputHostSubmitPad/request and actual pipe IO are not compiled.',
  frame:'Actual binder, frame content assignments, complete focus neutral/assembly/typed serializer and actual legacy envelope merge.',
  boundaryInputs:'Opening-neutral readiness, OS/request-ID input, motion-proof source and frame blend/calibration outputs are fixture inputs; not an entire OnGamepad replay.',noHardware:true});
if(prepareOnly){
 save('PREPARED.json',{status:'PREPARED_NOT_COMPILED_NOT_EXECUTED',originalCanonicalId:'controller.unified-standard-input',
  mainFile,headerRoot,run,compiledParts:Object.entries(parts).map(([symbol,code])=>({symbol,sha256:sha(code)})),
  resolvedInputs:[...bytes].map(([name,value])=>({name,path:sourcePath(name),bytes:value.length,sha256:sha(value)})),
  generatedFixture:identity(path.join(run,'fixture.cpp')),actualCompiles:0,actualExecutions:0,originalFourProfilesNotRun:true,
  actualWindowsEventsPrepared:true,noDeviceIo:true,fullParentExecuted:false,sourceReady:false});
 console.log(JSON.stringify({status:'PREPARED_NOT_COMPILED_NOT_EXECUTED',run}));process.exit(0);
}
const vc=process.env.YMCC_VCVARS64??'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
assert(fs.existsSync(vc));const results={};
const quote=s=>{assert(!/["\r\n%]/.test(s));return '"'+s+'"';};
const temporary=path.join(run,'temporary');fs.mkdirSync(temporary);
const childEnv={...process.env,TEMP:temporary,TMP:temporary};
const tools=new Map([[process.execPath,identity(process.execPath)],[vc,identity(vc)]]);
phase='build-and-execute';
for(const profile of ['Release','Checked'])for(const diag of [false,true]){
  const label=profile+(diag?'-diagnostic-on':'-diagnostic-off'),dir=path.join(run,label);fs.mkdirSync(dir);
  const exe=path.join(dir,'fixture.exe'),cmd=path.join(dir,'build.cmd');
  const flags=profile==='Release'?'/O2 /MT /DNDEBUG':'/Od /MTd /RTC1 /D_DEBUG';
  fs.writeFileSync(cmd,calibrationToolchainPreamble(vc)+'set VSLANG=1033\r\nwhere cl > '+quote(path.join(dir,'cl-path.txt'))+'\r\n'+
    'cl /nologo /std:c++20 /utf-8 /EHsc /W4 /WX /permissive- /sdl /showIncludes '+flags+
    (diag?' /DYMCC_MSI_REPORT_CHAIN_DIAGNOSTIC':'')+(commonHidOnly?' /DYMCC_COMMON_HID_DIRECTED':'')+' /I'+quote(inc)+' '+quote(path.join(run,'fixture.cpp'))+
    ' /Fo'+quote(path.join(dir,'fixture.obj'))+' /Fe'+quote(exe)+' /link /VERBOSE:LIB advapi32.lib\r\nexit /b %errorlevel%\r\n');
  let r=spawnSync('cmd.exe',['/d','/c',cmd],{cwd:dir,env:childEnv,encoding:'utf8',timeout:60000});
  fs.writeFileSync(path.join(dir,'build.log'),(r.stdout??'')+(r.stderr??''));assert.equal(r.status,0,'Execution/build failed; see '+dir);
  const cl=fs.readFileSync(path.join(dir,'cl-path.txt'),'utf8').trim().split(/\r?\n/)[0];
  for(const p of [cl,...['link.exe','c1xx.dll','c2.dll','mspdb140.dll'].map(n=>path.join(path.dirname(cl),n))])if(fs.existsSync(p))tools.set(p,identity(p));
  const dependencies=new Map();
  for(const line of r.stdout.split(/\r?\n/)){
    if(/\(\d+(?:,\d+)?\):/.test(line))continue;
    const match=line.match(/([A-Za-z]:[\\/].+)$/);if(!match)continue;
    const p=match[1].trim().replace(/:$/,'');if(fs.existsSync(p)&&fs.statSync(p).isFile())dependencies.set(p,identity(p));
  }
  assert(dependencies.size>10,'Compiler must report actual include/dependency files');
  fs.writeFileSync(path.join(dir,'dependencies.json'),JSON.stringify([...dependencies.values()],null,2)+'\n');
  r=spawnSync(exe,[],{cwd:dir,env:childEnv,encoding:'utf8',timeout:15000});
  fs.writeFileSync(path.join(dir,'results.json'),r.stdout??'');fs.writeFileSync(path.join(dir,'execute.stderr.log'),r.stderr??'');
  assert.equal(r.status,0,'Execution/build failed; see '+dir);const j=JSON.parse(r.stdout);assert.equal(j.failures.length,0);
  assert(j.issuedCommands.length>0,'Actual issued envelopes required');
  const issuedPath=path.join(dir,'issued-submit-frames.jsonl');
  fs.writeFileSync(issuedPath,j.issuedCommands.map(row=>JSON.stringify(row)+'\n').join(''));
  results[label]={passed:true,cases:j.cases.length,assertions:j.assertions,exe:identity(exe),issued:identity(issuedPath),issuedCount:j.issuedCommands.length};
  console.log(label+': '+JSON.stringify(results[label]));
}
save('tool-identities.json',[...tools.values()]);
phase='verify-source';
const liveMain=fs.readFileSync(mainFile,'utf8').replace(/\r\n/g,'\n');
const liveUi=fs.readFileSync(sourcePath(uiPath),'utf8').replace(/\r\n/g,'\n');
const checkedDefinitions=bindings.filter(b=>b.kind).map(b=>{
  const code=extract(b.file===uiPath?liveUi:b.file==='native/main.cpp'?liveMain:fs.readFileSync(sourcePath(b.file),'utf8').replace(/\r\n/g,'\n'),b.kind,b.symbol,b.file,[]);
  return {symbol:b.symbol,file:b.file,compiledSha256:b.sha256,currentSha256:sha(code),same:sha(code)===b.sha256};
});
const changedRelated=checkedDefinitions.filter(b=>!b.same),reportNeutralStable=liveMain.includes(parts.REPORT_NEUTRAL);
save('current-fragments.json',{checkedDefinitions,changedRelated,reportNeutralStable});
const sourceChanged=[...bytes.keys()].filter(n=>sha(fs.readFileSync(sourcePath(n)))!==sha(bytes.get(n)));
assert.equal(changedRelated.length,0,'Compiled source definitions changed during run');assert(reportNeutralStable,'Published-neutral expression changed');
assert.equal(sourceChanged.length,0,'Source or fixture dependencies changed during run');
const issuedSource=results['Release-diagnostic-off'].issued.path;
const issued=path.join(run,'issued-submit-frames.jsonl');fs.copyFileSync(issuedSource,issued);
const summary={passed:true,status:'PASS_SELECTED_SCOPE',run,results,sourceChangedDuringRun:sourceChanged,
  sourceMainSha256:sha(bytes.get('native/main.cpp')),node:identity(process.execPath),
  issuedCommands:identity(issued),issuedCommandsFrom:'Release-diagnostic-off',
  exactFragmentsOnly:true,noDeviceIo:true,noHostOrSdkExecuted:true,noPipeIo:true,
  currentMainlineSubmitRequestFunctionsSha256:sha(submit+request),relatedDefinitionsStable:true,
  fullPhysicalToConsumerAcceptance:false,elapsedMs:performance.now()-started};
save('RESULT.json',summary);fs.writeFileSync(path.join(out,'LATEST.json'),JSON.stringify(summary,null,2)+'\n');
console.log('Evidence: '+run);
