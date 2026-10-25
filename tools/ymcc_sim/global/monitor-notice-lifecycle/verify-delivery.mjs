import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { pathToFileURL } from 'node:url';
import { getMonitorNoticeLifecycleAdapter, PRODUCT_SOURCES, TOOL_AUTHORITY_SOURCES } from './adapter.mjs';
const args = process.argv.slice(2), option = key => { const at = args.indexOf(key); return at < 0 ? undefined : args[at + 1]; };
const evidence = path.resolve(option('--evidence-dir') ?? '');
if (!option('--evidence-dir')) throw Error('EVIDENCE_REQUIRED');
const gateFile = path.resolve(option('--lifecycle-gate-module') ?? path.join(import.meta.dirname, '../capability-lifecycle/index.mjs'));
const { lifecycleContractFingerprint, normalizeLifecycleContract, EXIT_REQUIREMENTS, PERSISTENT_REQUIREMENTS, applyLifecycleVerdict } = await import(pathToFileURL(gateFile));
const read = file => JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const result = read(path.join(evidence, 'result.json')), checks = [];
function need(name, condition, observed = null) { checks.push({ name, passed: Boolean(condition), observed }); }
const adapter = getMonitorNoticeLifecycleAdapter(), contract = normalizeLifecycleContract(adapter.lifecycleContract);
need('persistent contract normalized by the actual shared gate', contract.features[0].persistence === 'persistent');
need('actual gate fingerprint bound in runtime evidence', result.lifecycleEvidence.contractFingerprint === lifecycleContractFingerprint(contract));
need('shared gate source declared as an evidence input', adapter.evidenceInputs.includes('tools/ymcc_sim/global/capability-lifecycle/index.mjs'));
need('all 21 named cases ran without infrastructure failures', result.cases.length === 21 && result.counts.infrastructureFailures === 0 && result.cases.every(row => row.checks.length > 0 && typeof row.passed === 'boolean'));
need('all ten observed failures remain FAIL', result.status === 'FAIL' && result.ok === false && result.cases.filter(row => !row.passed).length === 10);
const features = result.lifecycleEvidence.features, observations = features[0].observations;
for (const requirement of [...EXIT_REQUIREMENTS, ...PERSISTENT_REQUIREMENTS]) {
  const matching = observations.filter(row => row.requirement === requirement), item = matching[0];
  const linked = item && result.cases.filter(row => row.id === item.caseId);
  need('one real case links requirement ' + requirement, matching.length === 1 && linked?.length === 1 && (!item.passed || linked[0].passed), item?.caseId);
}
for (const relative of PRODUCT_SOURCES) {
  const captured = fs.readFileSync(path.join(evidence, 'source-capture', relative));
  need('actual captured byte identity ' + relative, hash(captured) === result.sourceHashes[relative] && hash(fs.readFileSync(path.join(result.provenance.root, relative))) === result.sourceHashes[relative]);
}
need('source and reference identities unchanged through real execution', result.sourceDrift.length === 0);
need('native boundary remains explicitly unverified', result.boundaries.nativeCarrierExecuted === false && result.boundaries.nativeForceDestructionVerified === false);
const tools = result.provenance.toolBinding, authority = tools?.before;
need('complete precompile and postrun authority receipts match', tools?.status === 'VERIFIED' && JSON.stringify(authority) === JSON.stringify(tools.after) && Array.isArray(authority?.tools) && authority.tools.length >= authority.fileCount && authority.packageCount > 0 && authority.tools.length <= 3072);
const toolPins = new Map((authority?.tools ?? []).map(row => [row.path.toLowerCase(), row.sha256]));
need('actual service exe and Node executable belong to the complete bound closure', toolPins.has(authority?.compilerBinary?.toLowerCase()) && toolPins.has(authority?.nodeExecutable?.toLowerCase()) && authority.compilerBinary.endsWith('esbuild.exe') && authority.nodeExecutable.toLowerCase() === fs.realpathSync(process.execPath).toLowerCase());
const launches = tools?.compilerServiceLaunches ?? [];
need('actual observed compiler service launch is bound', launches.length === 1 && Number.isSafeInteger(launches[0].pid) && launches[0].pid > 0 && launches[0].path.toLowerCase() === authority.compilerBinary.toLowerCase() && launches[0].sha256 === toolPins.get(launches[0].path.toLowerCase()) && JSON.stringify(launches[0].args) === JSON.stringify(['--service=' + authority.compilerVersion, '--ping']));
need('observed loaded CJS dependencies are pinned within the full package scope', tools?.observedToolDependencies?.length > 0 && tools.observedToolDependencies.every(row => toolPins.get(row.path.toLowerCase()) === row.sha256) && tools.observedToolDependencies.some(row => row.path.toLowerCase() === authority.compilerEntry.toLowerCase()));
need('actual authority helper closure is declared and unchanged', TOOL_AUTHORITY_SOURCES.every(relative => adapter.evidenceInputs.includes(relative)) && JSON.stringify(tools?.helperSourcesBefore) === JSON.stringify(tools?.helperSourcesAfter) && tools.helperSourcesBefore.length === TOOL_AUTHORITY_SOURCES.length && tools.helperSourcesAfter.every(row => hash(fs.readFileSync(path.join(result.provenance.root, row.path))) === row.sha256));
const phaseNames = tools?.phases.map(row => row.phase) ?? [];
need('tool lock precedes compile and full verification follows cases', JSON.stringify(phaseNames) === JSON.stringify(['precompile-start', 'precompile-locked', 'compile-start', 'compile-complete', 'postrun-start', 'postrun-verified']) && tools.refusal === null);
const beforeFile = read(path.join(evidence, 'tool-binding.before.json')), afterFile = read(path.join(evidence, 'tool-binding.after.json'));
need('separate original tool receipts bind both complete phases', beforeFile.status === 'PRECOMPILE_LOCKED' && afterFile.status === 'VERIFIED' && JSON.stringify(beforeFile.before) === JSON.stringify(authority) && JSON.stringify(afterFile.after) === JSON.stringify(tools.after));

