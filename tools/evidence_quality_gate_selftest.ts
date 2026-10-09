import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';

type Claim = { claimId: string; maximumTier: string; evidence: string[]; missingForRuntime: string[] };
const root = process.cwd();
const gate = JSON.parse(readFileSync(resolve(root, 'tools/EvidenceQualityGate.v1.json'), 'utf8')) as { schemaVersion: number; status: string; runtimeClosure: string; tiers: string[]; claims: Claim[]; requiredReferences: string[]; automaticDowngrade: string[] };
const ledger = JSON.parse(readFileSync(resolve(root, 'tools/HCParityDriftLedger.v2.json'), 'utf8')) as { entries: Array<{ driftId: string; status: string; runtimeEvidence: unknown[] }> };
const native = readFileSync(resolve(root, 'native/main.cpp'), 'utf8');
const hcInputUtils = readFileSync(resolve(root, 'src/bridge/hcInputUtils.ts'), 'utf8');
const validTiers = new Set(['source-located', 'ui-cas', 'mock-pass', 'isolated-probe', 'product-candidate', 'R1-runtime-observed']);
if (gate.schemaVersion !== 1 || gate.status !== 'SPEC-READY' || gate.runtimeClosure !== 'UNENCLOSED' || gate.claims.length < 5) throw new Error('T19 gate must remain a populated unclosed specification');
if (gate.tiers.length !== validTiers.size || gate.tiers.some((tier) => !validTiers.has(tier))) throw new Error('T19 evidence tier vocabulary diverged from BUS');
for (const claim of gate.claims) {
  if (!validTiers.has(claim.maximumTier) || claim.evidence.length === 0 || claim.missingForRuntime.length === 0) throw new Error(`T19 claim ${claim.claimId} is over-promoted or lacks an evidence gap`);
}
for (const file of gate.requiredReferences) readFileSync(resolve(root, file), 'utf8');
if (ledger.entries.some((entry) => entry.status === 'closed-runtime')) throw new Error('T19 cannot accept a runtime closure from this source-only evidence set');
const legacyParityComments = [native, hcInputUtils].reduce((count, source) => count + (source.match(/EXACT_HC/g) ?? []).length, 0);
if (!gate.automaticDowngrade.some((rule) => rule.includes('legacy partial candidate'))) throw new Error('T19 must retain the legacy partial-candidate downgrade rule');
const evidence = {
  evidenceId: 'T19-E01-EVIDENCE-QUALITY-GATE-STATIC-AUDIT-20260904', status: 'SPEC-READY', systemMutation: false,
  assertions: { requiredReferencesPresent: true, claimsHaveEvidenceGaps: true, noSourceOnlyRuntimePromotion: true, legacyParityCommentsDowngraded: true, runtimeClosure: 'UNENCLOSED' },
  sourceFindings: { legacyExactHcCommentCount: legacyParityComments },
};
const output = resolve(root, '../../Build/Validation/GyroVirtual/T19-E01-evidence-quality-gate-static-audit-20260904.json');
mkdirSync(resolve(output, '..'), { recursive: true });
writeFileSync(output, JSON.stringify(evidence, null, 2));
console.log(`evidence quality gate selftest: PASS (${output})`);
