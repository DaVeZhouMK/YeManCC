/** T6 mock: HC IController rumble/haptic lifecycle. No HID or EC writes. */
export type FeedbackCapability = 'none' | 'rumble';
export type FeedbackPhase = 'disabled' | 'capability-verified' | 'target-ready' | 'command-admitted' | 'active' | 'quiescing' | 'safe-zero' | 'released';
export type HapticStrength = 'low' | 'medium' | 'high';

export interface FeedbackCommand {
  targetId: string;
  epoch: number;
  configRevision: number;
  largeMotor: number;
  smallMotor: number;
  delayMs: number;
}

export interface FeedbackTrace {
  phase: FeedbackPhase;
  capability: FeedbackCapability;
  lastVibration: { large: number; small: number };
  lastHapticReleased: boolean;
  cancelledPrevious: boolean;
  safeZeroSent: boolean;
  transportWrites: number;
  reason: string | null;
}

const HAPTIC_DELAY: Record<HapticStrength, number> = { low: 40, medium: 80, high: 120 };
// HC IController.RumbleHaptic routes all right-side face/shoulder/start/
// stick/touchpad directional buttons to the small motor.
const RIGHT_BUTTONS = new Set([
  'b1', 'b2', 'b3', 'b4', 'r1', 'r2soft', 'start',
  'rightstickclick', 'rightsticktouch', 'rightpadclick',
  'rightpadclickup', 'rightpadclickdown', 'rightpadclickleft', 'rightpadclickright',
]);
const BOTH_BUTTONS = new Set(['touchpadtouch', 'leftpadtouch', 'rightpadtouch']);

export class FeedbackParticipantMock {
  private phase: FeedbackPhase = 'disabled';
  private capability: FeedbackCapability = 'none';
  private targetId: string | null = null;
  private epoch = 0;
  private configRevision = 0;
  private lastVibration = { large: 0, small: 0 };
  private lastHapticReleased = false;
  private cancelledPrevious = false;
  private safeZeroSent = false;
  private active = false;
  readonly transportWrites = 0; // mock never writes HID

  verify(capability: FeedbackCapability, targetId: string, epoch: number, configRevision: number): FeedbackTrace {
    this.capability = capability;
    this.targetId = targetId;
    this.epoch = epoch;
    this.configRevision = configRevision;
    this.phase = capability === 'rumble' ? 'capability-verified' : 'disabled';
    this.cancelledPrevious = false;
    this.safeZeroSent = false;
    this.active = false;
    this.lastVibration = { large: 0, small: 0 };
    this.lastHapticReleased = false;
    return this.snapshot(capability === 'rumble' ? null : 'capability-missing');
  }

  ready(): FeedbackTrace {
    if (this.phase !== 'capability-verified') return this.snapshot('not-verified');
    this.phase = 'target-ready';
    return this.snapshot(null);
  }

  rumble(command: FeedbackCommand): FeedbackTrace {
    if (this.capability !== 'rumble') return this.snapshot('capability-missing');
    if (this.phase === 'disabled' || this.phase === 'released' || this.phase === 'quiescing') return this.snapshot('admission-stopped');
    if (command.targetId !== this.targetId || command.epoch !== this.epoch || command.configRevision !== this.configRevision) return this.snapshot('stale-command');
    if (![command.largeMotor, command.smallMotor, command.delayMs].every((value) => Number.isFinite(value) && value >= 0)) return this.snapshot('range-invalid');
    this.cancelledPrevious = this.active;
    this.phase = 'active';
    this.active = true;
    this.safeZeroSent = false;
    this.lastVibration = { large: Math.min(255, command.largeMotor), small: Math.min(255, command.smallMotor) };
    return this.snapshot(null);
  }

  setHaptic(strength: HapticStrength, button: string, released = false): FeedbackTrace {
    const delay = HAPTIC_DELAY[strength] ?? HAPTIC_DELAY.low;
    const key = button.toLowerCase();
    // The HC base implementation forwards this flag to controller-specific
    // overrides.  The mock has no controller-specific transport, but keeps
    // the flag in its trace so it cannot silently disappear from the contract.
    this.lastHapticReleased = released;
    if (RIGHT_BUTTONS.has(key)) return this.rumble({ targetId: this.targetId || '', epoch: this.epoch, configRevision: this.configRevision, largeMotor: 0, smallMotor: 255, delayMs: delay });
    if (BOTH_BUTTONS.has(key)) return this.rumble({ targetId: this.targetId || '', epoch: this.epoch, configRevision: this.configRevision, largeMotor: 255, smallMotor: 255, delayMs: delay / 4 });
    return this.rumble({ targetId: this.targetId || '', epoch: this.epoch, configRevision: this.configRevision, largeMotor: 255, smallMotor: 0, delayMs: delay });
  }

  stopRumble(): FeedbackTrace {
    this.active = false;
    this.lastVibration = { large: 0, small: 0 };
    this.lastHapticReleased = false;
    this.safeZeroSent = true;
    if (this.phase === 'active' || this.phase === 'command-admitted' || this.phase === 'target-ready') this.phase = 'safe-zero';
    return this.snapshot(null);
  }

  suspend(): FeedbackTrace {
    this.phase = 'quiescing';
    this.active = false;
    this.lastVibration = { large: 0, small: 0 };
    this.lastHapticReleased = false;
    this.safeZeroSent = true;
    this.phase = 'safe-zero';
    return this.snapshot(null);
  }

  release(): FeedbackTrace {
    this.stopRumble();
    this.phase = 'released';
    this.targetId = null;
    return this.snapshot(null);
  }

  snapshot(reason: string | null): FeedbackTrace {
    return {
      phase: this.phase,
      capability: this.capability,
      lastVibration: { ...this.lastVibration },
      lastHapticReleased: this.lastHapticReleased,
      cancelledPrevious: this.cancelledPrevious,
      safeZeroSent: this.safeZeroSent,
      transportWrites: this.transportWrites,
      reason,
    };
  }
}
