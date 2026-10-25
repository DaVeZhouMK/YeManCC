// Output geometry only; no display enumeration, sensors, timers or saved offsets.
import type { HudCrosshair } from './monitorHudModel';
import { hudPlacement } from './monitorHudProtocol';
export interface HudViewport {
  width: number; height: number;
  workArea?: { left: number; top: number; width: number; height: number };
}
export function hudCarrierPlacement(viewport: HudViewport, width: number, height: number, position: number, crosshair?: HudCrosshair) {
  return crosshair && crosshair !== 'none'
    ? { left: 0, top: 0, width: viewport.width, height: viewport.height }
    : hudPlacement(viewport.width, viewport.height, width, height, position);
}
export function validHudViewport(value: unknown): value is HudViewport {
  const v = value as HudViewport;
  const dimension = (n: unknown) => typeof n === 'number' && Number.isFinite(n) && n > 0 && n <= 16384;
  if (!v || !dimension(v.width) || !dimension(v.height)) return false;
  const a = v.workArea;
  return !a || (dimension(a.width) && dimension(a.height) && Number.isFinite(a.left) && Number.isFinite(a.top) &&
    a.left >= 0 && a.top >= 0 && a.left + a.width <= v.width && a.top + a.height <= v.height);
}
