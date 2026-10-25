import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {lifecycleContractFingerprint,auditLifecycleAcceptance} from '../capability-lifecycle/index.mjs';
import {getDeckyPersistentLifecycleAdapters,lifecycleContract} from './adapter.mjs';
import {lifecycleConsumerTests} from './consumer-selftest.mjs';
import {typedIds,historicalStallId,productionWiringId,historicalValidationId,resourceZero,verifyProductionWiring,verifyTypedRuntime,preserveHistoricalCallbackNegative} from './typed-binding-gate.mjs';
const root=path.resolve(import.meta.dirname,'../../../..');
const args=process.argv.slice(2),get=name=>{const i=args.indexOf(name);return i<0?undefined:args[i+1];};
const out=path.resolve(get('--evidence-dir')||'work/decky-persistent-lifecycle');
const relative=path.relative(root,out);if(relative===''||(!path.isAbsolute(relative)&&relative!=='..'&&!relative.startsWith('..'+path.sep)))throw Error('Evidence must be outside source tree');
if(fs.existsSync(out))throw Error('Fresh evidence directory required');fs.mkdirSync(out,{recursive:true});
const sha=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const readJson=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const adapter=getDeckyPersistentLifecycleAdapters(root)[0];
const inputs=new Map();
function bind(rel){if(inputs.has(rel))return;const file=path.join(root,rel);if(!fs.statSync(file).isFile())throw Error('Missing source '+rel);inputs.set(rel,sha(fs.readFileSync(file)));}
// Full selected source capture is additionally verified by the delivery harness.
for(const rel of [adapter.path,...adapter.evidenceInputs,'tools/decky-loader-contract.mjs','PowerControl/decky/PluginLoader_noconsole.exe','PowerControl/decky/LICENSE.decky-loader','.gitattributes','scripts/bump-version.mjs'])bind(rel);
for(const rel of fs.readdirSync(path.join(root,'src/bridge')).filter(n=>/\.(ts|js)$/.test(n)).map(n=>'src/bridge/'+n))bind(rel);
for(const rel of fs.readdirSync(path.join(root,'native')).filter(n=>/\.(h|cpp|js)$/.test(n)).map(n=>'native/'+n))bind(rel);
for(const rel of fs.readdirSync(path.join(root,'decky-plugin/src')).filter(n=>/\.(ts|tsx|js)$/.test(n)).map(n=>'decky-plugin/src/'+n))bind(rel);
const report={schemaVersion:1,passed:false,status:'FAIL',softwareAcceptance:false,physicalSleepPerformed:false,officialLoaderExecuted:false,installedApplicationsStarted:false,hardwareWrites:0,cases:[],sourceHashes:Object.fromEntries(inputs),sourceInputs:[...inputs].map(([path,sha256])=>({path,sha256})),lifecycleContract,lifecycleEvidence:{schemaVersion:1,contractFingerprint:lifecycleContractFingerprint(lifecycleContract),features:[{id:'plugins.decky',observations:[]}]},boundaries:[adapter.boundary,'Selected frozen consumer capture, not full HC snapshot or release approval. Host/client, native and public-gate scopes remain separate.']};
const cases=report.cases;
const tools=readJson(path.join(root,'tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json'));
const nativeEnv={...process.env,INCLUDE:tools.native.include.join(';'),LIB:tools.native.lib.join(';'),PATH:path.dirname(tools.native.compiler)+';'+path.dirname(tools.native.linker)+';'+process.env.PATH,CL:'',_CL_:'',LINK:'',_LINK_:''};
const env={...nativeEnv,YMCC_DECKY_ARTIFACT_ROOT:path.join(out,'original-artifacts')};
function execute(id,program,argv,{timeout=45000,cwd=out,environment=env,allowFailure=false,record=true,preserveStreams=false}={}){
 const t=Date.now(),run=spawnSync(program,argv,{cwd,env:environment,windowsHide:true,timeout,maxBuffer:16*1024*1024});
 const stem=id.replaceAll('/','_'),log=path.join(out,stem+'.log'),stdout=run.stdout??Buffer.alloc(0),stderr=run.stderr??Buffer.alloc(0);
 fs.writeFileSync(log,Buffer.concat([stdout,stderr]));
 const row={id,name:id,passed:run.status===0&&!run.error,exitCode:run.status,elapsedMs:Date.now()-t,command:{program,arguments:argv,cwd},executableSha256:fs.existsSync(program)?sha(fs.readFileSync(program)):null,log:path.relative(out,log).replaceAll('\\','/'),logSha256:sha(fs.readFileSync(log)),...(run.error?{error:run.error.message}:{})};
 if(preserveStreams){for(const [name,bytes] of [['stdout',stdout],['stderr',stderr]]){row[name]=stem+'.'+name+'.bin';fs.writeFileSync(path.join(out,row[name]),bytes);row[name+'Sha256']=sha(bytes);}}
 if(record)cases.push(row);console.log(JSON.stringify({phase:id,passed:row.passed,exitCode:row.exitCode,elapsedMs:row.elapsedMs}));
 if(!row.passed&&!allowFailure)throw Error(id+' failed; '+log);return row;
}
function attempt(id,action){try{return action();}catch(error){const row={id,name:id,passed:false,error:error.stack};cases.push(row);(report.integrationErrors??=[]).push({id,error:error.stack});return null;}}
try{
 if(adapter.blockedReason)throw Error(adapter.blockedReason);
 // Current acceptance always executes a fresh source-built typed Runtime first,
 // so an obsolete legacy extractor cannot suppress the production-path evidence.
 const wiring=attempt(productionWiringId,()=>verifyProductionWiring({root,out,tools,execute}));if(wiring)cases.push(wiring);
 const typedExecution=execute('native.typed-runtime-build-and-execution',process.execPath,[path.join(root,'tools/decky_binding_runtime_selftest.mjs'),path.join(out,'typed-runtime')],{cwd:root,timeout:120000,allowFailure:true});
 const typed=attempt('native.typed-runtime-evidence-validation',()=>verifyTypedRuntime({root,out,execution:typedExecution}));
 if(typed){report.typedRuntime=typed.receipt;cases.push(...typed.rows);cases.push({id:'native.typed-runtime-evidence-validation',name:'native.typed-runtime-evidence-validation',passed:true,measurements:typed.receipt});}
 // Execute every existing regression. Errors remain failing rows; a failed
 // extractor is reported without dropping the later owners/health/budget gates.
 const baseline=get('--baseline-result');
 if(baseline){
  attempt('original.plugins.decky-update-recovery',()=>{const original=readJson(baseline);if(original.passed!==true||!original.sourceInputs?.length)throw Error('Baseline is not a passing bound original result');for(const i of original.sourceInputs)if(sha(fs.readFileSync(path.join(root,i.path)))!==i.sha256)throw Error('Baseline source mismatch '+i.path);report.originalUpdateRecovery={result:path.resolve(baseline),sha256:sha(fs.readFileSync(baseline)),sourceVerified:true,receipt:original};cases.push({id:'original.plugins.decky-update-recovery',name:'Unchanged full original Decky update recovery',passed:true,measurements:{reusedBoundReceipt:true,nativeChecks:original.nativeChecks}});});
 }else{
  const baseOut=path.join(out,'b');
  execute('original.plugins.decky-update-recovery',process.execPath,[path.join(root,'tools/ymcc_sim/global/decky-update-recovery/run.mjs'),'--evidence-dir',baseOut],{timeout:240000,cwd:root,allowFailure:true});
  attempt('original.plugins.decky-update-recovery-receipt',()=>{const file=path.join(baseOut,'result.json'),original=readJson(file);report.originalUpdateRecovery={result:file,sha256:sha(fs.readFileSync(file)),sourceVerified:true,receipt:original};if(original.passed!==true){report.originalBaselineReplayFailed=true;throw Error(original.error??'Original update recovery receipt is FAIL');}});
 }
 execute('official-byte-identity',process.execPath,[path.join(root,'tools/decky-loader-contract.mjs'),'verify'],{cwd:root,allowFailure:true});
 for(const name of ['decky_loader_official_selftest','decky_sidebar_safety_selftest','decky_sidebar_client_selftest','decky_sidebar_host_selftest','decky_sidebar_hang_recovery_selftest','decky_sidebar_stability_matrix_selftest']){
  const artifact=path.join(out,'original-artifacts',name);execute('original.'+name,process.execPath,[path.join(root,'tools',name+'.mjs'),artifact],{cwd:root,timeout:60000,allowFailure:true});
 }
 const nativeOut=path.join(out,'native');fs.mkdirSync(nativeOut);
 attempt('native.committed-ready-consumer-extraction',()=>{
  const main=fs.readFileSync(path.join(root,'native/main.cpp'),'utf8'),start=main.indexOf('    case WM_POWER_RESUME_COMMIT:'),body=main.indexOf('\n',start)+1,end=main.indexOf('            ipc_emit("power.resumed"',body);
  if(start<0||end<body||!main.slice(body,end).includes('requestBindingRefresh(currentPowerGeneration())'))throw Error('Review changed committed-ready extraction boundaries');
  const extracted=main.slice(body,end);fs.writeFileSync(path.join(nativeOut,'committed-ready-consumer.inc'),'static void consumeCommittedReady(){\n'+extracted+'        }\n}\n');
  report.nativeConsumerExtraction={source:'native/main.cpp',sourceSha256:inputs.get('native/main.cpp'),beginByte:body,endByte:end,sliceSha256:sha(extracted),generatedSha256:sha(fs.readFileSync(path.join(nativeOut,'committed-ready-consumer.inc'))),scope:'Original committed-ready gate, watchdog stop, Runtime rebind/check and diagnostic; subsequent UI notification is not executed'};
 });
 const libraries=['advapi32.lib','user32.lib','userenv.lib','winhttp.lib','ws2_32.lib','bcrypt.lib'];
 function build(id,source,exe){return execute(id,tools.native.compiler,['/nologo','/std:c++20','/utf-8','/EHsc','/MT','/O2','/DNOMINMAX','/DUNICODE','/D_UNICODE','/I'+path.join(root,'native'),'/I'+path.join(root,'deps/json'),'/I'+nativeOut,source,'/Fo'+exe+'.obj','/Fe'+exe,'/link',...libraries],{timeout:60000,allowFailure:true});}
 for(const name of ['runtime','recovery','health','health_close','crash_breaker']){
  const exe=path.join(nativeOut,'original-'+name+'.exe'),built=build('build.original-native-'+name,path.join(root,'tools','decky_sidebar_'+name+'_selftest.cpp'),exe);
  const argv=name==='health_close'?[path.join(nativeOut,'health-close-fixture')]:name==='crash_breaker'?[path.join(nativeOut,'crash-fixture')]:[];
  if(built.passed)execute('original.native-'+name,exe,argv,{timeout:name==='crash_breaker'?35000:15000,allowFailure:true});
  else cases.push({id:'original.native-'+name,name:'original.native-'+name,passed:false,error:'Original regression could not execute because its unchanged source failed compilation'});
 }
 const bindingExe=path.join(nativeOut,'binding-cancel.exe'),bindingBuild=build('build.native-binding-cancel',path.join(root,'tools/decky_binding_cancel_selftest.cpp'),bindingExe);
 if(bindingBuild.passed)execute('native.binding-cancel-network',bindingExe,[path.join(nativeOut,'binding-fixtures')],{timeout:20000,allowFailure:true});
 attempt('native.binding-cancel-receipt',()=>{const file=path.join(nativeOut,'binding-cancel-results.json'),bindingResult=readJson(file);if(bindingResult.ok!==true||bindingResult.cases.length!==2||bindingResult.cases.some(row=>!row.ok))throw Error('Real binding network cancellation incomplete');report.bindingCancellation={path:'native/binding-cancel-results.json',sha256:sha(fs.readFileSync(file)),receipt:bindingResult};});
 const exe=path.join(nativeOut,'persistent-fixture.exe'),persistentBuild=build('build.native-persistent',path.join(import.meta.dirname,'native-fixture.cpp'),exe);
 let nativeResult={cases:[]};
 if(persistentBuild.passed){
  const nativeRun=execute('native-persistent-execution',exe,[path.join(nativeOut,'native-fixtures')],{timeout:50000,allowFailure:true,record:false,preserveStreams:true});
  attempt('native.historical-callback-negative-execution',()=>{
   const file=path.join(nativeOut,'native-result.json');nativeResult=readJson(file);
   report.nativeExecutable={path:path.relative(out,exe).replaceAll('\\','/'),sha256:sha(fs.readFileSync(exe))};report.nativeResult={path:'native/native-result.json',sha256:sha(fs.readFileSync(file)),nativeExecuted:nativeResult.nativeExecuted};
   const history=preserveHistoricalCallbackNegative({root,out,execution:nativeRun,nativeResult});report.historicalCallbackNegative=history.record;cases.push(history.row);
   // Only the known negative moves to historical evidence. All other original
   // results, including any new failure, continue to gate current acceptance.
   cases.push(...nativeResult.cases.filter(row=>row.id!==historicalStallId));
  });
 }else cases.push({id:historicalValidationId,name:historicalValidationId,passed:false,error:'Historical negative fixture failed compilation; no negative result may be inferred'});
 try{const consumer=await lifecycleConsumerTests(nativeResult);fs.writeFileSync(path.join(out,'consumer-result.json'),JSON.stringify(consumer,null,2)+'\n');cases.push(...consumer.cases);}catch(error){cases.push({id:'consumer.lifecycle-execution',name:'consumer.lifecycle-execution',passed:false,error:error.stack});}
 const find=id=>cases.find(c=>c.id===id),meas=id=>find(id)?.measurements??{};
 function combine(id,requirement,ids,measurements){const pass=ids.every(id=>find(id)?.passed===true);const row={id,name:id,passed:pass,componentCaseIds:ids,measurements};cases.push(row);report.lifecycleEvidence.features[0].observations.push({requirement,caseId:id,passed:pass,measurements});}
 const jsExitId='consumer.exit-disposes-observers-and-delivered-late-recovery-callbacks',jsExit=meas(jsExitId);
 const allResources=nativeId=>({processes:meas(nativeId).ownedChildAliveAtDeadline===true?1:meas(nativeId).remainingOwnedResources===0?0:null,handles:meas(nativeId).heldResourcesAtDeadline??meas(nativeId).remainingOwnedResources??null,workers:meas(nativeId).completedBeforeFixtureRelease===false?1:meas(nativeId).remainingOwnedResources===0?0:null,timers:jsExit.remainingOwnedResources?.timers??null,subscriptions:jsExit.remainingOwnedResources?.subscriptions??null});
 const typedResources=ids=>{const proven=ids.every(id=>resourceZero(meas(id).atExit??meas(id).atDeadline??meas(id).afterOff));return {processes:proven?0:null,handles:proven?0:null,workers:proven?0:null,timers:jsExit.remainingOwnedResources?.timers??null,subscriptions:jsExit.remainingOwnedResources?.subscriptions??null};};
 const exitId='native.exit-clears-owned-process-job-worker-event',offId='native.explicit-off-and-fatal-off-veto-late-wakes',lateId='native.late-actual-recovery-callback-after-exit',fallbackId='native.job-stop-failure-falls-back-to-owned-job-close';
 const typedGuard=[productionWiringId,'native.typed-runtime-evidence-validation',historicalValidationId];
 const stops=[typedIds.stalled,typedIds.descendants],typedBounded=stops.every(id=>find(id)?.passed===true&&meas(id).stopMs<3200&&meas(id).completedBeforeFixtureRelease===true&&meas(id).fixtureReleaseUnsentAtMeasurement===true);
 const typedRevoked=stops.every(id=>meas(id).ownedLoaderExited===true&&meas(id).bindingChildExited===true&&meas(id).descendantExited===true&&meas(id).lateRefreshRejected===true&&meas(id).lateRecoveryRejected===true);
 combine('acceptance.exit','destroyOnExit',[exitId,typedIds.healthy,typedIds.inactive,...stops,jsExitId,...typedGuard],{...meas(exitId),remainingOwnedResources:typedResources([typedIds.healthy,typedIds.inactive,...stops]),ownerRevoked:typedRevoked&&meas(exitId).ownerRevoked===true,bounded:typedBounded&&meas(exitId).bounded===true,currentProductionCaseIds:[typedIds.healthy,...stops]});
 const fallback=meas(fallbackId);
 combine('acceptance.stalled-stop','destroyWhenStopStalls',[...stops,fallbackId,jsExitId,...typedGuard],{currentProductionCaseIds:stops,historicalCallbackNegative:report.historicalCallbackNegative?.path??null,deadlineMs:3200,bounded:typedBounded,ownerRevoked:typedRevoked,fallbackExercised:typedBounded&&fallback.fallbackExercised===true,fallbackOwnedOnly:typedRevoked&&fallback.fallbackOwnedOnly===true,remainingOwnedResources:typedResources(stops),successfulOwnedJobCloseFallback:fallback,conditionVariable:meas(typedIds.stalled),descendants:meas(typedIds.descendants)});
 combine('acceptance.late-after-exit','preventResurrectionAfterExit',[lateId,...stops,jsExitId,'consumer.late-attach-resolution-cannot-recreate-stopped-host',...typedGuard],{...meas(lateId),...jsExit,ownerRevoked:typedRevoked&&meas(lateId).ownerRevoked===true,bounded:typedBounded&&meas(lateId).bounded===true,remainingOwnedResources:typedResources(stops),lateCallbacksDelivered:(meas(lateId).lateCallbacksDelivered??0)+(jsExit.lateCallbacksDelivered??0)});
 combine('acceptance.explicit-and-fatal-off','explicitOffStaysOff',[offId,typedIds.inactive,...stops,jsExitId,...typedGuard],{...meas(offId),remainingOwnedResources:typedResources(stops),desiredEnabled:false,lateCallbacksDelivered:meas(offId).rejectedLateRequests??0,restartsAfterExit:meas(offId).additionalStarts??null,bounded:typedBounded&&meas(offId).bounded===true,ownerRevoked:typedRevoked&&meas(offId).ownerRevoked===true});
 const sleepId='consumer.sleep-rejects-write-and-automatically-publishes-current-ready';combine('acceptance.sleep-ready-health','recoverAfterSleep',['native.committed-ready-executes-original-rebind-consumer',typedIds.ready,typedIds.retained,sleepId,...typedGuard],{...meas(sleepId),currentProductionCaseIds:[typedIds.ready,typedIds.retained],ownedReady:meas(typedIds.ready),retainedReady:meas(typedIds.retained)});
 const transientId='consumer.transient-read-exception-does-not-self-recover';combine('acceptance.transient-failure','recoverAfterTransientFailure',[transientId],meas(transientId));
 const limitId='native.crash-budget-cannot-renew-on-recovery-or-unknown';combine('acceptance.recovery-limits','respectRecoveryLimits',[limitId],meas(limitId));
 report.sourceChanges=[...inputs].filter(([rel,digest])=>sha(fs.readFileSync(path.join(root,rel)))!==digest).map(([path])=>path);report.sourceUnchanged=report.sourceChanges.length===0;
 if(!report.sourceUnchanged)throw Error('Source changed during execution: '+report.sourceChanges.join(', '));
 report.passed=cases.every(c=>c.passed===true);report.status=report.passed?'PASS':'FAIL';report.softwareAcceptance=report.passed;
}catch(error){report.error=error.stack;report.passed=false;report.status='FAIL';}
function summary(){report.summary={cases:cases.length,passed:cases.filter(c=>c.passed).length,failed:cases.filter(c=>!c.passed).length,failedCases:cases.filter(c=>!c.passed).map(c=>c.id)};}
// The standalone --evidence-dir may have any fresh basename. The public contract
// still reads its unchanged canonical evidence path, using an identical copy.
const canonicalName='decky-persistent-lifecycle',auditRoot=path.basename(out)===canonicalName?path.dirname(out):path.join(out,'public-audit-input');
const auditFile=path.join(auditRoot,canonicalName,'result.json'),file=path.join(out,'result.json');
if(auditFile!==file)report.publicAuditInput={path:path.relative(out,auditFile).replaceAll('\\','/'),identicalRawReceipt:true};
function save(){summary();fs.writeFileSync(file,JSON.stringify(report,null,2)+'\n');if(auditFile!==file){fs.mkdirSync(path.dirname(auditFile),{recursive:true});fs.copyFileSync(file,auditFile);}}
function audit(){const execution={id:adapter.id,status:report.passed?'PASS':'FAIL',exitCode:report.passed?0:1,evidenceDir:auditRoot,sourceInputs:report.sourceInputs,structuredEvidence:[{path:auditFile,sha256:sha(fs.readFileSync(auditFile)),failed:!report.passed}]};return auditLifecycleAcceptance({repoRoot:root,adapters:[adapter],results:[execution]});}
save();let publicAudit=audit();
if(report.passed&&publicAudit.status!=='PASS'){fs.writeFileSync(path.join(out,'public-lifecycle-audit-initial.json'),JSON.stringify(publicAudit,null,2)+'\n');cases.push({id:'acceptance.public-lifecycle-audit',name:'acceptance.public-lifecycle-audit',passed:false,diagnostics:publicAudit});report.passed=false;report.status='FAIL';report.softwareAcceptance=false;save();publicAudit=audit();}
fs.writeFileSync(path.join(out,'public-lifecycle-audit.json'),JSON.stringify(publicAudit,null,2)+'\n');
console.log(JSON.stringify({status:report.status,...report.summary,publicAudit:publicAudit.status,error:report.error}));process.exitCode=report.passed&&publicAudit.status==='PASS'?0:1;
