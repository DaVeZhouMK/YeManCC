import {validateManagedTerminal,managedCurrentVersion} from './managed-terminal-receipts.mjs';
import {validatePrimaryThread} from './primary-thread-receipts.mjs';
import {validateExitFence} from './exit-fence-receipts.mjs';
import {validateOwnedCleanupComposition} from './owned-cleanup-composition-receipts.mjs';
import {validateSensorEnsure} from './sensor-ensure-receipts.mjs';
import {validatePartialInit} from './partial-init-receipts.mjs';
import {validateAdmission, validateDsu, validateSensors, validateX2, validateGamebar, validateExit} from './owner-receipts.mjs';
import {currentFragments} from './source-fragments.mjs';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
export const hashFile = file => createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const need = (condition, code) => assert.ok(condition, code);
const shaPattern = /^[a-f0-9]{64}$/;
export const CHILDREN = Object.freeze(['source-binding.json', 'descriptor-decode/source-binding.json',
  'semantic-witness/RESULT.json', 'timer-recorder/RESULT.json', 'resume-generation/RESULT.json', 'timer-adapter/RESULT.json', 'worker-retirement/RESULT.json', 'stop-wait/RESULT.json', 'startup-wait/result.json', 'startup-lifecycle/RESULT.json', 'exit-fence/RESULT.json', 'partial-init/RESULT.json',
  'stop-receipt/RESULT.json', 'exit-continuation/RESULT.json', 'dsu-owner/RESULT.json', 'sensor-owners/RESULT.json', 'x2-owner/RESULT.json', 'gamebar-owner/RESULT.json', 'sensor-ensure/RESULT.json', 'owned-cleanup-composition/RESULT.json', 'primary-thread/RESULT.json', 'managed-terminal/RESULT.json']);
