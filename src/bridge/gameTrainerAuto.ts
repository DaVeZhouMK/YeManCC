import { detectedGameName, type DetectedGame } from './gamedetect';
import { subscribePolicyGameStatus } from './gamePolicyTarget';
import { canAutoPrepareTrainer, cancelAutomaticTrainerPreparations, getGameTrainerState, getReadyGameTrainer, prepareGameTrainer, trainerTaskKey, refreshGameTrainerData, subscribeGameTrainerState } from './gameTrainer';
import { startGameTrainerRecovery } from './gameTrainerRecovery';
import { getSettingsGeneration } from './settingsRepository';
import { getUiSetting, loadUiSettings } from './uiSettings';

// One subscription in the main renderer; it remains active while YMCC is hidden.
// Automatic work prepares files only. Launching belongs exclusively to the click handler.
export function startGameTrainerAutoPrepare(): () => void {
  let stopped = false;
  let initialized = false;
  let game: DetectedGame | null = null;
  let gameEpoch = 0;
  let sourceGeneration = getSettingsGeneration();
  let timer: ReturnType<typeof setTimeout> | null = null;
  let catalogTimer: ReturnType<typeof setTimeout> | null = null;
  const scheduleCatalog = (delay = 30000) => {
    if (catalogTimer !== null) clearTimeout(catalogTimer);
    catalogTimer = null;
    if (stopped || !initialized || !getUiSetting('trainerAutoPrepare')) return;
    catalogTimer = setTimeout(() => {
      catalogTimer = null;
      if (stopped || !getUiSetting('trainerAutoPrepare')) return;
      const generation = getSettingsGeneration();
      void refreshGameTrainerData().then(result => {
        if (generation !== getSettingsGeneration()) return;
        if (result.catalog?.updated && game && getGameTrainerState(detectedGameName(game), game.path)?.state === 'unavailable') {
          const instance = trainerTaskKey(detectedGameName(game), game.path) + '|' + game.pid + '|' + game.processCreated;
          retryAt.delete(instance); transientFailures.delete(instance); schedule();
        }
        scheduleCatalog(Math.max(60000, (result.retryAfter || Date.now() + 86400000) - Date.now()));
      })
        .catch(() => { if (generation === getSettingsGeneration()) scheduleCatalog(21600000); });
    }, delay);
  };
  const retryAt = new Map<string, number>();
  const transientFailures = new Map<string, number>();
  const retryTransient = (instance: string, retryAfter = Date.now() + 30000) => {
    const failures = (transientFailures.get(instance) || 0) + 1;
    transientFailures.set(instance, failures);
    // Initial attempt plus at most three quick retries per game instance. Download quotas remain backend-owned.
    retryAt.set(instance, failures <= 3 ? Math.max(Date.now() + 5000, Math.min(Date.now() + 60000, Number.isFinite(retryAfter) ? retryAfter : Date.now() + 30000)) : Date.now() + 3600000);
  };
  const schedule = () => {
    if (timer !== null) clearTimeout(timer);
    timer = null;
    if (stopped || !initialized) return;
    const generation = getSettingsGeneration();
    if (sourceGeneration !== generation) { sourceGeneration = generation; retryAt.clear(); transientFailures.clear(); }
    if (!getUiSetting('trainerAutoPrepare')) { cancelAutomaticTrainerPreparations(); return; }
    const target = game;
    const targetEpoch = gameEpoch;
    const name = detectedGameName(target);
    if (!target || !canAutoPrepareTrainer(name, target.path)) return;
    const key = trainerTaskKey(name, target.path);
    const instance = key + '|' + target.pid + '|' + target.processCreated;
    const delay = Math.max(2500, (retryAt.get(instance) || 0) - Date.now());
    timer = setTimeout(() => {
      timer = null;
      if (stopped || targetEpoch !== gameEpoch) return;
      if (generation !== getSettingsGeneration()) { schedule(); scheduleCatalog(); return; }
      if (!getUiSetting('trainerAutoPrepare') || game?.pid !== target.pid || game.processCreated !== target.processCreated || trainerTaskKey(detectedGameName(game), game.path) !== key) return;
      if (retryAt.size >= 128 && !retryAt.has(instance)) { const oldest = retryAt.keys().next().value!; retryAt.delete(oldest); transientFailures.delete(oldest); }
      retryAt.set(instance, Date.now() + 3600000);
      if (getReadyGameTrainer(name, target.path, target)) { retryAt.set(instance, Date.now() + 86400000); schedule(); return; }
      void prepareGameTrainer(name, { automatic: true, processPath: target.path, processTarget: { pid: target.pid, processCreated: target.processCreated } }).then((result) => {
        if (stopped || targetEpoch !== gameEpoch || generation !== getSettingsGeneration()) return;
        if (result.state === 'failed' && result.retryable === true) retryTransient(instance, result.retryAfter);
        else { transientFailures.delete(instance); retryAt.set(instance, result.state === 'ready' ? Date.now() + 86400000 : Math.max(Date.now() + 3600000, result.retryAfter || 0)); }
      }).catch(() => { if (!stopped && targetEpoch === gameEpoch && generation === getSettingsGeneration()) retryTransient(instance); }).finally(() => { if (!stopped && targetEpoch === gameEpoch && generation === getSettingsGeneration()) schedule(); });
    }, delay);
  };
  const stopRecovery = startGameTrainerRecovery();
  const stopGame = subscribePolicyGameStatus((next) => {
    const changed = !!game !== !!next || game && next && (game.pid !== next.pid || game.processCreated !== next.processCreated || game.identityGeneration !== next.identityGeneration || trainerTaskKey(detectedGameName(game), game.path) !== trainerTaskKey(detectedGameName(next), next.path));
    if (changed) { gameEpoch++; cancelAutomaticTrainerPreparations(); }
    game = next; schedule();
  });
  const onPreferenceChange = () => { schedule(); scheduleCatalog(); };
  const stopTrainerState = subscribeGameTrainerState(() => {
    if (sourceGeneration !== getSettingsGeneration()) onPreferenceChange();
  });
  window.addEventListener('ui-settings:changed', onPreferenceChange);
  void loadUiSettings().then(() => { initialized = true; schedule(); scheduleCatalog(); }).catch(() => { /* Unreadable preferences do not start automatic network work. */ });
  return () => {
    stopped = true;
    if (timer !== null) clearTimeout(timer);
    if (catalogTimer !== null) clearTimeout(catalogTimer);
    stopGame();
    stopRecovery();
    stopTrainerState();
    window.removeEventListener('ui-settings:changed', onPreferenceChange);
    cancelAutomaticTrainerPreparations();
  };
}