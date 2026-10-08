import { readFileSync, mkdirSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';

const tools = process.cwd();
const x360 = JSON.parse(readFileSync(resolve(tools, 'tools/hidmaestro_real_probe_evidence.json'), 'utf8')) as Record<string, any>;
const ds4 = JSON.parse(readFileSync(resolve(tools, 'tools/hidmaestro_ds4_probe_evidence_20260903.json'), 'utf8')) as Record<string, any>;
if (x360.status !== 'PASS' || x360.probe?.create == null) throw new Error('T7 missing isolated X360 create evidence');
if (x360.descriptorProbe?.reportSizeBytes !== 18) throw new Error('T7 X360 report size not recorded');
if (!String(x360.remaining || '').includes('HidHide') && !JSON.stringify(x360.remaining).includes('HidHide')) throw new Error('T7 HidHide gap was dropped');
if (ds4.status !== 'PASS') throw new Error('T7 DS4 isolated probe missing');

const matrix = {
  schemaVersion: 1,
  revision: 1,
  closure: 'UNENCLOSED',
  matrixId: 'ProviderCapabilityMatrix.v1',
  hidMaestro: {
    identity: 'HIDMaestro-1.7.0',
    parityClass: 'adapted',
    status: 'located',
    isolatedCreateSubmitDispose: true,
    productInputHost: false,
    hidapiSteamGameConsumer: false,
    notes: 'Isolated HIDMaestroTest.exe X360/DS4 probes exist. They are not the YeManCC InputHost adapter.',
  },
  hidHide: {
    identity: 'HidHide-1.5.230',
    parityClass: 'unknown',
    status: 'blocked',
    xinputIsolation: false,
    notes: 'HidHide is not an XInput block. Diff/allowlist/restore remain unproven.',
  },
  virtualManager: {
    identity: 'HC VirtualManager 0.32.4.0/06c0b954',
    parityClass: 'unknown',
    status: 'located',
    productEquivalent: false,
  },
};

const out = resolve(tools, '../../Build/Validation/HC-Parity/A1-provider-capability-matrix-20260904.json');
mkdirSync(resolve(out, '..'), { recursive: true });
writeFileSync(out, JSON.stringify({ evidenceId: 'A1-PROVIDER-CAPABILITY-MATRIX-20260904', status: 'PASS_REFERENTIAL', systemMutation: false, matrix }, null, 2));
if (matrix.hidMaestro.productInputHost) throw new Error('T7 promoted isolated probe to product host');
console.log(`T7 provider capability matrix selftest: PASS (${out})`);
