import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {fileURLToPath} from 'node:url';
import {run,sha,unique,LIMIT,verify,nativeTools} from '../../../fixtures/radial_private/scope.mjs';
import {
 MODULE,REAL_RENDERER_ID,ORIGINAL_ENTRIES,FIXED_NODE,FIXED_RUNTIME,RUNTIME_VERSION,
 PRODUCT_RENDERER_INPUTS,RENDERER_INPUTS,RENDERER_COMPONENT,rendererReviewBinding,
} from './real-renderer-adapter.mjs';

const REPO=path.resolve(import.meta.dirname,'../../../..');
const CORE='G:/YeManCC-Work/Testing/YMCC';
const EVIDENCE=path.join(CORE,'Evidence');
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const save=(file,value)=>fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n');
const key=file=>path.resolve(file).toLowerCase();
const pin=file=>({path:path.resolve(file),sha256:sha(file)});
const inside=(root,file)=>{const rel=path.relative(path.resolve(root),path.resolve(file));return rel===''||(!path.isAbsolute(rel)&&rel!=='..'&&!rel.startsWith('..'+path.sep));};
const need=(value,message)=>assert(value,message);
const PREPARE_RECORDS=['REAL-RENDERER-PREPARE-PROCESS.json','REAL-RENDERER-NEGATIVE-PREPARE-PROCESS.json'];
const PREPARE_STATUSES=['REAL_RENDERER_NATIVE_PREPARED_REQUIRES_EXPLICIT_EXACT_PARENT_REVIEW','REAL_RENDERER_NEGATIVE_NATIVE_PREPARED_REQUIRES_EXPLICIT_EXACT_PARENT_REVIEW'];
const PREPARED_STATUS='PREPARED_REQUIRES_INDEPENDENT_PARENT_REVIEW';
const BOUNDARY='owned-private-desktop-composition-independent-input-and-explicit-webview-runtime';
export const ORIGINAL_BUDGETS=Object.freeze({compileMs:120000,reviewedLaunchMs:150000,ownedWorkerMs:120000,maxArtifactInputs:LIMIT});
function blocked(message){const error=Error(message);error.blocked=true;throw error;}
function absolute(value,message){need(typeof value==='string'&&path.isAbsolute(value)&&!/[\0\r\n]/.test(value),message);return path.resolve(value);}
function noReparse(file){need(key(fs.realpathSync(file))===key(file),'FORWARDED_OR_REPARSE_PATH:'+file);}
function freshEvidence(value){
 const root=absolute(value,'EXPLICIT_EVIDENCE_ROOT_REQUIRED');
 need(inside(EVIDENCE,root)&&key(root)!==key(EVIDENCE),'COMPONENT_CORE_EVIDENCE_SUBDIRECTORY_REQUIRED');
 need(!fs.existsSync(root),'FRESH_COMPONENT_EVIDENCE_REQUIRED');fs.mkdirSync(root,{recursive:true});noReparse(root);return root;
}
function preparedRoot(value){
 const root=absolute(value,'EXPLICIT_SHORT_PREPARED_ROOT_REQUIRED');
 need(key(path.dirname(root))===key(path.join(CORE,'Workspace/Rd'))&&/^[a-z0-9-]{1,12}$/i.test(path.basename(root)),'OWNED_CORE_SHORT_PREPARED_ROOT_REQUIRED');
 if(fs.existsSync(root))noReparse(root);return root;
}
function repoPins(){return Object.fromEntries(RENDERER_INPUTS.map(rel=>[rel,sha(path.join(REPO,rel))]));}
function verifyRepo(pins){assert.deepEqual(Object.keys(pins).sort(),[...RENDERER_INPUTS].sort(),'EXACT_DECLARED_SOURCE_SET');for(const [rel,digest]of Object.entries(pins))need(sha(path.join(REPO,rel))===digest,'COMPONENT_SOURCE_CHANGED:'+rel);}
function verifyRuntime(manifest){
 const root=absolute(manifest.runtimeRoot,'EXPLICIT_RUNTIME_ROOT_REQUIRED');noReparse(root);
 need(key(root)===key(FIXED_RUNTIME)&&manifest.runtimeVersion===RUNTIME_VERSION,'EXACT_FIXED_RUNTIME_REQUIRED');
 need(manifest.membership==='complete-recursive-files-no-reparse'&&manifest.runtimeDownloaded===false,'ORIGINAL_RUNTIME_MEMBERSHIP_POLICY');
 const entries=fs.readdirSync(root,{recursive:true,withFileTypes:true});need(entries.every(e=>!e.isSymbolicLink()),'RUNTIME_REPARSE_REFUSED');
 const files=entries.filter(e=>e.isFile()).map(e=>path.join(e.parentPath??e.path,e.name));
 need(files.length===916&&manifest.runtimeFiles.length===916,'EXACT_916_RUNTIME_MEMBERS_REQUIRED');
 const expected=new Map();for(const row of manifest.runtimeFiles){
  need(typeof row.path==='string'&&!path.isAbsolute(row.path)&&!row.path.split(/[\\/]/).some(part=>part==='..'||part===''),'RUNTIME_MEMBER_ESCAPE');
  const file=path.join(root,row.path);need(!expected.has(key(file))&&/^[a-f0-9]{64}$/.test(row.sha256),'RUNTIME_DUPLICATE_OR_BAD_PIN');expected.set(key(file),row.sha256);
 }
 for(const file of files){noReparse(file);need(expected.get(key(file))===sha(file),'RUNTIME_IDENTITY_CHANGED:'+file);}
 need(expected.has(key(path.join(root,'msedgewebview2.exe'))),'FIXED_RUNTIME_EXECUTABLE_REQUIRED');return manifest;
}

