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
  powerReady: () => boolean;
  ready: () => boolean;
  admissionKey: () => string;
  load: () => Promise<GameCustomConfig>;
  subscribeConfig: (listener: (config: GameCustomConfig) => void) => () => void;
  subscribeGame: (listener: () => void) => () => void;
  subscribeReady: (listener: () => void) => () => void;
  create: (target: GameCustomProfileTarget, name: string, options: { automatic: true; admit: () => boolean }) => Promise<boolean>;
  restore: (admit: () => boolean) => Promise<unknown>;
}

/** One event consumer, no poller. Duplicate observations never create or rewrite a profile. */
export function createGameProfileAutoMemory(deps: GameProfileAutoMemoryDeps) {
  type Candidate = { target: GameCustomProfileTarget; name: string; key: string; admission: string; lifetime: number; optInEpoch: number; revoked?: true };
  type ConfigRead = { lifetime: number; generation: number; revision: number; powerScope: string; revoked?: true };
  type Attempt = { key: string; failedIn?: string };
  let active = false, lifetime = 0, enabled = false, configGeneration = -1;
  let observed = '', handled: Attempt | null = null, loading: ConfigRead | null = null;
  let pending: Candidate | null = null, inFlight: Candidate | null = null;
  let worker: Promise<void> | null = null, lastError = '', configRevision = 0, optInEpoch = 0;
  let stops: Array<() => void> = [];
  const keyFor = (game: DetectedGame | null) => game && game.pid > 0 && game.processCreated && game.path
    ? JSON.stringify(captureGameCustomProfileTarget(game, deps.generation())) : '';
  const admitted = (candidate: Candidate) => !candidate.revoked && active && lifetime === candidate.lifetime && enabled && candidate.optInEpoch === optInEpoch &&
    deps.ready() && candidate.admission === deps.admissionKey() &&
    configGeneration === candidate.target.generation && deps.generation() === candidate.target.generation &&
    keyFor(deps.game()) === candidate.key;
  const configReadCurrent = (read: ConfigRead) => !read.revoked && active && read.lifetime === lifetime &&
    read.generation === deps.generation() && read.revision === configRevision &&
    deps.powerReady() && read.powerScope === deps.admissionKey();

  async function drain() {
    while (active && pending) {
      const candidate = pending; pending = null; inFlight = candidate;
      const attempt: Attempt = { key: candidate.key };
      let creationSettled = false;
      try {
        if (!admitted(candidate)) continue;
        // A queued candidate can lose public admission before this worker runs.
        handled = attempt;
        const admit = () => admitted(candidate);
        const created = await deps.create(candidate.target, candidate.name, { automatic: true, admit });
        creationSettled = true;
        if (created && admit()) await deps.restore(admit);
        if (admitted(candidate)) lastError = '';
      } catch (error) {
        if (admitted(candidate)) {
          if (!creationSettled) attempt.failedIn = candidate.admission;
          lastError = String((error as Error).message || error);
        }
      } finally { if (inFlight === candidate) inFlight = null; }
    }
  }
  function launch() {
    if (worker) return;
    worker = Promise.resolve().then(drain).finally(() => { worker = null; if (active && pending) launch(); });
  }
  function observe() {
    if (!active) return;
    // Ready can reopen the same public scope. It cannot renew an old operation.
    if (loading && !configReadCurrent(loading)) loading.revoked = true;
    if (inFlight && !admitted(inFlight)) { inFlight.revoked = true; handled = null; }
    // A failure that settled before the power notification gets one attempt
    // under the next admission. The original create still checks the file.
    if (handled?.failedIn !== undefined && (!deps.powerReady() || handled.failedIn !== deps.admissionKey())) handled = null;
    if (configGeneration !== deps.generation()) { void reload(); return; }
    const game = deps.game(), key = keyFor(game);
    if (key !== observed) { observed = key; handled = null; pending = null; }
    if (pending && !admitted(pending)) pending = null;
    if (!enabled || !game || !key || !deps.ready() || key === handled?.key || pending) return;
    pending = { target: captureGameCustomProfileTarget(game, deps.generation()),
      name: detectedGameName(game) || game.name, key, admission: deps.admissionKey(), lifetime, optInEpoch };
    launch();
  }
  function accept(config: GameCustomConfig) {
    ++configRevision;
    const next = config.autoMemory === true;
    if (next !== enabled) { ++optInEpoch; enabled = next; handled = null; pending = null; }
    configGeneration = deps.generation();
    // Config saves alone do not re-create a deleted profile in the same game.
    observe();
  }
  function reload(): void {
    if (loading || !active || !deps.powerReady()) return;
    const read: ConfigRead = { lifetime, generation: deps.generation(), revision: configRevision, powerScope: deps.admissionKey() };
    loading = read;
    void Promise.resolve().then(() => configReadCurrent(read) ? deps.load() : undefined).then(config => {
      if (config && configReadCurrent(read)) { lastError = ''; accept(config); }
    }).catch(error => {
      if (configReadCurrent(read)) lastError = String((error as Error).message || error);
    }).finally(() => {
      if (loading !== read) return;
      loading = null;
      // Ensure loads can migrate settings. Wait for the original operation to
      // settle before continuing a retired read under the current Ready scope.
      if (!configReadCurrent(read) && configGeneration !== deps.generation()) reload();
    });
  }
  return {
    start() {
      if (active) return;
      active = true; ++lifetime; configGeneration = -1; enabled = false; observed = ''; handled = null;
      stops = [deps.subscribeConfig(accept), deps.subscribeGame(observe), deps.subscribeReady(observe)];
      void reload();
    },
    stop() {
      active = false; ++lifetime; pending = null;
      for (const stop of stops) stop();
      stops = [];
    },
    diagnostics: () => ({ active, enabled, pending: !!pending, busy: !!inFlight, lastError }),
  };
}

const autoMemory = createGameProfileAutoMemory({
  generation: getSettingsGeneration, game: getPolicyGame,
  powerReady: () => isSharedPowerLifecycleAdmitted(getSharedPowerLifecycle()),
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
  restore: admit => restorePerformanceScheduleIfConfigured(undefined, { ownsPowerRequest: admit }),
});
export const startGameProfileAutoMemory = () => autoMemory.start();
export const stopGameProfileAutoMemory = () => autoMemory.stop();
export const getGameProfileAutoMemoryDiagnostics = () => autoMemory.diagnostics();
