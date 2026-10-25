import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
import childProcess from 'node:child_process';
import { performance } from 'node:perf_hooks';
import { pathToFileURL } from 'node:url';
import { createMemoryHost } from './memory-boundary.mjs';
import { PRODUCT_SOURCES, LEGACY_INPUTS, TOOL_AUTHORITY_SOURCES, getMonitorNoticeLifecycleAdapter } from './adapter.mjs';

const argv = process.argv.slice(2);
const option = key => { const at = argv.indexOf(key); return at < 0 ? undefined : argv[at + 1]; };
const gateFile = path.resolve(option('--lifecycle-gate-module') ?? path.join(import.meta.dirname, '../capability-lifecycle/index.mjs'));
const { lifecycleContractFingerprint, EXIT_REQUIREMENTS, PERSISTENT_REQUIREMENTS } = await import(pathToFileURL(gateFile));
const root = path.resolve(option('--repo-root') ?? path.resolve(import.meta.dirname, '../../../..'));
if (!option('--evidence-dir')) throw Error('EXPLICIT_FRESH_EVIDENCE_DIR_REQUIRED');
const out = path.resolve(option('--evidence-dir'));
const inside = (parent, child) => { const relative = path.relative(parent, child); return relative === '' || (!relative.startsWith('..') && !path.isAbsolute(relative)); };
if (inside(root, out)) throw Error('EVIDENCE_MUST_BE_OUTSIDE_READ_ONLY_PRODUCT_REPO');
if (fs.existsSync(out)) throw Error('FRESH_EVIDENCE_DIR_REQUIRED');
fs.mkdirSync(out, { recursive: true });
const write = (relative, value) => { const file = path.join(out, relative); if (!inside(out, file)) throw Error('OUTPUT_PATH_ESCAPE'); fs.mkdirSync(path.dirname(file), { recursive: true }); fs.writeFileSync(file, typeof value === 'string' ? value : JSON.stringify(value, null, 2) + '\n', { flag: 'wx' }); };
const { sha256: hash } = await import(pathToFileURL(path.join(root, 'tools/ymcc_sim/global/monitor-signals/common.mjs')));
const legacyMonitorInput = await import(pathToFileURL(path.join(root, 'tools/ymcc_sim/global/monitor-input/adapter.mjs')));
const legacyMonitorSignals = await import(pathToFileURL(path.join(root, 'tools/ymcc_sim/global/monitor-signals/adapter.mjs')));
const reusedDescriptors = [legacyMonitorInput.getMonitorInputAdapter(), legacyMonitorInput.getMonitorFeedbackAdapter(), ...legacyMonitorSignals.getMonitorAdapters()].map(({id, timeoutMs, path: entry}) => ({ id, timeoutMs, path: entry }));
const gateBeforeSha256 = hash(fs.readFileSync(gateFile));
const started = performance.now(), cases = [], sourceHashes = {}, captured = new Map(), referencesBefore = {};
const referencePaths = [...LEGACY_INPUTS, 'src/bridge/monitorHudHost.ts', 'src/monitorNoticeOutputEntry.ts', 'native/monitor_desktop_hud.h', 'tools/ymcc_sim/HC-UPGRADE-REGRESSION.md', 'AGENTS.md'];
for (const relative of PRODUCT_SOURCES) { const bytes = fs.readFileSync(path.join(root, relative)); captured.set(relative, bytes); sourceHashes[relative] = hash(bytes); write('source-capture/' + relative, bytes.toString('utf8')); }
for (const relative of referencePaths) referencesBefore[relative] = hash(fs.readFileSync(path.join(root, relative)));
write('source-hashes.before.json', { sourceHashes, referencesBefore, productSourcesCapturedForThisUnitOnly: true, fullRepositorySnapshot: false });
const req = createRequire(path.join(root, 'package.json'));
let toolAuthority, toolImplementationBefore, toolApi;
const toolBinding = { schemaVersion: 1, status: 'NOT_LOCKED', phases: [], before: null, after: null, helperSourcesBefore: [], helperSourcesAfter: [], observedToolDependencies: [], compilerServiceLaunches: [], refusal: null,
  meaning: 'Complete predeclared package/tool scope with observed compiler service and loaded CJS inputs; not an OS loaded-module graph or installed application acceptance.' };
