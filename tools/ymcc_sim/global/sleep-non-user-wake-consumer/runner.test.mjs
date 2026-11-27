import test from 'node:test';import assert from 'node:assert/strict';import fs from'node:fs';import {getSleepNonUserWakeConsumerAdapters,inputFiles,REPO,MODULE}from'./adapter.mjs';
import {inspectNonUserWakeOwnedJob,runNonUserWake} from './run.mjs';
import path from 'node:path';
import vm from 'node:vm';
import {createHash} from 'node:crypto';
import {toolchainSpec,COMPILER_DIAGNOSTIC_ENVIRONMENT} from './adapter.mjs';
test('original script,three original plus one reviewed replay and actual source helpers are explicit',()=>{const [a]=getSleepNonUserWakeConsumerAdapters();for(const p of['tools/sleep_non_user_wake_selftest.py','native/main.cpp','native/sleep_retry_budget.h','native/fan_physical_wake.h'])assert(a.evidenceInputs.includes(p));assert.deepEqual(a.evidenceInputs.filter(x=>x.startsWith('tools/fixtures/sleep_non_user_wake/')&&x.endsWith('.json')).sort(),["tools/fixtures/sleep_non_user_wake/amd_power_button_wake.json","tools/fixtures/sleep_non_user_wake/amd_usb4_failure.json","tools/fixtures/sleep_non_user_wake/g3e_usb4_delayed_entry.json","tools/fixtures/sleep_non_user_wake/intel_usb4_failure.json","tools/fixtures/sleep_non_user_wake/win5_mouse_entry_failure.json"]);assert(a.artifactInputs.length<640);});
test('shared ownership and OS/compiler/Python boundaries are referenced from the frozen module',()=>{const files=inputFiles();for(const f of['scratch.mjs','boundary.h','sitecustomize.py','worker.mjs'])assert(files.includes('tools/ymcc_sim/global/sleep-native-recovery-consumer/'+f));const s=JSON.parse(fs.readFileSync(REPO+'/'+MODULE+'/spec.json','utf8'));assert.equal(s.ioReview.realSystemSleepFunctionActuallyExecuted,false);});
test('natural Job requires assigned child exit and structured zero before disposal',()=>{
 const limits={deadlineEpochMs:200000,cleanupReserveMs:10000};
 const job={status:0,error:null,signal:null,stdout:'YMCC_SIM_OWNED_JOB childPid=42'};
 const receipt={schemaVersion:1,status:'EXITED',exitCode:0,rootExitCode:0,deadlineExpired:false,
  jobAssigned:true,childPid:42,childExitedBeforeReturn:true,resourceObservationVersion:1,jobQueryCount:2,
  activeProcessesBeforeCleanup:0,activeProcessesFinal:0,zeroObservedBeforeDispose:true,jobDisposed:true,cleanupError:null,
  deadlineEpochMs:limits.deadlineEpochMs,cleanupReserveMs:limits.cleanupReserveMs,finishedEpochMs:190000,
  terminationRequested:false,terminationSucceeded:false};
 assert.equal(inspectNonUserWakeOwnedJob(job,receipt,limits).naturalExit,true);
 assert.equal(inspectNonUserWakeOwnedJob({...job,stdout:''},receipt,limits).naturalExit,true);
 assert.equal(inspectNonUserWakeOwnedJob(job,null,limits).naturalExit,false);
 for(const delta of [{schemaVersion:0},{jobAssigned:false},{childPid:null},{childExitedBeforeReturn:false},
  {resourceObservationVersion:0},{jobQueryCount:0},{activeProcessesFinal:1},{zeroObservedBeforeDispose:false},
  {jobDisposed:false},{cleanupError:'query-failed'},{deadlineEpochMs:200001},{cleanupReserveMs:0},
  {finishedEpochMs:200001},{deadlineExpired:true},{status:'TIMEOUT',exitCode:124,rootExitCode:null},
  {rootExitCode:2},{terminationRequested:true,terminationSucceeded:true}]){
  assert.equal(inspectNonUserWakeOwnedJob(job,{...receipt,...delta},limits).naturalExit,false,JSON.stringify(delta));
 }
 for(const delta of [{jobQueryCount:1},{activeProcessesBeforeCleanup:undefined},{activeProcessesBeforeCleanup:-1},
  {activeProcessesBeforeCleanup:1},{terminationRequested:undefined},{terminationSucceeded:undefined},
  {terminationRequested:false,terminationSucceeded:true},{terminationRequested:true,terminationSucceeded:true},
  {activeProcessesBeforeCleanup:1,terminationRequested:true,terminationSucceeded:false}]){
  const inconsistent=inspectNonUserWakeOwnedJob(job,{...receipt,...delta},limits);
  assert.equal(inconsistent.resourcesReturned,false,JSON.stringify(delta));
  assert.equal(inconsistent.naturalExit,false,JSON.stringify(delta));
 }
 const forced=inspectNonUserWakeOwnedJob(job,{...receipt,activeProcessesBeforeCleanup:1,terminationRequested:true,terminationSucceeded:true},limits);
 assert.equal(forced.resourcesReturned,true);assert.equal(forced.naturalExit,false);
 assert.equal(inspectNonUserWakeOwnedJob({...job,status:2},receipt,limits).naturalExit,false);
 assert.equal(inspectNonUserWakeOwnedJob({...job,error:{code:'ETIMEDOUT'}},receipt,limits).naturalExit,false);
 assert.equal(inspectNonUserWakeOwnedJob({...job,signal:'SIGTERM'},receipt,limits).naturalExit,false);
});

