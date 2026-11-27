import { detectedGameName, isGameStatusCurrent, isCurrentGameTarget, subscribeGameTargetReadiness, type DetectedGame } from './gamedetect';
import { subscribePolicyGameStatus } from './gamePolicyTarget';
import { canAutoPrepareTrainer, cancelAutomaticTrainerPreparations, getGameTrainerState, getReadyGameTrainer, prepareGameTrainer, readGameTrainerCatalogDueAt, trainerTaskKey, refreshGameTrainerData, subscribeGameTrainerState } from './gameTrainer';
import { startGameTrainerRecovery } from './gameTrainerRecovery';
import { getSettingsGeneration } from './settingsRepository';
import { getUiSetting, loadUiSettings, uiSettingsLoaded } from './uiSettings';
import { subscribeTrainerLifecycle } from './trainerLifecycle';

const catalogCheckKey = 'yeman.trainer.catalog-next-check.v1';
const catalogStartupDelay = 30_000;
const catalogOpenGrace = 10_000;
const catalogDay = 86_400_000;
const catalogWindowEvents = ['ipc:window.shown', 'ipc:window.restored', 'ipc:window.summoned', 'ipc:window.focus'];

function catalogNextCheck(now: number): number {
  const earliest = now + catalogStartupDelay;
  try {
    const stored = Number(localStorage.getItem(catalogCheckKey));
    if (Number.isSafeInteger(stored) && stored <= now + catalogDay) return Math.max(earliest, stored);
  } catch { /* This hint is optional; the backend owns the refresh budget. */ }
  return earliest;
}

