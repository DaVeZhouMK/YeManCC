#!/usr/bin/env node
// Invoke the existing complete native TU consumers; no lifecycle algorithm lives here.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawn} from 'node:child_process';
import {REPO,ID,sourceInputs,artifactInputs} from './adapter.mjs';
const args=process.argv.slice(2);
const usage='Use --out ABS_NEW_DIRECTORY [--compile-out ABS_NEW_DIRECTORY]. Runs current pending+publication in one compiled candidate, then five current compiled rollback controls, at most two test compilers concurrently.';
if(args.length===1&&args[0]==='--help'){console.log(usage);process.exit(0);}
if(![2,4].includes(args.length)||args[0]!=='--out'||!path.isAbsolute(args[1])||(args.length===4&&(args[2]!=='--compile-out'||!path.isAbsolute(args[3]))))throw Error(usage);
const out=path.resolve(args[1]);if(fs.existsSync(out))throw Error('Fresh out required');fs.mkdirSync(out,{recursive:true});
// The adapter supplies a short sibling under this same evidence run for MSVC.
const compileOut=path.resolve(args[3]??path.join(out,'tu'));if(fs.existsSync(compileOut))throw Error('Fresh compile-out required');fs.mkdirSync(compileOut,{recursive:true});
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const fact=p=>{const b=fs.readFileSync(p);return{path:path.resolve(p),sha256:sha(b),bytes:b.length};};
const deadline=Date.now()+870000;
const checks=[],runs=[],rollbacks=[],startedAt=new Date().toISOString();let before=[],tools=[],error=null;
function check(assertion,ok,detail={}){checks.push({assertion,passed:ok===true,...detail});}
function equalSet(a,b){return a.length===b.length&&new Set(a).size===a.length&&[...a].sort().every((v,i)=>v===[...b].sort()[i]);}
const tokenFailures=['empty-token0 worker preserves newly enqueued token1 pending','new token1 episode is consumed and completed exactly once'];
const chainFailures=['empty leftover invokes no second hide','empty leftover preserves the completed real episode','completed real episode enqueue is refused','same real episode is never hidden twice'];
const publicationFailures=['accepted MSI late-hide work retains a drainable notification or completed receipt','accepted MSI notification executes without a second producer'];
const inflightFailures=['same in-flight MSI episode reuses the worker token without enqueueing another hide','one MSI source episode has exactly one actual successful hide after in-flight BUS reentry'];
const orphanFailures=['retained late-hide marker without a running owner permits a fresh BUS notification','a new actual worker completes the episode after the diagnostic exception'];
const modeCounts={'late-hide-pending':{cases:19,checks:75},'late-hide-publication':{cases:1,checks:7},'late-hide-inflight':{cases:1,checks:8},'late-hide-inflight-edges':{cases:13,checks:33},'late-hide-orphaned-inflight':{cases:1,checks:3}};
function replaceOnce(bytes,needle,replacement,label){const token=Buffer.from(needle),at=bytes.indexOf(token);if(at<0||bytes.indexOf(token,at+token.length)>=0)throw Error('CURRENT_ROLLBACK_ANCHOR_NOT_UNIQUE: '+label);return Buffer.concat([bytes.subarray(0,at),Buffer.from(replacement),bytes.subarray(at+token.length)]);}
function prepareRollbacks(){
 const mainPath=path.join(REPO,'native/main.cpp'),headerPath=path.join(REPO,'native/msi_late_hide_pending.h');
 const main=fs.readFileSync(mainPath),header=fs.readFileSync(headerPath);
 if(!header.includes(Buffer.from('inline constexpr bool buildEnabled=true;')))throw Error('CURRENT_PENDING_POLICY_NOT_ENABLED');
 const cleanup=/if\s*\(ymcc::msi::latehide::mayComplete\(lateHideMsiPendingRequired,\s*lateHideHasPending\)\)\s*(?=inputHostLateHidePendingClearIfToken\(lateHideToken\);)/g;
 const matches=[...main.toString('utf8').matchAll(cleanup)];if(matches.length!==1)throw Error('CURRENT_CLEANUP_GUARD_NOT_UNIQUE');
 const tail=matches[0][0],withoutTail=replaceOnce(main,tail,'','worker cleanup guard');
 const expression=/const bool msiOrderedPublication = ymcc::msi::latehide::buildEnabled &&\r?\n[ \t]+inputHostLateHideManagedMsiRouteType\(\);/g;
 const publicationMatches=[...main.toString('utf8').matchAll(expression)];if(publicationMatches.length!==1)throw Error('CURRENT_PUBLICATION_EXPRESSION_NOT_UNIQUE');
 const publicationFrom=publicationMatches[0][0],publicationTo='const bool msiOrderedPublication = false;';
 // Match only the queue-lock if-block calling the MSI exact-owner helper.
 // Its definition, publication ordering and both worker guards stay captured.
 const reuseBranch=/^[ \t]*if \(msiPublication != nullptr && inputHostLateHideReuseQueuedPublication\(msiPublication,\r?\n[ \t]*std::find\(g_inputHostLifecycleWorkQueue\.begin\(\), g_inputHostLifecycleWorkQueue\.end\(\),\r?\n[ \t]*requested\) != g_inputHostLifecycleWorkQueue\.end\(\)\)\) \{\r?\n(?:[ \t]*\/\/[^\r\n]*\r?\n)*[ \t]*if \(coalescedOut\) \*coalescedOut = !msiPublication->alreadyCompleted;\r?\n[ \t]*return true;\r?\n[ \t]*\}\r?\n/gm;
 const reuseMatches=[...main.toString('utf8').matchAll(reuseBranch)];if(reuseMatches.length!==1)throw Error('CURRENT_MSI_OWNER_REUSE_BRANCH_NOT_UNIQUE');
 const reuseFrom=reuseMatches[0][0],withoutReuse=replaceOnce(main,reuseFrom,'','MSI queue exact-owner reuse branch only');
 const ownerMatches=[...main.toString('utf8').matchAll(/^[ \t]*g_inputHostLifecycleWorkInFlight\.load\(std::memory_order_acquire\) &&\r?\n/gm)];
 if(ownerMatches.length!==1)throw Error('CURRENT_ACTUAL_OWNER_CHECK_NOT_UNIQUE');
 const ownerFrom=ownerMatches[0][0];
 const guard=/^[ \t]*struct LateHideFlightClaim \{\r?\n[ \t]*bool armed;\r?\n[ \t]*~LateHideFlightClaim\(\) \{ if \(armed\) inputHostLateHideInFlightSet\(false, 0\); \}\r?\n[ \t]*\} lateHideFlightClaim\{ymcc::msi::latehide::buildEnabled &&\r?\n[ \t]*inputHostLateHideManagedMsiRouteType\(\)\};\r?\n/gm;
 const guards=[...main.toString('utf8').matchAll(guard)];if(guards.length!==1)throw Error('CURRENT_MSI_FLIGHT_GUARD_NOT_UNIQUE');
 const guardFrom=guards[0][0];const discharge=/^[ \t]*lateHideFlightClaim\.armed = false;[^\r\n]*\r?\n/gm;
 const discharges=[...main.toString('utf8').matchAll(discharge)];if(discharges.length!==1)throw Error('CURRENT_MSI_FLIGHT_GUARD_DISCHARGE_NOT_UNIQUE');
 const dischargeFrom=discharges[0][0];
 let withoutOwner=replaceOnce(main,ownerFrom,'','actual worker ownership check');withoutOwner=replaceOnce(withoutOwner,guardFrom,'','MSI exception flight guard');withoutOwner=replaceOnce(withoutOwner,dischargeFrom,'','MSI exception flight discharge');
 const ownerPatches=[{file:'native/main.cpp',from:ownerFrom,to:'',scope:'actual lifecycle owner check only'},{file:'native/main.cpp',from:guardFrom,to:'',scope:'MSI-only exceptional flight marker retirement guard only'},{file:'native/main.cpp',from:dischargeFrom,to:'',scope:'MSI guard normal discharge only; original normal marker clearing stays'}];
 const cleanupPatch={file:'native/main.cpp',from:tail,to:'',scope:'known worker empty-item cleanup guard only'};
 const configs=[
  {name:'rollback-both',suite:'late-hide-pending',main:withoutTail,header:replaceOnce(header,'inline constexpr bool buildEnabled=true;','inline constexpr bool buildEnabled=false;','pending default'),patches:[cleanupPatch,{file:'native/msi_late_hide_pending.h',from:'inline constexpr bool buildEnabled=true;',to:'inline constexpr bool buildEnabled=false;',scope:'existing pending guard default only'}],expectedFailures:[...chainFailures,...tokenFailures]},
  {name:'rollback-cleanup-only',suite:'late-hide-pending',main:withoutTail,header,patches:[cleanupPatch],expectedFailures:[...tokenFailures]},
  {name:'rollback-publication-only',suite:'late-hide-publication',main:replaceOnce(main,publicationFrom,publicationTo,'ordered publication only'),header,patches:[{file:'native/main.cpp',from:publicationFrom,to:publicationTo,scope:'MSI ordered publication selection only; pending default and cleanup guard retained'}],expectedFailures:[...publicationFailures]},
  {name:'rollback-inflight-only',suite:'late-hide-inflight',main:withoutReuse,header,patches:[{file:'native/main.cpp',from:reuseFrom,to:'',scope:'MSI queue exact pending owner reuse if-block only; helper definition, ordered publication, pending default and cleanup guard retained'}],expectedFailures:[...inflightFailures]},
  {name:'rollback-orphan-owner',suite:'late-hide-orphaned-inflight',main:withoutOwner,header,patches:ownerPatches,expectedFailures:[...orphanFailures]}
 ];
 for(const config of configs){
  const dir=path.join(out,config.name,'product-before');fs.mkdirSync(path.join(dir,'native'),{recursive:true});
  const records=[];for(const [relative,bytes]of [['native/main.cpp',config.main],['native/msi_late_hide_pending.h',config.header]]){fs.writeFileSync(path.join(dir,relative),bytes,{flag:'wx'});records.push({relative,sha256:sha(bytes)});}
  const manifest=path.join(dir,'MANIFEST.json');fs.writeFileSync(manifest,JSON.stringify(records,null,2)+'\n',{flag:'wx'});
  const provenance={kind:'current-compiled-rollback',historicalOriginal:false,productFilesModified:false,originals:[fact(mainPath),fact(headerPath)],patches:config.patches,outputs:records,manifest:fact(manifest)};
  const origin=path.join(dir,'ORIGIN.json');fs.writeFileSync(origin,JSON.stringify(provenance,null,2)+'\n',{flag:'wx'});
  rollbacks.push({name:config.name,suite:config.suite,dir,manifest:fact(manifest),origin:fact(origin),expectedFailures:config.expectedFailures});
 }
}
function changed(rows){return rows.flatMap(r=>{try{return sha(fs.readFileSync(r.path))===r.sha256?[]:[r.path];}catch{return[r.path];}});}
function validateBindings(dir,name){
 const file=path.join(dir,'source-manifest.json'),m=read(file);if(!Array.isArray(m.records)||!m.records.length||!Array.isArray(m.compiledGenerated))throw Error('MISSING_COMPLETE_NATIVE_MANIFEST: '+name);
 const missing=[];
 for(const r of [...m.records,...m.copiedButUncompiled])for(const p of [r.original,r.snapshot])if(fact(p).sha256!==r.sha256)missing.push(p);
 for(const r of m.compiledGenerated)if(fact(r.path).sha256!==r.sha256)missing.push(r.path);
 for(const r of m.sourceExpansion.inputs)if(fact(r.file).sha256!==r.sha256)missing.push(r.file);
 if(fact(m.sourceExpansion.expandedSnapshot).sha256!==m.sourceExpansion.expandedSha256)missing.push(m.sourceExpansion.expandedSnapshot);
 check(name+' exact original/saved/generated compiler inputs',missing.length===0,{changed:missing});
 for(const symbol of ['inputHostRunQueuedLifecycleWork','inputHostQueueLifecycleWorkInternal','inputHostLateHideEnqueueEpisode','inputHostLateHidePendingRead','inputHostLateHidePendingCommit','inputHostLateHidePendingClearIfToken','inputHostLateHideMarkEpisodeCompleted']){const r=m.core.find(x=>x.symbol===symbol);check(name+' original actual body '+symbol,!!r&&r.unchanged&&r.sha256===r.executedSha256);}
 const pending=m.records.find(x=>x.path==='native/msi_late_hide_pending.h');
 check(name+' real pending header was compiler-observed',!!pending&&m.observedIncludePaths.includes(path.resolve(pending.snapshot).toLowerCase()));
 return{...fact(file),compiledSources:m.records.length,copiedUncompiled:m.copiedButUncompiled.length,generated:m.compiledGenerated.length,sourceClosureMeaning:m.sourceClosureMeaning};
}
// Test-only child concurrency. Product threads and the global timeout contract stay unchanged.
const compilerConcurrency=2,outputLimit=8*1024*1024;
function spawnCaptured(command,args,options){return new Promise(resolve=>{
 let stdout='',stderr='',size=0,error=null,timedOut=false,settled=false;
 const child=spawn(command,args,{cwd:options.cwd,windowsHide:true,stdio:['ignore','pipe','pipe']});
 const timer=setTimeout(()=>{timedOut=true;error=Object.assign(new Error('test child exceeded its original bounded budget'),{code:'ETIMEDOUT'});child.kill();},options.timeoutMs);
 const collect=kind=>chunk=>{size+=chunk.length;if(size>outputLimit){error=new Error('test child output exceeded the declared evidence bound');child.kill();return;}if(kind==='stdout')stdout+=chunk.toString('utf8');else stderr+=chunk.toString('utf8');};
 child.stdout.on('data',collect('stdout'));child.stderr.on('data',collect('stderr'));
 child.once('error',failure=>{error=failure;});
 child.once('close',(status,signal)=>{if(settled)return;settled=true;clearTimeout(timer);resolve({status,signal,error,timedOut,stdout,stderr});});
 });}
