import { detectedGameName, type DetectedGame } from './gamedetect';
import { getPolicyGame, subscribePolicyGameStatus } from './gamePolicyTarget';
import { canAutoPrepareTrainer, getGameTrainerState, getReadyGameTrainer, restoreReadyGameTrainer, subscribeGameTrainerState, trainerTaskKey } from './gameTrainer';
import { getSettingsGeneration } from './settingsRepository';

const RECOVERY_POLL_MS = 5000;
let leases = 0;
let stopShared: (() => void) | null = null;
let refreshShared: (() => void) | null = null;
function identity(game: DetectedGame | null): string {
  return game ? JSON.stringify([trainerTaskKey(detectedGameName(game), game.path), game.pid, game.processCreated, game.identityGeneration ?? 0]) : '';
}
// Decky requests a one-off consistency check from the resident owner. It owns no timer.
export function refreshGameTrainerRecovery(): void { refreshShared?.(); }
// The existing preparation owner pushes state changes. Only an unresolved result
// needs local receipt polling, and confirmed readiness stops it completely.
export function startGameTrainerRecovery(): () => void {
  if (leases++ === 0) {
    let stopped = false, epoch = 0, misses = 0, recoveryKey = '';
    let timer: ReturnType<typeof setTimeout> | null = null;
    let checking: { identity: string; generation: number } | null = null;
    const recover = () => {
      if (timer !== null) clearTimeout(timer);
      timer = null;
      const target = getPolicyGame(), generation = getSettingsGeneration();
      if (stopped || !target || !Number.isSafeInteger(target.pid) || target.pid <= 0 || !/^[1-9][0-9]*$/.test(target.processCreated)
        || !canAutoPrepareTrainer(detectedGameName(target), target.path) || getReadyGameTrainer(detectedGameName(target), target.path, target)) {
        ++epoch; checking = null; misses = 0; recoveryKey = ''; return;
      }
      // Preparation already owns its request polling and will push every state.
      const preparation = getGameTrainerState(detectedGameName(target), target.path);
      if (['queued', 'searching', 'downloading'].includes(preparation?.state || '')) {
        ++epoch; checking = null; return;
      }
      const selected = identity(target), key = JSON.stringify([selected, generation]);
      if (key !== recoveryKey) { recoveryKey = key; misses = 0; }
      if (checking?.identity === selected && checking.generation === generation) return;
      const observed = ++epoch;
      checking = { identity: selected, generation };
      const isCurrent = () => !stopped && epoch === observed && generation === getSettingsGeneration()
        && identity(getPolicyGame()) === selected;
      void restoreReadyGameTrainer(detectedGameName(target), target.path, { processTarget: target, isTargetCurrent: isCurrent })
        .catch(() => null).finally(() => {
          if (stopped || epoch !== observed) return;
          checking = null;
          const current = getPolicyGame();
          const state = current && getGameTrainerState(detectedGameName(current), current.path);
          // A known terminal answer waits for an owner event or a Decky recheck.
          if (current && !getReadyGameTrainer(detectedGameName(current), current.path, current)
            && !['unavailable', 'budget-limited', 'cancelled'].includes(state?.state || '')) {
            const delay = Math.min(60000, RECOVERY_POLL_MS * 2 ** Math.min(misses++, 4));
            timer = setTimeout(recover, delay);
          }
        });
    };
    const refresh = () => { misses = 0; recover(); };
    refreshShared = refresh;
    let receivedInitial = false;
    const stopGame = subscribePolicyGameStatus(() => { receivedInitial = true; recover(); });
    const stopTrainer = subscribeGameTrainerState(recover);
    window.addEventListener('ui-settings:changed', refresh);
    window.addEventListener('ipc:power.resumed', refresh);
    if (!receivedInitial) recover();
    stopShared = () => {
      stopped = true; ++epoch; refreshShared = null;
      if (timer !== null) clearTimeout(timer);
      stopGame(); stopTrainer();
      window.removeEventListener('ui-settings:changed', refresh);
      window.removeEventListener('ipc:power.resumed', refresh);
    };
  }
  let released = false;
  return () => {
    if (released) return;
    released = true;
    if (--leases === 0) { stopShared?.(); stopShared = null; }
  };
}
