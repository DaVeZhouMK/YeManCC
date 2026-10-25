// Independent expected values: task contract + hash-pinned HC protocol oracle.
// This module never reads candidate headers, snapshots, or executable output.
import path from 'node:path';
import fs from 'node:fs';
import { fileURLToPath } from 'node:url';
import { deriveOracle, cppOracle, sha } from '../hc_onex_x2_protocol/oracle.mjs';
export function expectations(workspace, protocolFixtures) {
  const protocol = deriveOracle(workspace, protocolFixtures);
  const fixtures = path.dirname(fileURLToPath(import.meta.url));
  const pins = JSON.parse(fs.readFileSync(path.join(fixtures, 'framing-pins.json')));
  const audit = path.join(workspace, 'Build/Validation/HC-ModelCapabilities-20261017/OneXRuntime/FramingAudit');
  if (pins.commit !== protocol.commit) throw Error('Framing and protocol use different HC versions');
  const provenance = [];
  for (const pin of [pins.dll, ...pins.text]) {
    const file = path.join(audit, pin.path), bytes = fs.readFileSync(file);
    if (sha(bytes) !== pin.sha256) throw Error(`Framing source pin mismatch: ${pin.path}`);
    if (pin.gitBlob && (bytes.length !== pin.bytes ||
        sha(Buffer.concat([Buffer.from(`blob ${bytes.length}\0`), bytes]), 'sha1') !== pin.gitBlob))
      throw Error('Exact HC HidLibrary DLL blob mismatch');
    provenance.push({path: file, sha256: pin.sha256});
  }
  const code = fs.readFileSync(path.join(audit, pins.text[0].path), 'utf8');
  if (!/Array\.Resize\(ref _data, reportSize - 1\)/.test(code) ||
      !/_reportId = deviceData\.Data\[0\]/.test(code) ||
      !/Array\.Copy\(deviceData\.Data, 1, _data, 0, length\)/.test(code))
    throw Error('Pinned HidReport normalization contract missing');
  return {
    framing: { source: 'a250 exact HidLibrary 3.2.49, static evidence only', provenance,
      stripBytes: 1, reportIdRestrictedToZero: false, raw64Logical: 63, raw65Logical: 64 },
    schemaVersion: 1,
    authority: 'Session safety/timing requirements supplied by user; wire pages/roles from fixed HC source',
    policy: {
      insertionDelayMs: 4000, pageGapMs: 50, writeTimeoutMs: 250,
      retainedNoticeMs: 1000, bindRetryMs: 2000, statusDelayMs: 200,
      reinitIntervalMs: 2000, maxReinitializations: 4, eventCapacity: 128,
      maxInputBytes: 512,
    },
    applicationContract: {
      rearObserved: 'Redundant decoded rear UP emits RearObserved with vendor ID/current generation/context; redundant DOWN emits no event',
      discardPreparedWrite: 'Only matching Live/current prepared ticket; only writePending may change; no advance or read cancellation',
      transportBoundary: 'Session exposes no submitted marker; transport must never call discardPreparedWrite after kernel submission',
    },
    // Named tags are an API contract, not obtained by querying the candidate.
    roles: { None: 0, Turbo: 1, Keyboard: 2, Home: 3, L4: 4, R4: 5 },
    protocol,
  };
}
export function generatedHeader(expected) {
  return cppOracle(expected.protocol) + '\nnamespace policy {\n' +
    Object.entries(expected.policy).map(([key, value]) =>
      `inline constexpr std::uint64_t ${key} = ${value};`).join('\n') + '\n}\n';
}