import { evaluateInputCapability } from '../src/bridge/inputCapabilityGate';
const base = { assetIdentityMatch: true, hcInputIsolationProven: true, descriptorVerified: true, hidhideRecoveryVerified: true, physicalOwnerContractClosed: true, realRuntimeAuthorized: true };
const ready = evaluateInputCapability(base);
if (ready.status !== 'READY' || ready.visibility !== 'available' || ready.reasons.length) throw new Error('ready gate failed');
const hidden = evaluateInputCapability({ ...base, hcInputIsolationProven: false });
if (hidden.status !== 'UNENCLOSED' || hidden.visibility !== 'hidden' || !hidden.reasons.includes('hc-isolation-unproven')) throw new Error('hard gate failure not hidden');
const disabled = evaluateInputCapability({ ...base, descriptorVerified: false });
if (disabled.visibility !== 'disabled' || disabled.status !== 'UNENCLOSED') throw new Error('soft gate failure not disabled');
console.log('input capability gate selftest: PASS');