async function runVariant(name,rollback=null){
 const suite=rollback?.suite??'late-hide-current';
 const modes=rollback?[suite]:['late-hide-pending','late-hide-publication','late-hide-inflight','late-hide-inflight-edges','late-hide-orphaned-inflight'];
 const dir=path.join(compileOut,name),argv=[path.join(REPO,'tools/input_coordinator_lifecycle_selftest.mjs'),'--out',dir,'--suite',suite,...(rollback?['--product-before',rollback.dir]:[])];
 const timeoutMs=Math.min(360000,deadline-Date.now());if(timeoutMs<1000)throw Error('SCENARIO_TIME_BUDGET_EXHAUSTED');
 const command={executable:process.execPath,args:argv,cwd:REPO,startedAt:new Date().toISOString(),timeoutMs};
 const result=await spawnCaptured(process.execPath,argv,{cwd:REPO,timeoutMs});
 command.finishedAt=new Date().toISOString();command.exitCode=result.status;command.signal=result.signal;command.error=result.error?.message??null;
 fs.mkdirSync(path.join(out,name),{recursive:true});fs.writeFileSync(path.join(out,name,'command.json'),JSON.stringify(command,null,2)+'\n',{flag:'wx'});fs.writeFileSync(path.join(out,name,'stdout.log'),result.stdout??'',{flag:'wx'});fs.writeFileSync(path.join(out,name,'stderr.log'),result.stderr??'',{flag:'wx'});
 const receiptPath=path.join(dir,'RESULT.json');if(!fs.existsSync(receiptPath))throw Error('SUITE_RESULT_MISSING: '+name+' '+(command.error??'exit '+result.status));
 const receipt=read(receiptPath);if(!Array.isArray(receipt.results))throw Error('SUITE_CHILD_RESULTS_MISSING: '+name);
 check(name+' existing harness passes its requested polarity',result.status===0&&!result.error&&receipt.status==='PASS',{exitCode:result.status,status:receipt.status});
 check(name+' executes exactly the requested actual modes',receipt.suite===suite&&equalSet(receipt.results.map(r=>r.mode),modes)&&receipt.mutation===null);
 check(name+' complete native TU and one compiled candidate shared by its modes',receipt.sourceFullTranslationUnit===true&&receipt.oneCompiledCandidate===true);
 check(name+' source before/after stable',receipt.changed.length===0&&receipt.compiledGeneratedChanged.length===0&&receipt.copiedButUncompiledChanged.length===0&&receipt.expandedSourceChanged===false);
 check(name+' expected pending failure list bound',equalSet(receipt.expectedLateHideFailures,rollback?.suite==='late-hide-pending'?rollback.expectedFailures:[]));
 check(name+' expected publication failure list bound',equalSet(receipt.expectedPublicationFailures,rollback?.suite==='late-hide-publication'?rollback.expectedFailures:[]));
 check(name+' expected inflight failure list bound',equalSet(receipt.expectedInflightFailures,rollback?.suite==='late-hide-inflight'?rollback.expectedFailures:[]));
 check(name+' expected orphan failure list bound',equalSet(receipt.expectedOrphanFailures,rollback?.suite==='late-hide-orphaned-inflight'?rollback.expectedFailures:[]));
 const executable=fact(path.join(dir,'CoordinatorFixture.exe')),consumers=[];
 for(const mode of modes){
  const r=receipt.results.find(r=>r.mode===mode),d=r?.result;if(!d)throw Error('SUITE_CHILD_RESULT_MISSING: '+name+' '+mode);
  const expected=rollback?.expectedFailures??[],counts=modeCounts[mode];
  check(name+' '+mode+' exact actual scenario/check counts',d.cases.length===counts.cases&&d.checks===counts.checks,{cases:d.cases.length,checks:d.checks});
  check(name+' '+mode+' no forbidden external I/O or product/device execution',d.forbiddenIo===0&&d.normalProductMainExecuted===false&&d.realDeviceIo===false&&receipt.normalProductMainExecuted===false&&receipt.realDeviceIo===false&&receipt.productionEdited===false&&d.fullTranslationUnit===true&&!r.timedOut);
  check(name+' '+mode+' exact consumer polarity and failed assertions',d.status===(rollback?'FAIL':'PASS')&&r.exitCode===(rollback?1:0)&&equalSet(d.failures.map(x=>x.assertion),expected),{expectedFailures:expected,actualFailures:d.failures});
  consumers.push({mode,status:d.status,exitCode:r.exitCode,scenarios:d.cases.length,checks:d.checks,forbiddenIo:d.forbiddenIo,failures:d.failures,executable,fixtureResult:fact(path.join(dir,mode,'fixture-result.json'))});
 }
 if(rollback){const bound=receipt.productBeforeBinding;check(name+' compiled rollback manifest bound exactly',!!bound&&path.resolve(bound.manifest)===rollback.manifest.path&&bound.manifestSha256===rollback.manifest.sha256);}
 else check(name+' uses current positive product bytes',receipt.productBeforeBinding===null);
 const sourceManifest=validateBindings(dir,name);
 const pending=receipt.results.find(r=>r.mode==='late-hide-pending')?.result;
 const publication=receipt.results.find(r=>r.mode==='late-hide-publication')?.result;
 const chain=pending?.cases.find(c=>c.name==='actual-worker-real-empty-same-real'),zero=pending?.cases.find(c=>c.name==='source-directed-empty-token0-new-token1');
 const scheduling=publication?.cases.find(c=>c.name==='actual-producer-notification-before-pending-commit');
 const inflight=receipt.results.find(r=>r.mode==='late-hide-inflight')?.result?.cases.find(c=>c.name==='same-episode-BUS-reentry-during-actual-worker');
 const edges=receipt.results.find(r=>r.mode==='late-hide-inflight-edges')?.result;
 if(!rollback){
  check('positive real-empty-real is exactly one hide',chain?.afterReal?.hideCalls===1&&chain.afterEmpty.hideCalls===1&&chain.afterEmpty.completed===true&&chain.afterRepeat.hideCalls===1&&chain.afterRepeat.sameRealApplied===1&&chain.afterRepeat.enqueueOutcome==='already-completed');
  check('positive token0 successor is consumed once',zero?.afterEmpty?.hasPending===true&&zero.afterEmpty.token===1&&zero.afterNewReal.hideCalls===1&&zero.afterNewReal.completed===true&&zero.fieldAttribution===false);
  check('positive actual publication is guarded across two threads',scheduling?.producerAndWorkerAreSeparateThreads===true&&scheduling.queueGuardedAtCommitBoundary===true&&scheduling.fieldAttribution===false&&scheduling.afterSeparateProducer.hidesBefore===1&&scheduling.afterSeparateProducer.hidesAfter===1&&scheduling.afterSeparateProducer.completed===true);
  check('positive actual BUS reentry keeps the worker token and one completed hide',inflight?.fieldAttribution===false&&inflight.duringReentry.initialToken>0&&inflight.duringReentry.tokenAfterBUS===inflight.duringReentry.initialToken&&inflight.duringReentry.queueDepthAfterBUS===0&&inflight.afterDrain.hideCalls===1&&inflight.afterDrain.appliedCompletions===1&&inflight.afterDrain.hasPending===false&&inflight.afterDrain.requested===false);
 }
 const run={name,suite,kind:rollback?'current-compiled-rollback':'current-positive',historicalOriginal:false,command,receipt:fact(receiptPath),sourceManifest,executable,buildCommand:fact(path.join(dir,'build.cmd')),buildLog:fact(path.join(dir,'build.log')),consumers,consumerStatus:consumers.every(c=>c.status==='PASS')?'PASS':'FAIL',scenarios:consumers.reduce((n,c)=>n+c.scenarios,0),checks:consumers.reduce((n,c)=>n+c.checks,0),forbiddenIo:consumers.reduce((n,c)=>n+c.forbiddenIo,0),expectedFailures:rollback?.expectedFailures??[],failures:consumers.flatMap(c=>c.failures),chain:{afterReal:chain?.afterReal,afterEmpty:chain?.afterEmpty,afterRepeat:chain?.afterRepeat},token0:{afterEmpty:zero?.afterEmpty,afterNewReal:zero?.afterNewReal,fieldAttribution:zero?.fieldAttribution},publication:scheduling,inflight:{duringReentry:inflight?.duringReentry,afterDrain:inflight?.afterDrain,fieldAttribution:inflight?.fieldAttribution},inflightEdges:{cases:edges?.cases.map(c=>c.name)}};
 runs.push(run);console.log(name+': harness '+receipt.status+', consumer '+run.consumerStatus+', '+run.scenarios+'/'+run.checks+', expected failures '+run.expectedFailures.length);
 const drift=[...changed(before),...changed(tools)];if(drift.length)throw Error('INPUT_DRIFT_AFTER_'+name+': '+drift.join(','));
}
try{
 before=sourceInputs(REPO).map(relative=>({relative,...fact(path.join(REPO,relative))}));tools=artifactInputs(REPO).map(fact);
 fs.writeFileSync(path.join(out,'INPUTS-BEFORE.json'),JSON.stringify({sources:before,tools},null,2)+'\n',{flag:'wx'});
 prepareRollbacks();await runVariant('positive');
 let cursor=0;const childErrors=[];
 const workerOutcomes=await Promise.allSettled(Array.from({length:compilerConcurrency},async()=>{
  while(cursor<rollbacks.length){const rollback=rollbacks[cursor++];try{await runVariant(rollback.name,rollback);}catch(failure){childErrors.push(failure);check(rollback.name+' actual bounded child completed',false,{error:failure.stack??String(failure)});}}
 }));
 for(const outcome of workerOutcomes)if(outcome.status==='rejected'){childErrors.push(outcome.reason);check('test compiler worker completion',false,{error:String(outcome.reason)});}
 const order=['positive',...rollbacks.map(r=>r.name)];runs.sort((a,b)=>order.indexOf(a.name)-order.indexOf(b.name));
 if(childErrors.length)throw new AggregateError(childErrors,'one or more actual compiled controls failed their execution contract');
}catch(e){error=e.stack??String(e);check('all six actual consumer compilations completed',false,{error});}
const drift=[...changed(before),...changed(tools)];check('source and tool inputs unchanged for the whole scenario',drift.length===0,{changed:drift});check('combined positive plus all five compiled controls actually ran',runs.length===6);
const passed=checks.every(c=>c.passed),bindings={schemaVersion:2,scenario:ID,status:passed?'PASS':'FAIL',sources:before,toolInputs:tools,sourceChanged:drift,compiledVariants:runs.map(r=>({name:r.name,suite:r.suite,receipt:r.receipt,sourceManifest:r.sourceManifest,executable:r.executable,consumers:r.consumers})),rollbacks,productionEdited:false,modelCreated:false};
fs.writeFileSync(path.join(out,'SOURCE-BINDINGS.json'),JSON.stringify(bindings,null,2)+'\n',{flag:'wx'});
const summary={schemaVersion:3,compilerConcurrency,testConcurrencyOnly:true,scenario:ID,status:passed?'PASS':'FAIL',passed,profile:'full',startedAt,finishedAt:new Date().toISOString(),actualCases:runs[0]?.scenarios??0,actualChecks:runs[0]?.checks??0,controls:runs.filter(r=>r.kind==='current-compiled-rollback').map(r=>({name:r.name,suite:r.suite,expectedAssertionFailures:r.expectedFailures.length,actualAssertionFailures:r.failures.length,consumerStatus:r.consumerStatus})),runs,checks,failures:checks.filter(c=>!c.passed),error,sourceChanged:drift,bindings:fact(path.join(out,'SOURCE-BINDINGS.json')),normalProductMainExecuted:false,realSdkConstructed:false,deviceDriverRegistryServiceOperations:false,physicalHardwareValidated:false,historicalOriginalClaimed:false,publicationSuiteExecuted:runs.some(r=>r.consumers.some(c=>c.mode==='late-hide-publication')),inflightSuiteExecuted:runs.some(r=>r.consumers.some(c=>c.mode==='late-hide-inflight')),inflightEdgesSuiteExecuted:runs.some(r=>r.consumers.some(c=>c.mode==='late-hide-inflight-edges'))};
fs.writeFileSync(path.join(out,'RESULT.json'),JSON.stringify(summary,null,2)+'\n',{flag:'wx'});console.log(JSON.stringify({status:summary.status,actualCases:summary.actualCases,actualChecks:summary.actualChecks,controls:summary.controls,sourceChanged:drift,failures:summary.failures.length}));process.exitCode=passed?0:1;
