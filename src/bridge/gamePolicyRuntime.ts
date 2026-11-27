import { watch } from 'vue';
import { performanceConsumptionSignature, normalizeHyperThreadMode, normalizeControlPowerSchemes, beginInvalidTdpRecovery, finishInvalidTdpRecovery, type InvalidTdpRecovery } from './performanceControls';
import { isGameStatusCurrent, isCurrentGameTarget, subscribeGameTargetReadiness } from './gamedetect';
import { powerSourceMode, powerSourceBatteryPresent } from './powerSource';
import { detectPerformanceTemplate, detectPowerMode } from './yeman';
import { loadGlobalFrameRates, onFrameRatesChanged, dedicatedFrameRatePair, applyIndependentFrameRates } from './frameRateLimits';
import { setFloatRtssLinked, getFloatInfo } from './autofloat';
import { subscribeMonitorSnapshot, type MonitorSnapshotView } from './monitorData';
import { monitorProofFresh } from './monitorSnapshotSignals';
import { subscribePolicyGameStatus, getPolicyGame, gamePolicyKey, isPolicyGameInitialized } from './gamePolicyTarget';
import { loadGameCustomConfig, loadPerformanceSchedule, restorePerformanceScheduleIfConfigured,
  onGameCustomConfigChanged, onPerformanceScheduleChanged, onContinuousPerformanceApplied, isPerformanceScheduleBusy, reconcilePerformanceScheduleTdp, getInvalidPerformanceTdpTarget, reconcileInvalidPerformanceTdp, continuousControlsFromSchedule, continuousControlsForEntry } from './performanceSchedule';
import { applyGameCorePolicy, clearGameCorePolicy, detectGameCorePolicy } from './gameCorePolicy';
import { registerScheduledTask } from '@/scheduler';
import { powerLifecycle } from './api';
import { beginSharedPowerLifecycleReadback, getSharedPowerLifecycle, onSharedPowerLifecycle, isSharedPowerLifecycleAdmitted, sharedPowerWriteScope } from './powerLifecycleState';
import { isQuickActionBusy } from './quickActionLock';
import { createPowerResumeQueue } from './powerResume';

