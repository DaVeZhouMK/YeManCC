export type InputLifecyclePhase = 'disabled' | 'verified' | 'opening' | 'target-ready' | 'neutralized' | 'admitted' | 'active' | 'quiescing' | 'safe-zero' | 'released' | 'faulted';
export type InputLifecycleEvent = 'enable' | 'verified' | 'target-ready' | 'neutral' | 'admit' | 'frame' | 'reconfigure' | 'suspend' | 'resume' | 'physical-source-remove' | 'physical-source-insert' | 'virtual-target-fault' | 'crash' | 'close' | 'safe-zero' | 'release' | 'rearm';

export interface InputReleaseProof { frameAdmissionStopped: boolean; neutralSent: boolean; readerStopped: boolean; targetRemoved: boolean; visibilityRestored: boolean; handlesDisposed: boolean; workersJoined: boolean; }
export interface InputLifecycleSnapshot {
  phase: InputLifecyclePhase;
  epoch: number;
  powerGeneration: number;
  configRevision: number;
  owner: string | null;
  releaseProof: InputReleaseProof | null;
  sourceAdmission: 'admitted' | 'suppressed';
  /** Physical controller/container identity. Never reuse for the virtual target. */
  physicalSourceIdentity: string | null;
  /** Virtual target/backend identity. Kept in a separate identity domain. */
  virtualTargetIdentity: string | null;
  targetSession: 'retained' | 'released';
  rearmRequired: boolean;
  /** Only a completed system-suspend release may use the Resume path. */
  resumeEligible: boolean;
}

type InputLifecycleDetails = Partial<Pick<
  InputLifecycleSnapshot,
  'powerGeneration' | 'configRevision' | 'physicalSourceIdentity' | 'virtualTargetIdentity'
>>;

const emptyProof = (): InputReleaseProof => ({ frameAdmissionStopped: false, neutralSent: false, readerStopped: false, targetRemoved: false, visibilityRestored: false, handlesDisposed: false, workersJoined: false });
const complete = (p: InputReleaseProof | null) => !!p && Object.values(p).every(Boolean);

