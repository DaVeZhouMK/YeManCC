import { subscribePolicyGameStatus, getPolicyGame, gamePolicyKey, isPolicyGameInitialized } from './gamePolicyTarget';
import { loadGameCustomConfig, loadPerformanceSchedule, restorePerformanceScheduleIfConfigured,
  resolveGameCustomProfiles, onGameCustomConfigChanged, onPerformanceScheduleChanged } from './performanceSchedule';
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
let observedSchedule = '';
let stops: (() => void)[] = [];
function scheduleSignature(config: Awaited<ReturnType<typeof loadPerformanceSchedule>>): string {
  return JSON.stringify([config.configured, config.enabled, config.active, config.profiles]);
}
async function reconcile(): Promise<void> {
  if (!active || running || !dirty || !isPolicyGameInitialized() || isQuickActionBusy()) return;
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
    const identity = game ? game.pid + ':' + game.processCreated : '';
    // Input-only/display-name edits must not replay CPU/TDP/FPS or process affinity.
    const performanceSignature = JSON.stringify([identity, schedule.configured, schedule.enabled,
      enabledEntry ? resolveGameCustomProfiles(enabledEntry, schedule) : [schedule.active, schedule.profiles]]);
    const coreSignature = JSON.stringify([identity, enabledEntry?.corePolicyEnabled,
      enabledEntry?.corePolicyMode, enabledEntry?.hyperThreadPolicyEnabled, enabledEntry?.hyperThreadPolicy]);
    if (!active || generation !== epoch) return;
    if (performanceSignature !== appliedPerformance) {
      // A game-exclusive profile is an independent owner. It must be restored
      // even when the global performance scheduler is currently in manual mode;
      // otherwise an AC/DC event or a resident retry silently skips the game's
      // dedicated profile.
      if (enabledEntry || (schedule.configured && schedule.enabled)) {
        await restorePerformanceScheduleIfConfigured();
      }
      if (!active || generation !== epoch) return;
      appliedPerformance = performanceSignature;
    }
    if (coreSignature !== appliedCore) {
      const capabilities = await detectGameCorePolicy();
      if (!capabilities) return; // failed probe is not permission to drop a lease.
      const mode = enabledEntry && capabilities.heterogeneous && enabledEntry.corePolicyEnabled !== false
        ? enabledEntry.corePolicyMode || 'default' : 'default';
      const hyper = enabledEntry && capabilities.smtAvailable && enabledEntry.hyperThreadPolicyEnabled !== false
        ? enabledEntry.hyperThreadPolicy || 'default' : 'default';
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
  stops = [
    subscribePolicyGameStatus(invalidate),
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
