import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';

type Plane = { plane: string; currentStatus: string; safeStop: string; evidenceRequired: string[] };
const root = process.cwd();
const topology = JSON.parse(readFileSync(resolve(root, 'tools/RogVisibilityTopology.v1.json'), 'utf8')) as { schemaVersion: number; status: string; runtimeClosure: string; planes: Plane[]; rogOemNegative: { report: string; productDisposition: string; safeStop: string } };
const native = readFileSync(resolve(root, 'native/main.cpp'), 'utf8');
const adaptive = readFileSync(resolve(root, '../../../Isolated/HC-Candidate-0.32.4.0-06c0b954-20260902/HandheldCompanion/Controllers/Asus/XboxAdaptiveController.cs'), 'utf8');
const rog = readFileSync(resolve(root, '../../../Isolated/HC-Candidate-0.32.4.0-06c0b954-20260902/HandheldCompanion/Devices/ASUS/ROGAlly.cs'), 'utf8');
if (topology.schemaVersion !== 1 || !['SPEC-READY', 'SOURCE-CONTRACT-READY'].includes(topology.status) || topology.runtimeClosure !== 'UNENCLOSED' || topology.planes.length !== 3) throw new Error('T18 topology must retain three separate unclosed planes');
for (const name of ['P-OWNER', 'P-HID', 'P-XINPUT']) {
  const plane = topology.planes.find((candidate) => candidate.plane === name);
  if (!plane || !plane.safeStop || plane.evidenceRequired.length < 3) throw new Error(`T18 ${name} is not independently specified`);
}
if (!adaptive.includes('XBoxController(true)') || !rog.includes('public bool XBoxController(bool disabled)')) throw new Error('T18 HC ROG OEM Disable source was not located');
const oemDisableAbsent = !/rogSuppressForFrontend|rogRestoreForGame|rogWriteXboxMode/.test(native) && !native.includes('report[4] = 0x02');
const pHidExactSelectedTransaction = native.includes('hidHideApplyPhysicalTransaction(') &&
  native.includes('hidHideRestorePhysicalTransaction(') &&
  !native.includes('hidHideLegacyUnadmittedGamingScan();');
if (!oemDisableAbsent || !pHidExactSelectedTransaction) throw new Error('T18 source safety boundary changed; do not infer ROG isolation');
if (topology.rogOemNegative.report !== '5A D1 0B 01 02' || !topology.rogOemNegative.productDisposition.includes('permanently forbidden')) throw new Error('T18 ROG OEM negative contract is incomplete');
const evidence = {
  evidenceId: 'T18-E01-ROG-VISIBILITY-TOPOLOGY-STATIC-AUDIT-20260906', status: 'SOURCE-CONTRACT-READY', systemMutation: false,
  assertions: { threePlanesSeparated: true, hcOemDisableLocated: true, oemDisableAbsentFromYmccCandidate: true, pHidExactSelectedTransaction: true, pXinputUnenclosed: true, runtimeClosure: 'UNENCLOSED' },
};
const output = resolve(root, '../../Build/Validation/GyroVirtual/T18-E01-rog-visibility-topology-static-audit-20260906.json');
mkdirSync(resolve(output, '..'), { recursive: true });
writeFileSync(output, JSON.stringify(evidence, null, 2));
console.log(`ROG visibility topology selftest: PASS (${output})`);
