// Full production-script VM regression; no Steam, CDP or native-module execution.
// Timers advance logical milliseconds: the 1000ms deadline is simulated, not a
// wall-clock measurement or evidence of native-cache freshness.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import crypto from 'node:crypto';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';

const sourcePath = process.env.YMCC_STEAM_FOCUS_TEST_SOURCE || fileURLToPath(new URL('../native/steam_live.js', import.meta.url));
const source = fs.readFileSync(sourcePath, 'utf8');
const hash = text => crypto.createHash('sha256').update(text).digest('hex');
const sourceSHA256 = hash(source);
const plain = value => JSON.parse(JSON.stringify(value));
const request = () => ({ operation: 'sleep.inspect', account: 42, appids: [123],
  runningLinkRead: true, nativeFocusFeedbackRead: true });
const tuple = (appid = 123, overrides = {}) => ({ appid, pid: 7320,
  windowid: 17, strExeName: 'original-game.exe', ...overrides });
const payload = () => ({ focusedApp: tuple(), rgFocusable: [tuple(),
  tuple(0, { pid: 0, windowid: 0, strExeName: null })] });
const normalize = t => ({ appid: t.appid >>> 0, pid: t.pid,
  windowid: t.windowid, exeName: t.strExeName ?? null });
const results = [], unhandled = [];
const coverage = { fixtures: 0, registrations: 0, unregisterCalls: 0, maximumActive: 0 };
const onUnhandled = error => unhandled.push(String(error));
process.on('unhandledRejection', onUnhandled);

class Clock {
  now = 0;
  nextId = 1;
  timers = new Map();
  requests = [];
  setTimeout = (callback, delay = 0, ...args) => {
    assert.equal(typeof callback, 'function');
    assert(Number.isFinite(delay) && delay >= 0, 'timer must have a bounded delay');
    const id = this.nextId++;
    this.requests.push(delay);
    this.timers.set(id, { at: this.now + delay, callback, args });
    return id;
  };
  clearTimeout = id => this.timers.delete(id);
  async settle(promise) {
    let done = false, value, error;
    Promise.resolve(promise).then(v => { value = v; done = true; }, e => { error = e; done = true; });
    for (let turn = 0; turn < 200; turn++) {
      for (let microtask = 0; microtask < 24; microtask++) await Promise.resolve();
      if (done) { if (error) throw error; return value; }
      const next = [...this.timers].sort((a, b) => a[1].at - b[1].at || a[0] - b[0])[0];
      assert(next, 'full script stalled without a bounded timer');
      const [id, task] = next;
      this.timers.delete(id); this.now = task.at;
      task.callback(...task.args);
    }
    assert.fail('full script exceeded the bounded logical timer budget');
  }
}

