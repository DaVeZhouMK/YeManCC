import { getPolicyGame, subscribePolicyGameStatus } from './gamePolicyTarget';
import { detectedGameName, isGameStatusCurrent, subscribeGameTargetReadiness, type DetectedGame } from './gamedetect';
import { getSettingsGeneration } from './settingsRepository';
import { getSharedPowerLifecycle, isSharedPowerLifecycleAdmitted, onSharedPowerLifecycle } from './powerLifecycleState';
import { captureGameCustomProfileTarget } from './gameCustomProfileTarget';
import {
  createGameCustomProfile, loadGameCustomConfig, onGameCustomConfigChanged, restorePerformanceScheduleIfConfigured,
  type GameCustomConfig, type GameCustomProfileTarget,
} from './performanceSchedule';

export interface GameProfileAutoMemoryDeps {
  generation: () => number;
  game: () => DetectedGame | null;
  ready: () => boolean;
  admissionKey: () => string;
  load: () => Promise<GameCustomConfig>;
  subscribeConfig: (listener: (config: GameCustomConfig) => void) => () => void;
  subscribeGame: (listener: () => void) => () => void;
  subscribeReady: (listener: () => void) => () => void;
  create: (target: GameCustomProfileTarget, name: string, options: { automatic: true; admit: () => boolean }) => Promise<boolean>;
  restore: () => Promise<unknown>;
}

/** One event consumer, no poller. Duplicate observations never create or rewrite a profile. */
export function createGameProfileAutoMemory(deps: GameProfileAutoMemoryDeps) {
  type Candidate = { target: GameCustomProfileTarget; name: string; key: string; admission: string; lifetime: number; optInEpoch: number };
  let active = false, lifetime = 0, enabled = false, configGeneration = -1;
  let observed = '', handled = '', loading: Promise<void> | null = null;
  let pending: Candidate | null = null, inFlight: Candidate | null = null;
  let worker: Promise<void> | null = null, lastError = '', configRevision = 0, optInEpoch = 0;
  let stops: Array<() => void> = [];
  const keyFor = (game: DetectedGame | null) => game && game.pid > 0 && game.processCreated && game.path
    ? JSON.stringify(captureGameCustomProfileTarget(game, deps.generation())) : '';
  const admitted = (candidate: Candidate) => active && lifetime === candidate.lifetime && enabled && candidate.optInEpoch === optInEpoch &&
    deps.ready() && candidate.admission === deps.admissionKey() &&
    configGeneration === candidate.target.generation && deps.generation() === candidate.target.generation &&
    keyFor(deps.game()) === candidate.key;

  async function drain() {
    while (active && pending) {
      const candidate = pending; pending = null; inFlight = candidate;
      try {
        if (!admitted(candidate)) continue;
        // A queued candidate can lose public admission before this worker runs.
        handled = candidate.key;
        const created = await deps.create(candidate.target, candidate.name, { automatic: true, admit: () => admitted(candidate) });
        if (created && admitted(candidate)) await deps.restore();
        if (admitted(candidate)) lastError = '';
      } catch (error) {
        if (admitted(candidate)) lastError = String((error as Error).message || error);
      } finally { if (inFlight === candidate) inFlight = null; }
    }
  }
  function launch() {
    if (worker) return;
    worker = Promise.resolve().then(drain).finally(() => { worker = null; if (active && pending) launch(); });
  }
  function observe() {
    if (!active) return;
    if (configGeneration !== deps.generation()) { void reload(); return; }
    const game = deps.game(), key = keyFor(game);
    if (key !== observed) { observed = key; handled = ''; pending = null; }
    if (inFlight && !admitted(inFlight)) handled = '';
    if (pending && !admitted(pending)) pending = null;
    if (!enabled || !game || !key || !deps.ready() || key === handled || pending) return;
    pending = { target: captureGameCustomProfileTarget(game, deps.generation()),
      name: detectedGameName(game) || game.name, key, admission: deps.admissionKey(), lifetime, optInEpoch };
    launch();
  }
  function accept(config: GameCustomConfig) {
    ++configRevision;
    const next = config.autoMemory === true;
    if (next !== enabled) { ++optInEpoch; enabled = next; handled = ''; pending = null; }
    configGeneration = deps.generation();
    // Config saves alone do not re-create a deleted profile in the same game.
    observe();
  }
  function reload(): Promise<void> {
    if (loading) return loading;
    const owner = lifetime, generation = deps.generation(), revision = configRevision;
    const read = Promise.resolve().then(deps.load).then(config => {
      if (active && lifetime === owner && deps.generation() === generation && revision === configRevision) accept(config);
    }).catch(error => {
      if (active && lifetime === owner) lastError = String((error as Error).message || error);
    }).finally(() => {
      if (loading === read) loading = null;
      if (active && lifetime === owner && deps.generation() !== generation) void reload();
    });
    loading = read;
    return read;
  }
  return {
    start() {
      if (active) return;
      active = true; ++lifetime; configGeneration = -1; enabled = false; observed = ''; handled = '';
      stops = [deps.subscribeConfig(accept), deps.subscribeGame(observe), deps.subscribeReady(observe)];
      void reload();
    },
    stop() {
      active = false; ++lifetime; pending = null; loading = null;
      for (const stop of stops) stop();
      stops = [];
    },
    diagnostics: () => ({ active, enabled, pending: !!pending, busy: !!inFlight, lastError }),
  };
}

const autoMemory = createGameProfileAutoMemory({
  generation: getSettingsGeneration, game: getPolicyGame,
  ready: () => isGameStatusCurrent() && isSharedPowerLifecycleAdmitted(getSharedPowerLifecycle()),
  admissionKey: () => {
    const power = getSharedPowerLifecycle();
    return JSON.stringify([power?.generation, power?.observedNotificationSequence, power?.suspendSerial]);
  },
  load: () => loadGameCustomConfig('ensure', { requireReady: true }),
  subscribeConfig: onGameCustomConfigChanged,
  subscribeGame: listener => subscribePolicyGameStatus(listener),
  subscribeReady: listener => {
    const stops = [subscribeGameTargetReadiness(listener),
      onSharedPowerLifecycle(listener, { observeNotificationSequence: true, observeSuspendSerial: true })];
    return () => { for (const stop of stops) stop(); };
  },
  create: createGameCustomProfile,
  restore: restorePerformanceScheduleIfConfigured,
});
export const startGameProfileAutoMemory = () => autoMemory.start();
export const stopGameProfileAutoMemory = () => autoMemory.stop();
export const getGameProfileAutoMemoryDiagnostics = () => autoMemory.diagnostics();
