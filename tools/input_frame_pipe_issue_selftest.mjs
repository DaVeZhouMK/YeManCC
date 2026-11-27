// Candidate for the ORIGINAL tools/input_frame_pipe_issue_selftest.mjs.
// No new canonical consumer. Preparation path does not import a subprocess API.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {fileURLToPath,pathToFileURL} from 'node:url';
const here=path.dirname(fileURLToPath(import.meta.url));
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const options={repo:path.resolve(here,'..'),caseGroup:'all',cases:[],prepareOnly:false};
const aliases=new Map([['--repo','repo'],['--Repo','repo'],['--MainFile','mainFile'],['--HeaderRoot','headerRoot'],['--out','out'],['--case-group','caseGroup'],['--real-pipe-case','realPipeCase'],['--real-pipe-variant','realPipeVariant']]);
const seen=new Set();
for(let i=2;i<process.argv.length;i++){
 const arg=process.argv[i];
 if(arg==='--case'){const value=process.argv[++i];assert(value&&!value.startsWith('--')&&!options.cases.includes(value),'empty/duplicate original case');options.cases.push(value);continue;}
 if(arg==='--prepare-only'){assert(!seen.has(arg),'duplicate prepare-only');seen.add(arg);options.prepareOnly=true;continue;}
 assert(aliases.has(arg),'unknown option '+arg);const key=aliases.get(arg),value=process.argv[++i];
 assert(value&&!value.startsWith('--')&&!seen.has(key),'empty/duplicate '+arg);seen.add(key);options[key]=value;
}
if(seen.has('realPipeVariant'))assert(seen.has('realPipeCase'),'--real-pipe-variant requires --real-pipe-case');
if(seen.has('realPipeCase'))assert(!seen.has('caseGroup')&&!options.cases.length,'--real-pipe-case is exclusive with --case-group/--case');
if(options.cases.length){if(!seen.has('caseGroup'))options.caseGroup='original';assert.equal(options.caseGroup,'original','--case requires original group');}
assert(['all','original','pipe-cancellation','request-shared-power','scheduling-delay'].includes(options.caseGroup),'unknown/empty group');
if(seen.has('repo')||seen.has('mainFile')||seen.has('headerRoot'))for(const key of ['repo','mainFile','headerRoot'])assert(seen.has(key),'paired explicit input missing '+key);
const repo=path.resolve(options.repo),mainFile=path.resolve(options.mainFile??path.join(repo,'native/main.cpp'));
const headerRoot=path.resolve(options.headerRoot??path.join(repo,'native'));
for(const [key,value]of Object.entries({repo,mainFile,headerRoot}))assert(path.isAbsolute(value),'absolute '+key);
if(seen.has('realPipeCase')){
 const {prepareRealPipe}=await import('./real_pipe_prepare.mjs');
 prepareRealPipe({options,seen,repo,mainFile,headerRoot,runnerPath:fileURLToPath(import.meta.url)});
}else{
const output=path.resolve(options.out??path.join(repo,'../../../Build/Validation/VirtualInput-Submission-R113-20261016/pipe-issue-formal'));
// Preserve the original run layout, with exclusive names and immutable output.
const run=options.prepareOnly?output:path.join(output,'runs',new Date().toISOString().replace(/[:.]/g,'-'));
assert(!fs.existsSync(run),'fresh run required');fs.mkdirSync(run,{recursive:true});
const source=path.join(run,'source');fs.mkdirSync(source);
const write=(relative,bytes)=>{const file=path.join(run,relative);fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,bytes,{flag:'wx'});return {path:file,bytes:Buffer.byteLength(bytes),sha256:sha(bytes)};};
const inputBytes=new Map();
function pin(file,role){file=path.resolve(file);if(!inputBytes.has(file))inputBytes.set(file,{bytes:fs.readFileSync(file),roles:[]});const row=inputBytes.get(file);if(!row.roles.includes(role))row.roles.push(role);return row.bytes;}
const importRepo=async relative=>{pin(path.join(repo,relative),'preparation-helper');return import(pathToFileURL(path.join(repo,relative)).href);};
const {snapshotVirtualControllerSource}=await importRepo('tools/virtual_controller_runtime_source.mjs');
const {extract:originalExtract}=await importRepo('tools/fixtures/legion_motion_consumer/extract_main.mjs');
const {controllerInputFixtureInputs,snapshotControllerInputFixture}=await importRepo('tools/lib/hc_controller_input_fixture.mjs');
const {calibrationToolchainPreamble}=await importRepo('tools/fixtures/hc_reviewed_calibration/toolchain.mjs');
const actualMain=pin(mainFile,'actual-main'),mainBefore=sha(actualMain);
const mappedRuntime=[];
const runtime=snapshotVirtualControllerSource(mainFile,path.join(run,'runtime-source'),{readFile:file=>{
 const origin=path.resolve(file)===mainFile?mainFile:[path.join(headerRoot,path.basename(file)),path.join(repo,'native',path.basename(file))].find(fs.existsSync);
 assert(origin,'runtime header missing '+file);mappedRuntime.push({logical:file,actual:path.resolve(origin)});return pin(origin,'actual-runtime-expansion');
}});
const main=runtime.source,bindings=[];
function extract(kind,name){const entry=originalExtract(main,name,kind);bindings.push({...entry,code:undefined,source:runtime.manifest.expandedSnapshot});return entry.code;}
function block(text,label,a,b,last=false){const hits=needle=>[...new Set([needle,needle.replaceAll('\r\n','\n').replaceAll('\n','\r\n')])].map(value=>({value,index:last?text.lastIndexOf(value):text.indexOf(value)})).filter(row=>row.index>=0);const starts=hits(a);assert.equal(starts.length,1,label+' start');const i=starts[0].index;const ends=[...new Set([b,b.replaceAll('\r\n','\n').replaceAll('\n','\r\n')])].map(value=>text.indexOf(value,i+starts[0].value.length)).filter(index=>index>i);assert.equal(ends.length,1,label+' end');const code=text.slice(i,ends[0]);bindings.push({symbol:label,kind:'actual-block',sha256:sha(code),bytes:Buffer.byteLength(code),code});return code;}
const submit=extract('function','inputHostSubmitPad'),request=extract('function','inputHostRequestPreparedLocked');
write('actual-inputHostRequestPreparedLocked.cpp',request);
const latePre=block(request,'actual r6 post-tuple/identity pre-business block',
 '        if(t_inputHostRequestPower&&!t_inputHostRequestPower->current()){\n            // Exact tuple/identity is proven above.',
 '        if (helloCommand) {');