function fixture(options = {}) {
  coverage.fixtures++;
  const clock = new Clock();
  const stats = { registrations: 0, unregisterCalls: 0, active: 0, maximumActive: 0,
    configReads: 0, actions: [], writes: [], sequence: [], cleanupBeforeReturn: 0, handleGetterReads: 0 };
  const state = { account: 42, mode: options.mode ?? 'async', callback: null,
    snapshot: 'payload' in options ? options.payload : payload(), inRegistration: false };
  const action = name => (...args) => {
    stats.actions.push({ name, args }); throw Error(`unrelated action: ${name}`);
  };
  const protect = (object, name) => new Proxy(object, {
    set(target, key) { stats.writes.push(`${name}.${String(key)}=`); return false; },
    deleteProperty(target, key) { stats.writes.push(`delete ${name}.${String(key)}`); return false; },
    defineProperty(target, key) { stats.writes.push(`define ${name}.${String(key)}`); return false; }
  });
  const overview = protect({ appid: 123, gameid: '123', display_name: 'Original game', app_type: 1,
    local_per_client_data: protect({ display_status: 11 }, 'local'),
    selected_per_client_data: protect({ display_status: 11 }, 'selected'), display_status: 11 }, 'overview');
  const running = protect([], 'running');
  const map = new Map([[123, overview]]);
  const ui = protect({ m_runningAppIDs: running,
    WindowStore: protect({ GetAppWindowIDs: () => [], BHasStandaloneKeyboard: () => false }, 'WindowStore'),
    GetShowingLockScreen: () => false, GetFocusedWindowInstance: () => null,
    ScopeRunningApps: action('ScopeRunningApps') }, 'SteamUIStore');
  const editor = protect({ EditingConfigurationAppId: 0, EditedConfiguration: null,
    PreviewedConfiguration: null, IsUpdatingEditingConfiguration: false,
    m_nEditNumber: 0, m_nLastSavedEditNumber: 0,
    SetEditingConfiguration: action('SetEditingConfiguration'),
    SaveEditingConfiguration: action('SaveEditingConfiguration'),
    UpdateEditingConfiguration: action('UpdateEditingConfiguration') }, 'editor');
  const originalAccountObject = { GetAccountID: () => state.account };
  const settings = { m_CMInterface: { steamid: originalAccountObject },
    GetClientSetting: () => [true, action('setting-write')] };
  const rawWindow = { SteamUIStore: ui,
    appStore: { m_mapApps: { get: key => map.get(key), set: action('overview.set'),
      delete: action('overview.delete'), clear: action('overview.clear') } },
    settingsStore: settings, controllerConfiguratorStore: editor,
    ControllerStore: protect({ GetControllers: () => [{ nControllerIndex: 0, eControllerType: 2 }] }, 'ControllerStore'),
    FocusedAppWindowStore: protect({ GetFocusedAppID: () => 0, GetFocusedWindowID: () => 0,
      GetFocusedOverlayAppID: () => 0, GetFocusedOverlayPID: () => 0,
      SetFocusedApp: action('SetFocusedApp') }, 'FocusedAppWindowStore') };
  const allowedWrites = new Set(['__ymccSteamLiveBusy', '__ymccNativeFocusReadQuarantine']);
  const window = new Proxy(rawWindow, {
    set(target, key, value) {
      if (!allowedWrites.has(key)) { stats.writes.push(`window.${String(key)}=`); return false; }
      return Reflect.set(target, key, value);
    },
    deleteProperty(target, key) {
      if (!allowedWrites.has(key)) { stats.writes.push(`delete window.${String(key)}`); return false; }
      return Reflect.deleteProperty(target, key);
    },
    defineProperty(target, key, value) {
      if (!allowedWrites.has(key)) { stats.writes.push(`define window.${String(key)}`); return false; }
      return Reflect.defineProperty(target, key, value);
    }
  });
  const emit = snapshot => {
    stats.sequence.push('callback');
    options.beforeCallback?.({ state, settings, window, clock });
    state.callback(snapshot);
    options.afterCallback?.({ state, settings, window, clock });
  };
  const api = {};
  function register(callback) {
    assert.equal(this, api, 'register must retain its System.UI owner');
    assert.equal(typeof callback, 'function');
    stats.registrations++; coverage.registrations++;
    stats.active++; stats.maximumActive = Math.max(stats.maximumActive, stats.active);
    coverage.maximumActive = Math.max(coverage.maximumActive, stats.maximumActive);
    assert.equal(stats.active, 1, 'at most one active native subscription');
    stats.sequence.push('register-enter'); state.callback = callback; state.inRegistration = true;
    options.onRegister?.({ state, settings, window, clock });
    const handle = { unregister() {
      assert.equal(this, handle, 'unregister must retain its owned handle');
      stats.unregisterCalls++; coverage.unregisterCalls++;
      if (state.inRegistration) stats.cleanupBeforeReturn++;
      assert(!state.inRegistration, 'sync callback cannot cleanup before register returns');
      stats.sequence.push('unregister');
      options.onCleanup?.({ state, settings, window, clock });
      if (options.cleanup === 'throws') throw Error('cleanup rejected');
      if (options.cleanup === 'promise') return Promise.resolve();
      if (options.cleanup === 'rejection') return Promise.reject(Error('async cleanup rejected'));
      assert.equal(stats.active, 1, 'owned unregister must run exactly once'); stats.active--;
    } };
    if (state.mode === 'sync' || state.mode === 'sync-throw') emit(state.snapshot);
    if (state.mode === 'throw' || state.mode === 'sync-throw') {
      state.inRegistration = false; stats.sequence.push('register-throw');
      throw Error('registration failed after possible native installation');
    }
    if (state.mode === 'async') clock.setTimeout(() => emit(state.snapshot), options.delay ?? 25);
    state.inRegistration = false; stats.sequence.push('register-return');
    if (options.handle === 'missing') return undefined;
    if (options.handle === 'null') return null;
    if (options.handle === 'invalid') return { unregister: 1 };
    if (options.handle === 'getter-once') {
      const unregister = handle.unregister;
      return Object.defineProperty(handle, 'unregister', { get() {
        stats.handleGetterReads++;
        assert.equal(stats.handleGetterReads, 1, 'owned unregister getter must be captured only once');
        return unregister;
      } });
    }
    if (options.handle === 'getter-throws') return Object.defineProperty({}, 'unregister', {
      get() { throw Error('unregister contract inaccessible'); }
    });
    return handle;
  }
  if (!options.missingAPI) api.RegisterForFocusChangeEvents = register;
  if (options.nonFunctionAPI) api.RegisterForFocusChangeEvents = 1;
  const SteamClient = { System: { UI: api },
    UI: { RegisterForFocusChangeEvents: action('wrong-UI-register'), GetUIMode: action('GetUIMode'),
      RegisterForUIModeChanged: action('mode-register') },
    Apps: { RaiseWindowForGame: action('RaiseWindowForGame'), LaunchApp: action('LaunchApp'),
      TerminateApp: action('TerminateApp') },
    Input: { GetConfigForAppAndController() {
      stats.configReads++;
      return options.pendingConfig ? new Promise(() => {}) : Promise.resolve({ URL: 'empty.vdf', eSelectionType: 2 });
    } }, Settings: { SetSetting: action('SetSetting') },
    Overlay: { RegisterForOverlayActivated: action('overlay-register') } };
  if (options.noSystem) delete SteamClient.System;
  if (options.noSystemUI) SteamClient.System.UI = null;
  const NativeDate = Date;
  class LogicalDate extends NativeDate { static now() { return clock.now; } }
  // Unmodified full source expression; no extracted helper or substitute code.
  const context = vm.createContext({ window, SteamClient, Date: LogicalDate,
    performance: { now: () => clock.now }, setTimeout: clock.setTimeout,
    clearTimeout: clock.clearTimeout, console });
  const fn = new vm.Script(source, { filename: sourcePath }).runInContext(context, { timeout: 1000 });
  assert.equal(typeof fn, 'function');
  const before = JSON.stringify({ running, overview, editor });
  return { fn, window, state, stats, clock, settings, originalAccountObject, api, emit,
    call: req => clock.settle(fn(req)),
    installAPI() { SteamClient.System = { UI: api }; api.RegisterForFocusChangeEvents = register; },
    safe({ configs = 0, idle = true } = {}) {
      assert.deepEqual(stats.actions, [], 'no setters/launches/rescope/overlay/settings actions');
      assert.deepEqual(stats.writes, [], 'no unrelated store/window writes');
      assert.equal(JSON.stringify({ running, overview, editor }), before, 'Input/Running/backend unchanged');
      assert.equal(stats.configReads, configs, 'runningLinkRead skips controller config RPC');
      assert.equal(stats.cleanupBeforeReturn, 0); assert(stats.maximumActive <= 1);
      assert(!('__ymccSteamLiveBusy' in window), 'ordinary busy guard must be released');
      if (idle) assert.equal(clock.timers.size, 0, 'owned timeout must be cleared');
    } };
}

