// One trailing intent for continuous performance edits. No business policy,
// hardware access, saved settings, intervals or per-step network requests.
export type PerformanceValue = string | number | boolean;
export type PerformancePatch = Record<string, PerformanceValue>;
export interface PendingPerformanceIntent<View> { view: View; patch: PerformancePatch; }
export class PerformanceIntentQueue<View> {
  private disposed = false;
  private editing = false;
  private pending: PendingPerformanceIntent<View> | null = null;
  private applying: PendingPerformanceIntent<View> | null = null;
  private confirming=false;
  private confirmationTimer:ReturnType<typeof setTimeout>|null=null;
  private timer: ReturnType<typeof setTimeout> | null = null;
  private listeners = new Set<() => void>();
  constructor(private deps: {
    current: () => View | null;
    sameSource: (expected: View, current: View) => boolean;
    allowed: (patch: PerformancePatch, view: View) => boolean;
    value: (field: string, view: View) => PerformanceValue | undefined;
    send: (intent: PendingPerformanceIntent<View>) => Promise<unknown>;
    schedule: (action: () => void, delay: number) => ReturnType<typeof setTimeout>;
    cancel: (timer: ReturnType<typeof setTimeout>) => void;
    delayMs?: number;
    confirmOnSnapshot?:boolean;
  }) {}
  // Keep the displayed intent through the actual reply. Dropping it before send
  // briefly restored old values and toggled the whole native panel's busy styles.
  snapshot() {
    const intent=this.pending??this.applying;
    return intent?{...structuredClone(intent),phase:this.pending?(this.editing?'editing':'queued'):this.confirming?'confirming':'applying'}:null;
  }
  subscribe(listener: () => void) { if(this.disposed)return () => {}; this.listeners.add(listener); return () => { this.listeners.delete(listener); }; }
  private notify() { for (const listener of this.listeners) { try { listener(); } catch {} } }
  private valid(expected: View, patch: PerformancePatch, current: View | null): current is View {
    return current !== null && this.deps.sameSource(expected, current) && this.deps.allowed(patch, current);
  }
  observe(current: View | null = this.deps.current()) {
    if(this.disposed)return;
    // The host can be busy with OUR write. That must not remove its draft;
    // a connection/session/game/source change still invalidates it immediately.
    if(this.applying&&(!current||!this.deps.sameSource(this.applying.view,current)||this.confirming&&this.valuesConfirmed(this.applying,current)))this.finishApplying(this.applying);
    if(!this.pending)return;
    if(!this.valid(this.pending.view,this.pending.patch,current)){this.cancel();return;}
    this.pending.view=structuredClone(current);
  }
  private valuesConfirmed(intent:PendingPerformanceIntent<View>,current:View) {
    return Object.entries(intent.patch).every(([field,value])=>this.deps.value(field,current)===value);
  }
  private finishApplying(intent:PendingPerformanceIntent<View>) {
    if(this.applying!==intent)return;
    if(this.confirmationTimer!==null)this.deps.cancel(this.confirmationTimer);
    this.confirmationTimer=null;this.confirming=false;this.applying=null;this.notify();
  }
  begin() {
    if(this.disposed)return;
    this.editing=true;
    if(this.timer!==null)this.deps.cancel(this.timer);
    this.timer=null;
  }
  end() {
    if(this.disposed||!this.editing)return;
    this.editing=false;
    if(this.pending)this.arm(this.pending);
  }
  private arm(intent: PendingPerformanceIntent<View>) {
    this.timer = this.deps.schedule(() => {
      if (this.pending !== intent || this.editing) return;
      this.timer=null;
      const latest=this.deps.current();
      if(!this.valid(intent.view,intent.patch,latest)){this.pending=null;this.notify();return;}
      intent.view=structuredClone(latest);
      this.pending=null;this.applying=intent;this.notify();
      // send is admitted once; failures roll back visually and are never replayed.
      if(this.applying!==intent)return;
      const admitted=this.deps.current();
      if(!this.valid(intent.view,intent.patch,admitted)){this.applying=null;this.notify();return;}
      intent.view=structuredClone(admitted);
      let result:Promise<unknown>;
      try{result=this.deps.send(intent);}catch(error){result=Promise.reject(error);}
      void Promise.resolve(result).then(()=>{
        if(this.applying!==intent)return;
        if(!this.deps.confirmOnSnapshot){this.finishApplying(intent);return;}
        const current=this.deps.current();
        if(current&&(!this.deps.sameSource(intent.view,current)||this.valuesConfirmed(intent,current))){this.finishApplying(intent);return;}
        // A write receipt can precede the host's next published snapshot. Keep
        // the same draft until that readback; one bounded wait never resends it.
        this.confirming=true;this.notify();
        if(this.applying===intent)this.confirmationTimer=this.deps.schedule(()=>this.finishApplying(intent),5000);
      },()=>this.finishApplying(intent));
    }, this.deps.delayMs ?? 3000);
  }
  choose(field: string, value: PerformanceValue, expected: View) {
    if (this.disposed||this.applying) return;
    if (!/^[a-zA-Z][a-zA-Z0-9]{0,63}$/.test(field) ||
        typeof value === 'number' && !Number.isFinite(value)) return;
    const current = this.deps.current();
    if (!this.valid(expected, { [field]: value }, current)) return;
    const previous = this.pending && this.deps.sameSource(this.pending.view, current) ? this.pending.patch : {};
    const patch = { ...previous, [field]: value };
    if (value === this.deps.value(field, current)) delete patch[field];
    if (this.timer !== null) this.deps.cancel(this.timer);
    this.timer = null;
    if (!Object.keys(patch).length) { this.pending = null; this.notify(); return; }
    if (!this.deps.allowed(patch, current)) { this.pending = null; this.notify(); return; }
    const intent = { view: structuredClone(current), patch };
    this.pending = intent;
    if(!this.editing)this.arm(intent);
    this.notify();
  }
  cancel() {
    this.editing=false;
    if(this.confirmationTimer!==null)this.deps.cancel(this.confirmationTimer);
    this.confirmationTimer=null;this.confirming=false;
    if (this.timer !== null) this.deps.cancel(this.timer);
    this.timer = null;
    if (this.pending||this.applying) { this.pending=null;this.applying=null;this.notify(); }
  }
  dispose() { this.disposed = true; this.cancel(); this.listeners.clear(); }
}
