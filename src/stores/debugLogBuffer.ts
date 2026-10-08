// stores/debugLogBuffer.ts — bounded ring buffer for bridge diagnostic logs.
//
// Keeps the most recent `capacity` entries in a fixed-size slot array with
// O(1) append and oldest-slot overwrite, so a full buffer no longer shifts
// ~600 reactive array elements on every new entry (the old `push`+`splice`).
// A chronological snapshot is materialized lazily only when a reactive
// consumer actually reads it, and entries are stored as plain objects so
// background appends do not build deep reactive proxies.
import { shallowRef, type ShallowRef } from 'vue';
import type { LogEntry } from '@/bridge/ipc';

export interface LogRing {
  readonly capacity: number;
  /** Invalidate reactive consumers whenever entries change. */
  readonly version: ShallowRef<number>;
  append(entry: LogEntry): void;
  clear(): void;
  size(): number;
  /** Oldest→newest chronological snapshot; cached until the next mutation. */
  snapshot(): LogEntry[];
}

export function createLogRing(capacity: number): LogRing {
  const slots: (LogEntry | undefined)[] = new Array(capacity);
  let head = 0; // next write index
  let count = 0; // live entries, saturated at capacity
  let cached: LogEntry[] | null = null;
  let cachedVersion = -1;
  const version = shallowRef(0);

  function invalidate(): void {
    cached = null;
    version.value++;
  }

  function append(entry: LogEntry): void {
    slots[head] = entry;
    head = (head + 1) % capacity;
    if (count < capacity) count++;
    invalidate();
  }

  function clear(): void {
    slots.fill(undefined);
    head = 0;
    count = 0;
    invalidate();
  }

  function size(): number {
    return count;
  }

  function snapshot(): LogEntry[] {
    if (cached && cachedVersion === version.value) return cached;
    const out: LogEntry[] = new Array(count);
    const start = count < capacity ? 0 : head;
    for (let i = 0; i < count; i++) out[i] = slots[(start + i) % capacity] as LogEntry;
    cached = out;
    cachedVersion = version.value;
    return out;
  }

  return { capacity, version, append, clear, size, snapshot };
}
