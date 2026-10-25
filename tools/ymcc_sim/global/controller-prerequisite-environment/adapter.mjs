import fs from 'node:fs';
import path from 'node:path';
import {REPO,toolchainFiles} from '../inputhost-protocol/adapter.mjs';
const modulePath='tools/ymcc_sim/global/controller-prerequisite-environment';
export function getPrerequisiteEnvironmentAdapters(repo=REPO){
 const python=JSON.parse(fs.readFileSync(path.join(repo,modulePath,'toolchain-inputs.json'),'utf8')).python;
 return [{
  id:'controller.prerequisite-environment',domain:'controller',
  title:'Actual startup package review: inventory schemas, future versions, coexisting unknown packages and maintenance refusal',
  kind:'node',path:modulePath+'/run.mjs',args:['--out','{outDir}/prerequisite'],timeoutMs:180000,
  profiles:['desktop','full'],scope:'behavioral-mock',
  evidenceInputs:[
   'GamepadPrerequisites/Program.cs','GamepadPrerequisites/AutoSetup.cs','GamepadPrerequisites/HidMaestroPackagePreparation.cs',
   'native/ProcessEfficiency.cs','tools/gamepad_package_preparation_selftest.py',
   'tools/fixtures/gamepad_package_preparation/Fixture.cs','tools/fixtures/gamepad_package_preparation/LegacyFixture.cs',
   'tools/fixtures/gamepad_package_preparation/legacy/Program.cs.txt',
   'tools/fixtures/gamepad_package_preparation/legacy/AutoSetup.cs.txt',
   'tools/fixtures/gamepad_package_preparation/legacy/provenance.json',
   ...['adapter.mjs','run.mjs','toolchain-inputs.json','README.md'].map(f=>modulePath+'/'+f)],
  artifactInputs:[...toolchainFiles(repo),python,path.join(repo,'deps/hidmaestro/HIDMaestro.Core.dll')],
  resultFiles:['prerequisite/global-result.json'],
  boundary:'Actual Program review/dispatch and package Execute with private XML/staging and explicit owner/client facts. Pinned official SDK is loaded only for metadata/resources; no SDK methods/controllers, normal setup, driver/device/registry/certificate changes. Original source readiness fault and compiled semantic mutations must fail. Store supply is never live device binding or game-consumption proof.'
 }];
}
