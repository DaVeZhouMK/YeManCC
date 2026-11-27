import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {extract,sha} from '../msi_owned_return_retired_io/source-binding.mjs';
import {calibrationToolchainPreamble} from '../hc_reviewed_calibration/toolchain.mjs';
import {startOwnedRawProducer,rawProgramLauncherDeclaration} from '../../ymcc_sim/global/decky-persistent-lifecycle/raw-owned-producer.mjs';

export function prepareInputResumeSource(main,template){
 const bindings=[],take=name=>{const marker='[[maybe_unused]] static bool '+name+'(',at=main.indexOf(marker);if(at<0)return extract(main,'function',name,'native/main.cpp',bindings);const begin=at+'[[maybe_unused]] '.length,rows=[],code=extract(main.slice(begin),'function',name,'native/main.cpp',rows);for(const row of rows){row.startUtf8Byte+=Buffer.byteLength(main.slice(0,begin));row.endUtf8ByteExclusive+=Buffer.byteLength(main.slice(0,begin));row.attributeOmitted='[[maybe_unused]]';bindings.push(row);}return code;},structure=name=>extract(main,'struct',name,'native/main.cpp',bindings);
 const powerEnum=main.match(/^enum class PowerLifecycle\s*:\s*uint8_t\s*\{[^\n]+\};$/m);assert(powerEnum);
 const line=anchor=>{const rows=main.split('\n').filter(row=>row.startsWith(anchor));assert.equal(rows.length,1,'Unique original declaration '+anchor);return rows[0];};
 const guardParts=[line('static thread_local bool tls_rogRemapActive'),structure('RogRemapGuardScope'),take('rogRemapTryEnter'),take('rogRemapTryEnterWithin'),take('rogRemapLeaveBoundary'),structure('RogRemapBoundaryScope'),line('static constexpr unsigned kRogRemapRecoverWaitBudgetMs')].join('\n');
 const bind=take('rogRecoverBindSerialized'),scoped=bind.includes('unsigned long long powerGeneration'),pending=scoped?[structure('RogRecoverPending'),line('static std::mutex g_rogRecoverPendingMx'),line('static RogRecoverPending g_rogRecoverPending;')].join('\n'):line('static std::atomic<unsigned long long> g_rogRecoverPendingGen');
 const at=main.indexOf('    case WM_POWER_INPUT_RECOVER: {'),end=main.indexOf('    case WM_QUERYENDSESSION:',at);assert(at>=0&&end>at&&at===main.lastIndexOf('    case WM_POWER_INPUT_RECOVER: {'));
 const caseText=main.slice(at,end),signature='static LRESULT fixtureActualInputCase(WPARAM w) ',adapted=signature+caseText.slice(caseText.indexOf('{'));
 const wholeCase=extract(adapted,'function','fixtureActualInputCase','native/main.cpp',[]).slice(signature.length);
 bindings.push({symbol:'WM_POWER_INPUT_RECOVER',kind:'case',file:'native/main.cpp',beginUtf8Byte:Buffer.byteLength(main.slice(0,at)),endUtf8ByteExclusive:Buffer.byteLength(main.slice(0,at))+Buffer.byteLength('    case WM_POWER_INPUT_RECOVER: '+wholeCase),sha256:sha(wholeCase),onlyCaseHeaderAdaptedForBalancedExtractor:true});
 const timerAt=main.indexOf('if(w==kSleepPowerRedeliveryTimer){');assert(timerAt>=0&&timerAt===main.lastIndexOf('if(w==kSleepPowerRedeliveryTimer){'));
 const timerTail=main.slice(timerAt),timerFunction='static LRESULT fixtureActualTimer() '+timerTail.slice(timerTail.indexOf('{'));
 const timerBody=extract(timerFunction,'function','fixtureActualTimer','native/main.cpp',[]).slice('static LRESULT fixtureActualTimer() '.length),timer='if(w==kSleepPowerRedeliveryTimer)'+timerBody;
 const parts={POWER_ENUM:powerEnum[0],MESSAGES:['#define WM_POWER_RESUME_COMMIT ','#define WM_POWER_INPUT_RECOVER ','static constexpr UINT kSleepPowerForwardMessage=','static constexpr UINT kSharedPowerStateMessage=','static constexpr UINT_PTR kSleepPowerRedeliveryTimer='].map(line).join('\n'),ROG_GUARDS:guardParts,ROG_PENDING:pending,ROG_BIND:bind.replace('static bool rogRecoverBindSerialized(','static bool productionRogRecoverBindSerialized('),ROG_CONSUME:take('rogRemapConsumeDeferredRecover'),ROG_INVOKE:scoped?'return productionRogRecoverBindSerialized(why,generation,dispatched);':'(void)generation;if(dispatched)*dispatched=true;return productionRogRecoverBindSerialized(why);',GAMEPAD_PROGRESS:main.includes('struct GamepadResumeProgress {')?structure('GamepadResumeProgress'):'',RECOVER:take('gamepadRecoverAfterResume'),DELIVERY:[line('static std::atomic<unsigned long long> g_powerResumeCommitRedelivery'),take('sleepPowerArmRedelivery'),...(main.includes('static void sleepPowerArmUnknownRedelivery(')?[take('sleepPowerArmUnknownRedelivery')]:[]),...(main.includes('static void sleepPowerRetainCommittedDelivery(')?[take('sleepPowerRetainCommittedDelivery')]:[]),take('sleepPowerRedeliverCommitted')].join('\n'),INPUT_CASE:wholeCase,TIMER:timer,CLEAR_ROG_PENDING:scoped?'std::lock_guard<std::mutex> lock(g_rogRecoverPendingMx);g_rogRecoverPending={};':'g_rogRecoverPendingGen.store(0);',HAS_ROG_PENDING:scoped?'std::lock_guard<std::mutex> lock(g_rogRecoverPendingMx);return g_rogRecoverPending.device!=0;':'return g_rogRecoverPendingGen.load()!=0;'};
 if(timerBody.includes('sgRedeliverS4Wake(h);')){
  const state=[...main.matchAll(/^static struct SgFailedS4Delivery \{[\s\S]*?^\} g_sgFailedS4Delivery;/gm)];assert.equal(state.length,1,'Unique original S4 delivery state');
  const redelivery=take('sgRedeliverS4Wake');
  parts.MESSAGES+='\n'+['#define WM_SG_S4_WAKE ','#define WM_SG_S0_WAKE ','static constexpr UINT WM_FAN_KERNEL_POWER_FACT ',
   'static std::atomic<ULONGLONG> g_sgLatestSleepEntryFileTime{','static std::atomic<ULONGLONG> g_sgClosedSleepIntentFileTime{'].map(line).join('\n');
  parts.DELIVERY+='\n'+[line('static std::mutex g_sgFailedS4Mutex;'),state[0][0],line('static SgFailedS4Delivery::Wake g_sgOriginalHibernateIngress;'),take('sgKernelEventSupersededByS4'),redelivery].join('\n');
 }
 if(wholeCase.includes('g_wakeFocusNudgePower'))parts.DELIVERY+='\n'+line('static ymcc::sleeppower::SharedPowerV1 g_wakeFocusNudgePower;');
 if(template.includes('@@RAW_READER@@')){
 const commitAt=main.indexOf('    case WM_POWER_RESUME_COMMIT: {'),commitEnd=main.indexOf('    case WM_POWER_INPUT_RECOVER:',commitAt);assert(commitAt>=0&&commitEnd>commitAt);const text=main.slice(commitAt,commitEnd),signature='static LRESULT completeActualCommit(WPARAM w,LPARAM l) ';const body=extract(signature+text.slice(text.indexOf('{')),'function','completeActualCommit','native/main.cpp',[]).slice(signature.length);
 bindings.push({symbol:'WM_POWER_RESUME_COMMIT',kind:'case',file:'native/main.cpp',beginUtf8Byte:Buffer.byteLength(main.slice(0,commitAt)),bodySha256:sha(body),onlyCaseHeaderAdaptedForBalancedExtractor:true});
 Object.assign(parts,{RAW_READER:take('readSharedSystemPower').replace('static int __cdecl readSharedSystemPower(','static int __cdecl fixtureProductionReadSharedSystemPower('),POWER_CAPTURE:'[[maybe_unused]] '+take('captureSharedPowerLifecycle'),POWER_CURRENT:take('sharedPowerLifecycleCurrent'),PUBLIC_COMMIT:body,PUBLIC_PROJECTION:take('projectSharedPowerLifecycle'),PUBLIC_PUBLISHER:take('publishSharedPowerLifecycle').replace('static void publishSharedPowerLifecycle(','static void productionPublishSharedPowerLifecycle(')});
 if(template.includes('@@PUBLIC_PROJECTION_WINDOW@@')){
  const queue=parts.PUBLIC_PUBLISHER.includes('sleepPowerQueueProjection(')?take('sleepPowerQueueProjection')+'\n':'';
  const tagged=Boolean(queue)||parts.PUBLIC_PUBLISHER.includes('g_sharedPowerProjectionPosted');
  parts.PUBLIC_PROJECTION_WINDOW='static void diagnosticProjectionWindow([[maybe_unused]] HWND h,[[maybe_unused]] WPARAM w){projectSharedPowerLifecycle(h);}';
  parts.RESET_PUBLIC_PROJECTION='';
  if(tagged){
   const declarations=['static std::mutex g_sharedPowerProjectionPostMutex;','static bool g_sharedPowerProjectionPosted=','static constexpr WPARAM kSharedPowerProjectionPostReceipt='].map(line);
   const start=main.indexOf('    case kSharedPowerStateMessage: {'),end=main.indexOf('\n    case kSleepPowerForwardMessage:',start);
   assert(start>=0&&end>start&&start===main.lastIndexOf('    case kSharedPowerStateMessage: {'));
   const windowText=main.slice(start,end),sig='static LRESULT diagnosticProjectionWindow(HWND h,WPARAM w) ';
   const body=extract(sig+windowText.slice(windowText.indexOf('{')),'function','diagnosticProjectionWindow','native/main.cpp',[]).slice(sig.length);
   assert(body.includes('sleepPowerProjectionPostConsumed(w);'));
   bindings.push({symbol:'kSharedPowerStateMessage',kind:'case',file:'native/main.cpp',beginUtf8Byte:Buffer.byteLength(main.slice(0,start)),bodySha256:sha(body),onlyCaseHeaderAdaptedForBalancedExtractor:true});
   parts.PUBLIC_PUBLISHER=declarations.join('\n')+'\n'+queue+parts.PUBLIC_PUBLISHER+'\n'+take('sleepPowerProjectionPostConsumed');
   parts.PUBLIC_PROJECTION_WINDOW=sig+body;
   parts.RESET_PUBLIC_PROJECTION='sleepPowerProjectionPostConsumed(kSharedPowerProjectionPostReceipt);';
  }
 }else assert(!/g_sharedPowerProjectionPosted|sleepPowerQueueProjection\(/.test(parts.PUBLIC_PUBLISHER),'INPUT_RESUME_TAGGED_PROJECTION_REQUIRES_WINDOW_RECEIPT');
 }
 let code=template;for(const [name,value]of Object.entries(parts)){const marker='@@'+name+'@@';assert.equal(code.split(marker).length,2,'Unique original fixture slot '+name);code=code.replace(marker,()=>value);}assert(!/@@\w+@@/.test(code));
 return {code,bindings,compiledParts:Object.entries(parts).map(([symbol,value])=>({symbol,sha256:sha(value)})),adaptations:['Exact ROG bind function identifier renamed to productionRogRecoverBindSerialized; wrapper observes attempts and forwards exact result/output.','Original timed_mutex has an observer which forwards its exact real timed lock; original guard/scopes and800ms wait execute.','Case header adapted solely to lexical function extractor; original case body is byte-preserved.','Legacy source without borrowed power uses original one-argument method; new consumer supplies immutable generation.','Device, Host, screenpad/MSI and focus/timer leaves are inert; ReadyFlag observes exact stores and uses real atomic storage.']};
}
export async function runInputResumeFixture({repo,out,selectedCase,mainFile=path.join(repo,'native/main.cpp'),headerRoot=path.join(repo,'native'),expectedMainSha256,prepareOnly=false}){
 const startedEpochMs=Date.now();
 const started=performance.now();assert(!fs.existsSync(out),'INPUT_RESUME_FRESH_OUTPUT_REQUIRED');fs.mkdirSync(out,{recursive:true});
 const save=(name,data)=>fs.writeFileSync(path.join(out,name),typeof data==='string'||Buffer.isBuffer(data)?data:JSON.stringify(data,null,2)+'\n',{flag:'wx'});
 assert(path.isAbsolute(mainFile)&&path.isAbsolute(headerRoot),'INPUT_RESUME_ABSOLUTE_SOURCE_REQUIRED');
 const headers=['sleep_power_dispatch.h','sleep_power_state.h','sleep_power_observation.h','sleep_wake_admission.h','recovery_resume_commit.h'];
 const sourcePaths=[...headers.map(name=>'native/'+name),'native/main.cpp','deps/json/json.hpp','tools/input_power_gate_r122_selftest.mjs','tools/fixtures/input_power_gate_r122/resume.mjs','tools/fixtures/input_power_gate_r122/resume.cpp','tools/fixtures/msi_owned_return_retired_io/source-binding.mjs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs'];
 const observedPath=relative=>relative==='native/main.cpp'?mainFile:headers.some(name=>relative==='native/'+name)?path.join(headerRoot,path.basename(relative)):path.join(repo,relative);
 const bytes=new Map(sourcePaths.map(relative=>[relative,fs.readFileSync(observedPath(relative))]));
 if(expectedMainSha256)assert.equal(sha(bytes.get('native/main.cpp')),expectedMainSha256,'INPUT_RESUME_MAIN_IDENTITY_MISMATCH');
 for(const name of ['fixture.cpp','fixture.exe','fixture.obj','json.hpp',...headers,'dependencies.json'])assert(path.resolve(out,name).length<=259,'INPUT_RESUME_NATIVE_OUTPUT_PATH_TOO_LONG');
 const main=bytes.get('native/main.cpp').toString('utf8'),prepared=prepareInputResumeSource(main,bytes.get('tools/fixtures/input_power_gate_r122/resume.cpp').toString('utf8'));
 save('fixture.cpp',prepared.code);save('json.hpp',bytes.get('deps/json/json.hpp').toString('utf8'));for(const name of headers)save(name,bytes.get('native/'+name));
 save('TASK.json',{originalParentId:'controller.unified-standard-input',originalEntry:'tools/input_power_gate_r122_selftest.mjs',selection:'resume-only',selectedCase:selectedCase??null,bindings:prepared.bindings,compiledParts:prepared.compiledParts,adaptations:prepared.adaptations,sourceHashes:[...bytes].map(([relative,value])=>({relative,path:observedPath(relative),bytes:value.length,sha256:sha(value)})),compiledSha256:sha(prepared.code),scope:'Exact complete input message and gamepad progress, real ROG bounded mutex/deferral, actual Dispatch and original input delivery timer. SharedPowerV1 is the actual ABI with an explicit current-generation memory reader. Registration, Device/Host/screenpad/MSI/focus leaves are inert; no physical proof.'});
 const vc=process.env.YMCC_VCVARS64??'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
 const q=value=>{assert(!/["%\r\n]/.test(value));return '"'+value+'"';};
 const exe=path.join(out,'fixture.exe'),command=calibrationToolchainPreamble(vc,{boundInheritedPath:true})+'set VSLANG=1033\r\necho YMCC_INPUT_RESUME_TOOLCHAIN_BEGIN\r\nwhere cl\r\nwhere link\r\nset VCTools\r\nset WindowsSDK\r\nset INCLUDE\r\nset LIB\r\nset PATH\r\necho YMCC_INPUT_RESUME_TOOLCHAIN_END\r\ncl /nologo /std:c++20 /utf-8 /EHsc /W4 /WX /permissive- /sdl /O2 /MT /DNDEBUG /I'+q(out)+' '+q(path.join(out,'fixture.cpp'))+' /Fo'+q(path.join(out,'fixture.obj'))+' /Fe'+q(exe)+' /sourceDependencies '+q(path.join(out,'dependencies.json'))+' /link /VERBOSE:LIB\r\nexit /b %errorlevel%\r\n';
 save('build.cmd',command);
 const processes=[];
 const powershell=process.env.YMCC_SIM_RAW_PROGRAM_PWSH??'G:/YeManCC-Work/Environment/Toolchains/PowerShell/7.4.6/pwsh.exe';
 const nativeToolsFile=path.join(repo,'tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json');
 const nativeTools=JSON.parse(fs.readFileSync(nativeToolsFile,'utf8').replace(/^\uFEFF/,''));
 const pin=file=>{const value=fs.readFileSync(file);return {path:fs.realpathSync(file),bytes:value.length,sha256:sha(value)};};
 const unique=rows=>[...new Map(rows.map(row=>[path.resolve(row.path).toLowerCase(),row])).values()];
 const launcher=rawProgramLauncherDeclaration({...process.env,YMCC_SIM_RAW_PROGRAM_PWSH:powershell});
 const tools=unique([process.execPath,process.env.ComSpec??'C:/Windows/System32/cmd.exe',vc,...launcher.files,...nativeTools.files.map(row=>row.path)].map(pin));
 save('TOOL-CLOSURE.json',tools);
 const sourceInputs=unique([...bytes].map(([relative])=>pin(observedPath(relative))).concat([pin(nativeToolsFile),pin(path.join(repo,'tools/ymcc_sim/global/host-process.ps1')),pin(path.join(repo,'tools/ymcc_sim/global/decky-persistent-lifecycle/raw-owned-producer.mjs')),...['fixture.cpp','json.hpp',...headers,'build.cmd'].map(name=>pin(path.join(out,name)))]));
 if(prepareOnly){const result={schemaVersion:1,status:'PREPARED_ORIGINAL_INPUT_RESUME_ONLY',out,mainObserved:pin(mainFile),compiled:pin(path.join(out,'fixture.cpp')),selectedCase:selectedCase??null,compiles:0,executions:0,fullParentExecuted:false,sourceReady:false,canonicalId:'controller.unified-standard-input',originalEntry:'tools/input_power_gate_r122_selftest.mjs',sourceInputs,sourceHashes:[...bytes].map(([relative])=>pin(observedPath(relative))),tools};save('PREPARED.json',result);return result;}
 const ownerArgs=['-NoLogo','-NoProfile','-NonInteractive','-Command','([Diagnostics.Process]::GetProcessById('+process.pid+')).StartTime.ToUniversalTime().Ticks.ToString()'];
 const owner=spawnSync(powershell,ownerArgs,{windowsHide:true,encoding:'utf8',timeout:5000});
 save('OWNER-IDENTITY-PROCESS.json',{program:pin(powershell),args:ownerArgs,pid:owner.pid,exitCode:owner.status,signal:owner.signal,error:owner.error?.message??null,stdout:owner.stdout,stderr:owner.stderr,scope:'Read-only producer birth query; not a business case'});
 assert.equal(owner.status,0,'INPUT_RESUME_OWNER_IDENTITY_FAILED');const ownerStartTicks=owner.stdout.trim();assert(/^\d{17,20}$/.test(ownerStartTicks));
 const call=async(name,program,args,timeout)=>{
  const begin=performance.now(),allocatedAtEpochMs=Date.now(),parentBudgetMs=150000,parentRemainingBeforeMs=parentBudgetMs-(allocatedAtEpochMs-startedEpochMs);
  assert(parentRemainingBeforeMs>3000,'INPUT_RESUME_PARENT_BUDGET_EXHAUSTED');
  const producer=startOwnedRawProducer({id:name,program,args,cwd:out,env:process.env,powershell,ownerStartTicks,host:path.join(repo,'tools/ymcc_sim/global/host-process.ps1'),directory:path.join(out,name+'-owned'),stdoutPath:path.join(out,name+'.stdout.log'),stderrPath:path.join(out,name+'.stderr.log'),allocatedAtEpochMs,parentStartedAtEpochMs:startedEpochMs,parentBudgetMs,originalBudgetMs:timeout,parentRemainingBeforeMs,deadlineEpochMs:allocatedAtEpochMs+Math.min(timeout,parentRemainingBeforeMs),cleanupReserveMs:2000,sourceInputs,toolArtifacts:unique([...tools,...(name==='execute'?[pin(exe)]:[])]),toolCleanupPolicy:name==='compile'?{kind:'MSVC_VCTIP_AFTER_ROOT_EXIT',maximumProcesses:1,program:pin(path.join(path.dirname(nativeTools.native.compiler),'vctip.exe'))}:null});
  const proof=await producer.wait();const row={name,program,args,pid:proof.pid??null,exitCode:proof.exitCode,signal:proof.signal??null,error:proof.error,timeoutMs:timeout,elapsedMs:performance.now()-begin,proof};save(name+'-PROCESS.json',row);processes.push(row);
  assert.equal(proof.error,null,'INPUT_RESUME_PROCESS_COLLECTION_FAILED');assert.equal(proof.rawExecution?.job?.activeProcessesFinal,0,'INPUT_RESUME_JOB_ZERO_REQUIRED');assert(name==='compile'?['NATURAL','DECLARED_TOOL_FINALIZATION'].includes(proof.rawExecution?.completion?.kind):proof.rawExecution?.completion?.kind==='NATURAL','INPUT_RESUME_QUALIFIED_COMPLETION_REQUIRED');
  return {status:proof.exitCode,stdout:fs.readFileSync(path.join(out,name+'.stdout.log'),'utf8')};
 };
 const build=await call('compile',process.env.ComSpec??'C:/Windows/System32/cmd.exe',['/d','/c',path.join(out,'build.cmd')],60000);assert.equal(build.status,0,'INPUT_RESUME_COMPILE_FAILED');
 const run=await call('execute',exe,selectedCase?[selectedCase]:[],15000);assert(run.status===0||run.status===1||run.status===2,'INPUT_RESUME_UNEXPECTED_EXIT');
 let behavior=null;if(run.status!==2)behavior=JSON.parse(run.stdout);
 const sourceDrift=[...bytes].filter(([relative,value])=>sha(fs.readFileSync(observedPath(relative)))!==sha(value)).map(([relative])=>relative);
 const result={schemaVersion:1,passed:run.status===0&&sourceDrift.length===0,status:sourceDrift.length?'FAIL_INPUT_RESUME_SOURCE_DRIFT':run.status===0?'PASS_SCOPED_INPUT_RESUME':'FAIL_SCOPED_INPUT_RESUME',behavior,processes,sourceDrift,elapsedMs:performance.now()-started,selectedCase:selectedCase??null,defaultPowerProfilesExecuted:false,fullParentExecuted:false,actualOsOrDevices:false};
 save('RESULT.json',result);console.log(JSON.stringify({status:result.status,cases:behavior?.cases.length,assertions:behavior?.assertions,failures:behavior?.failures}));
 if(run.status!==0||sourceDrift.length)process.exitCode=run.status||1;
 return result;
}
