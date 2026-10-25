import type { BootModeId } from './bootModeModel';

/** Local, silent MP4 loops. Each mode keeps its own static poster fallback. */
export const BOOT_MODE_PREVIEWS: Record<BootModeId, { src: string; fallback: string }> = {
    global: { src: '/boot-modes/mode-01-global.mp4', fallback: '/boot-modes/mode-01-global.jpg' },
    xbox: { src: '/boot-modes/mode-02-xbox.mp4', fallback: '/boot-modes/mode-02-xbox.jpg' },
    steam: { src: '/boot-modes/mode-03-steam.mp4', fallback: '/boot-modes/mode-03-steam.jpg' },
    'xbox-steam': { src: '/boot-modes/mode-04-steam-xbox.mp4', fallback: '/boot-modes/mode-04-steam-xbox.jpg' },
};
