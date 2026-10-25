import fs from 'node:fs';import path from 'node:path';import {spawnSync} from 'node:child_process';import {fileURLToPath} from 'node:url';
import {extract} from '../legion_motion_consumer/extract_main.mjs';
import {closeBrace} from '../hc_remaining_model_capabilities/expectations.mjs';
import {calibrationToolchainPreamble} from '../hc_reviewed_calibration/toolchain.mjs';
import {FIXTURE,HEADERS,REPO,WORKSPACE,need,inside,hash as sha,read,write,loadSourceSet,verifyManaged,once,digest} from './contract.mjs';
import {prepareCombinedFixture} from './prepare-combined-fixture.mjs';
import {SOURCE_FAULTS} from './faults.mjs';
import {XBOX_NATIVE_FAULTS,steamNativeFaultsForCurrent} from './xbox-faults.mjs';
import {xboxCombinedTests} from './prepare-xbox-fixture.mjs';
import {recordNativeCompile} from '../controller_game_motor_pipe/native-stage-evidence.mjs';
export function generateCombined({caseRoot,out,sourceSetFile,overrides={}}){
 need(inside(path.join(WORKSPACE,'Build'),caseRoot)&&inside(caseRoot,out)&&caseRoot!==out,'OWNED_CASE_GENERATION_REQUIRED');
 need(!fs.existsSync(out),'FRESH_GENERATED_CASE_REQUIRED');fs.mkdirSync(out,{recursive:true});
 const frozen=loadSourceSet(sourceSetFile,caseRoot);
 const sourceSet={...frozen,resolve(relative){const p=overrides[relative];if(p){need(inside(caseRoot,p),'PRIVATE_FAULT_OUTSIDE_CASE');return p;}return frozen.resolve(relative);}};
 const sourceFile=sourceSet.resolve('native/main.cpp');
 const generated=prepareCombinedFixture({sourceFile,out:path.join(out,'generated'),sourceSet});
 const inc=path.join(out,'source');fs.mkdirSync(inc);
 for(const relative of [...HEADERS,'deps/json/json.hpp'])fs.copyFileSync(sourceSet.resolve(relative),path.join(inc,path.basename(relative)));
const source=fs.readFileSync(sourceFile,'utf8'),bindings=[];const fn=n=>{const b=extract(source,n);bindings.push(b);return b.code;},type=n=>{const b=extract(source,n,'struct');bindings.push(b);return b.code;};
const constants=['kInputHostProtocolConfigHash','kInputHostTargetId','kInputHostOwnerIdentity','kInputHostLockedCoreSha256','kInputHostLockedCoreMvid'].map(name=>{const rows=[...source.matchAll(new RegExp('^static constexpr const char\\* '+name+'\\s*=[^;]+;','gm'))];if(rows.length!==1)throw Error('Native constant '+name);return rows[0][0];}).join('\n');
const ws=source.indexOf('static std::string W2U(const wchar_t*'),we=source.indexOf('\n}',ws)+2,wide=source.match(/^static std::string W2U\(const std::wstring& w\)[^\r\n]+/m);if(ws<0||we<ws||!wide)throw Error('Unicode body');const start=source.indexOf('namespace c7aReceiptFast {'),end=closeBrace(source,source.indexOf('{',start));
const requestDeclarations=[...source.matchAll(/^static bool inputHostRequestLocked\([^{};]+;/gm)].filter(r=>/bool\s+advanceSequence\s*=\s*true/.test(r[0]));
need(requestDeclarations.length===1,'ACTUAL_REQUEST_FORWARD_DECLARATION_MISSING_OR_AMBIGUOUS');
bindings.push({name:'inputHostRequestLocked actual forward declaration',code:requestDeclarations[0][0],sha256:digest(requestDeclarations[0][0])});
const declaration=(name,pattern)=>{const match=[...source.matchAll(pattern)];need(match.length===1,'ACTUAL_PERSONA_STATE_DECLARATION:'+name);bindings.push({name,code:match[0][0],sha256:digest(match[0][0])});return match[0][0];};
const persona=[
 declaration('g_inputHostPersonaMx',/^static std::mutex g_inputHostPersonaMx;\r?$/gm),
 declaration('g_inputHostBoundPersona',/^static std::string g_inputHostBoundPersona[^;]*;\r?$/gm),
 declaration('g_inputHostRequestedPersonaMatchesBound',/^static std::atomic<bool> g_inputHostRequestedPersonaMatchesBound[^;]*;\r?$/gm),
 fn('inputHostRefreshPersonaMatchLocked'),fn('inputHostSetBoundPersona'),fn('inputHostClearBoundPersona'),
 'static void fixtureSetRequestedPersona(const std::string& persona){std::lock_guard<std::mutex> lock(g_inputHostPersonaMx);g_realStickTest.virtualTargetPersona=persona;inputHostRefreshPersonaMatchLocked();}',
 'static void fixtureSetMatchingPersona(const std::string& persona){fixtureSetRequestedPersona(persona);inputHostSetBoundPersona(persona);}'
].join('\n');
const oldAdmission=fs.readFileSync(sourceSet.resolve(FIXTURE+'/FrameAdmissionControls.cpp'),'utf8');
const oldAdmissionWithSetup='#define wmain FixtureOriginalAdmissionMain\n'+oldAdmission+'\n#undef wmain\nint wmain(int argc,wchar_t**argv){fixtureSetMatchingPersona("dualsense-edge");return FixtureOriginalAdmissionMain(argc,argv);}'
const personaControls='#define wmain FixtureUnusedOriginalAdmissionMain\n'+oldAdmission+'\n#undef wmain\n'+fs.readFileSync(sourceSet.resolve(FIXTURE+'/PersonaAdmissionControls.cpp'),'utf8');
const diagnostics=[
 'using PhysicalMemory::nativeGameFeedbackDiagnosticsSnapshot;',
 fn('inputHostConfiguredPersona'),
 fn('inputHostPersona'),requestDeclarations[0][0],fn('inputHostRequestLocked'),fn('inputHostLogSubmitStats')
].join('\n');
const steamProtocolControls='#define wmain FixtureUnusedSonyCombinedMain\n'+fs.readFileSync(sourceSet.resolve(FIXTURE+'/CombinedMotorTests.cpp'),'utf8')+'\n#undef wmain\n'+fs.readFileSync(sourceSet.resolve(FIXTURE+'/SteamProtocolControls.cpp'),'utf8');
const steamEntry=fs.readFileSync(sourceSet.resolve(FIXTURE+'/SteamCombinedEntry.cpp'),'utf8').replace('#include "CombinedMotorTests.cpp"',()=>fs.readFileSync(sourceSet.resolve(FIXTURE+'/CombinedMotorTests.cpp'),'utf8'));
const xboxProtocolControls='#define wmain FixtureUnusedSonyXboxProtocolMain\n'+fs.readFileSync(sourceSet.resolve(FIXTURE+'/CombinedMotorTests.cpp'),'utf8')+'\n#undef wmain\n#define wmain FixtureUnusedSteamProtocolMain\n'+fs.readFileSync(sourceSet.resolve(FIXTURE+'/SteamProtocolControls.cpp'),'utf8')+'\n#undef wmain\n'+fs.readFileSync(sourceSet.resolve(FIXTURE+'/XboxProtocolControls.cpp'),'utf8');
const replacements={PERSONA_MATCH:persona,DIAGNOSTIC_CONSUMER:diagnostics,PHYSICAL_MEMORY:fs.readFileSync(generated.physicalMemory,'utf8'),COMBINED_TESTS:'#if defined(YMCC_XBOX_PROTOCOL_CONTROLS)\n'+xboxProtocolControls+'\n#elif defined(YMCC_STEAM_PROTOCOL_CONTROLS)\n'+steamProtocolControls+'\n#elif defined(YMCC_PERSONA_ADMISSION_CONTROLS)\n'+personaControls+'\n#elif defined(YMCC_FRAME_ADMISSION_CONTROLS)\n'+oldAdmissionWithSetup+'\n#else\n'+xboxCombinedTests(sourceSet)+'\n#endif\n',FRAME_ADMISSION:['inputHostFrameSubmissionCurrent','inputHostFrameSubmissionMayIssue','inputHostFrameSubmissionTransportRejected','inputHostFrameSubmissionRequestCurrent'].map(fn).join('\n'),GAME_MOTOR:[type('InputHostGameMotorDomKeys'),fn('inputHostGameMotorFromDom'),'using PhysicalMemory::inputHostConsumeGameMotorReplyLocked;',fn('inputHostObserveGameMotorBindingLocked')].join('\n'),FRAME_SCOPE:type('InputFrameTransportScope'),CONSTANTS:constants,UNICODE:source.slice(ws,we)+'\n'+wide[0]+'\n'+fn('U2W'),NEW_ID:fn('inputHostNewId'),HASH:['inputHostCryptoProvider','inputHostSha256','inputHostCanonicalFrameHash'].map(fn).join('\n'),ENVELOPE:fn('inputHostEnvelopeLocked'),PIPE:['inputHostPipeCloseOwned','inputHostCreateServerPipeLocked','inputHostPipeIo','inputHostWaitClientLocked'].map(fn).join('\n'),FAST_RECEIPT:type('InputHostFastReceipt'),SCANNER:source.slice(start,end+1),AFTER_FRAME:fn('inputHostAfterSubmitFrameAccepted'),RELEASE_TIMING_LOG:fn('inputHostReleaseTimingReceiptLog'),COMMAND_RESULT_LOG:fn('inputHostCommandResultLog'),REQUEST:fn('inputHostRequestPreparedLocked')};
let code=fs.readFileSync(generated.template,'utf8');for(const[name,text]of Object.entries(replacements)){const marker='@@'+name+'@@';if(code.split(marker).length!==2)throw Error('Combined marker '+name);code=code.replace(marker,()=>text);}if(/@@\w+@@/.test(code))throw Error('Unfilled markers');fs.writeFileSync(inc+'/fixture.cpp',code);const inputs=[...new Set([...sourceSet.sources.map(r=>r.snapshot),sourceFile,...Object.values(overrides),generated.physicalMemory,generated.template,generated.bridge,sourceSetFile])].map(file=>({file,sha256:sha(file)}));fs.writeFileSync(out+'/source-bindings.json',JSON.stringify({inputs,bindings:bindings.map(({code,...r})=>r),wholeNativeSourceSha256:sha(sourceFile),mode:sourceSet.mode,sharedFrameAndInbox:true},null,2)+'\n');

 return{out,inc,sourceFile,sourceSet,inputs,generated,fixture:path.join(inc,'fixture.cpp')};
}
export function runCombined({caseRoot,out,sourceSetFile,managedManifest,modes=['Release','Checked'],overrides={},expectedAssertion=null,compileOnly=false,frameControls=false,personaControlsOnly=false,steamProtocolControlsOnly=false,xboxProtocolControlsOnly=false}){
 const actual=loadSourceSet(sourceSetFile,caseRoot),steamFaults=steamNativeFaultsForCurrent(fs.readFileSync(actual.resolve('native/main.cpp'),'utf8'),fs.readFileSync(actual.resolve('native/game_feedback_receipt.h'),'utf8'));
 const fault=expectedAssertion?[...SOURCE_FAULTS,...steamFaults,...XBOX_NATIVE_FAULTS].find(f=>f.expected===expectedAssertion):null;
 if(expectedAssertion)need(fault,'UNKNOWN_NEGATIVE_ASSERTION');
 const overridden=Object.keys(overrides);
 if(overridden.length){
  need(fault&&overridden.length===1,'POSITIVE_CANDIDATE_OVERRIDE_FORBIDDEN');
  const relative=fault.kind==='source'?'native/main.cpp':fault.kind==='receipt'?'native/game_feedback_receipt.h':'native/game_feedback_dispatch.h';
  need(overridden[0]===relative,'PRIVATE_FAULT_WRONG_SOURCE');
  const original=loadSourceSet(sourceSetFile,caseRoot).resolve(relative);
  need(fs.readFileSync(overrides[relative],'utf8')===once(fs.readFileSync(original,'utf8'),fault.before,fault.after,fault.id),'PRIVATE_FAULT_NOT_EXACT_ORIGINAL_MUTATION');
 }else need(!expectedAssertion,'NEGATIVE_REQUIRES_DECLARED_PRIVATE_SOURCE_FAULT');
 need([frameControls,personaControlsOnly,steamProtocolControlsOnly,xboxProtocolControlsOnly].filter(Boolean).length<=1,'ONE_ADMISSION_CONTROL_GROUP_ONLY');
 need(!steamProtocolControlsOnly||!expectedAssertion||steamFaults.includes(fault),'STEAM_PROTOCOL_PRIVATE_FAULT_SCOPE');
 need(!(frameControls||personaControlsOnly)||(!expectedAssertion&&Object.keys(overrides).length===0),'ADMISSION_CONTROLS_REQUIRE_UNMUTATED_CURRENT_SOURCE');
 need(!xboxProtocolControlsOnly||!expectedAssertion||XBOX_NATIVE_FAULTS.includes(fault),'XBOX_PROTOCOL_PRIVATE_FAULT_SCOPE');
 const fixture=generateCombined({caseRoot,out,sourceSetFile,overrides});
 const managed=compileOnly||frameControls||personaControlsOnly||steamProtocolControlsOnly||xboxProtocolControlsOnly?null:verifyManaged(managedManifest,caseRoot);
 const {inc,sourceSet,inputs}=fixture,configurations=[];
 const dotnetRoot=read(sourceSet.resolve('tools/ymcc_sim/global/inputhost-protocol/toolchain-inputs.json')).dotnetRoot;
 for(const mode of modes){
  need(['Release','Checked'].includes(mode),'UNKNOWN_NATIVE_CONFIGURATION');
  const dir=path.join(out,mode);fs.mkdirSync(dir);const batch=path.join(dir,'compile.cmd'),flags=(mode==='Release'?'/O2 /MT /DNDEBUG':'/Od /MTd /RTC1 /D_DEBUG')+(frameControls?' /DYMCC_FRAME_ADMISSION_CONTROLS=1':personaControlsOnly?' /DYMCC_PERSONA_ADMISSION_CONTROLS=1':steamProtocolControlsOnly?' /DYMCC_STEAM_PROTOCOL_CONTROLS=1':xboxProtocolControlsOnly?' /DYMCC_XBOX_PROTOCOL_CONTROLS=1':'');
  fs.writeFileSync(batch,calibrationToolchainPreamble('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')+'chcp 65001 >nul\r\ncl /nologo /std:c++20 /utf-8 /EHsc /W3 /WX /showIncludes /DFIXED_POLICY=1 '+flags+' /I"'+inc+'" "'+fixture.fixture+'" /Fo"'+path.join(dir,'fixture.obj')+'" /Fe"'+path.join(dir,'fixture.exe')+'" advapi32.lib ole32.lib /link /verbose:lib\r\nexit /b %errorlevel%\r\n');
  const compileStarted=Date.now();const build=spawnSync('cmd.exe',['/d','/c',batch],{cwd:dir,windowsHide:true,encoding:'utf8',timeout:60000,maxBuffer:8*1024*1024});
  const log=(build.stdout??'')+(build.stderr??'');fs.writeFileSync(path.join(dir,'compile.log'),log);
  recordNativeCompile({root:out,dir,mode,command:{exe:'cmd.exe',args:['/d','/c',batch]},process:build,startedAt:compileStarted});
  need(!build.error&&!build.signal&&build.status===0,'COMBINED_COMPILATION_INCOMPLETE:'+path.join(dir,'compile.log'));
  const includes=[...new Set([...log.matchAll(/^\s*(?:Note: including file:|\u6ce8\u610f:\s*\u5305\u542b\u6587\u4ef6:)\s*(.+?)\r?$/gm)].map(m=>path.resolve(m[1])))];
  const linkerLibraries=[...new Set([...log.matchAll(/\b[A-Z]:\\[^\r\n]*?\.lib\b/gi)].map(m=>path.resolve(m[0])))];
  need(includes.length>20&&linkerLibraries.length>0,'ACTUAL_COMPILER_INCLUDE_OR_LIBRARY_OBSERVATION_MISSING');
  if(compileOnly){configurations.push({mode,status:'COMPILE_ONLY_NOT_ACCEPTANCE',buildExit:0,includes,linkerLibraries,executableSha256:sha(path.join(dir,'fixture.exe'))});continue;}
  const run=path.join(dir,'run');fs.mkdirSync(run);
  const execution=spawnSync(path.join(dir,'fixture.exe'),(frameControls||personaControlsOnly||steamProtocolControlsOnly||xboxProtocolControlsOnly)?[run]:[path.join(dotnetRoot,'dotnet.exe'),managed.value.fixtureDll,run],{cwd:dir,windowsHide:true,encoding:'utf8',timeout:120000,maxBuffer:32*1024*1024});
  fs.writeFileSync(path.join(dir,'stdout.log'),execution.stdout??'');fs.writeFileSync(path.join(dir,'stderr.log'),execution.stderr??'');
  need(!execution.error&&!execution.signal,'COMBINED_EXECUTABLE_INCOMPLETE');
  let result;try{result=read(path.join(run,'result.json'));}catch{try{result=JSON.parse(execution.stdout);}catch{throw Error('NO_COMPLETE_COMBINED_CONSUMER_RECEIPT');}}
  if(expectedAssertion)need(execution.status===1&&result.status==='FAIL'&&String(result.error).includes(expectedAssertion),'ORIGINAL_COMPILED_SOURCE_FAULT_NOT_CAUGHT');
  else if(xboxProtocolControlsOnly)need(execution.status===0&&result.status==='PASS'&&result.passed===true&&result.cases.length===4&&result.additionalXboxProtocolControlsOnly===true&&result.managedHostStarts===0,'XBOX_PROTOCOL_CONTROLS_FAILED');
   else if(steamProtocolControlsOnly)need(execution.status===0&&result.status==='PASS'&&result.passed===true&&result.cases.length===4&&result.additionalSteamProtocolControlsOnly===true&&result.managedHostStarts===0,'STEAM_PROTOCOL_CONTROLS_FAILED');
  else if(personaControlsOnly)need(execution.status===0&&result.status==='PASS'&&result.passed===true&&result.cases.length===7&&result.additionalPersonaControlsOnly===true&&result.managedHostStarts===0,'ADDITIONAL_PERSONA_CONTROLS_FAILED');
  else if(frameControls)need(execution.status===0&&result.status==='PASS'&&result.passed===true&&result.cases.length===6&&result.additionalControlsOnly===true&&result.managedHostStarts===0,'ADDITIONAL_ADMISSION_CONTROLS_FAILED');
  else need(execution.status===0&&result.status==='PASS'&&result.passed===true&&result.cases.length===41&&result.checks===1130,'SONY13_354_STEAM12_327_XBOX16_448_AND_PERSONA_EXPECTATIONS_CHANGED');
  configurations.push({mode,status:expectedAssertion?'EXPECTED_SEMANTIC_REJECTION':'PASS',buildExit:0,exit:execution.status,includes,linkerLibraries,result,executableSha256:sha(path.join(dir,'fixture.exe'))});
  console.log(JSON.stringify({mode,status:configurations.at(-1).status,cases:result.cases?.length,checks:result.checks,error:result.error}));
 }
 for(const r of inputs)need(sha(r.file)===r.sha256,'GENERATION_INPUT_CHANGED:'+r.file);
 if(managed){verifyManaged(managedManifest,caseRoot);need(sha(managedManifest)===managed.manifestSha256,'MANAGED_MANIFEST_CHANGED');}
 const result={schemaVersion:1,status:compileOnly?'COMPILE_ONLY_NOT_ACCEPTANCE':'PASS',passed:!compileOnly,mode:sourceSet.mode,inputs,drift:[],configurations,sourceExactMain:fixture.sourceFile,sourceExactMainSha256:sha(fixture.sourceFile),managedManifest:managed?{file:managedManifest,sha256:managed.manifestSha256}:null,expectedSemanticRejection:expectedAssertion,additionalXboxProtocolControls:xboxProtocolControlsOnly,additionalSteamProtocolControls:steamProtocolControlsOnly,additionalPersonaAdmissionControls:personaControlsOnly,additionalFrameAdmissionControls:frameControls,original13Case354Executed:!compileOnly&&!frameControls&&!personaControlsOnly&&!steamProtocolControlsOnly&&!xboxProtocolControlsOnly,actualPrivateWindowsPipe:!compileOnly&&!frameControls&&!personaControlsOnly&&!steamProtocolControlsOnly&&!xboxProtocolControlsOnly,actualExistingSerialWorker:!compileOnly&&!frameControls&&!personaControlsOnly&&!steamProtocolControlsOnly&&!xboxProtocolControlsOnly,sameAdmittedSourceFrame:true,actualPhysicalWritersControlledIo:true,productMainExecuted:false,realSdkLoaded:false,realHardwareIo:false,physicalAcceptance:false,productionEdited:false};
 write(path.join(out,'result.json'),result);return result;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 try{
  const args=process.argv.slice(2),opts={};for(let i=0;i<args.length;i+=2){need(['--case-root','--out','--source-set','--managed-manifest','--modes','--overrides','--expect-rejection','--compile-only','--frame-controls','--persona-controls','--steam-protocol-controls','--xbox-protocol-controls'].includes(args[i])&&!Object.hasOwn(opts,args[i]),'INVALID_OR_DUPLICATE_ARGUMENT');need(args[i+1]!==undefined,'MISSING_ARGUMENT');opts[args[i]]=args[i+1];}
  const r=runCombined({caseRoot:opts['--case-root'],out:opts['--out'],sourceSetFile:opts['--source-set'],managedManifest:opts['--managed-manifest'],modes:(opts['--modes']??'Release,Checked').split(','),overrides:opts['--overrides']?read(opts['--overrides']):{},expectedAssertion:opts['--expect-rejection']??null,compileOnly:opts['--compile-only']==='true',frameControls:opts['--frame-controls']==='true',personaControlsOnly:opts['--persona-controls']==='true',steamProtocolControlsOnly:opts['--steam-protocol-controls']==='true',xboxProtocolControlsOnly:opts['--xbox-protocol-controls']==='true'});console.log(JSON.stringify({status:r.status,mode:r.mode}));
 }catch(error){console.error(error.stack??error.message);process.exitCode=2;}
}
