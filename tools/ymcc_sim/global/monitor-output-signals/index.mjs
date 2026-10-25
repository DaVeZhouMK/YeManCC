import{lockMonitorToolScope,verifyMonitorToolScope,observeMonitorToolRuntime,MONITOR_TOOL_SOURCES}from './tool-authority.mjs';
import {verifyTemplateLabelOutcome} from './retained-label-contract.mjs';
import fs from'node:fs';import path from'node:path';import{fileURLToPath}from'node:url';
import{Worker,isMainThread,parentPort,workerData}from'node:worker_threads';
import{root,hash,pins,current,readJson,productInputs,retained}from'./common.mjs';import{runRetained}from'./retained.mjs';import{runBrowser}from'./browser.mjs';import{ownInputs}from'./adapter.mjs';import{checkMigrationRejections}from'./migration-checks.mjs';
import{resolveTemplateArtifact,verifyTemplateArtifact,verifyTemplateCopy}from'./template-artifact.mjs';
export const schema='ymcc.monitor-output-signals.v1';
export async function runMonitorOutputSignals({evidenceDir}={}){
 if(!evidenceDir)throw Error('evidenceDir is required');const out=path.resolve(evidenceDir);fs.mkdirSync(out,{recursive:true});if(fs.existsSync(path.join(out,'result.json')))throw Error('FRESH_OUTPUT_EVIDENCE_REQUIRED');
 const files=[...MONITOR_TOOL_SOURCES,...productInputs,'src/bridge/monitorHudHost.ts','tools/monitor_hud_control_clock_selftest.mjs','tools/monitor_output_templates_selftest.mjs','src/components/MonitorOutputSettings.vue','docs/monitor-simple-default-preset-20261014.md',...ownInputs.map(f=>'tools/ymcc_sim/global/monitor-output-signals/'+f)],sourceHashes=pins(files);
 const templateArtifact=resolveTemplateArtifact(root);
 const toolAuthority=lockMonitorToolScope(root,{templateArtifact});fs.writeFileSync(path.join(out,'tool-scope-pre.json'),JSON.stringify(toolAuthority,null,2));
 const [{getAdapters},{getExtraAdapters}]=await Promise.all([import('../adapters.mjs'),import('../extra-adapters.mjs')]);const inventory=[...getAdapters(root),...getExtraAdapters()];
 const registration=retained.map(test=>({source:test.source,direct:inventory.filter(a=>a.path===test.source).map(a=>a.id),references:inventory.filter(a=>a.evidenceInputs?.includes(test.source)||a.referenceInputs?.includes(test.source)).map(a=>a.id)}));
 fs.writeFileSync(path.join(out,'registration-audit.json'),JSON.stringify(registration,null,2));
 if(registration.find(row=>row.source==='tools/monitor_hud_control_clock_selftest.mjs').direct.length)throw Error('CLOCK_TEST_ALREADY_DIRECTLY_REGISTERED');
 const {originalTemplates,checks,browser,independentStages}=await runIndependentMonitorStages(out,{templateArtifact,toolAuthority});
 const templates=checks.find(test=>test.id==='templates');
 const originalFailures=originalTemplates.receipt?.fullCases?.filter(item=>item.status==='FAIL')??[];
 let labelSourceOutcome;try{labelSourceOutcome=verifyTemplateLabelOutcome(originalTemplates,templates);}catch(error){labelSourceOutcome={status:'FAIL',reason:error.message};}
 const metadataMigrationVerified=labelSourceOutcome.status==='PASS';
 const migrationChecks=await checkMigrationRejections(path.join(out,'migration-rejections'),path.join(out,'retained/templates/model-under-test.mjs'));

 for(const checked of checks)for(const[f,pin]of Object.entries(checked.loadedSourceHashes)){if(sourceHashes[f]&&sourceHashes[f]!==pin)throw Error('OUTPUT_COMPILED_SOURCE_DRIFT:'+f);sourceHashes[f]=pin;}
 for(const[f,pin]of Object.entries(browser.sourceHashes)){if(sourceHashes[f]&&sourceHashes[f]!==pin)throw Error('OUTPUT_BROWSER_SOURCE_DRIFT:'+f);sourceHashes[f]=pin;}
 current(sourceHashes);verifyTemplateArtifact(templateArtifact);for(const test of [originalTemplates,...checks])if(test.templateArtifact)verifyTemplateCopy(test.templateArtifact);
 verifyMonitorToolScope(toolAuthority);const toolRuntime=observeMonitorToolRuntime(toolAuthority);fs.writeFileSync(path.join(out,'tool-scope-post.json'),JSON.stringify({status:'PASS',toolAuthority,toolRuntime},null,2));
 const ownedSourceHashes=Object.assign({},originalTemplates.ownedSourceHashes,...checks.map(test=>test.ownedSourceHashes));
 const cases=[...checks.flatMap(test=>(test.receipt?.fullCases??test.receipt?.cases?.map(label=>({label,status:test.status}))??[]).map((item,i)=>({id:test.id+'-'+(i+1),status:item.status,input:{retainedCheck:item.label},consumer:test.id==='templates'?'actual model normalization/field bounds/coordinator with memory routing; label metadata migrated once':'actual native-clock/desktop owner/carrier/sample functions with inert injected native pulses',state:{originalAssertionsPassed:item.status==='PASS'},feedback:item.error??item.label,scope:item.label.startsWith('Host separates')||item.label.startsWith('Existing OVL has')?'source-contract':'behavioral-mock'}))),...browser.cases];
 if(!metadataMigrationVerified)cases.push({id:'template-migration-contract',status:'FAIL',scope:'source-contract',feedback:labelSourceOutcome.reason??'Original source is neither the exact legacy correction nor the current unchanged assertion; retained original cases failed.'});
 if(checks.some(test=>test.status!=='PASS'))cases.push({id:'retained-output-protocol',status:'FAIL',feedback:'retained output suite failed; see raw stderr/receipt'});
 const status=cases.length&&cases.every(c=>c.status==='PASS')&&browser.ok?'PASS':'FAIL';
 const legacyTemplateAudit=readJson(path.join(path.dirname(fileURLToPath(import.meta.url)),'legacy-template-audit.json'));
 const result={schema,sourceApproval:false,toolAuthority,toolRuntime,independentStages,ok:status==='PASS',status,scope:'selected formal output consumer paths and retained clock behavior; not whole-monitor or physical output acceptance',
 sourceChanges:[],sourceHashes:{...sourceHashes,...ownedSourceHashes},templateArtifact,templateArtifacts:[originalTemplates,...checks].filter(test=>test.templateArtifact).map(test=>test.templateArtifact),registrationAudit:registration,cases,inputConsumerStateFeedback:cases,
 executedProductModules:['src/bridge/monitorOutput.ts','src/bridge/monitorOutputCoordinator.ts','src/bridge/monitorLayout.ts','src/bridge/monitorHudClock.ts','src/bridge/desktopHudOwner.ts','src/bridge/monitorHudSampleSource.ts','src/monitorHudOutputEntry.ts','src/monitorNoticeOutputEntry.ts','src/bridge/monitorHudRenderer.ts','src/bridge/monitorHudLayoutLane.ts','src/bridge/desktopHudProtocol.ts','src/bridge/monitorShortcutNotice.ts','src/bridge/monitorHudTransportLease.ts'],
 executionMeaning:'Only named consumers and assertions above; other compiled dependency/source associations are not full behavior coverage.',
 retainedRegression:checks,originalTemplateBefore:originalTemplates,metadataMigration:{status:metadataMigrationVerified?'PASS':'FAIL',originalFailures,changes:templates.metadataAssertionChanges,sourceLabelOutcome:labelSourceOutcome,presetContract:templates.presetContract,negativeControls:migrationChecks},browserEvidence:path.join(out,'browser/result.json'),legacyTemplateAudit:{status:metadataMigrationVerified?(labelSourceOutcome.labelChangeApplied?'RESOLVED_STALE_LABEL_METADATA':'CURRENT_ORIGINAL_LABELS_WITHOUT_REWRITE'):'NOT_RESOLVED',templateSuiteAccepted:templates.status==='PASS',historicalRecord:legacyTemplateAudit},
 compatibility:{controlClockAll163AssertionsPreserved:checks.find(test=>test.id==='control-clock').allOriginalAssertionsPreserved,allBehaviorAssertionsPreserved:checks.every(test=>test.allBehaviorAssertionsPreserved),metadataOnlyChanges:templates.metadataAssertionChanges,historicalBuildDependency:false,sourceDrift:[],frames:'desktop-hud protocol 1 exact id/nonce/increasing sequence/transport and field deadlines',physicalOutputVerified:false},
 remainingGaps:[
 {id:'native-publisher-and-real-window',status:'NOT_EXECUTED',detail:'Only injected native ready/layout/frame/close/pulse acknowledgement; no real desktop/native window or sensor publisher.'},
 {id:'steam-rtss-settings',status:'IO_INJECTED',detail:'Steam/Decky/settings/RTSS ports are memory stubs; Steam/Decky runtime is not tested. Readonly source maintenance audit performed separately.'},
 {id:'whole-ui-and-physical',status:'NOT_EXECUTED',detail:'No entire App/UI engine/dragging/OS input, display driver, gameplay or measured CPU/FPS performance claim.'}],
 hardwareIo:false,realHostStarted:false,nativeAppStarted:false,physicalOutputVerified:false};
 fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2));return result;
}