// One subscription in the main renderer; it remains active while YMCC is hidden.
// Automatic work prepares files only. Launching belongs exclusively to the click handler.
export function startGameTrainerAutoPrepare(): () => void {
  let stopped = false;
  // The lifecycle adapter also gates Ready during WebView confirmation.
  let paused = true;
  let initialized = false;
  let powerEpoch = 0, preferenceAttempt = -1, preferenceReadPending = false;
  let game: DetectedGame | null = null;
  let gameEpoch = 0;
  let catalogEpoch = 0;
  let sourceGeneration = getSettingsGeneration();
  let timer: ReturnType<typeof setTimeout> | null = null;
  let catalogTimer: ReturnType<typeof setTimeout> | null = null;
  let catalogDueAt = catalogNextCheck(Date.now());
  let restoreCatalogDueAt = true;
  let catalogOpenUntil = 0;
  const onWindowOpened = () => { catalogOpenUntil = Date.now() + catalogOpenGrace; };
  const scheduleCatalog = () => {
    if (catalogTimer !== null) clearTimeout(catalogTimer);
    catalogTimer = null;
    if (stopped || paused || !initialized || !getUiSetting('trainerAutoPrepare')) return;
    catalogTimer = setTimeout(async () => {
      catalogTimer = null;
      if (stopped || paused || !getUiSetting('trainerAutoPrepare')) return;
      if (Date.now() < Math.max(catalogDueAt, catalogOpenUntil)) { scheduleCatalog(); return; }
      const generation = getSettingsGeneration();
      const catalogObservation = catalogEpoch;
      const catalogCurrent = () => !stopped && !paused && catalogObservation === catalogEpoch &&
        getUiSetting('trainerAutoPrepare') && generation === getSettingsGeneration();
      // Browser profile recovery may discard the localStorage schedule.
      if (restoreCatalogDueAt) {
        const dueAt = await readGameTrainerCatalogDueAt();
        if (!catalogCurrent()) return;
        restoreCatalogDueAt = false;
        if (dueAt > Date.now()) { catalogDueAt = dueAt; scheduleCatalog(); return; }
        if (Date.now() < catalogOpenUntil) { scheduleCatalog(); return; }
      }
      void refreshGameTrainerData().then(result => {
        if (!catalogCurrent()) return;
        if (result.catalog?.updated && game && getGameTrainerState(detectedGameName(game), game.path)?.state === 'unavailable') {
          const instance = trainerTaskKey(detectedGameName(game), game.path) + '|' + game.pid + '|' + game.processCreated;
          retryAt.delete(instance); transientFailures.delete(instance); schedule();
        }
        const now = Date.now();
        catalogDueAt = Math.max(now + 60_000, Math.min(now + catalogDay, result.retryAfter || now + catalogDay));
        if (result.catalog) {
          try { localStorage.setItem(catalogCheckKey, String(catalogDueAt)); } catch { /* Advisory only. */ }
        }
        scheduleCatalog();
      })
        .catch(() => {
          if (stopped || catalogObservation !== catalogEpoch || generation !== getSettingsGeneration()) return;
          catalogDueAt = Date.now() + 21_600_000;
          scheduleCatalog();
        });
    }, Math.max(0, Math.max(catalogDueAt, catalogOpenUntil) - Date.now()));
  };
  const retryAt = new Map<string, number>();
  const transientFailures = new Map<string, number>();
  let preparing: { instance: string; reservation: number } | null = null;
  const retirePreparation = () => {
    // This is only the in-flight scheduling placeholder, never a completed
    // failure cooldown or the backend's durable download allowance.
    if (preparing && retryAt.get(preparing.instance) === preparing.reservation) retryAt.delete(preparing.instance);
    preparing = null;
    cancelAutomaticTrainerPreparations();
  };
  const retryTransient = (instance: string, retryAfter = Date.now() + 30000) => {
    const failures = (transientFailures.get(instance) || 0) + 1;
    transientFailures.set(instance, failures);
    // Initial attempt plus at most three quick retries per game instance. Download quotas remain backend-owned.
    retryAt.set(instance, failures <= 3 ? Math.max(Date.now() + 5000, Math.min(Date.now() + 60000, Number.isFinite(retryAfter) ? retryAfter : Date.now() + 30000)) : Date.now() + 3600000);
  };
  const schedule = () => {
    if (timer !== null) clearTimeout(timer);
    timer = null;
    if (stopped || paused || !initialized) return;
    const generation = getSettingsGeneration();
    if (sourceGeneration !== generation) {
      sourceGeneration = generation;
      retryAt.clear(); transientFailures.clear();
      catalogDueAt = Date.now() + catalogStartupDelay;
      restoreCatalogDueAt = false; // A switch of settings source is a fresh observation.
      try { localStorage.removeItem(catalogCheckKey); } catch { /* Advisory only. */ }
      scheduleCatalog();
    }
    if (!getUiSetting('trainerAutoPrepare')) { cancelAutomaticTrainerPreparations(); return; }
    const target = game;
    const targetEpoch = gameEpoch;
    const current = () => !stopped && !paused && targetEpoch === gameEpoch && generation === getSettingsGeneration() &&
      isGameStatusCurrent() && !!target && isCurrentGameTarget(target);
    const name = detectedGameName(target);
    if (!target || !isGameStatusCurrent() || !isCurrentGameTarget(target) || !canAutoPrepareTrainer(name, target.path)) return;
    const key = trainerTaskKey(name, target.path);
    const instance = key + '|' + target.pid + '|' + target.processCreated;
    const delay = Math.max(2500, (retryAt.get(instance) || 0) - Date.now());
    timer = setTimeout(() => {
      timer = null;
      if (stopped || paused || targetEpoch !== gameEpoch) return;
      if (generation !== getSettingsGeneration()) { schedule(); scheduleCatalog(); return; }
      if (!current()) return;
      if (!getUiSetting('trainerAutoPrepare') || game?.pid !== target.pid || game.processCreated !== target.processCreated || trainerTaskKey(detectedGameName(game), game.path) !== key) return;
      if (retryAt.size >= 128 && !retryAt.has(instance)) { const oldest = retryAt.keys().next().value!; retryAt.delete(oldest); transientFailures.delete(oldest); }
      retryAt.set(instance, Date.now() + 3600000);
      if (getReadyGameTrainer(name, target.path, target)) { retryAt.set(instance, Date.now() + 86400000); schedule(); return; }
      const attempt = { instance, reservation: retryAt.get(instance)! };
      preparing = attempt;
      void prepareGameTrainer(name, { automatic: true, processPath: target.path, processTarget: { pid: target.pid, processCreated: target.processCreated } }).then((result) => {
        if (!current()) return;
        if (result.state === 'failed' && result.retryable === true) retryTransient(instance, result.retryAfter);
        else { transientFailures.delete(instance); retryAt.set(instance, result.state === 'ready' ? Date.now() + 86400000 : Math.max(Date.now() + 3600000, result.retryAfter || 0)); }
      }).catch(() => { if (current()) retryTransient(instance); }).finally(() => { if (preparing === attempt) preparing = null; if (current()) schedule(); });
    }, delay);
  };
  const initializePreferences = () => {
    if (stopped || paused || initialized) return;
    if (uiSettingsLoaded()) { initialized = true; return; }
    if (preferenceReadPending || preferenceAttempt === powerEpoch) return;
    preferenceAttempt = powerEpoch;
    preferenceReadPending = true;
    void loadUiSettings().then(() => {
      if (stopped) return;
      initialized = true;
      schedule(); scheduleCatalog();
    }).catch(() => {
      // Until the preferences are known, leave automatic work disarmed.
    }).finally(() => {
      preferenceReadPending = false;
      if (initialized || preferenceAttempt === powerEpoch || stopped || paused) return;
      // A confirmed Ready arrived while the old read was still settling.
      initializePreferences();
      if (initialized) { schedule(); scheduleCatalog(); }
    });
  };
  const stopRecovery = startGameTrainerRecovery();
  const stopLifecycle = subscribeTrainerLifecycle(() => {
    paused = true;
    ++powerEpoch;
    ++gameEpoch;
    ++catalogEpoch;
    if (timer !== null) clearTimeout(timer);
    if (catalogTimer !== null) clearTimeout(catalogTimer);
    timer = catalogTimer = null;
    retirePreparation();
  }, () => { if (paused) { paused = false; initializePreferences(); schedule(); scheduleCatalog(); } });
  const stopGame = subscribePolicyGameStatus((next) => {
    const changed = !!game !== !!next || game && next && (game.pid !== next.pid || game.processCreated !== next.processCreated || game.identityGeneration !== next.identityGeneration || trainerTaskKey(detectedGameName(game), game.path) !== trainerTaskKey(detectedGameName(next), next.path));
    if (changed) { gameEpoch++; retirePreparation(); }
    game = next; schedule();
  });
  const stopReadiness = subscribeGameTargetReadiness(() => {
    ++gameEpoch;
    // Unknown is not exit: keep the display, catalog and successful receipts.
    // Cancel only automatic work; its old completion cannot restore authority.
    if (!isGameStatusCurrent()) retirePreparation();
    schedule();
  });
  const onPreferenceChange = () => { ++catalogEpoch; schedule(); scheduleCatalog(); };
  const stopTrainerState = subscribeGameTrainerState(() => {
    if (sourceGeneration !== getSettingsGeneration()) onPreferenceChange();
  });
  window.addEventListener('ui-settings:changed', onPreferenceChange);
  for (const event of catalogWindowEvents) window.addEventListener(event, onWindowOpened);
  return () => {
    stopped = true;
    if (timer !== null) clearTimeout(timer);
    if (catalogTimer !== null) clearTimeout(catalogTimer);
    stopGame();
    stopReadiness();
    stopRecovery();
    stopLifecycle();
    stopTrainerState();
    window.removeEventListener('ui-settings:changed', onPreferenceChange);
    for (const event of catalogWindowEvents) window.removeEventListener(event, onWindowOpened);
    cancelAutomaticTrainerPreparations();
  };
}