function toolPhase(phase) { toolBinding.phases.push({ phase, elapsedMs: performance.now() - started }); }
function toolRefusal(stage, error) {
  toolBinding.status = 'REFUSED_' + stage.toUpperCase();
  toolBinding.refusal = { stage, code: error.code ?? error.message, message: String(error.message), stack: String(error.stack ?? error), details: error.details ?? null, businessCasesExecuted: cases.length };
  write('tool-binding.refusal.' + stage + '.json', toolBinding);
}
try {
  toolPhase('precompile-start');
  toolApi = await import(pathToFileURL(path.join(root, 'tools/ymcc_sim/global/bundled-node-tool-authority.mjs')));
  if (JSON.stringify([...toolApi.BUNDLED_AUTHORITY_SOURCES].sort()) !== JSON.stringify([...TOOL_AUTHORITY_SOURCES].sort())) throw Error('DECLARED_TOOL_HELPER_CLOSURE_DIFFERS');
  toolImplementationBefore = toolApi.verifyBundledImplementation(root);
  toolAuthority = toolApi.lockBundledNodeTools(root);
  toolBinding.before = toolAuthority; toolBinding.helperSourcesBefore = toolImplementationBefore;
  toolBinding.status = 'PRECOMPILE_LOCKED'; toolPhase('precompile-locked');
  write('tool-binding.before.json', toolBinding);
} catch (error) {
  toolRefusal('precompile', error);
  write('result.json', { schemaVersion: 1, id: 'lifecycle.monitor-notice-host', status: 'BLOCKED_TOOL_BINDING', ok: false, cases: [], sourceHashes,
    counts: { cases: 0, passed: 0, failed: 0, checks: 0, infrastructureFailures: 1 }, businessNotExecuted: true,
    provenance: { root, toolBinding }, boundaries: { productSourceRewritten: false, nativeCarrierExecuted: false, nativeForceDestructionVerified: false, fullGlobalRegisteredRunExecuted: false },
    preservedFailurePolicy: 'Original r4 business FAIL remains preserved. Tool authority refusal is not a detected product failure or a passing run.' });
  console.error(JSON.stringify({ status: 'BLOCKED_TOOL_BINDING', stage: 'precompile', error: toolBinding.refusal.code, out }));
  process.exit(10);
}
const cacheBeforeCompiler = new Set(Object.keys(req.cache));
const esbuildEntry = fs.realpathSync(req.resolve('esbuild'));
if (esbuildEntry.toLowerCase() !== toolAuthority.compilerEntry.toLowerCase()) throw Error('RESOLVED_COMPILER_DIFFERS_FROM_LOCK');
const esbuild = req(toolAuthority.compilerEntry);
if (esbuild.version !== toolAuthority.compilerVersion) throw Error('COMPILER_VERSION_DIFFERS_FROM_LOCK');
const originalSpawn = childProcess.spawn;
childProcess.spawn = function(file, args, options) {
  const actual = fs.realpathSync(file), digest = hash(fs.readFileSync(actual));
  const pin = toolAuthority.tools.find(item => item.path.toLowerCase() === actual.toLowerCase());
  if (actual.toLowerCase() !== toolAuthority.compilerBinary.toLowerCase() || !pin || pin.sha256 !== digest) throw Error('UNBOUND_COMPILER_SERVICE');
  if (JSON.stringify(args) !== JSON.stringify(['--service=' + toolAuthority.compilerVersion, '--ping'])) throw Error('UNREVIEWED_COMPILER_SERVICE_ARGUMENTS');
  const launched = originalSpawn.call(childProcess, file, args, options);
  toolBinding.compilerServiceLaunches.push({ path: actual, sha256: digest, args: [...args], pid: launched.pid, elapsedMs: performance.now() - started, windowsHide: options?.windowsHide === true });
  return launched;
};
let build;
try {
  toolPhase('compile-start');
  build = await esbuild.build(
{ stdin: { contents: `export {startMonitorNoticeHost} from './src/bridge/monitorNoticeHost'; export {SHORTCUT_FEEDBACK_EVENT} from './src/bridge/shortcutActionFeedback'; export {presentUpdateReady,registerUpdateReadyPresenter} from './src/bridge/updateReadyNotice'; export {setLogSink} from './src/bridge/ipc';`, resolveDir: root, sourcefile: 'lifecycle-harness-entry.ts', loader: 'ts' },
  bundle: true, write: false, platform: 'browser', format: 'iife', globalName: 'NoticeLifecycleActual', target: 'es2022', metafile: true,
  plugins: [{ name: 'pinned-original-product-bytes', setup(builder) { builder.onLoad({ filter: /[\\/]src[\\/].*\.ts$/ }, args => {
    const relative = path.relative(root, args.path).replaceAll('\\', '/'), bytes = captured.get(relative);
    if (!bytes) throw Error('UNREVIEWED_PRODUCT_IMPORT:' + relative);
    return { contents: bytes.toString('utf8'), loader: 'ts', resolveDir: path.dirname(args.path) };
  }); } }],
});
  toolPhase('compile-complete');
} catch (error) {
  toolRefusal('compile', error);
  throw error;
} finally { childProcess.spawn = originalSpawn; }
if (toolBinding.compilerServiceLaunches.length !== 1 || !Number.isSafeInteger(toolBinding.compilerServiceLaunches[0].pid)) throw Error('ACTUAL_COMPILER_SERVICE_NOT_OBSERVED');
write('actual-monitor-notice.bundle.js', build.outputFiles[0].text);
write('build-metafile.json', build.metafile);
const bundle = build.outputFiles[0].text;
const zero = value => Object.values(value).every(count => count === 0);
const check = (condition, name, actual) => ({ name, passed: Boolean(condition), actual: actual ?? null });
async function run(id, name, requirement, exercise) {
  const begin = performance.now(); let env;
  const row = { id, name, requirement, passed: false, checks: [], measurements: {}, infrastructureFailure: false };
  try {
    const response = await exercise(policy => { if (env) throw Error('ONE_ENVIRONMENT_PER_CASE'); env = createMemoryHost(bundle, policy); return env; });
    row.checks = response.checks; row.measurements = response.measurements;
    row.passed = row.checks.length > 0 && row.checks.every(item => item.passed);
  } catch (error) { row.infrastructureFailure = true; row.error = String(error.stack ?? error); }
  // This is fixture teardown after the verdict; it cannot change the observed result.
  if (env) {
    row.observedBeforeHarnessTeardown = env.snapshot();
    row.independentHealth = env.trace.filter(t => t.type === 'independent.health');
    row.traceFile = 'traces/' + id + '.json';
    env.stop(); await env.clock.advance(12000);
    row.harnessTeardown = { verdictAlreadyCaptured: true, afterStopAndTimeouts: env.snapshot(), nativeForceDestroyExecuted: false };
    write(row.traceFile, env.trace);
  }
  row.durationMs = performance.now() - begin; cases.push(row);
}
async function stoppedMeasurements(env, stall = false) {
  await env.clock.advance(12000);
  const after = env.snapshot(); let presenterSlotReleased = false;
  try { const release = env.api.registerUpdateReadyPresenter(async () => false); presenterSlotReleased = true; release(); } catch { /* actual occupied slot */ }
  return { remainingOwnedResources: { ...after.remainingOwnedResources, presenterRegistrations: Number(!presenterSlotReleased) },
    bounded: env.clock.timers.size === 0 && env.operations.size === 0 && !after.host.opening,
    ownerRevoked: after.host.owner === null, stateDisposed: after.host.state.ended, presenterSlotReleased,
    fallbackExercised: after.host.cleanup?.fallbackRetirements > 0 && after.host.cleanup?.lastRetirement?.resourcesDestroyed === true, fallbackOwnedOnly: after.host.cleanup?.lastRetirement?.ownedOnly === true, nativeForceDestroyExecuted: false,
    observationWindowMs: 12000, stallInjected: stall, sourceInspection: after.host };
}

