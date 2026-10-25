// Consumer-only shape/layout helpers. No sensor APIs, IO or refresh timers.
export const MAX_CORE_USAGE_ITEMS = 128; // Unified packet bound; not the visual count.
export const MAX_VISIBLE_CORE_TILES = 16;
const normalizedCoreLabels = new WeakSet<object>();
// Only plain arrays copied, validated and frozen by this module are trusted.
// An external Object.freeze (including getters/proxies) cannot set this mark.
const ownedCoreUsage = new WeakSet<object>();
// Only a plain, module-created frozen label array earns the ownership mark.
export function freezeCoreUsageLabels(input: unknown, count: number): string[] {
  if (!Array.isArray(input)) return Object.freeze([]) as unknown as string[];
  const limit = Math.max(0, Math.min(input.length, MAX_CORE_USAGE_ITEMS, Number.isFinite(count) ? Math.floor(count) : 0));
  if (normalizedCoreLabels.has(input) && input.length <= limit) return input as string[];
  const labels = new Array<string>(limit);
  for (let i=0;i<limit;++i) { const value=input[i]; labels[i]=typeof value==='string'?value.trim().slice(0,24):''; }
  Object.freeze(labels); normalizedCoreLabels.add(labels); return labels;
}
export function markCoreUsageLabels(input: string[]): string[] { return freezeCoreUsageLabels(input, input.length); }
export function coreUsageValues(input: unknown): Array<number | null> {
  if (!Array.isArray(input)) return [];
  if (ownedCoreUsage.has(input)) return input as Array<number | null>;
  const count = Math.min(input.length, MAX_CORE_USAGE_ITEMS);
  // Native/parser payloads freeze already bounded arrays. Reuse them through
  // the read-only consumer path; mutable fixtures and untrusted arrays retain
  // the original validating copy behavior.
  if (Object.isFrozen(input) && input.length <= MAX_CORE_USAGE_ITEMS) {
    let valid = true;
    for (let i=0;i<count;++i) { const value=input[i]; if (!(typeof value === 'number' && Number.isFinite(value) && value >= 0 && value <= 100) && value !== null) { valid = false; break; } }
    if (valid) return input as Array<number | null>;
  }
  const result = new Array<number | null>(count);
  for (let i=0;i<result.length;++i) { const value=input[i];
    result[i] = typeof value === 'number' && Number.isFinite(value) && value >= 0 && value <= 100 ? value : null;
  }
  return result;
}
// Called only at the existing uniform JSON admission. This returns a private
// immutable result without tagging or freezing the caller's input array.
export function freezeCoreUsageValues(input: unknown): Array<number | null> {
  if (Array.isArray(input) && ownedCoreUsage.has(input)) return input as Array<number | null>;
  const count = Array.isArray(input) ? Math.min(input.length, MAX_CORE_USAGE_ITEMS) : 0;
  const values = new Array<number | null>(count);
  for (let i=0;i<count;++i) { const value=input[i];
    values[i] = typeof value === 'number' && Number.isFinite(value) && value >= 0 && value <= 100 ? value : null;
  }
  Object.freeze(values); ownedCoreUsage.add(values);
  return values;
}
// A missing/stale publisher array is a neutral empty rail, not invented loads.
// Keep the compact strip visible without any visible placeholder text.
let lastTileInput: readonly (number | null)[] | null = null;
let lastTileStale = false;
let lastTileValues: Array<number | null> = [];
export function coreTileUsage(input: unknown, stale = false): Array<number | null> {
  const count = Array.isArray(input) ? Math.min(input.length, MAX_VISIBLE_CORE_TILES) : 0;
  // Only the owned normalization proves stable values; external frozen getters
  // remain on the read/validate path. Cache results themselves are immutable.
  const frozen = Array.isArray(input) && ownedCoreUsage.has(input);
  if (frozen && input === lastTileInput && stale === lastTileStale) return lastTileValues;
  if (!count || stale) {
    const values = Array<number | null>(count || MAX_VISIBLE_CORE_TILES).fill(null);
    if (frozen) { Object.freeze(values); lastTileInput = input as readonly (number | null)[]; lastTileStale = stale; lastTileValues = values; }
    return values;
  }
  const values = new Array<number | null>(count);
  for (let i=0;i<count;++i) { const value=input[i];
    values[i] = typeof value === 'number' && Number.isFinite(value) && value >= 0 && value <= 100 ? value : null;
  }
  if (frozen) { Object.freeze(values); lastTileInput = input as readonly (number | null)[]; lastTileStale = stale; lastTileValues = values; }
  return values;
}
export function coreUsageLabels(input: unknown, count: number): string[] {
  if (!Array.isArray(input)) return [];
  const limit = Math.min(count, MAX_CORE_USAGE_ITEMS);
  // Parser labels are already trimmed/bounded and frozen alongside coreUsage.
  // Reuse only that immutable form; mutable or untrusted labels are copied and
  // normalized exactly as before.
  if (normalizedCoreLabels.has(input as object)) return input.length === limit ? input as string[] : input.slice(0,limit) as string[];
  if (Object.isFrozen(input) && input.length <= MAX_CORE_USAGE_ITEMS) {
    let valid = true;
    for (let i=0;i<Math.min(input.length,limit);++i) { const value=input[i]; if (typeof value !== 'string' || value !== value.trim().slice(0,24)) { valid=false; break; } }
    if (valid) return input.length === limit ? input as string[] : input.slice(0,limit) as string[];
  }
  return Array.from(input.slice(0, limit), value => typeof value === 'string' ? value.trim().slice(0, 24) : '');
}
export function coreTileLayout(count: number) {
  const total = Math.max(0, Math.min(MAX_VISIBLE_CORE_TILES, Number.isFinite(count) ? Math.floor(count) : 0));
  return { total, columns: total, rows: total ? 1 : 0 };
}
