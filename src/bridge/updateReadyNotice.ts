/** A presentation seam, owned by the existing monitor popup host. No telemetry,
 * persistent HUD, extra window or installation is started by the updater. */
let presenter: ((version: string) => Promise<boolean>) | null = null;
export function registerUpdateReadyPresenter(next: (version: string) => Promise<boolean>): () => void {
  if (presenter) throw Error('更新通知已有呈现者');
  presenter = next;
  return () => { if (presenter === next) presenter = null; };
}
export async function presentUpdateReady(version: string): Promise<boolean> {
  try { return await presenter?.(version) === true; } catch { return false; }
}