function sourceRows(rows, repo, expected, label) {
  need(Array.isArray(rows) && rows.length > 0, label + ':SOURCE_ROWS_MISSING');
  const seen = new Set();
  for (const row of rows) {
    const relative = path.isAbsolute(row.path) ? path.relative(repo, row.path).replaceAll('\\', '/') : row.path;
    need(Object.hasOwn(expected, relative) && !seen.has(relative), label + ':SOURCE_OUTSIDE_DECLARATION_OR_DUPLICATE');
    seen.add(relative);
    need(shaPattern.test(row.sha256) && row.sha256 === expected[relative] && hashFile(path.join(repo, relative)) === row.sha256,
      label + ':SOURCE_HASH_MISMATCH:' + relative);
  }
  return seen;
}
export function validateReceipts({repo, output, sourceHashes, tools}) {
  need(tools?.native && tools.files?.length > 0 && tools.sdkVersion, 'REVIEWED_TOOL_IDENTITIES_REQUIRED');
  const normalized = value => path.normalize(value).toLowerCase();
  const toolMap = new Map(tools.files.map(row => [normalized(row.path), row]));
  function tool(actualPath, expectedPath, reportedHash, label) {
    need(typeof actualPath === 'string' && normalized(actualPath) === normalized(expectedPath), label + ':ACTUAL_TOOL_PATH_MISMATCH');
    const declared = toolMap.get(normalized(expectedPath));
    need(declared && hashFile(actualPath) === declared.sha256 &&
      (reportedHash === undefined || reportedHash === declared.sha256), label + ':ACTUAL_TOOL_HASH_MISMATCH');
  }
  const sdk = (value, label) => need(typeof value === 'string' && value.replace(/[\\/]$/, '') === tools.sdkVersion, label + ':ACTUAL_SDK_MISMATCH');

  const receiptHashes = [], values = new Map();
  for (const name of CHILDREN) {
    const file = path.join(output, name), value = JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
    const ownerStatus = ['dsu-owner/RESULT.json', 'x2-owner/RESULT.json'].includes(name);
    if(name==='owned-cleanup-composition/RESULT.json') need(value.pass===true&&value.status==='ACTUAL_WRAPPER_AND_OWNED_PREDICATES_COMPOSED_PASS',name+':CHILD_NOT_PASS');
    else if(name==='primary-thread/RESULT.json') need(value.pass===true&&value.status==='PASS_CURRENT_CONSUMERS_WITH_PRIVATE_KERNEL_OWNERS',name+':CHILD_NOT_PASS');
    else if (ownerStatus) need(value.status === 'PASS' || value.status === 'pass' || value.pass === true, name + ':CHILD_NOT_PASS');
    else need(![value.pass, value.passed, value.ok].includes(false) &&
      (value.status === undefined || value.status === 'PASS'), name + ':CHILD_NOT_PASS');
    values.set(name, value); receiptHashes.push({path: name, sha256: hashFile(file)});
  }
  const acquisition = values.get(CHILDREN[0]);
  sourceRows(acquisition.source, repo, sourceHashes, 'acquisition');
  need(acquisition.memoryRawInputBoundaries === true && acquisition.productionHelperExercised === true &&
    acquisition.realRawInputCalls === 0 && acquisition.physicalInputVerified === false, 'ACQUISITION_BOUNDARY');
  for (const variant of ['default', 'partial', 'explicit']) {
    const name = variant + '-result.txt', text = fs.readFileSync(path.join(output, name), 'utf8');
    need(/cases=51 failures=0 realRawInputCalls=0 RESULT=PASS\s*$/.test(text), 'ACQUISITION_ACTUAL_VARIANT:' + variant);
    receiptHashes.push({path: name, sha256: hashFile(path.join(output, name))});
  }
  const decode = values.get(CHILDREN[1]);
  sdk(decode.sdkVersion, 'decode');
  sourceRows(decode.source, repo, sourceHashes, 'decode');
  need(decode.realRawInputCalls === 0 && decode.realHidCalls === 0 && decode.productionHelperExercised === true,
    'DECODE_BOUNDARY');
  need(decode.results?.length === 4 && decode.results.every(row => row.passed === true), 'DECODE_CONSUMER_VARIANTS');
  const witness = values.get(CHILDREN[2]);
  sourceRows(witness.source, repo, sourceHashes, 'witness');
  need(witness.pass === true && witness.realRawInputCalls === 0 && witness.realHidCalls === 0 && witness.physicalSuppressionProven === false,
    'WITNESS_BOUNDARY');
  need(witness.cases?.length === 4 && witness.cases.filter(row => row.expectedNegative === false).length === 2 &&
    witness.cases.filter(row => row.expectedNegative === false).every(row => row.exit === 0 && row.failures === 0), 'WITNESS_POSITIVE_VARIANTS');
  need(witness.cases.filter(row => row.expectedNegative === true).length === 2 &&
    witness.cases.filter(row => row.expectedNegative === true).every(row => row.exit > 0 && row.failures > 0 && row.expectedFailure), 'WITNESS_SPECIFIED_NEGATIVES');
  const timer = values.get(CHILDREN[3]);
  need(shaPattern.test(timer.compiler?.sha256), 'timer:COMPILER_HASH_MISSING');
  tool(timer.compiler?.path, tools.native.compiler, timer.compiler?.sha256, 'timer');
  sdk(timer.compiler?.windowsSdkVersion, 'timer');
  sourceRows(timer.sourceBindings, repo, sourceHashes, 'timer');
  need(timer.pass === true && timer.sourceStableDuringRun === true && timer.realTimerOrInputIO === false &&
    timer.callbacksDrainedProven === false && timer.steamConsumptionProven === false, 'TIMER_BOUNDARY');
  need(timer.configurations?.length === 2 && timer.configurations.every(row => row.pass === true &&
    row.burstEntries === 100000 && row.watchedAllocations === 0 && row.concurrency?.conservationPassed === true), 'TIMER_RUNTIME_VARIANTS');
  need(timer.negativeRejections === 4 && timer.negatives?.length === 4 &&
    timer.negatives.every(row => row.caught === true && row.exitCode > 0 && row.expected), 'TIMER_SPECIFIED_NEGATIVES');
  const resume = values.get(CHILDREN[4]);
  tool(resume.toolchain?.compiler, tools.native.compiler, undefined, 'resume.compiler');
  tool(resume.toolchain?.dumpbin, tools.native.dumpbin, undefined, 'resume.dumpbin');
  tool(resume.toolchain?.vcvars64, tools.vcvars, undefined, 'resume.vcvars');
  sdk(resume.toolchain?.windowsSdkVersion, 'resume');
  need(JSON.stringify(resume.toolchain?.explicitSdkInclude?.map(normalized)) === JSON.stringify(tools.native.include.slice(1, 5).map(normalized)) &&
    JSON.stringify(resume.toolchain?.explicitSdkLib?.map(normalized)) === JSON.stringify(tools.native.lib.slice(1).map(normalized)), 'RESUME_ACTUAL_INCLUDE_LIB_MISMATCH');
  sourceRows(Object.entries(resume.binding?.sourceSha256Before ?? {}).map(([path, sha256]) => ({path, sha256})), repo, sourceHashes, 'resume');
  need(resume.binding?.runnerSha256 === sourceHashes['tools/msi_visibility_resume_generation_selftest.py'], 'RESUME_RUNNER_SOURCE');
  need(resume.pass === true && resume.positive?.pass === true && resume.negative?.pass === true &&
    resume.negative?.kind === 'expected-failure' && resume.negativeExpectedFailureDetected === true &&
    resume.finalSnapshotBindingStable === true && resume.realDeviceSteamInstallSleepFanIO === false, 'RESUME_ACTUAL_HOOK_AND_NEGATIVE');
  const adapter = values.get(CHILDREN[5]);
  need(shaPattern.test(adapter.compiler?.sha256) && shaPattern.test(adapter.compiler?.vcvarsSha256), 'adapter:COMPILER_HASH_MISSING');
  tool(adapter.compiler?.path, tools.native.compiler, adapter.compiler?.sha256, 'adapter');
  tool(adapter.compiler?.vcvarsPath, tools.vcvars, adapter.compiler?.vcvarsSha256, 'adapter.vcvars');
  sdk(adapter.compiler?.sdkVersion, 'adapter');
  const adapterSources = sourceRows(Object.values(adapter.binding?.sources ?? {}), repo, sourceHashes, 'actual-adapter');
  need(adapterSources.has('native/main.cpp') && adapterSources.has('native/input_timer_evidence.h'), 'ACTUAL_MAIN_SOURCE_MISSING');
  need(adapter.pass === true && adapter.binding?.testedFragmentsStable === true && adapter.checks?.negativeCompiles === 4 &&
    adapter.checks?.positiveCounterCompiles === 2 && adapter.profiles?.length === 2, 'ACTUAL_ADAPTER_VARIANTS');
  need(adapter.profiles.every(row => row.pass === true && row.positiveCounterCompile === true && row.realControlImportsAbsent === true &&
    row.negativeCompiles?.length === 2 && row.negativeCompiles.every(n => n.pass === true && n.exitCode > 0)), 'ACTUAL_ATOMIC_NEGATIVES');
  const retirement = values.get(CHILDREN[6]);
  sourceRows(Object.entries(retirement.sourceHashes ?? {}).map(([path,sha256])=>({path,sha256})), repo, sourceHashes, 'worker-retirement');
  need(retirement.pass === true && retirement.evidenceComplete === true && retirement.sourceStable === true &&
    retirement.productionRetirementAccepted === true && retirement.currentProductionSynchronous === true &&
    retirement.currentProductionFaultStopGuards === 2 && retirement.realWinmmOrDeviceCalls === false, 'ACTUAL_CURRENT_WORKER_RETIREMENT');
  need(shaPattern.test(retirement.compiler?.sha256) && shaPattern.test(retirement.compiler?.vcvarsSha256), 'retirement:COMPILER_HASH_MISSING');
  tool(retirement.compiler?.path, tools.native.compiler, retirement.compiler?.sha256, 'retirement');
  tool(retirement.compiler?.vcvarsPath, tools.vcvars, retirement.compiler?.vcvarsSha256, 'retirement.vcvars');
  sdk(retirement.compiler?.sdkVersion, 'retirement');
  need(retirement.profiles?.length === 6, 'RETIREMENT_BUILD_VARIANTS');
  for (const profile of ['Release','Checked']) {
    const variants = retirement.profiles.filter(row => row.profile === profile);
    need(variants.length === 3, 'RETIREMENT_PROFILE_MISSING');
    for (const route of ['legacy','synchronous','negative-sync-without-fault-stop']) {
      const selected = variants.filter(row => row.route === route);
      need(selected.length === 1 && selected[0].cases?.length === 6, 'RETIREMENT_CASE_VARIANT_MISSING');
      const row = selected[0];
      need(row.cases.every(c => c.actualCallbackExecuted && c.actualWorkerExecuted && c.ownedThreadsJoined &&
        c.ownedActiveCallbacksAfter === 0 && c.remainingFixtureEvents === 0 && !c.gateTimedOut), 'RETIREMENT_OWNED_FIXTURE_NOT_RETIRED');
      if (route === 'legacy') need(row.isCurrentProductionRoute === false && row.legacyRejectedAsSafe === true && row.negativeExitCode === 1 && row.negativeFailures === 24 && row.cases.every(c => c.unsafeObserved && c.closedEventSignals === 1), 'RETIREMENT_LEGACY_DEFECT_NOT_REPRODUCED');
      else if (route === 'synchronous') need(row.isCurrentProductionRoute === true && row.cases.every(c => !c.unsafeObserved && c.stopAtCancel === true && c.closedEventSignals === 0 && c.busCalls === 0), 'RETIREMENT_CURRENT_SOURCE_NOT_ACCEPTED');
      else need(row.expectedFailureCaught === true && row.exitCode === 1 && row.failures === 6, 'RETIREMENT_STOP_REMOVAL_NOT_REJECTED');
    }
  }
  const stop = values.get(CHILDREN[7]);
  sourceRows(Object.entries(stop.sourceHashes ?? {}).map(([path,sha256])=>({path,sha256})), repo, sourceHashes, 'stop-wait');
  need(stop.pass === true && stop.evidenceComplete === true && stop.sourceStable === true &&
    stop.productionOwnershipCheckAccepted === true && stop.currentProductionCheckedWait === true &&
    stop.ordinaryStopLogClassified === true && stop.boundedStalledProductionDestructionProven === false &&
    stop.realDeviceDriverServiceWinmmOrHost === false, 'ACTUAL_CURRENT_STOP_OWNERSHIP');
  need(shaPattern.test(stop.compiler?.sha256) && shaPattern.test(stop.compiler?.vcvarsSha256), 'stop:COMPILER_HASH_MISSING');
  tool(stop.compiler?.path, tools.native.compiler, stop.compiler?.sha256, 'stop');
  tool(stop.compiler?.vcvarsPath, tools.vcvars, stop.compiler?.vcvarsSha256, 'stop.vcvars');
  sdk(stop.compiler?.sdkVersion, 'stop');
  need(stop.profiles?.length === 4 && stop.kernelProfiles?.length === 4, 'STOP_BUILD_VARIANTS');
  for (const profile of ['Release','Checked']) {
    for (const route of ['legacy','checked']) {
      const memory = stop.profiles.filter(row => row.profile === profile && row.route === route);
      const kernel = stop.kernelProfiles.filter(row => row.profile === profile && row.route === route);
      need(memory.length === 1 && kernel.length === 1, 'STOP_VARIANT_MISSING');
      const m=memory[0], k=kernel[0];
      need(m.pass === true && m.cases?.length === 24 && m.cases.every(c => c.actualStopFunctionExecuted && c.ownedFixtureThreadJoined), 'STOP_MEMORY_OWNERSHIP_UNPROVEN');
      need(k.pass === true && k.realKernelWaitAndClose === true && k.productionStopFunctionExecuted === true && k.cases?.length === 2 &&
        k.ownedThreadJoined === true && k.ownedEventsRemaining === 0 && k.ownedHandlesRemaining === 0 && k.ownedThreadsRemaining === 0 &&
        k.processExitUsedAsCleanup === false && k.cases.every(c => c.ownedThreadJoined && c.ownedEventsRemaining === 0 &&
          c.ownedHandlesRemaining === 0 && c.ownedThreadsRemaining === 0 && c.workerCooperativeCompletion && c.blockedInvalidApiAttempts === 0), 'STOP_REAL_KERNEL_OWNERSHIP_UNPROVEN');
      const held=k.cases.filter(c => c.case === 'worker-held-until-controller-release');
      need(held.length === 1 && held[0].firstStopWait?.waitResult === 258 && held[0].workerStillIncompleteAtFirstReturn === true, 'STOP_REAL_TIMEOUT_NOT_OBSERVED');
      if(route==='legacy') need(m.isCurrentProductionRoute === false && k.isCurrentProductionRoute === false && m.legacyRejectedAsSafe === true &&
        m.negativeExitCode === 1 && m.negativeFailures > 0 && m.cases.filter(c=>c.lostUnverifiedOwner).length===16 && held[0].lostUnverifiedOwner === true, 'STOP_LEGACY_NEGATIVE_NOT_CAUGHT');
      else need(m.isCurrentProductionRoute === true && k.isCurrentProductionRoute === true && m.cases.every(c=>!c.lostUnverifiedOwner && c.liveHandleCloses===0) &&
        held[0].retainedAtFirstReturn === true && held[0].lostUnverifiedOwner === false && held[0].liveHandleCloses === 0 &&
        held[0].secondStopAfterControllerJoin === true && held[0].lateReclaimedAfterSignal === true, 'STOP_CURRENT_SOURCE_NOT_ACCEPTED');
    }
  }
  const startupUnit = values.get(CHILDREN[8]);
  const startupUnitSources = sourceRows(Object.entries(startupUnit.sources ?? {}).map(([path,sha256])=>({path,sha256})), repo, sourceHashes, 'startup-unit');
  need(startupUnit.pass === true && startupUnit.sourceInputsStable === true &&
    startupUnitSources.has('native/input_capture_startup_wait.h') && startupUnit.profiles?.length === 2, 'STARTUP_UNIT_CURRENT_HEADER_REQUIRED');
  tool(startupUnit.compiler?.path, tools.native.compiler, startupUnit.compiler?.sha256, 'startup-unit');
  sdk(startupUnit.compiler?.windowsSdkVersion, 'startup-unit');
  for (const profile of ['Release','Checked']) {
    const rows = startupUnit.profiles.filter(row=>row.profile===profile);
    need(rows.length===1 && rows[0].pass===true, 'STARTUP_UNIT_PROFILE_REQUIRED');
    const unit = rows[0];
    need(unit.positive?.pass===true && unit.positive.failures===0 && unit.positive.allThreadsExplicitlyJoined===true &&
      unit.positive.threadsStarted===129 && unit.positive.threadsJoined===129 && unit.positive.cases?.length===8 &&
      unit.build?.controlImportsAbsent===true, 'STARTUP_UNIT_EXPLICIT_JOINS_REQUIRED');
    need(unit.negativeControls?.length===2 && ['delete-notify','delete-predicate'].every(name=>unit.negativeControls.some(n=>
      n.mutation===name && n.rejectedAsExpected===true && n.exitCode===1 && n.observations?.allThreadsExplicitlyJoined===true &&
      n.observations.threadsStarted===n.observations.threadsJoined)), 'STARTUP_UNIT_SPECIFIED_NEGATIVES_REQUIRED');
  }
  const startup = values.get(CHILDREN[9]);
  const startupSources = sourceRows(Object.entries(startup.sourceHashes ?? {}).map(([path,sha256])=>({path,sha256})), repo, sourceHashes, 'startup-lifecycle');
  need(startup.pass===true && startup.evidenceComplete===true && startup.sourceStable===true &&
    startup.sourceClosureVerified===true && startup.processExitCountsAsCleanup===false && startup.wholeLifecycleAcceptance===false &&
    startup.productionStartupWaitAccepted===true && startup.currentProductionCooperativeWait===true &&
    startup.realOwnedKernelThreadWaitCloseAndEventDelay===true && startup.realOwnedKernelThreadWaitCloseAndSleep===false &&
    startup.realKernelSleepCalled===false && startup.legacyDelayLeaf==='owned unnamed kernel event timed wait; no Sleep API' && startup.realDeviceHostServiceOrWinmm===false &&
    startup.arbitraryStalledProductionDestructionProven===false && startupSources.has('native/main.cpp') &&
    startupSources.has('native/input_capture_startup_wait.h'), 'ACTUAL_CURRENT_STARTUP_WAIT_REQUIRED');
  tool(startup.compiler?.path, tools.native.compiler, startup.compiler?.sha256, 'startup');
  tool(startup.compiler?.vcvarsPath, tools.vcvars, startup.compiler?.vcvarsSha256, 'startup.vcvars');
  sdk(startup.compiler?.sdkVersion, 'startup');
  need(startup.profiles?.length===4, 'STARTUP_FULL_SOURCE_VARIANTS_REQUIRED');
  for (const profile of ['Release','Checked']) for (const route of ['legacy','cooperative']) {
    const rows=startup.profiles.filter(row=>row.profile===profile && row.route===route);
    need(rows.length===1 && rows[0].pass===true && rows[0].cases?.length===3, 'STARTUP_FULL_SOURCE_CASES_REQUIRED');
    const row=rows[0], cooperative=route==='cooperative';
    need(row.isCurrentProductionRoute===cooperative, 'STARTUP_CANDIDATE_ONLY_NOT_ACCEPTED');
    need(row.cases.every(c=>c.actualStartWorkerStopExecuted===true && c.realOwnedKernelThread===true &&
      c.ownedThreadJoined===true && c.ownershipSettled===true && c.contextDestroyed===true && c.contextRetained===false &&
      c.controllerClosedBeforeJoin===false && c.processExitCountsAsCleanup===false &&
      c.productHandleClosed===true && c.controllerHandleClosed===true &&
      c.ownedHandlesRemaining===0 && c.unexpectedHardwareTimerCalls===0 && c.roleGateTimedOut===false &&
      c.productionRecoveryProven===false && c.arbitraryStalledWorkerDisposalProven===false), 'STARTUP_OWNERSHIP_OR_BOUNDARY_UNPROVEN');
    for (const scenario of [0,1,2]) {
      const cases=row.cases.filter(c=>c.scenario===scenario);need(cases.length===1, 'STARTUP_SCENARIO_REQUIRED');const c=cases[0];
      if(scenario<2) need(c.firstWaitResult===(cooperative?0:258) && c.retainedAtFirstReturn===!cooperative &&
        c.coInitCalls===0 && c.reason==='capture-cancelled-before-init', 'STARTUP_STOP_WINDOW_NOT_REPRODUCED_OR_FIXED');
      else need(c.firstWaitResult===0 && c.coInitCalls===1 && c.reason==='capture-com-init-failed' &&
        c.normalDelayElapsedMs>=2400, 'STARTUP_NORMAL_2500MS_DEFER_NOT_PRESERVED');
    }
  }
  need(startup.negativeProfiles?.length===8, 'STARTUP_CONTROLLER_FAILURE_CONTROLS_REQUIRED');
  for (const profile of ['Release','Checked']) for (const route of ['legacy','cooperative']) {
    const positive=startup.profiles.find(row=>row.profile===profile && row.route===route);
    const expected=route==='cooperative'?startup.actualFunctionHashes:startup.legacyFunctionHashes;
    need(Object.keys(expected??{}).length===3 && Object.entries(expected).every(([name,hash])=>
      shaPattern.test(hash) && positive.compiledFunctionHashes?.[name]===hash), 'STARTUP_ACTUAL_FUNCTION_BINDING_REQUIRED');
    for (const negative of ['controller-timeout','controller-wait-failed']) {
      const rows=startup.negativeProfiles.filter(row=>row.profile===profile && row.route===route && row.negative===negative);
      need(rows.length===1, 'STARTUP_CONTROLLER_FAILURE_CONTROL_MISSING');const row=rows[0];
      need(row.expectedFailureCaught===true && row.pass===false && row.exitCode===1 && row.failures===1 &&
        row.cases?.length===1 && row.retainedOwners===0, 'STARTUP_CONTROLLER_FAILURE_NOT_REJECTED');
      const c=row.cases[0];
      need(c.pass===false && c.failureInjection===negative && c.retainedAfterInitialJoinFailure===true &&
        c.recoveryWaitPerformed===true && c.recoveryJoinResult===0 && c.ownedThreadJoined===true &&
        c.ownershipSettled===true && c.contextDestroyed===true && c.contextRetained===false &&
        c.ownedHandlesRemaining===0 && c.productHandleClosed===true && c.controllerHandleClosed===true &&
        c.controllerClosedBeforeJoin===false && c.processExitCountsAsCleanup===false && c.unexpectedHardwareTimerCalls===0 &&
        c.controllerJoinResult===(negative==='controller-timeout'?258:4294967295), 'STARTUP_FAILURE_CLEANUP_UNPROVEN');
    }
  }
  const fence=values.get(CHILDREN[10]);
  const exitFence=validateExitFence(fence,{repo,tool,sdk,tools,current:currentFragments(repo),source:(rows,label)=>sourceRows(Object.entries(rows??{}).map(([path,sha256])=>({path,sha256})),repo,sourceHashes,label)});
  const historical = JSON.parse(fs.readFileSync(new URL('./historical-inputs.json', import.meta.url), 'utf8')).files;
  const source = (rows, label, {baseline=false,legacy=false}={}) => {
    rows=Array.isArray(rows)?rows:Object.entries(rows??{}).map(([path,sha256])=>({path,sha256}));
    const currentRows=[],bound=new Set();
    for(const row of rows){
      const key=row.path;
      const pinnedKey=baseline&&key==='native/main.cpp'?'baseline-dependencies/native/main.cpp':key;
      const allowed=baseline?(key==='native/main.cpp'||key==='identity/BASELINE-DEPENDENCIES.json'||key.startsWith('baseline-dependencies/')):
        legacy&&(key==='identity/LEGACY-EMERGENCY-DEPENDENCIES.json'||key.startsWith('legacy-emergency/'));
      if(allowed){
        const pin=historical[pinnedKey];
        need(pin&&!bound.has(key)&&row.sha256===pin.sha256&&hashFile(pin.path)===pin.sha256,'PARTIAL_HISTORICAL_SOURCE_PIN_MISMATCH:'+key);
        bound.add(key);
      }else currentRows.push(row);
    }
    for(const key of sourceRows(currentRows,repo,sourceHashes,label)){need(!bound.has(key),'PARTIAL_DUPLICATE_SOURCE');bound.add(key);}
    return bound;
  };
  const fragments = currentFragments(repo);
  const partialInit=validatePartialInit(values.get(CHILDREN[11]),{tools,tool,sdk,source,current:fragments,historical});
  const stopReceipt=validateAdmission(values.get(CHILDREN[12]),{source,current:fragments});
  const exitContinuation=validateExit(values.get(CHILDREN[13]),{source,current:fragments,tool,sdk,tools});
  const dsuOwner=validateDsu(values.get(CHILDREN[14]),{source,tool,sdk,tools});
  const sensorOwners=validateSensors(values.get(CHILDREN[15]),{source,tool,sdk,tools});
  const x2Owner=validateX2(values.get(CHILDREN[16]),{source,sdk});
  const gamebarOwner=validateGamebar(values.get(CHILDREN[17]),{source,tool,tools});
  const sensorEnsure=validateSensorEnsure(values.get(CHILDREN[18]),{repo,sourceHashes,source,tool,sdk,tools,current:fragments});
  const ownedCleanupComposition=validateOwnedCleanupComposition(values.get(CHILDREN[19]),{repo,sourceHashes,source,tool,sdk,tools,current:fragments});
  const primaryThread=validatePrimaryThread(values.get(CHILDREN[20]),{repo,sourceHashes,tools});
  const managedTerminal=validateManagedTerminal(values.get(CHILDREN[21]),{repo,sourceHashes,tools,version:managedCurrentVersion()});
  return {receiptHashes, cases: [
    {id: 'raw-acquisition', pass: true, variants: 3, checks: 153},
    {id: 'descriptor-decode', pass: true, runtimeVariants: 2, checks: 374},
    {id: 'semantic-witness', pass: true, positiveVariants: 2, rejectedMutants: 2},
    {id: 'bounded-timer-recorder', pass: true, positiveVariants: 2, rejectedMutants: 4, checks: timer.totalBaselineChecks},
    {id: 'actual-resume-generation', pass: true, checks: resume.checks, rejectedMutants: 1},
    {id: 'actual-timer-callback-export', pass: true, checks: adapter.checks.runtime, rejectedOrdinaryCounterBuilds: 4},
    {id: 'actual-worker-retirement', pass: true, currentBuilds: 2, currentSchedules: 12, legacyCounterexamples: 12, guardRemovalFailures: 12, checks: retirement.checks},
    {id: 'actual-stop-wait-ownership', pass: true, currentMemorySchedules: 48, realKernelSchedules: 8, memoryChecks: stop.checks, kernelChecks: stop.kernelChecks, boundedProductionDestructionProven: false},
    {id: 'startup-wait-synchronization', pass: true, profiles: 2, explicitlyJoinedThreads: 258, specifiedNegativeBuilds: 4},
    {id: 'actual-startup-stop-window', pass: true, profiles: 2, fullSourceSchedules: 12, legacyTimeouts: 4, fixtureFailureControls: 8, boundedProductionDestructionProven: false},
    exitFence,
    partialInit,
    stopReceipt,
    exitContinuation,
    dsuOwner,
    sensorOwners,
    x2Owner,
    gamebarOwner,
    sensorEnsure,
    ownedCleanupComposition,
    primaryThread,
    managedTerminal,
  ]};
}