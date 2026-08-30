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

/** R1 queue model: failure keeps the detached Applied snapshot until the
 * bounded retry budget is exhausted, while lifecycle generations reject late
 * callbacks from a closed/suspended session. */
class SessionRetryModel {
  private generation = 0;
  private accepting = false;
  private pending: { id: string; generation: number; attempts: number } | null = null;
  private writes: string[] = [];
  private readonly maxAttempts = 3;

  beginSession(): number {
    this.generation += 1;
    this.accepting = false;
    this.pending = null;
    return this.generation;
  }

  activate(generation: number): boolean {
    if (generation !== this.generation) return false;
    this.accepting = true;
    return true;
  }

  invalidate(): void {
    this.generation += 1;
    this.accepting = false;
    this.pending = null;
  }

  applied(id: string, generation: number): boolean {
    if (!this.accepting || generation !== this.generation) return false;
    this.pending = { id, generation, attempts: 0 };
    return true;
  }

  discarded(generation: number): boolean {
    if (!this.accepting || generation !== this.generation) return false;
    this.pending = null;
    return true;
  }

  tick(curve: string, fail = false): void {
    if (!this.pending || !this.accepting) return;
    if (fail) {
      this.pending.attempts += 1;
      if (this.pending.attempts >= this.maxAttempts) this.pending = null;
      return;
    }
    this.writes.push(`${this.pending.id}:${curve}`);
    this.pending = null;
  }

  hasPending(): boolean { return this.pending !== null; }
  getWriteCount(): number { return this.writes.length; }
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

{
  const model = new SessionRetryModel();
  const generation = model.beginSession();
  assert(model.activate(generation) && model.applied('Applied-A', generation), 'R1 Applied snapshot was not accepted');
  model.tick('curve', true);
  assert(model.hasPending(), 'R1 clone/apply failure consumed Applied before retry');
  model.tick('curve', true);
  assert(model.hasPending(), 'R1 second failure consumed Applied before retry budget exhausted');
  model.tick('curve', true);
  assert(!model.hasPending() && model.getWriteCount() === 0, 'R1 retry budget did not terminate without a false write');
  checks.push('applied-failure-bounded-retry');
}

{
  const model = new SessionRetryModel();
  const oldGeneration = model.beginSession();
  assert(model.activate(oldGeneration) && model.applied('stale', oldGeneration), 'R1 old session was not established');
  model.invalidate();
  const newGeneration = model.beginSession();
  assert(model.activate(newGeneration), 'R1 new session was not activated');
  assert(!model.applied('late-old', oldGeneration), 'R1 late Applied crossed Close/Resume generation boundary');
  assert(!model.discarded(oldGeneration), 'R1 late Discarded crossed Close/Resume generation boundary');
  assert(!model.hasPending(), 'R1 stale profile remained pending after lifecycle invalidation');
  assert(model.applied('current', newGeneration), 'R1 current session Applied was rejected');
  model.tick('curve');
  assert(model.getWriteCount() === 1, 'R1 current session profile was not applied');
  checks.push('session-generation-rejects-late-profile');
}

console.log(JSON.stringify({ ok: true, checks, hardwareWrites: false }));
