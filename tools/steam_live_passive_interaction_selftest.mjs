import assert from 'node:assert/strict';
import fs from 'node:fs';
import crypto from 'node:crypto';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';

// Execute the full production expression with the existing dispatch fixture.
// Steam/cache methods below are offline doubles, never a live Steam connection.
const arg = name => { const i = process.argv.indexOf(name); return i < 0 ? null : process.argv[i + 1]; };
const sourcePath = arg('--source') ?? fileURLToPath(new URL('../native/steam_live.js', import.meta.url));
const source = fs.readFileSync(sourcePath, 'utf8');
const hash = text => crypto.createHash('sha256').update(text).digest('hex');
const fixturePath = fileURLToPath(new URL('./steam_sleep_dispatch_guard_selftest.mjs', import.meta.url));
const fixtureFile = fs.readFileSync(fixturePath, 'utf8');
const start = fixtureFile.indexOf('function fixture('), end = fixtureFile.indexOf('\nconst results=[];', start);
assert(start >= 0 && end > start, 'existing dispatch fixture boundary changed');
const body = fixtureFile.slice(start, end), anchor = 'return {fn,request,stats,state,apply};';
assert.equal(body.split(anchor).length, 2, 'existing dispatch fixture return changed');
const appid = 2399932135, gameid = '10307630032478011392';
const build = vm.runInNewContext('(' + body.replace(anchor,
  'return {fn,request,stats,state,apply,window,SteamClient,ui,active,editor,row};') + ')',
  { source, appid, gameid, assert, vm, performance, setTimeout, clearTimeout, queueMicrotask, console });