const normal = result.cases.find(row => row.id === 'normal-stop-zero-resources');
need('normal stop has measured zero categories', normal.passed && Object.values(normal.measurements.remainingOwnedResources).every(count => count === 0));
const stall = result.cases.find(row => row.id === 'stop-close-hold');
need('stalled close keeps actual window and native operation while JS IPC times out', !stall.passed && stall.measurements.remainingOwnedResources.windows === 1 && stall.measurements.remainingOwnedResources.nativeOperations === 1 && stall.measurements.remainingOwnedResources.pendingIpc === 0 && !stall.measurements.fallbackExercised);
const timeoutTrace = read(path.join(evidence, 'traces/late-create-after-real-ipc-timeout.json'));
need('actual IPC timeout and late create both observed in execution trace', timeoutTrace.some(row => row.type === 'ipc.complete' && row.cmd === 'monitorNotice.create' && row.ok === false && row.error === 'IPC command timed out: monitorNotice.create') && timeoutTrace.some(row => row.type === 'native.window.created' && row.at >= Date.UTC(2026, 9, 22) + 11000));
for (const id of ['sleep-owner-loss-no-self-heal', 'transient-carrier-loss-no-self-heal']) {
  const row = result.cases.find(item => item.id === id);
  need('independent health observers actually ran for ' + id, !row.passed && row.independentHealth.length >= 5 && row.measurements.healthObserved && row.measurements.recovered === false);
}
const stopped = result.cases.find(row => row.id === 'late-events-after-stop');
need('late product callbacks delivered with zero resurrection', stopped.passed && stopped.measurements.lateCallbacksDelivered > 0 && stopped.measurements.restartsAfterExit === 0);
const off = observations.find(row => row.requirement === 'explicitOffStaysOff');
need('OFF prohibits resurrection while measured host control listeners remain', off.passed === true && off.measurements.remainingOwnedResources.subscriptions === 8 && off.measurements.remainingOwnedResources.stateEntries === 1 && off.measurements.hostControlListening.subscriptions === 8 && off.measurements.hostControlListening.duplicateHistoryEntries === 1 && off.measurements.hostControlListening.destructionRequiredForExplicitOff === false && off.measurements.desiredEnabled === false && off.measurements.lateCallbacksDelivered > 0 && off.measurements.restartsAfterExit === 0 && off.measurements.updateAccepted === false);
const verdict = applyLifecycleVerdict({ status: 'FAIL', exitCode: 2 }, { status: 'INCOMPLETE', features: features });
need('actual gate preserves the nonzero producer verdict', verdict.exitCode === 2 && verdict.status === 'FAIL');
if (option('--process-receipt')) {
  const processResult = read(option('--process-receipt'));
  need('raw child naturally exited 2 without timeout or signal', processResult.exitCode === 2 && processResult.naturallyExited && !processResult.signal && !processResult.timedOut);
  need('executed candidate entry identity remains current', processResult.candidateEntryBeforeSha256 === processResult.candidateEntryAfterSha256 && processResult.candidateEntryAfterSha256 === hash(fs.readFileSync(path.join(import.meta.dirname, 'index.mjs'))));
}
const report = { schemaVersion: 1, status: checks.every(row => row.passed) ? 'DELIVERY_VERIFIED_PRODUCT_FAIL_PRESERVED' : 'DELIVERY_VERIFICATION_FAILED', fixtureDeliveryVerified: checks.every(row => row.passed), productAcceptance: false, checks, sourceResult: { path: path.join(evidence, 'result.json'), sha256: hash(fs.readFileSync(path.join(evidence, 'result.json'))) }, actualGate: { path: gateFile, sha256: hash(fs.readFileSync(gateFile)) } };
fs.writeFileSync(path.join(evidence, 'delivery-verification.json'), JSON.stringify(report, null, 2) + '\n', { flag: 'wx' });
console.log(JSON.stringify({ status: report.status, checks: checks.length, failedChecks: checks.filter(row => !row.passed) }));
process.exitCode = report.fixtureDeliveryVerified ? 0 : 2;
