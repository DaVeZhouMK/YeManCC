/** Display-only minimum press visibility. It never records or dispatches an action. */
export function createPressFeedback(publish: (keys: readonly string[]) => void, duration = 180) {
  const entries = new Map<string, { held: boolean; started: number; timer?: ReturnType<typeof setTimeout> }>();
  const send = () => publish([...entries.keys()]);
  return {
    accept(keys: readonly string[]) {
      const current = new Set(keys); let changed = false;
      for (const key of current) {
        const prior = entries.get(key);
        if (prior) { if (!prior.held) prior.started = Date.now(); if (prior.timer) clearTimeout(prior.timer); prior.timer = undefined; prior.held = true; }
        else { entries.set(key, { held: true, started: Date.now() }); changed = true; }
      }
      for (const [key, entry] of entries) if (!current.has(key) && entry.held) {
        entry.held = false;
        const remaining = duration - (Date.now() - entry.started);
        if (remaining > 0) entry.timer = setTimeout(() => { entries.delete(key); send(); }, remaining);
        else { entries.delete(key); changed = true; }
      }
      if (changed) send();
    },
    reset() { for (const entry of entries.values()) if (entry.timer) clearTimeout(entry.timer); entries.clear(); send(); },
  };
}
