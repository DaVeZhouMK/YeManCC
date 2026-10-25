// Actual before/after projections, with deterministic cross-device inputs.
// No native readers, hardware, app restart or settings writes.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import { createRequire } from 'node:module';
import { pathToFileURL } from 'node:url';
const root = process.cwd();
const out = path.resolve(process.env.YMCC_UNIFIED_PROJECTION_OUT || '../../../Build/Tasks/YMCC-Steam-HUD-Probe/unified-internal-data-20261017/projection');
fs.mkdirSync(out, { recursive: true });
const { build } = createRequire(root + '/package.json')('esbuild');
const files = ['monitorHudModel', 'monitorTopProjection'];
const sources = Object.fromEntries(files.map(name => [path.resolve('src/bridge/' + name + '.ts'), fs.readFileSync('tools/fixtures/unified_internal_' + name + '_before_20261017.ts', 'utf8')]));
const pins = {};
async function load(side) {
  const result = await build({ stdin: { contents: "export * from './src/bridge/monitorHudModel';export * from './src/bridge/monitorTopProjection';export * from './src/bridge/monitorCoreTiles';", resolveDir: root, loader: 'ts' }, bundle: true, platform: 'node', format: 'esm', write: false, metafile: true,
    plugins: [{ name: 'frozen-before', setup(b) { if (side === 'before') b.onLoad({ filter: /\.ts$/ }, a => sources[a.path] === undefined ? null : { contents: sources[a.path], loader: 'ts', resolveDir: path.dirname(a.path) }); } }] });
  const file = path.join(out, side + '.mjs'); fs.writeFileSync(file, result.outputFiles[0].text);
  pins[side] = {};
  for (const input of Object.keys(result.metafile.inputs)) {
    if (!fs.existsSync(input)) continue;
    const bytes = side === 'before' && sources[path.resolve(input)] !== undefined ? sources[path.resolve(input)] : fs.readFileSync(input);
    pins[side][input] = crypto.createHash('sha256').update(bytes).digest('hex');
  }
  return import(pathToFileURL(file).href + '?' + Date.now());
}
const before = await load('before'), after = await load('after');
const now = 1710000000000;
function base() {
  return { schema: 1, source: 'snapshot', session: 7, relayEpoch: 1, generation: 9, seq: 11, collectedAt: now, heartbeatAt: now, hwinfoOkAt: now, hwDown: false, topActive: true, fpsActive: true,
    top: { ts: now, tdpW: 30, freqMhz: 4100, tempC: 75, cpuUsage: 35, cpuUsageValid: true, cpuUsageTs: now, cpuPowerValid: true, cpuClockValid: true, cpuTempValid: true, ac: 0, hasBattery: true,
      batteryPercent: 72, batteryStatusTs: now, chargeW: -42, batteryPowerValid: true, batteryPowerTs: now, batteryPowerEstimated: true, batteryPowerSource: 'package-power-model', remainMin: 75, batteryTimeValid: true, batteryTimeTs: now, batteryTimeEstimated: true,
      coreUsage: Array.from({ length: 128 }, (_, i) => i % 101), coreLabels: Array.from({ length: 128 }, (_, i) => 'CPU' + i), coreUsageTs: now,
      gpuName: 'GPU #0 / Nvidia 5080', gpuSelectionPresent: true, gpuSelectionPinned: true, gpuUsagePct: 90, gpuPowerW: 140, gpuClockMhz: 2200, gpuVoltageV: .9, cpuVoltageV: 1.1, gpuTempC: 70,
      gpuMemoryUsedMb: 6000, gpuMemoryAvailableMb: 10000, gpuMemoryTotalMb: 16000, gpuMemoryLoadPct: 37.5, physicalMemoryUsedMb: 15000, physicalMemoryAvailableMb: 17000, physicalMemoryTotalMb: 32000, physicalMemoryLoadPct: 46.875,
      networkDownMBps: 2, networkUpMBps: .3, fileSystemSource: 'windows-filesystem-disk-activity', fileSystemTs: now, fileReadMBps: 10, fileWriteMBps: 2,
      fpsLimitValid: true, fpsLimitSource: 'rtss-global', fpsLimitTs: now, fpsLimit: 120, fanEnabled: true, fanSpeedValid: true, fanSpeedSource: 'ymcc-fan-target', fanSpeedTs: now, fanSpeedPercent: 76,
      thermalThrottleFound: true, thermalThrottleMax: 0, virtualMemoryCommittedFound: true, virtualMemoryCommittedMb: 17000, virtualMemoryLoadFound: true, virtualMemoryLoadPct: 50 },
    fps: { ts: now, fps: 117, fpsSource: 'hwinfo-presented', fpsObservedAt: now, fpsValidUntil: now + 5000, fpsPollPeriodMs: 2000, fps1: 85, fps1Source: 'hwinfo-presented-1percent-low', fps1ObservedAt: now, fps1ValidUntil: now + 5000, fps1PollPeriodMs: 2000,
      gpu: 90, packagePower: 30, game: 'Cyberpunk2077.exe', pid: 2077, processCreated: '2026-10-17T00:00:00Z', frametime: 8.54, frameTimeSource: 'hwinfo-presented-avg', frameTimeTs: now, frameTimeValidUntil: now + 5000, frameTimePollPeriodMs: 2000, frameTimes: Array.from({ length: 120 }, (_, i) => 8 + (i % 9) / 10) } };
}
let comparisons = 0;
function compare(input, time, label, tdp = null) {
  const original = structuredClone(input);
  assert.deepStrictEqual(after.projectTopMonitorView(input, time), before.projectTopMonitorView(input, time), 'top: ' + label);
  assert.deepStrictEqual(after.hudFromUniformSnapshot(input, time, tdp, false), before.hudFromUniformSnapshot(input, time, tdp, false), 'HUD: ' + label);
  assert.deepStrictEqual(input, original, 'source mutation: ' + label);
  ++comparisons;
}
const times = [-1, 0, 100, 4999, 5000, 5999, 6000, 6001, 20000].map(age => now + age);
const values = [undefined, null, false, true, -100, -1, -.01, 0, .05, 1, 76, 100, 101, 1000001, NaN, Infinity, -Infinity, '', '75', {}, [], now - 6000, now - 5999, now, now + 1];
for (const time of times) compare(base(), time, 'base-' + time);
compare(null, now, 'null');
for (const scope of ['top', 'fps']) {
  for (const key of Object.keys(base()[scope])) {
    for (const value of values) {
      const input = base(); input[scope][key] = value;
      // Malformed curve/label shapes stay within existing defensive public paths.
      compare(input, now, scope + '.' + key + '=' + String(value));
    }
    const missing = base(); delete missing[scope][key]; compare(missing, now, scope + '.' + key + '-absent');
  }
}
for (const time of times) for (const source of ['hwinfo-presented', 'rtss', undefined]) for (const mode of ['desktop', 'amd-uma', 'mixed-hwdown', 'battery-only', 'fan-only', 'cores-only', 'filesystem-only', 'legacy', 'stopped']) {
  const input = base(); input.fps.fpsSource = source;
  if (mode === 'desktop') { input.top.hasBattery = false; input.top.batteryPercent = -1; }
  if (mode === 'amd-uma') { input.top.gpuName = 'GPU #1 / AMD Radeon'; input.top.gpuMemoryTotalMb = input.top.gpuMemoryAvailableMb = input.top.gpuMemoryLoadPct = null; input.top.gpuMemorySource = 'hwinfo-d3d-dedicated'; }
  if (mode === 'mixed-hwdown') { input.hwDown = input.top.hwDown = true; input.top.ts = now - 10000; }
  if (mode.endsWith('-only')) { input.hwDown = true; input.fps = null; input.top.ts = now - 10000; for (const key of ['cpuUsageTs','coreUsageTs','batteryStatusTs','batteryPowerTs','batteryTimeTs','fpsLimitTs','fanSpeedTs','fileSystemTs']) input.top[key] = 0;
    if (mode === 'battery-only') input.top.batteryStatusTs = input.top.batteryPowerTs = input.top.batteryTimeTs = now;
    if (mode === 'fan-only') input.top.fanSpeedTs = now;
    if (mode === 'cores-only') input.top.coreUsageTs = now;
    if (mode === 'filesystem-only') input.top.fileSystemTs = now;
  }
  if (mode === 'legacy') for (const key of Object.keys(input.top)) if (/Valid$|Ts$|Source$/.test(key)) delete input.top[key];
  if (mode === 'stopped') { input.top = null; input.fps = null; input.heartbeatAt = input.hwinfoOkAt = null; }
  compare(input, time, mode + '/' + source + '/' + time, { status: 'applied', proof: 'write-ack', mode: 'dc', watts: 30 });
}
// Access instrumentation proves the internal path avoids curve/core element work.
const work = {};
for (const [side, mod] of Object.entries({ before, after })) {
  const input = base(), counts = { frameSamples: 0, coreSamples: 0, memoryFields: 0 };
  for (const [key, counter] of [['coreUsage', 'coreSamples']]) input.top[key] = new Proxy(input.top[key], { get(a, k, r) { if (/^\d+$/.test(String(k))) ++counts[counter]; return Reflect.get(a, k, r); } });
  input.fps.frameTimes = new Proxy(input.fps.frameTimes, { get(a, k, r) { if (/^\d+$/.test(String(k))) ++counts.frameSamples; return Reflect.get(a, k, r); } });
  for (const key of ['gpuMemoryUsedMb','gpuMemoryAvailableMb','gpuMemoryTotalMb','gpuMemoryLoadPct','physicalMemoryUsedMb','physicalMemoryAvailableMb','physicalMemoryTotalMb','physicalMemoryLoadPct']) { const v = input.top[key]; Object.defineProperty(input.top, key, { enumerable: true, get() { ++counts.memoryFields; return v; } }); }
  mod.projectTopMonitorView(input, now); work[side] = counts;
}
assert(work.before.coreSamples >= 128 && work.before.frameSamples >= 120);
assert.equal(work.after.coreSamples, 0); assert.equal(work.after.frameSamples, 0); assert.equal(work.after.memoryFields, 8);
// Shared projections still process all HUD data; explicit cross-device parity is above.
const bench = [];
let sink = 0;
function timing(mod, kind, iterations, scenario) {
  const input = base(); input.top.coreUsage.length = input.top.coreLabels.length = scenario.cores; if (!scenario.frames) input.fps = null; input.top.coreUsage = mod.freezeCoreUsageValues(input.top.coreUsage); input.top.coreLabels = mod.freezeCoreUsageLabels(input.top.coreLabels, 128); if (input.fps) Object.freeze(input.fps.frameTimes);
  const run = kind === 'top' ? () => mod.projectTopMonitorView(input, now) : () => mod.hudFromUniformSnapshot(input, now, null, false);
  for (let i = 0; i < 3000; ++i) sink += run()?.stale === true ? 1 : 0;
  const start = process.hrtime.bigint(); for (let i = 0; i < iterations; ++i) { const value = run(); sink += kind === 'top' ? value.tempC : value.values.cpuTemp; }
  return Number(process.hrtime.bigint() - start) / iterations;
}
const scenarios = [{ id: 'desktop-16', cores: 16, frames: false }, { id: 'game-16', cores: 16, frames: true }, { id: 'bounded-128', cores: 128, frames: true }];
for (let round = 0; round < 9; ++round) for (const scenario of scenarios) for (const kind of ['top', 'hud']) {
  const row = { round, scenario: scenario.id, kind }; for (const side of round % 2 ? ['after', 'before'] : ['before', 'after']) row[side] = timing(side === 'after' ? after : before, kind, 18000, scenario); bench.push(row);
}
const median = a => [...a].sort((a, b) => a - b)[Math.floor(a.length / 2)];
const timings = Object.fromEntries(scenarios.map(scenario => [scenario.id, Object.fromEntries(['top', 'hud'].map(kind => { const a = bench.filter(x => x.scenario === scenario.id && x.kind === kind); return [kind, { pairedMedianRatio: median(a.map(x => x.after / x.before)), improvedPairs: a.filter(x => x.after < x.before).length, pairs: a.length }]; }))]));
const result = { ok: true, comparisons, work, timings, bench, sourcePins: pins, sink, scope: 'Actual production projection functions, deterministic snapshots and isolated function timing. No live process CPU utilization measurement or game test.' };
fs.writeFileSync(path.join(out, 'RESULT.json'), JSON.stringify(result, null, 2));
console.log(JSON.stringify({ ok: true, comparisons, work, timings }, null, 2));
