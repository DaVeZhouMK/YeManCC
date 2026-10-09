import { InputHostSupervisorMock } from '../src/bridge/inputHostSupervisorMock';
import { mkdirSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
const s = new InputHostSupervisorMock();
s.start('run-1', 'input-host', 1);
let duplicate = false; try { s.start('run-2', 'input-host', 2); } catch { duplicate = true; }
if (!duplicate) throw new Error('duplicate host start accepted');
s.crash();
const journal = s.getJournal();
if (!journal || journal.phase !== 'recovering') throw new Error('crash journal missing');
let corrupt = false; try { s.recover({ ...journal!, releaseProofComplete: false }); } catch { corrupt = true; }
if (!corrupt || s.snapshot().phase !== 'faulted') throw new Error('corrupt journal not fail-closed');
const fresh = new InputHostSupervisorMock();
fresh.start('run-3', 'input-host', 3); fresh.crash();
const good = fresh.getJournal();
fresh.recover({ ...good!, releaseProofComplete: true });
if (fresh.snapshot().phase !== 'stopped') throw new Error('valid recovery did not stop host');
const evidencePath = resolve(process.cwd(), '../../Build/Validation/HC-Parity/S-17-input-host-supervisor-mock-trace.json');
mkdirSync(resolve(evidencePath, '..'), { recursive: true });
writeFileSync(evidencePath, JSON.stringify({
  evidenceId: 'S-17-INPUT-HOST-SUPERVISOR-MOCK-20260903', status: 'PASS', systemMutation: false,
  assertions: { duplicateStartRejected: duplicate, corruptJournalFailClosed: corrupt, validRecoveryStopped: fresh.snapshot().phase === 'stopped' },
  recoveredJournal: good,
}, null, 2));
console.log(`input host supervisor selftest: PASS (${evidencePath})`);
