import { invoke } from './ipc';
export interface SteamOverlayFixState {
  via: 'none' | 'live' | 'file' | 'deferred' | 'error' | 'deprecated';
  busy: boolean;
  pending: boolean;
  desiredValue: number | null;
  deprecated?: true;
  enabled?: false;
  value?: number;
  persisted?: boolean;
  reason?: string;
}
/** @deprecated Abandoned sleep strategy. Read-only compatibility status, no UI. */
export function steamOverlayFixGet(): Promise<SteamOverlayFixState> {
  return invoke<SteamOverlayFixState>('steam.settings.get',{scope:'overlay'});
}
