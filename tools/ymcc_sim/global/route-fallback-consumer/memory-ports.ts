/** Only effects leave through these memory terminals; no route logic here. */
export const window = (globalThis as any).__routeFallbackOwnedWindow as Window;
export const dialog = {
  confirm: (title: string, message: string): Promise<boolean> =>
    (globalThis as any).__routeFallbackMemory.confirm(title, message),
};
export const resetApplicationSettings = (): Promise<{backups: string[]}> =>
  (globalThis as any).__routeFallbackMemory.reset();
