import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
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
