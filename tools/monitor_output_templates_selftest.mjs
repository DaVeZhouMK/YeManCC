// Offline template/model/three-way output tests. No native, Steam, game or sensors.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
const root = process.cwd(), req = createRequire(path.join(root, 'package.json'));
const { build } = req('esbuild');
const out = path.resolve(process.env.YMCC_MONITOR_MODEL_OUT || path.join(root, '../../../Build/Tasks/YMCC-Steam-HUD-Probe/templates-tdp-content-20261010'));
fs.mkdirSync(out, { recursive: true });
const bundle = await build({ stdin: { contents: `export * from './src/bridge/monitorHudModel.ts';export * from './src/bridge/monitorOutputCoordinator.ts';`, resolveDir: root, loader: 'ts' }, bundle: true, format: 'esm', platform: 'node', write: false });
const modulePath = path.join(out, 'model-under-test.mjs'); fs.writeFileSync(modulePath, bundle.outputFiles[0].text);
const m = await import('file:///' + modulePath.replaceAll('\\', '/') + '?test=' + Date.now());
const cases = []; const check = (label, fn) => { fn(); cases.push(label); };
const defaults = m.normalizeMonitorOutput({ymccTemplate:'A'}); // Full bank A fixture; default B is covered by monitor_templates_selftest.
check('Honeycomb is selectable and survives independent banks, display wire, and export without changing defaults',()=>{
 const option=m.HUD_LAYOUTS.find(x=>x.value==='honeycomb');assert.equal(option.label,'蜂窝监控');assert.equal(option.group,'长方形');
 const p=m.normalizeMonitorOutput({ymccTemplate:'A',ymcc:{layout:'honeycomb',scale:1},ymccTemplates:{B:{layout:'honeycomb',scale:1.4}}});
 const b=m.switchMonitorHudTemplate(p,'B');assert.equal(b.ymcc.layout,'honeycomb');assert.equal(b.ymcc.scale,1.4);
 const a=m.switchMonitorHudTemplate(b,'A');assert.equal(a.ymcc.layout,'honeycomb');assert.equal(a.ymcc.scale,1);
 const exported=m.monitorTemplateDocument(a);assert.equal(exported.sampleDataIncluded,false);const restored=m.normalizeMonitorOutput(JSON.parse(JSON.stringify(exported.preferences)));assert.equal(restored.ymccTemplates.B.layout,'honeycomb');assert.equal(m.normalizeMonitorOutput(restored,false).ymcc.layout,'honeycomb');
 assert.equal(m.normalizeMonitorOutput().ymcc.layout,'capsule');assert.equal(m.hudBaseLayout('honeycomb'),'capsule');
});
check('Retired peak graph is discarded; legacy extra category migrates into battery',()=>{
 const old=m.normalizeMonitorOutput({ymcc:{fields:{cpuPeakBar:true},fieldGroup:'附加'}});
 assert.equal(Object.hasOwn(old.ymcc.fields,'cpuPeakBar'),false);assert.equal(old.ymcc.fieldGroup,'电池');
 assert(!m.HUD_METRICS.some(x=>x.id==='cpuPeakBar'));assert(!m.HUD_PERCENT_GRAPHS.some(x=>x.id==='cpuPeakBar'));
});
check('Text defaults 120 percent and clamps both banks at 200; individual values retain 300',()=>{
 const p=m.normalizeMonitorOutput({ymccTemplate:'A',ymcc:{textScale:3,valueScales:{fps:3}},ymccTemplates:{B:{textScale:9,valueScales:{cpuPower:3}}}});
 assert.equal(defaults.ymcc.textScale,1.2);assert.equal(p.ymcc.textScale,2);assert.equal(p.ymcc.valueScales.fps,3);
 const b=m.switchMonitorHudTemplate(p,'B');assert.equal(b.ymcc.textScale,2);assert.equal(b.ymcc.valueScales.cpuPower,3);
 const a=m.switchMonitorHudTemplate(b,'A');assert.equal(a.ymcc.textScale,2);assert.equal(a.ymcc.valueScales.fps,3);
 assert.deepEqual(m.HUD_TEMPLATES.map(x=>x.label),['A模板','B模板']);
});
check('CPU metadata and selected GPU identity survive the common display wire; absent GPU clears',()=>{
 const snapshot={top:{gpuName:'Selected GPU',gpuSelectionPresent:true}};
 const data=m.hudFromUniformSnapshot(snapshot,10000,null,false,'CPU metadata');
 const wire=m.boundedHudData(data);assert.equal(wire.cpuName,'CPU metadata');assert.equal(wire.gpuName,'Selected GPU');
 assert.equal(m.hudFromUniformSnapshot({top:{...snapshot.top,gpuSelectionPresent:false}},10000,null,false,'CPU').gpuName,'');
 assert.equal(m.hudFromUniformSnapshot(null,10000,null,false).gpuName,'');
});

