import { invoke } from './ipc';
export interface SteamOverlayFixState {
  via: 'none' | 'live' | 'file' | 'deferred' | 'error';
  busy: boolean;
  pending: boolean;
  desiredValue: number;
  value?: number;
  persisted?: boolean;
  reason?: string;
}
export function steamOverlayFixGet(): Promise<SteamOverlayFixState> {
  return invoke<SteamOverlayFixState>('steam.settings.get',{scope:'overlay'});
}
