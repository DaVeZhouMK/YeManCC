import { InputDiagnosticsBus } from '../src/bridge/inputDiagnostics';
import type { InputDiagnosticRecordV1 } from '../src/bridge/inputContracts';
const bus = new InputDiagnosticsBus();
const record: InputDiagnosticRecordV1 = {
  schemaVersion: 1, revision: 1, closure: 'NOT-OBSERVED', timestampUtc: new Date(0).toISOString(), timestampLocal: new Date(0).toString(),
  source: 'input-host', eventName: 'input.frame', operation: 'submit', runId: 'r1', hostInstanceId: 'host-1', processId: 1,
  nativeGeneration: 1, inputEpoch: 1, configRevision: 1, configHash: null, targetId: 't1', persona: 'xbox360', lifecyclePhase: 'active',
  transportResult: 'ok', callbackResult: 'ok', readback: 'not-observed', physicalObservation: 'not-observed', error: null, releaseProofId: null,
};
if (bus.append(record) || bus.snapshot().length !== 0) throw new Error('disabled diagnostics recorded data');
bus.setEnabled(true);
if (!bus.append(record) || bus.snapshot().length !== 1) throw new Error('enabled diagnostics did not record');
const exported = bus.exportSnapshot();
if (exported.schemaVersion !== 1 || exported.records.length !== 1) throw new Error('diagnostic export invalid');
bus.clear();
if (bus.snapshot().length !== 0) throw new Error('diagnostic clear failed');
bus.setEnabled(false);
if (bus.append(record) || bus.snapshot().length !== 0) throw new Error('disabled diagnostics recorded after toggle');
console.log('input diagnostics selftest: PASS');