check('Auto-hide defaults on; explicit false and true survive normalization',()=>{
 assert.equal(defaults.ymcc.hideUnavailable,true);
 for(const value of [false,true]){const saved=m.normalizeMonitorOutput({ymcc:{hideUnavailable:value}});assert.equal(saved.ymcc.hideUnavailable,value);assert.equal(m.normalizeMonitorOutput(saved).ymcc.hideUnavailable,value);}
 for(const value of [undefined,null,0,'false'])assert.equal(m.normalizeMonitorOutput({ymcc:{hideUnavailable:value}}).ymcc.hideUnavailable,true);
});
check('RTSS retains three templates with safe old-preference/default normalization', () => {
  assert.deepEqual(m.RTSS_MONITOR_TEMPLATES.map(x=>x.value),['W','L','J']);
  for(const choice of ['W','L','J'])assert.equal(m.normalizeMonitorOutput({rtssTemplate:choice}).rtssTemplate,choice);
  assert.equal(m.normalizeMonitorOutput({rtssTemplate:'off'}).rtssTemplate,'W');
});
check('Default simple preset uses capsule/Tasha and the screenshot selection', () => {
  assert.equal(defaults.output, 'ymcc'); assert.equal(defaults.ymcc.layout, 'capsule'); assert.equal(defaults.ymcc.opacity, .8);
  assert(defaults.ymcc.fields.cpuCores); assert(defaults.ymcc.fields.gpuBar);
  assert.equal(defaults.ymcc.dataPreset,'simple');assert.equal(defaults.ymcc.underlayPreset,'ac8-pink');assert.equal(defaults.ymcc.underlayOpacity,.25);assert.equal(defaults.ymcc.fpsEmphasis,true);
  assert.equal(Object.values(defaults.ymcc.fields).filter(Boolean).length,21);assert.deepEqual(defaults.ymcc.fields,m.hudDataPresetFields('simple'));
  assert.deepEqual(m.HUD_DATA_PRESETS.map(p=>p.label),['精简','简约','标准','详细']);
  for(const id of ['frametime','cpuUsage','cpuUsageBar','cpuVoltage','gpuTemp','gpuVoltage','fanSpeed','fanSpeedBar','ramUsed','vramUsed','clock','netDown','diskRead'])assert.equal(defaults.ymcc.fields[id],false,id);
});
check('Exactly two public display outputs; each metric has unique stable ID', () => {
  assert.deepEqual(m.MONITOR_OUTPUTS.map(x => x.value), ['desktop', 'rtss']);
  assert.equal(new Set(m.HUD_METRICS.map(x => x.id)).size, m.HUD_METRICS.length); assert.equal(m.HUD_METRICS.length, 40);
});
check('Legacy Steam choices migrate to the public desktop output, shared HUD appearance is preserved', () => {
  const old=m.normalizeMonitorOutput({output:'steam',enabled:false,ymcc:{crosshair:'dot',scale:1.4}}),next=m.selectableMonitorOutput(old);
  assert.equal(next.output,'ymcc');assert.equal(next.ymccCarrier,'desktop');assert.equal(next.enabled,false);assert.deepEqual(next.ymcc,old.ymcc);assert.equal(m.monitorOutputChoice(next),'desktop');
  const desktop=m.selectableMonitorOutput({output:'ymcc',ymccCarrier:'desktop'});assert.equal(m.monitorOutputChoice(desktop),'desktop');assert.equal(m.monitorOutputChoice(m.selectableMonitorOutput({output:'rtss'})),'rtss');
});
check('Detailed defaults omit clock; migration is one-time and preserves later choices',()=>{
 assert.equal(m.hudDataPresetFields('detailed').clock,false);
 const oldFields=Object.fromEntries(m.HUD_METRICS.map(metric=>[metric.id,true]));
 const migrated=m.normalizeMonitorOutput({ymcc:{dataPreset:'detailed',fields:oldFields,dataPresetFields:{detailed:oldFields}}});
 assert.equal(migrated.ymcc.fields.clock,false);assert.equal(migrated.ymcc.dataPresetFields.detailed.clock,false);
 assert.equal(Object.values(migrated.ymcc.fields).filter(Boolean).length,m.HUD_METRICS.length-1);
 migrated.ymcc.fields.clock=true;migrated.ymcc.dataPresetFields.detailed.clock=true;
 const saved=m.normalizeMonitorOutput(migrated);assert.equal(saved.ymcc.fields.clock,true);assert.equal(saved.ymcc.dataPresetFields.detailed.clock,true);
 const hidden=m.selectableMonitorOutput({output:'ymcc',ymccCarrier:'steam-preferred',ymcc:{underlayPreset:'tifa',scale:2}});
 assert.equal(hidden.ymccCarrier,'desktop');assert.equal(hidden.ymcc.scale,2);assert.equal(hidden.ymcc.underlayPreset,'tifa');
});
for (const metric of m.HUD_METRICS) check('Independent toggle: ' + metric.id, () => {
  const raw = structuredClone(defaults); raw.ymcc.fields[metric.id] = !raw.ymcc.fields[metric.id]; const result = m.normalizeMonitorOutput(raw);
  assert.equal(result.ymcc.fields[metric.id], raw.ymcc.fields[metric.id]);
  for (const other of m.HUD_METRICS.filter(x => x.id !== metric.id)) assert.equal(result.ymcc.fields[other.id], defaults.ymcc.fields[other.id]);
});
check('Clamps malformed appearance, ignores unknown IDs, deep-copies nested fields', () => {
  const result = m.normalizeMonitorOutput({ output: 'bad', enabled: 'false', rtssScale: Infinity, ymcc: { scale: -1, opacity: 2, saturation: NaN, position: 99, radius: 50, fields: { fps: false, cpuCores: true, malware: true } } });
  assert.equal(result.ymcc.scale, .2); assert.equal(result.ymcc.opacity, 1); assert.equal(result.ymcc.saturation, 1); assert.equal(result.ymcc.position, 1); assert.equal(result.ymcc.radius, 18);
  assert.equal(result.ymcc.fields.fps, false); assert(!('malware' in result.ymcc.fields)); assert.equal(result.enabled, true);
  result.ymcc.fields.cpuPower = false; assert.equal(defaults.ymcc.fields.cpuPower, true);
});
check('Bounded child admission caps arrays and preserves invalid gaps without fake zeros', () => {
  const raw=m.makeHudFixture();raw.coreUsage=Array(1000).fill(32);raw.coreUsage[1]=NaN;raw.coreUsage[2]=-1;raw.coreClocks=Array(1000).fill(4.35);raw.frameTimes=Array(1000).fill(8.5);
  const data=m.boundedHudData(raw);assert.equal(data.coreUsage.length,128);assert.equal(data.coreClocks.length,128);assert.equal(data.frameTimes.length,120);assert.equal(data.coreUsage[1],null);assert.equal(data.coreUsage[2],null);assert.equal(data.coreUsage[0],32);
});
check('Packet admission copies values, bounds clock strings and rejects non-number readings', () => {
  const raw=m.makeHudFixture();raw.values.clock='x'.repeat(1000);raw.values.fps='117';raw.values.gpuPower=Infinity;raw.values.batteryPower=-32;
  const data=m.boundedHudData(raw);assert.equal(data.values.clock.length,16);assert.equal(data.values.fps,null);assert.equal(data.values.gpuPower,null);assert.equal(data.values.batteryPower,-32);assert.notEqual(data.values,raw.values);assert.notEqual(data.coreUsage,raw.coreUsage);
});
check('Expired packet admission cannot keep numbers or charts visually alive', () => {
  const data=m.boundedHudData(m.makeHudFixture(),true);assert.equal(data.stale,true);assert.equal(m.hudValue(data,'fps'),'—');
});
check('Missing actual data is not fixture or synthetic frametime', () => {
  const data = m.hudFromUniformSnapshot(null, 10000); assert.equal(data.source, 'ymcc-uniform'); assert.equal(m.hudValue(data, 'fps'), '—'); assert.equal(m.hudValue(data, 'frametime'), '—'); assert.deepEqual(data.coreUsage, []);
});
const snapshot = { schema: 1, session: 1, generation: 1, seq: 1, collectedAt: 9990, hwDown: false,
  top: { ts: 9900, tdpW: 28, tempC: 60, freqMhz: 4300, cpuUsage: 0, gpuPowerW: 90, gpuClockMhz: 2100, hasBattery: false, chargeW: 0, batteryPercent: -1, remainMin: -1, hwDown: false },
  fps: { ts: 9990, pid: 42, fps: 120, fps1: 90, gpu: 81 } };
