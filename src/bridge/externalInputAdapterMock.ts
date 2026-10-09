import type { EncodedVirtualReport } from './personaReportMock';

export type AdapterPhase = 'idle' | 'ready' | 'released';
export type AdapterTrace = { event: 'create' | 'report' | 'neutral' | 'release'; epoch: number; detail: string };
export type AdapterResult = { ok: true } | { ok: false; reason: 'not-ready' | 'writer-conflict' | 'transport-failed' };

/**
 * F4 fixture adapter. It models the mandatory create/write/neutral/release
 * contract without importing any driver or performing a system mutation.
 */
export class ExternalInputAdapterMock {
  private phase: AdapterPhase = 'idle';
  private epoch: number | null = null;
  private failNextWrite = false;
  readonly trace: AdapterTrace[] = [];

  create(epoch: number): AdapterResult {
    if (this.phase === 'ready') return { ok: false, reason: 'writer-conflict' };
    this.phase = 'ready';
    this.epoch = epoch;
    this.trace.push({ event: 'create', epoch, detail: 'fixture target created' });
    return { ok: true };
  }

  failOnceForTest(): void {
    this.failNextWrite = true;
  }

  submit(report: EncodedVirtualReport): AdapterResult {
    if (this.phase !== 'ready' || this.epoch !== report.epoch) return { ok: false, reason: 'not-ready' };
    if (this.failNextWrite) {
      this.failNextWrite = false;
      this.abort(report.epoch, 'fixture transport failure');
      return { ok: false, reason: 'transport-failed' };
    }
    this.trace.push({ event: report.neutral ? 'neutral' : 'report', epoch: report.epoch, detail: report.persona });
    return { ok: true };
  }

  abort(epoch: number, detail: string): void {
    if (this.phase !== 'ready' || this.epoch !== epoch) return;
    this.trace.push({ event: 'neutral', epoch, detail });
    this.trace.push({ event: 'release', epoch, detail });
    this.phase = 'released';
    this.epoch = null;
  }

  snapshot(): { phase: AdapterPhase; epoch: number | null; released: boolean } {
    return { phase: this.phase, epoch: this.epoch, released: this.phase === 'released' };
  }
}
