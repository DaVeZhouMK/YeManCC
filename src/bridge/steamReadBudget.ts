// Event-driven automatic read budget. This owns no timers, scans or IO.
// Explicit actions may renew a failed budget; idle never schedules work.
export const STEAM_AUTOMATIC_READ_GAP_MS = 5000;
export const STEAM_AUTOMATIC_READ_FAILURE_LIMIT = 3;
export function createSteamReadBudget(gapMs = STEAM_AUTOMATIC_READ_GAP_MS, failureLimit = STEAM_AUTOMATIC_READ_FAILURE_LIMIT) {
  let lastStarted = -Infinity, failures = 0;
  return {
    delay(now: number): number | null { return failures >= failureLimit ? null : Math.max(0, lastStarted + gapMs - now); },
    started(now: number) { lastStarted = now; },
    finished(ok: boolean) { failures = ok ? 0 : failures + 1; },
    renew() { failures = 0; },
    get failures() { return failures; },
  };
}