import {getJoinedSenderChoices,getJoinedSenderSelection} from './adapter.mjs';

function joinedSenderCatalog(){
 const source=fs.readFileSync(path.join(REPO,'tools/fixtures/sleep_non_user_wake/resume_sender_join.inc'),'utf8');
 const declarations=source.split(/\r?\n/).filter(line=>/^\s*run\(/.test(line));
 const names=declarations.map(line=>{
  const declaration=/^\s*run\("([a-z0-9-]+)",/.exec(line);
  assert(declaration,'Malformed original joined run declaration: '+line);return declaration[1];
 });
 assert.equal(names.length,133);assert.equal(new Set(names).size,names.length);
 return {source,declarations,names};
}

function memoryJoinedSelector(source){
 const reads=[],catalogPath=path.join(REPO,'tools/fixtures/sleep_non_user_wake/resume_sender_join.inc');
 const fakeFs={readFileSync(file,encoding){
  assert.equal(file,catalogPath);assert.equal(encoding,'utf8');reads.push(file);return source;
 }};
 const choices=vm.compileFunction('return ('+getJoinedSenderChoices.toString()+');',
  ['fs','path','REPO'])(fakeFs,path,REPO);
 const selection=vm.compileFunction('return ('+getJoinedSenderSelection.toString()+');',
  ['getJoinedSenderChoices','REPO'])(choices,REPO);
 return {choices:()=>choices(REPO),selection:value=>selection(REPO,value),reads};
}

test('joined sender choices and exact/default/from selections match every original run declaration',()=>{
 const {names}=joinedSenderCatalog();
 assert.deepEqual(getJoinedSenderChoices(),names);
 assert.deepEqual(getJoinedSenderSelection(),{requested:null,selectors:names});
 assert.deepEqual(getJoinedSenderSelection(REPO,null),{requested:null,selectors:names});
 for(const [index,name] of names.entries()){
  assert.deepEqual(getJoinedSenderSelection(REPO,name),{requested:name,selectors:[name]});
  assert.deepEqual(getJoinedSenderSelection(REPO,'from:'+name),
   {requested:'from:'+name,selectors:names.slice(index)});
 }
});

test('joined selectors reject malformed requests/catalogs and runner refuses arguments before evidence allocation',()=>{
 const {source,declarations,names}=joinedSenderCatalog();
 const valid=memoryJoinedSelector(source);
 assert.deepEqual(valid.choices(),names);
 assert.deepEqual(valid.selection(null),{requested:null,selectors:names});
 const unknown={message:'NONUSER_JOINED_SELECTION_UNKNOWN'};
 const invalid=['',' ','unknown-joined-case','from:','from:unknown-joined-case',
  'from:from:'+names[0],'FROM:'+names[0],' '+names[0],names[0]+' ',
  0,1,NaN,false,true,{},[],[names[0]],new String(names[0]),Symbol('joined'),1n,()=>{}];
 for(const [index,value] of invalid.entries()){
  assert.throws(()=>getJoinedSenderSelection(REPO,value),unknown,'Actual selection input '+index);
  assert.throws(()=>valid.selection(value),unknown,'Memory selection input '+index);
 }
 for(const [label,changed] of [
  ['duplicate',source.replace(declarations.at(-1),declarations[0])],
  ['missing',source.replace(declarations[0],'')],
  ['extra',source+'\n run("unexpected-joined-case",[]{});\n']
 ]){
  assert.notEqual(changed,source,label);
  const control=memoryJoinedSelector(changed),catalogError={message:'NONUSER_JOINED_CASE_CATALOG_CHANGED'};
  assert.throws(control.choices,catalogError,label+' choices');
  assert.throws(()=>control.selection(null),catalogError,label+' default');
  assert.throws(()=>control.selection(names[0]),catalogError,label+' exact');
  assert.throws(()=>control.selection('from:'+names[0]),catalogError,label+' suffix');
  assert.equal(control.reads.length,4,label+' must use the exact original FS port');
 }
 let evidenceCalls=0;const reached=Error('memory evidence boundary');
 const original=vm.compileFunction('return ('+runNonUserWake.toString()+');',
  ['need','getJoinedSenderSelection','REPO','evidencePath'])(
   (ok,code)=>{if(!ok)throw Error(code);},getJoinedSenderSelection,REPO,
   ()=>{++evidenceCalls;throw reached;});
 const argumentsError={message:'NATIVE_RECOVERY_ARGUMENTS'};
 assert.throws(()=>original('unused',{resumeSenderCase:names[0]}),argumentsError);
 const flags=['prototype','resumeClassificationOnly','resumeClassificationEdgeOnly',
  'resumeClassificationLifetimeOnly','resumeClassificationPublicationOnly','resumeClassificationSenderOnly'];
 for(let first=0;first<flags.length;first++)for(let second=first+1;second<flags.length;second++){
  assert.throws(()=>original('unused',{[flags[first]]:true,[flags[second]]:true,resumeSenderCase:names[0]}),
   argumentsError,flags[first]+' + '+flags[second]);
 }
 for(const value of invalid)assert.throws(()=>original('unused',
  {resumeClassificationSenderOnly:true,resumeSenderCase:value}),unknown);
 assert.equal(evidenceCalls,0,'Rejected arguments reached evidencePath');
 for(const value of [null,names[0],'from:'+names[0]])assert.throws(()=>original('unused',
  {resumeClassificationSenderOnly:true,resumeSenderCase:value}),error=>error===reached);
 assert.equal(evidenceCalls,3,'Valid controls did not reach the original evidence boundary');
});

// Execute the original selector against exact in-memory manifests and FS ends.
// No installed tool, historical evidence or alternate selection algorithm is used.
function memoryDiagnosticToolchain({uiState='ENOENT',editOwn=()=>{},editShared=()=>{}}={}){
 const repo=path.resolve('memory-diagnostic-toolchain');
 const sha=value=>createHash('sha256').update(value).digest('hex');
 const compiler={path:path.join(repo,'tools','cl.exe'),sha256:sha('compiler')};
 const ui={path:path.join(repo,'tools','2052','clui.dll'),sha256:sha('reviewed-ui')};
 const unknown={path:path.join(repo,'tools','unknown.dll'),sha256:sha('unknown-input')};
 const shared={nodeExecutable:process.execPath,files:[compiler,ui,unknown]};
 editShared(shared,{compiler,ui,unknown});
 const sharedBytes=Buffer.from(JSON.stringify(shared));
 const own={sharedManifest:'shared-tools.json',sharedManifestSha256:sha(sharedBytes),additional:[],
  reviewedAbsentOptionalInputs:[{...ui,reviewedState:'absent',reason:'Reviewed optional UI; owned compiler diagnostics use English.',requiredVSLANG:'1033'}]};
 editOwn(own,{compiler,ui,unknown});
 const ownPath=path.join(repo,MODULE,'toolchain-inputs.json'),sharedPath=path.join(repo,own.sharedManifest);
 const statCalls=[];
 const accessError=Object.assign(Error('reviewed UI access denied'),{code:'EACCES'});
 const fakeFs={
  readFileSync(file,encoding){
   const bytes=file===ownPath?Buffer.from(JSON.stringify(own)):file===sharedPath?sharedBytes:null;
   assert(bytes,'Unexpected selector read: '+file);return encoding?bytes.toString(encoding):bytes;
  },
  statSync(file){
   statCalls.push(file);assert.equal(file,ui.path,'Only the reviewed tuple may probe absence');
   if(uiState==='present')return {isFile:()=>true};
   if(uiState==='EACCES')throw accessError;
   throw Object.assign(Error('reviewed UI absent'),{code:uiState});
  }
 };
 const dependencies={fs:fakeFs,path,createHash,REPO:repo,MODULE,process,COMPILER_DIAGNOSTIC_ENVIRONMENT};
 const keys=Object.keys(dependencies);
 const select=vm.compileFunction('return ('+toolchainSpec.toString()+');',keys)(...keys.map(key=>dependencies[key]));
 return {select:()=>select(repo),compiler,ui,unknown,statCalls,accessError};
}

// Stop the actual runner immediately after its original source/tool SHA gates.
// A successful preflight never allocates scratch or starts a child/compiler.
function originalToolPreflight(tool,{hashes=new Map(),errors=new Map()}={}){
 const seen=[],reached=Error('memory preflight complete');
 const original={path:'original.py',sha256:'original-source'};
 const spec={originalScript:original,originalFixtures:[],additionalFixtures:[],reviewedSharedLeafIdentities:[]};
 const pins=new Map(tool.files.map(row=>[row.path,row.sha256]));
 pins.set(path.join(REPO,original.path),original.sha256);
 const dependencies={evidencePath:()=>path.resolve('memory-preflight'),read:()=>spec,REPO,MODULE,path,
  process:{execPath:process.execPath},toolchainSpec:()=>tool,
  need:(ok,code)=>{if(!ok)throw Error(code);},
  hash:file=>{seen.push(file);if(errors.has(file))throw errors.get(file);assert(pins.has(file),'Unexpected preflight read: '+file);return hashes.has(file)?hashes.get(file):pins.get(file);},
  verifyPublicWakeSource:()=>({source:{directHash:relative=>pins.get(path.join(REPO,relative)),verifyOriginalSourceReview:verify=>verify()},launch:()=>({receipt:null}),receipt:null}),
  verifyCurrentSleepSourceReview:()=>({status:'MEMORY_PORT_ONLY'}),fs:{mkdirSync(){throw reached;}}};
 const keys=Object.keys(dependencies);
 const run=vm.compileFunction('return ('+runNonUserWake.toString()+');',keys)(...keys.map(key=>dependencies[key]));
 return {run:()=>run('memory-preflight'),seen,reached};
}

test('compiler diagnostic selection is owned English for every parent VSLANG and leaves parent environment intact',()=>{
 assert.deepEqual(COMPILER_DIAGNOSTIC_ENVIRONMENT,{VSLANG:'1033'});
 assert(Object.isFrozen(COMPILER_DIAGNOSTIC_ENVIRONMENT));
 const before={...process.env},had=Object.hasOwn(process.env,'VSLANG'),saved=process.env.VSLANG;
 try{
  for(const value of [undefined,'1033','2052','arbitrary-parent-language']){
   if(value===undefined)delete process.env.VSLANG;else process.env.VSLANG=value;
   const parent={...process.env},control=memoryDiagnosticToolchain(),selected=control.select();
   assert.deepEqual(selected.files,[control.compiler,control.unknown],String(value));
   assert.deepEqual(selected.omittedReviewedOptionalInputs.map(row=>row.path),[control.ui.path]);
   assert.strictEqual(selected.compilerDiagnosticEnvironment,COMPILER_DIAGNOSTIC_ENVIRONMENT);
   assert.deepEqual({...process.env},parent,'Selector mutated parent environment: '+String(value));
   assert.deepEqual(control.statCalls,[control.ui.path]);
  }
 }finally{if(had)process.env.VSLANG=saved;else delete process.env.VSLANG;}
 assert.deepEqual({...process.env},before,'Control did not restore its own parent environment');
});

test('restored reviewed UI and unknown missing inputs retain the original runner SHA preflight',()=>{
 const restored=memoryDiagnosticToolchain({uiState:'present'}),selected=restored.select();
 assert.deepEqual(selected.files,[restored.compiler,restored.ui,restored.unknown]);
 assert.deepEqual(selected.omittedReviewedOptionalInputs,[]);
 const positive=originalToolPreflight(selected);
 assert.throws(positive.run,error=>error===positive.reached);
 assert(positive.seen.includes(restored.ui.path),'Restored UI bypassed original SHA preflight');
 const changed=originalToolPreflight(selected,{hashes:new Map([[restored.ui.path,'0'.repeat(64)]])});
 assert.throws(changed.run,{message:'NATIVE_RECOVERY_TOOL_CHANGED:'+restored.ui.path});
 assert(!changed.seen.includes(restored.unknown.path),'Preflight continued after restored UI hash refusal');
 const absent=memoryDiagnosticToolchain(),withUnknown=absent.select();
 assert(withUnknown.files.some(row=>row.path===absent.unknown.path));
 assert(!withUnknown.omittedReviewedOptionalInputs.some(row=>row.path===absent.unknown.path));
 const missing=Object.assign(Error('unknown input absent'),{code:'ENOENT'});
 const refused=originalToolPreflight(withUnknown,{errors:new Map([[absent.unknown.path,missing]])});
 assert.throws(refused.run,error=>error===missing);
 assert(refused.seen.includes(absent.unknown.path),'Unknown input never reached original preflight');
});

test('optional diagnostic omission refuses access errors, bad review tuples and changed manifest hashes',()=>{
 const denied=memoryDiagnosticToolchain({uiState:'EACCES'});
 assert.throws(denied.select,error=>error===denied.accessError);
 const otherError=memoryDiagnosticToolchain({uiState:'ENOTDIR'});
 assert.throws(otherError.select,{code:'ENOTDIR'});
 for(const [label,edit] of [
  ['unreviewed path',(own,{unknown})=>{own.reviewedAbsentOptionalInputs[0].path=unknown.path+'-unreviewed';}],
  ['wrong original hash',own=>{own.reviewedAbsentOptionalInputs[0].sha256='0'.repeat(64);}],
  ['missing reason',own=>{own.reviewedAbsentOptionalInputs[0].reason='';}],
  ['wrong reviewed state',own=>{own.reviewedAbsentOptionalInputs[0].reviewedState='present';}],
  ['duplicate tuple',own=>{own.reviewedAbsentOptionalInputs.push({...own.reviewedAbsentOptionalInputs[0]});}]
 ])assert.throws(memoryDiagnosticToolchain({editOwn:edit}).select,/^Error: NONUSER_OPTIONAL_TOOL_REVIEW_MISMATCH:/,label);
 const wrongLanguage=memoryDiagnosticToolchain({editOwn:own=>{own.reviewedAbsentOptionalInputs[0].requiredVSLANG='2052';}});
 assert(wrongLanguage.select().files.some(row=>row.path===wrongLanguage.ui.path),'Wrong declared language received omission');
 assert.throws(memoryDiagnosticToolchain({editOwn:own=>{own.sharedManifestSha256='0'.repeat(64);}}).select,
  {message:'NONUSER_SHARED_TOOL_MANIFEST_CHANGED'});
 assert.throws(memoryDiagnosticToolchain({editShared:(shared,{compiler})=>{shared.files.push({...compiler,sha256:'0'.repeat(64)});}}).select,
  /^Error: NONUSER_INHERITED_TOOL_HASH_CONFLICT:/);
 assert.throws(memoryDiagnosticToolchain({editOwn:(own,{compiler})=>{own.additional.push({...compiler,sha256:'0'.repeat(64)});}}).select,
  /^Error: NONUSER_TOOL_HASH_CONFLICT:/);
 for(const delta of [{sha256:'bad-hash'},{priorSha256:'0'.repeat(64)},{reviewScope:'unreviewed'}]){
  const control=memoryDiagnosticToolchain({editOwn:(own,{compiler})=>{
   own.reviewedCurrentToolOverrides=[{path:compiler.path,priorSha256:compiler.sha256,sha256:'f'.repeat(64),
    reviewScope:'current-installed-software-validation-tool-only',...delta}];
  }});
  assert.throws(control.select,/^Error: NONUSER_CURRENT_TOOL_REVIEW_MISMATCH:/);
 }
});
test('original runner binds receipt parameters and retains unproven or forced resources',()=>{
 // Execute the original runner body with memory-only filesystem/process ends.
 // This tests composition and archive/cleanup decisions, not worker behavior.
 for(const scenario of ['normal','missing','unassigned','zero-unproven','forced','initial-active-but-not-reclaimed','one-query','forced-but-initially-empty']){
  const files=new Map(),counts={archive:0,cleanup:0};
  const root=path.resolve('memory-sleep-receipt'),scratch=path.join(root,'scratch');
  const fakeFs={mkdirSync(){},existsSync:file=>files.has(file),writeFileSync:(file,value)=>files.set(file,String(value))};
  const write=(file,value)=>files.set(file,JSON.stringify(value));
  const spec={originalScript:{path:'original.py',sha256:'source'},originalFixtures:[],additionalFixtures:[],reviewedSharedLeafIdentities:[],ioReview:{actualFunctions:1},expectedProfiles:{s3:1,capsUnknown:1,main:1}};
  files.set(path.join(REPO,MODULE,'spec.json'),JSON.stringify(spec));
  files.set(path.join(scratch,'worker-result.json'),JSON.stringify({status:'PASS',facts:{}}));
  const read=file=>JSON.parse(files.get(file));
  let actualArguments=null;
  const spawnSync=(_program,args)=>{
   actualArguments=args;
   const deadline=Number(args[args.indexOf('-DeadlineEpochMs')+1]);
   const reserve=Number(args[args.indexOf('-CleanupReserveMs')+1]);
   const file=args[args.indexOf('-DeadlineReceiptPath')+1];
   const receipt={schemaVersion:1,status:'EXITED',exitCode:0,rootExitCode:0,deadlineExpired:false,
    jobAssigned:scenario!=='unassigned',childPid:42,childExitedBeforeReturn:true,
    resourceObservationVersion:1,jobQueryCount:scenario==='one-query'?1:2,activeProcessesFinal:0,
    activeProcessesBeforeCleanup:['forced','initial-active-but-not-reclaimed'].includes(scenario)?1:0,
    zeroObservedBeforeDispose:scenario!=='zero-unproven',jobDisposed:true,cleanupError:null,
    deadlineEpochMs:deadline,cleanupReserveMs:reserve,finishedEpochMs:Date.now(),
    terminationRequested:['forced','forced-but-initially-empty'].includes(scenario),terminationSucceeded:['forced','forced-but-initially-empty'].includes(scenario)};
   if(scenario!=='missing')write(file,receipt);
   return {status:0,error:null,signal:null,stdout:'YMCC_SIM_OWNED_JOB childPid=42',stderr:''};
  };
  const dependencies={evidencePath:()=>root,read,REPO,MODULE,CASE_ID:'sleep.non-user-wake-consumer',
   toolchainSpec:()=>({nodeExecutable:process.execPath,powershellExecutable:'memory-pwsh',files:[]}),
   need:(value,code)=>{if(!value)throw Error(code);},hash:()=> 'source',path,
   process:{execPath:process.execPath,env:{}},verifyCurrentSleepSourceReview:()=>({status:'MEMORY_PORT_ONLY'}),
   verifyPublicWakeSource:()=>({source:{directHash:()=> 'source',verifyOriginalSourceReview:()=>({status:'MEMORY_PORT_ONLY'})},launch:()=>({receipt:{status:'MEMORY_PORT_ONLY'}}),receipt:{status:'MEMORY_PORT_ONLY'}}),
   fs:fakeFs,captureInputs:()=>({sources:[],selection:'legacy'}),allocateScratch:()=>({markerSha256:'marker'}),
   write,prepareExecution:()=>scratch,spawnSync,verifyScratch:()=>{},verifyExecutedArtifacts:()=>{},verifyOwnedPhases:()=>{},
   inspectNonUserWakeOwnedJob,archiveScratch:()=>{++counts.archive;return{files:[]};},
   cleanupScratch:()=>{++counts.cleanup;return{cleaned:true};},walk:()=>[],console:{log(){}}};
  const keys=Object.keys(dependencies);
  const original=vm.compileFunction('return ('+runNonUserWake.toString()+');',keys)(...keys.map(key=>dependencies[key]));
  const result=original(root);
  assert(actualArguments.includes('-DeadlineReceiptPath'));assert(actualArguments.includes('-CleanupReserveMs'));
  assert.equal(result.passed,scenario==='normal',scenario);
  assert.equal(counts.cleanup,scenario==='normal'?1:0,scenario);
  assert.equal(counts.archive,scenario==='normal'||scenario==='forced'?1:0,scenario);
  assert.equal(result.boundary.ownedWindowsJob,scenario!=='missing'&&scenario!=='unassigned',scenario);
  assert.equal(result.executionArchive.archivePerformed,scenario==='normal'||scenario==='forced',scenario);
  if(scenario!=='normal'){assert(result.workerError);assert(result.remainingGaps.length>1);}
 }
});

test('joined build reference context relocates owned scratch, retains drift and refuses invalid original authorities',async t=>{
 const {pinJoinedBuildReference,joinedBuildEnvironment,joinedBuildContext,joinedCompilationSourceIdentity,verifyJoinedBuildReference}=await import('./joined-build-reference.mjs');
 const roleRows=[{path:MODULE+'/README.md',sha256:'1'.repeat(64)},
  {path:MODULE+'/runner.test.mjs',sha256:'2'.repeat(64)},
  {path:'native/main.cpp',sha256:'3'.repeat(64)},
  {path:MODULE+'/worker.mjs',sha256:'4'.repeat(64)}];
 const originalRoles=joinedCompilationSourceIdentity(roleRows);
 assert.deepEqual(originalRoles.nonCompilation.map(row=>row.path).sort(),[MODULE+'/README.md',MODULE+'/runner.test.mjs'].sort());
 for(const selected of [[0],[1],[0,1]]){
  const changed=roleRows.map((row,index)=>selected.includes(index)?{...row,sha256:'5'.repeat(64)}:row);
  assert.deepEqual(joinedCompilationSourceIdentity(changed).compilation,originalRoles.compilation);
  assert.notDeepEqual(joinedCompilationSourceIdentity(changed).all,originalRoles.all,'Complete source evidence still distinguishes changed test/document bytes');
 }
 for(const index of [2,3]){const changed=roleRows.map((row,i)=>i===index?{...row,sha256:'6'.repeat(64)}:row);assert.notDeepEqual(joinedCompilationSourceIdentity(changed).compilation,originalRoles.compilation);}
 for(const suffix of ['other.test.mjs','other.md']){
  const expanded=joinedCompilationSourceIdentity([...roleRows,{path:MODULE+'/'+suffix,sha256:'7'.repeat(64)}]);
  assert(expanded.compilation.some(row=>row.path===MODULE+'/'+suffix),'No generic test/Markdown extension exemption');
 }
 for(const malformed of [roleRows.slice(1),[...roleRows,roleRows[0]],roleRows.map((row,index)=>index===0?{...row,path:'../README.md'}:row),roleRows.map((row,index)=>index===0?{...row,path:MODULE+'/readme.md'}:row)])
  assert.throws(()=>joinedCompilationSourceIdentity(malformed),/^Error: NONUSER_BUILD_REFERENCE_(?:SOURCE_MEMBERSHIP|NONCOMPILATION_ROLE_MISSING)$/);
 const controlBase=path.resolve(REPO,'../../../Testing/YMCC/Workspace/SleepJoinedBuildReferenceControls');
 fs.mkdirSync(controlBase,{recursive:true});
 const controlRoot=fs.mkdtempSync(path.join(controlBase,'runner-context-'));
 t.diagnostic('Pure joined build reference controls retained at '+controlRoot);
 const firstRoot=path.join(controlRoot,'owned-a'),secondRoot=path.join(controlRoot,'owned-b');
 const fixedTools=path.join(controlRoot,'fixed-tools');
 const environmentFor=root=>({
  PATH:[path.join(fixedTools,'compiler'),path.join(fixedTools,'sdk')].join(path.delimiter),
  ComSpec:path.join(fixedTools,'cmd.exe'),ToolPath:path.join(fixedTools,'cl.exe'),
  CL:'/FI"'+path.join(root,'input-sources/boundary.h')+'" /sourceDependencies "'+path.join(root,'resume-sender/original-output/deps')+'" /FC',
  LINK:'/verbose:lib /LIBPATH:"'+path.join(fixedTools,'lib')+'"',
  TEMP:path.join(root,'resume-sender/original-output/temp'),TMP:path.join(root,'resume-sender/original-output/temp'),
  PYTHONPATH:path.join(root,'input-sources/python-guard'),
  YMCC_SLEEP_PY_CONFIG:path.join(root,'resume-sender/python-config.json'),
  YMCC_SLEEP_NATIVE_ROOT:path.join(root,'resume-sender/original-output'),
  VSLANG:'1033',UNRELATED:'unchanged',EMPTY:''
 });
 const recipeFor=root=>[
  '@echo off','call "'+path.join(fixedTools,'vcvars64.bat')+'" >nul',
  'cl /nologo /std:c++20 /EHsc /O1 "'+path.join(root,'resume-sender/original-output/joined.cpp')+'" /Fe"'+path.join(root,'resume-sender/original-output/joined.exe')+'"',
  'exit /b %errorlevel%'
 ];
 const environment=environmentFor(firstRoot),batchLines=recipeFor(firstRoot);
 const inherited={...environment,CODEX_TOOL_CALL_ID:'first-owned-shell',UNREVIEWED_CUSTOM_INPUT:'retained'},beforeEnvironment={...inherited};
 const childEnvironment=joinedBuildEnvironment(inherited);
 assert.deepEqual(inherited,beforeEnvironment,'Preparing the owned environment does not mutate its caller');
 assert(!Object.keys(childEnvironment).some(name=>/^CODEX_TOOL_CALL_ID$/i.test(name)),'Exact routing metadata is absent from the actual owned child environment');
 assert.equal(childEnvironment.UNREVIEWED_CUSTOM_INPUT,'retained','Unknown environment inputs remain actual child inputs');
 assert.deepEqual(childEnvironment,joinedBuildEnvironment({...inherited,CODEX_TOOL_CALL_ID:'later-owned-shell'}),'Per-call routing metadata is removed rather than ignored in a fingerprint');
 assert.deepEqual(joinedBuildEnvironment({...inherited,codex_tool_call_id:'case-alias'}),childEnvironment,'Every Windows-case spelling of the exact routing variable is removed');
 const context=joinedBuildContext({root:firstRoot,environment,batchLines});
 assert.deepEqual(joinedBuildContext({root:secondRoot,environment:environmentFor(secondRoot),batchLines:recipeFor(secondRoot)}),context,
  'Exact owned scratch roles must relocate together');
 const reordered=Object.fromEntries(Object.entries(environment).reverse().map(([name,value])=>[name.toLowerCase(),value]));
 assert.deepEqual(joinedBuildContext({root:firstRoot,environment:reordered,batchLines}),context,
  'Environment key case and insertion order do not change its Windows identity');
 for(const [label,changed] of [
  ['unrelated value',{...environment,UNRELATED:'changed'}],
  ['additional variable',{...environment,EXTRA_UNRELATED:'present'}],
  ['removed variable',Object.fromEntries(Object.entries(environment).filter(([name])=>name!=='EMPTY'))],
  ['CL option',{...environment,CL:environment.CL+' /DREFERENCE_CONTROL=1'}],
  ['tool search path',{...environment,PATH:environment.PATH+path.delimiter+path.join(fixedTools,'other')}],
  ['tool path value',{...environment,ToolPath:path.join(fixedTools,'other-cl.exe')}],
  ['command interpreter',{...environment,ComSpec:path.join(fixedTools,'other-cmd.exe')}]
 ])assert.notDeepEqual(joinedBuildContext({root:firstRoot,environment:changed,batchLines}),context,label);
 assert.notDeepEqual(joinedBuildContext({root:firstRoot,environment,batchLines:batchLines.map(line=>line.replace('/O1','/Od'))}),context,
  'Recipe options remain part of build identity');
 assert.notDeepEqual(
  joinedBuildContext({root:firstRoot,environment:{...environment,UNRELATED:firstRoot},batchLines}),
  joinedBuildContext({root:secondRoot,environment:{...environmentFor(secondRoot),UNRELATED:secondRoot},batchLines:recipeFor(secondRoot)}),
  'Unrelated environment values must not receive owned path normalization');
 assert.notDeepEqual(
  joinedBuildContext({root:firstRoot,environment:{...environment,CL:environment.CL+' /I"'+firstRoot+'-peer"'},batchLines}),
  joinedBuildContext({root:secondRoot,environment:{...environmentFor(secondRoot),CL:environmentFor(secondRoot).CL+' /I"'+secondRoot+'-peer"'},batchLines:recipeFor(secondRoot)}),
  'A sibling path sharing the scratch prefix is an external CL input');
 assert.notDeepEqual(
  joinedBuildContext({root:firstRoot,environment,batchLines:[...batchLines,'call "'+firstRoot+'-peer/tool.cmd"']}),
  joinedBuildContext({root:secondRoot,environment:environmentFor(secondRoot),batchLines:[...recipeFor(secondRoot),'call "'+secondRoot+'-peer/tool.cmd"']}),
  'A sibling recipe path must not receive owned scratch normalization');
 for(const name of ['PATH','CL','ToolPath','TEMP'])assert.throws(()=>joinedBuildContext({root:firstRoot,
  environment:{...environment,[name.toLowerCase()]:environment[name]},batchLines}),
  {message:'NONUSER_BUILD_REFERENCE_ENVIRONMENT'},'Duplicate case-insensitive environment name '+name);

 // These are synthetic refusal inputs, not a replacement original graph or a
 // successful compilation/scenario receipt. All writes belong to this fresh root.
 const file=path.join(controlRoot,'result.json');
 const original={schemaVersion:1,id:'sleep.non-user-wake-consumer',status:'FAIL',passed:true,
  executionSelection:'resume-classification-sender-only',prototypeOnly:false};
 const originalBytes=Buffer.from(JSON.stringify(original)+'\n');
 fs.writeFileSync(file,originalBytes,{flag:'wx'});
 assert.deepEqual(pinJoinedBuildReference(file),{path:file,bytes:originalBytes.length,
  sha256:createHash('sha256').update(originalBytes).digest('hex')});
 const refusal=error=>error instanceof Error&&/^NONUSER_BUILD_REFERENCE_/.test(error.message);
 assert.throws(()=>pinJoinedBuildReference(path.relative(process.cwd(),file)),refusal,'Relative authority');
 const wrongName=path.join(controlRoot,'RESULT-not-original.json');
 fs.writeFileSync(wrongName,originalBytes,{flag:'wx'});
 assert.throws(()=>pinJoinedBuildReference(wrongName),refusal,'Wrong authority basename');
 assert.throws(()=>pinJoinedBuildReference(path.join(controlRoot,'missing/result.json')),refusal,'Missing authority');
 const reparseTarget=path.join(controlRoot,'reparse-target');fs.mkdirSync(reparseTarget);
 fs.writeFileSync(path.join(reparseTarget,'result.json'),originalBytes,{flag:'wx'});
 const reparse=path.join(controlRoot,'authority-junction');
 fs.symlinkSync(reparseTarget,reparse,process.platform==='win32'?'junction':'dir');
 assert(fs.lstatSync(reparse).isSymbolicLink(),'Reparse control was not created');
 assert.throws(()=>pinJoinedBuildReference(path.join(reparse,'result.json')),refusal,'Reparse authority ancestor');
 const snapshot={sources:[],tool:{files:[]}};
 for(const [index,delta] of [{status:'FAIL'},{status:'UNKNOWN'},{status:'NOT_RUN'},
  {status:'BUILD_REFERENCE'},{status:'PASS',passed:false},{status:'PASS',prototypeOnly:true}].entries()){
  const fakeRoot=path.join(controlRoot,'non-pass-'+index);fs.mkdirSync(fakeRoot);
  const fake=path.join(fakeRoot,'result.json');fs.writeFileSync(fake,JSON.stringify({...original,...delta}),{flag:'wx'});
  const authority=pinJoinedBuildReference(fake);
  assert.throws(()=>verifyJoinedBuildReference(authority,{snapshot}),
   {message:'NONUSER_BUILD_REFERENCE_ORIGINAL_RESULT'},'Synthetic unqualified original '+index);
 }

 // Exercise the exact current private reader, not a second implementation.
 // These owned files model read/role races only; complete old graph refusal is
 // separately covered by the actual archive controls in the task evidence.
 const referenceSource=fs.readFileSync(path.join(REPO,MODULE,'joined-build-reference.mjs'),'utf8');
 const readerStart=referenceSource.indexOf(' const version=file=>{'),readerEnd=referenceSource.indexOf(' const json=',readerStart);
 assert(readerStart>0&&readerEnd>readerStart,'Exact reference bytes reader anchors');
 const readerFactory=vm.compileFunction(referenceSource.slice(readerStart,readerEnd)+'\nreturn bytes;',
  ['fs','path','digest','need','noReparse','memo']);
 const readerOriginalBytes=Buffer.from('unchanged-version-proof'),readerChangedBytes=Buffer.from('different-version-proof');
 const digest=bytes=>createHash('sha256').update(bytes).digest('hex'),expectedDigest=digest(readerOriginalBytes);
 for(const mode of ['stable','after-read','between-roles','touch-only','persistent-churn']){
  const file=path.join(controlRoot,'reader-'+mode+'.bin');fs.writeFileSync(file,readerOriginalBytes,{flag:'wx'});
  const baseline=new Date('2026-11-12T00:00:00Z');fs.utimesSync(file,baseline,baseline);
  let reads=0,mutations=0;
  const proxy=new Proxy(fs,{get(object,name){if(name!=='readFileSync')return Reflect.get(object,name);return function(target,...args){
   const bytes=object.readFileSync(target,...args);if(path.resolve(target)!==file)return bytes;
   reads++;
   if((mode==='after-read'||mode==='touch-only'||mode==='persistent-churn')&&(reads===1||mode==='persistent-churn')){
    mutations++;if(mode!=='touch-only')fs.writeFileSync(file,readerChangedBytes);
    fs.utimesSync(file,baseline,new Date(baseline.getTime()+mutations*2000));
   }
   return bytes;
  };}});
  const need=(ok,code)=>{if(!ok)throw Error('NONUSER_BUILD_REFERENCE_'+code);};
  const bytes=readerFactory(proxy,path,digest,need,target=>assert(!fs.lstatSync(target).isSymbolicLink()),new Map());
  if(mode==='after-read')assert.throws(()=>bytes(file,expectedDigest),/MEMBER_CHANGED:/);
  else if(mode==='persistent-churn'){assert.throws(()=>bytes(file,expectedDigest),/MEMBER_CHANGED_DURING_REFERENCE:/);assert.equal(reads,2,'Unstable metadata retries are bounded');}
  else{
   assert(bytes(file,expectedDigest).equals(readerOriginalBytes));
   if(mode==='between-roles'){fs.writeFileSync(file,readerChangedBytes);fs.utimesSync(file,baseline,new Date(baseline.getTime()+4000));assert.throws(()=>bytes(file,expectedDigest),/MEMBER_CHANGED:/);}
   else{assert(bytes(file,expectedDigest).equals(readerOriginalBytes));assert.equal(reads,mode==='stable'?1:2,'Only unchanged version roles share a byte read');}
  }
 }
});
