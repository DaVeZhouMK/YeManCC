// Shared, event-driven consumer of the existing top-menu game recognition.
// One in-flight library read/conversion, eight bounded derived rasters, no poller.
import type { DetectedGame } from './gamedetect';
export interface GameLogoResult { image: string; revision: string }
export function createGameLogoConsumer(deps: {
  subscribeGame(listener: (game: DetectedGame | null) => void): () => void;
  read(executable: string): Promise<GameLogoResult | null>;
  prepare(image: string): Promise<string>;
  now(): number;
}) {
  const listeners = new Set<(image: string) => void>();
  const cache = new Map<string, { image: string; at: number }>();
  let off: (() => void) | null = null, active = false, pending = false;
  let current: DetectedGame | null = null, targetKey = '', displayed = '', epoch = 0;
  const pathKey = (path: string) => path.trim().replace(/\//g, '\\').toLowerCase();
  function publish(image: string) {
    if (displayed === image) return;
    displayed = image;
    for (const listener of [...listeners]) try { listener(image); } catch { /* isolate consumers */ }
  }
  async function pump() {
    if (pending || !active || !current) return;
    const own = epoch, game = current, key = pathKey(game.path);
    pending = true;
    try {
      const cached = cache.get(key);
      if (cached && deps.now() >= cached.at && deps.now() - cached.at < 30000) { if (own === epoch && active) publish(cached.image); return; }
      const raw = await deps.read(game.path);
      if (own !== epoch || !active) return;
      const image = raw ? await deps.prepare(raw.image) : '';
      if (own !== epoch || !active) return;
      cache.delete(key); cache.set(key, { image, at: deps.now() });
      while (cache.size > 8) cache.delete(cache.keys().next().value!);
      publish(image);
    } catch { if (own === epoch && active) publish(''); }
    finally { pending = false; if (own !== epoch && active && current) void pump(); }
  }
  function receive(game: DetectedGame | null) {
    const valid = game && Number.isSafeInteger(game.pid) && game.pid > 0 && /^\d+$/.test(game.processCreated) && game.processCreated !== '0' && /^[a-z]:[\\/].+\.exe$/i.test(game.path);
    const next = valid ? [game!.pid, game!.processCreated, pathKey(game!.path)].join('|') : '';
    current = valid ? { ...game! } : null;
    if (next === targetKey) return;
    targetKey = next; ++epoch; publish('');
    if (current) void pump();
  }
  return {
    subscribe(listener: (image: string) => void) {
      listeners.add(listener); try { listener(displayed); } catch { /* isolated */ }
      if (!active) { active = true; off = deps.subscribeGame(receive); }
      let closed = false;
      return () => { if (closed) return; closed = true; listeners.delete(listener); if (!listeners.size) {
        active = false; ++epoch; off?.(); off = null; current = null; targetKey = ''; displayed = '';
      } };
    },
    invalidate() { cache.clear(); if (!active) return; ++epoch; publish(''); if (current) void pump(); },
  };
}