const plain = value => JSON.parse(JSON.stringify(value));
const results = [];
async function test(name, run) {
  const detail = {};
  try { await run(detail); results.push({ name, passed: true, ...detail }); }
  catch (error) { results.push({ name, passed: false, ...detail, error: String(error.stack ?? error) }); }
}
function fixture({ mode = 'lazy', mobx = true, signed = false } = {}) {
  const f = build(), ws = f.ui.WindowStore, browser = {};
  const forbidden = { focusedMethodReads: 0, focusedCalls: 0, aggregateWindowReads: 0,
    windowLookupCalls: 0, vrConstructions: 0, simulatedCacheReads: 0,
    publicWindowStoreReads: 0, publicNavigationReads: 0, alternateWindowReads: 0 };
  const reads = { activeContext: 0, browserWindow: 0, keyboard: 0, lock: 0, observableValues: 0 };
  const nav = { RootWindow: browser, BIsActive: () => true };
  const manager = { GetActiveContext() { reads.activeContext++; return nav; } };
  f.active.m_BrowserWindow = browser;
  Object.defineProperty(f.active, 'BrowserWindow', { configurable: true, get() { reads.browserWindow++; return this.m_BrowserWindow; } });
  f.active.IsAnyVRWindow = () => f.state.vr === true;
  f.ui.m_WindowStore = ws; f.ui.m_GamepadNavigationManager = manager;
  f.cache = (field, instances, wrapped = mobx) => {
    const data = new Map(instances.map((instance, i) => [i, wrapped ? { value_: instance } : instance]));
    const cache = wrapped ? { data_: data, get size() { return data.size; },
      *values() { reads.observableValues++; for (const wrapper of data.values()) yield wrapper.value_; } } : data;
    ws[field] = cache;
    return { cache, data };
  };
  // The current frozen observation has one entry in each existing MobX map.
  f.cache('m_mapOverlayPopupByPID', [{ BrowserWindow: {} }]);
  f.cache('m_mapDesiredWindowInstances', [f.active]);
  let simulated;
  ws.CreateSimulatedVRWindow = () => { forbidden.vrConstructions++; return { BrowserWindow: {} }; };
  Object.defineProperty(ws, 'SteamUIWindows', { get() {
    forbidden.aggregateWindowReads++;
    if (!simulated) simulated = ws.CreateSimulatedVRWindow();
    return [...ws.m_mapOverlayPopupByPID.values(), ...ws.m_mapDesiredWindowInstances.values(), simulated];
  } });
  ws.GetWindowInstanceFromWindow = root => {
    forbidden.windowLookupCalls++;
    return ws.SteamUIWindows.find(instance => instance.BrowserWindow === root) ?? null;
  };
  Object.defineProperty(f.ui, 'GetFocusedWindowInstance', { configurable: true, get() {
    forbidden.focusedMethodReads++;
    if (mode === 'throw-access') throw Error('fixture-focused-method-read');
    return () => {
      forbidden.focusedCalls++;
      if (mode === 'throw-call') throw Error('fixture-focused-method-call');
      return ws.GetWindowInstanceFromWindow(manager.GetActiveContext().RootWindow);
    };
  } });
  Object.defineProperty(f.ui, 'WindowStore', { configurable: true, get() {
    forbidden.publicWindowStoreReads++; if (f.poisonPublic) throw Error('public WindowStore read'); return ws;
  } });
  Object.defineProperty(f.ui, 'NavigationManager', { configurable: true, get() {
    forbidden.publicNavigationReads++; if (f.poisonPublic) throw Error('public NavigationManager read'); return manager;
  } });
  Object.defineProperty(ws, 'm_simulatedVRGamepadUIOnDesktopInstance', { get() {
    forbidden.simulatedCacheReads++; throw Error('computed simulated VR cache must not be read');
  } });
  for (const [object, key] of [[f.ui, 'ActiveWindowInstance'], [ws, 'GamepadUIMainWindowInstance'], [f.active, 'DesktopOverlay']]) {
    Object.defineProperty(object, key, { get() { forbidden.alternateWindowReads++; throw Error('forbidden window entry'); } });
  }
  ws.BHasStandaloneKeyboard = () => { reads.keyboard++; return f.state.keyboard; };
  f.ui.GetShowingLockScreen = () => { reads.lock++; return f.state.lock; };
  if (signed) { f.row.appid = appid | 0; f.window.appStore.m_mapApps = new Map([[appid | 0, f.row]]); f.ui.m_runningAppIDs = [appid | 0]; }
  f.passive = flag => ({ operation: 'sleep.inspect', account: 42, appids: [appid], ...(flag === undefined ? {} : { runningLinkRead: flag }) });
  Object.assign(f, { ws, nav, manager, browser, forbidden, reads });
  return f;
}
function capture(f, reply, detail) {
  Object.assign(detail, { forbidden: { ...f.forbidden }, reads: { ...f.reads }, configReads: f.stats.configReads,
    raises: f.stats.raises, ok: reply.ok, mutated: reply.mutated, reason: reply.reason ?? null,
    interactive: plain(reply.inputContext?.interactive ?? null) });
}
function passive(f, reply, { available = true, idle = true, reason } = {}) {
  assert(reply.ok && reply.mutated === false);
  for (const [key, count] of Object.entries(f.forbidden)) assert.equal(count, 0, 'passive touched ' + key);
  const i = reply.inputContext.interactive;
  assert.equal(i.available, available); assert.equal(i.idle, idle);
  if (reason) assert.equal(i.reason, reason);
  if (!available) assert.equal(typeof i.reason, 'string', 'unknown evidence needs an explicit reason');
  assert.equal(f.stats.raises, 0); assert.equal(reply.gameInput, 'unknown');
  assert.equal(reply.inputContext.gameInput, 'not-tested'); assert.equal(reply.inputContext.inputEventsObserved, false);
  assert.equal(f.window.__ymccSteamLiveBusy, undefined);
}
for (const flag of [false, true, undefined]) for (const mode of ['lazy', 'throw-call', 'throw-access']) {
  await test(`passive flag=${flag ?? 'omitted'} existing cache focused=${mode}`, async detail => {
    const f = fixture({ mode }), reply = await f.fn(f.passive(flag)); capture(f, reply, detail); passive(f, reply);
    assert.equal(f.stats.configReads, flag === true ? 0 : 1);
    const i = reply.inputContext.interactive;
    assert.equal(i.lockScreen, false); assert.equal(i.keyboard, false); assert.equal(i.overlayOrVrWindow, false);
    assert.equal(i.menuOpen, 0); assert.equal(i.menuSuppressed, false); assert.equal(i.windowInstanceObserved, true);
    assert.equal(i.windowActivity.rootMatchesBrowser, true);
  });
}
for (const flag of [false, true]) for (const change of ['lock', 'keyboard', 'menu', 'suppressed', 'vr', 'overlay', 'configurator']) {
  await test(`passive real veto flag=${flag}: ${change}`, async detail => {
    const f = fixture(); f.state[change] = change === 'menu' ? 1 : true;
    const reply = await f.fn(f.passive(flag)); capture(f, reply, detail); passive(f, reply, { idle: false });
    if (change === 'vr') assert.equal(reply.inputContext.interactive.windowKinds.vr, true);
  });
}
for (const flag of [false, true]) {
  await test(`passive signed GameID/config cap flag=${flag}`, async detail => {
    const f = fixture({ signed: true }), queries = [];
    f.window.ControllerStore.GetControllers = () => [0, 1, 2].map(index => ({ nControllerIndex: index, eControllerType: 2 }));
    f.SteamClient.Input.GetConfigForAppAndController = async (app, index) => { f.stats.configReads++; queries.push({ app, index }); return { URL: 'empty.vdf' }; };
    const reply = await f.fn(f.passive(flag)); capture(f, reply, detail); passive(f, reply);
    assert.equal(reply.inputContext.configQueriesTruncated, true);
    assert.deepEqual(queries, flag ? [] : [{ app: appid | 0, index: 0 }, { app: appid | 0, index: 1 }]);
    assert.equal(reply.overviews[0].gameidString, gameid); assert.equal(reply.overviews[0].overviewKey, appid | 0);
  });
  await test(`passive native private record flag=${flag}`, async detail => {
    const f = fixture();
    class Identity {
      constructor() { this.m_ulSteamID = 76561197960265770n; }
      GetAccountID() { return f.state.account; }
      ConvertTo64BitString(){return this.m_ulSteamID.toString()}
    }
    f.window.settingsStore.m_CMInterface.steamid = new Identity();
    const reply = await f.fn({ ...f.passive(flag), nativeRecordIdentityRead: true }); capture(f, reply, detail); passive(f, reply);
    detail.nativeRecordIdentity = plain(reply._ymccPrivateNativeRecord);
    assert.equal(reply._ymccPrivateNativeRecord.status, 'observed'); assert.equal(reply._ymccPrivateNativeRecord.gameid64, gameid);
    assert.equal(reply._ymccPrivateNativeRecord.steamid64, '76561197960265770');
  });
}
for (const flag of [false, true]) {
  for (const [name, change, reason] of [
    ['missing raw store', f => { delete f.ui.m_WindowStore; }, 'steam-passive-window-cache-unavailable'],
    ['missing manager', f => { delete f.ui.m_GamepadNavigationManager; }, 'steam-passive-window-cache-unavailable'],
    ['missing map', f => { delete f.ws.m_mapOverlayPopupByPID; }, 'steam-passive-window-cache-unavailable'],
    ['missing root', f => { f.nav.RootWindow = null; }, 'steam-passive-window-root-unavailable'],
    ['no matching instance', f => { f.nav.RootWindow = {}; }, 'steam-passive-window-cache-no-match'],
    ['empty caches', f => { f.cache('m_mapOverlayPopupByPID', []); f.cache('m_mapDesiredWindowInstances', []); }, 'steam-passive-window-cache-no-match'],
    ['ambiguous distinct instances', f => { f.cache('m_mapOverlayPopupByPID', [{ ...f.active, BrowserWindow: f.browser }]); }, 'steam-passive-window-cache-ambiguous'],
    ['simulated-only root', f => { f.cache('m_mapDesiredWindowInstances', []); }, 'steam-passive-window-cache-no-match'],
    ['map limit', f => { f.cache('m_mapOverlayPopupByPID', Array.from({ length: 65 }, () => ({ BrowserWindow: {} }))); }, 'steam-passive-window-cache-limit'],
    ['MobX wrapper missing value', f => { f.ws.m_mapDesiredWindowInstances = { data_: new Map([[0, {}]]) }; }, 'steam-passive-window-cache-invalid'],
    ['BrowserWindow throws', f => { Object.defineProperty(f.active, 'BrowserWindow', { get() { throw Error('identity unavailable'); } }); }, 'steam-passive-window-cache-read-failed'],
  ]) await test(`passive unknown flag=${flag}: ${name}`, async detail => {
    const f = fixture(); change(f); const reply = await f.fn(f.passive(flag)); capture(f, reply, detail);
    passive(f, reply, { available: false, idle: false, reason });
  });
}
await test('data-field accessor is refused without invoking its getter', async detail => {
  const f = fixture(); let getterCalls = 0;
  Object.defineProperty(f.ws, 'm_mapDesiredWindowInstances', { get() { getterCalls++; throw Error('no getter invocation'); } });
  const reply = await f.fn(f.passive(true)); capture(f, reply, detail);
  passive(f, reply, { available: false, idle: false, reason: 'steam-passive-window-cache-unavailable' }); assert.equal(getterCalls, 0);
});
await test('MobX data_ accessor is refused without invoking it', async detail => {
  const f = fixture(); let getterCalls = 0;
  f.ws.m_mapDesiredWindowInstances = Object.defineProperty({}, 'data_', { get() { getterCalls++; throw Error('computed data'); } });
  const reply = await f.fn(f.passive(true)); capture(f, reply, detail);
  passive(f, reply, { available: false, idle: false, reason: 'steam-passive-window-cache-unavailable' }); assert.equal(getterCalls, 0);
});
await test('MobX/native Map methods cannot redirect selection into window enumeration', async detail => {
  const f = fixture(); f.poisonPublic = true;
  for (const cache of [f.ws.m_mapOverlayPopupByPID, f.ws.m_mapDesiredWindowInstances]) {
    cache.values = () => { throw Error('do not use observable map enumeration'); };
    Object.defineProperty(cache, 'size', { get() { throw Error('do not read computed map size'); } });
  }
  const reply = await f.fn(f.passive(true)); capture(f, reply, detail); passive(f, reply);
});
await test('native Map cache and cross-map duplicate of the same instance are valid', async detail => {
  const f = fixture({ mobx: false }); f.cache('m_mapOverlayPopupByPID', [f.active], false);
  const reply = await f.fn(f.passive(false)); capture(f, reply, detail); passive(f, reply);
});
await test('a unique overlay-map instance is selected by exact BrowserWindow identity', async detail => {
  const f = fixture(); f.cache('m_mapOverlayPopupByPID', [f.active]); f.cache('m_mapDesiredWindowInstances', [{ BrowserWindow: {} }]);
  const reply = await f.fn(f.passive(true)); capture(f, reply, detail); passive(f, reply);
});
await test('same-size cache replacement during a guard fails post-observation validation', async detail => {
  const f = fixture(); f.ui.GetShowingLockScreen = () => { f.cache('m_mapDesiredWindowInstances', [{ ...f.active, BrowserWindow: f.browser }]); return false; };
  const reply = await f.fn(f.passive(true)); capture(f, reply, detail);
  passive(f, reply, { available: false, idle: false, reason: 'steam-passive-window-cache-changed' });
});
await test('root changes during a guard cannot publish idle', async detail => {
  const f = fixture(); f.active.IsAnyVRWindow = () => { f.nav.RootWindow = {}; return false; };
  const reply = await f.fn(f.passive(true)); capture(f, reply, detail);
  passive(f, reply, { available: false, idle: false, reason: 'steam-passive-window-cache-changed' });
});
for (const change of ['lock', 'keyboard', 'menu', 'suppressed', 'vr', 'overlay', 'configurator']) {
  await test('actuator retains original interactive veto: ' + change, async detail => {
    const f = fixture(); f.state[change] = change === 'menu' ? 1 : true;
    const reply = await f.fn(f.request); capture(f, reply, detail);
    assert.equal(reply.reason, 'steam-interactive-ui-active'); assert.equal(reply.mutated, false);
    assert(f.forbidden.focusedCalls > 0); assert.equal(f.stats.raises, 0); assert.equal(f.stats.configReads, 0);
  });
}
for (const mode of ['throw-call', 'throw-access']) await test('actuator still rejects unknown getter: ' + mode, async detail => {
  const f = fixture({ mode }), reply = await f.fn(f.request); capture(f, reply, detail);
  assert.equal(reply.ok, false); assert.equal(reply.mutated, false); assert.equal(f.stats.raises, 0); assert(f.forbidden.focusedMethodReads > 0);
});
for (const flag of [false, true]) await test(`actuator flag=${flag} cannot enable passive mode`, async detail => {
  const f = fixture(); f.state.lock = true;
  const reply = await f.fn({ ...f.request, runningLinkRead: flag, passiveInspect: true }); capture(f, reply, detail);
  assert.equal(reply.reason, 'steam-interactive-ui-active'); assert.equal(f.stats.raises, 0); assert(f.forbidden.focusedCalls > 0);
});
await test('actuator retains original complete GameID return and native verification boundary', async detail => {
  const f = fixture(), reply = await f.fn(f.request); capture(f, reply, detail);
  assert(!reply.ok && reply.mutated && reply.actionAcknowledged && reply.verificationPending);
  assert.equal(reply.reason, 'steam-input-ui-focus-readback-pending'); assert.equal(f.stats.raises, 1); assert(f.forbidden.focusedCalls > 1);
  assert.equal(reply.inputContext.interactive.available, true); assert.equal(reply.inputContext.interactive.idle, true);
  assert.equal(reply.healthVerified, false); assert.equal(reply.gameInput, 'not-tested');
});
await test('passive account mismatch still rejects before any window observation', async detail => {
  const f = fixture(); f.state.account = 43;
  const reply = await f.fn(f.passive(false)); capture(f, reply, detail);
  assert.equal(reply.reason, 'steam-account-changed'); assert.equal(reply.ok, false); assert.equal(f.forbidden.focusedMethodReads, 0);
});
const report = { suite: 'steam-live-passive-interaction-existing-cache-v2', sourcePath, sourceSHA256: hash(source),
  reusedFixture: { path: fixturePath, sha256: hash(fixtureFile), adaptation: 'expose fixture objects; add verified native/MobX cache and descriptor doubles' },
  usesProductionScript: true, caseCount: results.length, passed: results.filter(x => x.passed).length,
  failed: results.filter(x => !x.passed).length, cases: results,
  liveSteamAccess: false, deployed: false, recoveryVerified: false, firstBadCauseProven: false };
if (arg('--output')) fs.writeFileSync(arg('--output'), JSON.stringify(report, null, 2) + '\n');
console.log(JSON.stringify(process.argv.includes('--quiet') ? { ...report, cases: undefined } : report, null, 2));
if (report.failed) process.exitCode = 1;
