// Synthetic publisher packets only: real unified parser + HUD mapper, no hardware.
import assert from 'node:assert/strict';
import { createMonitorDataHub, type MonitorSnapshotView } from '@/bridge/monitorData';
import { hudFromUniformSnapshot, boundedHudData, HUD_METRICS, hudValue } from '@/bridge/monitorHudModel';
const now = 1791705600000;
const cases: string[] = [];
const keys = ['gpuTempC', 'gpuMemoryUsedMb', 'gpuMemoryTotalMb', 'physicalMemoryUsedMb', 'physicalMemoryTotalMb'] as const;
async function parse(extra: object) {
  let reads = 0;
  const commands: string[] = [];
  const hub = createMonitorDataHub({ now: () => now, setTimer: () => 1, clearTimer: () => {}, readTextFile: async () => { throw Error('Secondary reads forbidden'); }, invoke: async command => {
    commands.push(command);
    if (command === 'monitor.snapshot') return { schema: 1, available: true, session: 1, generation: 1, seq: ++reads, collectedAt: now, topActive: true, fpsActive: false, hwDown: false, top: JSON.stringify({ts:now-100,hwDown:false,...extra}), fps:null };
    return {ok:true,cleaned:true};
  }});
  const release = hub.acquire('metrics-consumer-test', {top:true});
  await new Promise(resolve => setImmediate(resolve));
  const [a,b] = await Promise.all([hub.read(),hub.read()]);
  assert.equal(reads,1);assert.deepEqual(a,b);
  assert(commands.every(c => ['monitor.start','monitor.stop','monitor.takeover','monitor.snapshot'].includes(c)));
  release();assert(a?.top);return a!;
}
async function check(name: string, fn: () => void | Promise<void>) { await fn(); cases.push(name); }
const complete = {gpuTempC:68,gpuMemoryUsedMb:8192,gpuMemoryTotalMb:12288,physicalMemoryUsedMb:16384,physicalMemoryTotalMb:32768};
await check('Optional native fields survive existing unified parser and concurrent reads still merge', async () => {
  const s=await parse(complete);for(const k of keys)assert.equal(s.top![k],complete[k]);
  const d=hudFromUniformSnapshot(s,now);assert.equal(d.source,'ymcc-uniform');assert.equal(d.values.gpuTemp,68);assert.equal(d.values.vramUsed,8);assert.equal(d.values.vramTotal,12);assert.equal(d.values.ramUsed,16);assert.equal(d.values.ramTotal,32);
});
await check('Legacy packet shape remains unchanged and missing extensions do not become 0', async () => {
  const s=await parse({});for(const k of keys)assert(!Object.hasOwn(s.top!,k));const d=hudFromUniformSnapshot(s,now);for(const k of ['gpuTemp','vramUsed','vramTotal','ramUsed','ramTotal'] as const){assert.equal(d.values[k],null);assert.equal(hudValue(d,k),'—');}
});
await check('Malformed optional values are unknown rather than string/bool/array-to-number coercion', async () => {
  const s=await parse({gpuTempC:'68',gpuMemoryUsedMb:false,gpuMemoryTotalMb:[8192],physicalMemoryUsedMb:-1,physicalMemoryTotalMb:'Infinity'});for(const k of keys)assert.equal(s.top![k],null);
});
await check('Explicit nulls remain null', async () => {const s=await parse(Object.fromEntries(keys.map(k=>[k,null])));for(const k of keys)assert.equal(s.top![k],null);});
await check('Genuine explicit zero readings retain their meaning', async () => {const s=await parse(Object.fromEntries(keys.map(k=>[k,0])));const d=hudFromUniformSnapshot(s,now);for(const k of ['gpuTemp','vramUsed','vramTotal','ramUsed','ramTotal'] as const)assert.equal(d.values[k],0);});
await check('Used memory can be known while capacity is unavailable', async () => {const d=hudFromUniformSnapshot(await parse({gpuMemoryUsedMb:2048,physicalMemoryUsedMb:4096}),now);assert.equal(d.values.vramUsed,2);assert.equal(d.values.ramUsed,4);assert.equal(d.values.vramTotal,null);assert.equal(d.values.ramTotal,null);});
await check('Capacity can be known while used memory is unavailable', async () => {const d=hudFromUniformSnapshot(await parse({gpuMemoryTotalMb:8192,physicalMemoryTotalMb:16384}),now);assert.equal(d.values.vramTotal,8);assert.equal(d.values.ramTotal,16);assert.equal(d.values.vramUsed,null);assert.equal(d.values.ramUsed,null);});
await check('Impossible used/capacity pair is rejected rather than clamped to fake full usage', async () => {const d=hudFromUniformSnapshot(await parse({gpuMemoryUsedMb:9000,gpuMemoryTotalMb:8192,physicalMemoryUsedMb:33000,physicalMemoryTotalMb:32768}),now);assert.equal(d.values.vramUsed,null);assert.equal(d.values.ramUsed,null);assert.equal(d.values.vramTotal,8);assert.equal(d.values.ramTotal,32);});
await check('Fresh FPS does not revive expired or future GPU temperature/memory', async () => {for(const ts of [now-6000,now+1]){const s=await parse({...complete,ts});s.fps={ts:now-10,fps:120,fps1:100,gpu:50,packagePower:20,game:'test',pid:123};const d=hudFromUniformSnapshot(s,now);assert.equal(d.values.fps,120);assert.equal(d.values.gpuTemp,undefined);assert.equal(d.values.ramUsed,undefined);assert.equal(d.values.vramUsed,undefined);}});
await check('Independent Windows core freshness cannot revive HWiNFO-down GPU/memory', async () => {const s=await parse({...complete,hwDown:true,coreUsage:[30],coreUsageTs:now-10});s.hwDown=true;const d=hudFromUniformSnapshot(s,now);assert.equal(d.stale,false);assert.deepEqual(d.coreUsage,[30]);assert.equal(d.values.gpuTemp,undefined);assert.equal(d.values.ramUsed,undefined);});
await check('Virtual committed memory is never substituted for physical RAM', async () => {const d=hudFromUniformSnapshot(await parse({virtualMemoryCommittedFound:true,virtualMemoryCommittedMb:24576}),now);assert.equal(d.values.ramUsed,null);assert.equal(d.values.ramTotal,null);});
await check('Shared desktop/Steam packet admission preserves mapped values and nulls', async () => {const d=boundedHudData(hudFromUniformSnapshot(await parse(complete),now));assert.equal(d.values.gpuTemp,68);assert.equal(d.values.ramUsed,16);assert.equal(d.values.vramUsed,8);});
await check('Native-integrated memory and selected-GPU temperature ready', () => {for(const k of ['gpuTemp','vramUsed','vramTotal','ramUsed','ramTotal'])assert.equal(HUD_METRICS.find(m=>m.id===k)!.ready,true);});
console.log(JSON.stringify({passed:cases.length,cases,scope:'Synthetic data through exact consumer parser/model/packet mapper; no native publisher or installed HUD acceptance',newReaders:0,newTimers:0,hardwareTouched:false,cpuBenchmark:false},null,2));