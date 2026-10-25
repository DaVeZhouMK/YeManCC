// Display transport projection only. Saved preferences and the shared unified
// snapshot retain their images; each output frame carries its active raster.
import type { HudData, MonitorOutputPreferences } from './monitorHudModel';
export function hudWirePreferences(preferences: MonitorOutputPreferences): MonitorOutputPreferences {
  const {ymccTemplates: _banks, ymccTemplateResets: _resets, ...wire}=preferences;
  if (preferences.ymcc.underlay === 'custom' || !preferences.ymcc.underlayImage) return wire;
  return { ...wire, ymcc: { ...preferences.ymcc, underlayImage: '' } };
}
export function hudWireData(data: HudData, preferences: MonitorOutputPreferences): HudData {
  if (preferences.ymcc.underlay === 'game-logo' || !data.gameLogo) return data;
  return { ...data, gameLogo: '' };
}
