import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {sourceInputs as baseSources} from '../controller-msi-late-hide-pending/adapter.mjs';
import {sourceInputs as boundSources,artifactInputs as boundArtifacts} from '../controller-cold-msi-archive/adapter.mjs';

export const ID='controller.focus-recovery';
export const MODULE='tools/ymcc_sim/global/controller-focus-recovery';
export const REPO=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
export const MANAGED_DLL='G:/YeManCC-Work/Build/Validation/VirtualController-Lifecycle-20261019/integration/managed/env-return200-r4/bin/Release/net10.0/Fixture.dll';
// Reuse the existing SHA-bound SDK-free managed environment authority only.
// This scenario runs neither the archived MSI suite nor another lifecycle model.
export function sourceInputs(repo=REPO){return [...new Set([...baseSources(repo),...boundSources(repo),MODULE+'/adapter.mjs',MODULE+'/README.md'])].sort();}
export function artifactInputs(repo=REPO){return boundArtifacts(repo);}
export function getFocusRecoveryAdapters(repo=REPO){
 let inputs;
 try{
  const runner=fs.readFileSync(path.join(repo,'tools/input_coordinator_lifecycle_selftest.mjs'),'utf8');
  if(!runner.includes('focus-recovery')||!fs.statSync(path.join(repo,'tools/fixtures/input_coordinator_lifecycle/focus_recovery_cases.inc')).isFile())throw Error('FOCUS_RECOVERY_ACTUAL_CONSUMER_NOT_WIRED');
  inputs={evidenceInputs:sourceInputs(repo),artifactInputs:artifactInputs(repo)};
 }catch(error){inputs={evidenceInputs:[MODULE+'/adapter.mjs',MODULE+'/README.md'],artifactInputs:[],blockedReason:'FOCUS_RECOVERY_INPUTS_UNAVAILABLE: '+error.message};}
 return [{id:ID,domain:'controller',title:'Actual gamepad tick and private wire: focus survives transport-fault recovery',kind:'node',path:'tools/input_coordinator_lifecycle_selftest.mjs',args:['--out','{outDir}/focus-recovery','--managed-dll',MANAGED_DLL,'--suite','focus-recovery'],profiles:['full'],timeoutMs:900000,scope:'behavioral-mock',...inputs,resultFiles:['focus-recovery/RESULT.json'],boundary:'Existing complete native TU, unchanged inputCaptureOnGamepad, output serializer and R3 inspection/recovery. Actual owned pipe disconnect and SDK-free managed session; cached radial owner is the explicit UI/OS leaf. Tests persistent focus and focus entered after recovery queues. No normal product Main, real HID SDK, physical motion sensor, real device/driver/service, actual game consumption or full hardware acceptance.'}];
}