// The registration gate above remains serial. Only isolated, nondependent stages
// overlap: original templates, migrated templates/clock, and browser consumers.
// Every original tool pre/post verification stays inside its owning stage.
export function runMonitorIndependentStage(stage,out,{templateArtifact,toolAuthority}={}){
 if(!['original-templates','browser'].includes(stage))throw Error('MONITOR_INDEPENDENT_STAGE_REQUIRED');
 return new Promise((resolve,reject)=>{
  const worker=new Worker(new URL(import.meta.url),{workerData:{monitorIndependentStage:stage,out,templateArtifact,toolAuthority}});
  const threadId=worker.threadId;let reply=null,failure=null;
  worker.once('message',value=>{reply=value;});worker.once('error',error=>{failure=error;});
  // Join even failed workers before reporting. No terminate() can orphan a
  // retained producer or bypass the browser's existing finally/terminal check.
  worker.once('exit',code=>{
   if(failure)return reject(failure);
   if(code!==0||reply?.kind!=='ymcc.monitor-independent-stage.v1'||reply.stage!==stage)return reject(Error('MONITOR_INDEPENDENT_STAGE_TERMINAL:'+stage+':'+code));
   if(!reply.ok)return reject(Object.assign(Error(reply.error?.message??'MONITOR_INDEPENDENT_STAGE_FAILED'),{stack:reply.error?.stack}));
   resolve({value:reply.value,terminal:{stage,threadId,pid:process.pid,exitCode:code,joined:true}});
  });
 });
}
export async function runIndependentMonitorStages(out,{templateArtifact,toolAuthority,launchStage=runMonitorIndependentStage,retainedRunner=runRetained}={}){
 const options={templateArtifact,toolAuthority};
 // Attach rejection handlers before the synchronous retained producer starts.
 const joined=Promise.allSettled([launchStage('original-templates',out,options),launchStage('browser',out,options)]);
 let checks,retainedError;try{checks=retainedRunner(path.join(out,'retained'),{ids:['templates','control-clock'],templateMode:'migrated',...options});}catch(error){retainedError=error;}
 const stages=await joined;
 if(retainedError)throw retainedError;
 const rejected=stages.find(stage=>stage.status==='rejected');if(rejected)throw rejected.reason;
 return {originalTemplates:stages[0].value.value,checks,browser:stages[1].value.value,
  independentStages:{mode:'owned-worker-threads-after-original-registration-gate',registrationGatePreserved:true,retainedChildTimeoutMs:30000,
   stages:stages.map(stage=>stage.value.terminal),allWorkersJoined:stages.every(stage=>stage.value.terminal.joined)}};
}
if(!isMainThread&&workerData?.monitorIndependentStage){
 const {monitorIndependentStage:stage,out,templateArtifact,toolAuthority}=workerData;
 try{
  const value=stage==='original-templates'?runRetained(path.join(out,'original-template-before'),{ids:['templates'],templateMode:'original',templateArtifact,toolAuthority})[0]
   :stage==='browser'?await runBrowser(path.join(out,'browser'),{toolAuthority}):(()=>{throw Error('MONITOR_INDEPENDENT_STAGE_REQUIRED');})();
  parentPort.postMessage({kind:'ymcc.monitor-independent-stage.v1',stage,ok:true,value});
 }catch(error){parentPort.postMessage({kind:'ymcc.monitor-independent-stage.v1',stage,ok:false,error:{message:error.message,stack:error.stack}});}
 finally{parentPort.close();}
}

if(isMainThread&&process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 try{if(process.argv[2]!=='--evidence-dir'||!process.argv[3]||process.argv.length!==4)throw Error('EXPECTED --evidence-dir <fresh-dir>');const r=await runMonitorOutputSignals({evidenceDir:process.argv[3]});console.log(JSON.stringify({schema,status:r.status,checks:r.cases.length,behavioral:r.cases.filter(c=>c.scope!=='source-contract').length,sourceContracts:r.cases.filter(c=>c.scope==='source-contract').length,templateMigration:r.metadataMigration.status}));process.exitCode=r.ok?0:1;}
 catch(error){console.error(error.stack);process.exitCode=1;}
}
