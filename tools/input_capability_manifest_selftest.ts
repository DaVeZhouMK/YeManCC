import fs from 'node:fs';
import path from 'node:path';
import { evaluateInputCapability } from '../src/bridge/inputCapabilityGate';

const manifestPath = path.resolve('tools/input-capability-manifest-20260903.json');
const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8')) as {
  schemaVersion: number; closure: string; realRuntimeAuthorized: boolean;
  capabilities: Record<string, boolean>; runtimeVisibility: string; updaterExcluded: boolean;
};
if (manifest.schemaVersion !== 1 || manifest.closure !== 'UNENCLOSED' || manifest.runtimeVisibility !== 'hidden' || !manifest.updaterExcluded) {
  throw new Error('capability manifest must remain enclosed and updater-excluded');
}
const decision = evaluateInputCapability({
  assetIdentityMatch: true,
  hcInputIsolationProven: manifest.capabilities.hcInputIsolation === true,
  descriptorVerified: manifest.capabilities.hidMaestroX360CreateSubmitRemove === true && manifest.capabilities.physicalControllerAcquisition === true,
  hidhideRecoveryVerified: manifest.capabilities.physicalHidHideSuppression === true,
  physicalOwnerContractClosed: manifest.capabilities.mockOwnership === true && manifest.capabilities.gameConsumerObserved === true,
  realRuntimeAuthorized: manifest.realRuntimeAuthorized === true,
});
if (decision.visibility !== 'hidden' || decision.status !== 'UNENCLOSED' || !decision.reasons.includes('hc-isolation-unproven')) {
  throw new Error(`manifest unexpectedly admitted runtime: ${JSON.stringify(decision)}`);
}
console.log(`input capability manifest selftest: PASS (${decision.visibility}/${decision.status})`);
