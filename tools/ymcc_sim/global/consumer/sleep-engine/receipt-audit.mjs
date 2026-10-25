/** Audit an actually executed receipt; never launches or attaches to a Host. */
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
const ownRoot = path.dirname(fileURLToPath(import.meta.url));
const sha = /^[a-f0-9]{64}$/i;
const toolFiles = new Set(['Program.cs', 'Scenarios.cs', 'MemoryScenarios.cs', 'SleepEngineProbe.csproj', 'Invoke-SleepEngine.ps1', 'receipt-contract.ps1']);
const normalize = value => String(value ?? '').replaceAll('\\', '/').replace(/\/+$/, '').toLowerCase();
const moduleName = value => normalize(value).split('/').at(-1);
export function auditSleepEngineReceipt(receipt, { expectedHostSha256, expectedBackendSha256, expectedMode = 'all', verifySourceHashes = true } = {}) {
  const findings = [];
  const require = (condition, code, stage, eventId = null) => { if (!condition) findings.push({ code, stage, eventId }); };
  require(sha.test(expectedHostSha256 ?? ''), 'EXPECTED_HOST_IDENTITY_REQUIRED', 'input');
  require(receipt?.kind === 'sleep-engine-consumption', 'SLEEP_ENGINE_RECEIPT_REQUIRED', 'input');
  if (!receipt || typeof receipt !== 'object') return { status: 'FAIL', findings };
  require(receipt.mode === expectedMode, 'EXECUTED_MODE_DIFFERS', 'input');
  require(receipt.status === 'PASS', 'RECEIPT_NOT_PASS', 'feedback');
  const loaded = Array.isArray(receipt.actualHost?.loaded) ? receipt.actualHost.loaded : [];
  const host = loaded.find(row => moduleName(row?.path) === 'yemanfanhost.dll');
  require(host && sha.test(host.sha256 ?? '') && host.sha256.toLowerCase() === expectedHostSha256?.toLowerCase(), 'ACTUAL_LOADED_HOST_IDENTITY_DIFFERS', 'consumer');
  require(host && normalize(host.path) === normalize(receipt.actualHost?.payloadRoot) + '/yemanfanhost.dll', 'LOADED_HOST_SOURCE_PATH_DIFFERS', 'consumer');
  require(receipt.wrapperVerification?.hostUnchanged === true && receipt.wrapperVerification?.actualHostSha256?.toLowerCase() === expectedHostSha256?.toLowerCase(), 'HOST_CHANGED_OR_UNVERIFIED', 'consumer');
  for (const row of loaded) require(['yemanfanhost.dll', 'yemanfanbackend.dll', 'microsoft.windows.sdk.net.dll', 'winrt.runtime.dll'].includes(moduleName(row?.path)) && sha.test(row.sha256 ?? ''), 'UNREVIEWED_LOADED_TARGET', 'consumer');
  if (expectedBackendSha256) {
    require(sha.test(expectedBackendSha256), 'EXPECTED_BACKEND_IDENTITY_INVALID', 'input');
    require(receipt.wrapperVerification?.actualBackendSha256?.toLowerCase() === expectedBackendSha256.toLowerCase(), 'PINNED_BACKEND_IDENTITY_DIFFERS', 'input');
    const backend = loaded.find(row => moduleName(row?.path) === 'yemanfanbackend.dll');
    if (backend) require(backend.sha256?.toLowerCase() === expectedBackendSha256.toLowerCase(), 'LOADED_BACKEND_IDENTITY_DIFFERS', 'consumer');
  }
  const safety = receipt.safety ?? {};
  require(safety.productionMainInvoked === false && receipt.process?.mainCalled === false, 'PRODUCTION_MAIN_WAS_NOT_EXCLUDED', 'consumer');
  require(safety.nativeSchedulerExercised === false && safety.physicalHardwareValidated === false, 'SOFTWARE_SCOPE_PROMOTED_TO_NATIVE_OR_PHYSICAL', 'feedback');
  require(safety.hcLoaded === false && Array.isArray(safety.nativeAttempts) && safety.nativeAttempts.length === 0, 'HC_OR_TARGET_NATIVE_ATTEMPT', 'consumer');
  require(safety.operatingSystemSleepPerformed === false && safety.hardwareWritesPerformed === false, 'REAL_SLEEP_OR_WRITE_BOUNDARY', 'state');
  require(safety.machineGateAcquired === false && safety.existingHostStopped === false, 'EXISTING_HOST_BOUNDARY', 'consumer');
  require(receipt.process?.naturalExitObservedByParent === true && receipt.process?.exitCode === 0 && receipt.process?.forcedCleanup === false && receipt.wrapperVerification?.childNaturalExit === true, 'NATURAL_EXIT_NOT_CONFIRMED', 'feedback');
  const memory = expectedMode === 'memory';
  require(memory ? receipt.process?.memoryEngineCompleted === true : receipt.process?.serverCompleted === true, 'CONSUMER_DID_NOT_COMPLETE', 'consumer');
  require(memory ? receipt.http?.actualDispatch === false && safety.networkUsed === false && safety.localServerConstructed === false && receipt.http?.baseUrl === ''
    : receipt.http?.actualDispatch === true && safety.networkUsed === true && safety.localServerConstructed === true && /^http:\/\/127\.0\.0\.1:\d+$/.test(receipt.http?.baseUrl ?? ''), 'ACTUAL_TRANSPORT_SCOPE_DIFFERS', 'consumer');
  require(receipt.http?.tokenSource === 'fresh-random-in-memory' && receipt.http?.tokenPersisted === false, 'FRESH_SYNTHETIC_CREDENTIAL_REQUIRED', 'input');
  const evidence = Array.isArray(receipt.inputConsumerStateFeedback) ? receipt.inputConsumerStateFeedback : [];
  require(evidence.length > 0, 'EXECUTED_EVIDENCE_MISSING', 'consumer');
  const ids = new Set();
  for (const row of evidence) {
    if (!row || typeof row !== 'object') { require(false, 'FOUR_STAGE_EVIDENCE_MISSING', 'consumer'); continue; }
    const id = row?.input?.eventId ?? null;
    require(row?.input && row?.consumer && row?.state && row?.feedback, 'FOUR_STAGE_EVIDENCE_MISSING', 'consumer', id);
    require(typeof id === 'string' && !ids.has(id) && row.input.provenance === 'source-derived-fixture', 'INPUT_IDENTITY_OR_PROVENANCE_INVALID', 'input', id); ids.add(id);
    require(row.state?.before && row.state?.after, 'AUTHORITATIVE_STATE_PAIR_MISSING', 'state', id);
    for (const state of [row.state?.before, row.state?.after]) for (const key of ['hardwareWrites', 'hardwareWritesEnabled', 'hardwareWritesObserved'])
      require(state?.[key] === false, 'PHYSICAL_WRITE_FLAG_NOT_FALSE_' + key, 'state', id);
    if (!memory) {
      require(row.consumer?.type === 'YeManFanHost.LocalFanHostServer' && row.consumer?.entry === 'HandleAsync' && row.consumer?.engine === 'YeManFanHost.FanHostEngine' && row.consumer?.transport === 'actual-loopback-http', 'REAL_HTTP_CONSUMER_MISSING', 'consumer', id);
      require(Number.isInteger(row.feedback?.httpStatus) && row.feedback.httpStatus >= 200 && row.feedback.httpStatus <= 599 && row.feedback.requestIdPresent === true && row.feedback.hardwareReceipt === false, 'HTTP_FEEDBACK_MISSING', 'feedback', id);
    }
  }
  if (expectedMode === 'all') {
    const groups = Array.isArray(receipt.groups) ? receipt.groups : [];
    require(groups.length === 3 && ['forms', 'typeseries', 'rapidwake'].every(mode => groups.filter(row => row?.mode === mode).length === 1), 'ORIGINAL_MODE_ISOLATION_MISSING', 'consumer');
    require(new Set(groups.map(row => row?.process?.pid)).size === 3 && groups.every(row => row?.process?.pid > 0), 'FRESH_ENGINE_PROCESSES_NOT_DISTINCT', 'consumer');
    require(new Set(groups.map(row => row?.http?.baseUrl)).size === 3, 'INDEPENDENT_LOOPBACK_PORTS_NOT_DISTINCT', 'consumer');
    for (const group of groups) {
      require(group.status === 'PASS' && group.process?.naturalExitObservedByParent === true && group.process?.exitCode === 0 && group.process?.serverCompleted === true && group.process?.mainCalled === false, 'GROUP_EXECUTION_OR_EXIT_FAILED_' + group.mode, 'feedback');
      require(group.safety?.hcLoaded === false && Array.isArray(group.safety?.nativeAttempts) && !group.safety.nativeAttempts.length, 'GROUP_HC_OR_NATIVE_ATTEMPT', 'consumer');
      const target = group.actualHost?.loaded?.find(row => moduleName(row.path) === 'yemanfanhost.dll');
      require(target?.sha256 === expectedHostSha256, 'GROUP_LOADED_HOST_IDENTITY_DIFFERS', 'consumer');
      if (expectedBackendSha256) require(group.actualHost?.expectedBackendSha256 === expectedBackendSha256 && group.checks?.some(row => row.name === 'explicit Backend hash matches' && row.passed === true), 'GROUP_BACKEND_PAIR_NOT_VERIFIED', 'consumer');
    }
  }
  const scenarios = Array.isArray(receipt.scenarios) ? receipt.scenarios : [];
  const expectedCounts = expectedMode === 'all' ? { forms: 20, typeseries: 3, rapidwake: 6, negative: 12 }
    : expectedMode === 'memory' ? { memory: 4 } : expectedMode === 'forms' ? { forms: 20, negative: 4 }
      : expectedMode === 'typeseries' ? { typeseries: 3, negative: 4 } : expectedMode === 'rapidwake' ? { rapidwake: 6, negative: 4 } : { negative: 4 };
  for (const [group, count] of Object.entries(expectedCounts)) require(scenarios.filter(row => row.mode === group).length === count, 'SCENARIO_MAPPING_INCOMPLETE_' + group, 'consumer');
  for (const row of scenarios) {
    if (!row || typeof row !== 'object') { require(false, 'SCENARIO_EVIDENCE_INVALID', 'consumer'); continue; }
    require(row.status === 'PASS' && Array.isArray(row.requestEventIds) && row.requestEventIds.length > 0 && row.requestEventIds.every(id => ids.has(id)), 'SCENARIO_NOT_EXECUTED_' + row.id, 'consumer');
    require(sha.test(row.fixtureSha256 ?? ''), 'FIXTURE_IDENTITY_MISSING', 'input');
  }
  require(Array.isArray(receipt.checks) && receipt.checks.length > 0 && receipt.checks.every(row => row.passed === true), 'EXECUTED_CHECK_FAILED_OR_MISSING', 'state');
  const sources = Array.isArray(receipt.sourceInputs) ? receipt.sourceInputs : [];
  require(verifySourceHashes ? sources.length === toolFiles.size && new Set(sources.map(row => moduleName(row?.path))).size === toolFiles.size : sources.length > 0, 'TOOL_SOURCE_IDENTITIES_MISSING', 'consumer');
  if (verifySourceHashes) {
    require(receipt.sourceStability?.unchanged === true && receipt.sourceStability?.beforeBuildChecked === true, 'SOURCE_BUILD_RUN_STABILITY_UNCONFIRMED', 'consumer');
    const after = Array.isArray(receipt.sourceInputsAfter) ? receipt.sourceInputsAfter : [];
    require(after.length === sources.length && sources.every(before => after.some(item => normalize(item.path) === normalize(before.path) && item.sha256 === before.sha256)), 'SOURCE_CHANGED_DURING_BUILD_OR_RUN', 'consumer');
  }
  for (const row of sources) {
    if (!row || typeof row !== 'object') { require(false, 'TOOL_SOURCE_IDENTITY_INVALID', 'consumer'); continue; }
    const filename = moduleName(row?.path);
    const recognized = [...toolFiles].find(item => item.toLowerCase() === filename);
    require(Boolean(recognized) && sha.test(row.sha256 ?? ''), 'TOOL_SOURCE_IDENTITY_INVALID', 'consumer');
    if (verifySourceHashes && recognized) {
      const actual = path.join(ownRoot, recognized);
      require(normalize(row.path) === normalize(actual), 'TOOL_SOURCE_PATH_DIFFERS', 'consumer');
      const current = crypto.createHash('sha256').update(fs.readFileSync(actual)).digest('hex');
      require(current === row.sha256, 'TOOL_SOURCE_CHANGED_AFTER_RUN', 'consumer');
    }
  }
  return { schemaVersion: 1, kind: 'sleep-engine-receipt-audit', status: findings.length ? 'FAIL' : 'PASS',
    scope: memory ? 'actual-engine-memory-only' : 'actual-selected-host-engine-and-loopback-dispatch-only', findings,
    summary: { events: evidence.length, scenarios: scenarios.length, checks: receipt.checks?.length ?? 0, loadedTargetAssemblies: loaded.length,
      backendActuallyLoaded: loaded.some(row => moduleName(row?.path) === 'yemanfanbackend.dll'), sourceHashesVerified: verifySourceHashes, currentWrapperAcceptance: verifySourceHashes },
    boundary: { nativeSchedulerVerified: false, physicalSleepVerified: false, hardwareAcceptance: false } };
}