check('Maps existing uniform fields with correct units; valid zero preserved', () => {
  const data = m.hudFromUniformSnapshot(snapshot, 10000); assert.equal(m.hudValue(data, 'fps'), '120'); assert.equal(m.hudValue(data, 'cpuClock'), '4.30'); assert.equal(m.hudValue(data, 'cpuUsage'), '0'); assert.equal(m.hudValue(data, 'batteryPercent'), '—'); assert.equal(m.hudValue(data, 'gpuTemp'), '—');
});
check('Each payload uses its own freshness; later FPS does not revive stale CPU', () => {
  const data = m.hudFromUniformSnapshot({ ...snapshot, top: { ...snapshot.top, ts: 1000 } }, 10000); assert.equal(m.hudValue(data, 'cpuTemp'), '—'); assert.equal(m.hudValue(data, 'fps'), '120');
  const stale = m.hudFromUniformSnapshot(snapshot, 16000); assert(stale.stale); assert.equal(m.hudValue(stale, 'gpuUsage'), '—');
});
check('Hardware-down and future timestamps reject metrics; battery signed power preserved', () => {
  assert.equal(m.hudValue(m.hudFromUniformSnapshot({ ...snapshot, hwDown: true }, 10000), 'fps'), '—');
  assert.equal(m.hudValue(m.hudFromUniformSnapshot(snapshot, 9000), 'cpuTemp'), '—');
  const data = m.hudFromUniformSnapshot({ ...snapshot, top: { ...snapshot.top, hasBattery: true, batteryPercent: 74, chargeW: -12.5, remainMin: 80 } }, 10000);
  assert.equal(m.hudValue(data, 'batteryPower'), '-12.5');
});
check('Export contains preferences only, never fixture readings or sensor providers', () => {
  const doc = m.monitorTemplateDocument(defaults);
  assert.equal(doc.dataSource, 'ymcc-uniform-only'); assert.equal(doc.sampleDataIncluded, false);
  assert(!('values' in doc)); assert(!JSON.stringify(doc).includes('Provider'));
});
check('Null/NaN/out-of-range meters stay bounded without changing readings', () => {
  assert.equal(m.percentBar(NaN), 0); assert.equal(m.percentBar(null), 0); assert.equal(m.percentBar(140), 100); assert.equal(m.percentBar(-2), 0);
});
const ovlPath = path.join(root, 'PowerControl/RTSS-Overlays/YeManOBS-W-1.ovl'); const ovl = fs.readFileSync(ovlPath, 'latin1');
check('Existing OVL has 80 sources / 74 layers, hardware aliases and actual 8/16 CPU load graphs', () => {
  assert.match(ovl, /Sources=80/); assert.match(ovl, /Layers=74/); assert.match(ovl, /\[Layer39\][\s\S]*?<G=CPU1 usage/); assert.match(ovl, /\[Layer40\][\s\S]*?<G=CPU16 usage>/);
  assert.match(ovl, /Name=Core 0 Clock/); assert.match(ovl, /Name=P-core 0 Clock/); assert.match(ovl, /<G=GPU D3D Usage/); assert.match(ovl, /<G=Frametime/);
});
function harness(options = {}) {
  const calls = []; let active = new Set(['rtss']); let backend = true;
  const deps = {
    async capabilities() { calls.push('capabilities'); return { steam: options.steam !== false, ymcc: options.ymcc !== false }; },
    async ensureRtss() { calls.push('ensure-rtss'); backend = true; },
    async setSteam(pos) { calls.push('steam:' + pos); if (options.fail === 'steam:' + pos) return { confirmed: false, reason: 'queued' }; pos ? active.add('steam') : active.delete('steam'); if(pos)assert(active.size<=1); return { confirmed: true }; },
    async setRtss(show) { calls.push('rtss:' + show); if (options.fail === 'rtss:' + show) return { confirmed: false }; show ? active.add('rtss') : active.delete('rtss'); if(show)assert(active.size<=1); return { confirmed: true }; },
    async setYmcc(show, prefs) { calls.push('ymcc:' + show); if (options.fail === 'ymcc:' + show) return { confirmed: false }; if (show) assert.equal(prefs.ymcc.layout, 'capsule'); show ? active.add('ymcc') : active.delete('ymcc'); if(show)assert(active.size<=1); return { confirmed: true }; },
  };
  return { router: m.createMonitorOutputCoordinator(deps), calls, active: () => [...active], backend: () => backend };
}
for (const output of ['steam', 'ymcc', 'rtss']) {
  const h = harness(); const result = await h.router.apply({ ...defaults, output });
  check('One output + retained RTSS backend: ' + output, () => { assert(result.ok); assert.equal(result.applied, output); assert.deepEqual(h.active(), [output]); assert(h.backend()); assert.deepEqual(h.calls.slice(0, 5), ['capabilities', 'ensure-rtss', 'steam:0', 'rtss:false', 'ymcc:false']); });
}
{
  const h = harness(); const result = await h.router.apply({ ...defaults, enabled: false });
  check('Display master OFF hides outputs without stopping RTSS or changing frame limits', () => { assert(result.ok); assert.equal(result.applied, 'off'); assert.deepEqual(h.active(), []); assert(h.backend()); assert(!h.calls.some(c => /stop|limit/.test(c))); });
}
for (const fail of ['steam:0', 'rtss:false', 'ymcc:false']) {
  const h = harness({ fail }); const result = await h.router.apply(defaults);
  check('Unconfirmed hide prevents target enable: ' + fail, () => { assert(!result.ok); assert.equal(result.applied, 'unknown'); assert(!h.calls.includes('ymcc:true')); });
}
{
  const h = harness({ ymcc: false }); const result = await h.router.apply(defaults);
  check('Unintegrated product HUD rejects BEFORE any write or backend start', () => { assert(!result.ok); assert.equal(result.phase, 'preflight'); assert.deepEqual(h.calls, ['capabilities']); assert.deepEqual(h.active(), ['rtss']); });
}
{
  const h = harness({ steam: false }); const result = await h.router.apply({ ...defaults, output: 'rtss' });
  check('Unknown Steam state cannot be treated as hidden', () => { assert(!result.ok); assert.deepEqual(h.calls, ['capabilities']); });
}
{
  const h = harness(); const first = structuredClone(defaults); const a = h.router.apply(first); first.output = 'steam'; const b = h.router.apply({ ...defaults, output: 'rtss' });
  await a; await b;
  check('Serial requests snapshot intent and converge to last output without overlap', () => { assert.deepEqual(h.active(), ['rtss']); assert(h.calls.includes('ymcc:true')); });
}
for (const layout of ['corner', 'bar', 'capsule']) {
  const prefs = structuredClone(defaults); prefs.ymcc.layout = layout;
  fs.writeFileSync(path.join(out, 'ymcc-' + layout + '.json'), JSON.stringify(m.monitorTemplateDocument(prefs), null, 2));
}
const report = { suite: 'YMCC output templates and exclusive routing', passed: cases.length, cases,
  scope: 'Production pure TS model/coordinator with memory-only deps; no Steam/game/native/process IO. No fresh live carrier validation.' };
fs.writeFileSync(path.join(out, 'TEMPLATE-MODEL-SELFTEST.json'), JSON.stringify(report, null, 2)); console.log(JSON.stringify(report, null, 2));