const lateTail=block(request,'actual DOM final-current and wire sequence block',
 '    if(!inputHostRequestPowerReceiptCurrent(command,responseOut,true,localFrameSubmission)){',
 '    if (std::strcmp(command, "SUBMIT_FRAME") == 0) {',true);
const legacyCall=block(submit,'actual legacy SUBMIT_FRAME request call',
 '                return inputHostRequest','            };');
 const backendHold=block(submit,'actual backend fault hold consumer',
 '                        g_inputHostFaultHoldsStart.store(true, std::memory_order_release);',
 '                    }\n                }\n            }');
 const localFrameCall=request.includes('InputFrameSubmission* localFrameSubmission = nullptr');
 const parts={
 LOCAL_CONTEXT_CONDITION:block(submit,'actual context classification expression','            } else if(frameSubmission.rejected',') {\n                // Context retirement').replace('            } else if(',''),
 TRANSPORT_UNUSABLE:block(submit,'actual unusable transport condition','                const bool transportUnusable =','                if (transportUnusable)'),
 TRANSPORT_FAULT:block(submit,'actual unusable transport handling','                    // R103: transport health cannot erase an accepted PREPARE.','                } else {\n                    g_inputHostLifecycle = "faulted-no-write";'),
 SUBMISSION_RESULT:block(submit,'actual final hostSubmission output','    result["hostSubmission"] =','    if(frameSubmission.rejected ||'),
 LEGACY_CALL:legacyCall, BACKEND_HOLD:backendHold, LOCAL_FRAME_ARGUMENT:localFrameCall?'&frameSubmission':'nullptr',
 COMMAND_DEADLINES:['inputHostStartProtectionNow','inputHostRequestIsLifecycleCommand','inputHostRequestHasExecutionDeadline'].map(n=>extract('function',n)).join('\n'),
 LEGION_BINDING:extract('function','legionMotionModelBinding'),LEGION_CURRENT:extract('function','legionMotionSubmissionCurrent'),
 SNAPSHOT:extract('struct','InputCaptureGamepadSnapshot'),
 REQUEST_TLS:['t_inputHostRequestPower','t_inputHostRequestHardwareRequired'].map(name=>{const rows=[...main.matchAll(new RegExp('^static thread_local[^\\r\\n]*\\b'+name+'[^\\r\\n]*;\\r?$','gm'))];assert.equal(rows.length,1,name);bindings.push({symbol:name,kind:'declaration',sha256:sha(rows[0][0])});return rows[0][0];}).join('\n'),
 SCOPE:extract('struct','InputFrameSubmission')+'\nstatic thread_local InputFrameSubmission* t_inputFrameSubmission=nullptr;\n'+extract('struct','InputFrameSubmissionScope')+'\nstatic thread_local bool t_inputFrameTransportGuard=false;\n'+extract('struct','InputFrameTransportScope')+'\n'+extract('struct','GameFeedbackFrameDispatchScope')+'\n'+extract('struct','InputHostRequestPowerScope'),
 SOURCE_ELIGIBLE:extract('function','msiDinputMainSourceEligible'),MSI_NEUTRAL:extract('function','msiControllerNeutralCleanupAllowed'),
 SOURCE_ADMISSION:extract('function','inputHostPhysicalPublicationSourceAdmitted'),
 READ_PROGRESS:(()=>{const rows=[...main.matchAll(/^static ymcc::input::frameSubmission::ReadProgress g_inputHostReadProgress;\r?$/gm)];assert.equal(rows.length,1,'actual read-progress declaration');bindings.push({symbol:'g_inputHostReadProgress',kind:'declaration',sha256:sha(rows[0][0])});return rows[0][0];})(),
 ADAPTERS:['inputCaptureBindStandardInput','inputHostFrameSubmissionSample','inputHostFrameSubmissionCurrent'].map(n=>extract('function',n)).join('\n'),
 NOTE:extract('function','inputHostFrameSubmissionNote'),
 REQUEST_GUARD:['inputHostWindowsMotionSubmissionCurrent','inputHostFrameSharedPowerCurrent','inputHostFrameSubmissionMayIssue','inputHostFrameSubmissionTransportRejected','inputHostFrameSubmissionRequestCurrent','inputHostRequestNeedsHardwarePower','inputHostRequestNeedsPowerScope','inputHostReceiptPassedSequenceGate','inputHostRequestPowerReceiptCurrent'].map(n=>extract('function',n)).join('\n'),
 FRAME:extract('struct','FocusFrameContent')+'\n'+extract('function','focusIsolationNeutralContent')+'\n'+extract('function','focusIsolationAssembleFrame'),
 PIPE_CLOSE:extract('function','inputHostPipeCloseOwned'),
 RECREATE_WRITE:extract('struct','InputHostRecreateWrite'),
 PIPE_WRAPPERS:['inputHostPipeIo','inputHostWaitClientLocked'].map(n=>extract('function',n)).join('\n'),
 REQUEST_POWER_ADMISSION:block(request,'actual payload-aware request power admission','    const bool hardwarePowerRequired=','    const bool deferredVisibilityCommand='),
 PRE_ENVELOPE:block(request,'actual request transport scope and before-envelope admission','    const bool deferredVisibilityCommand','    // R2：envelope'),
 FORGET_TARGET_INPUT:block(request,'actual input retirement and compact frame unbinding','    const auto forgetTargetInput=','    std::string requestText;'),
 TRANSPORT:block(request,'actual Connect through Read plus context/failure dispatch guards','    DWORD lastError = ERROR_SUCCESS;','    if (!read) {'),
 FAILED_CONNECT_CLEANUP:block(request,'actual transport failure resource retirement and flags','        if (pipe) {','        // Normal-use incidents'),
 LATE_ACK_TAIL:lateTail,
 LATE_ACK_CONSUMER:'static unsigned fixtureBusinessConsumers=0;\nstatic bool fixtureValidatedReceiptEntry(const char* command,const json& response,bool advanceSequence,unsigned long long sequenceValue,json* responseOut,InputFrameSubmission* localFrameSubmission=nullptr){\n (void)localFrameSubmission;\n'+latePre+'\n ++fixtureBusinessConsumers;return true;\n}',
 SETUP:block(submit,'actual sample/ticket/public source scope binding','    // Bind normal BUS input','    // OFF revokes normal BUS'),
 POWER:block(submit,'actual initial frame power admission','    const bool powerAdmitted =','    if (!powerAdmitted)'),
 NORMALIZE:block(submit,'actual normalization/neutral admission with original scope acquisition','        frameSubmission.powerScope=g_inputHostInputPowerCache.acquire(','        suppressPublication ='),
 SUPPRESS:block(submit,'actual source suppression','        suppressPublication =','        inputHostRecoveryHealthObserveLocked'),
 ASSEMBLY:block(submit,'actual full neutral construction','    FocusFrameContent effectiveContent =','    // B1：hash / motionProof'),
 HOST_READY:block(submit,'actual final Host ready','        const bool hostFrameReady =','        const ymcc::screenpads::NativeTransportObservation'),
 NOFAULT:block(submit,'actual context rejection response','                // Context retirement is not a Host fault.','            } else {\n                // 跳过原因计数'),
};
const fixtureDirectory=path.join(here,'fixtures/input_frame_pipe_issue');
const templateFile=path.join(fixtureDirectory,'fixture.cpp');let fixture=pin(templateFile,'original-paired-template').toString('utf8');
const controllerFixture=snapshotControllerInputFixture(repo,path.join(run,'controller-input'));fixture=controllerFixture.preamble+fixture;
for(const [key,value]of Object.entries(parts)){assert.equal(fixture.split('@@'+key+'@@').length,2,key);fixture=fixture.replace('@@'+key+'@@',()=>value);}assert(!/@@\w+@@/.test(fixture));
write('fixture.cpp',fixture);write('request_cases.inc',pin(path.join(fixtureDirectory,'request_cases.inc'),'parameterized-original-cases'));
const roots=[headerRoot,path.join(repo,'native'),path.join(repo,'capabilities'),path.join(repo,'deps/json')],headers=new Map();
function header(file){file=path.resolve(file);const name=path.basename(file);if(headers.has(name)){assert.equal(headers.get(name).origin,file,'header basename collision '+name);return;}const bytes=pin(file,'quoted-header-closure');headers.set(name,{origin:file,bytes});for(const match of bytes.toString('utf8').matchAll(/^\s*#\s*include\s*"([^"\r\n]+)"/gm)){const resolved=[path.resolve(path.dirname(file),match[1]),...roots.map(root=>path.resolve(root,match[1]))].find(fs.existsSync);assert(resolved,'unresolved quoted header '+match[1]);header(resolved);}}
for(const name of ['input_frame_submission.h','touch_haptic_dispatch.h','game_feedback_receipt.h','windows_motion_submission.h','legion_motion_source.h','legion_motion_message_clock.h','virtual_output_status.h','input_host_pipe_transport.h','input_host_shared_power.h','input_host_compact_frame_reply.h','sleep_power_remote_contract.h','msi_virtual_report_diagnostic.h','virtual_recovery_intent.h','virtual_start_protection.h','HcControllerMotionCapabilityCatalog.h','json.hpp']){const found=roots.map(root=>path.join(root,name)).find(fs.existsSync);assert(found,'required header '+name);header(found);}
for(const relative of controllerInputFixtureInputs)pin(path.join(repo,relative),'controller-fixture-input');
for(const [name,row]of headers)write('source/'+name,row.bytes);
write('source/windows.h',pin(path.join(fixtureDirectory,'windows.h'),'fake-win32-leaves'));
write('snapshot/tools/input_frame_pipe_issue_selftest.mjs',pin(fileURLToPath(import.meta.url),'actual-paired-runner'));
const vc=process.env.YMCC_VCVARS64??'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
const recipes=[];
for(const profile of ['Release','Checked']){
 const recipeDir=path.join(run,profile);fs.mkdirSync(recipeDir);const flags=profile==='Release'?'/O2 /MT /DNDEBUG':'/Od /MTd /RTC1 /Zi /D_DEBUG';
 const recipe=calibrationToolchainPreamble(vc)+'set VSLANG=1033\r\ncl /nologo /std:c++20 /utf-8 /EHsc /W4 /WX /permissive- /sdl '+flags+' /I"'+source+'" "'+path.join(run,'fixture.cpp')+'" /Fo"'+path.join(recipeDir,'fixture.obj')+'" /Fe"'+path.join(recipeDir,'fixture.exe')+'" /Fd"'+path.join(recipeDir,'fixture.pdb')+'" /sourceDependencies "'+path.join(recipeDir,'dependencies.json')+'"\r\nexit /b %errorlevel%\r\n';
 recipes.push({profile,directory:recipeDir,...write(profile+'/build.cmd',recipe)});
}
const prepared={status:'PREPARED_NOT_COMPILED_NOT_EXECUTED',originalEntry:'tools/input_frame_pipe_issue_selftest.mjs',repo,mainFile,headerRoot,caseGroup:options.caseGroup,selectedCases:options.cases,mainSha256:mainBefore,runtimeExpansion:runtime.manifest,mappedRuntime,bindings:bindings.map(({code,...row})=>row),parts:Object.fromEntries(Object.entries(parts).map(([name,code])=>[name,{bytes:Buffer.byteLength(code),sha256:sha(code)}])),headers:[...headers].map(([name,row])=>({name,origin:row.origin,sha256:sha(row.bytes)})),inputPins:[...inputBytes].map(([file,row])=>({path:file,bytes:row.bytes.length,sha256:sha(row.bytes),roles:row.roles})),recipes,actualPreparedRecipeCount:recipes.length,compiled:false,executed:false,fullRequestExecuted:false,fullTupleParserExecuted:false,scope:'original paired frame/pipe fragments plus actual r6 helper/pre-business/tail blocks; real C++ Runtime/Scope/Registration with controlled Win32/Reader leaves',limits:'Request envelope/tuple parsing and business endpoints excluded; post-validation entry requires upstream tuple proof; no Native producer/real devices/managed Host/Steam acceptance'};
for(const [file,row]of inputBytes)assert.equal(sha(fs.readFileSync(file)),sha(row.bytes),'input changed during preparation '+file);
write('PREPARED.json',JSON.stringify(prepared,null,2)+'\n');
console.log(JSON.stringify({status:prepared.status,run,mainSha256:mainBefore,preparedOnly:options.prepareOnly,compilerInvocations:0,actualPassed:0}));
if(!options.prepareOnly){
 // Same original executable/profile mechanism; this branch is not used by this task.
 const {spawnSync}=await import('node:child_process');
 const result={run,caseGroup:options.caseGroup,profiles:{},sourceChanged:[]};
 for(const recipe of recipes){
  const build=spawnSync('cmd.exe',['/d','/c',recipe.path],{cwd:recipe.directory,encoding:'utf8',windowsHide:true,timeout:60000});
  write(recipe.profile+'/compile.txt',(build.stdout??'')+'\n'+(build.stderr??''));if(build.status!==0){result.profiles[recipe.profile]={buildPassed:false};break;}
  const execution=spawnSync(path.join(recipe.directory,'fixture.exe'),['--case-group',options.caseGroup,...options.cases.flatMap(name=>['--case',name])],{cwd:recipe.directory,encoding:'utf8',windowsHide:true,timeout:15000});
  write(recipe.profile+'/results.json',execution.stdout??'');write(recipe.profile+'/stderr.txt',execution.stderr??'');
  let report;try{report=JSON.parse(execution.stdout);}catch{result.profiles[recipe.profile]={buildPassed:true,executePassed:false};break;}
  const names=report.cases?.map(row=>row.name)??[];
  const exactCases=!options.cases.length||(names.length===options.cases.length&&options.cases.every(name=>names.includes(name)));
  const selected=exactCases&&report.caseGroup===options.caseGroup&&names.length>0&&new Set(names).size===names.length&&names.length===report.tests&&names.every(name=>options.caseGroup==='all'||(options.caseGroup==='original'?!name.startsWith('pipe-cancellation/')&&!name.startsWith('request-shared-power/'):name.startsWith(options.caseGroup+'/')));
  result.profiles[recipe.profile]={buildPassed:true,passed:execution.status===0&&report.passed===true&&selected,selectionMatches:selected,selectedCases:names,tests:report.tests,assertions:report.assertions,failures:report.failures};
  if(!result.profiles[recipe.profile].passed)break;
 }
 result.sourceChanged=[...inputBytes].filter(([file,row])=>sha(fs.readFileSync(file))!==sha(row.bytes)).map(([file])=>file);
 result.expandedSourceStable=!mappedRuntime.some(row=>result.sourceChanged.includes(row.actual));
 write('RESULT.json',JSON.stringify(result,null,2)+'\n');fs.writeFileSync(path.join(output,'LATEST.json'),JSON.stringify(result,null,2)+'\n');
 process.exitCode=Object.keys(result.profiles).length===recipes.length&&Object.values(result.profiles).every(row=>row.passed)&&result.sourceChanged.length===0&&result.expandedSourceStable?0:1;
}

} // real-pipe manual preparation is exclusive with all original preparation/execution.
