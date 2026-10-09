import fs from 'node:fs';
import path from 'node:path';

const evidencePath = path.resolve('tools/input-route-evidence-20260903.json');
const evidence = JSON.parse(fs.readFileSync(evidencePath, 'utf8')) as {
  schemaVersion: number; revision: number; closure: string;
  entries: Array<Record<string, unknown>>;
};
if (evidence.schemaVersion !== 1 || evidence.revision !== 1 || evidence.closure !== 'UNENCLOSED') {
  throw new Error('route evidence root must remain UNENCLOSED');
}
const expected = new Map([
  ['ymcc-native-lb-rb-summon', ['LB+RB', 'YMCC-ROUTE-LB-RB-006']],
  ['ymcc-native-b-double-tap-minimize', ['B double-tap', 'YMCC-ROUTE-B-DOUBLE-TAP-007']],
  ['ymcc-native-start-dpad-adjust', ['Start+D-pad', 'YMCC-ROUTE-START-DPAD-008']],
]);
if (evidence.entries.length !== expected.size) throw new Error('route evidence set changed unexpectedly');
for (const entry of evidence.entries) {
  const routeId = String(entry.routeId);
  if (entry.schemaVersion !== 1 || entry.closure !== 'UNENCLOSED' || entry.hcParityId !== null || entry.owner !== null || entry.releaseProofId !== null) {
    throw new Error(`route ${routeId} was admitted without HC/owner/release evidence`);
  }
  const expectedRoute = expected.get(routeId);
  if (!expectedRoute || expectedRoute[0] !== entry.physicalControlId) throw new Error(`unexpected route ${routeId}`);
}
const ledger = JSON.parse(fs.readFileSync(path.resolve('tools/HCParityLedger.v1.json'), 'utf8')) as { entries: Array<Record<string, unknown>> };
for (const [, [, parityId]] of expected) {
  const entry = ledger.entries.find((candidate) => candidate.parityId === parityId);
  if (!entry || entry.domain !== 'route' || entry.status !== 'blocked' || entry.parityClass !== 'unknown') throw new Error(`ledger link missing for ${parityId}`);
}
console.log(`input route evidence selftest: PASS (${evidence.entries.length} migration candidates remain UNENCLOSED)`);
