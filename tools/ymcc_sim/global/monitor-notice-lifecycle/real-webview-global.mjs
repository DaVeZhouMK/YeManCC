import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {fileURLToPath} from 'node:url';
import {run,sha,unique,LIMIT} from '../../../fixtures/radial_private/scope.mjs';
import {MODULE,REAL_WEBVIEW_ID,ORIGINAL_ENTRIES,FIXED_NODE,WEBVIEW_INPUTS,WEBVIEW_COMPONENT} from './real-webview-adapter.mjs';

const REPO=path.resolve(import.meta.dirname,'../../../..');
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const save=(file,value)=>fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n');
const key=file=>path.resolve(file).toLowerCase();
const pin=file=>({path:path.resolve(file),sha256:sha(file)});
const inside=(root,file)=>{const rel=path.relative(path.resolve(root),path.resolve(file));return rel===''||(!path.isAbsolute(rel)&&rel!=='..'&&!rel.startsWith('..'+path.sep));};
const need=(value,message)=>assert(value,message);
function blocked(message){const error=Error(message);error.blocked=true;throw error;}
function absolute(value,message){need(typeof value==='string'&&path.isAbsolute(value)&&!/[\0\r\n]/.test(value),message);return path.resolve(value);}
function noReparse(file){need(key(fs.realpathSync(file))===key(file),'FORWARDED_OR_REPARSE_PATH:'+file);}
function freshRoot(value){const root=absolute(value,'EXPLICIT_EVIDENCE_ROOT_REQUIRED');need(!inside(REPO,root)&&!inside(root,REPO),'EVIDENCE_OUTSIDE_PRODUCT_ROOT_REQUIRED');need(!fs.existsSync(root),'FRESH_COMPONENT_EVIDENCE_REQUIRED');fs.mkdirSync(root,{recursive:true});noReparse(root);return root;}
function repoPins(){return Object.fromEntries(WEBVIEW_INPUTS.map(rel=>[rel,sha(path.join(REPO,rel))]));}
function verifyRepo(pins){for(const [rel,digest]of Object.entries(pins))need(sha(path.join(REPO,rel))===digest,'COMPONENT_SOURCE_CHANGED:'+rel);}
function verifyRuntime(manifest){
 const root=absolute(manifest.runtimeRoot,'EXPLICIT_RUNTIME_ROOT_REQUIRED');noReparse(root);
 need(/^\d+\.\d+\.\d+\.\d+$/.test(manifest.runtimeVersion),'EXPLICIT_RUNTIME_VERSION_REQUIRED');
 const entries=fs.readdirSync(root,{recursive:true,withFileTypes:true});need(entries.every(e=>!e.isSymbolicLink()),'RUNTIME_REPARSE_REFUSED');
 const files=entries.filter(e=>e.isFile()).map(e=>path.join(e.parentPath??e.path,e.name));
 need(files.length===manifest.runtimeFiles.length&&files.length<=LIMIT,'RUNTIME_EXACT_MEMBERSHIP_REQUIRED');
 const expected=new Map();for(const row of manifest.runtimeFiles){need(typeof row.path==='string'&&!path.isAbsolute(row.path)&&!row.path.split(/[\\/]/).includes('..'),'RUNTIME_MEMBER_ESCAPE');const file=path.join(root,row.path);need(!expected.has(key(file))&&/^[a-f0-9]{64}$/.test(row.sha256),'RUNTIME_DUPLICATE_OR_BAD_PIN');expected.set(key(file),row.sha256);}
 for(const file of files){noReparse(file);need(expected.get(key(file))===sha(file),'RUNTIME_IDENTITY_CHANGED:'+file);}
 need(expected.has(key(path.join(root,'msedgewebview2.exe'))),'FIXED_RUNTIME_EXECUTABLE_REQUIRED');
 return {runtimeRoot:root,runtimeVersion:manifest.runtimeVersion,runtimeFiles:manifest.runtimeFiles};
}
function verifyOriginalPrepared(state,dir){
 need(key(state.out)===key(dir),'ORIGINAL_PREPARED_OUT_MISMATCH');noReparse(dir);
 need(inside(dir,state.exe.path)&&sha(state.exe.path)===state.exe.sha256,'ORIGINAL_EXECUTABLE_CHANGED');
 const allowedSources=new Set([...WEBVIEW_INPUTS,...(state.beforeNativeInputs??[]).map(row=>path.isAbsolute(row.path)?(inside(REPO,row.path)?path.relative(REPO,row.path).replaceAll('\\\\','/'):row.path):row.path)]);
 for(const [file,digest]of Object.entries(state.scopeReceipt.sourceHashes)){
  if(path.isAbsolute(file))need(inside(dir,file),'ORIGINAL_GENERATED_INPUT_ESCAPE');
  else {need(!path.isAbsolute(file)&&!file.split(/[\\\\/]/).includes('..'),'ORIGINAL_SOURCE_PATH_ESCAPE');need(fs.existsSync(path.join(REPO,file))&& (allowedSources.has(file)||file.includes('deps/webview2/build/native/include/')),'UNDECLARED_ORIGINAL_SOURCE:'+file);}
  need(sha(path.isAbsolute(file)?file:path.join(REPO,file))===digest,'ORIGINAL_PREPARED_SOURCE_CHANGED:'+file);
 }
 for(const row of state.scopeReceipt.compiledArtifacts){need(inside(dir,row.path),'ORIGINAL_ARTIFACT_ESCAPE');need(sha(row.path)===row.sha256,'ORIGINAL_ARTIFACT_CHANGED:'+row.path);}
 for(const row of state.scopeReceipt.toolArtifacts)need(sha(row.path)===row.sha256,'ORIGINAL_TOOL_CHANGED:'+row.path);
 need(unique([...state.scopeReceipt.toolArtifacts,...state.scopeReceipt.compiledArtifacts]).length<=LIMIT,'ORIGINAL_COMBINED_LIMIT');
 const graph=read(path.join(dir,'native-build-graph.json'));need(graph.pass===true&&key(graph.executable)===key(state.exe.path)&&graph.executableSha256===state.exe.sha256,'ORIGINAL_BUILD_GRAPH_MISMATCH');
}
async function original(root,label,entry,args){
 const command={executable:FIXED_NODE,args:[path.join(REPO,entry),...args],cwd:REPO,wrapperTimeoutMs:180000,originalCompileTimeoutMs:120000,originalReviewedLaunchTimeoutMs:150000,originalWorkerTimeoutMs:120000,startedAt:new Date().toISOString()};
 save(path.join(root,label+'.command.json'),command);let job;
 try{job=await run(FIXED_NODE,command.args,{cwd:REPO,env:{...process.env},windowsHide:true,timeout:180000,maxBuffer:4e6});}catch(error){job=error;}
 fs.writeFileSync(path.join(root,label+'.stdout.log'),job.stdout??'');fs.writeFileSync(path.join(root,label+'.stderr.log'),job.stderr??'');
 const outcome={...command,finishedAt:new Date().toISOString(),exitCode:Number.isInteger(job.code)?job.code:0,signal:job.signal??null,killed:job.killed??false};save(path.join(root,label+'.command-result.json'),outcome);
 need(outcome.exitCode===0&&!outcome.signal&&!outcome.killed,'ORIGINAL_SELFTEST_FAILED:'+label);return outcome;
}

