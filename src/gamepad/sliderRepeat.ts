/** One tick is always one edit step. Only the interval accelerates. */
export const EDIT_REPEAT_BASE_MS = 55;
export const EDIT_REPEAT_MIN_MS = 25;
export const EDIT_REPEAT_DELAY_MS = 400;
export const EDIT_REPEAT_RAMP_MS = 1600;

/** Same bounded linear cadence as the native UI input consumer. */
export function editRepeatInterval(heldMs: number, baseMs = EDIT_REPEAT_BASE_MS, accelerate = true): number {
  const base = Number.isFinite(baseMs) && baseMs > 0 ? Math.max(5, Math.min(220, Math.floor(baseMs))) : EDIT_REPEAT_BASE_MS;
  if (!accelerate) return base;
  const held = Number.isFinite(heldMs) ? Math.max(0, heldMs) : 0;
  const ramp = Math.min(EDIT_REPEAT_RAMP_MS, Math.max(0, held - EDIT_REPEAT_DELAY_MS));
  return base - Math.floor(ramp * (base - Math.min(base, EDIT_REPEAT_MIN_MS)) / EDIT_REPEAT_RAMP_MS);
}
