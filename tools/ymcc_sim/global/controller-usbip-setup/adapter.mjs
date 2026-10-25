import {toolchainFiles} from '../inputhost-protocol/adapter.mjs';
export const MODULE='tools/ymcc_sim/global/controller-usbip-setup';
export function getUsbipSetupAdapters(repo) {
 const shared=[MODULE+'/adapter.mjs',MODULE+'/README.md','tools/ymcc_sim/global/inputhost-protocol/adapter.mjs','tools/ymcc_sim/global/inputhost-protocol/toolchain-inputs.json'];
 const artifacts=toolchainFiles(repo);
 return [{
  id:'controller.usbip-backend-preparation',domain:'controller',title:'Actual USBIP backend preparation and advisory factory ordering with compiled callback and gating faults',
  kind:'node',path:'tools/usbip_backend_preparation_selftest.mjs',args:['--out','{outDir}/usbip-backend'],timeoutMs:180000,profiles:['desktop','full'],scope:'behavioral-mock',
  evidenceInputs:[...shared,'tools/usbip_backend_preparation_selftest.mjs','tools/fixtures/usbip_backend_preparation/Fixture.cs','InputHost/Program.cs'],artifactInputs:artifacts,resultFiles:['usbip-backend/RESULT.json'],
  boundary:'Actual three preparation/factory consumer bodies, inert SDK and diagnostic callback terminals. No SDK assembly, product Main, device, driver, installation, sleep or game-consumption evidence.'
 },{
  id:'controller.usbip-setup-evidence',domain:'controller',title:'Bounded USBIP lifecycle evidence with inert registry and CM leaves, private files and log faults',
  kind:'node',path:'tools/usbip_setup_evidence_selftest.mjs',args:['--out','{outDir}/usbip-evidence'],timeoutMs:180000,profiles:['desktop','full'],scope:'behavioral-mock',
  evidenceInputs:[...shared,'tools/usbip_setup_evidence_selftest.mjs','tools/fixtures/usbip_setup_evidence/Fixture.cs','tools/fixtures/usbip_setup_evidence/README.md','InputHost/UsbipSetupEvidence.cs','InputHost/XboxSdkDiagnostics.cs'],artifactInputs:artifacts,resultFiles:['usbip-evidence/RESULT.json'],
  boundary:'Actual evidence sink with declared read-only OS leaves replaced. Files and hashes are private. Driver binding remains a registered observation and SYS bytes remain disk-only, never a loaded-memory or attach claim.'
 }];
}