/** Deterministic mock of the single HC/input lifecycle; no process or device IO. */
export class InputLifecycleMock {
  private state: InputLifecycleSnapshot = {
    phase: 'disabled', epoch: 0, powerGeneration: 0, configRevision: 0, owner: null, releaseProof: null,
    sourceAdmission: 'suppressed', physicalSourceIdentity: null, virtualTargetIdentity: null,
    targetSession: 'released', rearmRequired: false, resumeEligible: false,
  };
  transition(event: InputLifecycleEvent, details: InputLifecycleDetails = {}): InputLifecycleSnapshot {
    const s = this.state;
    const before = this.snapshot();
    try {
      if (details.powerGeneration !== undefined && details.powerGeneration < s.powerGeneration) throw new Error('stale-power-generation');
      if (details.configRevision !== undefined && details.configRevision < s.configRevision) throw new Error('stale-config-revision');
      if (details.powerGeneration !== undefined) s.powerGeneration = details.powerGeneration;
      if (details.configRevision !== undefined) s.configRevision = details.configRevision;
      switch (event) {
      case 'enable':
        if (s.phase !== 'disabled' && s.phase !== 'released') throw new Error('enable-invalid');
        s.epoch++; s.phase = 'verified'; s.owner = 'input-host'; s.releaseProof = null;
        s.physicalSourceIdentity = details.physicalSourceIdentity ?? null;
        s.virtualTargetIdentity = details.virtualTargetIdentity ?? null;
        s.sourceAdmission = 'admitted'; s.targetSession = 'retained'; s.rearmRequired = false;
        s.resumeEligible = false;
        break;
      case 'verified': if (s.phase !== 'verified') throw new Error('verify-invalid'); s.phase = 'opening'; break;
      case 'target-ready': if (s.phase !== 'opening') throw new Error('target-invalid'); s.phase = 'target-ready'; break;
      case 'neutral': if (s.phase !== 'target-ready' && s.phase !== 'safe-zero') throw new Error('neutral-invalid'); s.phase = 'neutralized'; break;
      case 'admit': if (s.phase !== 'neutralized') throw new Error('admit-invalid'); s.phase = 'admitted'; break;
      case 'frame': if (s.phase !== 'admitted' && s.phase !== 'active') throw new Error('frame-not-admitted'); s.phase = 'active'; break;
      case 'reconfigure': if (s.phase !== 'active' && s.phase !== 'admitted') throw new Error('reconfigure-invalid'); s.phase = 'quiescing'; s.releaseProof = emptyProof(); s.releaseProof.frameAdmissionStopped = true; break;
      case 'suspend': case 'crash': case 'close':
        if (!['active','admitted','neutralized','target-ready','opening'].includes(s.phase)) throw new Error('stop-invalid');
        s.phase = event === 'crash' ? 'faulted' : 'quiescing'; s.epoch++; s.releaseProof = emptyProof(); s.releaseProof.frameAdmissionStopped = true;
        s.sourceAdmission = 'suppressed'; s.targetSession = 'released'; s.rearmRequired = true;
        s.resumeEligible = event === 'suspend';
        break;
      case 'physical-source-remove':
        if (!['active','admitted','neutralized','target-ready','opening'].includes(s.phase)) throw new Error('source-remove-invalid');
        if (!details.physicalSourceIdentity) throw new Error('source-identity-required');
        if (s.physicalSourceIdentity === null) throw new Error('unmatched-physical-source-remove');
        if (s.physicalSourceIdentity !== details.physicalSourceIdentity) throw new Error('physical-source-identity-mismatch');
        s.sourceAdmission = 'suppressed';
        s.physicalSourceIdentity = null;
        s.targetSession = 'retained';
        s.rearmRequired = false;
        // HC source loss is a writer no-write boundary, not a product
        // target release or epoch rotation.
        break;
      case 'physical-source-insert':
        if (!details.physicalSourceIdentity) throw new Error('source-identity-required');
        if (s.physicalSourceIdentity !== null) throw new Error('physical-source-topology-ambiguous');
        s.physicalSourceIdentity = details.physicalSourceIdentity;
        s.sourceAdmission = 'suppressed';
        s.targetSession = s.targetSession === 'released' ? 'released' : 'retained';
        s.rearmRequired = true;
        // Insert provides only a fresh candidate; Coordinator must rearm.
        break;
      case 'virtual-target-fault':
        if (!['active','admitted','neutralized','target-ready','opening'].includes(s.phase)) throw new Error('virtual-target-fault-invalid');
        if (details.virtualTargetIdentity !== undefined) {
          if (s.virtualTargetIdentity === null) throw new Error('unmatched-virtual-target-fault');
          if (s.virtualTargetIdentity !== details.virtualTargetIdentity) throw new Error('virtual-target-identity-mismatch');
        }
        s.phase = 'faulted'; s.epoch++; s.releaseProof = emptyProof(); s.releaseProof.frameAdmissionStopped = true;
        s.sourceAdmission = 'suppressed'; s.virtualTargetIdentity = null; s.targetSession = 'released'; s.rearmRequired = true;
        break;
      case 'safe-zero': if (s.phase !== 'quiescing' && s.phase !== 'faulted') throw new Error('safe-zero-invalid'); s.phase = 'safe-zero'; if (s.releaseProof) s.releaseProof.neutralSent = true; break;
      case 'release':
        if (s.phase !== 'safe-zero') throw new Error('release-invalid');
        if (s.releaseProof) Object.keys(s.releaseProof).forEach(k => (s.releaseProof as any)[k] = true);
        s.phase = 'released'; s.owner = null; s.sourceAdmission = 'suppressed'; s.virtualTargetIdentity = null; s.targetSession = 'released'; s.rearmRequired = true;
        break;
      case 'resume':
        if (s.phase !== 'released' || !s.resumeEligible) throw new Error('resume-requires-system-suspend-release');
        s.epoch++; s.phase = 'verified'; s.releaseProof = null; s.sourceAdmission = 'admitted'; s.targetSession = 'retained'; s.rearmRequired = false; s.resumeEligible = false;
        break;
      case 'rearm':
        if (s.phase !== 'released' && s.phase !== 'faulted' && !s.rearmRequired) throw new Error('rearm-invalid');
        s.epoch++; s.phase = 'verified'; s.releaseProof = null; s.owner = 'input-host'; s.sourceAdmission = 'admitted'; s.targetSession = 'retained'; s.rearmRequired = false; s.resumeEligible = false;
        break;
      }
      return this.snapshot();
    } catch (error) {
      // Lifecycle transitions are atomic: a rejected identity, epoch, or
      // phase event must not partially advance the mock's counters/state.
      this.state = before;
      throw error;
    }
  }
  /** Rotate the coordinator epoch without tearing down the target. */
  rotateEpoch(): InputLifecycleSnapshot {
    if (!['admitted', 'active', 'released'].includes(this.state.phase)) throw new Error('epoch-rotation-requires-admission');
    this.state.epoch += 1;
    return this.snapshot();
  }
  snapshot(): InputLifecycleSnapshot { return { ...this.state, releaseProof: this.state.releaseProof ? { ...this.state.releaseProof } : null }; }
  hasCompleteReleaseProof(): boolean { return complete(this.state.releaseProof); }
}
