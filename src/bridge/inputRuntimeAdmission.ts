import { evaluateInputCapability, type InputCapabilityInputs, type InputCapabilityDecision } from './inputCapabilityGate';

export interface RuntimeAdmissionDecision extends InputCapabilityDecision { phase: 'disabled' | 'verified'; admitted: boolean; }

/** Gate used by a future InputHost supervisor before allocating a session. */
export function evaluateRuntimeAdmission(capabilities: InputCapabilityInputs): RuntimeAdmissionDecision {
  const decision = evaluateInputCapability(capabilities);
  if (decision.status !== 'READY') return { ...decision, phase: 'disabled', admitted: false };
  return { ...decision, phase: 'verified', admitted: true };
}
