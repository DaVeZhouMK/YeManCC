import { watch } from 'vue';
import { performanceConsumptionSignature, normalizeHyperThreadMode, normalizeControlPowerSchemes } from './performanceControls';
import { isGameStatusCurrent, isCurrentGameTarget, subscribeGameTargetReadiness } from './gamedetect';
import { powerSourceMode } from './powerSource';
import { detectPowerMode } from './yeman';
import { loadGlobalFrameRates, onFrameRatesChanged, dedicatedFrameRatePair, applyIndependentFrameRates } from './frameRateLimits';
import { setFloatRtssLinked } from './autofloat';
import { subscribePolicyGameStatus, getPolicyGame, gamePolicyKey, isPolicyGameInitialized } from './gamePolicyTarget';
import { loadGameCustomConfig, loadPerformanceSchedule, restorePerformanceScheduleIfConfigured,
  onGameCustomConfigChanged, onPerformanceScheduleChanged, onContinuousPerformanceApplied, isPerformanceScheduleBusy } from './performanceSchedule';
import { applyGameCorePolicy, clearGameCorePolicy, detectGameCorePolicy } from './gameCorePolicy';
import { registerScheduledTask } from '@/scheduler';
import { powerLifecycle } from './api';
import { isQuickActionBusy } from './quickActionLock';

// Resident owner: automatic dedicated policies never depend on menu/page visibility.
let active = false;
let epoch = 0;
let running = false;
let dirty = true;
let appliedPerformance = '';
let appliedCore = '';
let appliedFrames = '';
let observedSchedule = '';
let stops: (() => void)[] = [];
let stopRetry: (() => void) | null = null;
function ensureRetry(): void {
  if (active && !stopRetry) stopRetry = registerScheduledTask(
    'game-policy-runtime-retry', 2000, reconcile, { pauseWhenHidden: false });
}
function scheduleSignature(config: Awaited<ReturnType<typeof loadPerformanceSchedule>>): string {
  return JSON.stringify([config.configured, config.enabled, config.active, config.profiles, config.controls, normalizeControlPowerSchemes(config.powerSchemes), normalizeHyperThreadMode(config.hyperThreadPolicy)],(key,value)=>key==='fpsTarget'?undefined:value);
}
async function reconcile(): Promise<void> {
  if (!active || running || !dirty || !isPolicyGameInitialized() || !isGameStatusCurrent() || isQuickActionBusy() || isPerformanceScheduleBusy()) return;
  running = true;
  const generation = epoch;
  const game = getPolicyGame();
  const current = () => active && generation === epoch && isGameStatusCurrent() &&
    (game ? isCurrentGameTarget(game) : getPolicyGame() === null);
  try {
    const lifecycle = await powerLifecycle.get().catch(() => null);
    if (!current() || lifecycle?.phase !== 'ready' || lifecycle.hardwareWritesAllowed !== true) return;
    const config = await loadGameCustomConfig();
    const schedule = await loadPerformanceSchedule();
    if (!current()) return;
    observedSchedule = scheduleSignature(schedule);
    const entry = game ? config.entries[gamePolicyKey(game)] : undefined;
    const enabledEntry = entry?.enabled !== false ? entry : undefined;
    const identity = game ? JSON.stringify([game.pid, game.processCreated, game.path.toLowerCase(), game.identityGeneration ?? 0]) : '';
    // Input-only/display-name edits must not replay CPU/TDP/FPS or process affinity.
    const side = powerSourceMode.value;
    if (!side) return;
    const globalFrames = await loadGlobalFrameRates();
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
    if (!current()) return;
    if (performanceSignature !== appliedPerformance) {
      if (isQuickActionBusy() || isPerformanceScheduleBusy()) return; // shared owner may have changed during the reads above
      // An automatic dedicated bank is independent of the global scheduler.
      // Passive AC/DC or resident refreshes may restore that automatic owner,
      // but must leave a manual bank unchanged until the user adjusts it.
      if (enabledEntry || (schedule.configured && schedule.enabled)) {
        await restorePerformanceScheduleIfConfigured(undefined, { automaticOnly: true });
      }
      if (!current()) return;
      appliedPerformance = performanceSignature;
    }
    const frameSignature=JSON.stringify([identity,side,!!enabledEntry,frameOwner?.enabled,frames[side].fps,frames[side].lastFps]);
    if(frameSignature!==appliedFrames){
      if(!await applyIndependentFrameRates(current))return;
      if(!active || generation!==epoch)return;
      appliedFrames=frameSignature;
    }
    if (coreSignature !== appliedCore) {
      const capabilities = await detectGameCorePolicy();
      if (!capabilities) return; // failed probe is not permission to drop a lease.
      const mode = 'default';
      const hyper = capabilities.smtAvailable ? hyperIntent : 'default';
      if (!current()) return;
      if (game && (mode !== 'default' || hyper !== 'default')) {
        const result = await applyGameCorePolicy(game, mode, hyper);
        if (!result.ok || !result.applied) return;
      } else if (!await clearGameCorePolicy()) return;
      if (!current()) return;
      appliedCore = coreSignature;
    }
    if (!current()) return;
    dirty = false;
  } catch {
    // Retain state and retry; never manufacture a restore from a failed read.
  } finally {
    running = false;
    // An acknowledged policy needs no background wake-up. Busy/power/failure
    // paths leave dirty set and retain their original bounded-rate retry.
    if (!dirty) { stopRetry?.(); stopRetry = null; }
    else ensureRetry();
  }
}
function invalidate(): void { epoch += 1; dirty = true; ensureRetry(); void reconcile(); }
export function startGamePolicyRuntime(): void {
  if (active) return;
  active = true; dirty = true;
  ensureRetry();
  appliedFrames = ''; // A stopped owner cannot acknowledge a later RTSS session.
  setFloatRtssLinked(false);
  stops = [
    onContinuousPerformanceApplied(signature => { if (active) appliedPerformance = signature; },
      () => { if (active) appliedPerformance = ''; }),
    subscribePolicyGameStatus(invalidate),
    subscribeGameTargetReadiness((ready) => {
      if (!ready) invalidate();
      else { dirty = true; ensureRetry(); void reconcile(); }
    }),
    watch(powerSourceMode,invalidate),
    onFrameRatesChanged(invalidate),
    onGameCustomConfigChanged(invalidate),
    onPerformanceScheduleChanged((config) => {
      const next = scheduleSignature(config);
      if (next === observedSchedule) return; // our own idempotent save is not a new policy.
      observedSchedule = next; invalidate();
    }),
  ];
}
export function stopGamePolicyRuntime(): void {
  active = false; epoch += 1;
  stopRetry?.(); stopRetry = null;
  for (const stop of stops) stop(); stops = [];
}
