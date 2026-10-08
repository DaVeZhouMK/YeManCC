import { fs } from './api';

// Icons are derived UI assets, never part of the user's persisted launch list.
// Reuse pending/successful reads, bound memory, and limit shell IO to two jobs.
const MAX_CACHED_ICONS = 128;
const cache = new Map<string, Promise<string | null>>();
const queue: Array<() => void> = [];
let activeReads = 0;

function startNext(): void {
  while (activeReads < 2 && queue.length) {
    activeReads++;
    queue.shift()!();
  }
}

export function getQuickAppFileIcon(path: string): Promise<string | null> {
  const key = path.trim().replace(/\\/g, '/').toLowerCase();
  if (!key) return Promise.resolve(null);
  const cached = cache.get(key);
  if (cached) {
    cache.delete(key);
    cache.set(key, cached);
    return cached;
  }

  let result: Promise<string | null>;
  result = new Promise<string | null>(resolve => {
    queue.push(() => {
      void Promise.resolve().then(() => fs.getFileIcon(path)).then(icon => {
        // Never accept a shell path, URL or active SVG from native/old bridges.
        // The native encoder caps PNG output at 32 KiB before base64 encoding.
        resolve(typeof icon === 'string' && icon.length < 65536 &&
          /^data:image\/png;base64,[A-Za-z0-9+/]+={0,2}$/.test(icon) ? icon : null);
      }, () => resolve(null)).finally(() => {
        activeReads--;
        startNext();
      });
    });
  });
  cache.set(key, result);
  if (cache.size > MAX_CACHED_ICONS) cache.delete(cache.keys().next().value!);
  // Don't negatively cache missing files/temporary IPC errors across remounts.
  void result.then(icon => {
    if (!icon && cache.get(key) === result) cache.delete(key);
  });
  startNext();
  return result;
}
