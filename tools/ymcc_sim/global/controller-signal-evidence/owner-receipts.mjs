import assert from 'node:assert/strict';
import fs from 'node:fs';
const need=(v,c)=>assert.ok(v,c);
const schema=JSON.parse(fs.readFileSync(new URL('./consumer-schema.json',import.meta.url),'utf8'));
export function exact(rows,names,key,code){need(Array.isArray(rows)&&rows.length===names.length&&new Set(rows.map(r=>r[key])).size===names.length&&names.every(n=>rows.some(r=>r[key]===n)),code);}
export function validateAdmission(r,context){
 const {source,current}=context;source(r.sourceHashes,'stop-receipt');current.hashes(r.actualFunctions,'stop-receipt');
 need(r.pass===true&&r.status==='PASS'&&r.evidenceComplete===true&&r.sourceStable===true&&r.sourceFreezeMainMatched===true&&r.wholeLifecycleAcceptance===false&&r.actualRetireResourcesBodyExecuted===false&&r.actualStartWorkerBodyExecuted===false&&r.arbitraryBlockedParentBoundedDestruction==='UNKNOWN'&&r.processExitUsedAsCleanup===false,'STOP_RECEIPT_BOUNDARY');
 need(r.profiles?.length===4,'STOP_RECEIPT_VARIANTS');
 for(const profile of ['Release','Checked'])for(const route of ['current','negative-without-admission']){
  const rows=r.profiles.filter(x=>x.profile===profile&&x.route===route);need(rows.length===1,'STOP_RECEIPT_VARIANTS');const p=rows[0],positive=route==='current';
  need(p.checks===(positive?59:17)&&p.failures===(positive?0:4)&&p.expectedFailureCaught===!positive,'STOP_RECEIPT_SPECIFIED_ADMISSION_NEGATIVE');
  exact(p.cases,positive?[0,1,2,3]:[0],'stage','STOP_RECEIPT_SCHEDULES');
  need(p.cases.every(c=>c.productionStartAndStopExecuted===true&&c.starterJoined===true&&c.workerJoined===true&&c.ownedHandlesRemaining===0&&c.contextRetained===false&&c.processExitUsedAsCleanup===false&&c.hostCleanupWhileWorkerLive===(positive?0:1)),'STOP_RECEIPT_LOST_OWNER');
  need(p.lease?.stopOwnerDebt===2048&&p.lease?.settledAfterLeaseRelease===true,'STOP_RECEIPT_STOP_LEASE_DEBT');
  if(positive)need(p.cases.every(c=>c.fixturePass===true&&c.finalStopSettled===true&&c.firstStopSettled===false&&c.firstStopDebts===1),'STOP_RECEIPT_START_RESERVATION_DEBT');
  else need(p.cases[0].firstStopSettled===true&&p.cases[0].firstStopDebts===0,'STOP_RECEIPT_SPECIFIED_ADMISSION_NEGATIVE');
 }
 return {id:'actual-typed-stop-receipt',pass:true,profiles:2,schedules:8,admissionNegativeBuilds:2,wholeLifecycleAcceptance:false};
}
export function validateDsu(r,{source,tool,tools}){
 need(r.status==='PASS'&&Array.isArray(r.sources),'DSU_BOUNDARY');source(r.sources,'dsu-owner');source(r.sourceIdentityAfter,'dsu-owner.after');
 need(r.sourceIdentityAfter.length===r.sources.length&&r.sourceIdentityAfter.every(x=>x.unchanged===true),'DSU_SOURCE_DRIFT');tool(r.toolchain?.vcvars,tools.vcvars,undefined,'dsu.vcvars');exact(r.runs,schema.dsuRuns,'name','DSU_VARIANTS');
 for(const row of r.runs){const positive=!row.name.startsWith('negative-');need(row.accepted===true&&row.compile?.exitCode===0&&row.compile?.signal===null&&row.compile?.error===null&&row.runtime?.exitCode===(positive?0:1)&&row.runtime?.signal===null&&row.runtime?.error===null,'DSU_ACTUAL_PROCESS_OUTCOME');
  const summary=row.events?.findLast(e=>e.status==='PASS'||e.status==='FAIL');
  if(positive){const privateRun=row.name.startsWith('private-');need(summary?.status==='PASS'&&summary.cases===(privateRun?28:8)&&summary.checks===(privateRun?391:88),'DSU_CASE_INVENTORY');need(row.events.filter(e=>e.loopback===true).length===(privateRun?7:6),'DSU_LOOPBACK_EXECUTION');if(privateRun)need(row.events.some(e=>e.apiReleaseAttempts===162&&e.apiReleaseSuccesses===125&&e.injectedFailures===48&&e.blockedUnsafeApiCalls===0&&e.emergencyCleanup===0),'DSU_OWNERS_RETIRED');}
  else{const expected=row.name==='negative-running-only'?'running-false-with-exited-unjoined-thread':'stop-failure-retains-closesocket';need(summary?.status==='FAIL'&&summary.case===expected&&row.events.at(-1)?.rescueOwnedResourcesReleased===true,'DSU_SPECIFIED_OWNER_NEGATIVE');}
 }
 return {id:'dsu-resource-owner',pass:true,publicBuilds:2,privateBuilds:2,negativeBuilds:2,actualLoopback:true,arbitraryBlockingDestruction:'UNKNOWN'};
}
export function validateSensors(r,{source,tool,sdk,tools}){
 source(r.sources,'sensor-owners');tool(r.compiler?.path,tools.native.compiler,r.compiler?.sha256,'sensor.compiler');tool(r.compiler?.vcvarsPath,tools.vcvars,r.compiler?.vcvarsSha256,'sensor.vcvars');sdk(r.compiler?.sdkVersion,'sensor');
 need(r.pass===true&&r.status==='PASS'&&r.sourceCurrent===true&&(!r.sourceDrift || r.sourceDrift.length===0)&&r.boundedArbitraryComDestructionProven===false&&r.fullTypedCaptureStopIntegrated===false&&r.installedAppDeviceDriverSleepDeployment===false&&r.defaultSensorActivationAllowed===false,'SENSOR_BOUNDARY');
 need(r.profiles?.length===20,'SENSOR_VARIANTS');const failures={'provider-running-is-stopped':7,'provider-unchecked-close':5,'provider-start-unknown':4,'census-unchecked-close':5,'census-cancelled-is-stopped':4};
 for(const profile of ['Release','Checked']){const rows=r.profiles.filter(x=>x.profile===profile);exact(rows,schema.sensorCases,'case','SENSOR_VARIANTS');for(const p of rows){const negative=Object.hasOwn(failures,p.case);need(p.buildExit===0&&p.accepted===true&&p.expectedNegative===negative&&p.exit===(negative?1:0),'SENSOR_PROCESS_OUTCOME');if(negative)need(p.checks===89&&p.failures===failures[p.case],'SENSOR_SPECIFIED_OWNER_NEGATIVE');else if(['memory','kernel'].includes(p.case))need(p.checks===(p.case==='memory'?89:61)&&p.failures===0,'SENSOR_OWNED_KERNEL_OR_MEMORY_CASES');}}
 need(r.inventoryConsumers?.exit===0,'SENSOR_INVENTORY_CONSUMER');exact(r.inventoryConsumers.configurations,['Release','Checked'],'name','SENSOR_INVENTORY_CONFIGS');for(const p of r.inventoryConsumers.configurations){const v=p.report;need(v?.checks===201&&v.failures?.length===0&&v.actualNativeQueryAndExport===true&&v.actualSdkMetadataCensus===true&&v.hardwareIo===false&&v.normalAppStarted===false&&v.newProductionDlls?.length===0,'SENSOR_INVENTORY_BOUNDARY');}
 return {id:'sensor-resource-owners',pass:true,builds:20,privateKernelBuilds:2,negativeBuilds:10,inventoryBuilds:2,arbitraryComDestructionProven:false};
}
export function validateX2(r,{source,sdk}){
 source(r.inputs,'x2-owner');sdk(r.compiler?.windowsSdkVersion,'x2');need(r.status==='pass'&&r.realDeviceApiCalls===0&&r.hardwareAccess===false&&r.network===false&&r.deployment===false&&r.drift?.length===0&&r.apiBoundary?.unmockedSdkCalls?.length===0&&r.apiBoundary?.allWaitTimeoutsAssertedZero===true,'X2_MEMORY_BOUNDARY');
 exact(r.configurations,['Release','Checked'],'name','X2_CONFIGURATIONS');need(JSON.stringify(r.scenarios)===JSON.stringify(schema.x2Cases),'X2_SCENARIO_DECLARATION');
 for(const p of r.configurations){need(p.status==='pass'&&p.checks===110750&&p.deviceApiUndefinedSymbols===0&&p.deviceApiImports===0,'X2_REAL_DEVICE_IMPORT');exact(p.cases,schema.x2Cases,'scenario','X2_SCENARIOS');need(p.cases.every(c=>c.status==='pass'&&c.exitCode===0&&Number.isInteger(c.checks)&&c.checks>0),'X2_SCENARIO_OUTCOME');}
 exact(r.mutations,schema.x2Mutations,'name','X2_OWNER_NEGATIVES');need(r.mutations.every(m=>m.status==='detected'&&m.report?.status==='fail'&&m.report?.section),'X2_OWNER_NEGATIVE_NOT_CAUGHT');
 return {id:'x2-resource-owner',pass:true,builds:2,casesPerBuild:46,negativeBuilds:24,realDeviceCalls:0};
}
export function validateGamebar(r,{source,tool,tools}){
 source(r.sourceHashes,'gamebar-owner');tool(r.compiler,tools.native.compiler,undefined,'gamebar.compiler');need(r.status==='PASS'&&r.pass===true&&Object.keys(r.sourceCurrentAtFinish??{}).length===5&&Object.values(r.sourceCurrentAtFinish).every(x=>x===true),'GAMEBAR_SOURCE_DRIFT');
 exact(r.profiles,['Release','Checked'],'name','GAMEBAR_PROFILES');for(const p of r.profiles){need(p.actualHeaderIncluded===true&&p.realSdkCompileOnly===true&&p.forbiddenImportsAbsent===true,'GAMEBAR_MEMORY_AND_SDK_BOUNDARY');exact(p.cases,schema.gamebarCases,'name','GAMEBAR_SCENARIOS');need(p.cases.reduce((n,c)=>n+c.observed.checks,0)===343,'GAMEBAR_CHECK_COUNTS');need(p.cases.every(c=>c.exit===0&&c.observed.failures===0&&c.observed.remainingKernelHandles===0&&c.observed.remainingSubscriptions===0&&c.observed.active===0&&c.observed.fallbackCleanup===0),'GAMEBAR_LOST_OWNER');}
 need(r.mutations?.length===4,'GAMEBAR_NEGATIVES');for(const profile of ['Release','Checked']){const rows=r.mutations.filter(m=>m.profile===profile);exact(rows,schema.gamebarMutations,'name','GAMEBAR_NEGATIVES');need(rows.every(m=>m.exit===1&&m.detected===true&&m.observed.failures>0),'GAMEBAR_OWNER_NEGATIVE_NOT_CAUGHT');}
 return {id:'gamebar-resource-owner',pass:true,builds:2,ownedKernelCases:48,negativeBuilds:4,wholeCallBoundedWinRTShutdown:'UNKNOWN'};
}
export function validateExit(r,{source,current,tool,sdk,tools}){
 source(r.sourceHashes,'exit-continuation');tool(r.compiler?.path,tools.native.compiler,r.compiler?.sha256,'exit.compiler');sdk(r.compiler?.sdkVersion,'exit');
 need(r.pass===true&&r.status==='PASS'&&r.diagnosticEvidenceComplete===true&&r.productionExitContinuationAccepted===true&&r.sourceStable===true&&r.wholeLifecycleAcceptance===false&&r.installedApplicationExecuted===false&&r.realOwnedKernelObjectsOnly===true&&r.realCaptureOrHostExecuted===false&&r.arbitraryBlockedParentBoundedDestruction==='UNKNOWN'&&r.realChildResourcesExecuted===false&&r.actualRetireResourcesBodyExecuted===false&&r.historicalPass===false&&r.historicalStatus==='PRODUCTION_QUIT_BEFORE_CAPTURE_SETTLED','EXIT_CONTINUATION_BOUNDARY');
 const expected=current.exit();need(Object.keys(expected).length===Object.keys(r.actualFragments??{}).length&&Object.entries(expected).every(([k,h])=>r.actualFragments[k]===h),'EXIT_CURRENT_VERBATIM_FRAGMENTS');need(r.profiles?.length===4,'EXIT_CONTINUATION_VARIANTS');
 for(const profile of ['Release','Checked'])for(const route of ['historical','current']){const rows=r.profiles.filter(x=>x.profile===profile&&x.route===route);need(rows.length===1,'EXIT_CONTINUATION_VARIANTS');const p=rows[0],positive=route==='current';need(p.compiled===true&&p.executed===true&&p.fixturePass===true&&p.pass===positive&&p.status===(positive?'PASS':'PRODUCTION_QUIT_BEFORE_CAPTURE_SETTLED')&&p.cases?.length===(positive?3:2),'EXIT_CONTINUATION_SCENARIOS');
  need(p.cases.every(c=>c.actualDestroyExitPathExecuted===true&&c.actualMainFinallyExecuted===true&&c.fixturePass===true&&c.ownedThreadJoined===true&&c.ownedHandlesRemaining===0&&c.ownedEventsRemaining===0&&c.contextRetained===false&&c.processExitUsedAsCleanup===false&&c.wholeLifecycleAcceptance===false),'EXIT_CONTINUATION_LOST_OWNER');
  if(positive)need(p.cases.every(c=>c.actualExitGateAndBackgroundOwnerExecuted===true&&c.cleanupJoinedBeforeClose===1&&c.cleanupClosed===1&&c.productionExitContinuationGapObserved===false&&c.coUninitializeBeforeThreadJoin===false&&c.sealBeforeThreadJoin===false),'EXIT_GATE_BEFORE_OWNER_RETIREMENT');
  else need(p.cases.filter(c=>c.productionExitContinuationGapObserved===true&&c.held===true&&c.quitBeforeThreadJoin===true).length===1,'EXIT_HISTORICAL_GAP_REQUIRED');
 }
 return {id:'actual-exit-continuation',pass:true,currentBuilds:2,historicalGapReproduced:true,wholeLifecycleAcceptance:false};
}