function feedback(r, { available, callbackReceived, registrationAttempted = true,
  ownSubscriptionUnregistered = true } = {}) {
  assert.equal(r.ok, true, `read failed: ${r.reason}`); assert.equal(r.mutated, false);
  assert.equal(r.gameInput, 'unknown'); assert.equal(r.inputContext.gameInput, 'not-tested');
  assert.equal(r.overviews[0].localDisplayStatus, 11, 'feedback cannot fabricate Running');
  assert.deepEqual(plain(r.runningAppIDs), []);
  const v = r.nativeFocusFeedback;
  assert(v && typeof v === 'object', 'missing nativeFocusFeedback');
  assert.equal(v.implementation, 'steam-native-focus-feedback-v1');
  assert.equal(v.source, 'steam-native-focus-subscription');
  assert.equal(typeof v.reason, 'string'); assert(v.reason.length > 0);
  assert.equal(v.available, available, 'availability requires every native tuple to satisfy the schema');
  assert.equal(v.callbackReceived, callbackReceived);
  assert.equal(v.registrationAttempted, registrationAttempted);
  assert.equal(v.ownSubscriptionUnregistered, ownSubscriptionUnregistered);
  for (const key of ['nativeCacheFreshnessKnown', 'fullGameIdCarried', 'processBirthCarried',
    'gameInputVerified', 'continueVerified']) assert.equal(v[key], false, `${key} must remain unverified`);
  if (available) {
    assert(Array.isArray(v.focusable) && v.focusable.length <= 128);
    for (const row of [v.focused, ...v.focusable]) {
      assert.deepEqual(Object.keys(row).sort(), ['appid', 'exeName', 'pid', 'windowid']);
      for (const key of ['appid', 'pid', 'windowid']) assert(typeof row[key] === 'number' &&
        Number.isInteger(row[key]) && row[key] >= 0 && row[key] <= 0xffffffff);
      assert(row.exeName === null || typeof row.exeName === 'string');
    }
  }
  return v;
}
async function test(name, body) {
  try { await body(); results.push({ name, passed: true }); }
  catch (error) { results.push({ name, passed: false, error: error.message }); }
}

