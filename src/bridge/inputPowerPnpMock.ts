export type InputIngressEvent = 'suspend' | 'resume' | 'device-remove' | 'device-insert';
export type InputIngressEventClass = 'system-power' | 'physical-source' | 'virtual-target';
export interface InputIngressMessage {
  event: InputIngressEvent;
  eventClass: InputIngressEventClass;
  generation: number;
  deviceIdentity?: string;
  sequence: number;
}
export interface InputIngressState {
  generation: number;
  phase: 'ready' | 'suspended' | 'resuming' | 'source-absent' | 'released';
  physicalSourceIdentity: string | null;
  virtualTargetIdentity: string | null;
  publication: 'enabled' | 'suppressed';
  targetSession: 'retained' | 'released';
  rearmRequired: boolean;
  lastEventClass: InputIngressEventClass | null;
  lastSequence: number;
}

/** Deterministic native power/PnP ordering model; no OS subscriptions. */
export class InputPowerPnpMock {
  private state: InputIngressState = {
    generation: 0,
    phase: 'ready',
    physicalSourceIdentity: null,
    virtualTargetIdentity: null,
    publication: 'enabled',
    targetSession: 'retained',
    rearmRequired: false,
    lastEventClass: null,
    lastSequence: 0,
  };
  apply(message: InputIngressMessage): InputIngressState {
    const before = { ...this.state };
    try {
      if (!Number.isSafeInteger(message.generation) || message.generation < this.state.generation) throw new Error('stale-power-generation');
      if (!Number.isSafeInteger(message.sequence) || message.sequence <= this.state.lastSequence) throw new Error('duplicate-or-out-of-order-event');
      if ((message.event === 'device-insert' || message.event === 'device-remove') && !message.deviceIdentity) throw new Error('device-identity-required');
      if ((message.event === 'suspend' || message.event === 'resume') && message.eventClass !== 'system-power') throw new Error('event-class-mismatch');
      if ((message.event === 'device-remove' || message.event === 'device-insert') &&
          message.eventClass !== 'physical-source' && message.eventClass !== 'virtual-target') throw new Error('event-class-mismatch');
      if (message.event === 'device-remove' && message.eventClass === 'physical-source') {
        if (this.state.physicalSourceIdentity === null) throw new Error('unmatched-physical-source-remove');
        if (message.deviceIdentity !== this.state.physicalSourceIdentity) throw new Error('physical-source-identity-mismatch');
      }
      if (message.event === 'device-remove' && message.eventClass === 'virtual-target') {
        if (this.state.virtualTargetIdentity === null) throw new Error('unmatched-virtual-target-remove');
        if (message.deviceIdentity !== this.state.virtualTargetIdentity) throw new Error('virtual-target-identity-mismatch');
      }
      this.state.lastSequence = message.sequence;
      this.state.generation = message.generation;
      this.state.lastEventClass = message.eventClass;
      switch (message.event) {
      case 'suspend':
        if (this.state.phase === 'released') throw new Error('suspend-after-release');
        this.state.phase = 'suspended';
        this.state.publication = 'suppressed';
        this.state.targetSession = 'released';
        this.state.rearmRequired = true;
        break;
      case 'resume':
        // Resume is a power transaction, not a generic recovery shortcut. A
        // released virtual target/backend must use an explicit rearm path.
        if (this.state.phase !== 'suspended') throw new Error('resume-without-system-suspend');
        this.state.phase = 'resuming';
        this.state.publication = 'suppressed';
        this.state.rearmRequired = true;
        break;
      case 'device-remove':
        if (message.eventClass === 'physical-source') {
          // HC ControllerManager removes only the matching physical source
          // admission. The virtual target/session is not inferred from this
          // source event, so no release or epoch/rearm is synthesized here.
          this.state.phase = 'source-absent';
          this.state.physicalSourceIdentity = null;
          this.state.publication = 'suppressed';
          this.state.targetSession = 'retained';
          this.state.rearmRequired = false;
        } else {
          // A virtual target/backend PnP event is a separate fault boundary.
          this.state.phase = 'released';
          this.state.virtualTargetIdentity = null;
          this.state.publication = 'suppressed';
          this.state.targetSession = 'released';
          this.state.rearmRequired = true;
        }
        break;
      case 'device-insert':
        this.state.phase = 'resuming';
        if (message.eventClass === 'physical-source') {
          // This deterministic ingress mock models one selected physical
          // source. HC itself tracks multiple controllers; silently replacing
          // the selected identity here would hide a topology mismatch.
          if (this.state.physicalSourceIdentity !== null) throw new Error('physical-source-topology-ambiguous');
          this.state.physicalSourceIdentity = message.deviceIdentity!;
        } else {
          if (this.state.virtualTargetIdentity !== null) throw new Error('virtual-target-topology-ambiguous');
          this.state.virtualTargetIdentity = message.deviceIdentity!;
        }
        // Insert only supplies a fresh source candidate. Coordinator must
        // explicitly rearm before publication resumes.
        this.state.publication = 'suppressed';
        this.state.targetSession = this.state.targetSession === 'released' ? 'released' : 'retained';
        this.state.rearmRequired = true;
        break;
      }
      return { ...this.state };
    } catch (error) {
      // Rejected events are transactional: a bad PnP/power message must not
      // consume sequence/generation state or make a later valid event stale.
      this.state = before;
      throw error;
    }
  }
  snapshot(): InputIngressState { return { ...this.state }; }
}
