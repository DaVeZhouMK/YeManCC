import { invoke, isNativeRuntime } from './ipc';
import { subscribeGameStatus } from './gamedetect';
import { createGameLogoConsumer, type GameLogoResult } from './gameLogoConsumer';
import { prepareHudUnderlayFile } from './monitorHudUnderlayImport';
import { normalizeHudUnderlayImage } from './monitorHudModel';
const consumer = createGameLogoConsumer({
  subscribeGame: subscribeGameStatus, now: Date.now,
  async read(executable) {
    if (!isNativeRuntime) return null;
    const raw = await invoke<GameLogoResult | null>('customSteamLibrary.gameLogo', { executable }, { timeoutMs: 10000 });
    return raw && typeof raw.image === 'string' && raw.image.length <= 2800000 &&
      /^data:image\/(?:png|jpeg|webp);base64,[A-Za-z0-9+/]+={0,2}$/.test(raw.image) && typeof raw.revision === 'string' ? raw : null;
  },
  async prepare(image) {
    const binary = atob(image.slice(image.indexOf(',') + 1)), bytes = new Uint8Array(binary.length);
    for (let i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i);
    return normalizeHudUnderlayImage(await prepareHudUnderlayFile(new File([bytes], 'game-logo', { type: 'application/octet-stream' })));
  },
});
// Returning from the existing editor invalidates both missing and changed assets.
// No file watcher, disk scan, network lookup or extra game-detection schedule.
if (typeof window !== 'undefined') window.addEventListener('customSteamLibrary:closed', () => consumer.invalidate());
export const subscribeMonitorGameLogo = consumer.subscribe;