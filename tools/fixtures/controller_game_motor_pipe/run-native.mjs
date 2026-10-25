// Execute complete current native request code over real private Windows pipes.
// Managed counterpart runs actual source-extracted pipe loop + HostSession, inert SDK.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import assert from 'node:assert/strict';
import {extract} from '../legion_motion_consumer/extract_main.mjs';
import {closeBrace} from '../hc_remaining_model_capabilities/expectations.mjs';
import {calibrationToolchainPreamble} from '../hc_reviewed_calibration/toolchain.mjs';
import {REPO as repo,WORKSPACE,FIXTURE,NATIVE_HEADERS,NATIVE_SOURCES,inside,verifyManaged,write} from './contract.mjs';
import {noRevisionGuard,NATIVE_REVISION_FAULT} from './faults.mjs';
import {lifecycleContext} from './lifecycle-context.mjs';
import {targetReturnContext} from './target-return-context.mjs';
import {recordNativeCompile} from './native-stage-evidence.mjs';
const options={};for(let i=2;i<process.argv.length;i++){const key=process.argv[i];assert(['--out','--case-root','--managed-dll','--managed-manifest','--source','--dotnet','--compile-only','--expect-rejection','--revision-guard-fault'].includes(key)&&!Object.hasOwn(options,key),'invalid/duplicate option');options[key]=['--compile-only','--revision-guard-fault'].includes(key)?true:process.argv[++i];}
assert(options['--out']&&options['--case-root']);const caseRoot=path.resolve(options['--case-root']);assert(inside(path.join(WORKSPACE,'Build'),caseRoot));const root=path.resolve(options['--out']);assert(inside(caseRoot,root)&&root!==caseRoot);assert(!fs.existsSync(root),'Fresh output required');fs.mkdirSync(root,{recursive:true});
const sha=b=>crypto.createHash('sha256').update(b).digest('hex'),hash=file=>sha(fs.readFileSync(file));
const source=path.resolve(options['--source']||path.join(repo,'native/main.cpp'));
assert(path.resolve(source)===path.resolve(repo,'native/main.cpp')||inside(caseRoot,source),'Source must be current production main or an owned snapshot');
const headerNames=NATIVE_HEADERS.map(p=>path.basename(p));
const headerOriginal=name=>path.join(repo,NATIVE_HEADERS.find(p=>path.basename(p)===name));
let privateHeader=null;
if(options['--revision-guard-fault']){
 assert(options['--expect-rejection']===NATIVE_REVISION_FAULT.expectedAssertion,'Exact original assertion required');
 privateHeader=path.join(root,'private-source-fault','game_feedback_receipt.h');fs.mkdirSync(path.dirname(privateHeader));
 const original=headerOriginal('game_feedback_receipt.h');fs.writeFileSync(privateHeader,noRevisionGuard(fs.readFileSync(original,'utf8')),{flag:'wx'});
 write(path.join(root,'private-source-fault.json'),{...NATIVE_REVISION_FAULT,original,originalSha256:hash(original),privateHeader,privateSha256:hash(privateHeader),productionModified:false});
}else assert(!options['--expect-rejection'],'Only the declared private guard mutation can expect rejection');
const headerPath=name=>name==='game_feedback_receipt.h'&&privateHeader?privateHeader:headerOriginal(name);
const managedContract=options['--compile-only']?null:verifyManaged(options['--managed-manifest'],caseRoot);
if(managedContract)assert(path.resolve(options['--managed-dll'])===path.resolve(managedContract.value.fixtureDll));
const sources=[source,...NATIVE_SOURCES.filter(p=>p!=='native/main.cpp').map(p=>path.join(repo,p)),path.join(repo,FIXTURE,'contract.mjs'),path.join(repo,FIXTURE,'faults.mjs'),...(privateHeader?[privateHeader]:[])].map(p=>({path:p,sha256:hash(p)}));
const main=fs.readFileSync(source,'utf8'),bindings=[];
const lifecycle=lifecycleContext({sourceFile:source,resolveInput:r=>path.join(repo,r)});bindings.push(...lifecycle.bindings);
const targetReturn=targetReturnContext({sourceFile:source,resolveInput:r=>path.join(repo,r)});bindings.push(...targetReturn.bindings);
const fn=name=>{const b=extract(main,name);bindings.push(b);return b.code;};
const struct=name=>{const b=extract(main,name,'struct');bindings.push(b);return b.code;};
function section(marker,label){const start=main.indexOf(marker);assert(start>=0&&main.indexOf(marker,start+marker.length)<0,'source anchor '+label);const brace=main.indexOf('{',start),end=closeBrace(main,brace);const code=main.slice(start,end+1);bindings.push({name:label,sha256:sha(code),code});return code;}
const constants=['kInputHostProtocolConfigHash','kInputHostTargetId','kInputHostOwnerIdentity','kInputHostLockedCoreSha256','kInputHostLockedCoreMvid'].map(name=>{const matches=[...main.matchAll(new RegExp('^static constexpr const char\\* '+name+'\\s*=[^;]+;','gm'))];assert(matches.length===1,name);bindings.push({name,code:matches[0][0],sha256:sha(matches[0][0])});return matches[0][0];}).join('\n');
const unicodeStart=main.indexOf('static std::string W2U(const wchar_t*'),unicodeEnd=main.indexOf('\n}',unicodeStart)+2;assert(unicodeStart>0&&unicodeEnd>unicodeStart);const wide=main.match(/^static std::string W2U\(const std::wstring& w\)[^\r\n]+/m);assert(wide);
const unicode=main.slice(unicodeStart,unicodeEnd)+'\n'+wide[0]+'\n'+fn('U2W');bindings.push({name:'W2U overloads',code:unicode,sha256:sha(unicode)});
const declaration=(name,pattern)=>{const matches=[...main.matchAll(pattern)];assert(matches.length===1,'actual persona state '+name);bindings.push({name,code:matches[0][0],sha256:sha(matches[0][0])});return matches[0][0];};
const actualPersona=[
 declaration('g_inputHostPersonaMx',/^static std::mutex g_inputHostPersonaMx;\r?$/gm),
 declaration('g_inputHostBoundPersona',/^static std::string g_inputHostBoundPersona[^;]*;\r?$/gm),
 declaration('g_inputHostRequestedPersonaMatchesBound',/^static std::atomic<bool> g_inputHostRequestedPersonaMatchesBound[^;]*;\r?$/gm),
 fn('inputHostRefreshPersonaMatchLocked'),fn('inputHostSetBoundPersona'),fn('inputHostClearBoundPersona'),fn('inputHostConfiguredPersona'),
 'static void fixtureSetMatchingPersona(const std::string& persona){{std::lock_guard<std::mutex> lock(g_inputHostPersonaMx);g_realStickTest.virtualTargetPersona=persona;inputHostRefreshPersonaMatchLocked();}inputHostSetBoundPersona(persona);}'
].join('\n');
const replacements={GAME_REVOKE:fn('gamepadSerialRevokeGameFeedback'),CAPTURE_TYPE:struct('InputFrameSubmission'),FRAME_ADMISSION:['inputHostFrameSubmissionCurrent','inputHostFrameSubmissionMayIssue','inputHostFrameSubmissionTransportRejected','inputHostFrameSubmissionRequestCurrent'].map(fn).join(String.fromCharCode(10)),GAME_MOTOR:[struct('InputHostGameMotorDomKeys'),fn('inputHostGameMotorFromDom'),fn('inputHostConsumeGameMotorReplyLocked'),fn('inputHostObserveGameMotorBindingLocked')].join('\n'),FRAME_SCOPE:struct('InputFrameTransportScope'),CONSTANTS:constants,UNICODE:unicode,NEW_ID:fn('inputHostNewId'),HASH:['inputHostCryptoProvider','inputHostSha256','inputHostCanonicalFrameHash'].map(fn).join('\n'),ENVELOPE:fn('inputHostEnvelopeLocked'),PIPE:['inputHostPipeCloseOwned','inputHostCreateServerPipeLocked','inputHostPipeIo','inputHostWaitClientLocked'].map(fn).join('\n'),FAST_RECEIPT:struct('InputHostFastReceipt'),SCANNER:section('namespace c7aReceiptFast {','c7aReceiptFast'),AFTER_FRAME:fn('inputHostAfterSubmitFrameAccepted'),RELEASE_TIMING_LOG:fn('inputHostReleaseTimingReceiptLog'),COMMAND_RESULT_LOG:fn('inputHostCommandResultLog'),REQUEST:fn('inputHostRequestPreparedLocked')};
const inc=path.join(root,'source');fs.mkdirSync(inc);for(const name of headerNames)fs.copyFileSync(headerPath(name),path.join(inc,name));fs.copyFileSync(path.join(repo,'deps/json/json.hpp'),path.join(inc,'json.hpp'));const rawMotorTests=fs.readFileSync(path.join(repo,FIXTURE,'NativeMotorPipeTests.cpp'),'utf8');
const oldBindingExpectation='const bool supported=persona=="dualsense-edge";';
const currentBindingExpectation='const auto* expectedProfile=ymcc::capabilities::controller::gameMotorReceiptForProfile(ymcc::controllerPersona::profileIdentity(persona));const bool supported=expectedProfile&&expectedProfile->token>=1&&expectedProfile->token<=3;';
assert(rawMotorTests.split(oldBindingExpectation).length===2,'one historical binding expectation');
const compiledMotorTests=rawMotorTests.replace(oldBindingExpectation,currentBindingExpectation);
fs.writeFileSync(path.join(inc,'NativeMotorPipeTests.cpp'),compiledMotorTests);
bindings.push({name:'D12 current target-metadata binding expectation; Sony packet remains unsupported under Xbox',rawSource:path.join(repo,FIXTURE,'NativeMotorPipeTests.cpp'),rawSha256:sha(rawMotorTests),compiledCopySha256:sha(compiledMotorTests),before:oldBindingExpectation,after:currentBindingExpectation,originalTestFileUnchanged:true,assertionsRemoved:0,productionSourceModified:false});
let cpp=fs.readFileSync(path.join(repo,FIXTURE,'NativeEntry.cpp'),'utf8');
const replaceTemplate=(before,after,label)=>{assert(cpp.split(before).length===2,'actual persona template '+label);cpp=cpp.replace(before,after);};
replaceTemplate('@@FRAME_ADMISSION@@',lifecycle.code+'\n@@FRAME_ADMISSION@@','actual lifecycle dependency closure');
replaceTemplate('@@REQUEST@@',targetReturn.code+'\n@@REQUEST@@','actual target write-ahead duty with owned environment leaves');
replaceTemplate('static struct { bool enabled=true,virtualTargetEnabled=true; } g_realStickTest;','static struct { bool enabled=true,virtualTargetEnabled=true; std::string virtualTargetPersona="dualsense-edge"; } g_realStickTest;\n'+actualPersona,'current settings and real persona state');
replaceTemplate('fixturePersona=selected;','fixturePersona=selected;fixtureSetMatchingPersona(selected);','current requested and bound persona');
replaceTemplate('  g_inputHostProcess=nullptr;','  inputHostClearBoundPersona();g_inputHostProcess=nullptr;','retire actual bound persona');
for(const[name,code]of Object.entries(replacements)){assert(cpp.split('@@'+name+'@@').length===2,name);cpp=cpp.replace('@@'+name+'@@',()=>code);}assert(!/@@\w+@@/.test(cpp));fs.writeFileSync(path.join(inc,'fixture.cpp'),cpp);fs.writeFileSync(path.join(root,'source-bindings.json'),JSON.stringify({sources,bindings,lifecycle:{inputs:lifecycle.inputs,boundaries:lifecycle.boundaries},targetReturn:{inputs:targetReturn.inputs,boundary:targetReturn.boundary}},null,2));
const configurations=[];const vc=process.env.YMCC_VCVARS64||'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
for(const mode of ['Release','Checked']){
 const dir=path.join(root,mode);fs.mkdirSync(dir);const flags=mode==='Release'?'/O2 /MT /DNDEBUG':'/Od /MTd /RTC1 /D_DEBUG';
 const batch=path.join(dir,'compile.cmd');fs.writeFileSync(batch,calibrationToolchainPreamble(vc)+'chcp 65001 >nul\r\ncl /nologo /std:c++20 /utf-8 /EHsc /W4 /WX /showIncludes /permissive- '+flags+' /I"'+inc+'" "'+path.join(inc,'fixture.cpp')+'" /Fo"'+path.join(dir,'fixture.obj')+'" /Fe"'+path.join(dir,'fixture.exe')+'" advapi32.lib ole32.lib /link /verbose:lib\r\nexit /b %errorlevel%\r\n');
 const compileStarted=Date.now();let p=spawnSync('cmd.exe',['/d','/c',batch],{cwd:dir,windowsHide:true,encoding:'utf8',timeout:60000,maxBuffer:4*1024*1024});const compileText=(p.stdout||'')+(p.stderr||'');fs.writeFileSync(path.join(dir,'compile.log'),compileText);recordNativeCompile({root,dir,mode,command:{exe:'cmd.exe',args:['/d','/c',batch]},process:p,startedAt:compileStarted});assert(!p.error&&!p.signal,'native compiler did not finish');assert.equal(p.status,0,'native integration compilation '+mode+'; '+path.join(dir,'compile.log'));
 const includes=[...new Set([...compileText.matchAll(/^\s*(?:Note: including file:|注意:\s*包含文件:)\s*(.+?)\r?$/gm)].map(m=>path.resolve(m[1])))];assert(includes.length>20);const linkerLibraries=[...new Set([...compileText.matchAll(/\b[A-Z]:\\[^\r\n]*?\.lib\b/gi)].map(m=>path.resolve(m[0])))];assert(linkerLibraries.length>0,'actual linker library observation missing');
 if(options['--compile-only']){configurations.push({mode,status:'COMPILE_ONLY_NOT_ACCEPTANCE',includes,linkerLibraries,executableSha256:hash(path.join(dir,'fixture.exe'))});continue;}
 assert(options['--managed-dll']&&fs.existsSync(options['--managed-dll']),'compiled managed fixture required');
 const run=path.join(dir,'run');fs.mkdirSync(run);p=spawnSync(path.join(dir,'fixture.exe'),[options['--dotnet']||'C:/Program Files/dotnet/dotnet.exe',path.resolve(options['--managed-dll']),run,...(privateHeader?['stale-motor-revision']:[])],{cwd:dir,windowsHide:true,encoding:'utf8',timeout:90000,maxBuffer:4*1024*1024});fs.writeFileSync(path.join(dir,'stdout.log'),p.stdout||'');fs.writeFileSync(path.join(dir,'stderr.log'),p.stderr||'');
 let result;try{result=JSON.parse(fs.readFileSync(path.join(run,'result.json'),'utf8'));}catch{try{result=JSON.parse(p.stdout);}catch{result={status:'FAIL',error:'no complete consumer receipt'};}}
 assert(!p.error&&!p.signal,'native executable did not finish');
 const expected=options['--expect-rejection'];
 if(expected){assert(p.status===1&&result.status==='FAIL'&&result.error.includes(expected),'semantic counterexample not rejected by original assertion');}
 else assert(p.status===0&&result.status==='PASS'&&result.passed&&result.cases.length===10,'native/managed integrated assertions failed; '+path.join(dir,'stderr.log'));
 configurations.push({mode,status:expected?'EXPECTED_SEMANTIC_REJECTION':'PASS',includes,linkerLibraries,exitCode:p.status,result,executableSha256:hash(path.join(dir,'fixture.exe'))});console.log(mode+': '+configurations.at(-1).status+' '+(result.checks??0)+' checks');
}
if(managedContract){verifyManaged(managedContract.manifest,caseRoot);assert(hash(managedContract.manifest)===managedContract.manifestSha256);}
const changed=sources.filter(s=>hash(s.path)!==s.sha256);assert(changed.length===0,'inputs changed during integration run');
const result={schemaVersion:1,status:options['--compile-only']?'COMPILE_ONLY_NOT_ACCEPTANCE':'PASS',passed:!options['--compile-only'],expectedSemanticRejection:options['--expect-rejection']||null,sources,sourceUnchanged:true,configurations,managedDll:options['--managed-dll']?{path:path.resolve(options['--managed-dll']),sha256:hash(options['--managed-dll'])}:null,boundary:{normalProductMainExecuted:false,sourceExactNativeRequest:true,sourceExactManagedSession:true,realPrivateWindowsPipes:true,realSdkLoaded:false,deviceIo:false,normalCaptureFrameTicketsArmed:false,fixtureCaptureTicketsArmed:true,sourceExactFrameAdmission:true,fullNativeCoordinatorLifecycleExecuted:false,physicalReturnValidated:false}};
fs.writeFileSync(path.join(root,'RESULT.json'),JSON.stringify(result,null,2));console.log(JSON.stringify({status:result.status,configurations:configurations.map(c=>({mode:c.mode,status:c.status}))}));
