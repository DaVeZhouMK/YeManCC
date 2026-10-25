import { watch } from 'vue';
import { powerSourceMode } from './powerSource';
import { detectPowerMode } from './yeman';
import { loadGlobalFrameRates, onFrameRatesChanged, dedicatedFrameRatePair, applyIndependentFrameRates } from './frameRateLimits';
import { setFloatRtssLinked } from './autofloat';
import { subscribePolicyGameStatus, getPolicyGame, gamePolicyKey, isPolicyGameInitialized } from './gamePolicyTarget';
import { loadGameCustomConfig, loadPerformanceSchedule, restorePerformanceScheduleIfConfigured,
  onGameCustomConfigChanged, onPerformanceScheduleChanged, isPerformanceScheduleBusy } from './performanceSchedule';
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
function scheduleSignature(config: Awaited<ReturnType<typeof loadPerformanceSchedule>>): string {
  return JSON.stringify([config.configured, config.enabled, config.active, config.profiles, config.controls],(key,value)=>key==='fpsTarget'?undefined:value);
}
async function reconcile(): Promise<void> {
  if (!active || running || !dirty || !isPolicyGameInitialized() || isQuickActionBusy() || isPerformanceScheduleBusy()) return;
  running = true;
  const generation = epoch;
  try {
    const lifecycle = await powerLifecycle.get().catch(() => null);
    if (lifecycle?.phase !== 'ready' || lifecycle.hardwareWritesAllowed !== true) return;
    const game = getPolicyGame();
    const config = await loadGameCustomConfig();
    const schedule = await loadPerformanceSchedule();
    observedSchedule = scheduleSignature(schedule);
    const entry = game ? config.entries[gamePolicyKey(game)] : undefined;
    const enabledEntry = entry?.enabled !== false ? entry : undefined;
    const identity = game ? game.pid + ':' + game.processCreated + ':' + (game.identityGeneration ?? 0) : '';
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
    const performanceSignature = JSON.stringify([performanceOwner, identity, side, frameDemand, performanceBank],
      (key,value)=>key==='fpsTarget'?undefined:value);
    // Core selection is owned by ContinuousPerformancePanel/powercfg. This
    // resident watcher retains only the EXE-scoped SMT preference, which is
    // intentionally independent from the global AC/DC core split.
    const smtEntry = entry?.smtIndependent ? entry : enabledEntry;
    const coreSignature = JSON.stringify([identity, smtEntry?.smtIndependent,
      smtEntry?.hyperThreadPolicyEnabled, smtEntry?.hyperThreadPolicy]);
    if (!active || generation !== epoch) return;
    if (performanceSignature !== appliedPerformance) {
      // An automatic dedicated bank is independent of the global scheduler.
      // Passive AC/DC or resident refreshes may restore that automatic owner,
      // but must leave a manual bank unchanged until the user adjusts it.
      if (enabledEntry || (schedule.configured && schedule.enabled)) {
        await restorePerformanceScheduleIfConfigured(undefined, { automaticOnly: true });
      }
      if (!active || generation !== epoch) return;
      appliedPerformance = performanceSignature;
    }
    const frameSignature=JSON.stringify([identity,side,!!enabledEntry,frameOwner?.enabled,frames[side].fps,frames[side].lastFps]);
    if(frameSignature!==appliedFrames){
      if(!await applyIndependentFrameRates(()=>active && generation===epoch))return;
      if(!active || generation!==epoch)return;
      appliedFrames=frameSignature;
    }
    if (coreSignature !== appliedCore) {
      const capabilities = await detectGameCorePolicy();
      if (!capabilities) return; // failed probe is not permission to drop a lease.
      const mode = 'default';
      const hyper = smtEntry && capabilities.smtAvailable && smtEntry.hyperThreadPolicyEnabled !== false
        ? smtEntry.hyperThreadPolicy || 'default' : 'default';
      if (!active || generation !== epoch) return;
      if (game && (mode !== 'default' || hyper !== 'default')) {
        const result = await applyGameCorePolicy(game, mode, hyper);
        if (!result.ok || !result.applied) return;
      } else if (!await clearGameCorePolicy()) return;
      if (!active || generation !== epoch) return;
      appliedCore = coreSignature;
    }
    dirty = false;
  } catch {
    // Retain state and retry; never manufacture a restore from a failed read.
  } finally { running = false; }
}
function invalidate(): void { epoch += 1; dirty = true; void reconcile(); }
export function startGamePolicyRuntime(): void {
  if (active) return;
  active = true; dirty = true;
  appliedFrames = ''; // A stopped owner cannot acknowledge a later RTSS session.
  setFloatRtssLinked(false);
  stops = [
    subscribePolicyGameStatus(invalidate),
    watch(powerSourceMode,invalidate),
    onFrameRatesChanged(invalidate),
    onGameCustomConfigChanged(invalidate),
    onPerformanceScheduleChanged((config) => {
      const next = scheduleSignature(config);
      if (next === observedSchedule) return; // our own idempotent save is not a new policy.
      observedSchedule = next; invalidate();
    }),
    registerScheduledTask('game-policy-runtime-retry', 2000, reconcile, { pauseWhenHidden: false }),
  ];
}
export function stopGamePolicyRuntime(): void {
  active = false; epoch += 1;
  for (const stop of stops) stop(); stops = [];
}
