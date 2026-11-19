import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
if (process.argv[2] === '--owner-transition-evidence') {
  // Prepared selected fixture lives beside this bundle; default compilation stays marker-only.
  await import(new URL('./logging_owner_transition_fixture.mjs', import.meta.url).href);
} else if (process.argv[2] === '--overlay-entry-evidence') {
  await import(new URL('./logging_overlay_entry_fixture.mjs', import.meta.url).href);
} else if (process.argv[2] === '--exec-guard-evidence') {
  await import(new URL('./logging_exec_guard_fixture.mjs', import.meta.url).href);
} else {
const source = readFileSync(resolve(process.cwd(), 'src/gamepad/engine.ts'), 'utf8');
const required = [
  "inputOwnerRuntime.summon()",
  "inputOwnerRuntime.semanticAction()",
  'inputOwnerRuntime.sourceRemoved()',
  "inputOwnerRuntime.stop('suspend')",
  "inputOwnerRuntime.stop(reason)",
  "inputOwnerRuntime.stop('crash')",
  "inputOwnerRuntime.stop('close')",
  "ipc:window.blur",
  "ipc:window.focus",
  "clearSummonState('blur')",
  "inputOwnerRuntime.snapshot().owner === 'none'",
  "current.epoch !== admission.epoch",
  "restartGamepadState()",
  "new CustomEvent('input:owner-trace'",
];
for (const marker of required) if (!source.includes(marker)) throw new Error(`owner runtime integration marker missing: ${marker}`);
if (!source.includes("inputOwnerRuntime.start()")) throw new Error('owner runtime game-consumer start marker missing');
console.log('input owner runtime integration selftest: PASS');
}
