import { InputPowerPnpMock } from '../src/bridge/inputPowerPnpMock';
import { mkdirSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
const p = new InputPowerPnpMock();
p.apply({ event: 'suspend', eventClass: 'system-power', generation: 1, sequence: 1 });
p.apply({ event: 'resume', eventClass: 'system-power', generation: 1, sequence: 2 });
if (p.snapshot().phase !== 'resuming') throw new Error('resume phase missing');
let dup = false; try { p.apply({ event: 'resume', eventClass: 'system-power', generation: 1, sequence: 2 }); } catch { dup = true; }
if (!dup) throw new Error('duplicate event accepted');
let stale = false; try { p.apply({ event: 'device-insert', eventClass: 'physical-source', generation: 0, sequence: 3, deviceIdentity: 'pad-1' }); } catch { stale = true; }
if (!stale) throw new Error('stale generation accepted');
let unmatched = false; try { p.apply({ event: 'device-remove', eventClass: 'physical-source', generation: 2, sequence: 3, deviceIdentity: 'pad-1' }); } catch { unmatched = true; }
if (!unmatched) throw new Error('unmatched physical source remove accepted');
p.apply({ event: 'device-insert', eventClass: 'physical-source', generation: 2, sequence: 3, deviceIdentity: 'pad-1' });
let topologyAmbiguousRejected = false;
try { p.apply({ event: 'device-insert', eventClass: 'physical-source', generation: 2, sequence: 4, deviceIdentity: 'pad-2' }); } catch { topologyAmbiguousRejected = true; }
if (!topologyAmbiguousRejected || p.snapshot().physicalSourceIdentity !== 'pad-1' || p.snapshot().lastSequence !== 3) throw new Error('second active physical source silently replaced selected identity');
const sourceRemoved = p.apply({ event: 'device-remove', eventClass: 'physical-source', generation: 2, sequence: 5, deviceIdentity: 'pad-1' });
if (sourceRemoved.phase !== 'source-absent' || sourceRemoved.publication !== 'suppressed' || sourceRemoved.targetSession !== 'retained' || sourceRemoved.rearmRequired) {
  throw new Error('physical source remove incorrectly released target/session');
}
p.apply({ event: 'device-insert', eventClass: 'physical-source', generation: 3, sequence: 6, deviceIdentity: 'pad-2' });
if (p.snapshot().physicalSourceIdentity !== 'pad-2' || p.snapshot().publication !== 'suppressed' || !p.snapshot().rearmRequired) throw new Error('physical insert did not remain pending rearm');
const v = new InputPowerPnpMock();
v.apply({ event: 'device-insert', eventClass: 'virtual-target', generation: 1, sequence: 1, deviceIdentity: 'virtual-1' });
const virtualRemoved = v.apply({ event: 'device-remove', eventClass: 'virtual-target', generation: 1, sequence: 2, deviceIdentity: 'virtual-1' });
if (virtualRemoved.phase !== 'released' || virtualRemoved.targetSession !== 'released' || !virtualRemoved.rearmRequired) throw new Error('virtual target remove did not enter release boundary');
let virtualResumeRejected = false;
try { v.apply({ event: 'resume', eventClass: 'system-power', generation: 1, sequence: 3 }); } catch { virtualResumeRejected = true; }
if (!virtualResumeRejected) throw new Error('virtual target release incorrectly accepted power resume');
if (v.snapshot().lastSequence !== 2 || v.snapshot().generation !== 1) throw new Error('rejected resume consumed ingress sequence/generation');
const evidencePath = resolve(process.cwd(), '../../Build/Validation/HC-Parity/S-18-input-power-pnp-mock-trace.json');
mkdirSync(resolve(evidencePath, '..'), { recursive: true });
writeFileSync(evidencePath, JSON.stringify({
  evidenceId: 'S-18-INPUT-POWER-PNP-MOCK-20260903', status: 'PASS', systemMutation: false,
  assertions: {
    duplicateRejected: dup,
    staleGenerationRejected: stale,
    unmatchedPhysicalSourceRemoveRejected: unmatched,
    topologyAmbiguousInsertRejected: topologyAmbiguousRejected,
    rejectedResumeDidNotConsumeSequence: v.snapshot().lastSequence === 2 && v.snapshot().generation === 1,
    physicalSourceRemoveIsNoWriteAndRetainsTarget: sourceRemoved.phase === 'source-absent' && sourceRemoved.targetSession === 'retained' && sourceRemoved.publication === 'suppressed',
    physicalInsertRequiresExplicitRearm: p.snapshot().rearmRequired,
    virtualTargetRemoveReleases: virtualRemoved.phase === 'released' && virtualRemoved.targetSession === 'released',
  },
  finalState: p.snapshot(),
  virtualTargetFaultState: virtualRemoved,
}, null, 2));
console.log(`input power/pnp selftest: PASS (${evidencePath})`);