await run('normal-stop-zero-resources', 'Normal stop clears actual owner, timer, subscriptions and updater presenter', 'destroyOnExit', async make => {
  const e = make(); await e.ready(); const initial = e.snapshot(); e.feedback(); await e.clock.flush(); const active = e.snapshot(); e.stop();
  const m = await stoppedMeasurements(e);
  return { measurements: { ...m, initial, active }, checks: [check(initial.remainingOwnedResources.windows === 1, 'ON pre-owns persistent window before feedback', initial.remainingOwnedResources), check(active.host.state.timerPending && active.host.state.current?.id, 'real normalized feedback arms actual expiry timer', active.host.state), check(zero(m.remainingOwnedResources), 'all feature owned resource categories reach zero', m.remainingOwnedResources), check(m.bounded && m.ownerRevoked && m.stateDisposed && m.presenterSlotReleased, 'bounded owner revocation and real presenter release', m)] };
});
await run('expiry-retains-persistent-host', 'Receipt expires while the enabled host keeps its window and subscriptions', null, async make => {
  const e = make(); await e.ready(); e.feedback(); await e.clock.flush(); await e.clock.advance(9000); const m = e.snapshot();
  return { measurements: m, checks: [check(m.host.state.current === null && !m.host.state.timerPending, 'actual receipt expiry clears only transient notice', m.host.state), check(m.remainingOwnedResources.windows === 1 && m.host.owner?.ready && m.remainingOwnedResources.subscriptions > 0, 'host retains resources beyond receipt lifetime', m.remainingOwnedResources)] };
});
for (const mode of ['false', 'error', 'hold']) {
  await run('stop-close-' + mode, 'Stop with native close ' + mode + ' must not lose a live owned window', mode === 'hold' ? 'destroyWhenStopStalls' : 'destroyOnExit', async make => {
    const e = make({ decide: request => request.cmd === 'monitorNotice.close' ? mode : 'success' }); await e.ready(); e.feedback(); await e.clock.flush(); e.stop(); e.stop();
    const m = await stoppedMeasurements(e, mode === 'hold'); m.closeAttempts = e.count('monitorNotice.close');
    return { measurements: m, checks: [check(m.ownerRevoked && m.stateDisposed, 'actual JS owner revoked even on close failure', m), check(zero(m.remainingOwnedResources), 'window and held operations actually reach zero', m.remainingOwnedResources), ...(mode === 'hold' ? [check(m.bounded && m.fallbackExercised && m.fallbackOwnedOnly, 'stalled stop exercises bounded product fallback for owned resources', m)] : [])] };
  });
}
await run('repeated-stop-idempotent', 'Repeated stop invokes native close once and keeps all resources released', 'destroyOnExit', async make => {
  const e = make(); await e.ready(); e.feedback(); await e.clock.flush(); for (let i = 0; i < 5; i++) e.stop(); const m = await stoppedMeasurements(e); m.closeAttempts = e.count('monitorNotice.close');
  return { measurements: m, checks: [check(m.closeAttempts === 1, 'five stops produce exactly one close request', m.closeAttempts), check(zero(m.remainingOwnedResources) && m.ownerRevoked && m.bounded, 'all resources remain zero', m)] };
});
await run('late-events-after-stop', 'Power, theme, feedback, retired callbacks and updater cannot resurrect stopped host', 'preventResurrectionAfterExit', async make => {
  const e = make(); await e.ready(); e.feedback(); await e.clock.flush(); e.stop(); await e.clock.flush(); const marker = e.trace.length;
  for (const event of ['power.pending', 'power.suspending', 'power.resumed']) e.emit(event);
  e.theme(); e.feedback(); e.deliverRetiredCallbacks(); e.deliverRetiredTimers(); const updateAccepted = await e.api.presentUpdateReady('0.0.999'); await e.clock.advance(30000);
  const m = { ...await stoppedMeasurements(e), lateCallbacksDelivered: e.lateCount(), restartsAfterExit: e.count('monitorNotice.create', marker), framesAfterExit: e.count('monitorNotice.frame', marker), updateAccepted };
  return { measurements: m, checks: [check(m.lateCallbacksDelivered > 0, 'retired callbacks were actually delivered', m.lateCallbacksDelivered), check(m.restartsAfterExit === 0 && m.framesAfterExit === 0 && !updateAccepted, 'no new window/frame or updater acceptance after exit', m), check(zero(m.remainingOwnedResources) && m.ownerRevoked, 'resources stay released', m)] };
});
for (const closeMode of ['success', 'false']) {
  await run('late-create-before-timeout-close-' + closeMode, 'Create returns after stop but before real IPC timeout; close ' + closeMode, 'destroyOnExit', async make => {
    const e = make({ decide: request => request.cmd === 'monitorNotice.create' ? 'hold' : request.cmd === 'monitorNotice.close' ? closeMode : 'success' }); await e.ready(); const before = e.snapshot(); e.stop(); await e.clock.advance(50); await e.lateReplies();
    const m = { ...await stoppedMeasurements(e), before, lateCallbacksDelivered: e.lateCount(), closeAttempts: e.count('monitorNotice.close') };
    return { measurements: m, checks: [check(before.host.opening && m.lateCallbacksDelivered > 0, 'actual prepare continuation receives late create', m), check(zero(m.remainingOwnedResources) && m.ownerRevoked && m.bounded, 'late-created native window is actually destroyed', m.remainingOwnedResources)] };
  });
}
await run('late-create-after-real-ipc-timeout', 'A native create reply after real IPC timeout leaves an untracked native window', 'destroyWhenStopStalls', async make => {
  const e = make({ decide: request => request.cmd === 'monitorNotice.create' ? 'hold' : 'success' }); await e.ready(); e.stop(); await e.clock.advance(11000); const beforeReply = e.snapshot(); await e.lateReplies();
  const m = { ...await stoppedMeasurements(e, true), beforeReply, lateCallbacksDelivered: e.lateCount(), closeAttempts: e.count('monitorNotice.close') };
  return { measurements: m, checks: [check(m.lateCallbacksDelivered > 0 && !beforeReply.host.opening, 'reply arrives after actual invoke timeout settled prepare', m), check(zero(m.remainingOwnedResources), 'late window is destroyed despite discarded IPC reply', m.remainingOwnedResources), check(m.bounded && m.ownerRevoked && m.fallbackExercised && m.fallbackOwnedOnly, 'owned fallback covers late create after timeout', m)] };
});
await run('off-stays-off-with-events', 'Explicit OFF stays OFF across power, theme, feedback and updater requests', 'explicitOffStaysOff', async make => {
  const e = make(); await e.ready(); e.feedback(); await e.clock.flush(); e.host.update({ ...e.preferences, popupEnabled: false }); await e.clock.flush(); const marker = e.trace.length;
  e.externalLoss(); e.nextGeneration(); for (const event of ['power.pending', 'power.suspending', 'power.resumed']) e.emit(event); e.theme(); e.feedback(); const updateAccepted = await e.api.presentUpdateReady('0.0.999'); e.deliverRetiredCallbacks(); e.deliverRetiredTimers(); const observations = await e.recoveryWindow('explicit-OFF');
  const m = { ...e.snapshot(), healthObserved: true, currentGeneration: observations.at(-1).currentGeneration, recovered: observations.at(-1).recovered, restartsAfterOff: e.count('monitorNotice.create', marker), lateCallbacksDelivered: e.lateCount(), updateAccepted, observations };
  return { measurements: m, checks: [check(!m.host.enabled && m.remainingOwnedResources.windows === 0 && !m.host.state.current && !m.host.state.timerPending, 'OFF closes window and notice timer', m), check(m.restartsAfterOff === 0 && !updateAccepted, 'OFF never self heals or accepts updater notice', m)] };
});
await run('off-during-pending-create', 'Explicit OFF closes a create response arriving while preparation is in flight', 'explicitOffStaysOff', async make => {
  const e = make({ decide: request => request.cmd === 'monitorNotice.create' ? 'hold' : 'success' }); await e.ready(); e.host.update({ ...e.preferences, popupEnabled: false }); await e.lateReplies(); e.emit('power.resumed'); e.feedback(); await e.clock.advance(30000); const m = { ...e.snapshot(), lateCallbacksDelivered: e.lateCount() };
  return { measurements: m, checks: [check(m.lateCallbacksDelivered > 0 && !m.host.enabled && m.host.owner === null && m.remainingOwnedResources.windows === 0, 'late create cannot override explicit OFF', m)] };
});
await run('sleep-with-live-owner-clears-receipt', 'Sleep edges clear real receipt while a surviving owner stays healthy', null, async make => {
  const e = make(); await e.ready(); e.feedback(); await e.clock.flush(); for (const event of ['power.pending', 'power.suspending', 'power.resumed']) e.emit(event); await e.clock.flush(); const m = { ...e.snapshot(), health: e.health('surviving-owner') };
  return { measurements: m, checks: [check(!m.host.state.current && !m.host.state.timerPending && m.health.recovered, 'clear receipt and observe surviving resource health', m)] };
});
await run('sleep-owner-loss-no-self-heal', 'ON host lost during sleep must independently recover without user feedback', 'recoverAfterSleep', async make => {
  const e = make(); await e.ready(); e.feedback(); await e.clock.flush(); const healthyBefore = e.health('before-sleep');
  e.emit('power.pending', { generation: 1 }); e.emit('power.suspending', { generation: 1 }); e.externalLoss('sleep invalidates carrier'); e.nextGeneration(); e.emit('power.resumed', { generation: 2 });
  const observations = await e.recoveryWindow('after-resume'), last = observations.at(-1), recovery = e.host.inspect().recovery;
  const committedReady = e.trace.find(t => t.type === 'native.event' && t.event === 'power.resumed' && t.data.generation === recovery?.committedGeneration);
  const m = { ...last, healthyBefore, observations, attempts: e.count('monitorNotice.status') - 1, recoveryPolicy: recovery?.policy ?? null, enabled: e.host.inspect().enabled, observationWindowMs: 30000,
    sleepObserved: e.trace.some(t => t.type === 'native.event' && t.event === 'power.suspending' && t.data.generation === 1),
    readyGranted: Boolean(committedReady && recovery?.committedGeneration === last.currentGeneration && !recovery.powerHeld), committedReadyEvent: committedReady ?? null, actualRecoveryState: recovery ?? null };
  return { measurements: m, checks: [check(healthyBefore.recovered && m.enabled && m.healthObserved, 'start healthy, remain ON and perform independent health observations', m), check(m.recovered && m.observedGeneration === m.currentGeneration, 'healthy current generation owner restored after sleep', m)] };
});
await run('transient-carrier-loss-no-self-heal', 'ON host externally closed must recover without another receipt or settings change', 'recoverAfterTransientFailure', async make => {
  const e = make(); await e.ready(); const healthyBefore = e.health('before-transient-loss'); e.externalLoss(); const observations = await e.recoveryWindow('after-transient-loss'), last = observations.at(-1), m = { ...last, healthyBefore, observations, attempts: e.count('monitorNotice.status') - 1, recoveryPolicy: e.host.inspect().recovery?.policy ?? null, observationWindowMs: 30000 };
  return { measurements: m, checks: [check(m.enabled && m.healthObserved && healthyBefore.recovered, 'transient failure has independently observed healthy precondition and ON intent', m), check(m.recovered && m.currentGeneration === m.observedGeneration, 'host regains independently observed health', m)] };
});
await run('transient-status-error-no-self-heal', 'A single status failure during prepare must recover while user remains ON', 'recoverAfterTransientFailure', async make => {
  let failures = 1; const e = make({ decide: request => request.cmd === 'monitorNotice.status' && failures-- > 0 ? 'error' : 'success' }); await e.ready(); const observations = await e.recoveryWindow('after-single-status-error'), last = observations.at(-1), m = { ...last, observations, attempts: e.count('monitorNotice.status'), recoveryPolicy: e.host.inspect().recovery?.policy ?? null, statusAttempts: e.count('monitorNotice.status'), createAttempts: e.count('monitorNotice.create'), faultBudgetRemaining: failures, observationWindowMs: 30000 };
  return { measurements: m, checks: [check(m.enabled && m.healthObserved && m.faultBudgetRemaining <= 0, 'single transient error exhausted; healthy transport available', m), check(m.recovered && m.observedGeneration === m.currentGeneration, 'host retries and health recovers independently', m)] };
});
await run('feedback-driven-recreation-loses-triggering-receipt', 'Receipt triggering on-demand recreation must reach the actual frame consumer', 'recoverAfterTransientFailure', async make => {
  const e = make(); await e.ready(); e.externalLoss(); const value = e.feedback(); await e.clock.flush(); const health = e.health('after-feedback-triggered-recreation');
  const receiptFrames = e.trace.filter(t => t.type === 'native.request' && t.cmd === 'monitorNotice.frame' && t.args.frame.feedback.id === value.id);
  const m = { ...health, triggeringFeedbackId: value.id, deliveredReceiptFrames: receiptFrames.length, sourceState: e.host.inspect().state, automaticRecoveryWithoutFeedback: false };
  return { measurements: m, checks: [check(m.healthObserved && m.recovered, 'on-demand create actually restores a healthy owner', m), check(m.deliveredReceiptFrames > 0, 'triggering receipt actually reaches monitorNotice.frame', m)] };
});
await run('second-feedback-after-recreation-delivers', 'A later receipt is normalized, timed and delivered after recreation completes', null, async make => {
  const e = make(); await e.ready(); e.externalLoss(); e.feedback(); await e.clock.flush(); const later = e.feedback(); await e.clock.flush(); const m = { health: e.health('second-feedback'), state: e.host.inspect().state, frames: e.trace.filter(t => t.type === 'native.request' && t.cmd === 'monitorNotice.frame').map(t => t.args.frame.feedback.id), laterId: later.id };
  return { measurements: m, checks: [check(m.health.recovered && m.frames.includes(later.id) && m.state.timerPending, 'real feedback consumer sends the later receipt', m)] };
});
await run('transient-feedback-burst-has-no-retry-budget', 'Repeated feedback against a transient status fault must respect a finite retry limit', 'respectRecoveryLimits', async make => {
  const e = make({ decide: request => request.cmd === 'monitorNotice.status' ? 'error' : 'success' }); await e.ready(); const before = e.count('monitorNotice.status');
  for (let i = 0; i < 8; i++) { e.feedback(); await e.clock.flush(); await e.clock.advance(100); }
  const recovery = e.host.inspect().recovery, policy = recovery?.policy;
  const declared = Number.isSafeInteger(policy?.maxAttempts) && policy.maxAttempts > 0 && Array.isArray(policy.retryDelaysMs) && policy.retryDelaysMs.length === policy.maxAttempts - 1 && policy.retryDelaysMs.every(ms => Number.isFinite(ms) && ms > 0) && Array.isArray(policy.resetOn) && policy.resetOn.length > 0;
  const enforced = declared && e.count('monitorNotice.status') <= policy.maxAttempts && recovery.attempts === e.count('monitorNotice.status');
  const m = { health: e.health('retry-burst'), currentGeneration: e.health('retry-burst-generation').currentGeneration, recoveryAttempts: e.count('monitorNotice.status') - before, observedAttempts: e.trace.filter(t => t.type === 'native.request' && t.cmd === 'monitorNotice.status').map(t => t.at), probeWindowMs: 1000, stimulusCount: 8, sourceEnforcedBudgetClaimed: enforced, rateLimitObserved: e.count('monitorNotice.status') - before < 8, documentedRecoveryLimit: enforced ? structuredClone(policy) : null, unverifiedReason: enforced ? null : 'The actual product did not declare and enforce a finite attempt policy with reset boundaries in this probe.', actualRecoveryState: recovery ?? null };
  // Observe exhaustion and no additional starts while still ON. Only then
  // exercise explicit OFF; no fixture-side reset or recovery is supplied.
  await e.clock.advance(30000);
  const atLimit = e.host.inspect().recovery, attemptsBeforeFurtherInput = e.count('monitorNotice.status');
  for (let i = 0; i < 8; i++) { e.feedback(); e.host.update(e.preferences); await e.clock.flush(); }
  await e.clock.advance(30000);
  m.limitReached = Boolean(m.documentedRecoveryLimit && atLimit?.attempts === m.documentedRecoveryLimit.maxAttempts && !atLimit.scheduled);
  m.additionalStarts = e.count('monitorNotice.status') - attemptsBeforeFurtherInput;
  m.postLimitObservationWindowMs = 60000;
  const marker = e.trace.length; e.host.update({ ...e.preferences, popupEnabled: false }); e.feedback(); e.emit('power.resumed'); await e.clock.flush(); m.explicitOffHonored = e.count('monitorNotice.status', marker) === 0 && !e.host.inspect().enabled;
  return { measurements: m, checks: [check(m.recoveryAttempts > 0, 'actual product prepare retries executed', m), check(m.rateLimitObserved && m.documentedRecoveryLimit !== null, 'a declared recovery limit and its actual enforcement must be observed', m)] };
});
await run('update-presenter-stop-during-frame', 'Actual updater presenter rejects a delivery completion after host stop', 'preventResurrectionAfterExit', async make => {
  const e = make({ decide: request => request.cmd === 'monitorNotice.frame' ? 'hold' : 'success' }); await e.ready(); const pending = e.api.presentUpdateReady('0.0.999'); await e.clock.flush(); const active = e.snapshot(); e.stop(); const marker = e.trace.length; await e.lateReplies(); const accepted = await pending;
  const m = { ...await stoppedMeasurements(e), active, accepted, lateCallbacksDelivered: e.lateCount(), restartsAfterExit: e.count('monitorNotice.create', marker), framesAfterExit: e.count('monitorNotice.frame', marker) };
  return { measurements: m, checks: [check(active.host.state.current?.actionId === 'app.updateReady' && m.lateCallbacksDelivered > 0, 'real updater notice was in flight and reply delivered late', m), check(!accepted && m.restartsAfterExit === 0 && m.framesAfterExit === 0 && zero(m.remainingOwnedResources), 'late updater acknowledgment cannot revive or claim acceptance', m)] };
});
await run('stale-ready-after-stop-does-not-own', 'A late native ready notification cannot grant ownership after stop', 'preventResurrectionAfterExit', async make => {
  const e = make(); await e.ready(); const owner = e.host.inspect().owner; e.stop(); const marker = e.trace.length; e.emit('monitorNotice.ready', owner); e.deliverRetiredCallbacks(); await e.clock.flush();
  const m = { ...await stoppedMeasurements(e), lateCallbacksDelivered: e.lateCount(), restartsAfterExit: e.count('monitorNotice.create', marker) };
  return { measurements: m, checks: [check(m.lateCallbacksDelivered > 0 && m.restartsAfterExit === 0 && m.ownerRevoked && zero(m.remainingOwnedResources), 'late ready callback cannot reacquire resources', m)] };
});

