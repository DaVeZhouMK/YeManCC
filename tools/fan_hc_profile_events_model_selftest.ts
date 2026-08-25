/**
 * T3 source-contract model. This does not load HC or the real Host and never
 * writes hardware. It exercises the exact boundary we deliberately keep
 * serialized: Applied is queued, Discarded is a wait marker, and a later
 * Applied owns the replacement. Sleep/close gates suppress queued writes.
 */

type ProfileEvent = { kind: 'applied'; id: string } | { kind: 'discarded' };

class HostBoundaryModel {
  private readonly events: ProfileEvent[] = [];
  private template = 'initial';
  private writes: string[] = [];
  private blocked = false;
  private discardedWaiting = false;
  private source = 'Background';

  onApplied(id: string, source = 'Background'): void {
    this.events.push({ kind: 'applied', id });
    this.source = source;
  }
  onDiscarded(): void {
    this.events.push({ kind: 'discarded' });
    this.discardedWaiting = true;
  }
  blockWrites(): void { this.blocked = true; }
  unblockWrites(): void { this.blocked = false; }

  tick(curve: string): void {
    if (this.blocked) return;
    let changed = false;
    while (this.events.length > 0) {
      const event = this.events.shift()!;
      if (event.kind === 'discarded') {
        // HC Discarded clears the current context but does not apply a
        // fallback; the following Applied event owns the replacement.
        continue;
      }
      this.template = event.id;
      this.discardedWaiting = false;
      changed = true;
    }
    if (changed) this.writes.push(`${this.template}:${curve}`);
  }

  snapshot(): { template: string; writes: readonly string[]; source: string; waiting: boolean } {
    return {
      template: this.template,
      writes: [...this.writes],
      source: this.source,
      waiting: this.discardedWaiting,
    };
  }
}

function assert(condition: unknown, message: string): void {
  if (!condition) throw new Error(message);
}

const checks: string[] = [];

{
  const model = new HostBoundaryModel();
  model.onApplied('A');
  model.onDiscarded();
  model.onApplied('B');
  model.tick('curve');
  const state = model.snapshot();
  assert(state.template === 'B', 'Applied after Discarded must win');
  assert(state.writes.length === 1 && state.writes[0] === 'B:curve', 'discard/apply burst wrote stale context');
  checks.push('discard-then-applied-burst');
}

{
  const model = new HostBoundaryModel();
  model.onApplied('Profile-UI', 'ProfilesPage');
  model.tick('curve');
  const state = model.snapshot();
  assert(state.source === 'ProfilesPage', 'Applied UpdateSource was normalized away');
  checks.push('applied-source-preserved');
}

{
  const model = new HostBoundaryModel();
  model.onDiscarded();
  model.tick('curve');
  const state = model.snapshot();
  assert(state.template === 'initial' && state.writes.length === 0, 'Discarded alone must not synthesize a default write');
  assert(state.waiting, 'Discarded alone must retain an explicit wait boundary');
  checks.push('discard-alone-no-fallback-write');
}

{
  const model = new HostBoundaryModel();
  model.onApplied('A');
  model.onApplied('B');
  model.tick('curve');
  const state = model.snapshot();
  assert(state.template === 'B' && state.writes.length === 1, 'Applied burst did not serialize to latest context');
  checks.push('applied-burst-latest-context');
}

{
  const model = new HostBoundaryModel();
  model.onApplied('A');
  model.blockWrites();
  model.tick('curve');
  assert(model.snapshot().writes.length === 0, 'sleep/close gate allowed queued profile write');
  model.unblockWrites();
  model.tick('curve');
  assert(model.snapshot().writes.length === 1, 'profile event was lost across a temporary write gate');
  checks.push('sleep-close-gate-suppresses-queued-write');
}

console.log(JSON.stringify({ ok: true, checks, hardwareWrites: false }));