function verifyRendererBuild(state,dir,index){
 need(state.status===PREPARE_STATUSES[index]&&key(state.out)===key(dir),'ORIGINAL_PREPARED_IDENTITY');noReparse(dir);
 need(!state.nativeLaunchAttempted&&!state.nativeExecutionStarted,'PREPARED_BUILD_ALREADY_LAUNCHED');
 need(key(state.runtimeRoot)===key(FIXED_RUNTIME)&&state.runtimeVersion===RUNTIME_VERSION&&state.runtimeFileCount===916,'ORIGINAL_RUNTIME_IDENTITY');
 need(key(state.web)===key(path.join(dir,'owned-web')),'ORIGINAL_WEB_ROOT_IDENTITY');
 need(key(state.exe.path)===key(path.join(dir,'RealRendererNotice.exe'))&&sha(state.exe.path)===state.exe.sha256,'ORIGINAL_EXECUTABLE_CHANGED');
 const receipt=state.scopeReceipt,allowed=new Set(RENDERER_INPUTS.map(rel=>rel.toLowerCase()));
 for(const [file,digest]of Object.entries(receipt.sourceHashes)){
  if(path.isAbsolute(file))need(inside(dir,file)&&receipt.compiledArtifacts.some(row=>key(row.path)===key(file)&&row.sha256===digest),'ORIGINAL_GENERATED_INPUT_ESCAPE');
  else need(!file.split(/[\\/]/).includes('..')&&allowed.has(file.toLowerCase()),'UNDECLARED_ORIGINAL_SOURCE:'+file);
  need(sha(path.isAbsolute(file)?file:path.join(REPO,file))===digest,'ORIGINAL_PREPARED_SOURCE_CHANGED:'+file);
 }
 assert.deepEqual(Object.keys(receipt.sourceHashes).filter(file=>file.startsWith('src/')).sort(),[...PRODUCT_RENDERER_INPUTS].sort(),'EXACT_DEDICATED_RENDERER_SOURCE_CLOSURE');
 const generated=['explicit-runtime-manifest.json','actual-product-renderer.metafile.json','owned-web/notice-renderer.js','owned-web/notice-renderer.css','owned-web/notice-output.html','frame.json','actual-product-renderer.cpp','renderer-native-transforms.json','compile.rsp','tool-versions.json','compile.log','native-dependencies.json','native.obj','RealRendererNotice.exe','native-build-graph.json'];
 if(index===1)generated.push('exact-renderer-negative.json');
 assert.deepEqual(receipt.compiledArtifacts.map(row=>path.relative(dir,row.path).replaceAll('\\','/')).sort(),generated.sort(),'EXACT_ORIGINAL_COMPILED_ARTIFACT_SET');
 for(const row of receipt.compiledArtifacts){need(inside(dir,row.path),'ORIGINAL_ARTIFACT_ESCAPE');noReparse(row.path);need(sha(row.path)===row.sha256,'ORIGINAL_ARTIFACT_CHANGED:'+row.path);}
 const proof=verify({repo:REPO,node:receipt.nodeToolAuthority,native:nativeTools(REPO),sourceHashes:receipt.sourceHashes,toolArtifacts:receipt.toolArtifacts,generated:receipt.compiledArtifacts});
 assert.deepEqual(proof,state.toolProof,'ORIGINAL_SCOPE_PROOF_CHANGED');
 need(proof.maxArtifactInputs===3072&&proof.artifactCount<=LIMIT,'ORIGINAL_COMBINED_LIMIT');
 const graph=read(path.join(dir,'native-build-graph.json'));
 need(graph.pass===true&&key(graph.executable)===key(state.exe.path)&&graph.executableSha256===state.exe.sha256,'ORIGINAL_BUILD_GRAPH_MISMATCH');
 const before=new Map(state.beforeNativeInputs.map(row=>[key(row.path),row.sha256]));
 for(const row of graph.nativeInputs.filter(row=>['source','generated'].includes(row.role)))need(before.get(key(row.path))===row.sha256,'NATIVE_INPUT_NOT_BOUND_BEFORE_COMPILE:'+row.path);
 for(const row of state.beforeNativeInputs)need(sha(row.path)===row.sha256,'PREBOUND_NATIVE_INPUT_CHANGED');
 const transforms=read(path.join(dir,'renderer-native-transforms.json'));
 need(transforms.allOriginalNineteenAssertionsAndBudgetsRetained===true&&transforms.transforms.length===3,'ORIGINAL_ADDITIVE_RENDERER_TRANSFORMS_REQUIRED');
 need(key(transforms.original.path)===key(path.join(REPO,MODULE,'real-webview-selftest.cpp'))&&sha(transforms.original.path)===transforms.original.sha256,'ORIGINAL_WEBVIEW_FIXTURE_IDENTITY');
 need(key(transforms.checks.path)===key(path.join(REPO,MODULE,'real-renderer-checks.h'))&&sha(transforms.checks.path)===transforms.checks.sha256,'ORIGINAL_RENDERER_CHECKS_IDENTITY');
 let restored=fs.readFileSync(transforms.generated.path,'utf8');need(sha(transforms.generated.path)===transforms.generated.sha256,'GENERATED_NATIVE_CHANGED');
 for(const {before,after}of [...transforms.transforms].reverse()){need(restored.split(after).length===2,'EXACT_ADDITIVE_TRANSFORM_REQUIRED');restored=restored.replace(after,before);}
 need(restored===fs.readFileSync(transforms.original.path,'utf8'),'ALL_ORIGINAL_ASSERTIONS_AND_BUDGETS_MUST_RESTORE');
 const meta=read(path.join(dir,'actual-product-renderer.metafile.json'));
 assert.deepEqual(Object.keys(meta.inputs).filter(file=>!file.split(/[\\/]/).includes('node_modules')).sort(),[...PRODUCT_RENDERER_INPUTS].sort(),'ACTUAL_PRODUCT_BUILD_INPUT_SET');
 need(!Object.keys(receipt.sourceHashes).some(file=>/src[\\/]App\.vue$|src[\\/]gamepad[\\/]engine\.ts$/.test(file)),'NOTICE_RENDERER_MUST_REMAIN_DEDICATED');
 if(index===1){
  const mutation=read(path.join(dir,'exact-renderer-negative.json'));
  assert.deepEqual(mutation,{original:{path:path.join(REPO,'src/monitorNoticeOutputEntry.ts'),sha256:sha(path.join(REPO,'src/monitorNoticeOutputEntry.ts'))},before:'if(!trusted||disposed||raw?.type',after:'if(true||!trusted||disposed||raw?.type',restoresExactOriginal:true,onlyMutation:'reject every product native feedback frame'},'EXACT_ORIGINAL_SINGLE_FRAME_ADMISSION_MUTATION');
  const original=fs.readFileSync(mutation.original.path,'utf8');need(original.split(mutation.before).length===2&&original.replace(mutation.before,mutation.after).replace(mutation.after,mutation.before)===original,'EXACT_NEGATIVE_RESTORES_PRODUCT_SOURCE');
 }
 return state;
}

