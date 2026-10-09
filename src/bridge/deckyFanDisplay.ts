// Ephemeral display coordination only. No settings, hardware owner, timers or IPC.
export interface FanMirrorDisplay { preset: 'soft' | 'balanced' | 'aggressive'; active: boolean; pending: boolean; notice: string; }
const listeners = new Set<(value: FanMirrorDisplay) => void>();
const blockers = new Set<() => boolean>();
const gateListeners = new Set<() => void>();
export function onFanMirrorUiGate(listener:() => void):() => void { gateListeners.add(listener); return () => { gateListeners.delete(listener); }; }
export function notifyFanMirrorUiGate():void { for(const listener of gateListeners) { try { listener(); } catch {} } }
export function onFanMirrorDisplay(listener: (value: FanMirrorDisplay) => void): () => void {
  listeners.add(listener); return () => { listeners.delete(listener); };
}
export function observeFanMirrorDisplay(value: FanMirrorDisplay): void {
  for (const listener of listeners) { try { listener({ ...value }); } catch {} }
}
export function registerFanMirrorUiBlocker(blocked: () => boolean): () => void {
  blockers.add(blocked); notifyFanMirrorUiGate();
  return () => { blockers.delete(blocked); notifyFanMirrorUiGate(); };
}
export function isFanMirrorUiBusy(): boolean {
  for (const blocked of blockers) { try { if (blocked()) return true; } catch { return true; } } return false;
}
