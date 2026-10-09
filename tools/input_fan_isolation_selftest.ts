import { readFileSync, mkdirSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';

const sourceRoot = resolve(process.cwd(), 'src/bridge');
const files = ['inputContracts.ts', 'inputCoordinatorMock.ts', 'inputLifecycleMock.ts', 'physicalInputOwnership.ts', 'virtualReportAssembler.ts', 'buttonMappingMock.ts', 'hcInputUtils.ts', 'gyroMotionMapperMock.ts', 'personaReportMock.ts', 'externalInputAdapterMock.ts', 'inputRuntimeAdmission.ts', 'inputCapabilityGate.ts', 'inputDiagnostics.ts', 'gyroVirtualFeature.ts', 'feedbackParticipantMock.ts'];
const forbidden = ['FanHost', 'YeManFanHost', 'fan-host', 'fanPayload', 'SetFanDuty', 'SetFanControl', 'ECRam', 'ACPI', 'WMI'];
const findings = files.map((name) => {
  const path = resolve(sourceRoot, name);
  const text = readFileSync(path, 'utf8');
  const tokens = forbidden.filter((token) => text.includes(token));
  return { name, path, forbiddenTokens: tokens };
});
const violations = findings.filter((item) => item.forbiddenTokens.length > 0);
if (violations.length) throw new Error(`input/fan isolation violation: ${JSON.stringify(violations)}`);
const evidencePath = resolve(process.cwd(), '../../Build/Validation/HC-Parity/S-11-input-fan-isolation-mock-trace.json');
mkdirSync(resolve(evidencePath, '..'), { recursive: true });
writeFileSync(evidencePath, JSON.stringify({
  evidenceId: 'S-11-INPUT-FAN-ISOLATION-MOCK-20260903',
  status: 'PASS',
  systemMutation: false,
  fanHostTouched: false,
  ecAcpiWmiWriteTouched: false,
  files: findings,
}, null, 2));
console.log(`input/fan isolation selftest: PASS (${evidencePath})`);
