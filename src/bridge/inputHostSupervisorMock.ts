export type SupervisorPhase = 'stopped' | 'starting' | 'running' | 'recovering' | 'faulted';
export interface SupervisorJournal { schemaVersion: 1; runId: string; epoch: number; ownerIdentity: string; phase: SupervisorPhase; releaseProofComplete: boolean; }
export interface SupervisorSnapshot { phase: SupervisorPhase; runId: string | null; epoch: number; ownerIdentity: string | null; recoveryRequired: boolean; }

const id = (value: string) => /^[A-Za-z0-9._:-]+$/.test(value);

/** Single InputHost supervisor model with explicit crash recovery ownership. */
export class InputHostSupervisorMock {
  private state: SupervisorSnapshot = { phase: 'stopped', runId: null, epoch: 0, ownerIdentity: null, recoveryRequired: false };
  private journal: SupervisorJournal | null = null;

  start(runId: string, ownerIdentity: string, epoch: number): SupervisorSnapshot {
    if (!id(runId) || !id(ownerIdentity) || !Number.isSafeInteger(epoch) || epoch < this.state.epoch) throw new Error('invalid-supervisor-start');
    if (this.state.phase !== 'stopped') throw new Error('duplicate-host-start');
    this.state = { phase: 'running', runId, epoch, ownerIdentity, recoveryRequired: false };
    this.journal = { schemaVersion: 1, runId, epoch, ownerIdentity, phase: 'running', releaseProofComplete: false };
    return this.snapshot();
  }

  crash(): SupervisorSnapshot {
    if (this.state.phase !== 'running') throw new Error('host-not-running');
    this.state = { ...this.state, phase: 'recovering', recoveryRequired: true };
    if (this.journal) this.journal = { ...this.journal, phase: 'recovering' };
    return this.snapshot();
  }

  recover(journal: SupervisorJournal | null): SupervisorSnapshot {
    if (this.state.phase !== 'recovering') throw new Error('recovery-not-required');
    if (!journal || journal.schemaVersion !== 1 || !id(journal.runId) || !id(journal.ownerIdentity) || !journal.releaseProofComplete) {
      this.state = { ...this.state, phase: 'faulted' };
      throw new Error('recovery-journal-invalid');
    }
    this.state = { phase: 'stopped', runId: null, epoch: journal.epoch, ownerIdentity: null, recoveryRequired: false };
    this.journal = { ...journal, phase: 'stopped' };
    return this.snapshot();
  }

  stop(releaseProofComplete: boolean): SupervisorSnapshot {
    if (this.state.phase !== 'running') throw new Error('host-not-running');
    if (!releaseProofComplete) { this.state = { ...this.state, phase: 'faulted', recoveryRequired: true }; throw new Error('release-incomplete'); }
    this.state = { phase: 'stopped', runId: null, epoch: this.state.epoch, ownerIdentity: null, recoveryRequired: false };
    if (this.journal) this.journal = { ...this.journal, phase: 'stopped', releaseProofComplete: true };
    return this.snapshot();
  }

  getJournal(): SupervisorJournal | null { return this.journal ? { ...this.journal } : null; }
  snapshot(): SupervisorSnapshot { return { ...this.state }; }
}
