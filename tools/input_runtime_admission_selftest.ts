import { evaluateRuntimeAdmission } from '../src/bridge/inputRuntimeAdmission';
const all = { assetIdentityMatch: true, hcInputIsolationProven: true, descriptorVerified: true, hidhideRecoveryVerified: true, physicalOwnerContractClosed: true, realRuntimeAuthorized: true };
const blocked = evaluateRuntimeAdmission({ ...all, hcInputIsolationProven: false });
if (blocked.admitted || blocked.phase !== 'disabled' || blocked.visibility !== 'hidden') throw new Error('blocked runtime was admitted');
const ready = evaluateRuntimeAdmission(all);
if (!ready.admitted || ready.phase !== 'verified' || ready.visibility !== 'available') throw new Error('ready runtime was not admitted');
console.log('input runtime admission selftest: PASS');