export const ORIGINAL_CHECK_NAMES=Object.freeze([
 'actual composition controller and inert local renderer become ready',
 'native exact retirement destroys actual WebView2 parent HWND',
 'production retirement invokes actual controller Close and receives success',
 'actual renderer callbacks do not retain the retired native State',
 'ordinary production close closes real controller and owns no State',
 'ordinary close revokes old native create lease',
 'real completed controller is held before delivery to production',
 'retire releases native State before real completion callback arrives',
 'late actual completion closes its real controller and cannot recreate State',
 'Carrier destructor destroys HWND and expires State while actual callback is held',
 'actual completion after Carrier destructor is safe and closes controller',
 'late real old callback closes only old controller and preserves new owner',
 'new current actual controller retires independently',
 'repeat actual controller lifecycle 0','repeat actual controller lifecycle 1','repeat actual controller lifecycle 2',
 'all nine real completion callbacks forwarded and references released',
 'all production native event hooks and owners released',
 'own explicit browser exits naturally before owned Job cleanup',
]);
/** Complete original sequence/failure mask, never a target-error substring test. */
export function validateRealWebViewResult(raw,launcher,{negative=false,runtimeVersion}={}){
 const count=negative?8:19,expected=ORIGINAL_CHECK_NAMES.slice(0,count);
 assert.deepEqual(raw.cases.map(row=>row.name),expected,'EXACT_ORIGINAL_WEBVIEW_CASE_SEQUENCE');
 assert.deepEqual(raw.cases.map(row=>row.passed),expected.map((_,i)=>!negative||i!==7),'EXACT_ORIGINAL_WEBVIEW_FAILURE_MASK');
 assert.equal(raw.checks,count,'ORIGINAL_CHECK_COUNT');assert.equal(raw.status,negative?'FAIL':'PASS');
 if(negative)assert.equal(raw.error,ORIGINAL_CHECK_NAMES[7],'EXACT_ORIGINAL_WEBVIEW_FAILURE_REASON');else need(!Object.hasOwn(raw,'error'),'UNEXPECTED_POSITIVE_ERROR');
 for(const name of ['actualWebView2Controllers','environmentForwardingObserver','syntheticLocalRendererHandshake','privateDesktop'])assert.equal(raw[name],true,name);
 for(const name of ['controllerTestDouble','actualProductRendererVue','desktopSwitched','normalYmccStarted','deviceIo','sleep','manualSleepAcceptanceReady'])assert.equal(raw[name],false,name);
 assert.equal(raw.startedActualCompositionControllers,negative?3:9);assert.equal(raw.completedActualCompositionControllers,negative?3:9);
 assert.equal(raw.remainingNativeHooks,0);assert.equal(raw.remainingNativeOwners,0);
 if(runtimeVersion)assert.equal(raw.runtimeVersion,runtimeVersion);
 assert.equal(raw.actualControllerCloseObservations.length,negative?3:9);
 for(const row of raw.actualControllerCloseObservations)assert.equal(row.realController,true);
 if(!negative){assert.equal(raw.browserNaturalExitBeforeJobClose,true);for(const row of raw.actualControllerCloseObservations){assert.equal(row.actualCloseCalls,1);assert.equal(row.actualCloseResult,0);assert.equal(row.forwardingControllersDestroyed,1);}}
 assert.equal(launcher.status,negative?'FAIL':'PASS');assert.equal(launcher.workerExitCode,negative?2:0);
 for(const name of ['privateDesktop','privateDesktopClosed','ownedJobOnly'])assert.equal(launcher[name],true,name);
 for(const name of ['desktopSwitched','forcedTermination','normalYmccStarted'])assert.equal(launcher[name],false,name);
 need(!Object.hasOwn(launcher,'error'),'UNEXPECTED_LAUNCHER_ERROR');
 return {status:raw.status,checks:count,controllers:raw.completedActualCompositionControllers,failedCases:raw.cases.filter(row=>!row.passed),exactOriginalMask:true,workerExitCode:launcher.workerExitCode};
}

