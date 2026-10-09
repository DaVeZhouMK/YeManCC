import { toRaw } from 'vue';

/**
 * Own a detached settings snapshot, including Vue proxies nested in otherwise
 * plain records/arrays. toRaw on the root alone does not unwrap nested proxies.
 * Keep structuredClone semantics (undefined, sparse arrays, shared references)
 * instead of a lossy JSON round-trip. Persistence still rejects cyclic JSON.
 */
export function snapshotSettingsData<T>(value: T): T {
  const seen = new WeakMap<object, object>();
  function unwrap(input: unknown): unknown {
    if (input === null || typeof input !== 'object') return input;
    const raw = toRaw(input);
    const prototype = Object.getPrototypeOf(raw);
    // A plain record may originate in another Window/VM realm. Its Object
    // prototype still has a null parent, unlike Date/Map/class prototypes.
    const record = prototype === null || Object.getPrototypeOf(prototype) === null;
    if (!Array.isArray(raw) && !record) {
      return raw; // Let structuredClone preserve/validate non-record values.
    }
    const existing = seen.get(raw);
    if (existing) return existing;
    const out = Array.isArray(raw) ? new Array(raw.length) : Object.create(prototype);
    seen.set(raw, out);
    for (const key of Object.keys(raw)) {
      Object.defineProperty(out, key, {
        value: unwrap((raw as Record<string, unknown>)[key]),
        enumerable: true, configurable: true, writable: true,
      });
    }
    return out;
  }
  return structuredClone(unwrap(value)) as T;
}
