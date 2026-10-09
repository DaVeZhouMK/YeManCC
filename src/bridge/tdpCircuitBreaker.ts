import {
  TdpCapabilityCircuitBreaker,
  type TdpCapabilityState,
  type TdpWriteResult,
} from '@/robust/repairModel';

// One process-wide gate covers manual TDP, AutoFloat, performance schedules,
// boot and wake re-apply. Native capability probing is intentionally not done
// here: a failed write is classified once and unsupported hardware is latched.
const breaker = new TdpCapabilityCircuitBreaker();

export function getTdpCapabilityState(): TdpCapabilityState {
  return breaker.getState();
}

export function resetTdpCapabilityState(): void {
  breaker.reset();
}

export function runTdpHardwareWrite(task: () => Promise<void>): Promise<TdpWriteResult> {
  return breaker.run(task);
}
