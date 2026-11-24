import { getCurrentGame, isCurrentGameTarget, subscribeGameTargetReadiness, type GameTargetIdentity } from './gamedetect';
import { invoke, on } from './ipc';

import {target,same,unknown,decode,type GameSteamTarget,type GameSteamContext} from './gameSteamContextModel';
export type {GameSteamTarget,GameSteamGame,GameSteamObservation,GameSteamContext,CachedSteamAssociation,GameSteamContextResponse} from './gameSteamContextModel';

export interface GameSteamContextReaderDeps {
  getGame(): GameTargetIdentity | null;
  isGameCurrent(game: GameTargetIdentity): boolean;
  invoke<T>(command: string, args: { expectedGame: GameSteamTarget }, options?: { signal?: AbortSignal }): Promise<T>;
  subscribeInvalidation?(callback: () => void): () => void;
}
export interface GameSteamContextReader {
  read(options?: { signal?: AbortSignal }): Promise<GameSteamContext>;
  /** One explicit consumer demand; the native Steam owner allocates/coalesces work. */
  refresh(options?: { signal?: AbortSignal }): Promise<GameSteamContext>;
  /** Client epoch edge only; no source subscription, query or refresh. */
  subscribeInvalidation(callback: () => void): () => void;
  invalidate(): void;
  dispose(): void;
}

/** Only pending IPC is coalesced here. Facts and demand tokens stay in TrackerOwner. */
export function createGameSteamContextReader(deps: GameSteamContextReaderDeps): GameSteamContextReader {
  let epoch = 0n, disposed = false, subscriptionFailed = false;
  let unsubscribe: (() => void) | undefined;
  type Pending = { promise: Promise<GameSteamContext>; cancellation: AbortController; subscribers: Set<(value: GameSteamContext) => void>; settled: boolean };
  const pending = new Map<string, Pending>();
  const cancellations = new Set<AbortController>();
  const invalidationListeners = new Set<() => void>();
  const publishInvalidation = () => {
    for (const listener of [...invalidationListeners]) { try { listener(); } catch { /* Notify remaining consumers. */ } }
  };
  const retirePending = () => {
    for (const cancellation of cancellations) cancellation.abort();
    cancellations.clear(); pending.clear();
  };
  // Revoke pending work only; never schedule a read, refresh, or warm from an event.
  const invalidate = () => { if (!disposed) { epoch++; retirePending(); publishInvalidation(); } };
  const current = (expected: GameSteamTarget): boolean => {
    try {
      const now = target(deps.getGame());
      return !!now && same(now, expected) && deps.isGameCurrent({ ...expected });
    } catch { return false; }
  };
  try { unsubscribe = deps.subscribeInvalidation?.(invalidate); }
  catch { subscriptionFailed = true; invalidate(); }
  const request = (command: 'gameSteam.context.get' | 'gameSteam.context.refresh', options?: { signal?: AbortSignal }): Promise<GameSteamContext> => {
      let expected: GameSteamTarget | null;
      try { expected = target(deps.getGame()); }
      catch { return Promise.resolve(unknown(null, 'game-read-failed')); }
      if (!expected || !current(expected)) return Promise.resolve(unknown(null, 'game-context-unknown'));
      const captured = expected;
      const game = { ...captured, processValidated: false as const };
      if (options?.signal?.aborted) return Promise.resolve(unknown(game, 'consumer-cancelled'));
      if (disposed || subscriptionFailed) return Promise.resolve(unknown(game, disposed ? 'reader-disposed' : 'invalidation-subscription-failed'));
      const admittedEpoch = epoch;
      const key = JSON.stringify([command, captured.pid, captured.processCreated, captured.path, captured.identityGeneration]);
      const join = (entry: Pending): Promise<GameSteamContext> => {
        return new Promise(resolve => {
          let finished = false;
          const finish = (value: GameSteamContext) => {
            if (finished) return;
            finished = true; options?.signal?.removeEventListener('abort', abort);
            entry.subscribers.delete(finish);
            if (!entry.subscribers.size && !entry.settled && pending.get(key) === entry) {
              pending.delete(key); cancellations.delete(entry.cancellation); entry.cancellation.abort();
            }
            resolve(value);
          };
          const abort = () => finish(unknown(current(captured) ? game : null, 'consumer-cancelled'));
          entry.subscribers.add(finish);
          options?.signal?.addEventListener('abort', abort, { once: true });
          if (options?.signal?.aborted) abort();
        });
      };
      const existing = pending.get(key);
      if (existing) return join(existing);
      const cancellation = new AbortController();
      cancellations.add(cancellation);
      // Schedule after insertion so even reentrant callers join this pending request.
      const entry: Pending = { promise: undefined!, cancellation, subscribers: new Set(), settled: false };
      const request: Promise<GameSteamContext> = Promise.resolve().then(async (): Promise<GameSteamContext> => {
        let result: GameSteamContext;
        if (disposed || cancellation.signal.aborted || epoch !== admittedEpoch || !current(captured)) return unknown(current(captured) ? game : null, 'request-invalidated');
        try {
          result = decode(await deps.invoke<unknown>(command, { expectedGame: { ...captured } }, { signal: cancellation.signal }), captured);
        } catch { result = unknown(game, 'context-query-failed'); }
        const stillCurrent = current(captured);
        if (!stillCurrent) return unknown(null, 'game-context-changed');
        if (disposed || epoch !== admittedEpoch) return unknown(game, disposed ? 'reader-disposed' : 'request-invalidated');
        return result;
      }).finally(() => { entry.settled = true; cancellations.delete(cancellation); if (pending.get(key) === entry) pending.delete(key); });
      entry.promise = request; pending.set(key, entry);
      // One completion handler per IPC. Cancelled consumers leave this Set
      // immediately instead of accumulating closures on a lost IPC promise.
      const complete = (value: GameSteamContext) => { for (const finish of [...entry.subscribers]) finish(value); };
      void request.then(complete, () => complete(unknown(game, 'context-query-failed')));
      return join(entry);
  };
  return {
    read: options => request('gameSteam.context.get', options),
    refresh: options => request('gameSteam.context.refresh', options),
    subscribeInvalidation(callback) {
      if (disposed) return () => {};
      invalidationListeners.add(callback);
      return () => { invalidationListeners.delete(callback); };
    },
    invalidate,
    dispose() {
      if (disposed) return;
      disposed = true; epoch++; retirePending();
      publishInvalidation(); invalidationListeners.clear();
      try { unsubscribe?.(); } catch { /* Late work remains revoked even if unsubscription fails. */ }
      unsubscribe = undefined;
    },
  };
}

