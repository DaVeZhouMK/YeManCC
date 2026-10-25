import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { pathToFileURL } from 'node:url';

const root = process.cwd();
const task = path.resolve('../../../Build/Tasks/YMCC-Steam-HUD-Probe/monitor-read-shell-share-20261017');
const out = path.join(task, 'tests');
fs.mkdirSync(out, { recursive: true });
const { build } = createRequire(path.join(root, 'package.json'))('esbuild');
const beforePath = path.join(task, 'before-monitorData.ts');
const beforeSource = fs.readFileSync(beforePath, 'utf8');
const ipcMock = `export async function invoke(command){return globalThis.__monitorReadInvoke(command)} export function on(){return ()=>{}}`;
const apiMock = `export const fs={readTextFile:async()=>''};`;

async function bundle(side) {
  const before = side === 'before';
  const result = await build({
    stdin: { resolveDir: root, loader: 'ts', contents: before ? `export * from 'monitorData-before';` : `export * from './src/bridge/monitorData.ts';` },
    bundle: true, write: false, platform: 'node', format: 'esm',
    plugins: [{ name: 'monitor-read-fixture', setup(api) {
      api.onResolve({ filter: /^monitorData-before$/ }, () => ({ path: beforePath, namespace: 'monitor-before' }));
      api.onLoad({ filter: /.*/, namespace: 'monitor-before' }, () => ({ contents: beforeSource, loader: 'ts', resolveDir: path.join(root, 'src/bridge') }));
      api.onResolve({ filter: /^\.\/(ipc|api)$/ }, args => ({ path: args.path, namespace: 'monitor-mock' }));
      api.onLoad({ filter: /.*/, namespace: 'monitor-mock' }, args => ({ contents: args.path === './ipc' ? ipcMock : apiMock, loader: 'ts' }));
    } }],
  });
  const file = path.join(out, `${side}-production.mjs`);
  fs.writeFileSync(file, result.outputFiles[0].text);
  return import(pathToFileURL(file) + `?${Date.now()}-${side}`);
}

function fixture() {
  let seq = 0;
  const now = 200000;
  const top = JSON.stringify({
    ts: now, hwDown: false, cpuUsage: 20, cpuUsageValid: true, cpuUsageTs: now,
    coreUsage: Array.from({ length: 128 }, (_, i) => i % 101),
    coreLabels: Array.from({ length: 128 }, (_, i) => `CPU ${i}`), coreUsageTs: now,
    hasBattery: false, gpuSelectionPresent: false,
  });
  const fps = JSON.stringify({ ts: now, fps: 120, fps1: 90, pid: 2077, processCreated: '123456789', game: 'fixture', gpu: 80, packagePower: 30,
    fpsSource: 'rtss', fpsObservedAt: now, fpsValidUntil: now + 5000, fpsPollPeriodMs: 0,
    frametime: 11, frameTimeSource: 'rtss-current-frame', frameTimeTs: now,
    frameTimeValidUntil: now + 5000, frameTimePollPeriodMs: 0, frameTimes: Array.from({ length: 120 }, (_, i) => 8 + i % 4) });
  return { now, next(command) {
    if (command === 'monitor.snapshot') return { schema: 1, available: true, session: 1, relayEpoch: 1,
      generation: 1, seq: ++seq, collectedAt: now + seq, heartbeatAt: now + seq, hwinfoOkAt: now + seq,
      topActive: true, fpsActive: true, hwDown: false, top, fps };
    if (command === 'monitor.start' || command === 'monitor.stop' || command === 'monitor.takeover') return { ok: true, cleaned: true };
    return {};
  } };
}

async function exercise(module) {
  const source = fixture();
  globalThis.__monitorReadInvoke = async command => source.next(command);
  const release = module.acquireMonitorDemand('read-test', { top: true, fps: true });
  const originalClone = globalThis.structuredClone;
  let cloneCalls = 0;
  globalThis.structuredClone = value => { ++cloneCalls; return originalClone(value); };
  try {
    const first = await module.readMonitorSnapshot();
    const second = await module.readMonitorSnapshot();
    assert(first && second && first.top && second.top && first.fps && second.fps);
    const beforeMutation = second.top.cpuUsage;
    first.top.cpuUsage = 99;
    return {
      cloneCalls, outerFresh: first !== second, topFresh: first.top !== second.top, fpsFresh: first.fps !== second.fps,
      coreUsageShared: first.top.coreUsage === second.top.coreUsage, coreLabelsShared: first.top.coreLabels === second.top.coreLabels,
      coreUsageFrozen: Object.isFrozen(first.top.coreUsage), frameTimesShared: first.fps.frameTimes === second.fps.frameTimes,
      frameTimesFrozen: Object.isFrozen(first.fps.frameTimes), scalarIsolation: second.top.cpuUsage === beforeMutation,
      values: { first: first.top.cpuUsage, second: second.top.cpuUsage, seq: second.seq },
    };
  } finally { globalThis.structuredClone = originalClone; release(); delete globalThis.__monitorReadInvoke; }
}

const before = await exercise(await bundle('before'));
const after = await exercise(await bundle('after'));
assert.equal(before.outerFresh, true); assert.equal(before.topFresh, true); assert.equal(before.fpsFresh, true); assert.equal(before.scalarIsolation, true);
assert.equal(after.outerFresh, true); assert.equal(after.topFresh, true); assert.equal(after.fpsFresh, true);
assert.equal(after.coreUsageShared, true); assert.equal(after.coreLabelsShared, true); assert.equal(after.coreUsageFrozen, true);
assert.equal(after.frameTimesShared, true); assert.equal(after.frameTimesFrozen, true); assert.equal(after.scalarIsolation, true); assert.equal(after.values.second, 20);
assert(after.cloneCalls < before.cloneCalls, `clone calls ${before.cloneCalls} -> ${after.cloneCalls}`);
const result = { ok: true, before, after, scope: 'Actual production monitorData singleton read path with pre-change/current clone shells. Native, IPC, hardware and elapsed CPU measurements are not used.' };
fs.writeFileSync(path.join(task, 'READ-SHELL-RESULT.json'), JSON.stringify(result, null, 2));
console.log(JSON.stringify(result, null, 2));