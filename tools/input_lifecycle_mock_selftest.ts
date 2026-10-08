import { InputLifecycleMock } from '../src/bridge/inputLifecycleMock';
const l = new InputLifecycleMock();
for (const e of ['enable','verified','target-ready','neutral','admit','frame'] as const) l.transition(e);
if (l.snapshot().phase !== 'active') throw new Error('active transition failed');
let unboundRemove = false;
try { l.transition('physical-source-remove', { physicalSourceIdentity: 'pad-0' }); } catch { unboundRemove = true; }
if (!unboundRemove || l.snapshot().phase !== 'active') throw new Error('unbound physical source remove was accepted or mutated state');

const identityDomains = new InputLifecycleMock();
identityDomains.transition('enable', { physicalSourceIdentity: 'pad-1', virtualTargetIdentity: 'target-1' });
for (const e of ['verified','target-ready','neutral','admit','frame'] as const) identityDomains.transition(e);
identityDomains.transition('physical-source-remove', { physicalSourceIdentity: 'pad-1' });
const afterPhysicalRemove = identityDomains.snapshot();
if (afterPhysicalRemove.physicalSourceIdentity !== null || afterPhysicalRemove.virtualTargetIdentity !== 'target-1' || afterPhysicalRemove.targetSession !== 'retained') {
  throw new Error('physical source removal crossed virtual target identity domain');
}
const wrongTargetFault = new InputLifecycleMock();
wrongTargetFault.transition('enable', { physicalSourceIdentity: 'pad-1', virtualTargetIdentity: 'target-1' });
for (const e of ['verified','target-ready','neutral','admit','frame'] as const) wrongTargetFault.transition(e);
let targetMismatchRejected = false;
try { wrongTargetFault.transition('virtual-target-fault', { virtualTargetIdentity: 'target-2' }); } catch { targetMismatchRejected = true; }
if (!targetMismatchRejected || wrongTargetFault.snapshot().virtualTargetIdentity !== 'target-1') throw new Error('virtual target identity mismatch was accepted or mutated state');
l.transition('suspend', { powerGeneration: 1 });
for (const e of ['safe-zero','release'] as const) l.transition(e);
if (!l.hasCompleteReleaseProof() || l.snapshot().owner !== null) throw new Error('release proof incomplete');
l.transition('resume', { powerGeneration: 1 });
if (l.snapshot().phase !== 'verified' || l.snapshot().epoch < 2) throw new Error('resume did not create new epoch');
const closed = new InputLifecycleMock();
for (const e of ['enable','verified','target-ready','neutral','admit','frame','close','safe-zero','release'] as const) closed.transition(e);
let closeResumeRejected = false;
const closedBeforeResume = closed.snapshot();
try { closed.transition('resume'); } catch { closeResumeRejected = true; }
if (!closeResumeRejected) throw new Error('close release incorrectly accepted power resume');
const closedAfterResume = closed.snapshot();
if (closedAfterResume.epoch !== closedBeforeResume.epoch || closedAfterResume.phase !== closedBeforeResume.phase || closedAfterResume.resumeEligible !== closedBeforeResume.resumeEligible) throw new Error('rejected resume mutated lifecycle state');
let stale = false;
try { l.transition('target-ready', { configRevision: -1 }); } catch { stale = true; }
if (!stale) throw new Error('stale config was accepted');
console.log('input lifecycle mock selftest: PASS');