function subscribeDefaultInvalidation(callback: () => void): () => void {
  // Suppress only complete positive duplicate transport publications. Unknown,
  // legacy and completion notifications remain revocation edges.
  let sourceSignature = '';
  const invalidate = () => { sourceSignature = ''; callback(); };
  const sourceChanged = (raw: unknown) => {
    let signature = '';
    try {
      const source = raw as Record<string, unknown> | null;
      const keys = ['pid','processCreated','processEpoch','accountEpoch','sourceAvailable','presence','windowsSessionKnown','windowsSession'];
      const u64 = (value: unknown): value is string => typeof value === 'string' && /^[1-9][0-9]{0,19}$/.test(value) && BigInt(value) <= 18446744073709551615n;
      if (source && typeof source === 'object' && !Array.isArray(source) && Object.keys(source).length === keys.length && keys.every(key => Object.prototype.hasOwnProperty.call(source, key)) &&
          source.sourceAvailable === true && source.presence === 1 && source.windowsSessionKnown === true &&
          Number.isSafeInteger(source.pid) && Number(source.pid) > 0 && Number(source.pid) <= 0xffffffff &&
          u64(source.processCreated) && u64(source.processEpoch) && u64(source.accountEpoch) &&
          Number.isSafeInteger(source.windowsSession) && Number(source.windowsSession) >= 0 && Number(source.windowsSession) <= 0xffffffff)
        signature = JSON.stringify(keys.map(key => source[key]));
    } catch { /* Unrecognized publications revoke instead of retaining facts. */ }
    if (signature && signature === sourceSignature) return;
    sourceSignature = signature; callback();
  };
  const stops: (() => void)[] = [];
  const stop = () => { for (const dispose of stops.splice(0).reverse()) { try { dispose(); } catch { /* Release the remaining subscriptions. */ } } };
  try {
    // Game readiness already fences the accepted power generation and sequence.
    stops.push(subscribeGameTargetReadiness(invalidate));
    if (typeof window !== 'undefined') for (const event of [
      'game.target.changed', 'game.rules.changed', 'steam.uiStateChanged',
      'webview.recovering',
    ]) stops.push(on(event, invalidate));
    if (typeof window !== 'undefined') {
      // A new association retires the pending query, not the verified Steam
      // identity used to deduplicate a later repeat of the same source event.
      stops.push(on('gameSteam.context.changed', callback));
      stops.push(on('gameSteam.context.sourceChanged', sourceChanged));
    }
    return stop;
  } catch (error) { stop(); throw error; }
}

export const gameSteamContext = createGameSteamContextReader({
  getGame: getCurrentGame, isGameCurrent: isCurrentGameTarget, invoke,
  subscribeInvalidation: subscribeDefaultInvalidation,
});
export default gameSteamContext;