export async function prepareRealWebViewComponent(options){
 need(key(process.execPath)===key(FIXED_NODE),'PINNED_NODE_REQUIRED');const root=freshRoot(options.evidenceDir),sources=repoPins();
 const pending={schemaVersion:1,status:'PREPARING_NO_WEBVIEW_EXECUTION',id:REAL_WEBVIEW_ID,nativeLaunchAttempted:false,sourceHashes:sources,fullPersistentAcceptance:false};save(path.join(root,'result.json'),pending);
 try{
  const runtime=absolute(options.runtime,'EXPLICIT_RUNTIME_REQUIRED');need(/^\d+\.\d+\.\d+\.\d+$/.test(options.runtimeVersion??''),'EXPLICIT_RUNTIME_VERSION_REQUIRED');
  for(const [rel,digest]of Object.entries(sources)){const saved=path.join(root,'source-snapshot',rel);fs.mkdirSync(path.dirname(saved),{recursive:true});fs.copyFileSync(path.join(REPO,rel),saved,fs.constants.COPYFILE_EXCL);need(sha(saved)===digest,'SOURCE_CAPTURE_CHANGED');}
  const p=path.join(root,'p'),r=path.join(root,'r');
  await original(root,'p-prepare',ORIGINAL_ENTRIES[0],['--out',p,'--runtime',runtime,'--runtime-version',options.runtimeVersion,'--prepare-only']);
  const positive=read(path.join(p,'REAL-WEBVIEW-PREPARE-PROCESS.json'));need(positive.status==='REAL_WEBVIEW_NATIVE_PREPARED_REQUIRES_EXPLICIT_EXACT_PARENT_REVIEW','ORIGINAL_POSITIVE_NOT_PREPARED');verifyOriginalPrepared(positive,p);
  await original(root,'r-prepare',ORIGINAL_ENTRIES[1],['--out',r,'--positive-root',p,'--prepare-only']);
  const negative=read(path.join(r,'REAL-WEBVIEW-NEGATIVE-PREPARE.json'));need(negative.status==='EXACT_SINGLE_STRONG_CAPTURE_NEGATIVE_PREPARED','ORIGINAL_NEGATIVE_NOT_PREPARED');verifyOriginalPrepared(negative,r);
  verifyRepo(sources);const manifest=read(positive.runtimeManifest.path);verifyRuntime(manifest);
  const builds=[{kind:'positive',prepareRecord:path.join(p,'REAL-WEBVIEW-PREPARE-PROCESS.json'),prepareRecordSha256:sha(path.join(p,'REAL-WEBVIEW-PREPARE-PROCESS.json')),preparedOut:p,executable:positive.exe},{kind:'strong-state-negative',prepareRecord:path.join(r,'REAL-WEBVIEW-NEGATIVE-PREPARE.json'),prepareRecordSha256:sha(path.join(r,'REAL-WEBVIEW-NEGATIVE-PREPARE.json')),preparedOut:r,executable:negative.exe}];
  const prepared={schemaVersion:1,id:REAL_WEBVIEW_ID,status:'PREPARED_REQUIRES_INDEPENDENT_PARENT_REVIEW',preparedAt:new Date().toISOString(),root,sourceHashes:sources,runtimeManifest:positive.runtimeManifest,builds,toolArtifacts:unique([...positive.scopeReceipt.toolArtifacts,...negative.scopeReceipt.toolArtifacts]),toolProofs:[positive.toolProof,negative.toolProof],nativeLaunchAttempted:false,nativeExecutionStarted:false,originalBudgets:{compileMs:120000,reviewedLaunchMs:150000,ownedWorkerMs:120000},lifecycleComponent:WEBVIEW_COMPONENT,fullPersistentAcceptance:false,fullProductAcceptance:false};
  need(prepared.toolArtifacts.length<=LIMIT,'ORIGINAL_COMBINED_LIMIT');save(path.join(root,'PREPARED.json'),prepared);
  const template={schemaVersion:1,approved:false,reviewedAt:null,reviewer:null,authorizationBasis:'AWAITING_INDEPENDENT_EXACT_PARENT_STATIC_REVIEW',cases:ORIGINAL_ENTRIES,builds:builds.map(b=>({preparedOut:b.preparedOut,executableSha256:b.executable.sha256})),nativeToolsSha256:sha(path.join(REPO,'tools/fixtures/radial_private/native-tools.json')),nodeGraphSha256:positive.toolProof.nodeGraphSha256,allowedBoundary:'owned-private-desktop-composition-independent-input-and-explicit-webview-runtime',runtimeOsConfinementReviewed:false,runtimeRoot:manifest.runtimeRoot,runtimeVersion:manifest.runtimeVersion,runtimeFiles:manifest.runtimeFiles,sourcePins:sources,preparedManifestSha256:sha(path.join(root,'PREPARED.json'))};
  save(path.join(root,'PARENT-REVIEW-TEMPLATE.json'),template);
  const result={...pending,status:'PREPARED_REQUIRES_INDEPENDENT_PARENT_REVIEW',preparedManifest:pin(path.join(root,'PREPARED.json')),parentReviewTemplate:pin(path.join(root,'PARENT-REVIEW-TEMPLATE.json')),requiredIndependentDecisions:['approved must be true only after independent static review','runtimeOsConfinementReviewed must be true only after independent review of the original browser/Job/private-desktop boundary'],requiredEnvironment:['YMCC_MONITOR_WEBVIEW_PREPARED_ROOT','YMCC_RADIAL_EXECUTION_REVIEW','YMCC_RADIAL_EXECUTION_REVIEW_SHA256'],originalScriptsExecutedWithPrepareOnly:true,originalPositiveExecuted:false,originalNegativeExecuted:false};save(path.join(root,'result.json'),result);return result;
 }catch(error){save(path.join(root,'result.json'),{...pending,status:'FAIL',error:String(error.stack??error),nativeLaunchAttempted:false});throw error;}
}