try {
  toolPhase('postrun-start');
  toolBinding.after = toolApi.verifyBundledToolScope(toolAuthority, root);
  toolBinding.helperSourcesAfter = toolApi.verifyBundledImplementation(root);
  if (JSON.stringify(toolImplementationBefore) !== JSON.stringify(toolBinding.helperSourcesAfter)) throw Error('TOOL_HELPER_SOURCE_CHANGED');
  toolBinding.observedToolDependencies = Object.keys(req.cache).filter(file => !cacheBeforeCompiler.has(file)).map(file => {
    const classified = toolApi.classifyBundledInput(root, file, fs.readFileSync(file), toolAuthority);
    if (classified.role !== 'tool') throw Error('COMPILER_DEPENDENCY_NOT_IN_TOOL_GRAPH');
    return classified.row;
  });
  if (!toolBinding.observedToolDependencies.some(row => row.path.toLowerCase() === esbuildEntry.toLowerCase())) throw Error('COMPILER_ENTRY_NOT_OBSERVED');
  toolPhase('postrun-verified'); toolBinding.status = 'VERIFIED';
  write('tool-binding.after.json', toolBinding);
} catch (error) { toolRefusal('postrun', error); }
const drift = [];
for (const [relative, expected] of Object.entries({ ...sourceHashes, ...referencesBefore })) {
  const actual = hash(fs.readFileSync(path.join(root, relative))); if (actual !== expected) drift.push({ relative, expected, actual });
}
write('source-hashes.after.json', { sourceHashes, referencesBefore, drift });
const primaryCases = {
  destroyOnExit: 'stop-close-false', destroyWhenStopStalls: 'stop-close-hold',
  preventResurrectionAfterExit: 'late-events-after-stop', explicitOffStaysOff: 'off-stays-off-with-events',
  recoverAfterSleep: 'sleep-owner-loss-no-self-heal', recoverAfterTransientFailure: 'transient-carrier-loss-no-self-heal',
  respectRecoveryLimits: 'transient-feedback-burst-has-no-retry-budget',
};
const observations = [...EXIT_REQUIREMENTS, ...PERSISTENT_REQUIREMENTS].map(requirement => {
  const row = cases.find(item => item.id === primaryCases[requirement]);
  const m = structuredClone(row.measurements);
  if (requirement === 'explicitOffStaysOff') {
    m.desiredEnabled = m.host.enabled; m.ownerRevoked = m.host.owner === null;
    m.bounded = m.remainingOwnedResources.windows === 0 && m.remainingOwnedResources.timers === 0 && m.remainingOwnedResources.pendingIpc === 0;
    m.lateCallbacksDelivered = row.measurements.lateCallbacksDelivered;
    m.restartsAfterExit = m.restartsAfterOff;
    m.offKeepsLiveSubscriptions = m.remainingOwnedResources.subscriptions;
    m.hostControlListening = { subscriptions: m.remainingOwnedResources.subscriptions, duplicateHistoryEntries: m.remainingOwnedResources.stateEntries, retainedForExplicitReenable: true, destructionRequiredForExplicitOff: false };
    m.offObservationPolicy = 'Explicit OFF prohibits automatic create and updater acceptance; resident host control listeners and bounded duplicate history may remain until stop.';
  }
  if (requirement === 'recoverAfterSleep' || requirement === 'recoverAfterTransientFailure') {
    m.desiredEnabled = m.enabled; m.currentGenerationNumber = m.currentGeneration;
    m.currentGeneration = m.recovered && m.currentGenerationNumber === m.observedGeneration;
    m.maxAttempts = Number.isSafeInteger(m.recoveryPolicy?.maxAttempts) ? m.recoveryPolicy.maxAttempts : null; // Missing actual product policy stays unverified.
    m.unconfirmedWritesReplayed = 0;
    if (requirement === 'recoverAfterSleep') { m.sleepObserved = row.measurements.sleepObserved === true; m.readyGranted = row.measurements.readyGranted === true; }
  }
  if (requirement === 'respectRecoveryLimits') { m.limitReached = row.measurements.limitReached === true; m.additionalStarts = row.measurements.additionalStarts ?? null; }
  return { requirement, caseId: row.id, passed: row.passed && (requirement !== 'explicitOffStaysOff' || (m.desiredEnabled === false && m.lateCallbacksDelivered > 0 && m.restartsAfterExit === 0 && m.updateAccepted === false)), measurements: m };
});
const failures = cases.filter(row => !row.passed), infrastructureFailures = cases.filter(row => row.infrastructureFailure);
const report = { schemaVersion: 1, id: 'lifecycle.monitor-notice-host', status: failures.length || drift.length || toolBinding.status !== 'VERIFIED' ? 'FAIL' : 'PASS', ok: failures.length === 0 && drift.length === 0 && toolBinding.status === 'VERIFIED',
  cases, sourceHashes, referencesBefore, sourceDrift: drift, counts: { cases: cases.length, passed: cases.length - failures.length, failed: failures.length, checks: cases.reduce((n, row) => n + row.checks.length, 0), infrastructureFailures: infrastructureFailures.length },
  lifecycleContract: getMonitorNoticeLifecycleAdapter().lifecycleContract,
  lifecycleEvidence: { schemaVersion: 1, contractFingerprint: lifecycleContractFingerprint(getMonitorNoticeLifecycleAdapter().lifecycleContract), features: [{ id: 'monitor.notice-host', observations }] },
  legacyFixtureReuse: { hashFunctionFrom: 'tools/ymcc_sim/global/monitor-signals/common.mjs', actualDescriptorsLoaded: reusedDescriptors, oldCasesReexecuted: false, oldCasesAndBudgetsModified: false },
  classification: { persistence: 'persistent', observedByCase: 'expiry-retains-persistent-host', rationale: getMonitorNoticeLifecycleAdapter().lifecycleContract.features[0].rationale },
  boundaries: { productSourceRewritten: false, executedProductSources: PRODUCT_SOURCES, realIpcTimeoutAndEventsExecuted: true, replacedTerminals: ['window.chrome.webview', 'window/document event and theme terminals', 'Date.now', 'setTimeout/clearTimeout'],
    nativeCarrierExecuted: false, nativeForceDestructionVerified: false, installedYmccStarted: false, deviceIoExecuted: false, realSleepExecuted: false,
    unverified: ['Native Carrier HWND/COM/lease resource destruction and force fallback', 'Full startMonitorHudHost outer consumer and App unmount', 'Renderer/Vue layout, original monitor-input cases and monitor-signals cases in this run', 'Real WebView2 IPC ordering/OS timer scheduling', 'Installed YMCC, actual sleep/resume, devices and physical inputs'],
    referenceOnlySources: referencePaths, legacyCasesChanged: false, fullGlobalRegisteredRunExecuted: false, publicRunnerModified: false },
  provenance: { root, toolBinding, lifecycleGate: { path: gateFile, sha256: gateBeforeSha256, afterSha256: hash(fs.readFileSync(gateFile)), externalCandidateForThisRun: Boolean(option('--lifecycle-gate-module')) }, node: { path: process.execPath, version: process.version, sha256: hash(fs.readFileSync(process.execPath)) }, esbuild: { entry: esbuildEntry, sha256: hash(fs.readFileSync(esbuildEntry)), version: esbuild.version },
    bundleSha256: hash(bundle), capturedOnlyUnitSources: true, fullRepositorySnapshot: false, adapterRegistration: 'candidate-only; parent import required' },
  observedFailures: failures.map(row => ({ caseId: row.id, reason: row.checks.filter(item => !item.passed).map(item => item.name), infrastructureFailure: row.infrastructureFailure })),
  durationMs: performance.now() - started, preservedFailurePolicy: 'Any real product behavior failure keeps status FAIL and exit code 2; fixture teardown and expected failures cannot turn this green.' };
write('result.json', report);
console.log(JSON.stringify({ status: report.status, out, ...report.counts, failedCases: failures.map(row => row.id), sourceDrift: drift.length, toolBindingStatus: toolBinding.status, toolArtifactCount: toolBinding.before?.tools.length }));
process.exitCode = report.ok ? 0 : 2;



