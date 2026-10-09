import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';

type DriftEntry = {
  driftId: string;
  disposition: string;
  status: string;
  sourceEvidence: unknown[];
  runtimeEvidence: unknown[];
  [key: string]: unknown;
};
const ledgerPath = resolve(process.cwd(), 'tools/HCParityDriftLedger.v2.json');
const ledger = JSON.parse(readFileSync(ledgerPath, 'utf8')) as { schemaVersion: number; entries: DriftEntry[] };
if (ledger.schemaVersion !== 2 || !Array.isArray(ledger.entries) || ledger.entries.length !== 15) {
  throw new Error('T14 ledger must contain exactly DGF-01 through DGF-15');
}
const required = [
  'driftId', 'domain', 'taskId', 'behavior', 'sourcePath', 'symbol', 'observedOrder', 'parityId', 'ymanOwner',
  'currentClaimOrPath', 'disposition', 'parityClass', 'status', 'sourceEvidence', 'runtimeEvidence', 'risk',
  'safeStop', 'rollback', 'requiredProbe', 'acceptance', 'closureRule', 'decisionOrChange', 'owner', 'reviewDate',
];
// T21/T14 re-audit may classify a DGF as statically located/implemented while
// keeping runtime evidence blocked. These are ledger statuses, not runtime
// closure claims; preserve the existing closed-runtime guard below.
const allowedStatus = new Set([
  'open', 'probing', 'spec-ready', 'blocked', 'contradicted', 'closed-mock', 'closed-runtime',
  'source-located / runtime-blocked', 'source-implemented / runtime-blocked',
]);
const seen = new Set<string>();
for (const entry of ledger.entries) {
  if (!/^DGF-(0[1-9]|1[0-5])$/.test(entry.driftId) || seen.has(entry.driftId)) throw new Error(`invalid/duplicate drift id: ${entry.driftId}`);
  seen.add(entry.driftId);
  for (const key of required) {
    const value = entry[key];
    if (value == null || value === '' || (Array.isArray(value) && key !== 'runtimeEvidence' && value.length === 0)) throw new Error(`${entry.driftId} missing ${key}`);
  }
  if (!allowedStatus.has(entry.status)) throw new Error(`${entry.driftId} has unsupported status ${entry.status}`);
  if (entry.status === 'closed-runtime' && entry.runtimeEvidence.length === 0) throw new Error(`${entry.driftId} closed runtime without R1 evidence`);
  if (entry.disposition === 'CONTRADICTED' && entry.status === 'closed-runtime') throw new Error(`${entry.driftId} contradicted behavior cannot be runtime closed`);
}
for (let index = 1; index <= 15; index += 1) {
  const id = `DGF-${String(index).padStart(2, '0')}`;
  if (!seen.has(id)) throw new Error(`missing ${id}`);
}
const evidence = {
  evidenceId: 'T14-E01-T19-E01-HC-PARITY-DRIFT-LEDGER-AUDIT-20260904',
  status: 'SPEC-READY', systemMutation: false,
  assertions: {
    allDgfRegistered: true, requiredFieldsPresent: true, runtimeEvidenceSeparated: true,
    closedRuntimeRequiresR1: true, contradictedCannotCloseRuntime: true,
  },
  entryStatus: ledger.entries.map(({ driftId, disposition, status }) => ({ driftId, disposition, status })),
};
const evidencePath = resolve(process.cwd(), '../../Build/Validation/GyroVirtual/T14-E01-T19-E01-hc-parity-drift-ledger-audit-20260904.json');
mkdirSync(resolve(evidencePath, '..'), { recursive: true });
writeFileSync(evidencePath, JSON.stringify(evidence, null, 2));
console.log(`HC parity drift ledger selftest: PASS (${evidencePath})`);
