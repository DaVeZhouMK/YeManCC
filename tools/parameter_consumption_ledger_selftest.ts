import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';

type Entry = { replayId: string; hcSource: string; nativeConsumer: string; safeStop: string; [key: string]: unknown };
const root = process.cwd();
const ledger = JSON.parse(readFileSync(resolve(root, 'tools/ParameterConsumptionLedger.v1.json'), 'utf8')) as {
  schemaVersion: number; status: string; runtimeClosure: string; transactionContract: string; entries: Entry[];
};
const native = readFileSync(resolve(root, 'native/main.cpp'), 'utf8');
const host = readFileSync(resolve(root, 'InputHost/Program.cs'), 'utf8');
const repository = readFileSync(resolve(root, 'src/bridge/settingsRepository.ts'), 'utf8');

if (ledger.schemaVersion !== 1 || ledger.status !== 'SPEC-READY' || ledger.runtimeClosure !== 'UNENCLOSED') throw new Error('T15 ledger must remain SPEC-READY / UNENCLOSED');
if (!ledger.transactionContract.includes('ACK(appliedRevision, appliedHash, status)')) throw new Error('T15 lacks exact ACK transaction contract');
const expected = Array.from({ length: 8 }, (_, index) => `T10-F${String(index + 1).padStart(2, '0')}`);
if (ledger.entries.length !== expected.length || JSON.stringify(ledger.entries.map((entry) => entry.replayId)) !== JSON.stringify(expected)) {
  throw new Error('T15 must enumerate exactly the eight contract parameter vectors');
}
for (const entry of ledger.entries) {
  if (!entry.hcSource || !entry.safeStop || !/not-consumed|partial standalone lane/.test(entry.nativeConsumer)) {
    throw new Error(`${entry.replayId} must state HC source, bounded native path, and safe-stop`);
  }
}
// The standalone lane has a deliberately bounded partial consumer. It is
// still not a complete T10 revisioned parameter transaction and remains
// unavailable in formal builds without the explicit lane marker.
// A13 同步（2026-09-20）：原字面量为 `const bool gyroSubmitAllowed = pairProven && g_gmLocked;`；
// 现源为 `... && !wakeGyroHold;`（main.cpp:19733，唤醒保持门）——**属性更强**（多一道门），
// 断言按新表达式更新，并同时要求旧表达式消失：若唤醒保持门被撤掉，本断言会失败。
const partialNativeConsumer = native.includes('g_realStickTest.gyroMultiplier') &&
  native.includes('g_realStickTest.motionSensitivityX') &&
  native.includes('g_realStickTest.motionSensitivityY') &&
  native.includes('g_realStickTest.gyroWeight') &&
  native.includes('g_realStickTest.motionInput') &&
  native.includes('const bool gyroSubmitAllowed = pairProven && g_gmLocked && !wakeGyroHold;') &&
  !native.includes('const bool gyroSubmitAllowed = pairProven && g_gmLocked;');
const hostCarriesRevisionHashTuple = host.includes('configRevision') && host.includes('configHash') && host.includes('SameTuple');
const hostDoesNotConsumeGyroParameters = !/gyroMultiplier|accelerometerMultiplier|motionSensitivityX|motionSensitivityY|gyroWeight|innerDeadzone|outerDeadzone|antiDeadzone|velocityMode|motionInput|outputShape|calibrationId/.test(host);
const uiHasBookkeepingOnly = repository.includes('hostAcknowledgedRevision') && repository.includes('appliedRevision');
// The native diagnostic is emitted for both lanes.  Its value is false only
// inside the explicit standalone test lane and true for the ordinary runtime;
// assert the source expression rather than a stale literal serialization.
const formalLaneStillMarked = native.includes('HC-REAL-STICK-TEST-v1') &&
  native.includes('formalReleaseIncluded", !g_realStickTest.standaloneTestLane');
if (!partialNativeConsumer || !hostCarriesRevisionHashTuple || !hostDoesNotConsumeGyroParameters || !uiHasBookkeepingOnly || !formalLaneStillMarked) {
  throw new Error('T15 source audit no longer matches the declared bounded partial-consumption boundary');
}
const evidence = {
  evidenceId: 'T15-E01-PARAMETER-CONSUMPTION-STATIC-AUDIT-20260904', status: 'SPEC-READY', systemMutation: false,
  assertions: {
    eightVectorsRegistered: true,
    hcSourcesRegistered: true,
    boundedStandaloneNativeConsumerObserved: true,
    hostCarriesRevisionHashTuple: true,
    hostDoesNotConsumeGyroParameters: true,
    uiAckBookkeepingDoesNotProveHostApply: true,
    runtimeClosure: 'UNENCLOSED',
  },
  safeStop: 'complete-parameter-transaction-not-consumed',
};
const output = resolve(root, '../../Build/Validation/GyroVirtual/T15-E01-parameter-consumption-static-audit-20260904.json');
mkdirSync(resolve(output, '..'), { recursive: true });
writeFileSync(output, JSON.stringify(evidence, null, 2));
console.log(`parameter consumption ledger selftest: PASS (${output})`);