async function original(root,label,entry,args,{prepare=false}={}){
 const env={...process.env};for(const name of Object.keys(env))if(/^WEBVIEW2_/i.test(name)||(prepare&&/^YMCC_RADIAL_EXECUTION_REVIEW(?:_SHA256)?$/i.test(name)))delete env[name];
 if(!prepare){const binding=rendererReviewBinding();need(binding.file&&binding.pin,'EXACT_RENDERER_PARENT_REVIEW_REQUIRED');env.YMCC_RADIAL_EXECUTION_REVIEW=binding.file;env.YMCC_RADIAL_EXECUTION_REVIEW_SHA256=binding.pin;}
 const command={executable:FIXED_NODE,args:[path.join(REPO,entry),...args],cwd:REPO,wrapperTimeoutMs:180000,originalBudgets:ORIGINAL_BUDGETS,startedAt:new Date().toISOString(),prepareOnly:prepare};
 save(path.join(root,label+'.command.json'),command);let job,failed=false;
 try{const pending=run(FIXED_NODE,command.args,{cwd:REPO,env,windowsHide:true,timeout:180000,maxBuffer:4e6});command.pid=pending.child?.pid;save(path.join(root,label+'.command.json'),command);job=await pending;}catch(error){job=error;failed=true;}
 fs.writeFileSync(path.join(root,label+'.stdout.log'),job.stdout??'');fs.writeFileSync(path.join(root,label+'.stderr.log'),job.stderr??'');
 const outcome={...command,finishedAt:new Date().toISOString(),exitCode:failed?(Number.isInteger(job.code)?job.code:null):0,signal:job.signal??null,killed:job.killed??false,...(failed?{error:String(job.message)}:{})};
 save(path.join(root,label+'.command-result.json'),outcome);need(outcome.exitCode===0&&!outcome.signal&&!outcome.killed,'ORIGINAL_SELFTEST_FAILED:'+label);return outcome;
}