// Resident owner: automatic dedicated policies never depend on menu/page visibility.
let active = false;
let epoch = 0;
let running = false;
const resumeQueue = createPowerResumeQueue();
let runningCurrent: (() => boolean) | null = null;
let runningRead: AbortController | null = null;
function retireWork(): void {
  const read = runningRead;
  runningRead = null;
  resumeQueue.invalidate(); running = false; runningCurrent = null;
  read?.abort();
}
async function withPolicyReadSignal<T>(work: (signal: AbortSignal) => Promise<T>): Promise<T> {
  const read = new AbortController();
  runningRead = read;
  try { return await work(read.signal); }
  finally { if (runningRead === read) runningRead = null; }
}
async function readPolicyFrameTemplate(current: () => boolean): Promise<'portable' | 'desktop'> {
  if (!current()) throw new Error('Frame template read retired');
  return withPolicyReadSignal(async signal => {
    let retire!: () => void;
    const retired = new Promise<never>((_, reject) => { retire = () => reject(signal.reason); });
    signal.addEventListener('abort', retire, { once: true });
    try { return await Promise.race([retired, detectPerformanceTemplate()]); }
    finally { signal.removeEventListener('abort', retire); }
  });
}
let powerHeld = true, powerGeneration = 0;
let powerNotificationSequence: number | undefined;
let powerSuspendSerial: number | undefined;
let dirty = true;
let appliedPerformance = '';
let tdpAppliedIntent = '';
let appliedCpuIntent = '';
let appliedCore = '';
let coreWritePending = false;
let uncertainCore = '';
let appliedFrames = '';
let observedSchedule = '';
let followsTemplateDefaults: boolean | null = null;
let invalidTdpRecovery: InvalidTdpRecovery | null = null;
let stops: (() => void)[] = [];
let stopRetry: (() => void) | null = null;
let tdpPending = false, tdpNextCheck = 0, tdpChecksRemaining = 0, tdpCeiling = 0;
let lastWakeGeneration = 0, tdpObservedAt = 0, tdpHighSince = 0, tdpCorrectionAfter = 0;
let lastWakeSuspendSerial: number | undefined;
function resetTdpChecks(): void {
  invalidTdpRecovery = null;
  tdpPending = true; tdpNextCheck = 0; tdpChecksRemaining = 2;
  tdpAppliedIntent = '';
  tdpCeiling = 0; tdpObservedAt = 0; tdpHighSince = 0;
}
// Two bounded settle checks cover OEM software that writes after wake. Reuse
// the existing resident task; healthy owners have no periodic hardware probe.
export function requestGamePolicyTdpReconciliation(generation: number): boolean {
  const power = getSharedPowerLifecycle();
  if (!active || powerHeld || !isSharedPowerLifecycleAdmitted(power) ||
      power.generation !== generation || powerGeneration !== generation || generation < lastWakeGeneration) return false;
  const serial = power.suspendSerial;
  const newWake = generation > lastWakeGeneration || (serial !== undefined &&
    lastWakeSuspendSerial !== undefined && serial > lastWakeSuspendSerial);
  if (!newWake) {
    lastWakeSuspendSerial ??= serial;
    return false;
  }
  lastWakeGeneration = generation; lastWakeSuspendSerial = serial;
  // The saved preference is unchanged, but RTSS may have lost it while asleep.
  appliedFrames = ''; dirty = true;
  epoch += 1; retireWork(); resetTdpChecks(); ensureRetry(); void reconcile();
  return true;
}
function observeTdp(snapshot: MonitorSnapshotView | null): void {
  if (!active || powerHeld || tdpCeiling <= 0 || invalidTdpRecovery) return;
  const top = snapshot?.top, now = Date.now(), at = snapshot?.fps?.hardwareObservedAt || top?.ts;
  if (!top || snapshot?.hwDown || top.hwDown || top.cpuPowerValid === false ||
      !monitorProofFresh(top.ts, now, 6000) || !monitorProofFresh(at, now, 6000) || at! > top.ts ||
      typeof top.tdpW !== 'number' || !Number.isFinite(top.tdpW) || top.tdpW < 0) { tdpHighSince = 0; return; }
  if (at! <= tdpObservedAt) return;
  const interrupted = at! - tdpObservedAt > 6000; tdpObservedAt = at!;
  // A live float lease owns this correction. An enabled timer with an expired
  // CPU/scene lease cannot suppress the independent saved TDP protection.
  const float = getFloatInfo();
  if (float.enabled && float.tdpOwnerCurrent !== false) { tdpHighSince = 0; return; }
  if (top.tdpW <= tdpCeiling + Math.max(3, tdpCeiling * .15)) { tdpHighSince = 0; return; }
  if (!tdpHighSince || interrupted) tdpHighSince = at!;
  if (at! - tdpHighSince < 6000 || now < tdpCorrectionAfter) return;
  tdpCorrectionAfter = now + 30000; tdpHighSince = 0;
  tdpPending = true; tdpNextCheck = 0; tdpChecksRemaining = 0;
  ensureRetry(); void reconcile();
}
function ensureRetry(): void {
  if (active && !powerHeld && (dirty || tdpPending) && !stopRetry) stopRetry = registerScheduledTask(
    'game-policy-runtime-retry', 2000, reconcile, { pauseWhenHidden: false });
}
function scheduleSignature(config: Awaited<ReturnType<typeof loadPerformanceSchedule>>): string {
  return JSON.stringify([config.configured, config.enabled, config.suspended, config.active, config.profiles, config.controls, normalizeControlPowerSchemes(config.powerSchemes), normalizeHyperThreadMode(config.hyperThreadPolicy)],(key,value)=>key==='fpsTarget'?undefined:value);
}
async function reconcile(): Promise<void> {
  if (running && runningCurrent?.() === false) retireWork();
  if (!active || powerHeld || running || (!dirty && !tdpPending) || isQuickActionBusy() || isPerformanceScheduleBusy()) return;
  if (!dirty && tdpPending && Date.now() < tdpNextCheck) return;
  return resumeQueue.run(powerGeneration, async ownerCurrent => {
  const generation = epoch;
  const admittedPower = getSharedPowerLifecycle();
  const game = getPolicyGame();
  const admittedGeneration = powerGeneration;
  const current = () => {
    const power = getSharedPowerLifecycle();
    return ownerCurrent() && active && !powerHeld && generation === epoch && isSharedPowerLifecycleAdmitted(power) &&
      power?.generation === admittedGeneration && power.observedNotificationSequence === admittedPower?.observedNotificationSequence &&
      power.suspendSerial === admittedPower?.suspendSerial;
  };
  if (!current()) return;
  running = true; runningCurrent = current;
  const gameCurrent = () => current() && isPolicyGameInitialized() && isGameStatusCurrent() &&
    (game ? isCurrentGameTarget(game) : getPolicyGame() === null);
  const reconcileInvalid = async () => {
    const recovery = invalidTdpRecovery, now = Date.now();
    if (!recovery || now < recovery.nextCheck || !current()) return;
    if (getInvalidPerformanceTdpTarget() === null) {
      // Unknown owner/range waits for its original change notification. Do not
      // turn an unavailable cached target into repeated full settings reloads.
      recovery.nextCheck = tdpNextCheck = now + 60000; return;
    }
    recovery.nextCheck = now + 60000;
    if (now >= recovery.nextWrite) {
      const applied = await reconcileInvalidPerformanceTdp(() => current() && invalidTdpRecovery === recovery).catch(() => false);
      if (!current() || invalidTdpRecovery !== recovery) return;
      finishInvalidTdpRecovery(recovery, now, applied);
    }
    tdpNextCheck = recovery.nextCheck;
  };
  try {
    // The invalid-only heartbeat consumes cached target/receipt state. It must
    // not reload settings, CPU topology or power on every slow check.
    if (!dirty && invalidTdpRecovery) { await reconcileInvalid(); return; }
    // Release a retired read from the scheduler without canceling writes that
    // already belong to the performance, frame rate, or affinity owner.
    const acceptReadback = beginSharedPowerLifecycleReadback();
    const lifecycle = await withPolicyReadSignal(signal => powerLifecycle.get(undefined, signal));
    if (!current()) return;
    // Native ingress can advance before the public notification arrives.
    // Let the shared source retire this scope and start the current owner.
    acceptReadback(lifecycle);
    if (!current() || !isSharedPowerLifecycleAdmitted(lifecycle) || lifecycle.generation !== admittedGeneration ||
        (lifecycle.suspendSerial !== undefined && lifecycle.suspendSerial !== admittedPower?.suspendSerial) ||
        (lifecycle.observedNotificationSequence !== undefined &&
          lifecycle.observedNotificationSequence !== admittedPower?.observedNotificationSequence)) return;
    const config = await loadGameCustomConfig('ensure', { requireReady: true });
    if (!current()) return;
    const schedule = await loadPerformanceSchedule();
    if (!current()) return;
    observedSchedule = scheduleSignature(schedule);
    const entry = game ? config.entries[gamePolicyKey(game)] : undefined;
    const enabledEntry = entry?.enabled !== false ? entry : undefined;
    followsTemplateDefaults = !enabledEntry && schedule.templateDefaults === true;
    const identity = game ? JSON.stringify([game.pid, game.processCreated, game.path.toLowerCase(), game.identityGeneration ?? 0]) : '';
    // Input-only/display-name edits must not replay CPU/TDP/FPS or process affinity.
    const side = powerSourceMode.value;
    if (!side) return;
    const tdpBank = enabledEntry ? continuousControlsForEntry(enabledEntry, schedule)[side] : continuousControlsFromSchedule(schedule)[side];
    const canOwnTdp = !schedule.suspended && (enabledEntry || schedule.configured);
    tdpCeiling = canOwnTdp ? tdpBank.tdp : 0;
    if (!canOwnTdp) { tdpPending = false; tdpChecksRemaining = 0; invalidTdpRecovery = null; }
    else if (tdpBank.tdpValid === false) {
      invalidTdpRecovery ??= beginInvalidTdpRecovery(Date.now());
      tdpPending = true; tdpNextCheck = invalidTdpRecovery.nextCheck;
    } else invalidTdpRecovery = null;
    const reconcileTdp = async (alreadyApplied = false) => {
      if (invalidTdpRecovery) { await reconcileInvalid(); return; }
      const now = Date.now();
      if (!tdpPending || now < tdpNextCheck || !current()) return;
      const applied = alreadyApplied || await reconcilePerformanceScheduleTdp(current).catch(() => false);
      if (!current()) return;
      if (!applied) { tdpNextCheck = now + 30000; return; }
      if (tdpChecksRemaining <= 0) tdpPending = false;
      else { tdpNextCheck = now + (tdpChecksRemaining === 2 ? 3000 : 7000); tdpChecksRemaining -= 1; }
    };
    if (!gameCurrent()) {
      // A dedicated bank may own TDP once the target is confirmed. Preserve
      // the original due time rather than charging a failed write for waiting.
      if (!Object.values(config.entries).some(entry => entry.enabled !== false)) await reconcileTdp();
      if (current()) dirty = false;
      return;
    }
    if (!dirty) { await reconcileTdp(); return; }
    // Settings reads can normalize or repair the file. Only the passive
    // template probe after that transaction can release the scheduler early.
    const globalFrames = await loadGlobalFrameRates(undefined, gameCurrent, () => readPolicyFrameTemplate(gameCurrent));
    if (!gameCurrent()) return;
    const frameOwner = enabledEntry ? config.rtss?.[gamePolicyKey(game)] : undefined;
    const frames = dedicatedFrameRatePair(frameOwner, globalFrames);
    // Ownership is part of the lease even when both banks contain identical values.
    // Legacy dedicated entries depend on their own side, not an unrelated global edit.
    const globalBank = schedule.controls?.[side] ?? schedule.profiles[side][schedule.active[side]];
    const controls = enabledEntry ? enabledEntry.controls : schedule.controls;
    const performanceOwner = enabledEntry ? ['dedicated', gamePolicyKey(game!)] : ['global', schedule.configured, schedule.enabled];
    const performanceBank = enabledEntry ? controls?.[side] ?? enabledEntry[side] : globalBank;
    // Positive legacy FPS changes use the independent target-update path; zero crossings
    // must still start/stop the automatic float owner.
    const frameDemand = controls ? frames[side].fps : frames[side].fps > 0;
    const performanceSignature = performanceConsumptionSignature(performanceOwner, identity, side, frameDemand, performanceBank, normalizeControlPowerSchemes(schedule.powerSchemes)[side]);
    // The global preference persists across game events. Only the current
    // process lease changes; a dedicated profile owns its own preference.
    const hyperIntent = enabledEntry
      ? normalizeHyperThreadMode(enabledEntry.hyperThreadPolicyEnabled ? enabledEntry.hyperThreadPolicy : 'default')
      : normalizeHyperThreadMode(schedule.hyperThreadPolicy);
    const coreSignature = JSON.stringify([identity, hyperIntent]);
    let performancePending = false;
    if (!gameCurrent()) return;
    let bankApplied = false;
    if (performanceSignature !== appliedPerformance && performanceSignature !== appliedCpuIntent) {
      if (isQuickActionBusy() || isPerformanceScheduleBusy()) return; // shared owner may have changed during the reads above
      // Restore the current saved owner once when identity/side/bank changes.
      // Matching writer receipts keep ordinary retries free of hardware replay.
      if (!schedule.suspended && (enabledEntry || schedule.configured) && tdpBank.cpuValid !== false) {
        const restored = await withPolicyReadSignal(signal => restorePerformanceScheduleIfConfigured(undefined, {
          automaticOnly: true, restoreRemembered: true, withReceipt: true, ownsPowerRequest: gameCurrent, powerReadSignal: signal,
        }));
        if (!gameCurrent()) return;
        if (restored.applied) { appliedPerformance = performanceSignature; bankApplied = true; }
        else if (restored.application && restored.application.cpu !== 'unconfirmed') appliedCpuIntent = performanceSignature;
        else performancePending = true;
      }
      if (!gameCurrent()) return;
    }
    // An explicit UI/Decky writer may have completed this exact owner while
    // the resident was waiting. Consume that receipt once; later settle checks
    // still run, but the commit itself must not be written a second time.
    if (tdpAppliedIntent === performanceSignature) { bankApplied = true; tdpAppliedIntent = ''; }
    await reconcileTdp(bankApplied);
    if (!gameCurrent()) return;
    const frameSignature=JSON.stringify([identity,side,!!enabledEntry,frameOwner?.enabled,frames[side].fps,frames[side].lastFps]);
    if(frameSignature!==appliedFrames){
      if(!await applyIndependentFrameRates(gameCurrent))return;
      if(!gameCurrent())return;
      appliedFrames=frameSignature;
    }
    if (coreSignature !== appliedCore) {
      if (coreWritePending) return;
      // Do not replay the same submitted write when IPC loses its receipt.
      if (uncertainCore === coreSignature) { dirty = performancePending; return; }
      const capabilities = await withPolicyReadSignal(detectGameCorePolicy);
      if (!capabilities) return; // failed probe is not permission to drop a lease.
      const mode = 'default';
      const hyper = capabilities.smtAvailable ? hyperIntent : 'default';
      const powerScope = sharedPowerWriteScope(admittedPower);
      if (!powerScope || !gameCurrent() || coreWritePending) return;
      coreWritePending = true;
      uncertainCore = '';
      // A new write may change the prior policy before the reply is received.
      appliedCore = '';
      try {
        if (game && (mode !== 'default' || hyper !== 'default')) {
          const result = await applyGameCorePolicy(game, mode, hyper, powerScope, true);
          if (!result.ok || !result.applied) return;
        } else if (!await clearGameCorePolicy(powerScope, undefined, true)) return;
        appliedCore = coreSignature;
      } catch (error) {
        uncertainCore = coreSignature;
        throw error;
      } finally {
        coreWritePending = false;
      }
      if (!gameCurrent()) return;
    }
    if (!gameCurrent()) return;
    dirty = performancePending;
  } catch {
    // Retain state and retry; never manufacture a restore from a failed read.
  } finally {
    if (ownerCurrent()) {
    running = false; runningCurrent = null;
    // An acknowledged policy needs no background wake-up. Busy/power/failure
    // paths leave dirty set and retain their original bounded-rate retry.
    if (!dirty && !tdpPending) { stopRetry?.(); stopRetry = null; }
    else ensureRetry();
    }
  }
  });
}
function invalidate(tdp = false): void { epoch += 1; retireWork(); dirty = true; if (tdp) resetTdpChecks(); ensureRetry(); void reconcile(); }
export function startGamePolicyRuntime(): void {
  if (active) return;
  active = true; dirty = true; powerHeld = true; powerGeneration = 0; powerNotificationSequence = undefined;
  powerSuspendSerial = undefined; lastWakeGeneration = 0; lastWakeSuspendSerial = undefined;
  followsTemplateDefaults = null;
  resetTdpChecks();
  ensureRetry();
  appliedFrames = ''; // A stopped owner cannot acknowledge a later RTSS session.
  setFloatRtssLinked(false);
  stops = [
    onContinuousPerformanceApplied(signature => { if (active) { appliedPerformance = signature; tdpAppliedIntent = signature; } },
      () => { if (active) { appliedPerformance = ''; tdpAppliedIntent = ''; appliedCpuIntent = ''; dirty = true; ensureRetry(); } }),
    onSharedPowerLifecycle(power => {
      if (!active) return;
      const scopeChanged = power.observedNotificationSequence !== powerNotificationSequence || power.suspendSerial !== powerSuspendSerial;
      powerNotificationSequence = power.observedNotificationSequence;
      powerSuspendSerial = power.suspendSerial;
      if (!isSharedPowerLifecycleAdmitted(power)) {
        if (!powerHeld) epoch += 1;
        powerHeld = true;
        const retry = stopRetry; stopRetry = null;
        retry?.();
        // Aborting the old read can synchronously deliver the next Ready.
        retireWork();
        return;
      }
      const previousGeneration = powerGeneration, wasHeld = powerHeld;
      powerGeneration = power.generation; powerHeld = false;
      if (previousGeneration === 0) {
        lastWakeGeneration = power.generation; lastWakeSuspendSerial = power.suspendSerial;
      } else if (requestGamePolicyTdpReconciliation(power.generation)) return;
      if (wasHeld) {
        ensureRetry();
        if (previousGeneration > 0) void reconcile();
      } else if (scopeChanged && (running || dirty || tdpPending)) {
        // Retire only interrupted work. A new observation is not another wake
        // and cannot reset the original TDP checks, timing or completed owners.
        retireWork(); ensureRetry(); void reconcile();
      }
    }, { observeNotificationSequence: true, observeSuspendSerial: true }),
    subscribeMonitorSnapshot(observeTdp),
    subscribePolicyGameStatus(() => invalidate(true)),
    // Revalidating the same game can follow a same-generation observation.
    // Only a new wake or a change of policy should restart TDP settling.
    subscribeGameTargetReadiness(() => invalidate()),
    watch(powerSourceMode,() => invalidate(true)),
    // Late battery classification can change fresh AC defaults from55 to120
    // without an AC/DC transition. Reuse the original bounded reconciliation.
    watch(powerSourceBatteryPresent,() => { if (followsTemplateDefaults !== false) invalidate(true); }),
    onFrameRatesChanged(() => invalidate()),
    onGameCustomConfigChanged(() => invalidate(true)),
    onPerformanceScheduleChanged((config) => {
      const next = scheduleSignature(config);
      if (next === observedSchedule) return; // our own idempotent save is not a new policy.
      observedSchedule = next; invalidate(true);
    }),
  ];
}
export function stopGamePolicyRuntime(): void {
  invalidTdpRecovery = null;
  active = false; epoch += 1; retireWork(); powerHeld = true; powerGeneration = 0;
  tdpPending = false; tdpCeiling = 0; tdpHighSince = 0;
  appliedPerformance = '';
  tdpAppliedIntent = '';
  appliedCpuIntent = '';
  stopRetry?.(); stopRetry = null;
  for (const stop of stops) stop(); stops = [];
}
