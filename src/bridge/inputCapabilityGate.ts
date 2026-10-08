export type InputCapabilityVisibility = 'hidden' | 'disabled' | 'available';
export interface InputCapabilityInputs {
  assetIdentityMatch: boolean;
  hcInputIsolationProven: boolean;
  descriptorVerified: boolean;
  hidhideRecoveryVerified: boolean;
  physicalOwnerContractClosed: boolean;
  realRuntimeAuthorized: boolean;
}
export interface InputCapabilityDecision {
  visibility: InputCapabilityVisibility;
  status: 'UNENCLOSED' | 'READY';
  reasons: string[];
}

/** Centralized fail-closed capability decision for future pages/Host routes. */
export function evaluateInputCapability(input: InputCapabilityInputs): InputCapabilityDecision {
  const reasons: string[] = [];
  if (!input.assetIdentityMatch) reasons.push('asset-mismatch');
  if (!input.hcInputIsolationProven) reasons.push('hc-isolation-unproven');
  if (!input.descriptorVerified) reasons.push('descriptor-mismatch');
  if (!input.hidhideRecoveryVerified) reasons.push('visibility-restore-unverified');
  if (!input.physicalOwnerContractClosed) reasons.push('owner-contract-unclosed');
  if (!input.realRuntimeAuthorized) reasons.push('runtime-authorization-missing');
  if (reasons.length === 0) return { visibility: 'available', status: 'READY', reasons: [] };
  const hardIdentityFailure = reasons.includes('asset-mismatch') || reasons.includes('hc-isolation-unproven');
  return { visibility: hardIdentityFailure ? 'hidden' : 'disabled', status: 'UNENCLOSED', reasons };
}
