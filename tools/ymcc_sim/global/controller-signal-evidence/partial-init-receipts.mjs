import assert from 'node:assert/strict';
import fs from 'node:fs';
import {digest} from './source-fragments.mjs';
const need=(v,c)=>assert.ok(v,c);
const schema=JSON.parse(fs.readFileSync(new URL('./consumer-schema.json',import.meta.url),'utf8'));
const exact=(rows,names,key,code)=>need(Array.isArray(rows)&&rows.length===names.length&&new Set(rows.map(r=>r[key])).size===names.length&&names.every(n=>rows.some(r=>r[key]===n)),code);
export function validatePartialInit(group,{source,tool,sdk,tools,current,historical}) {
 need(group.pass===true&&group.wholeLifecycleAcceptance===false,'PARTIAL_GROUP_BOUNDARY');exact(group.results,schema.partialVariants,'variant','PARTIAL_GROUP_BOUNDARY');
 let checks=0,negativeBuilds=0;
 const fragmentHashes=current.partial();
 for(const {variant,exitCode,result:r} of group.results){
  const baseline=variant==='baseline',positive=variant==='current';
  need(r.variant===variant&&r.diagnosticEvidenceComplete===true&&r.sourceStable===true&&r.sourceClosureVerified===true&&
   r.actualWholeShutdownExecuted===true&&r.actualInitPrefixExecuted===true&&r.fullInitExecuted===false&&r.actualWorkerThreadExecuted===false&&
   r.wholeLifecycleAcceptance===false&&r.realSensorDsuDeviceDriverServiceOrSleepExecuted===false&&r.installedApplicationExecuted===false&&r.processExitUsedAsCleanup===false&&
   r.explicitFixtureActorCallsAreProductionSelfHealing===false&&r.historicalBaselineAndReceiptsRewritten===false,'PARTIAL_ACTUAL_CONSUMER_SCOPE');
  const bound=source(r.sourceHashes,'partial-init.'+variant,{baseline,legacy:variant==='legacy-emergency-mutant'});need(bound.has('native/main.cpp')&&bound.has('tools/fixtures/input_capture_timer_retirement/partial_init_baseline.json'),'PARTIAL_SOURCE_CLOSURE');
  if(baseline){need(r.mainInput?.isCurrentMainline===false&&r.mainInput?.sha256===historical['baseline-dependencies/native/main.cpp'].sha256&&r.historicalDependencyClosureVerified===true&&r.selectedGeneration==='frozen-baseline'&&r.actualRetireResourcesExecuted===false&&r.actualEmergencyCleanupExecuted===false&&r.baselineOriginalReceiptSha256===historical['baseline-dependencies/ORIGINAL-RESULT.json'].sha256,'PARTIAL_HISTORICAL_CLOSURE_REQUIRED');}
  else{
   need(r.mainInput?.isCurrentMainline===true&&r.actualEmergencyCleanupExecuted===true&&r.actualRetireResourcesExecuted===true&&r.actualCheckedCloseExecuted===true&&bound.has('tools/fixtures/input_capture_timer_retirement/partial_retirement_fixture.cpp.in')&&bound.has('native/input_capture_completion.h'),'PARTIAL_ACTUAL_RETIREMENT_REQUIRED');
   need(Object.keys(r.currentFragmentHashes??{}).length===Object.keys(fragmentHashes).length&&Object.entries(fragmentHashes).every(([k,h])=>r.currentFragmentHashes[k]===h),'PARTIAL_CURRENT_FRAGMENT_BINDING');
   if(positive)need(Object.entries(fragmentHashes).every(([k,h])=>r.selectedFragments?.[k]?.sha256===h)&&r.selectedShutdownIsVerbatimCurrentMainline===true&&r.selectedPrefixIsVerbatimCurrentMainline===true,'PARTIAL_CURRENT_FRAGMENT_BINDING');
  }
  tool(r.compiler?.path,tools.native.compiler,r.compiler?.sha256,'partial-init.'+variant);tool(r.compiler?.vcvarsPath,tools.vcvars,r.compiler?.vcvarsSha256,'partial-init.vcvars.'+variant);sdk(r.compiler?.sdkVersion,'partial-init.'+variant);
  exact(r.profiles,['Release','Checked'],'profile','PARTIAL_PROFILES_REQUIRED');
  if(positive)need(exitCode===0&&r.pass===true&&r.currentSourceResourceContractAccepted===true&&r.checks===7258&&r.failures===0,'PARTIAL_CURRENT_SOURCE_REQUIRED');
  else need(exitCode===2&&r.pass===false&&r.currentSourceResourceContractAccepted===false&&r.negativeReproductionComplete===true,'PARTIAL_NEGATIVE_NOT_ACCEPTED');
  for(const p of r.profiles){
   need(p.allLeavesMemoryOnly===true&&p.fullInitExecuted===false&&p.processExitUsedAsCleanup===false,'PARTIAL_SCENARIOS_REQUIRED');
   exact(p.cases,baseline?schema.partialCases.slice(0,14):variant==='legacy-emergency-mutant'?schema.partialLegacyEmergencyCases:schema.partialCases,'name','PARTIAL_SCENARIOS_REQUIRED');
   need(p.checks===p.cases.reduce((n,c)=>n+c.checks,0)&&p.failures===p.cases.reduce((n,c)=>n+c.failures.length,0)&&p.cases.every(c=>Number.isInteger(c.checks)&&c.checks>=3),'PARTIAL_FAILURE_ACCOUNTING');
   const caseOf=n=>p.cases.find(c=>c.name===n),first=n=>caseOf(n).observations[0];
   if(positive){
    need(p.processExitCode===0&&p.fixturePass===true&&p.checks===3629&&p.failures===0&&p.cases.every(c=>c.pass===true&&c.failures.length===0),'PARTIAL_CURRENT_NOT_CLEAN');
    const prefix=p.cases.filter(c=>c.kind==='actual-contiguous-Init-prefix-only');need(prefix.length===6,'PARTIAL_PREFIX_BOUNDARY');
    const recovered=p.cases.filter(c=>!prefix.includes(c)&&c.name!=='emergency-gm-without-dll');
    need(recovered.length===50&&recovered.every(c=>c.finalAllResourcesReleased===true&&c.observations.at(-1).resourceCount===0),'PARTIAL_FINAL_MEMORY_OWNERS_RETAINED');
    for(const n of ['normal-sensor-shutdown-fails-then-settles','partial-sensor-shutdown-fails-then-settles','retire-sta-timeout','retire-sta-wait-failed','retire-metadata-is-not-destruction','emergency-pending-children']){
     const s=first(n);need(s.sinksAlive===1&&s.sensorThreadsAlive===1&&s.sensorEventsAlive===1&&s.sensorDisposalProven===false&&s.gmObjectsAlive===1&&s.dllRefsAlive===1&&(s.calls.gmDelete??0)===0&&(s.calls.freeLibrary??0)===0,'PARTIAL_SENSOR_RETENTION_IS_NOT_DISPOSAL');
    }
    const retained={'retire-sensor-thread-close-failure':['sensorThreadPointer','sensorThread'],'retire-sensor-wake-close-failure':['sensorWakePointer','sensorEvent'],'retire-oem-device-close-failure':['oemDevicePointer','oemDevice'],'retire-oem-event-close-failure':['oemEventPointer','oemEvent'],'retire-onex-device-close-failure':['onexDevicePointer','onexDevice'],'retire-onex-read-event-close-failure':['onexReadEventPointer','onexReadEvent'],'retire-onex-write-event-close-failure':['onexWriteEventPointer','onexWriteEvent'],'retire-timer-close-failure':['timerEventPointer','timerEvent'],'retire-notification-close-failure':['notificationPointer','notification'],'retire-free-library-failure':['dllPointer','dll']};
    for(const [name,[pointer,kind]] of Object.entries(retained)){const s=first(name);need(s[pointer]===true&&s.liveByKind[kind]>0,'PARTIAL_FAILED_CLOSE_LOST_OWNER:'+name);}
    const orphan=caseOf('emergency-gm-without-dll'),s=first(orphan.name);need(orphan.expectedUnresolvedMotionDebt===true&&orphan.finalAllResourcesReleased===false&&orphan.finalDebt===512&&orphan.boundedDestructionProven===false&&s.gmObjectsAlive===1&&s.gmPointer===true&&s.dllPointer===false&&orphan.observations.at(-1).resourceCount===2,'PARTIAL_ORPHAN_MOTION_DEBT_UNPROVEN');
    checks+=p.checks;
   }else{
    need(p.processExitCode===2&&p.fixturePass===false&&p.failures>0&&p.specifiedNegativeObserved===true,'PARTIAL_NEGATIVE_COUNTS_REQUIRED');negativeBuilds++;
    if(baseline)need(p.checks===384&&p.failures===201,'PARTIAL_HISTORICAL_COUNTS_CHANGED');
    const a=first('partial-sensor-start-failed'),n=first('normal-ready'),s=first('normal-sensor-shutdown-fails-then-settles');
    const guard=a.sinksAlive===1&&a.sensorThreadsAlive===1&&a.dllRefsAlive===1&&a.gmObjectsAlive===1&&(a.calls.sensorShutdown??0)===0&&(a.calls.gmDelete??0)===0&&(a.calls.freeLibrary??0)===0;
    let caught={baseline:guard,'ready-guard-mutant':guard,'skip-gm-delete-mutant':n.gmObjectsAlive===1&&(n.calls.gmDelete??0)===0,'skip-gm-free-mutant':n.dllRefsAlive===1&&(n.calls.freeLibrary??0)===0,'drop-sink-on-failure-mutant':s.sensorThreadsAlive===1&&s.sinksAlive===0}[variant];
    const targeted={'unsettled-sensor-unloads-gm-mutant':['sta-timeout','gmObjectsAlive'],'unchecked-sensor-close-mutant':['sensor-thread-close-failure','sensorThreadPointer','sensorThread'],'unchecked-oem-close-mutant':['oem-device-close-failure','oemDevicePointer','oemDevice'],'unchecked-onex-close-mutant':['onex-device-close-failure','onexDevicePointer','onexDevice'],'unchecked-timer-close-mutant':['timer-close-failure','timerEventPointer','timerEvent'],'unchecked-notification-close-mutant':['notification-close-failure','notificationPointer','notification'],'failed-free-nulls-owner-mutant':['free-library-failure','dllPointer','dll']};
    if(targeted[variant]){const [name,field,kind]=targeted[variant],c=caseOf('retire-'+name),v=c.observations[0];caught=c.pass===false&&(kind?!v[field]&&v.liveByKind[kind]>0:v.sensorThreadsAlive===1&&v[field]===0);}
    if(variant==='legacy-emergency-mutant'){const a=first('emergency-pending-children'),b=first('emergency-missing-delete-export'),c=first('emergency-free-library-failure');caught=r.legacyEmergencyDependencyClosureMatched&&a.sensorThreadsAlive===1&&a.gmObjectsAlive===0&&b.gmObjectsAlive===1&&!b.gmPointer&&c.dllRefsAlive===1&&!c.dllPointer;}
    if(variant==='gm-without-dll-loses-motion-debt-mutant'){const c=caseOf('emergency-gm-without-dll');caught=c.pass===false&&c.finalDebt===0&&c.observations[0].gmObjectsAlive===1;}
    need(caught,'PARTIAL_NEGATIVE_RESOURCE_WITNESS_REQUIRED');
   }
  }
 }
 return {id:'actual-partial-init-shutdown',pass:true,currentBuilds:2,currentChecks:checks,currentCasesPerProfile:57,recoverableResourceCasesPerProfile:50,retainedOrphanMotion:'UNKNOWN',negativeBuilds,actualWholeShutdown:true,actualEmergencyCleanup:true,fullInitExecuted:false,wholeLifecycleAcceptance:false};
}
