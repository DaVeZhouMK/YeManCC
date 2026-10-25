// Pure decoding reuse inside the existing YMCC hub. No reads, timer or TTL.
// Cache one bounded immutable wire string and a private parsed template. Each
// valid return is an owned copy; stop floors cannot poison later packets.
export const MONITOR_INVALID_CACHE_MAX_CHARS = 16 * 1024;
export function createMonitorPayloadDecoder<T>(deps: {
  parse(raw: string): T | null;
  copy(value: T): T;
  cacheable?(value: T): boolean;
  // Opt in only when null depends solely on raw text, never time or IO.
  cacheInvalid?: boolean;
}) {
  let previous = '', template: T | null = null, cachedInvalid = false;
  return (raw: string): T | null => {
    if (raw === previous && (template !== null || cachedInvalid)) return template === null ? null : deps.copy(template);
    const value = deps.parse(raw);
    if (value !== null && raw.length <= 65536 && deps.cacheable?.(value) !== false) {
      previous = raw; template = value; cachedInvalid = false;
      return deps.copy(template);
    }
    // The two pure hub parsers can reject a repeated bad document without
    // reparsing it. Share the one existing slot; a changed string retries at
    // once. Bound retained invalid text independently of the wire-byte gate.
    if (value === null && deps.cacheInvalid === true && raw.length <= MONITOR_INVALID_CACHE_MAX_CHARS) {
      previous = raw; template = null; cachedInvalid = true; return null;
    }
    // Large/unusual payloads retain the original admission behavior, releasing
    // prior storage rather than installing a partially validated template.
    previous = ''; template = null; cachedInvalid = false;
    return value;
  };
}