function reviewedPrepared(options){
 const value=options.preparedRoot??process.env.YMCC_MONITOR_WEBVIEW_PREPARED_ROOT;if(!value)blocked('MONITOR_WEBVIEW_PREPARED_ROOT_REQUIRED');
 const root=absolute(value,'PREPARED_ROOT_REQUIRED'),file=path.join(root,'PREPARED.json');if(!fs.existsSync(file))blocked('ORIGINAL_PREPARED_INPUT_MISSING');noReparse(root);
 const prepared=read(file);need(prepared.status==='PREPARED_REQUIRES_INDEPENDENT_PARENT_REVIEW'&&key(prepared.root)===key(root)&&prepared.id===REAL_WEBVIEW_ID,'PREPARED_COMPONENT_IDENTITY');verifyRepo(prepared.sourceHashes);
 for(const build of prepared.builds){need(inside(root,build.preparedOut)&&inside(root,build.prepareRecord),'PREPARED_INPUT_ESCAPE');need(sha(build.prepareRecord)===build.prepareRecordSha256,'PREPARED_RECORD_CHANGED');verifyOriginalPrepared(read(build.prepareRecord),build.preparedOut);}
 need(prepared.builds.length===2&&prepared.builds[0].kind==='positive'&&prepared.builds[1].kind==='strong-state-negative','EXACT_ORIGINAL_BUILD_PAIR_REQUIRED');
 need(sha(prepared.runtimeManifest.path)===prepared.runtimeManifest.sha256,'RUNTIME_MANIFEST_CHANGED');const runtime=verifyRuntime(read(prepared.runtimeManifest.path));
 const reviewFile=process.env.YMCC_RADIAL_EXECUTION_REVIEW,reviewPin=process.env.YMCC_RADIAL_EXECUTION_REVIEW_SHA256;
 if(!reviewFile||!reviewPin)blocked('NATIVE_PREEXECUTION_PARENT_REVIEW_REQUIRED');
 need(/^[a-f0-9]{64}$/.test(reviewPin)&&sha(reviewFile)===reviewPin,'EXACT_PARENT_REVIEW_PIN_REQUIRED');const review=read(reviewFile);
 if(review.approved!==true||review.runtimeOsConfinementReviewed!==true)blocked('INDEPENDENT_PARENT_APPROVAL_AND_RUNTIME_CONFINEMENT_REVIEW_REQUIRED');
 need(review.schemaVersion===1&&review.preparedManifestSha256===sha(file),'REVIEW_PREPARED_MANIFEST_IDENTITY');
 for(const entry of ORIGINAL_ENTRIES)need(review.case===entry||review.cases?.includes(entry),'REVIEW_ORIGINAL_CASE_BINDING');
 for(const build of prepared.builds)need(review.builds?.some(row=>key(row.preparedOut)===key(build.preparedOut)&&row.executableSha256===build.executable.sha256),'REVIEW_ORIGINAL_BUILD_BINDING');
 need(review.nativeToolsSha256===sha(path.join(REPO,'tools/fixtures/radial_private/native-tools.json'))&&review.nodeGraphSha256===prepared.toolProofs[0].nodeGraphSha256,'REVIEW_TOOL_IDENTITY');
 need(review.allowedBoundary==='owned-private-desktop-composition-independent-input-and-explicit-webview-runtime','REVIEW_ORIGINAL_BOUNDARY');
 need(key(review.runtimeRoot)===key(runtime.runtimeRoot)&&review.runtimeVersion===runtime.runtimeVersion,'REVIEW_RUNTIME_IDENTITY');assert.deepEqual(review.runtimeFiles,runtime.runtimeFiles,'REVIEW_EXACT_RUNTIME_MEMBERS');
 return {root,file,prepared,runtime,reviewFile,reviewPin};
}
export async function runRealWebViewComponent(options){
 need(key(process.execPath)===key(FIXED_NODE),'PINNED_NODE_REQUIRED');const root=freshRoot(options.evidenceDir),sources=repoPins();let attempted=false;
 const base={schemaVersion:1,id:REAL_WEBVIEW_ID,lifecycleComponent:WEBVIEW_COMPONENT,sourceHashes:sources,fullPersistentAcceptance:false,fullPersistentRecoveryAcceptance:false,fullProductAcceptance:false,normalYmccStarted:false,operatingSystemSleep:false};
 try{
  const input=reviewedPrepared(options),{prepared,runtime}=input,p=prepared.builds[0].preparedOut,r=prepared.builds[1].preparedOut;
  attempted=true;await original(root,'p-execute',ORIGINAL_ENTRIES[0],['--out',p,'--runtime',runtime.runtimeRoot,'--runtime-version',runtime.runtimeVersion]);
  const positiveState=read(path.join(p,'REAL-WEBVIEW-EXECUTE-PROCESS.json'));need(positiveState.status==='ACTUAL_REAL_WEBVIEW_NINE_CONTROLLERS_RETIRED_LATE_CALLBACKS_AND_NATURAL_BROWSER_EXIT_VERIFIED','ORIGINAL_POSITIVE_EXECUTION_NOT_ACCEPTED');
  const positive=validateRealWebViewResult(read(path.join(p,'real-webview-result.json')),read(path.join(p,'real-webview-private-launch.json')),{runtimeVersion:runtime.runtimeVersion});
  await original(root,'r-execute',ORIGINAL_ENTRIES[1],['--out',r,'--positive-root',p]);
  const negativeState=read(path.join(r,'REAL-WEBVIEW-NEGATIVE-EXECUTE.json'));need(negativeState.status==='EXACT_STRONG_STATE_CAPTURE_NEGATIVE_FAILS_AT_REAL_LATE_COMPLETION_OWNER_RELEASE','ORIGINAL_NEGATIVE_EXECUTION_NOT_ACCEPTED');assert.equal(negativeState.execution.code,2);assert.equal(negativeState.execution.signal,null);assert.equal(negativeState.execution.killed,false);
  const negative=validateRealWebViewResult(read(path.join(r,'real-webview-result.json')),read(path.join(r,'real-webview-private-launch.json')),{negative:true,runtimeVersion:runtime.runtimeVersion});
  verifyRepo(prepared.sourceHashes);verifyRuntime(runtime);need(sha(input.reviewFile)===input.reviewPin,'PARENT_REVIEW_CHANGED_DURING_EXECUTION');for(const build of prepared.builds)verifyOriginalPrepared(read(build.prepareRecord),build.preparedOut);
  const evidence=[];for(const [label,dir,processFile]of [['p',p,'REAL-WEBVIEW-EXECUTE-PROCESS.json'],['r',r,'REAL-WEBVIEW-NEGATIVE-EXECUTE.json']])for(const name of ['real-webview-result.json','real-webview-private-launch.json','run.log',processFile]){const target=path.join(root,label+'-'+name);fs.copyFileSync(path.join(dir,name),target,fs.constants.COPYFILE_EXCL);need(sha(target)===sha(path.join(dir,name)),'RAW_RECEIPT_COPY_CHANGED');evidence.push(pin(target));}
  const result={...base,status:'PASS',positive,negative,originalNegativeRawStatusPreserved:'FAIL',nativeLaunchAttempted:attempted,preparedManifest:pin(input.file),parentReview:{path:input.reviewFile,sha256:input.reviewPin},originalEvidence:evidence,runtimeIdentity:{root:runtime.runtimeRoot,version:runtime.runtimeVersion,fileCount:runtime.runtimeFiles.length},parentFeatureAcceptance:'INCOMPLETE_REQUIRES_SEPARATE_FULL_EXIT_AND_RECOVERY_EVIDENCE'};save(path.join(root,'result.json'),result);return result;
 }catch(error){const result={...base,status:error.blocked?'BLOCKED':'FAIL',error:String(error.stack??error),nativeLaunchAttempted:attempted,independentParentReviewRequired:true};save(path.join(root,'result.json'),result);error.componentResult=result;throw error;}
}
function parse(args){const options={mode:'run'},allowed=new Set(['mode','evidence-dir','runtime','runtime-version','prepared-root']);for(let i=0;i<args.length;i+=2){const name=args[i]?.slice(2);need(args[i]?.startsWith('--')&&allowed.has(name)&&args[i+1]&&!args[i+1].startsWith('--'),'INVALID_COMPONENT_ARGUMENT');const field=name.replace(/-([a-z])/g,(_,c)=>c.toUpperCase());need(!Object.hasOwn(options,field)||field==='mode'&&i===0,'DUPLICATE_COMPONENT_ARGUMENT');options[field]=args[i+1];}need(['prepare','run'].includes(options.mode),'INVALID_COMPONENT_MODE');return options;}
if(process.argv[1]&&key(process.argv[1])===key(fileURLToPath(import.meta.url))){try{const options=parse(process.argv.slice(2));const result=await(options.mode==='prepare'?prepareRealWebViewComponent(options):runRealWebViewComponent(options));console.log(JSON.stringify({id:REAL_WEBVIEW_ID,status:result.status,evidenceDir:options.evidenceDir,nativeLaunchAttempted:result.nativeLaunchAttempted}));}catch(error){console.error(error.stack);process.exitCode=error.blocked?10:2;}}