for (const mode of ['async', 'sync']) await test(`${mode}: schema, handle ownership and cleanup after register returns`, async () => {
  const f = fixture({ mode }), req = request(), before = JSON.stringify(req);
  const v = feedback(await f.call(req), { available: true, callbackReceived: true });
  assert.deepEqual(plain(v.focused), normalize(f.state.snapshot.focusedApp));
  assert.deepEqual(plain(v.focusable), f.state.snapshot.rgFocusable.map(normalize));
  assert.equal(f.stats.registrations, 1); assert.equal(f.stats.unregisterCalls, 1); assert.equal(f.stats.active, 0);
  assert(f.stats.sequence.indexOf('register-return') < f.stats.sequence.indexOf('unregister'));
  assert.equal(f.clock.now, mode === 'sync' ? 0 : 25);
  assert(!f.window.__ymccNativeFocusReadQuarantine); assert.equal(JSON.stringify(req), before); f.safe();
});
await test('first callback wins; duplicates and late callbacks cannot rewrite evidence', async () => {
  let f;
  f = fixture({ afterCallback() { f.state.callback({ focusedApp: tuple(456), rgFocusable: [] }); } });
  const r = await f.call(request()); feedback(r, { available: true, callbackReceived: true });
  assert.equal(r.nativeFocusFeedback.focused.appid, 123); const saved = JSON.stringify(r);
  f.state.callback({ focusedApp: tuple(789), rgFocusable: [] });
  assert.equal(JSON.stringify(r), saved); assert.equal(f.stats.unregisterCalls, 1); f.safe();
});
await test('invalid first callback cannot be replaced by a later valid callback', async () => {
  let f;
  f = fixture({ payload: { focusedApp: tuple(), rgFocusable: null }, afterCallback() { f.state.callback(payload()); } });
  feedback(await f.call(request()), { available: false, callbackReceived: true });
  assert.equal(f.stats.unregisterCalls, 1); assert(!f.window.__ymccNativeFocusReadQuarantine); f.safe();
});
for (const mode of ['sync', 'async']) await test(`${mode}: unregister getter captured exactly once`, async () => {
  const f = fixture({ mode, handle: 'getter-once' });
  feedback(await f.call(request()), { available: true, callbackReceived: true });
  assert.equal(f.stats.handleGetterReads, 1); assert.equal(f.stats.unregisterCalls, 1); f.safe();
});
await test('callback emitted during unregister cannot reenter or replace first evidence', async () => {
  let f;
  f = fixture({ onCleanup() { f.state.callback({ focusedApp: tuple(456), rgFocusable: [] }); } });
  const v = feedback(await f.call(request()), { available: true, callbackReceived: true });
  assert.equal(v.focused.appid, 123); assert.equal(f.stats.unregisterCalls, 1); f.safe();
});
await test('copied snapshot is independent of callback-owned mutable objects', async () => {
  const raw = payload();
  const f = fixture({ mode: 'sync', payload: raw, afterCallback() {
    raw.focusedApp.appid = 456; raw.rgFocusable[0].pid = 99; raw.rgFocusable.push(tuple(789));
  } });
  const v = feedback(await f.call(request()), { available: true, callbackReceived: true });
  assert.equal(v.focused.appid, 123); assert.equal(v.focusable[0].pid, 7320); assert.equal(v.focusable.length, 2); f.safe();
});
await test('timeout: exactly 1000 simulated ms, owned unregister, retry remains allowed', async () => {
  const f = fixture({ mode: 'timeout' });
  feedback(await f.call(request()), { available: false, callbackReceived: false });
  assert.equal(f.clock.now, 1000); assert(f.clock.requests.includes(1000));
  assert.equal(f.stats.unregisterCalls, 1); assert.equal(f.stats.active, 0);
  assert(!f.window.__ymccNativeFocusReadQuarantine); f.safe();
  f.state.mode = 'sync'; feedback(await f.call(request()), { available: true, callbackReceived: true });
  assert.equal(f.stats.registrations, 2); assert.equal(f.stats.unregisterCalls, 2); f.safe();
});
await test('callback at 999 simulated ms wins and clears its timeout', async () => {
  const f = fixture({ delay: 999 }); feedback(await f.call(request()), { available: true, callbackReceived: true });
  assert.equal(f.clock.now, 999); assert.equal(f.stats.unregisterCalls, 1); f.safe();
});
await test('overlapping calls return live-busy and never stack subscriptions', async () => {
  const f = fixture(), first = f.fn(request()), second = f.fn(request());
  const [a, b] = await f.clock.settle(Promise.all([first, second]));
  feedback(a, { available: true, callbackReceived: true });
  assert.equal(b.ok, false); assert.equal(b.reason, 'live-busy'); assert.equal(b.mutated, false);
  assert.equal(f.stats.registrations, 1); assert.equal(f.stats.unregisterCalls, 1); f.safe();
});
await test('sequential reads own and release exactly one subscription each', async () => {
  const f = fixture({ mode: 'sync' });
  for (let n = 0; n < 3; n++) { feedback(await f.call(request()), { available: true, callbackReceived: true }); f.safe(); }
  assert.equal(f.stats.registrations, 3); assert.equal(f.stats.unregisterCalls, 3);
});
await test('signed32/unsigned32 AppIDs including zero canonicalize all tuples', async () => {
  for (const appid of [0, 1, 0x7fffffff, -0x80000000, -1895035161, -1, 0x80000000, 2399932135, 0xffffffff]) {
    const f = fixture({ payload: { focusedApp: tuple(appid), rgFocusable: [tuple(appid), tuple(0)] } });
    const v = feedback(await f.call(request()), { available: true, callbackReceived: true });
    assert.equal(v.focused.appid, appid >>> 0); assert.equal(v.focusable[0].appid, appid >>> 0); f.safe();
  }
});
await test('PID/window uint32 endpoints and optional executable names', async () => {
  for (const strExeName of [undefined, null, '', 'game.exe']) {
    const f = fixture({ payload: { focusedApp: tuple(0, { pid: 0, windowid: 0xffffffff, strExeName }),
      rgFocusable: [tuple(123, { pid: 0xffffffff, windowid: 0, strExeName })] } });
    const v = feedback(await f.call(request()), { available: true, callbackReceived: true });
    assert.equal(v.focused.exeName, strExeName ?? null); assert.equal(v.focusable[0].pid, 0xffffffff); f.safe();
  }
});
for (const size of [0, 128]) await test(`rgFocusable boundary ${size}: all tuples valid`, async () => {
  const f = fixture({ payload: { focusedApp: tuple(), rgFocusable: Array.from({ length: size }, (_, i) => tuple(i)) } });
  const v = feedback(await f.call(request()), { available: true, callbackReceived: true }); assert.equal(v.focusable.length, size); f.safe();
});
const invalidPayloads = [
  ['null payload', () => null], ['array payload', () => []],
  ['missing focusedApp', () => ({ rgFocusable: [] })], ['null focusedApp', () => ({ focusedApp: null, rgFocusable: [] })],
  ['string focusedApp', () => ({ focusedApp: '123', rgFocusable: [] })], ['missing rgFocusable', () => ({ focusedApp: tuple() })],
  ['non-array rgFocusable', () => ({ focusedApp: tuple(), rgFocusable: { 0: tuple(), length: 1 } })],
  ['129 tuples', () => ({ focusedApp: tuple(), rgFocusable: Array.from({ length: 129 }, () => tuple()) })],
  ['sparse tuple array', () => ({ focusedApp: tuple(), rgFocusable: Array(1) })],
  ['invalid last tuple', () => ({ focusedApp: tuple(), rgFocusable: [tuple(), tuple(), null] })],
  ['array pretending to be tuple', () => ({ focusedApp: Object.assign([], tuple()), rgFocusable: [] })],
  ['throwing focusedApp getter', () => Object.defineProperty({ rgFocusable: [] }, 'focusedApp', { get() { throw Error('field'); } })],
  ['throwing tuple property', () => ({ focusedApp: tuple(), rgFocusable: [Object.defineProperty(tuple(), 'pid', { get() { throw Error('PID'); } })] })]
];
for (const [name, make] of invalidPayloads) await test(`schema rejects ${name} and still unregisters`, async () => {
  const f = fixture({ payload: make() }); feedback(await f.call(request()), { available: false, callbackReceived: true });
  assert.equal(f.stats.registrations, 1); assert.equal(f.stats.unregisterCalls, 1);
  assert(!f.window.__ymccNativeFocusReadQuarantine, 'schema-only failure stays retryable'); f.safe();
});
for (const field of ['appid', 'pid', 'windowid', 'strExeName']) await test(`invalid ${field} rejects whole focused/focusable snapshot`, async () => {
  const invalid = field === 'strExeName' ? [123, true, {}, []] :
    [undefined, null, '123', true, {}, [], new Number(123), NaN, Infinity, -Infinity, 1.5, 0x100000000,
      ...(field === 'appid' ? [-0x80000001] : [-1, -0x80000000])];
  for (const value of invalid) for (const where of ['focusedApp', 'rgFocusable']) {
    const raw = payload(); if (where === 'focusedApp') raw.focusedApp[field] = value; else raw.rgFocusable[1][field] = value;
    const f = fixture({ payload: raw });
    const r = await f.call(request());
    assert.equal(f.stats.unregisterCalls, 1); f.safe();
    try { feedback(r, { available: false, callbackReceived: true }); }
    catch (error) { error.message = `${field} in ${where}, invalid ${typeof value} ${String(value)}: ${error.message}`; throw error; }
  }
});
for (const kind of ['missingAPI', 'nonFunctionAPI', 'noSystem', 'noSystemUI']) await test(`${kind}: no registration/quarantine, later API recovers`, async () => {
  const f = fixture({ [kind]: true, mode: 'sync' });
  feedback(await f.call(request()), { available: false, callbackReceived: false, registrationAttempted: false, ownSubscriptionUnregistered: false });
  assert.equal(f.stats.registrations, 0); assert(!f.window.__ymccNativeFocusReadQuarantine); f.safe(); f.installAPI();
  feedback(await f.call(request()), { available: true, callbackReceived: true }); f.safe();
});
for (const mode of ['async', 'sync']) for (const handle of ['missing', 'null', 'invalid', 'getter-throws']) await test(`${mode}/${handle}: unusable cleanup handle quarantines subsequent reads`, async () => {
  // The async no-handle fixture emits no event: it may remain native-owned.
  const f = fixture({ mode: mode === 'async' ? 'timeout' : 'sync', handle });
  feedback(await f.call(request()), { available: false, callbackReceived: mode === 'sync', ownSubscriptionUnregistered: false });
  assert(f.window.__ymccNativeFocusReadQuarantine); assert.equal(f.stats.registrations, 1); f.safe();
  feedback(await f.call(request()), { available: false, callbackReceived: false, registrationAttempted: false, ownSubscriptionUnregistered: false });
  assert.equal(f.stats.registrations, 1); assert.equal(f.stats.unregisterCalls, 0); f.safe();
});
for (const mode of ['sync', 'timeout']) for (const cleanup of ['throws', 'promise', 'rejection']) await test(`${mode}/${cleanup}: failed or unknown cleanup quarantines without a second subscription`, async () => {
  const f = fixture({ mode, cleanup });
  feedback(await f.call(request()), { available: false, callbackReceived: mode === 'sync', ownSubscriptionUnregistered: false });
  assert.equal(f.stats.unregisterCalls, 1); assert(f.window.__ymccNativeFocusReadQuarantine); f.safe();
  feedback(await f.call(request()), { available: false, callbackReceived: false, registrationAttempted: false, ownSubscriptionUnregistered: false });
  assert.equal(f.stats.registrations, 1); assert.equal(f.stats.unregisterCalls, 1); f.safe();
});
for (const mode of ['throw', 'sync-throw']) await test(`${mode}: registration throw quarantines despite a possible installed subscription`, async () => {
  const f = fixture({ mode });
  feedback(await f.call(request()), { available: false, callbackReceived: mode === 'sync-throw', ownSubscriptionUnregistered: false });
  assert(f.window.__ymccNativeFocusReadQuarantine); f.safe();
  feedback(await f.call(request()), { available: false, callbackReceived: false, registrationAttempted: false, ownSubscriptionUnregistered: false });
  assert.equal(f.stats.registrations, 1); assert.equal(f.stats.unregisterCalls, 0); f.safe();
});
for (const operation of ['sleep.reconcile', 'sleep.input-resume', 'sleep.session.inspect', 'ui.mode.get',
  'menu.inspect', 'menu.toggle', 'settings.overlay', 'unknown', 'Sleep.inspect']) await test(`native flag rejects ${operation} before any action`, async () => {
  const f = fixture(), r = await f.call({ ...request(), operation });
  assert.equal(r.ok, false); assert.equal(r.reason, 'invalid-steam-native-focus-read');
  assert.equal(r.mutated, false); assert.equal(f.stats.registrations, 0); f.safe();
});
await test('invalid native flag types reject before registration/actions', async () => {
  for (const flag of [null, 0, 1, 'true', 'false', {}, [], new Boolean(true)]) {
    const f = fixture(), r = await f.call({ ...request(), nativeFocusFeedbackRead: flag });
    assert.equal(r.reason, 'invalid-steam-native-focus-read'); assert.equal(r.ok, false);
    assert.equal(r.mutated, false); assert.equal(f.stats.registrations, 0); f.safe();
  }
});
await test('native flag requires exact boolean runningLinkRead true', async () => {
  for (const flag of [undefined, false, null, 1, 'true', {}, [], new Boolean(true)]) {
    const f = fixture(), r = await f.call({ ...request(), runningLinkRead: flag });
    assert.equal(r.reason, 'invalid-steam-native-focus-read'); assert.equal(r.ok, false);
    assert.equal(r.mutated, false); assert.equal(f.stats.registrations, 0); f.safe();
  }
});
await test('false native flag opts out only in its authorized read lane', async () => {
  const f = fixture(), r = await f.call({ ...request(), nativeFocusFeedbackRead: false });
  assert.equal(r.ok, true); assert(!r.nativeFocusFeedback); assert.equal(f.stats.registrations, 0); f.safe();
  const bad = await f.call({ ...request(), operation: 'sleep.reconcile', nativeFocusFeedbackRead: false });
  assert.equal(bad.reason, 'invalid-steam-native-focus-read'); assert.equal(f.stats.registrations, 0); f.safe();
});
await test('ordinary inspect never subscribes and retains config inspection', async () => {
  const f = fixture(), r = await f.call({ operation: 'sleep.inspect', account: 42, appids: [123] });
  assert.equal(r.ok, true); assert(!r.nativeFocusFeedback); assert.equal(f.stats.registrations, 0);
  assert.equal(r.inputContext.configQueries.length, 1); f.safe({ configs: 1 });
});
await test('running-link read without native flag skips a stalled config RPC', async () => {
  const f = fixture({ pendingConfig: true }), r = await f.call({ operation: 'sleep.inspect', account: 42,
    appids: [123], runningLinkRead: true });
  assert.equal(r.ok, true); assert(!r.nativeFocusFeedback); assert.equal(f.stats.registrations, 0);
  assert.equal(r.inputContext.configQueries.length, 0); assert.equal(f.clock.now, 0); f.safe();
});
await test('native feedback read also skips a stalled config RPC', async () => {
  const f = fixture({ pendingConfig: true }); feedback(await f.call(request()), { available: true, callbackReceived: true });
  assert.equal(f.clock.now, 25); f.safe();
});
await test('invalid target AppIDs never register', async () => {
  for (const appids of [[0], [-1], ['123'], [NaN], [1.5], [0x100000000], Array(17).fill(123)]) {
    const f = fixture(), r = await f.call({ ...request(), appids });
    assert.equal(r.ok, false); assert.equal(r.reason, 'invalid-steam-appids'); assert.equal(f.stats.registrations, 0); f.safe();
  }
});
await test('entry account mismatch refuses registration', async () => {
  const f = fixture(); f.state.account = 43;
  const r = await f.call(request()); assert.equal(r.ok, false); assert.equal(r.reason, 'steam-account-changed');
  assert.equal(f.stats.registrations, 0); f.safe();
});
for (const point of ['onRegister', 'beforeCallback', 'onCleanup']) for (const mode of ['sync', 'async']) {
  for (const kind of ['value-drift', 'same-value-object-replacement']) await test(`${mode}/${point}/${kind}: outer failure after owned cleanup`, async () => {
    const f = fixture({ mode, [point]({ state, settings }) {
      if (kind === 'value-drift') state.account = 43;
      else settings.m_CMInterface.steamid = { GetAccountID: () => 42 };
    } });
    const r = await f.call(request()); assert.equal(r.ok, false); assert.equal(r.mutated, false);
    assert(['steam-account-changed', 'steam-session-changed'].includes(r.reason), `wrong account drift reason: ${r.reason}`);
    assert(!r.nativeFocusFeedback?.available, 'account drift cannot expose valid feedback');
    assert.equal(f.stats.registrations, 1); assert.equal(f.stats.unregisterCalls, 1); assert.equal(f.stats.active, 0);
    assert(!f.window.__ymccNativeFocusReadQuarantine, 'successful cleanup stays reusable'); f.safe();
  });
}
await test('account drift during timeout cleans up and fails outer result', async () => {
  for (const replace of [false, true]) {
    const f = fixture({ mode: 'timeout' });
    f.clock.setTimeout(() => {
      if (replace) f.settings.m_CMInterface.steamid = { GetAccountID: () => 42 }; else f.state.account = 43;
    }, 500);
    const r = await f.call(request()); assert.equal(r.ok, false); assert.equal(r.mutated, false);
    assert(!r.nativeFocusFeedback?.available); assert.equal(f.stats.unregisterCalls, 1); assert.equal(f.clock.now, 1000); f.safe();
  }
});
await test('async cleanup rejection produces no process unhandled rejection', async () => {
  // One real event-loop yield drains rejection reporting, never native timing.
  await new Promise(resolve => setImmediate(resolve)); assert.deepEqual(unhandled, []);
});

process.removeListener('unhandledRejection', onUnhandled);
const failed = results.filter(row => !row.passed);
const latestSourceSHA256 = hash(fs.readFileSync(sourcePath, 'utf8'));
console.log(JSON.stringify({ suite: 'steam-native-focus-feedback-full-script-vm', sourcePath,
  sourceSHA256, latestSourceSHA256, sourceChangedOnDiskDuringRun: latestSourceSHA256 !== sourceSHA256,
  timing: 'simulated VM logical milliseconds; 1000ms is not a wall-clock/runtime measurement',
  target: { account: 42, appids: [123] }, coverage, passed: results.length - failed.length,
  failed: failed.length, total: results.length, results }, null, 2));
if (failed.length) process.exitCode = 1;
