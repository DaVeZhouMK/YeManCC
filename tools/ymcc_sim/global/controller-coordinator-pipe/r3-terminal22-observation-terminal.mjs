// Terminal admission for the SDK-free terminal22 observer only.
// Completed assertions and a partial report never replace a natural exit.
export const ORIGINAL_NATIVE_BUDGET_MS = 60_000;
const reject = (code, detail = '') => {
  const error = new Error(code + (detail ? ': ' + detail : ''));
  error.code = code;
  throw error;
};
export function readCompletedNativeReport(terminal, expectedExit, readReport) {
  if (!terminal || typeof terminal !== 'object' || ![0, 1].includes(expectedExit) ||
      typeof readReport !== 'function') reject('NATIVE22_TERMINAL_INPUT_INVALID');
  // The process result is authoritative before any report parsing. In
  // particular, SIGTERM + an empty report must remain an explicit timeout.
  if (terminal.timedOut === true) reject('NATIVE22_ORIGINAL_BUDGET_TIMEOUT',
    'native process exceeded the original 60000ms budget');
  if (terminal.timedOut !== false) reject('NATIVE22_TERMINAL_METADATA_INCOMPLETE');
  if (terminal.launchError !== null) reject('NATIVE22_PROCESS_LAUNCH_UNPROVEN');
  if (terminal.signal !== null) reject('NATIVE22_NATURAL_EXIT_UNPROVEN', String(terminal.signal));
  if (terminal.outputTruncated !== false) reject('NATIVE22_OUTPUT_INCOMPLETE');
  if (!Number.isFinite(terminal.durationMs) || terminal.durationMs < 0)
    reject('NATIVE22_DURATION_UNPROVEN');
  if (terminal.durationMs > ORIGINAL_NATIVE_BUDGET_MS)
    reject('NATIVE22_ORIGINAL_BUDGET_EXCEEDED');
  if (terminal.exitCode !== expectedExit) reject('NATIVE22_ORIGINAL_EXIT_MISMATCH',
    'expected=' + expectedExit + ';observed=' + terminal.exitCode);
  // Reading/missing/invalid JSON remains a distinct error only after terminal
  // admission. The caller still owns every original semantic/group oracle.
  return readReport();
}
