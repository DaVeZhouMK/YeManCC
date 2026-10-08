import { ExternalInputAdapterMock } from '../src/bridge/externalInputAdapterMock';
import { encodePersonaReport } from '../src/bridge/personaReportMock';
import type { CanonicalFrame } from '../src/bridge/virtualReportAssembler';

const frame: CanonicalFrame = {
  schemaVersion: 1, runId: 'run', epoch: 7, powerGeneration: 1, targetId: 'target', persona: 'xbox360', configRevision: 1,
  inputSequence: 1, sampleSequence: 0, timestamp: 1, neutral: false, buttons: 0, axes: {}, triggers: {},
  rightStickBase: { x: 0, y: 0 }, gyroContribution: { x: 0, y: 0 }, rightStick: { x: 0, y: 0 },
};
const encoded = encodePersonaReport(frame);
if (!encoded.ok) throw new Error('fixture encoding failed');
const adapter = new ExternalInputAdapterMock();
if (!adapter.create(7).ok || adapter.create(7).ok) throw new Error('F4 writer exclusivity failed');
if (!adapter.submit(encoded.report).ok) throw new Error('F4 normal report failed');
adapter.failOnceForTest();
const failed = adapter.submit(encoded.report);
if (failed.ok || failed.reason !== 'transport-failed' || !adapter.snapshot().released) throw new Error('F4 failure did not release');
const events = adapter.trace.map((item) => item.event).join(',');
if (events !== 'create,report,neutral,release') throw new Error(`F4 release order failed: ${events}`);
console.log('external input adapter mock selftest: PASS');