const WEBVIEW_CHECK_NAMES=Object.freeze([
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
const RENDERER_CHECK_NAMES=Object.freeze([
 'real product Vue starts mounted without a notice',
 'production native frame is admitted by exact current renderer owner',
 'real product Vue presents native feedback and brightness range',
 'real product notice has actual layout and applied value',
 'production native frame refuses a different nonce',
 'real product Vue independently ignores a wrong owner frame',
 'real product Vue independently ignores an old sequence',
 'production explicit hide accepted for current owner',
 'real product Vue clears the hidden notice',
 'production admits next feedback after explicit hide',
 'real product Vue presents the next feedback',
 'real product Vue expires feedback and removes DOM',
 'production admits feedback after expiry',
 'real product Vue recovers presentation after expiry',
 'actual product pagehide disposes subscriptions and unmounts Vue',
 'native remains alive for post-unmount late-frame probe',
 'disposed actual renderer ignores a later native frame',
]);
export const ORIGINAL_CHECK_NAMES=Object.freeze([WEBVIEW_CHECK_NAMES[0],...RENDERER_CHECK_NAMES,...WEBVIEW_CHECK_NAMES.slice(1)]);
export const NEGATIVE_CHECK_NAMES=Object.freeze(ORIGINAL_CHECK_NAMES.slice(0,4));
/** Exact complete case/failure sequence; synthetic renderer19 cannot satisfy this gate. */
export function validateRealRendererResult(raw,launcher,{negative=false,runtimeVersion=RUNTIME_VERSION}={}){
 const names=negative?NEGATIVE_CHECK_NAMES:ORIGINAL_CHECK_NAMES,controllers=negative?1:9;
 assert.deepEqual(raw.cases,names.map((name,index)=>({name,passed:!negative||index!==3})),'EXACT_ORIGINAL_RENDERER_CASE_AND_FAILURE_SEQUENCE');
 assert.equal(raw.checks,negative?4:36,'ORIGINAL_RENDERER_CHECK_COUNT');assert.equal(raw.status,negative?'FAIL':'PASS');
 if(negative)assert.equal(raw.error,NEGATIVE_CHECK_NAMES[3],'EXACT_ORIGINAL_RENDERER_FAILURE_REASON');else need(!Object.hasOwn(raw,'error'),'UNEXPECTED_POSITIVE_ERROR');
 for(const name of ['actualWebView2Controllers','environmentForwardingObserver','actualProductRendererVue','privateDesktop'])assert.equal(raw[name],true,name);
 for(const name of ['controllerTestDouble','syntheticLocalRendererHandshake','desktopSwitched','normalYmccStarted','deviceIo','sleep','manualSleepAcceptanceReady'])assert.equal(raw[name],false,name);
 assert.equal(raw.startedActualCompositionControllers,controllers);assert.equal(raw.completedActualCompositionControllers,controllers);
 assert.equal(raw.remainingNativeHooks,0);assert.equal(raw.remainingNativeOwners,0);assert.equal(raw.runtimeVersion,runtimeVersion);
 assert.equal(raw.actualControllerCloseObservations.length,controllers);
 for(const row of raw.actualControllerCloseObservations){assert.equal(row.realController,true);assert.equal(row.actualCloseCalls,1);assert.equal(row.actualCloseResult,0);assert.equal(row.forwardingControllersDestroyed,1);}
 if(!negative){assert.equal(raw.browserNaturalExitBeforeJobClose,true);need(Number.isInteger(raw.browserPid)&&raw.browserPid>0,'ACTUAL_OWNED_BROWSER_PID_REQUIRED');need(key(raw.browserObservedImage)===key(path.join(FIXED_RUNTIME,'msedgewebview2.exe')),'ACTUAL_BROWSER_IMAGE_REQUIRED');}
 else need(!Object.hasOwn(raw,'browserNaturalExitBeforeJobClose'),'NEGATIVE_CANNOT_CLAIM_COMPLETE_BROWSER_EXIT');
 assert.equal(launcher.status,negative?'FAIL':'PASS');assert.equal(launcher.workerExitCode,negative?2:0);
 for(const name of ['privateDesktop','privateDesktopClosed','ownedJobOnly'])assert.equal(launcher[name],true,name);
 for(const name of ['desktopSwitched','forcedTermination','normalYmccStarted'])assert.equal(launcher[name],false,name);
 need(!Object.hasOwn(launcher,'error'),'UNEXPECTED_LAUNCHER_ERROR');
 return {status:raw.status,checks:raw.checks,controllers,failedCases:raw.cases.filter(row=>!row.passed),exactOriginalSequence:true,exactOriginalFailureMask:true,workerExitCode:launcher.workerExitCode};
}

export async function prepareRealRendererComponent(options){
 need(key(process.execPath)===key(FIXED_NODE),'PINNED_NODE_REQUIRED');
 need(!process.env.YMCC_RADIAL_EXECUTION_REVIEW&&!process.env.YMCC_RADIAL_EXECUTION_REVIEW_SHA256,'PREPARE_REQUIRES_NO_EXECUTION_REVIEW');
 const root=freshEvidence(options.evidenceDir),short=preparedRoot(options.preparedRoot),sources=repoPins();
 const base={schemaVersion:1,id:REAL_RENDERER_ID,sourceHashes:sources,lifecycleComponent:RENDERER_COMPONENT,nativeLaunchAttempted:false,nativeExecutionStarted:false,fullPersistentAcceptance:false,fullProductAcceptance:false,normalYmccStarted:false,operatingSystemSleep:false};
 save(path.join(root,'result.json'),{...base,status:'PREPARING_NO_WEBVIEW_EXECUTION'});
 try{
  need(key(absolute(options.runtime,'EXPLICIT_FIXED_RUNTIME_REQUIRED'))===key(FIXED_RUNTIME)&&options.runtimeVersion===RUNTIME_VERSION,'EXACT_FIXED_RUNTIME_REQUIRED');
  need(!fs.existsSync(path.join(short,'PREPARED.json')),'PREPARED_MANIFEST_ALREADY_EXISTS');
  const dirs=['p','r'].map(name=>path.join(short,name)),records=dirs.map((dir,index)=>path.join(dir,PREPARE_RECORDS[index]));
  const existing=records.map(file=>fs.existsSync(file));need(existing.every(Boolean)||existing.every(value=>!value),'INCOMPLETE_ORIGINAL_PREPARE_PAIR_RETAINED');
  let commands=[];
  if(!existing[0]){
   if(!fs.existsSync(short))fs.mkdirSync(short,{recursive:true});noReparse(short);
   const settled=await Promise.allSettled(ORIGINAL_ENTRIES.map((entry,index)=>original(root,index===0?'p-prepare':'r-prepare',entry,['--out',dirs[index],'--runtime',FIXED_RUNTIME,'--runtime-version',RUNTIME_VERSION,'--prepare-only'],{prepare:true})));
   save(path.join(root,'prepare-commands.json'),settled.map(row=>row.status==='fulfilled'?row.value:{status:'REJECTED',error:String(row.reason?.stack??row.reason)}));
   const failure=settled.find(row=>row.status==='rejected');if(failure)throw failure.reason;commands=settled.map(row=>row.value);
  }
  const states=records.map((file,index)=>verifyRendererBuild(read(file),dirs[index],index));
  const manifest=verifyRuntime(read(states[0].runtimeManifest.path));
  assert.deepEqual(read(states[1].runtimeManifest.path),manifest,'EXACT_SAME_RUNTIME_FOR_BOTH_BUILDS');
  for(const state of states)need(sha(state.runtimeManifest.path)===state.runtimeManifest.sha256,'ORIGINAL_RUNTIME_MANIFEST_CHANGED');
  verifyRepo(sources);
  for(const [rel,digest]of Object.entries(sources)){const target=path.join(root,'source-snapshot',rel);fs.mkdirSync(path.dirname(target),{recursive:true});fs.copyFileSync(path.join(REPO,rel),target,fs.constants.COPYFILE_EXCL);need(sha(target)===digest,'SOURCE_CAPTURE_CHANGED');}
  const builds=states.map((state,index)=>({kind:index===0?'positive':'frame-admission-negative',entry:ORIGINAL_ENTRIES[index],preparedOut:dirs[index],prepareRecord:records[index],prepareRecordSha256:sha(records[index]),executable:state.exe,runtimeManifest:state.runtimeManifest,toolProof:state.toolProof}));
  const toolArtifacts=unique(states.flatMap(state=>state.scopeReceipt.toolArtifacts));
  const preparedArtifacts=unique([...toolArtifacts,...states.flatMap(state=>state.scopeReceipt.compiledArtifacts),...records.map(pin)]);
  need(preparedArtifacts.length+1<=LIMIT,'ORIGINAL_COMBINED_PREPARED_LIMIT');
  const prepared={...base,status:PREPARED_STATUS,root:short,preparedAt:new Date().toISOString(),runtimeManifest:states[0].runtimeManifest,builds,toolArtifacts,preparedArtifacts,toolProofs:states.map(state=>state.toolProof),originalBudgets:ORIGINAL_BUDGETS,originalPrepareMode:existing[0]?'ADOPTED_EXPLICIT_ORIGINAL_PREPARE_ONLY_PAIR':'EXECUTED_ORIGINAL_PREPARE_ONLY_PAIR',originalPrepareCommands:commands};
  fs.writeFileSync(path.join(short,'PREPARED.json'),JSON.stringify(prepared,null,2)+'\n',{flag:'wx'});
  const template={schemaVersion:1,approved:false,reviewedAt:null,reviewer:null,authorizationBasis:'AWAITING_INDEPENDENT_EXACT_PARENT_STATIC_REVIEW',cases:[...ORIGINAL_ENTRIES],builds:builds.map(build=>({preparedOut:build.preparedOut,executableSha256:build.executable.sha256})),nativeToolsSha256:sha(path.join(REPO,'tools/fixtures/radial_private/native-tools.json')),nodeGraphSha256:states[0].toolProof.nodeGraphSha256,allowedBoundary:BOUNDARY,runtimeOsConfinementReviewed:false,runtimeRoot:manifest.runtimeRoot,runtimeVersion:manifest.runtimeVersion,runtimeFiles:manifest.runtimeFiles,sourcePins:sources,preparedManifestSha256:sha(path.join(short,'PREPARED.json'))};
  fs.writeFileSync(path.join(short,'PARENT-REVIEW-TEMPLATE.json'),JSON.stringify(template,null,2)+'\n',{flag:'wx'});
  const result={...base,status:PREPARED_STATUS,preparedManifest:pin(path.join(short,'PREPARED.json')),parentReviewTemplate:pin(path.join(short,'PARENT-REVIEW-TEMPLATE.json')),originalPrepareMode:prepared.originalPrepareMode,originalPositiveExecuted:false,originalNegativeExecuted:false,originalBudgets:ORIGINAL_BUDGETS,requiredEnvironment:['YMCC_MONITOR_RENDERER_PREPARED_ROOT','YMCC_RADIAL_EXECUTION_REVIEW','YMCC_RADIAL_EXECUTION_REVIEW_SHA256'],missingEvidence:['Independent parent exact static review with approved=true and runtimeOsConfinementReviewed=true','Current prepared positive36 / nine-controller native and private-launch receipts','Current exact frame-admission negative raw FAIL, four-check prefix, native exit2 and private-launch receipts','Subsequent central registration and current registered component run/audit by the parent','Full persistent parent recovery and full YMCC remain separate acceptance scopes']};
  save(path.join(root,'result.json'),result);return result;
 }catch(error){save(path.join(root,'result.json'),{...base,status:'FAIL',error:String(error.stack??error)});throw error;}
}

function reviewedPrepared(options){
 const value=options.preparedRoot??process.env.YMCC_MONITOR_RENDERER_PREPARED_ROOT;if(!value)blocked('MONITOR_RENDERER_PREPARED_ROOT_REQUIRED');
 const root=preparedRoot(value),file=path.join(root,'PREPARED.json');if(!fs.existsSync(file))blocked('ORIGINAL_RENDERER_PREPARED_INPUT_MISSING');
 const {file:reviewFile,pin:reviewPin}=rendererReviewBinding();
 if(!reviewFile||!reviewPin)blocked('NATIVE_PREEXECUTION_PARENT_REVIEW_REQUIRED');
 need(/^[a-f0-9]{64}$/.test(reviewPin)&&sha(absolute(reviewFile,'EXPLICIT_REVIEW_FILE_REQUIRED'))===reviewPin,'EXACT_PARENT_REVIEW_PIN_REQUIRED');noReparse(reviewFile);
 const review=read(reviewFile);
 if(review.approved!==true||review.runtimeOsConfinementReviewed!==true)blocked('INDEPENDENT_PARENT_APPROVAL_AND_RUNTIME_CONFINEMENT_REVIEW_REQUIRED');
 need(review.schemaVersion===1&&review.preparedManifestSha256===sha(file),'REVIEW_EXACT_RENDERER_PREPARED_MANIFEST_IDENTITY');
 need(typeof review.reviewer==='string'&&review.reviewer.trim().length>0&&Number.isFinite(Date.parse(review.reviewedAt)),'INDEPENDENT_PARENT_REVIEW_ATTRIBUTION_REQUIRED');
 const prepared=read(file);need(prepared.status===PREPARED_STATUS&&prepared.id===REAL_RENDERER_ID&&key(prepared.root)===key(root),'PREPARED_RENDERER_COMPONENT_IDENTITY');
 verifyRepo(prepared.sourceHashes);assert.deepEqual(review.sourcePins,prepared.sourceHashes,'REVIEW_EXACT_SOURCE_PINS');
 assert.deepEqual(review.cases,[...ORIGINAL_ENTRIES],'REVIEW_EXACT_ORIGINAL_CASE_PAIR');
 need(prepared.builds.length===2&&prepared.builds[0].kind==='positive'&&prepared.builds[1].kind==='frame-admission-negative','EXACT_ORIGINAL_BUILD_PAIR_REQUIRED');
 assert.deepEqual(review.builds,prepared.builds.map(build=>({preparedOut:build.preparedOut,executableSha256:build.executable.sha256})),'REVIEW_EXACT_ORIGINAL_BUILD_PAIR');
 need(review.allowedBoundary===BOUNDARY,'REVIEW_ORIGINAL_BOUNDARY');
 need(review.nativeToolsSha256===sha(path.join(REPO,'tools/fixtures/radial_private/native-tools.json'))&&prepared.toolProofs.every(proof=>review.nodeGraphSha256===proof.nodeGraphSha256),'REVIEW_TOOL_IDENTITY');
 assert.deepEqual(prepared.originalBudgets,ORIGINAL_BUDGETS,'ORIGINAL_BUDGETS_REQUIRED');
 const states=prepared.builds.map((build,index)=>{
  need(build.entry===ORIGINAL_ENTRIES[index]&&key(build.preparedOut)===key(path.join(root,index===0?'p':'r'))&&key(build.prepareRecord)===key(path.join(build.preparedOut,PREPARE_RECORDS[index])),'ORIGINAL_BUILD_LOCATION_IDENTITY');
  need(sha(build.prepareRecord)===build.prepareRecordSha256,'PREPARED_RECORD_CHANGED');
  const state=verifyRendererBuild(read(build.prepareRecord),build.preparedOut,index);assert.deepEqual(state.exe,build.executable,'PREPARED_EXECUTABLE_IDENTITY');return state;
 });
 need(sha(prepared.runtimeManifest.path)===prepared.runtimeManifest.sha256,'RUNTIME_MANIFEST_CHANGED');
 const runtime=verifyRuntime(read(prepared.runtimeManifest.path));
 for(const state of states){need(sha(state.runtimeManifest.path)===state.runtimeManifest.sha256,'BUILD_RUNTIME_MANIFEST_CHANGED');assert.deepEqual(read(state.runtimeManifest.path),runtime,'EXACT_RUNTIME_BUILD_PAIR');}
 need(key(review.runtimeRoot)===key(runtime.runtimeRoot)&&review.runtimeVersion===runtime.runtimeVersion,'REVIEW_RUNTIME_IDENTITY');assert.deepEqual(review.runtimeFiles,runtime.runtimeFiles,'REVIEW_EXACT_RUNTIME_MEMBERS');
 for(const row of prepared.preparedArtifacts)need(sha(row.path)===row.sha256,'PREPARED_ARTIFACT_CHANGED:'+row.path);
 need(unique([...prepared.preparedArtifacts,pin(file),pin(reviewFile)]).length<=LIMIT,'ORIGINAL_REVIEWED_COMBINED_LIMIT');
 return {root,file,prepared,runtime,reviewFile,reviewPin};
}

export async function runRealRendererComponent(options){
 need(key(process.execPath)===key(FIXED_NODE),'PINNED_NODE_REQUIRED');const root=freshEvidence(options.evidenceDir),sources=repoPins();let attempted=false;
 const base={schemaVersion:1,id:REAL_RENDERER_ID,lifecycleComponent:RENDERER_COMPONENT,sourceHashes:sources,fullPersistentAcceptance:false,fullPersistentRecoveryAcceptance:false,fullProductAcceptance:false,normalYmccStarted:false,operatingSystemSleep:false};
 try{
  const input=reviewedPrepared(options),{prepared,runtime}=input,p=prepared.builds[0].preparedOut,r=prepared.builds[1].preparedOut;
  attempted=true;await original(root,'p-execute',ORIGINAL_ENTRIES[0],['--out',p,'--runtime',runtime.runtimeRoot,'--runtime-version',runtime.runtimeVersion]);
  const positiveState=read(path.join(p,'REAL-RENDERER-EXECUTE-PROCESS.json'));
  need(positiveState.status==='ACTUAL_REAL_RENDERER_NINE_CONTROLLERS_RETIRED_LATE_CALLBACKS_AND_NATURAL_BROWSER_EXIT_VERIFIED','ORIGINAL_POSITIVE_EXECUTION_NOT_ACCEPTED');
  const positive=validateRealRendererResult(read(path.join(p,'real-webview-result.json')),read(path.join(p,'real-webview-private-launch.json')));
  await original(root,'r-execute',ORIGINAL_ENTRIES[1],['--out',r,'--runtime',runtime.runtimeRoot,'--runtime-version',runtime.runtimeVersion]);
  const negativeState=read(path.join(r,'REAL-RENDERER-NEGATIVE-EXECUTE-PROCESS.json'));
  need(negativeState.status==='EXACT_PRODUCT_RENDERER_FRAME_ADMISSION_NEGATIVE_FAILS_AT_ACTUAL_VUE_PRESENTATION','ORIGINAL_NEGATIVE_EXECUTION_NOT_ACCEPTED');
  assert.equal(negativeState.execution.code,2);assert.equal(negativeState.execution.signal,null);assert.equal(negativeState.execution.killed,false);
  const negative=validateRealRendererResult(read(path.join(r,'real-webview-result.json')),read(path.join(r,'real-webview-private-launch.json')),{negative:true});
  verifyRepo(prepared.sourceHashes);verifyRuntime(runtime);need(sha(input.reviewFile)===input.reviewPin,'PARENT_REVIEW_CHANGED_DURING_EXECUTION');
  for(const [index,build]of prepared.builds.entries()){need(sha(build.prepareRecord)===build.prepareRecordSha256,'PREPARED_RECORD_CHANGED_DURING_EXECUTION');verifyRendererBuild(read(build.prepareRecord),build.preparedOut,index);}
  const evidence=[];
  for(const [label,dir,processFile]of [['p',p,'REAL-RENDERER-EXECUTE-PROCESS.json'],['r',r,'REAL-RENDERER-NEGATIVE-EXECUTE-PROCESS.json']])for(const name of ['real-webview-result.json','real-webview-private-launch.json','run.log',processFile]){
   const target=path.join(root,label+'-'+name);fs.copyFileSync(path.join(dir,name),target,fs.constants.COPYFILE_EXCL);need(sha(target)===sha(path.join(dir,name)),'RAW_RECEIPT_COPY_CHANGED');evidence.push(pin(target));
  }
  const result={...base,status:'PASS',positive,negative,originalNegativeRawStatusPreserved:'FAIL',nativeLaunchAttempted:attempted,preparedManifest:pin(input.file),parentReview:pin(input.reviewFile),originalEvidence:evidence,runtimeIdentity:{root:runtime.runtimeRoot,version:runtime.runtimeVersion,fileCount:runtime.runtimeFiles.length},parentFeatureAcceptance:'REQUIRES_SEPARATE_CURRENT_FULL_PARENT_EXIT_AND_RECOVERY_EVIDENCE'};
  save(path.join(root,'result.json'),result);return result;
 }catch(error){const result={...base,status:error.blocked?'BLOCKED':'FAIL',error:String(error.stack??error),nativeLaunchAttempted:attempted,independentParentReviewRequired:true};save(path.join(root,'result.json'),result);error.componentResult=result;throw error;}
}
function parse(args){
 const options={},allowed=new Set(['mode','evidence-dir','runtime','runtime-version','prepared-root']);
 for(let index=0;index<args.length;index+=2){const name=args[index]?.slice(2);need(args[index]?.startsWith('--')&&allowed.has(name)&&args[index+1]&&!args[index+1].startsWith('--'),'INVALID_COMPONENT_ARGUMENT');const field=name.replace(/-([a-z])/g,(_,letter)=>letter.toUpperCase());need(!Object.hasOwn(options,field),'DUPLICATE_COMPONENT_ARGUMENT');options[field]=args[index+1];}
 options.mode??='run';need(['prepare','run'].includes(options.mode),'INVALID_COMPONENT_MODE');return options;
}
if(process.argv[1]&&key(process.argv[1])===key(fileURLToPath(import.meta.url))){
 try{const options=parse(process.argv.slice(2));const result=await(options.mode==='prepare'?prepareRealRendererComponent(options):runRealRendererComponent(options));console.log(JSON.stringify({id:REAL_RENDERER_ID,status:result.status,evidenceDir:options.evidenceDir,nativeLaunchAttempted:result.nativeLaunchAttempted}));}
 catch(error){console.error(error.stack);process.exitCode=error.blocked?10:2;}
}