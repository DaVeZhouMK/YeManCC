// Ephemeral display coordination only. No settings, hardware owner, timers or IPC.
export interface FanMirrorDisplay { preset: 'soft' | 'balanced' | 'aggressive'; active: boolean; pending: boolean; notice: string; }
let latest:FanMirrorDisplay|null=null;
export function getFanMirrorDisplay():FanMirrorDisplay|null {return latest?{...latest}:null;}
const listeners = new Set<(value: FanMirrorDisplay) => void>();
const blockers = new Set<() => boolean>();
const gateListeners = new Set<() => void>();
export function onFanMirrorUiGate(listener:() => void):() => void { gateListeners.add(listener); return () => { gateListeners.delete(listener); }; }
export function notifyFanMirrorUiGate():void { for(const listener of gateListeners) { try { listener(); } catch {} } }
export function onFanMirrorDisplay(listener: (value: FanMirrorDisplay) => void): () => void {
  listeners.add(listener); return () => { listeners.delete(listener); };
}
export function observeFanMirrorDisplay(value: FanMirrorDisplay): void {
  if(latest && latest.preset===value.preset && latest.active===value.active && latest.pending===value.pending && latest.notice===value.notice)return;
  const delivery=latest={...value};
  for (const listener of listeners) {
    // A nested publication owns the remaining display deliveries.
    if(latest!==delivery)break;
    try { listener({ ...delivery }); } catch {}
  }
}
export function registerFanMirrorUiBlocker(blocked: () => boolean): () => void {
  blockers.add(blocked); notifyFanMirrorUiGate();
  return () => { blockers.delete(blocked); notifyFanMirrorUiGate(); };
}
export function isFanMirrorUiBusy(): boolean {
  for (const blocked of blockers) { try { if (blocked()) return true; } catch { return true; } } return false;
}
