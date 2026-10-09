// One trailing-edge UI intent, never a hardware timer or resident policy.
import type { ClientState, MirrorSnapshot } from './mirrorClient';
export const POWER_CHOICE_DELAY_MS = 3000;
export interface PendingPowerChoice { field: 'acMode' | 'dcMode'; value: string; snapshot: MirrorSnapshot; }
export class PowerChoiceDebounce {
  private pending: PendingPowerChoice | null = null;
  private timer: ReturnType<typeof setTimeout> | null = null;
  private listeners = new Set<() => void>();
  constructor(private deps: { state: () => ClientState; send: (choice: PendingPowerChoice) => Promise<unknown>;
    schedule: typeof setTimeout; cancel: typeof clearTimeout }) {}
  snapshot() { return this.pending ? structuredClone(this.pending) : null; }
  subscribe(listener: () => void) { this.listeners.add(listener); return () => { this.listeners.delete(listener); }; }
  private notify() { for (const listener of this.listeners) listener(); }
  private valid(choice: PendingPowerChoice, state: ClientState) {
    const current = state.snapshot, expected = choice.snapshot;
    return state.connected && !state.busy && current?.ready && current.actions?.game &&
      current.runId === expected.runId && current.generation === expected.generation && current.revision === expected.revision &&
      current.game?.identity === expected.game?.identity && current.powerSource === (choice.field === 'acMode' ? 'ac' : 'dc') &&
      current.game?.fields[choice.field]?.supported && current.game.fields[choice.field].choices.some(option => option.data === choice.value && !option.disabled);
  }
  observe(state: ClientState) { if (this.pending && !this.valid(this.pending, state)) this.cancel(); }
  choose(field: 'acMode' | 'dcMode', value: string, snapshot: MirrorSnapshot) {
    const choice = { field, value, snapshot: structuredClone(snapshot) };
    if (!this.valid(choice, this.deps.state()) || value === 'follow' || value === 'legacy') return;
    this.cancel();
    // Returning to the saved value cancels the previous intent, without a write.
    if (value === snapshot.game?.fields[field]?.value) return;
    this.pending = choice;
    this.timer = this.deps.schedule(() => {
      if (this.pending !== choice) return;
      this.timer = null; this.pending = null; this.notify();
      if (this.valid(choice, this.deps.state())) void this.deps.send(choice).catch(() => {});
    }, POWER_CHOICE_DELAY_MS);
    this.notify();
  }
  cancel() { if (this.timer !== null) this.deps.cancel(this.timer); this.timer = null;
    if (this.pending) { this.pending = null; this.notify(); } }
  dispose() { this.cancel(); this.listeners.clear(); }
}
