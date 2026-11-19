// Read-only consumption of the existing main Steam installation producer.
// Only concurrent callers share a request. Settled facts are never cached.
export type SteamInstallationQuery = (command: 'steamInformation.get', args: { mask: 1 }, options: { timeoutMs: 5000 }) => Promise<unknown>;
export function createSteamInstallationReader(invoke: SteamInstallationQuery): () => Promise<readonly string[] | null> {
  let flight: Promise<readonly string[] | null> | null = null;
  const parse = (value: unknown): readonly string[] | null => {
    if (!value || typeof value !== 'object' || Array.isArray(value)) return null;
    const facts = value as { available?: unknown; version?: unknown; requested?: unknown; installations?: unknown };
    if (facts.available !== true || facts.version !== 1 || facts.requested !== 1 ||
        !Array.isArray(facts.installations) || facts.installations.length > 7) return null;
    const roots: string[] = [], seen = new Set<string>();
    for (const item of facts.installations) {
      if (!item || typeof item !== 'object' || Array.isArray(item)) continue;
      const row = item as { path?: unknown; error?: unknown };
      if (row.error !== 0 || typeof row.path !== 'string') continue;
      const root = row.path.trim().replace(/\//g, '\\');
      if (!root || root.length > 32768 || /[\x00-\x1f]/.test(root) ||
          !(/^[a-z]:\\/i.test(root) || /^\\\\[^\\]+\\[^\\]+/.test(root))) continue;
      const key = root.replace(/\\+$/, '').toLowerCase();
      if (!seen.has(key)) { seen.add(key); roots.push(root); }
    }
    return Object.freeze(roots);
  };
  return () => {
    if (flight) return flight;
    const task = Promise.resolve().then(() => invoke('steamInformation.get', { mask: 1 }, { timeoutMs: 5000 }))
      .then(parse).catch(() => null);
    flight = task;
    void task.then(() => { if (flight === task) flight = null; });
    return task;
  };
}
