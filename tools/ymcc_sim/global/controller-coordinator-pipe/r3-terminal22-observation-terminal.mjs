// Terminal admission for the SDK-free terminal22 observer only.
// Completed assertions and a partial report never replace a natural exit.
export const ORIGINAL_NATIVE_BUDGET_MS = 60_000;
const reject = (code, detail = '') => {
  const error = new Error(code + (detail ? ': ' + detail : ''));
  error.code = code;
  throw error;
};
const assessedBudgets = new WeakSet();
export function createNativeObservationBudget(managedInputContract, ms = ORIGINAL_NATIVE_BUDGET_MS) {
  if (!Number.isSafeInteger(ms) || ms < ORIGINAL_NATIVE_BUDGET_MS || ms > 180000 ||
      (ms !== ORIGINAL_NATIVE_BUDGET_MS && managedInputContract !== 'current105-terminal26-sdk-lifetime-compact-v1'))
    reject('NATIVE22_ASSESSED_BUDGET_INVALID');
  const budget = Object.freeze({ms, originalMs:ORIGINAL_NATIVE_BUDGET_MS,
    assessed:ms !== ORIGINAL_NATIVE_BUDGET_MS, managedInputContract,
    scope:'fixture-process-watchdog; original case and product windows unchanged'});
  assessedBudgets.add(budget);return budget;
}
export function readCompletedNativeReport(terminal, expectedExit, readReport, budget) {
  if (budget !== undefined && !assessedBudgets.has(budget)) reject('NATIVE22_ASSESSED_BUDGET_UNPROVEN');
  const limit = budget?.ms ?? ORIGINAL_NATIVE_BUDGET_MS;
  const assessed = budget?.assessed === true;
  if (!terminal || typeof terminal !== 'object' || ![0, 1].includes(expectedExit) ||
      typeof readReport !== 'function') reject('NATIVE22_TERMINAL_INPUT_INVALID');
  // The process result is authoritative before any report parsing. In
  // particular, SIGTERM + an empty report must remain an explicit timeout.
  if (terminal.timedOut === true) reject(assessed ? 'NATIVE22_ASSESSED_FIXTURE_BUDGET_TIMEOUT' : 'NATIVE22_ORIGINAL_BUDGET_TIMEOUT',
    'native process exceeded the ' + (assessed ? 'assessed ' + limit + 'ms fixture watchdog' : 'original 60000ms budget'));
  if (terminal.timedOut !== false) reject('NATIVE22_TERMINAL_METADATA_INCOMPLETE');
  if (terminal.launchError !== null) reject('NATIVE22_PROCESS_LAUNCH_UNPROVEN');
  if (terminal.signal !== null) reject('NATIVE22_NATURAL_EXIT_UNPROVEN', String(terminal.signal));
  if (terminal.outputTruncated !== false) reject('NATIVE22_OUTPUT_INCOMPLETE');
  if (!Number.isFinite(terminal.durationMs) || terminal.durationMs < 0)
    reject('NATIVE22_DURATION_UNPROVEN');
  if (terminal.durationMs > limit)
    reject(assessed ? 'NATIVE22_ASSESSED_FIXTURE_BUDGET_EXCEEDED' : 'NATIVE22_ORIGINAL_BUDGET_EXCEEDED');
  if (terminal.exitCode !== expectedExit) reject('NATIVE22_ORIGINAL_EXIT_MISMATCH',
    'expected=' + expectedExit + ';observed=' + terminal.exitCode);
  // Reading/missing/invalid JSON remains a distinct error only after terminal
  // admission. The caller still owns every original semantic/group oracle.
  return readReport();
}
