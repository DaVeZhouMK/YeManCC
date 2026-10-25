import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {toolchainFiles} from '../inputhost-protocol/adapter.mjs';

export const MODULE = 'tools/ymcc_sim/global/host-sdk-stop';
export const REPO = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
export const INPUTS = Object.freeze([
  'InputHost/Program.cs', 'InputHost/ScreenTouchpadReport.cs',
  'InputHost/HostControllerOutputStop.cs', 'InputHost/HostControllerIdleCtsRelease.cs',
  'tools/fixtures/hidmaestro/1.12.0/HMController.Dispose.cs',
  ...['binary-binding.json', 'HMController.Dispose.cs', 'HMController.StopThreads.cs',
    'HMController.StopWorkers.cs', 'HMController.CloseOutputCts.cs', 'HMController.OutputPollLoop.cs',
    'DeviceClock.Stop.cs', 'DeviceClock.Wake.cs', 'DeviceClock.Close.cs', 'DeviceClock.Loop.cs']
    .map(name => 'tools/fixtures/hidmaestro/2.0.0/' + name),
  'tools/ymcc_sim/global/inputhost-protocol/adapter.mjs',
  'tools/ymcc_sim/global/inputhost-protocol/toolchain-inputs.json',
  ...['adapter.mjs', 'run.mjs', 'README.md', 'PinnedSdkProof.cs', 'BclStopFixture.cs.in',
    'Legacy112StopFixture.cs.in', 'fixtures/HostControllerOutputStop.before.cs'].map(name => MODULE + '/' + name)
]);
export const artifacts = () => [...new Set([...toolchainFiles(REPO),
  path.join(REPO, 'deps/hidmaestro/HIDMaestro.Core.dll')])].sort();

// Exported for the main worker to register. No global catalog is edited here.
// Its outDir must remain under the workspace Build root.
export function getHostSdkStopAdapters() {
  return [{
    id: 'controller.host-sdk-stop', domain: 'controller',
    title: 'Pinned SDK stop ownership with actual Host helpers and real BCL workers',
    kind: 'node', path: MODULE + '/run.mjs',
    args: ['--evidence-dir', '{outDir}/host-sdk-stop'], timeoutMs: 240000,
    profiles: ['desktop', 'full'], scope: 'behavioral-mock',
    evidenceInputs: [...INPUTS], artifactInputs: artifacts(), resultFiles: ['host-sdk-stop/result.json'],
    boundary: 'Exact SDK 2.0 stop methods in fake controller/context; frozen 1.12 historical negative; actual Host helpers and owned BCL Thread/CTS/clock events. SDK bytes are metadata-only. No real SDK controller, IO, device, normal Host Main, or physical acceptance.'
  }];
}
