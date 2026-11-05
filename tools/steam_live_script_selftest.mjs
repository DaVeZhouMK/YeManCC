import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import { createHash } from 'node:crypto';
const args = process.argv.slice(2);
assert(args.length === 0 || (args.length === 2 && args[0] === '--case-filter' && args[1] === 'light-gamepad-lease'), 'unsupported steam live case filter or arguments');
const caseFilter = args[1] ?? null;
const sourceOverride = Boolean(process.env.YMCC_STEAM_LIVE_SOURCE);
assert(!sourceOverride || caseFilter === 'light-gamepad-lease', 'YMCC_STEAM_LIVE_SOURCE requires --case-filter light-gamepad-lease');
const sourcePath = process.env.YMCC_STEAM_LIVE_SOURCE || 'native/steam_live.js';
const sourceBytes = fs.readFileSync(sourcePath);
const source = sourceBytes.toString('utf8').replace(/\r\n/g,'\n');
function sleepOperationSource() {
  // Delimit on the owned sleep footer, not another task's changing menu syntax.
  const start=source.indexOf("    if (request.operation === 'sleep.inspect'");
  const footer="appBinding:ids.length?'operator-or-native-tracker-supplied-not-proven-by-js':'no-target-supplied'};\n    }";
  const end=source.indexOf(footer,start);
  assert(start>=0&&end>start&&source.lastIndexOf(footer)===end,'production sleep block boundary missing or ambiguous');
  return source.slice(start,end+footer.length);
}
const results = [];
function fixture(options = {}) {
  const stats = { edits: 0, saves: 0, overlayWrites: 0, loads: 0 };
  const account = options.account ?? 42, index = options.controllerIndex ?? 4, type = options.controllerNumericType ?? 2;
  const layoutPath = options.path ?? 'S:\\Steam\\42\\controller_neptune.vdf';
  const group = {mode:7,modeid:options.modeid ?? 22,mode_shift:false,settings:[{key:30,int_value:145},{key:99,int_value:77}]};
  const cfg = {controller_type:type,url:'autosave://'+layoutPath,sets:[{key:'Default',source_bindings:[{key:12,active_group:group}]}]};
  const s = {m_nEditNumber:0,m_nLastSavedEditNumber:0,IsUpdatingEditingConfiguration:false,PreviewedConfiguration:null,EditedConfiguration:cfg,
    EnsureEditingConfiguration(app,index) { stats.loads++;this.EditingConfigurationAppId=app;this.StableAppId=app;this.EditingConfigurationControllerIndex=index;this.m_updatingEditingConfigurationPromise=Promise.resolve(); },
    SetControllerSourceMode(app,msg) { stats.edits++; assert.equal(app,413080);assert.equal(msg.source_binding_key,12);assert.equal(msg.new_setting.key,30);assert.equal(msg.modeid,group.modeid);group.settings[0].int_value=options.clamp?10:msg.new_setting.int_value;this.m_nEditNumber++;if(options.concurrent)this.m_nEditNumber++;this.m_updatingEditingConfigurationPromise=options.timeout?new Promise(()=>{}):Promise.resolve(); },
    SaveEditingConfiguration(app,publish,callback) { stats.saves++;assert.equal(app,413080);assert.equal(publish,false);this.m_nLastSavedEditNumber=this.m_nEditNumber;this.EditingConfigurationAppId=-1;callback(); }
  };
  const settings = { clientSettings:{enable_overlay:false,overlay_fps_counter_corner:0,overlay_fps_counter_detail_level:1,overlay_fps_counter_scale_factor:1,overlay_fps_counter_saturation_factor:1,overlay_fps_counter_bgopacity:1},m_CMInterface:{steamid:{GetAccountID:()=>account}},GetClientSetting(key) {assert(key in this.clientSettings);return [this.clientSettings[key],async value=>{stats.overlayWrites++;this.clientSettings[key]=value;}];} };
  const window = {controllerConfiguratorStore:s,ControllerStore:{GetControllers:()=>('controllers' in options)?options.controllers:[{eControllerType:type,nControllerIndex:index}],GetControllerTypeString:value=>value===type?'controller_neptune':'controller_generic'},settingsStore:settings};
  const steamClient={Settings:{SetSetting(){}}};
  const fn=vm.runInNewContext(options.source ?? source,{window,get location(){return window.location;},SteamClient:steamClient,performance,setTimeout:(cb,n)=>setTimeout(cb,Math.min(n,20)),clearTimeout,console,...options.globals});
  const request = {operation:'mouse.set',controllerType:'controller_neptune',path:layoutPath,account,baseline:145,percent:137};
  return {fn,request,stats,s,cfg,group,window,settings,steamClient};
}
async function test(name,body) {
  try { await body(); results.push(name); }
  catch (error) {
    if (caseFilter === 'light-gamepad-lease') console.error(JSON.stringify({caseFilter, passed:results.length, cases:results, failedCase:name, defaultGroupExecuted:false}));
    throw error;
  }
}
let directedEvidence;
if (caseFilter === 'light-gamepad-lease') {
  const started = performance.now(), owner = 'a'.repeat(32), otherOwner = 'b'.repeat(32);
  const spPath = process.env.YMCC_STEAM_SP_JS;
  assert(spPath, 'light-gamepad-lease requires YMCC_STEAM_SP_JS pointing to a local Valve steamui/sp.js');
  const spBytes = fs.readFileSync(spPath), sp = spBytes.toString('utf8');
  const extract = (name, next, text = sp) => {
    const matches = [...text.matchAll(new RegExp(`\\b${name}\\([^)]*\\)\\{[\\s\\S]*?\\}(?=${next}\\()`, 'g'))];
    assert.equal(matches.length, 1, `ambiguous or unavailable Valve ${name}`);
    return matches[0][0];
  };
  const setterSource = extract('SetCatchAllGamepadInput', 'TakeFocusChangingIFrame');
  const actionSource = extract('OnButtonActionInternal', 'OnButtonDown');
  const managerActionOffset = sp.indexOf(actionSource);
  const managerActionTail = sp.slice(managerActionOffset, sp.indexOf('OnNavigationTypeChange(', managerActionOffset));
  const downSource = extract('OnButtonDown', 'OnButtonUp', managerActionTail), upSource = extract('OnButtonUp', 'BatchedUpdate', managerActionTail);
  const registerSource = extract('RegisterInputSource', 'UpdateSourceToGamepad');
  const sourceTypeMethod = extract('GetSourceType', 'SetControllerActive');
  const nativeInputMatches = [...sp.matchAll(/\bHandleControllerInputMessages\([^)]*\)\{[\s\S]*?\}(?=\}c\(\[)/g)];
  assert.equal(nativeInputMatches.length, 1, 'Valve native controller input method unavailable or ambiguous');
  const nativeInputMethod = nativeInputMatches[0][0];
  assert.match(registerSource, /\.GetSourceType\(\)==\w+\.Vz\.GAMEPAD/);
  assert.match(nativeInputMethod, /activeButtons\[\w+\]=!1,this\.OnButtonUp\(/);
  const keyboardEnumOffset = sp.indexOf('.KEYBOARD_SIMULATOR=');
  assert(keyboardEnumOffset >= 0, 'Valve navigation source enum boundary unavailable');
  const sourceEnum = sp.slice(sp.lastIndexOf('UNKNOWN=', keyboardEnumOffset), keyboardEnumOffset + 300);
  const gamepadTypeMatch = sourceEnum.match(/\b\w+\[\w+\.GAMEPAD=(\d+)\]="GAMEPAD"/);
  const keyboardTypeMatch = sourceEnum.match(/\b\w+\[\w+\.KEYBOARD_SIMULATOR=(\d+)\]="KEYBOARD_SIMULATOR"/);
  assert(gamepadTypeMatch && keyboardTypeMatch, 'Valve navigation source enum unavailable');
  const gamepadType = Number(gamepadTypeMatch[1]), keyboardType = Number(keyboardTypeMatch[1]);
  const buttons = Object.fromEntries(['OK', 'CANCEL', 'DIR_UP', 'DIR_DOWN', 'DIR_LEFT', 'DIR_RIGHT'].map(name => {
    const match = sp.match(new RegExp(`\\w+\\[\\w+\\.${name}=(\\d+)\\]="${name}"`));
    assert(match, `Valve button ${name} unavailable`);
    return [name, Number(match[1])];
  }));
  const receipts = {}, negativeControls = [];
  let buttonBalance;
  function leaseFixture(code = source) {
    const timers = new Map(), scheduled = [], events = [], listeners = new Set();
    const clock = { wall: 1800000000000, mono: 100, next: 1, timerFailure: false, cancelFailure: false };
    const globals = {
      Date: class extends Date { static now() { return clock.wall; } },
      performance: { now: () => clock.mono },
      setTimeout(callback, delay) {
        if (clock.timerFailure === 'throw') throw Error('timer-port-failed');
        if (clock.timerFailure === 'undefined') return undefined;
        assert(Number.isFinite(delay) && delay > 0 && delay <= 3000);
        const timer = { id: clock.next++, callback, due: clock.mono + delay, delay };
        timers.set(timer.id, timer); scheduled.push(timer);
        assert(timers.size <= 1, 'lease accumulated timers');
        return timer.id;
      },
      clearTimeout(id) { if (clock.cancelFailure) throw Error('cancel-port-failed'); timers.delete(id); }
    };
    const f = fixture({ source: code, globals });
    f.window.document = { defaultView: f.window };
    f.window.addEventListener = function(type, callback) {
      assert.equal(this, f.window); assert.equal(type, 'pagehide'); listeners.add(callback);
      if (clock.listenerFailure) throw Error('listener-port-failed');
    };
    f.window.removeEventListener = function(type, callback) {
      assert.equal(this, f.window); assert.equal(type, 'pagehide'); listeners.delete(callback);
    };
    const methods = vm.runInNewContext(`({${setterSource},${actionSource},${downSource},${upSource}})`, {
      pt() {}, Gt: assert, f: { pR: Object.fromEntries(Object.entries(buttons).map(([name, value]) => [value, name])) },
      c: { AE: (target, type, detail) => events.push({ target, type, ...detail }) }
    });
    const foreign = () => false;
    const sourceMethods = vm.runInNewContext(`({${sourceTypeMethod},${nativeInputMethod}})`, {
      f:Object.fromEntries(Object.values(buttons).map(button => [button, button])), e:{pR:buttons}, i:{Hh:{}} });
    const gamepadSource = { ...sourceMethods, m_eNavigationSourceType:gamepadType, m_rgControllers: new Map([[1, { activeButtons: {} }]]) };
    const keyboardSource = { GetSourceType:sourceMethods.GetSourceType, m_eNavigationSourceType:keyboardType };
    const managerMethods = Object.create(null);
    for (const name of ['SetCatchAllGamepadInput', 'OnButtonActionInternal', 'OnButtonDown', 'OnButtonUp'])
      Object.defineProperty(managerMethods, name, { value: methods[name], writable: true, configurable: true, enumerable: false });
    const manager = Object.create(managerMethods);
    Object.assign(manager, { m_rgGamepadInputSources: [gamepadSource, keyboardSource], m_rgCatchAllGamepadInput: [foreign],
      GetEventTarget: () => [{ className: 'fixture-target' }, { BIsGamepadInputSuppressed: () => false }],
      ChangeNavigationSource() {}, BatchedUpdate: callback => callback() });
    f.window.SteamUIStore = { m_GamepadNavigationManager: manager };
    Object.defineProperty(f.window.SteamUIStore, 'NavigationManager', { get() { throw Error('lazy NavigationManager getter used'); } });
    const request = (serial, enabled = true, extra = {}) => ({ operation: 'input.gamepadLease', account: 42, owner, serial, enabled, expiresAt: clock.wall + 3000, ...extra });
    const send = (serial, enabled = true, extra = {}) => f.fn(request(serial, enabled, extra));
    const advance = (milliseconds, fire = true) => {
      clock.mono += milliseconds; clock.wall += milliseconds;
      if (fire) for (const timer of [...timers.values()]) if (timer.due <= clock.mono && timers.delete(timer.id)) timer.callback();
    };
    gamepadSource.GetController = controller => {
      if (!gamepadSource.m_rgControllers.has(controller)) gamepadSource.m_rgControllers.set(controller, {activeButtons:{}});
      return gamepadSource.m_rgControllers.get(controller);
    };
    gamepadSource.OnButtonDown = (button, controller) => manager.OnButtonDown(button, gamepadSource.GetSourceType(), controller, false);
    gamepadSource.OnButtonUp = (button, controller) => manager.OnButtonUp(button, gamepadSource.GetSourceType(), controller);
    const dispatch = (down, button, source = gamepadType, controller = 1, repeat = false, target = manager) => {
      if (source === gamepadType) gamepadSource.GetController(controller).activeButtons[button] = down;
      target.OnButtonActionInternal(down, button, source, controller, repeat, undefined, undefined, false, undefined, undefined);
    };
    const probe = (target = manager, source = gamepadType) => {
      for (const button of Object.values(buttons)) for (const [down, repeat] of [[true, false], [true, true], [false, false]])
        dispatch(down, button, source, 1, repeat, target);
    };
    const assertSuppressed = () => {
      const before = events.length; probe();
      assert.equal(events.length, before, 'actual Valve consumer leaked navigation events');
    };
    const assertReleased = () => {
      const before = events.length; probe(); assert.equal(events.length - before, Object.keys(buttons).length * 3);
      assert.deepEqual(manager.m_rgCatchAllGamepadInput, [foreign]); assert.equal(timers.size, 0); assert.equal(listeners.size, 0);
      assert.equal(f.stats.edits + f.stats.saves + f.stats.overlayWrites + f.stats.loads, 0);
    };
    return { ...f, clock, timers, scheduled, events, listeners, manager, foreign, gamepadSource, keyboardSource, gamepadType, keyboardType, request, send, advance, dispatch, probe, assertSuppressed, assertReleased };
  }
  const receipt = result => {
    assert.equal(result.scope, 'steam-gamepad-navigation');
    assert.equal(typeof result.ok, 'boolean'); assert.equal(typeof result.mutated, 'boolean');
    assert.equal(typeof result.stage, 'string'); assert.equal(typeof result.enabled, 'boolean');
    assert.equal(typeof result.releasing, 'boolean');
    assert(Number.isSafeInteger(result.pendingReleaseCount) && result.pendingReleaseCount >= 0);
    assert(Number.isSafeInteger(result.serial) && result.serial >= 0);
    assert(Number.isFinite(result.remainingMs) && result.remainingMs >= 0 && result.remainingMs <= 3000);
    assert.equal(result.enabled || result.releasing, result.remainingMs > 0);
    assert(!(result.enabled && result.releasing));
    return result;
  };
  await test('light-gamepad-lease: actual Valve button down/up/repeat and four directions are intercepted', async () => {
    const f = leaseFixture(); f.probe(); assert.equal(f.events.length, 18);
    const result = receipt(await f.send(1)); assert(result.ok && result.mutated && result.enabled); assert.equal(result.remainingMs, 3000);
    receipts.acquire = result; f.assertSuppressed(); assert.equal(f.manager.m_rgCatchAllGamepadInput.length, 2);
    assert.equal(f.timers.size, 1); assert.equal(f.listeners.size, 1); f.advance(3000); f.assertReleased();
  });
  await test('light-gamepad-lease: exact release and duplicate release retain tombstone and foreign catchall', async () => {
    const f = leaseFixture(); await f.send(1); const old = f.window.__ymccLightGamepadLease;
    const result = receipt(await f.send(2, false)); assert(result.ok && result.mutated && !result.enabled); receipts.release = result;
    f.assertReleased(); const duplicate = receipt(await f.send(2, false)); assert(duplicate.ok && !duplicate.mutated);
    assert.equal(f.window.__ymccLightGamepadLease.serial, 2); assert.equal(f.window.__ymccLightGamepadLease.owner, owner);
    assert.equal(old.callback(), false); f.assertReleased();
  });
  await test('light-gamepad-lease: one-shot crash timeout expires without any later input or polling', async () => {
    const f = leaseFixture(); await f.send(1); f.advance(2999); f.assertSuppressed(); f.advance(1);
    assert.equal(f.window.__ymccLightGamepadLease.enabled, false); assert.equal(f.scheduled.length, 1); f.assertReleased();
    const result = receipt(await f.send(1)); assert(!result.ok); assert.equal(f.window.__ymccLightGamepadLease.serial, 1);
  });
  await test('light-gamepad-lease: expired callback fails open before delayed timeout and cleans only itself', async () => {
    const f = leaseFixture(); await f.send(1); f.advance(3001, false); f.assertReleased();
    assert.equal(f.window.__ymccLightGamepadLease.owner, owner); assert.equal(f.window.__ymccLightGamepadLease.serial, 1);
  });
  await test('light-gamepad-lease: delayed request keeps native remaining TTL and wall rollback cannot extend it', async () => {
    const f = leaseFixture(), request = f.request(1); f.advance(1200); const result = receipt(await f.fn(request));
    assert(result.ok); assert.equal(result.remainingMs, 1800); assert.equal(f.scheduled[0].delay, 1800);
    f.clock.wall -= 100000; f.advance(1800); f.assertReleased();
  });
  await test('light-gamepad-lease: wall-clock forward jump expires at the consumer even before monotonic deadline', async () => {
    const f = leaseFixture(); await f.send(1); f.clock.wall += 3001; f.assertReleased();
  });
  await test('light-gamepad-lease: duplicate ON cannot extend TTL or allocate a timer', async () => {
    const f = leaseFixture(), request = f.request(1); await f.fn(request); f.advance(750);
    const result = receipt(await f.fn(request)); assert(result.ok && !result.mutated); assert.equal(result.remainingMs, 2250);
    assert.equal(f.scheduled.length, 1); assert(!(await f.send(1)).ok); f.advance(2250); f.assertReleased();
  });
  await test('light-gamepad-lease: serial renewal cancels old timer and old callbacks cannot revoke a new lease', async () => {
    const f = leaseFixture(); await f.send(1); const old = f.window.__ymccLightGamepadLease, oldTimer = f.scheduled[0]; f.advance(750);
    const result = receipt(await f.send(2)); assert(result.ok && result.enabled); receipts.renew = result;
    assert.equal(f.timers.size, 1); assert.equal(f.manager.m_rgCatchAllGamepadInput.length, 2); assert.equal(f.listeners.size, 1);
    oldTimer.callback(); old.pagehide(); assert.equal(old.callback(), false); f.assertSuppressed();
    for (const enabled of [true, false]) assert(!(await f.send(1, enabled)).ok);
    await f.send(3, false); assert(!(await f.send(2)).ok); assert(!(await f.send(3)).ok);
    assert((await f.send(4)).ok); f.assertSuppressed(); f.advance(3000); f.assertReleased();
  });
  await test('light-gamepad-lease: cancellation before ON records tombstone and repeated cancellation is idempotent', async () => {
    const f = leaseFixture(); assert((await f.send(5, false)).ok); assert(!(await f.send(4)).ok); assert(!(await f.send(5)).ok);
    const result = receipt(await f.send(5, false)); assert(result.ok && !result.mutated); f.assertReleased();
    assert((await f.send(6)).ok); f.advance(3000); f.assertReleased();
  });
  await test('light-gamepad-lease: release without a manager never evaluates a lazy navigation getter', async () => {
    const f = leaseFixture(); delete f.window.SteamUIStore.m_GamepadNavigationManager;
    const result = receipt(await f.send(1, false)); assert(result.ok && !result.enabled); f.assertReleased();
  });
  await test('light-gamepad-lease: a foreign catchall installed later survives owned timeout cleanup', async () => {
    const f = leaseFixture(); await f.send(1); const foreign = () => false;
    const handle = f.manager.SetCatchAllGamepadInput(foreign); f.advance(3000);
    assert.deepEqual(f.manager.m_rgCatchAllGamepadInput, [f.foreign, foreign]); handle.Unregister(); f.assertReleased();
  });
  await test('light-gamepad-lease: foreign owner is rejected while live and can acquire after expiry with its own serial', async () => {
    const f = leaseFixture(); await f.send(20);
    for (const enabled of [true, false]) assert(!(await f.send(1, enabled, { owner: otherOwner })).ok);
    f.assertSuppressed(); f.advance(3000); assert((await f.send(1, true, { owner: otherOwner })).ok);
    const current = f.window.__ymccLightGamepadLease;
    assert(!(await f.send(100, false)).ok); assert.equal(f.window.__ymccLightGamepadLease, current);
    f.assertSuppressed(); f.advance(3000); f.assertReleased();
  });
  await test('light-gamepad-lease: expired release never advances tombstone or clears a newer owner', async () => {
    const f = leaseFixture(); await f.send(1); const expired = f.request(999, false); f.advance(3000);
    const before = f.window.__ymccLightGamepadLease;
    assert(!(await f.fn(expired)).ok); assert.equal(f.window.__ymccLightGamepadLease, before); assert.equal(before.serial, 1);
    assert((await f.send(1, true, { owner: otherOwner })).ok); const current = f.window.__ymccLightGamepadLease;
    assert(!(await f.fn(expired)).ok); assert.equal(f.window.__ymccLightGamepadLease, current); assert.equal(current.serial, 1);
    assert(!(await f.send(500, false, { owner: otherOwner, expiresAt: f.clock.wall })).ok);
    f.assertSuppressed(); f.advance(3000); f.assertReleased();
  });
  await test('light-gamepad-lease: malformed fields and TTL bounds preserve the live owner and serial', async () => {
    const f = leaseFixture(); await f.send(1); const current = f.window.__ymccLightGamepadLease;
    for (const extra of [{owner:'A'.repeat(32)}, {owner:'a'.repeat(31)}, {owner:null}, {serial:0}, {serial:-1}, {serial:1.5},
      {serial:Number.MAX_SAFE_INTEGER+1}, {serial:'2'}, {enabled:1}, {enabled:'false'}, {expiresAt:NaN}, {expiresAt:'1800000003000'},
      {expiresAt:f.clock.wall-1}, {expiresAt:f.clock.wall}, {expiresAt:f.clock.wall+3001}, {expiresAt:f.clock.wall+1.5}]) {
      const result = receipt(await f.send(2, true, extra)); assert(!result.ok && !result.mutated);
      assert.equal(f.window.__ymccLightGamepadLease, current); assert.equal(current.serial, 1);
    }
    f.assertSuppressed(); f.advance(3000); f.assertReleased();
  });
  await test('light-gamepad-lease: original account gate rejects without replacing the lease', async () => {
    const f = leaseFixture(); const denied = receipt(await f.send(1, true, {account:99})); assert(!denied.ok && !denied.mutated);
    assert.equal(f.window.__ymccLightGamepadLease, undefined); await f.send(1); const current = f.window.__ymccLightGamepadLease;
    assert(!(await f.send(2, false, {account:99})).ok); assert.equal(f.window.__ymccLightGamepadLease, current);
    f.assertSuppressed(); f.advance(3000); f.assertReleased();
  });
  await test('light-gamepad-lease: another operation busy allows lease renewal and release without clearing its owner', async () => {
    const f = leaseFixture(); await f.send(1); f.window.__ymccSteamLiveBusy = 'parallel-settings-owner'; f.advance(750);
    const renew = receipt(await f.send(2)); assert(renew.ok && renew.enabled && renew.remainingMs === 3000);
    assert.equal(f.window.__ymccSteamLiveBusy, 'parallel-settings-owner'); f.assertSuppressed();
    const ordinary = await f.fn({operation:'overlay.set',account:42,value:1});
    assert(!ordinary.ok && !ordinary.mutated && ordinary.reason === 'live-busy'); assert.equal(f.stats.overlayWrites, 0);
    assert(!(await f.send(3, false, {account:99})).ok); assert.equal(f.window.__ymccSteamLiveBusy, 'parallel-settings-owner');
    const release = receipt(await f.send(3, false)); assert(release.ok && !release.enabled);
    assert.equal(f.window.__ymccSteamLiveBusy, 'parallel-settings-owner'); f.assertReleased();
    assert((await f.send(3, false)).ok); assert.equal(f.window.__ymccSteamLiveBusy, 'parallel-settings-owner');
  });
  for (const change of ['account', 'identity', 'cm', 'settings', 'manager', 'store', 'document', 'defaultView', 'closed', 'callback-list', 'setter'])
    await test(`light-gamepad-lease: ${change} identity change fails open at the actual consumer`, async () => {
      const f = leaseFixture(); await f.send(1); const old = f.window.__ymccLightGamepadLease;
      if (change === 'account') f.settings.m_CMInterface.steamid.GetAccountID = () => 43;
      if (change === 'identity') f.settings.m_CMInterface.steamid = {GetAccountID:()=>42};
      if (change === 'cm') f.settings.m_CMInterface = {steamid:f.settings.m_CMInterface.steamid};
      if (change === 'settings') f.window.settingsStore = {...f.settings};
      if (change === 'manager') f.window.SteamUIStore.m_GamepadNavigationManager = {...f.manager};
      if (change === 'store') f.window.SteamUIStore = {...f.window.SteamUIStore};
      if (change === 'document') f.window.document = {defaultView:f.window};
      if (change === 'defaultView') f.window.document.defaultView = {};
      if (change === 'closed') f.window.closed = true;
      if (change === 'callback-list') f.manager.m_rgCatchAllGamepadInput = [...f.manager.m_rgCatchAllGamepadInput];
      if (change === 'setter') f.manager.SetCatchAllGamepadInput = () => {throw Error('replaced');};
      f.probe(); assert.equal(f.events.length, 18); assert.equal(old.enabled, false);
      assert.equal(f.timers.size, 0); assert.equal(f.listeners.size, 0); assert.equal(old.callback(), false);
      assert.deepEqual(f.manager.m_rgCatchAllGamepadInput, [f.foreign]);
      assert.equal(f.window.__ymccLightGamepadLease.serial, 1);
    });
  await test('light-gamepad-lease: pagehide cancels timeout and hook and cannot reenable the same document', async () => {
    const f = leaseFixture(); await f.send(1); const old = f.window.__ymccLightGamepadLease;
    for (const callback of [...f.listeners]) callback(); old.pagehide(); f.assertReleased();
    assert(!(await f.send(2)).ok); f.assertReleased(); assert.equal(f.window.__ymccLightGamepadLease.serial, 2);
  });
  for (const missing of ['manager-data', 'setter', 'callback-data', 'document'])
    await test(`light-gamepad-lease: unavailable ${missing} refuses without invoking lazy getters`, async () => {
      const f = leaseFixture();
      if (missing === 'manager-data') Object.defineProperty(f.window.SteamUIStore, 'm_GamepadNavigationManager', {get(){throw Error('manager getter used');}});
      if (missing === 'setter') f.manager.SetCatchAllGamepadInput = undefined;
      if (missing === 'callback-data') Object.defineProperty(f.manager, 'm_rgCatchAllGamepadInput', {get(){throw Error('callback getter used');}});
      if (missing === 'document') delete f.window.document;
      const result = receipt(await f.send(1)); assert(!result.ok && !result.enabled); assert.equal(f.timers.size, 0); assert.equal(f.listeners.size, 0);
    });
  for (const failure of ['throw-after-push', 'bad-handle', 'no-registration', 'double-registration'])
    await test(`light-gamepad-lease: registration ${failure} removes only its own hook`, async () => {
      const f = leaseFixture(), setter = f.manager.SetCatchAllGamepadInput;
      f.manager.SetCatchAllGamepadInput = function(callback) {
        if (failure === 'no-registration') return {Unregister(){}};
        const handle = setter.call(this, callback);
        if (failure === 'throw-after-push') throw Error('registration-failed');
        if (failure === 'bad-handle') return {};
        if (failure === 'double-registration') setter.call(this, callback);
        return handle;
      };
      const result = receipt(await f.send(1)); assert(!result.ok && !result.enabled); f.assertReleased();
    });
  for (const failure of ['throw', 'undefined'])
    await test(`light-gamepad-lease: timer ${failure} unregisters the hook and tombstones the attempt`, async () => {
      const f = leaseFixture(); f.clock.timerFailure = failure;
      const result = receipt(await f.send(1)); assert(!result.ok && !result.enabled); f.assertReleased();
      f.clock.timerFailure = false; assert(!(await f.send(1)).ok); assert((await f.send(2)).ok); f.advance(3000); f.assertReleased();
    });
  await test('light-gamepad-lease: listener failure and registration overrun return failure with no hook residue', async () => {
    const f = leaseFixture(); f.clock.listenerFailure = true;
    assert(!receipt(await f.send(1)).ok); f.assertReleased();
    f.clock.listenerFailure = false; const setter = f.manager.SetCatchAllGamepadInput;
    f.manager.SetCatchAllGamepadInput = function(callback) { const handle = setter.call(this, callback); f.advance(3000); return handle; };
    assert(!receipt(await f.send(2)).ok); f.assertReleased();
  });
  await test('light-gamepad-lease: account changes during the final receipt cannot report successful acquisition', async () => {
    const f = leaseFixture(); let reads = 0;
    f.settings.m_CMInterface.steamid.GetAccountID = () => ++reads < 4 ? 42 : 43;
    const result = receipt(await f.send(1)); assert(!result.ok && !result.enabled); f.assertReleased();
  });
  await test('light-gamepad-lease: cancellation failure never reports success and a late timer cannot revoke a successor', async () => {
    const f = leaseFixture(); await f.send(1); const late = f.scheduled[0]; f.clock.cancelFailure = true;
    const result = receipt(await f.send(2, false)); assert(!result.ok && result.mutated && !result.enabled); assert.equal(f.manager.m_rgCatchAllGamepadInput.length, 1);
    f.clock.cancelFailure = false; assert((await f.send(2, false)).ok); assert((await f.send(3)).ok);
    late.callback(); f.assertSuppressed(); f.advance(3000); f.assertReleased();
  });
  await test('light-gamepad-lease: unregister exception is failure even when owned-callback fallback succeeds', async () => {
    const f = leaseFixture(), setter = f.manager.SetCatchAllGamepadInput;
    f.manager.SetCatchAllGamepadInput = function(callback) { setter.call(this, callback); return {Unregister(){throw Error('unregister-failed');}}; };
    assert((await f.send(1)).ok); const result = receipt(await f.send(2, false)); assert(!result.ok && result.mutated && !result.enabled); f.assertReleased();
  });
  await test('light-gamepad-lease: pre-hook down is naturally released without synthesized input', async () => {
    const f = leaseFixture(); f.dispatch(true, buttons.OK);
    assert.equal(f.events.length, 1); assert.equal(f.events[0].type, 'vgp_onbuttondown');
    await f.send(1); f.dispatch(false, buttons.OK);
    assert.equal(f.events.length, 2); assert.equal(f.events[1].type, 'vgp_onbuttonup');
    await f.send(2, false); assert.equal(f.events.length, 2);
    buttonBalance = { status:'RESOLVED_NATURAL_UP', priorDownDispatched:1, hookedUpDispatched:1, syntheticEvents:0 };
    f.assertReleased();
  });
  await test('light-gamepad-lease: disabled-hook negative control is caught by actual Valve consumer leakage', async () => {
    const hook = 'dispatch.repeat === repeat && dispatch.block'; assert.equal(source.split(hook).length, 2);
    const f = leaseFixture(source.replace(hook, 'dispatch.repeat === repeat && false'));
    assert((await f.send(1)).ok);
    assert.throws(f.assertSuppressed, error => error instanceof assert.AssertionError && /actual Valve consumer leaked/.test(error.message));
    assert.equal(f.events.length, 18); negativeControls.push({ name:'disabled-hook', detected:true, actualConsumerLeakedEvents:f.events.length });
    f.advance(3000); f.assertReleased();
  });
  await test('light-gamepad-lease: actual native input consumer updates held state before the natural prior UP', async () => {
    const f = leaseFixture(); f.gamepadSource.HandleControllerInputMessages(1, buttons.OK, true, 0, 0);
    assert.equal(f.events.length, 1); assert.equal(f.gamepadSource.m_rgControllers.get(1).activeButtons[buttons.OK], true);
    assert((await f.send(1)).ok); assert.equal(f.events.length, 1);
    f.gamepadSource.HandleControllerInputMessages(1, buttons.OK, false, 0, 0);
    assert.equal(f.gamepadSource.m_rgControllers.get(1).activeButtons[buttons.OK], false);
    assert.equal(f.events.length, 2); assert.equal(f.events[1].type, 'vgp_onbuttonup');
    f.dispatch(false, buttons.OK); assert.equal(f.events.length, 2, 'prior UP can be allowed only once');
    assert((await f.send(2, false)).ok); assert.equal(f.events.length, 2); f.assertReleased();
  });
  await test('light-gamepad-lease: controller-specific prior UP never pays another controller blocked DOWN', async () => {
    const f = leaseFixture(); f.dispatch(true, buttons.OK, gamepadType, 1); assert((await f.send(1)).ok);
    f.dispatch(true, buttons.OK, gamepadType, 2); assert.equal(f.events.length, 1);
    f.dispatch(false, buttons.OK, gamepadType, 2); assert.equal(f.events.length, 1);
    f.dispatch(true, buttons.OK, gamepadType, 1, true); assert.equal(f.events.length, 1);
    f.dispatch(false, buttons.OK, gamepadType, 1); assert.equal(f.events.length, 2);
    const release = receipt(await f.send(2, false)); assert(release.ok && !release.releasing); f.assertReleased();
  });
  await test('light-gamepad-lease: keyboard mouse and VR dispatch sources pass unchanged through actual action', async () => {
    const f = leaseFixture(); assert((await f.send(1)).ok);
    for (const sourceType of [keyboardType, 3, 99]) { const before = f.events.length; f.probe(f.manager, sourceType); assert.equal(f.events.length - before, 18); }
    assert.equal(f.window.__ymccLightGamepadLease.blockedDown.size, 0); f.assertSuppressed();
    assert((await f.send(2, false)).ok); f.assertReleased();
  });
  await test('light-gamepad-lease: tail blocks only held blocked repeats and UP while new gamepad keys work', async () => {
    const f = leaseFixture(); assert((await f.send(1)).ok); f.dispatch(true, buttons.OK);
    const tail = receipt(await f.send(2, false)); assert(tail.ok && !tail.enabled && tail.releasing && tail.pendingReleaseCount === 1);
    receipts.tail = tail; assert.equal(f.events.length, 0);
    f.dispatch(true, buttons.OK, gamepadType, 1, true); assert.equal(f.events.length, 0);
    f.dispatch(true, buttons.CANCEL); f.dispatch(false, buttons.CANCEL); assert.equal(f.events.length, 2);
    f.dispatch(false, buttons.OK); assert.equal(f.events.length, 2, 'blocked DOWN matching UP must not leak');
    const settled = receipt(await f.send(2, false)); assert(settled.ok && !settled.enabled && !settled.releasing && settled.pendingReleaseCount === 0);
    assert.equal(Object.hasOwn(f.manager, 'OnButtonActionInternal'), false); f.assertReleased();
  });
  await test('light-gamepad-lease: tail keeps two controllers separate and settles only after both matching UPs', async () => {
    const f = leaseFixture(); await f.send(1); f.dispatch(true, buttons.OK, gamepadType, 1); f.dispatch(true, buttons.OK, gamepadType, 2);
    let result = receipt(await f.send(2, false)); assert(result.releasing && result.pendingReleaseCount === 2);
    f.dispatch(false, buttons.OK, gamepadType, 2); assert.equal(f.events.length, 0);
    result = receipt(await f.send(2, false)); assert(result.releasing && result.pendingReleaseCount === 1);
    f.dispatch(false, buttons.OK, gamepadType, 1); assert.equal(f.events.length, 0); f.assertReleased();
  });
  await test('light-gamepad-lease: ON renewal carries held sets without reclassifying blocked DOWN as prior UP', async () => {
    const f = leaseFixture(); f.dispatch(true, buttons.CANCEL); await f.send(1); f.dispatch(true, buttons.OK);
    f.advance(750); assert((await f.send(2)).ok); const current = f.window.__ymccLightGamepadLease;
    assert.equal(current.priorUp.size, 1); assert.equal(current.blockedDown.size, 1);
    f.dispatch(true, buttons.CANCEL, gamepadType, 1, true); assert.equal(current.blockedDown.size, 1);
    f.dispatch(false, buttons.OK); assert.equal(f.events.length, 1);
    f.dispatch(false, buttons.CANCEL); assert.equal(f.events.length, 2);
    assert((await f.send(3, false)).ok); f.assertReleased();
  });
  await test('light-gamepad-lease: repeated OFF and OFF renewal retain tail and a bounded single watchdog', async () => {
    const f = leaseFixture(); await f.send(1); f.dispatch(true, buttons.OK); const off = f.request(2, false);
    assert(receipt(await f.fn(off)).releasing); f.advance(750);
    const duplicate = receipt(await f.fn(off)); assert(duplicate.releasing && !duplicate.mutated && duplicate.remainingMs === 2250);
    const renewed = receipt(await f.send(3, false)); assert(renewed.releasing && renewed.remainingMs === 3000 && renewed.pendingReleaseCount === 1);
    assert.equal(f.timers.size, 1); assert.equal(f.listeners.size, 1); assert.equal(f.manager.m_rgCatchAllGamepadInput.length, 2);
    const current = f.window.__ymccLightGamepadLease;
    assert(!(await f.send(99, false, {expiresAt:f.clock.wall})).ok); assert.equal(f.window.__ymccLightGamepadLease, current);
    assert(!(await f.send(1, true, {owner:otherOwner})).ok);
    f.dispatch(false, buttons.OK); assert.equal(f.events.length, 0); f.assertReleased();
  });
  await test('light-gamepad-lease: tail reentry carries old blocked keys and newly allowed held keys', async () => {
    const f = leaseFixture(); await f.send(1); f.dispatch(true, buttons.OK); assert(receipt(await f.send(2, false)).releasing);
    f.dispatch(true, buttons.CANCEL); assert.equal(f.events.length, 1);
    assert(receipt(await f.send(3)).enabled); const current = f.window.__ymccLightGamepadLease;
    assert.equal(current.blockedDown.size, 1); assert.equal(current.priorUp.size, 1);
    f.dispatch(false, buttons.OK); assert.equal(f.events.length, 1);
    f.dispatch(false, buttons.CANCEL); assert.equal(f.events.length, 2);
    assert((await f.send(4, false)).ok); f.assertReleased();
  });
  await test('light-gamepad-lease: prior UP never forces a release tail and foreign catchalls can still veto', async () => {
    const f = leaseFixture(); f.dispatch(true, buttons.OK); await f.send(1);
    const foreign = f.manager.SetCatchAllGamepadInput(() => true);
    f.dispatch(false, buttons.OK); assert.equal(f.events.length, 1); assert.equal(f.window.__ymccLightGamepadLease.priorUp.size, 0);
    foreign.Unregister(); const settled = receipt(await f.send(2, false)); assert(!settled.releasing); f.assertReleased();
    const g = leaseFixture(); g.dispatch(true, buttons.OK); await g.send(1);
    assert(!receipt(await g.send(2, false)).releasing); assert.equal(g.events.length, 1);
    g.dispatch(false, buttons.OK); assert.equal(g.events.length, 2); g.assertReleased();
  });
  for (const stop of ['timeout', 'pagehide', 'account']) await test(`light-gamepad-lease: ${stop} clears a live tail hook wrapper timer and listener`, async () => {
    const f = leaseFixture(); await f.send(1); f.dispatch(true, buttons.OK); assert(receipt(await f.send(2, false)).releasing);
    if (stop === 'timeout') f.advance(3000);
    if (stop === 'pagehide') for (const callback of [...f.listeners]) callback();
    if (stop === 'account') { f.settings.m_CMInterface.steamid.GetAccountID = () => 43; f.dispatch(false, buttons.OK); }
    assert.equal(Object.hasOwn(f.manager, 'OnButtonActionInternal'), false); f.assertReleased();
  });
  await test('light-gamepad-lease: exact own action descriptor is restored after release', async () => {
    const f = leaseFixture(), original = f.manager.OnButtonActionInternal;
    Object.defineProperty(f.manager, 'OnButtonActionInternal', {value:original,writable:true,configurable:true,enumerable:false});
    const descriptor = Object.getOwnPropertyDescriptor(f.manager, 'OnButtonActionInternal');
    assert((await f.send(1)).ok); assert((await f.send(2, false)).ok);
    assert.deepEqual(Object.getOwnPropertyDescriptor(f.manager, 'OnButtonActionInternal'), descriptor); f.assertReleased();
  });
  await test('light-gamepad-lease: external replacement wrapping ours survives while ours becomes inert', async () => {
    const f = leaseFixture(); await f.send(1); const ours = f.manager.OnButtonActionInternal;
    const foreign = function(...args) { return ours.apply(this,args); }; f.manager.OnButtonActionInternal = foreign;
    f.dispatch(true, buttons.OK); f.dispatch(false, buttons.OK); assert.equal(f.events.length, 2);
    assert.equal(f.manager.OnButtonActionInternal, foreign); assert.equal(f.window.__ymccLightGamepadLease.enabled, false);
    f.assertReleased(); assert.equal(f.manager.OnButtonActionInternal, foreign);
  });
  for (const kind of ['sources', 'type-method', 'controllers', 'held-schema']) await test(`light-gamepad-lease: unsupported ${kind} fails without retaining owned resources`, async () => {
    const f = leaseFixture();
    if (kind === 'sources') delete f.manager.m_rgGamepadInputSources;
    if (kind === 'type-method') { f.gamepadSource.GetType = () => gamepadType; delete f.gamepadSource.GetSourceType; }
    if (kind === 'controllers') f.gamepadSource.m_rgControllers = {};
    if (kind === 'held-schema') f.gamepadSource.m_rgControllers.get(1).activeButtons[buttons.OK] = 1;
    const result = receipt(await f.send(1)); assert(!result.ok && !result.enabled && !result.releasing);
    assert.equal(Object.hasOwn(f.manager, 'OnButtonActionInternal'), false);
    assert.equal(f.manager.m_rgCatchAllGamepadInput.length, 1); assert.equal(f.timers.size, 0); assert.equal(f.listeners.size, 0);
  });
  await test('light-gamepad-lease: missing prior UP exception negative control catches actual consumer imbalance', async () => {
    const exception = 'block = false;\n                  lease.priorUp.delete(key);'; assert.equal(source.split(exception).length, 2);
    const f = leaseFixture(source.replace(exception, 'block = true;\n                  lease.priorUp.delete(key);'));
    f.dispatch(true, buttons.OK); assert((await f.send(1)).ok); f.dispatch(false, buttons.OK);
    assert.throws(() => assert.equal(f.events.length, 2, 'actual prior UP balance failed'), error => error instanceof assert.AssertionError);
    assert.equal(f.events.length, 1); negativeControls.push({name:'missing-prior-up',detected:true,actualDownEvents:1,actualUpEvents:0});
    f.advance(3000); f.assertReleased();
  });
  directedEvidence = { caseFilter, defaultGroupExecuted:false, productionExecution:`complete ${sourcePath}`, sourcePath, sourceOverride,
    sourceSha256:createHash('sha256').update(sourceBytes).digest('hex'), executedSourceSha256:createHash('sha256').update(source).digest('hex'),
    selftestSha256:createHash('sha256').update(fs.readFileSync('tools/steam_live_script_selftest.mjs')).digest('hex'),
    consumer:{path:spPath, bytes:spBytes.length, sha256:createHash('sha256').update(spBytes).digest('hex'),
      methods:['OnButtonActionInternal','SetCatchAllGamepadInput','OnButtonDown','OnButtonUp','GetSourceType','HandleControllerInputMessages'],
      inspectedMethods:['RegisterInputSource'], gamepadType, keyboardType,
      methodsSha256:createHash('sha256').update([setterSource,actionSource,downSource,upSource,sourceTypeMethod,nativeInputMethod,registerSource].join('\n')).digest('hex'), buttons},
    receipts, negativeControls, buttonBalance, elapsedMs:performance.now()-started, node:process.version,
    scope:'steam-gamepad-navigation', deviceOperations:0, unknown:['raw-axis-and-other-consumers','physical-input','all-SteamInput','all-analog-stick-APIs','real-Steam','native-CDP-integration','generated-header-not-run'] };
} else {
await test('actual embedded script is in sync',async()=>{const header=fs.readFileSync('native/steam_live_script.h','utf8');const chunks=[...header.split('inline constexpr const char kUiModeScript')[0].matchAll(/R"YMSTEAM\(([\s\S]*?)\)YMSTEAM"/g)].map(m=>m[1]);assert.equal(chunks.join(''),source.trimEnd());assert(chunks.every(c=>Buffer.byteLength(c)<=8192));});
await test('native edits/save use exact Steam desktop source/setting and preserve unrelated values',async()=>{const f=fixture(),r=await f.fn(f.request);assert(r.ok);assert(r.saved);assert.equal(r.percent,137);assert.equal(r.controllerIndex,4);assert.equal(f.stats.edits,1);assert.equal(f.stats.saves,1);assert.equal(f.group.settings[1].int_value,77);assert(!f.window.__ymccSteamLiveBusy);});
await test('read and same-value write do not modify or save',async()=>{const f=fixture();assert((await f.fn({...f.request,operation:'mouse.get'})).ok);assert((await f.fn({...f.request,percent:145})).ok);assert.equal(f.stats.edits,0);assert.equal(f.stats.saves,0);});
await test('current 323 is readable, never automatically clamped',async()=>{const f=fixture();f.group.settings[0].int_value=323;const r=await f.fn({...f.request,operation:'mouse.get'});assert.equal(r.percent,323);assert.equal(f.stats.edits,0);});
for (const percent of [1,100,300]) await test(`UI write boundary ${percent} goes through native and readback`,async()=>{const f=fixture();assert((await f.fn({...f.request,percent})).ok);assert.equal(f.stats.saves,1);});
for (const percent of [0,301,1.5,4294967297]) await test(`invalid request ${percent} never mutates`,async()=>{const f=fixture();const r=await f.fn({...f.request,percent});assert.equal(r.reason,'invalid-sensitivity');assert(!r.mutated);assert.equal(f.stats.edits,0);});
await test('dirty/preview/game editor conflicts never load or mutate',async()=>{for(const kind of ['dirty','preview','game']){const f=fixture();if(kind==='dirty')f.s.m_nEditNumber=1;if(kind==='preview')f.s.PreviewedConfiguration={};if(kind==='game')f.s.EditingConfigurationAppId=123;const r=await f.fn(f.request);assert.equal(r.reason,'live-editor-busy');assert.equal(f.stats.loads,0);assert.equal(f.stats.saves,0);}});
await test('zero live account during Steam reconnect is temporarily not ready, not a real account switch',async()=>{
 const f=fixture();f.window.settingsStore.m_CMInterface.steamid.GetAccountID=()=>0;
 const r=await f.fn({operation:'sleep.inspect',account:42,appids:[]});
 assert(!r.ok&&!r.mutated);assert.equal(r.reason,'steam-session-starting');assert.equal(f.stats.edits+f.stats.saves+f.stats.overlayWrites,0);
});
await test('account drift never mutates another account',async()=>{const f=fixture(),r=await f.fn({...f.request,account:43});assert.equal(r.reason,'steam-account-changed');assert.equal(f.stats.edits,0);});
await test('ambiguous/wrong devices never guessed',async()=>{for(const controllers of [[],[{eControllerType:30,nControllerIndex:0}],[{eControllerType:2,nControllerIndex:0},{eControllerType:2,nControllerIndex:1}]]){const f=fixture({controllers}),r=await f.fn(f.request);assert(!r.ok);assert.equal(f.stats.edits,0);}});
await test('wrong layout, mode, mode shift, duplicate source/setting refused',async()=>{for(const kind of ['url','mode','shift','source','setting']){const f=fixture();if(kind==='url')f.cfg.url+='wrong';if(kind==='mode')f.group.mode=4;if(kind==='shift')f.group.mode_shift=true;if(kind==='source')f.cfg.sets[0].source_bindings.push({...f.cfg.sets[0].source_bindings[0]});if(kind==='setting')f.group.settings.push({...f.group.settings[0]});assert(!(await f.fn(f.request)).ok);assert.equal(f.stats.edits,0);}});
await test('changed runtime baseline wins over stale caller',async()=>{const f=fixture(),r=await f.fn({...f.request,baseline:119});assert.equal(r.reason,'desktop-sensitivity-changed');assert.equal(f.stats.edits,0);});
await test('Steam clamping/rejecting a value is never saved/reported as success',async()=>{const f=fixture({clamp:true}),r=await f.fn({...f.request,percent:1});assert.equal(r.reason,'live-value-not-accepted');assert(r.mutated);assert.equal(f.stats.saves,0);});
await test('concurrent edit is not saved on behalf of the other UI',async()=>{const f=fixture({concurrent:true}),r=await f.fn(f.request);assert.equal(r.reason,'live-editor-changed');assert.equal(f.stats.saves,0);});
await test('timeout is bounded, marked uncertain, lock released',async()=>{const f=fixture({timeout:true}),r=await f.fn(f.request);assert.equal(r.reason,'live-timeout');assert(r.mutated);assert(!f.window.__ymccSteamLiveBusy);});
await test('same-context concurrency is rejected',async()=>{const f=fixture();f.window.__ymccSteamLiveBusy='other';assert.equal((await f.fn(f.request)).reason,'live-busy');assert.equal(f.stats.edits,0);});
await test('overlay read/set/noop uses Steam settings wrapper and native callback value',async()=>{const f=fixture();assert.equal((await f.fn({operation:'overlay.get',account:42})).value,0);assert((await f.fn({operation:'overlay.set',account:42,enabled:true})).ok);assert.equal(f.stats.overlayWrites,1);assert((await f.fn({operation:'overlay.set',account:42,enabled:true})).ok);assert.equal(f.stats.overlayWrites,1);assert.equal(f.stats.edits,0);});

await test('sleep inspection is read-only and never proves PID binding or game input',async()=>{
  const f=fixture();f.window.SteamUIStore={m_runningAppIDs:new Set([123,456]),WindowStore:{GetAppWindowIDs:()=>[7,8]}};
  f.window.appStore={m_mapApps:new Map([[123,{display_name:'Test',local_per_client_data:{display_status:2},selected_client_display_status:3,most_available_client_display_status:4,app_type:1}]])};
  const before=JSON.stringify([...f.window.SteamUIStore.m_runningAppIDs]);const r=await f.fn({operation:'sleep.inspect',account:42,appids:[123,999]});
  assert(r.ok&&!r.mutated);assert.equal(r.gameInput,'unknown');assert.equal(r.appBinding,'operator-or-native-tracker-supplied-not-proven-by-js');
  assert.equal(r.overviews[0].appid,123);assert.equal(r.overviews[1].overviewAvailable,false);assert.equal(r.overlayEnabled,false);
  assert.equal(JSON.stringify([...f.window.SteamUIStore.m_runningAppIDs]),before);assert.equal(f.stats.overlayWrites+f.stats.edits+f.stats.saves,0);
  assert.deepEqual(Object.keys(r.controllers[0]).sort(),['index','type']);
});
await test('sleep inspection rejects invalid AppIDs before any writes and bounds running candidates',async()=>{
  for(const appids of [[0],[-1],[1.1],[4294967296],['123'],Array(17).fill(1)]){const f=fixture();const r=await f.fn({operation:'sleep.inspect',account:42,appids});assert(!r.ok&&!r.mutated);assert.equal(r.reason,'invalid-steam-appids');}
  const f=fixture();f.window.SteamUIStore={m_runningAppIDs:new Set(Array.from({length:64},(_,n)=>n+1))};f.window.appStore={m_mapApps:new Map()};
  const r=await f.fn({operation:'sleep.inspect',account:42});assert(r.ok);assert.equal(r.runningAppIDs.length,16);assert.equal(r.overviews.length,16);
});
await test('missing Steam stores and drifting account remain unknown and never restart/launch',async()=>{
  const f=fixture();const unavailable=await f.fn({operation:'sleep.inspect',account:42});assert(!unavailable.ok&&!unavailable.mutated);
  f.window.SteamUIStore={m_runningAppIDs:new Set([123])};f.window.appStore={m_mapApps:{get(){f.settings.m_CMInterface.steamid.GetAccountID=()=>43;return undefined}}};
  const drift=await f.fn({operation:'sleep.inspect',account:42});assert.equal(drift.reason,'steam-account-changed');assert(!drift.mutated);assert.equal(f.stats.edits+f.stats.saves+f.stats.overlayWrites,0);
});

const valveScopeSource=fs.readFileSync('tools/fixtures/steam_scope_running_apps_valve_20261011.txt','utf8');
function installSteamScope(f,overviews,rawRunning=[]) {
  f.stats.scopeRuns=0;f.stats.windowRetired=[];
  f.window.appStore={m_mapApps:new Map(overviews.map(o=>[o.appid,o]))};
  const scope=vm.runInNewContext('({'+valveScopeSource+'})',{
    ze:{Ul:{Fi:4,Vv:1}},Ve:{VE:413080},m:{s5U:769},J:{tw:{get allApps(){return overviews}}},
    y:{rf:()=>false},wt:{Debug(){},IsDebugEnabled:()=>false},n:{h5(fn){f.stats.scopeRuns++;fn()},HO:v=>v}
  });
  Object.assign(scope,{m_runningAppIDs:[...rawRunning],m_remoteClientStreams:[],
    m_WindowStore:{BHasAppWindow:()=>false,OnAppNoLongerRunning:id=>f.stats.windowRetired.push(id)},
    WindowStore:{GetAppWindowIDs:()=>[]}});
  Object.assign(scope.m_WindowStore,scope.WindowStore);scope.WindowStore=scope.m_WindowStore;
  f.window.SteamUIStore=scope;return scope;
}
await test('log case signed shortcut ID is canonicalized, keyed overview/window lookup keeps signed key',async()=>{
  const f=fixture();const id=2399932135,signed=-1895035161;let windowKey;
  f.window.SteamUIStore={m_runningAppIDs:[signed,id,signed],WindowStore:{GetAppWindowIDs:key=>{windowKey=key;return [12932]}}};
  f.window.SteamUIStore.m_WindowStore=f.window.SteamUIStore.WindowStore;
  f.window.appStore={m_mapApps:new Map([[signed,{appid:signed,display_name:'TMNT',local_per_client_data:{display_status:4},selected_per_client_data:{display_status:11},display_status:4}]])};
  const r=await f.fn({operation:'sleep.inspect',account:42});assert(r.ok&&!r.mutated);assert.deepEqual([...r.runningAppIDs],[id]);
  assert(r.signedAliasesNormalized);assert.equal(r.overviews[0].overviewKey,signed);assert.equal(windowKey,signed);
  assert(r.overviews[0].localRunning&&r.overviews[0].inRunningList);assert.equal(r.overviews[0].selectedDisplayStatus,11);
});
await test('signed32 boundaries and native apps are preserved; strings/fractions/64bit IDs never rounded',async()=>{
  const f=fixture();f.window.SteamUIStore={m_runningAppIDs:[-2147483648,-1,945360,1,4294967295,0,-2147483649,4294967296,1.5,'10307630032478011392',Number('10307630032478011392')]};f.window.appStore={m_mapApps:new Map()};
  const r=await f.fn({operation:'sleep.inspect',account:42});assert.deepEqual([...r.runningAppIDs],[2147483648,4294967295,945360,1]);assert.equal(r.invalidRunningCount,6);
});
await test('truncated running list still checks explicitly supplied target beyond first 16',async()=>{
  const f=fixture();installSteamScope(f,[{appid:-1895035161,local_per_client_data:{display_status:4}}],[...Array.from({length:20},(_,i)=>i+1),-1895035161]);
  const r=await f.fn({operation:'sleep.inspect',account:42,appids:[2399932135]});assert(r.runningTruncated&&r.overviews[0].inRunningList);
});
await test('Valve native rescope repairs Running backend / missing UI using real installed method, no game launch',async()=>{
  const f=fixture();const scope=installSteamScope(f,[{appid:-1895035161,display_name:'TMNT',local_per_client_data:{display_status:4}},{appid:945360,local_per_client_data:{display_status:11}}],[945360]);
  const r=await f.fn({operation:'sleep.reconcile',account:42,appids:[2399932135]});assert(r.ok&&r.mutated&&r.refreshed&&r.runtimeAccepted&&!r.healthVerified);
  assert.equal(f.stats.scopeRuns,1);assert.deepEqual([...scope.m_runningAppIDs],[-1895035161]);assert(r.after.overviews[0].inRunningList);assert.equal(r.gameInput,'unknown');
  assert.equal(f.stats.overlayWrites+f.stats.edits+f.stats.saves,0);
});
await test('canonical alias presence does not hide a raw UI overview/running-key mismatch',async()=>{
  const f=fixture();const scope=installSteamScope(f,[{appid:2399932135,local_per_client_data:{display_status:4}}],[-1895035161]);
  const before=await f.fn({operation:'sleep.inspect',account:42,appids:[2399932135]});assert(before.overviews[0].canonicalInRunningList&&!before.overviews[0].inRunningList&&before.overviews[0].runningAliasMismatch);
  const repaired=await f.fn({operation:'sleep.reconcile',account:42,appids:[2399932135]});assert(repaired.ok&&repaired.mutated);assert.deepEqual([...scope.m_runningAppIDs],[2399932135]);assert.equal(f.stats.scopeRuns,1);
});
await test('conflicting signed and unsigned overview objects remain ambiguous, no state borrowed',async()=>{
  const f=fixture();installSteamScope(f,[{appid:2399932135,local_per_client_data:{display_status:4}}]);
  f.window.appStore.m_mapApps.set(-1895035161,{appid:-1895035161,local_per_client_data:{display_status:11}});
  const before=await f.fn({operation:'sleep.inspect',account:42,appids:[2399932135]});assert(before.overviews[0].aliasConflict&&!before.overviews[0].overviewAvailable);
  const attempted=await f.fn({operation:'sleep.reconcile',account:42,appids:[2399932135]});assert(!attempted.ok&&!attempted.mutated);assert.equal(f.stats.scopeRuns,0);
});
await test('Launching state can rescope, an already current signed ID does not repeat mutation',async()=>{
  const f=fixture();installSteamScope(f,[{appid:-1895035161,local_per_client_data:{display_status:1}}]);
  const r=await f.fn({operation:'sleep.reconcile',account:42,appids:[2399932135]});assert(r.ok&&r.mutated);assert.equal(f.stats.scopeRuns,1);
  const again=await f.fn({operation:'sleep.reconcile',account:42,appids:[2399932135]});assert(again.ok&&!again.mutated&&!again.refreshed);assert.equal(f.stats.scopeRuns,1);
});
await test('ReadyToLaunch/remote Running/unknown local state never fabricate running membership',async()=>{
  for(const local of [11,0,null,undefined]){const f=fixture();const scope=installSteamScope(f,[{appid:2399932135,local_per_client_data:{display_status:local},display_status:4,selected_per_client_data:{display_status:4}}]);
    const r=await f.fn({operation:'sleep.reconcile',account:42,appids:[2399932135]});assert(!r.ok&&!r.mutated);assert.equal(r.reason,'steam-backend-not-running');assert.equal(f.stats.scopeRuns,0);assert.equal(scope.m_runningAppIDs.length,0);}
});
await test('contact-bound ReadyToLaunch permits one verified Valve scope and requires live Running readback',async()=>{
 const f=fixture(),data={};let reads=0;Object.defineProperty(data,'display_status',{get(){return ++reads===1?11:4;}});
 const scope=installSteamScope(f,[{appid:-1895035161,gameid:'10307630032478011392',app_type:0x40000000,display_name:'TMNT',local_per_client_data:data}],[]);
 const r=await f.fn({operation:'sleep.reconcile',account:42,appids:[2399932135],contactRecovery:true,
   contactProof:{connected:true,appid:2399932135,pid:45256,created:'134363500000000000'},
   sleepSessionProcess:{pid:30460,created:'134363400000000000',root:'S:\Steam'}});
 assert(r.ok&&r.mutated&&r.runtimeAccepted&&r.contactRecovery.proofAccepted&&r.contactRecovery.accepted);assert.equal(r.reason,'steam-association-current');assert.equal(f.stats.scopeRuns,1);assert(r.after.overviews[0].localRunning&&r.after.overviews[0].inRunningList);assert.deepEqual([...scope.m_runningAppIDs],[-1895035161]);
});
await test('contact rescope rejects missing or malformed identity proof before any Valve call',async()=>{
 for(const proof of [undefined,{connected:true,appid:945360,pid:45256,created:'134363500000000000'},{connected:true,appid:2399932135,pid:45256,created:'0'}]){
  const f=fixture(),scope=installSteamScope(f,[{appid:2399932135,gameid:'10307630032478011392',app_type:0x40000000,local_per_client_data:{display_status:11}}],[]);
  const r=await f.fn({operation:'sleep.reconcile',account:42,appids:[2399932135],contactRecovery:true,contactProof:proof,sleepSessionProcess:{pid:30460,created:'134363400000000000',root:'S:\Steam'}});
  assert(!r.ok&&!r.mutated&&r.reason==='steam-contact-proof-rejected');assert.equal(f.stats.scopeRuns,0);assert.equal(scope.m_runningAppIDs.length,0);
 }
});
await test('contact rescope refuses the global Valve scope when an unrelated Running app is present',async()=>{
 const f=fixture(),scope=installSteamScope(f,[{appid:2399932135,gameid:'10307630032478011392',app_type:0x40000000,local_per_client_data:{display_status:11}},{appid:945360,local_per_client_data:{display_status:4}}],[945360]);
 const r=await f.fn({operation:'sleep.reconcile',account:42,appids:[2399932135],contactRecovery:true,
   contactProof:{connected:true,appid:2399932135,pid:45256,created:'134363500000000000'},
   sleepSessionProcess:{pid:30460,created:'134363400000000000',root:'S:\Steam'}});
 assert(!r.ok&&!r.mutated&&r.reason==='steam-contact-global-running-unsafe');assert.equal(f.stats.scopeRuns,0);assert.deepEqual([...scope.m_runningAppIDs],[945360]);
});
function contactRequest() {return {operation:'sleep.reconcile',account:42,appids:[2399932135],contactRecovery:true,
 contactProof:{connected:true,appid:2399932135,pid:45256,created:'134363500000000000'},
 sleepSessionProcess:{pid:30460,created:'134363400000000000',root:String.raw`S:\Steam`}};}
await test('contact global scope requires an actually empty raw list; malformed and sparse entries never authorize cleanup',async()=>{
 for(const raw of [['945360'],[null],[NaN],new Array(1),[945360,'bad'],Array.from({length:20},(_,i)=>i+1)]){
  const f=fixture(),scope=installSteamScope(f,[{appid:2399932135,gameid:'10307630032478011392',app_type:0x40000000,local_per_client_data:{display_status:11}}],raw);
  const before=[...scope.m_runningAppIDs],r=await f.fn(contactRequest());
  assert(!r.ok&&!r.mutated);assert.equal(r.reason,'steam-contact-global-running-unsafe');assert.equal(f.stats.scopeRuns,0);assert.deepEqual([...scope.m_runningAppIDs],before);assert.equal(f.stats.windowRetired.length,0);
 }
});
await test('naturally current exact contact is a zero-write success even with another Running app',async()=>{
 for(const raw of [[-1895035161],[-1895035161,945360]]){
  const f=fixture(),scope=installSteamScope(f,[{appid:-1895035161,gameid:'10307630032478011392',app_type:0x40000000,local_per_client_data:{display_status:4}}],raw);
  const r=await f.fn(contactRequest());assert(r.ok&&!r.mutated&&r.runtimeAccepted&&r.contactRecovery.proofAccepted&&r.contactRecovery.accepted);assert.equal(f.stats.scopeRuns,0);assert.deepEqual([...scope.m_runningAppIDs],raw);
 }
});
await test('a contact request becoming Running cannot drop exact GameID or proof checks',async()=>{
 for(const status of [1,4])for(const kind of ['gameid','proof']){
  const f=fixture();installSteamScope(f,[{appid:2399932135,gameid:kind==='gameid'?'10307630032478011393':'10307630032478011392',app_type:0x40000000,local_per_client_data:{display_status:status}}],[]);
  const request=contactRequest();if(kind==='proof')delete request.contactProof;
  const r=await f.fn(request);assert(!r.ok&&!r.mutated);assert.equal(r.reason,'steam-contact-proof-rejected');assert.equal(f.stats.scopeRuns,0);
 }
});
await test('naturally Running contact may rescope once with its exact identity and positive readback',async()=>{
 for(const status of [1,4]){
  const f=fixture();installSteamScope(f,[{appid:2399932135,gameid:'10307630032478011392',app_type:0x40000000,local_per_client_data:{display_status:status}}],[]);
  const r=await f.fn(contactRequest());assert(r.ok&&r.mutated&&r.contactRecovery.accepted);assert.equal(f.stats.scopeRuns,1);
 }
});
await test('contact identity changing after Scope remains mutated but unconfirmed',async()=>{
 for(const kind of ['gameid','type','replacement']){
  const f=fixture(),data={};let reads=0;const overview={appid:2399932135,gameid:'10307630032478011392',app_type:0x40000000,local_per_client_data:data};
  Object.defineProperty(data,'display_status',{get(){if(++reads===2)Promise.resolve().then(()=>{
    if(kind==='gameid')overview.gameid='10307630032478011393';
    if(kind==='type')overview.app_type=0;
    if(kind==='replacement')f.window.appStore.m_mapApps.set(2399932135,{...overview,gameid:'10307630032478011393'});
   });return 4;}});
  installSteamScope(f,[overview],[]);const r=await f.fn(contactRequest());assert(!r.ok&&r.mutated&&!r.runtimeAccepted&&!r.contactRecovery.accepted);assert.equal(r.reason,'steam-contact-rescope-not-confirmed');assert.equal(f.stats.scopeRuns,1);
 }
});
await test('missing target/wrong keyed identity/native UI schema changes all fail closed before mutation',async()=>{
  for(const kind of ['target','overview','identity','method','schema']){const f=fixture();const scope=installSteamScope(f,[{appid:2399932135,local_per_client_data:{display_status:4}}]);
    if(kind==='overview')f.window.appStore.m_mapApps.clear();if(kind==='identity')f.window.appStore.m_mapApps.set(2399932135,{appid:42,local_per_client_data:{display_status:4}});
    if(kind==='method')delete scope.ScopeRunningApps;if(kind==='schema')scope.ScopeRunningApps=function(){throw Error('must not call unknown method')};
    const r=await f.fn({operation:'sleep.reconcile',account:42,appids:kind==='target'?[]:[2399932135]});assert(!r.ok&&!r.mutated);assert.equal(f.stats.scopeRuns,0);}
});
await test('account change during native UI rescope is uncertain, never reported as successful recovery',async()=>{
  const f=fixture();const scope=installSteamScope(f,[{appid:-1895035161,local_per_client_data:{display_status:4}}]);
  Object.defineProperty(scope,'m_remoteClientStreams',{get(){f.settings.m_CMInterface.steamid.GetAccountID=()=>43;return []}});
  const r=await f.fn({operation:'sleep.reconcile',account:42,appids:[2399932135]});assert(!r.ok&&r.mutated);assert.equal(r.reason,'steam-account-changed');
});
await test('script never directly writes running membership or backend status; repair is one Valve call only',async()=>{
  const block=sleepOperationSource();
  assert(!/m_runningAppIDs\.(?:push|splice|add|delete|clear)\(/.test(block));assert(!/display_status\s*=/.test(block));
  assert.equal((block.match(/await bounded\(ui\.ScopeRunningApps\(\)\)/g)||[]).length,1);
  for(const forbidden of ['LaunchApp','RunGame','Restart','RELEASE_TARGET','PREPARE_TARGET','fan'])assert(!block.includes(forbidden));
});


function inputReturnFixture(options={}) {
  const f=fixture(options),id=options.appid??2399932135,key=options.signed?-1895035161:id;
  const overview={appid:key,gameid:options.gameid??(id===2399932135?'10307630032478011392':String(id)),display_name:'Original game',app_type:id===2399932135?0x40000000:1,local_per_client_data:{display_status:4}};
  const ui=installSteamScope(f,[overview],[key]);ui.GetShowingLockScreen=()=>false;ui.WindowStore.BHasStandaloneKeyboard=()=>false;
  // Existing data caches mirror the verified Valve/MobX field contract. Both
  // passive observation and the actuator's original getter see this same instance.
  f.setActiveWindow=active=>{
    if(!Object.hasOwn(active,'BrowserWindow'))active.BrowserWindow={};
    const nav={RootWindow:active.BrowserWindow};
    ui.m_GamepadNavigationManager={GetActiveContext:()=>nav};ui.NavigationManager=ui.m_GamepadNavigationManager;
    ui.m_WindowStore.m_mapOverlayPopupByPID=new Map();ui.m_WindowStore.m_mapDesiredWindowInstances=new Map([[1,active]]);
    ui.GetFocusedWindowInstance=()=>active;f.active=active;f.nav=nav;
  };
  f.setActiveWindow(options.activeWindow??{MenuStore:{GetOpenSideMenu:()=>0},IsAnyVRWindow:()=>false,IsGamepadUIOverlayWindow:()=>false,IsDesktopOverlayWindow:()=>false,IsControllerConfiguratorWindow:()=>false});
  f.focus={appid:options.focused??413080,overlay:0};f.window.FocusedAppWindowStore={GetFocusedAppID:()=>f.focus.appid,GetFocusedOverlayAppID:()=>f.focus.overlay,GetFocusedOverlayPID:()=>0,GetFocusedWindowID:()=>0};
  f.stats.raises=[];f.stats.configReads=[];
  f.steamClient.Apps={RaiseWindowForGame:gameid=>{f.stats.raises.push(gameid);assert.equal(typeof gameid,'string');if(!options.noReadback)f.focus.appid=id;if(options.accountDrift)f.settings.m_CMInterface.steamid.GetAccountID=()=>43;if(options.deviceDrift)f.window.ControllerStore.GetControllers=()=>[];return options.nativeReturn;}};
  f.steamClient.Input={GetConfigForAppAndController:async (app,index)=>{f.stats.configReads.push({app,index});return {URL:'S:\\Steam\\controller_base\\empty.vdf',eSelectionType:2,strSerialNumber:'NEVER_EXPORT',ProgenitorURL:'SECRET'}}};
  f.s.EditingConfigurationAppId=-1;
  f.inputRequest={operation:'sleep.input-resume',account:42,appids:[id]};return f;
}
function installNativeRecordIdentity(f,full='76561197960265770'){
 class Identity {constructor(value){this.m_ulSteamID=BigInt(value);}GetAccountID(){return 42;}ConvertTo64BitString(){return this.m_ulSteamID.toString()}}
 f.settings.m_CMInterface.steamid=new Identity(full);return f.settings.m_CMInterface.steamid;
}
await test('optional native record channel retains full uint64 only inside private read reply, with no JS persistence or writes',async()=>{
 const f=inputReturnFixture();installNativeRecordIdentity(f);
 const r=await f.fn({...f.inputRequest,operation:'sleep.inspect',nativeRecordIdentityRead:true});
 assert(r.ok&&!r.mutated);assert.equal(r._ymccPrivateNativeRecord.status,'observed');
 assert.equal(r._ymccPrivateNativeRecord.steamid64,'76561197960265770');assert.equal(r._ymccPrivateNativeRecord.gameid64,'10307630032478011392');assert.equal(r._ymccPrivateNativeRecord.appid,2399932135);
 assert.equal(r._ymccPrivateNativeRecord.source,'steam-ui-exact-native-record-identity');
 assert.equal(f.stats.raises.length,0);assert.equal(f.stats.edits,0);assert.equal(f.stats.saves,0);
 assert(!Object.keys(f.window).some(k=>k.startsWith('__ymcc')&&JSON.stringify(f.window[k])?.includes('76561197960265770')));
 const plain=await f.fn({...f.inputRequest,operation:'sleep.inspect'});assert.equal(plain._ymccPrivateNativeRecord,undefined);assert(!JSON.stringify(plain).includes('76561197960265770'));
});
await test('native record identity reads support native games and exact signed shortcut aliases',async()=>{
 for(const options of [{appid:945360},{signed:true}]){
  const f=inputReturnFixture(options);installNativeRecordIdentity(f);
  const r=await f.fn({...f.inputRequest,operation:'sleep.inspect',nativeRecordIdentityRead:true});
  assert(r.ok&&!r.mutated);assert.equal(r._ymccPrivateNativeRecord.status,'observed');assert.equal(r._ymccPrivateNativeRecord.gameid64,options.appid?'945360':'10307630032478011392');
 }
});
await test('unknown full-account getter and malformed or mismatched full account only disable optional diagnostic',async()=>{
 for(const mode of ['schema','missing','zero','overflow','mismatch']){
  const f=inputReturnFixture();const id=installNativeRecordIdentity(f,mode==='zero'?'0':mode==='overflow'?'18446744073709551616':mode==='mismatch'?'76561197960265771':undefined);
  if(mode==='schema')id.ConvertTo64BitString=function(){return '76561197960265770'};
  if(mode==='missing')delete f.settings.m_CMInterface.steamid;
  const r=await f.fn({...f.inputRequest,operation:'sleep.inspect',nativeRecordIdentityRead:true});
  if(mode==='missing'){assert(!r.ok&&!r.mutated);continue;}
  assert(r.ok&&!r.mutated);assert.equal(r._ymccPrivateNativeRecord.status,'unavailable');assert(!JSON.stringify(r).includes('76561197960265770'));
 }
});
await test('native record channel rejects ambiguous game aliases and incompatible full GameID without changing inspection',async()=>{
 for(const mode of ['alias','short-gameid','overflow','type']){
  const f=inputReturnFixture();installNativeRecordIdentity(f);const overview=f.window.appStore.m_mapApps.get(2399932135);
  if(mode==='alias')f.window.appStore.m_mapApps.set(2399932135|0,{...overview});
  if(mode==='short-gameid')overview.gameid='2399932135';
  if(mode==='overflow')overview.gameid='18446744073709551616';
  if(mode==='type')overview.app_type=1;
  const r=await f.fn({...f.inputRequest,operation:'sleep.inspect',nativeRecordIdentityRead:true});assert(r.ok&&!r.mutated);assert.equal(r._ymccPrivateNativeRecord.status,'unavailable');
 }
});
await test('identity object or full game replacement during inspection cannot supply a private original identity',async()=>{
 for(const mode of ['identity','overview','full-id']){
  const f=inputReturnFixture();const id=installNativeRecordIdentity(f),old=f.window.ControllerStore.GetControllers;let changed=false;
  f.window.ControllerStore.GetControllers=()=>{if(!changed){changed=true;if(mode==='identity')installNativeRecordIdentity(f);else if(mode==='overview'){const v=f.window.appStore.m_mapApps.get(2399932135);f.window.appStore.m_mapApps.set(2399932135,{...v});}else id.m_ulSteamID=BigInt('76561197960265771');}return old();};
  const r=await f.fn({...f.inputRequest,operation:'sleep.inspect',nativeRecordIdentityRead:true});assert(r.ok&&!r.mutated);assert.equal(r._ymccPrivateNativeRecord.status,'unavailable');assert.equal(r._ymccPrivateNativeRecord.reason,mode==='full-id'?'native-record-ui-account-unavailable':'native-record-ui-identity-raced');
 }
});
await test('optional private flag cannot attach to writes, unrelated operations, or malformed types',async()=>{
 for(const operation of ['sleep.input-resume','sleep.reconcile','overlay.set','mouse.get']){
  const f=inputReturnFixture();installNativeRecordIdentity(f);const r=await f.fn({...f.inputRequest,operation,nativeRecordIdentityRead:true});assert(!r.ok&&!r.mutated);assert.equal(r.reason,'invalid-steam-native-record-read');assert.equal(r._ymccPrivateNativeRecord,undefined);assert.equal(f.stats.raises.length,0);
 }
 for(const flag of [null,1,'true',{},[]]){const f=inputReturnFixture();const r=await f.fn({...f.inputRequest,operation:'sleep.inspect',nativeRecordIdentityRead:flag});assert(!r.ok&&!r.mutated);assert.equal(r.reason,'invalid-steam-native-record-read');}
});
await test('input inspect captures native Steam focus and selected empty config without serial/path leakage or writes',async()=>{
 const f=inputReturnFixture({signed:true}),r=await f.fn({...f.inputRequest,operation:'sleep.inspect'});
 assert(r.ok&&!r.mutated);assert.equal(r.inputContext.focus.appid,413080);assert.equal(r.inputContext.focus.overlayAppid,0);assert.equal(r.inputContext.configQueries[0].urlKind,'empty-or-passthrough');assert.equal(f.stats.configReads[0].app,-1895035161);assert.equal(r.inputContext.gameInput,'not-tested');assert(!JSON.stringify(r).includes('NEVER_EXPORT'));assert(!JSON.stringify(r).includes('SECRET'));assert(!JSON.stringify(r).includes('S:'));assert.equal(f.stats.raises.length,0);
});
await test('guarded native return refocuses the original shortcut using exact 64bit GameID, not a launcher or config reset',async()=>{
 const f=inputReturnFixture({signed:true}),r=await f.fn(f.inputRequest);assert(r.ok&&r.mutated&&r.runtimeAccepted);assert.equal(r.reason,'steam-input-context-refocused');assert.equal(f.stats.raises.length,1);assert.equal(f.stats.raises[0],'10307630032478011392');assert.equal(f.stats.configReads.length,0);assert.equal(r.inputContext.focus.appid,2399932135);assert.equal(r.gameInput,'not-tested');assert.equal(r.healthVerified,false);assert.equal(f.stats.scopeRuns+f.stats.edits+f.stats.saves+f.stats.overlayWrites,0);
});
await test('native RETURN numeric codes are bounded diagnostic evidence, never input acceptance or a replay authority',async()=>{
 for(const code of [1,2,3]){const f=inputReturnFixture({nativeReturn:code,noReadback:true});const r=await f.fn(trackedOriginalRequest(f));assert.equal(r.nativeReturn.kind,'uint32');assert.equal(r.nativeReturn.code,code);assert.equal(r.nativeReturn.observed,true);assert.equal(r.nativeReturn.authority,'diagnostic-only-no-admission');assert(!r.ok&&!r.runtimeAccepted&&!r.healthVerified&&r.actionAcknowledged);assert.equal(r.nativeReturn.nativeMeaningVerified,false);assert.equal(r.gameInput,'not-tested');assert.equal(f.stats.raises.length,1);}
});
await test('native RETURN unknown values never leak objects, invoke their conversion, round64bit numbers or infer a numeric success',async()=>{
 const secret={get sensitive(){throw Error('object must not be inspected')},toJSON(){throw Error('object must not be serialized')}};
 for(const value of [undefined,null,secret,NaN,Infinity,-1,4294967296,2n,'2',true,false]){const f=inputReturnFixture({nativeReturn:value,noReadback:true});const r=await f.fn(trackedOriginalRequest(f));assert.equal(r.nativeReturn.code,null);assert.equal(r.nativeReturn.observed,typeof value==='boolean');assert(!r.ok&&!r.runtimeAccepted);assert.equal(f.stats.raises.length,1);assert(!JSON.stringify(r.nativeReturn).includes('sensitive'));}
});
await test('native Steam app return uses the real native GameID and already focused game is a no-op',async()=>{
 const f=inputReturnFixture({appid:945360}),r=await f.fn(f.inputRequest);assert(r.ok&&r.mutated);assert.equal(f.stats.raises[0],'945360');const again=await f.fn(f.inputRequest);assert(again.ok&&!again.mutated);assert.equal(f.stats.raises.length,1);
});
await test('input resume refuses ReadyToLaunch and remote-only Running even if original process survives',async()=>{
 for(const status of [11,0,undefined]){const f=inputReturnFixture();f.window.appStore.m_mapApps.get(2399932135).local_per_client_data.display_status=status;const r=await f.fn(f.inputRequest);assert(!r.ok&&!r.mutated);assert.equal(f.stats.raises.length,0);}
});
function trackedOriginalRequest(f) {
  return {...f.inputRequest,originalTarget:{basis:'captured-original-tracker',appid:f.inputRequest.appids[0],pid:7320,created:'134361996917146698'}};
}
function readyToLaunch(f) {
  f.window.appStore.m_mapApps.get(f.inputRequest.appids[0]).local_per_client_data.display_status=11;
  f.window.SteamUIStore.m_runningAppIDs=[];
}
await test('captured native original tracker permits RETURN with ReadyToLaunch but NEVER fabricates Continue or list state',async()=>{
 const f=inputReturnFixture();readyToLaunch(f);const r=await f.fn(trackedOriginalRequest(f));
 assert(r.ok&&r.mutated&&r.runtimeAccepted&&!r.associationVerified&&!r.healthVerified);assert.equal(r.returnBasis,'captured-original-tracker');
 assert.equal(f.stats.raises.length,1);assert.equal(f.stats.raises[0],'10307630032478011392');assert.equal(r.after.overviews[0].localDisplayStatus,11);
 assert.equal(f.window.SteamUIStore.m_runningAppIDs.length,0);assert.equal(f.stats.scopeRuns+f.stats.edits+f.stats.saves+f.stats.overlayWrites,0);assert.equal(r.gameInput,'not-tested');
});
await test('tracked return authority must have exact target, nonzero native PID and canonical 64bit process birth',async()=>{
 for(const mode of ['absent','basis','appid','pid-zero','pid-fraction','pid-overflow','birth-zero','birth-number','birth-overflow','birth-leading-zero']){
  const f=inputReturnFixture();readyToLaunch(f);const req=trackedOriginalRequest(f),p=req.originalTarget;
  if(mode==='absent')delete req.originalTarget;if(mode==='basis')p.basis='ui-cache';if(mode==='appid')p.appid=945360;
  if(mode==='pid-zero')p.pid=0;if(mode==='pid-fraction')p.pid=1.5;if(mode==='pid-overflow')p.pid=0x100000000;
  if(mode==='birth-zero')p.created='0';if(mode==='birth-number')p.created=134361996917146698;
  if(mode==='birth-overflow')p.created='18446744073709551616';if(mode==='birth-leading-zero')p.created='0134361996917146698';
  const r=await f.fn(req);assert(!r.ok&&!r.mutated);assert.equal(f.stats.raises.length,0);
 }
});
await test('tracked fallback never treats unknown local state or missing Running list as ReadyToLaunch authority',async()=>{
 for(const status of [0,undefined,null,4,1,999]){const f=inputReturnFixture();readyToLaunch(f);f.window.appStore.m_mapApps.get(2399932135).local_per_client_data.display_status=status;
  const r=await f.fn(trackedOriginalRequest(f));assert(!r.ok&&!r.mutated);assert.equal(f.stats.raises.length,0);}
});
await test('tracked original return still respects overlay, another game, empty controllers, editor and user menus',async()=>{
 for(const mode of ['overlay','other-game','no-controller','editor','menu','lock']){const f=inputReturnFixture();readyToLaunch(f);
  if(mode==='overlay')f.focus.overlay=2399932135;if(mode==='other-game')f.focus.appid=945360;if(mode==='no-controller')f.window.ControllerStore.GetControllers=()=>[];
  if(mode==='editor')f.s.m_nEditNumber=1;if(mode==='menu')f.window.SteamUIStore.GetFocusedWindowInstance=()=>({MenuStore:{GetOpenSideMenu:()=>1}});
  if(mode==='lock')f.window.SteamUIStore.GetShowingLockScreen=()=>true;
  const r=await f.fn(trackedOriginalRequest(f));assert(!r.ok&&!r.mutated);assert.equal(f.stats.raises.length,0);}
});
await test('tracked return cannot borrow a wrong GameID or a differently bound replacement overview during native return',async()=>{
 for(const mode of ['wrong-id','number-id','replacement']){const f=inputReturnFixture();readyToLaunch(f);const o=f.window.appStore.m_mapApps.get(2399932135);
  if(mode==='wrong-id')o.gameid='2399932135';if(mode==='number-id')o.gameid=10307630032478011392;
  if(mode==='replacement')f.steamClient.Apps.RaiseWindowForGame=id=>{f.stats.raises.push(id);f.focus.appid=2399932135;f.window.appStore.m_mapApps.set(2399932135,{...o,gameid:'2399932135'});};
  const r=await f.fn(trackedOriginalRequest(f));assert(!r.ok);assert.equal(f.stats.raises.length,mode==='replacement'?1:0);}
});
await test('genuine native overview replacement with the SAME exact target can restore Running during original return',async()=>{
 const f=inputReturnFixture();readyToLaunch(f);const o=f.window.appStore.m_mapApps.get(2399932135);
 f.steamClient.Apps.RaiseWindowForGame=id=>{f.stats.raises.push(id);f.focus.appid=2399932135;
  f.window.appStore.m_mapApps.set(2399932135,{...o,local_per_client_data:{display_status:4}});f.window.SteamUIStore.m_runningAppIDs=[2399932135];};
 const r=await f.fn(trackedOriginalRequest(f));assert(r.ok&&r.mutated&&r.runtimeAccepted&&r.associationVerified&&!r.healthVerified);
 assert.equal(r.after.overviews[0].localDisplayStatus,4);assert.equal(r.gameInput,'not-tested');assert.equal(f.stats.raises.length,1);assert.equal(f.stats.scopeRuns,0);
});
await test('tracked ReadyToLaunch focus readback is uncertain after account/device changes, never gameplay success',async()=>{
 for(const mode of ['accountDrift','deviceDrift','noReadback']){const f=inputReturnFixture({[mode]:true});readyToLaunch(f);const r=await f.fn(trackedOriginalRequest(f));assert(!r.ok&&r.mutated&&!r.healthVerified);assert.equal(f.stats.raises.length,1);}
});
function cachedOriginalWindowRequest(f,basis='current-association') {
 return {...f.inputRequest,originalWindowReturn:true,originalTarget:{basis,appid:f.inputRequest.appids[0],pid:7320,created:'134361996917146698'}};
}
await test('cached original Steam focus is not a no-op when native OS proof requests returning the original window',async()=>{
 const f=inputReturnFixture({focused:2399932135});const r=await f.fn(cachedOriginalWindowRequest(f));
 assert(r.ok&&r.mutated&&r.runtimeAccepted&&r.originalWindowReturn&&r.associationVerified&&!r.healthVerified);
 assert.equal(f.stats.raises.length,1);assert.equal(f.stats.raises[0],'10307630032478011392');assert.equal(r.inputContextBefore.focus.appid,2399932135);assert.equal(r.gameInput,'not-tested');
 const g=inputReturnFixture({focused:2399932135}),noop=await g.fn(g.inputRequest);assert(noop.ok&&!noop.mutated);assert.equal(g.stats.raises.length,0);
});
await test('cached-focus ReadyToLaunch return still requires exact original tracker and never invents Continue',async()=>{
 const f=inputReturnFixture({focused:2399932135});readyToLaunch(f);const r=await f.fn(cachedOriginalWindowRequest(f,'captured-original-tracker'));
 assert(r.ok&&r.mutated&&r.originalWindowReturn&&!r.associationVerified&&!r.healthVerified);assert.equal(r.after.overviews[0].localDisplayStatus,11);assert.equal(r.after.runningAppIDs.length,0);assert.equal(f.stats.scopeRuns,0);
});
await test('cached original window return cannot be authorized by a flag alone, wrong identity or a nonboolean value',async()=>{
 for(const mode of ['no-proof','wrong-basis','wrong-app','zero-pid','no-birth','string-flag']){const f=inputReturnFixture({focused:2399932135}),req=cachedOriginalWindowRequest(f),p=req.originalTarget;
  if(mode==='no-proof')delete req.originalTarget;if(mode==='wrong-basis')p.basis='cached-steam-focus';if(mode==='wrong-app')p.appid=945360;if(mode==='zero-pid')p.pid=0;if(mode==='no-birth')delete p.created;if(mode==='string-flag')req.originalWindowReturn='true';
  const r=await f.fn(req);assert(!r.ok&&!r.mutated);assert.equal(f.stats.raises.length,0);}
});
await test('cached matching AppID no longer bypasses Overlay/editor/menu/lock/controller protection when a window return is requested',async()=>{
 for(const mode of ['overlay','editor','menu','lock','no-controller','other-game']){const f=inputReturnFixture({focused:2399932135});
  if(mode==='overlay')f.focus.overlay=2399932135;if(mode==='editor')f.s.m_nEditNumber=1;if(mode==='menu')f.window.SteamUIStore.GetFocusedWindowInstance=()=>({MenuStore:{GetOpenSideMenu:()=>1}});
  if(mode==='lock')f.window.SteamUIStore.GetShowingLockScreen=()=>true;if(mode==='no-controller')f.window.ControllerStore.GetControllers=()=>[];if(mode==='other-game')f.focus.appid=945360;
  const r=await f.fn(cachedOriginalWindowRequest(f));assert(!r.ok&&!r.mutated);assert.equal(f.stats.raises.length,0);}
});
await test('cached original native return does not make account/device drift or a missing exact GameID acceptable',async()=>{
 for(const mode of ['accountDrift','deviceDrift','gameid']){const f=inputReturnFixture({focused:2399932135,[mode]:true});if(mode==='gameid')f.window.appStore.m_mapApps.get(2399932135).gameid='2399932135';
  const r=await f.fn(cachedOriginalWindowRequest(f));assert(!r.ok);assert.equal(f.stats.raises.length,mode==='gameid'?0:1);assert(!r.healthVerified);}
});
await test('sleep repair never globally resumes Steam games or replaces its native overview/lifetime callback',async()=>{
 const block=sleepOperationSource();
 assert(!/SteamClient\.(?:User\.(?:ResumeSuspendedGames|PrepareForSystemSuspend)|Apps\.RegisterForAppOverviewChanges|GameSessions\.RegisterForAppLifetimeNotifications)\s*\(/.test(block));
});
await test('active overlay, another game and no Steam controller never receive an automatic focus override',async()=>{
 for(const mode of ['overlay','other-game','no-controller','unknown-overlay']){const f=inputReturnFixture();if(mode==='overlay')f.focus.overlay=2399932135;if(mode==='other-game')f.focus.appid=945360;if(mode==='no-controller')f.window.ControllerStore.GetControllers=()=>[];if(mode==='unknown-overlay')f.focus.overlay=undefined;const r=await f.fn(f.inputRequest);assert(!r.ok&&!r.mutated);assert.equal(f.stats.raises.length,0);}
});
await test('dirty/preview/loading or unknown editor state blocks input return without changing user layouts',async()=>{
 for(const mode of ['dirty','preview','updating','unknown']){const f=inputReturnFixture();if(mode==='dirty')f.s.m_nEditNumber=1;if(mode==='preview')f.s.PreviewedConfiguration={};if(mode==='updating')f.s.IsUpdatingEditingConfiguration=true;if(mode==='unknown')delete f.s.m_nEditNumber;const r=await f.fn(f.inputRequest);assert(!r.ok&&!r.mutated);assert.equal(f.stats.raises.length,0);assert.equal(f.stats.edits+f.stats.saves,0);}
});
await test('lock screen, standalone keyboard, overlay/VR windows, open side menu and unknown UI schema are respected',async()=>{
 for(const mode of ['lock','keyboard','vr','overlay','menu','unknown']){const f=inputReturnFixture();if(mode==='lock')f.window.SteamUIStore.GetShowingLockScreen=()=>true;if(mode==='keyboard')f.window.SteamUIStore.WindowStore.BHasStandaloneKeyboard=()=>true;if(mode==='vr')f.window.SteamUIStore.GetFocusedWindowInstance=()=>({IsAnyVRWindow:()=>true});if(mode==='overlay')f.window.SteamUIStore.GetFocusedWindowInstance=()=>({IsGamepadUIOverlayWindow:()=>true});if(mode==='menu')f.window.SteamUIStore.GetFocusedWindowInstance=()=>({MenuStore:{GetOpenSideMenu:()=>1}});if(mode==='unknown')delete f.window.SteamUIStore.GetShowingLockScreen;const r=await f.fn(f.inputRequest);assert(!r.ok&&!r.mutated);assert.equal(f.stats.raises.length,0);}
});
await test('mismatched, rounded Number, overflow or malformed GameIDs cannot refocus another game',async()=>{
 for(const gameid of ['10307630032478011393','10307630032494788608','18446744073709551616',10307630032478011392,'945360','-1','1e20']){const f=inputReturnFixture({gameid}),r=await f.fn(f.inputRequest);assert(!r.ok&&!r.mutated);assert.equal(f.stats.raises.length,0);}
});
await test('focus readback/account drift/device drift after a native return are uncertain, never actual gameplay success',async()=>{
 for(const mode of ['noReadback','accountDrift','deviceDrift']){const f=inputReturnFixture({[mode]:true}),r=await f.fn(f.inputRequest);assert(!r.ok&&r.mutated);assert.equal(f.stats.raises.length,1);assert(!r.healthVerified);assert(!f.window.__ymccSteamLiveBusy);}
});
await test('sleep native input return never waits on a UI-cache-only focus readback before OS original-PID postflight',async()=>{
 const block=sleepOperationSource(),start=block.indexOf("if(request.operation==='sleep.input-resume')"),end=block.indexOf("if(request.operation==='sleep.reconcile')",start);
 assert(start>=0&&end>start);assert(!/await waitFor\(/.test(block.slice(start,end)));
});
await test('sleep inspect publishes positive read-only interaction guards without registering listeners or touching windows',async()=>{
 const f=inputReturnFixture(),r=await f.fn({...f.inputRequest,operation:'sleep.inspect'});
 assert(r.ok&&!r.mutated&&r.inputContext.interactive.available&&r.inputContext.interactive.idle);
 assert.equal(r.inputContext.interactive.lockScreen,false);assert.equal(r.inputContext.interactive.menuOpen,0);assert.equal(f.stats.raises.length,0);
});
await test('missing/throwing or active native UI guards never publish positive PID-window handoff authority',async()=>{
 for(const mode of ['lock','keyboard','vr','overlay','config','menu','suppressed','menu-unknown','lock-unknown','missing','throwing']){
  const f=inputReturnFixture();
  if(mode==='lock')f.window.SteamUIStore.GetShowingLockScreen=()=>true;if(mode==='keyboard')f.window.SteamUIStore.WindowStore.BHasStandaloneKeyboard=()=>true;
  if(mode==='vr')f.setActiveWindow({...f.active,IsAnyVRWindow:()=>true});
  if(mode==='overlay')f.setActiveWindow({...f.active,IsGamepadUIOverlayWindow:()=>true});
  if(mode==='config')f.setActiveWindow({...f.active,IsControllerConfiguratorWindow:()=>true});
  if(mode==='menu')f.setActiveWindow({...f.active,MenuStore:{GetOpenSideMenu:()=>1}});
  if(mode==='suppressed')f.setActiveWindow({...f.active,MenuStore:{GetOpenSideMenu:()=>0,m_cSuppressRequests:1}});
  if(mode==='menu-unknown')f.setActiveWindow({...f.active,MenuStore:{}});
  if(mode==='lock-unknown')f.window.SteamUIStore.GetShowingLockScreen=()=>undefined;
  if(mode==='missing')delete f.window.SteamUIStore.m_WindowStore.m_mapDesiredWindowInstances;
  if(mode==='throwing')Object.defineProperty(f.active,'BrowserWindow',{get(){throw Error('unknown-ui')}});
  const r=await f.fn({...f.inputRequest,operation:'sleep.inspect'});assert(r.ok&&!r.mutated&&!r.inputContext.interactive.idle);assert.equal(f.stats.raises.length,0);
 }
});
await test('native original PID proof preserves an uncertain fresh UI-cache receipt instead of throwing away after',async()=>{
 const f=inputReturnFixture({noReadback:true}),req={...f.inputRequest,originalTarget:{basis:'current-association',appid:2399932135,pid:4284,created:'134362044485066103'}};
 const r=await f.fn(req);assert(!r.ok&&!r.runtimeAccepted&&r.mutated&&r.actionAcknowledged&&r.verificationPending);
 assert.equal(r.reason,'steam-input-ui-focus-readback-pending');assert.equal(r.uiFocusMatched,false);assert.equal(r.uiFocusReadback,'auxiliary-cache-native-pid-postflight-required');
 assert.equal(r.after.overviews[0].localDisplayStatus,4);assert.equal(r.inputContext.focus.appid,413080);assert.equal(f.stats.raises.length,1);assert(!r.healthVerified);
});
await test('ReadyToLaunch tracked PID return can await UI cache but never create Running, Continue or list state',async()=>{
 const f=inputReturnFixture({noReadback:true});readyToLaunch(f);const r=await f.fn(trackedOriginalRequest(f));
 assert(!r.ok&&r.actionAcknowledged&&r.verificationPending&&!r.associationVerified);assert.equal(r.after.overviews[0].localDisplayStatus,11);
 assert.equal(f.window.SteamUIStore.m_runningAppIDs.length,0);assert.equal(f.stats.raises.length,1);assert.equal(f.stats.scopeRuns,0);
});
await test('bare CDP input return without original PID proof cannot acquire pending authority from a stale UI cache',async()=>{
 const f=inputReturnFixture({noReadback:true}),r=await f.fn(f.inputRequest);
 assert(!r.ok&&r.actionAcknowledged&&!r.verificationPending);assert.equal(r.reason,'steam-input-context-not-confirmed');assert(r.after&&r.inputContext);assert.equal(f.stats.raises.length,1);
});
await test('post-RETURN overlay, editor, other game or controller drift cannot be excused as UI-cache latency',async()=>{
 for(const mode of ['overlay','editor','other-game','controller']){
  const f=inputReturnFixture({noReadback:true});const original=f.steamClient.Apps.RaiseWindowForGame;
  f.steamClient.Apps.RaiseWindowForGame=gameid=>{original(gameid);if(mode==='overlay')f.focus.overlay=2399932135;if(mode==='editor')f.s.m_nEditNumber=1;if(mode==='other-game')f.focus.appid=945360;if(mode==='controller')f.window.ControllerStore.GetControllers=()=>[];};
  const r=await f.fn({...f.inputRequest,originalTarget:{basis:'current-association',appid:2399932135,pid:4284,created:'134362044485066103'}});
  assert(!r.ok&&!r.verificationPending&&r.mutated);assert.equal(f.stats.raises.length,1);assert(!r.healthVerified);
 }
});
await test('lock screen, menu or configuration window becoming active AFTER RETURN vetoes pending observation',async()=>{
 for(const mode of ['lock','menu','config']){
  const f=inputReturnFixture({noReadback:true}),original=f.steamClient.Apps.RaiseWindowForGame;
  f.steamClient.Apps.RaiseWindowForGame=gameid=>{original(gameid);if(mode==='lock')f.window.SteamUIStore.GetShowingLockScreen=()=>true;
   if(mode==='menu')f.window.SteamUIStore.GetFocusedWindowInstance=()=>({MenuStore:{GetOpenSideMenu:()=>1}});
   if(mode==='config')f.window.SteamUIStore.GetFocusedWindowInstance=()=>({IsControllerConfiguratorWindow:()=>true});};
  const r=await f.fn(trackedOriginalRequest(f));assert(!r.ok&&!r.verificationPending&&r.mutated);assert.equal(r.reason,'steam-interactive-ui-active');assert.equal(f.stats.raises.length,1);
 }
});
await test('settled native raw return survives a subsequent interactive veto without acknowledging recovery',async()=>{
 for(const code of [1,2,3]){
  const f=inputReturnFixture({noReadback:true});
  f.steamClient.Apps.RaiseWindowForGame=gameid=>{f.stats.raises.push(gameid);f.window.SteamUIStore.GetShowingLockScreen=()=>true;return code;};
  const r=await f.fn(trackedOriginalRequest(f));
  assert.equal(r.reason,'steam-interactive-ui-active');assert(r.mutated&&!r.ok&&!r.runtimeAccepted&&!r.actionAcknowledged&&!r.verificationPending);
  assert.equal(r.nativeReturn?.code,code);assert.equal(r.nativeReturn?.authority,'diagnostic-only-no-admission');
  assert.equal(r.nativeReturn?.gameInputVerified,false);assert.equal(f.stats.raises.length,1);
 }
});
await test('rejected native call has no invented raw return and account veto retains only bounded settled evidence',async()=>{
 for(const mode of ['rejected','account','unsupported']){
  const f=inputReturnFixture({noReadback:true});
  f.steamClient.Apps.RaiseWindowForGame=gameid=>{f.stats.raises.push(gameid);
   if(mode==='rejected')return Promise.reject(new Error('return-rejected'));
   if(mode==='account')f.settings.m_CMInterface.steamid.GetAccountID=()=>43;
   else f.window.SteamUIStore.GetShowingLockScreen=()=>true;
   return mode==='unsupported'?{sensitive:'NEVER_EXPORT_NATIVE_RETURN'}:3;};
  const r=await f.fn(trackedOriginalRequest(f));assert(!r.ok&&!r.actionAcknowledged&&!r.verificationPending);assert.equal(f.stats.raises.length,1);
  if(mode==='rejected')assert.equal(r.nativeReturn,undefined);
  else {assert.equal(r.nativeReturn?.code,mode==='account'?3:null);assert(!JSON.stringify(r).includes('NEVER_EXPORT_NATIVE_RETURN'));}
 }
});
await test('account drift and RETURN rejection are terminal, never pending or action replay',async()=>{
 for(const mode of ['account','rejected']){
  const f=inputReturnFixture({noReadback:true}),original=f.steamClient.Apps.RaiseWindowForGame;
  f.steamClient.Apps.RaiseWindowForGame=gameid=>{original(gameid);if(mode==='account')f.settings.m_CMInterface.steamid.GetAccountID=()=>43;else return Promise.reject(new Error('return-rejected'));};
  const r=await f.fn(trackedOriginalRequest(f));assert(!r.ok&&!r.verificationPending&&r.mutated&&!r.actionAcknowledged);assert.equal(f.stats.raises.length,1);
 }
});
await test('bounded native config read timeout preserves independent association diagnostics',async()=>{
 const f=inputReturnFixture();f.steamClient.Input.GetConfigForAppAndController=()=>new Promise(()=>{});const r=await f.fn({...f.inputRequest,operation:'sleep.inspect'});assert(r.ok&&!r.mutated);assert.equal(r.inputContext.configQueries[0].status,'timeout');assert.equal(f.stats.raises.length,0);
});
await test('shortcut 32bit AppID, unknown app type and an actively open configurator cannot authorize native return',async()=>{
 for(const mode of ['short-id','type-missing','native-as-shortcut','editing']){const f=inputReturnFixture();const overview=f.window.appStore.m_mapApps.get(2399932135);if(mode==='short-id')overview.gameid='2399932135';if(mode==='type-missing')delete overview.app_type;if(mode==='native-as-shortcut'){overview.app_type=1;}if(mode==='editing')f.s.EditingConfigurationAppId=945360;const r=await f.fn(f.inputRequest);assert(!r.ok&&!r.mutated);assert.equal(f.stats.raises.length,0);}
});
await test('real Valve idle configurator has undefined AppId; absent getter or non-idle config remains blocked',async()=>{
 const f=inputReturnFixture();f.s.EditingConfigurationAppId=undefined;f.s.EditedConfiguration=undefined;const r=await f.fn(f.inputRequest);assert(r.ok&&r.mutated);assert.equal(f.stats.raises.length,1);
 for(const mode of ['getter-missing','config-without-appid']){const g=inputReturnFixture();g.s.EditingConfigurationAppId=undefined;g.s.EditedConfiguration=undefined;if(mode==='getter-missing')delete g.s.EditingConfigurationAppId;else g.s.EditedConfiguration={};const denied=await g.fn(g.inputRequest);assert(!denied.ok&&!denied.mutated);assert.equal(g.stats.raises.length,0);}
});
await test('unknown boolean lock/keyboard state and standalone configurator window never authorize game focus',async()=>{
 for(const mode of ['lock-unknown','keyboard-unknown','configurator']){const f=inputReturnFixture();if(mode==='lock-unknown')f.window.SteamUIStore.GetShowingLockScreen=()=>undefined;if(mode==='keyboard-unknown')f.window.SteamUIStore.WindowStore.BHasStandaloneKeyboard=()=>undefined;if(mode==='configurator')f.window.SteamUIStore.GetFocusedWindowInstance=()=>({IsControllerConfiguratorWindow:()=>true});const denied=await f.fn(f.inputRequest);assert(!denied.ok&&!denied.mutated);assert.equal(f.stats.raises.length,0);}
});
await test('source has one exact native return call and never releases forced configurator focus or rewrites selected config',async()=>{
 const block=sleepOperationSource();assert.equal((block.match(/SteamClient\.Apps\.RaiseWindowForGame\(gameid\)/g)||[]).length,1);assert(!/SteamClient\.Input\.(?:ForceConfiguratorFocus|SetSelectedConfigForApp|ResetControllerBindings)\(/.test(block));
});

await test('a clean cached original-game config is not mistaken for a visible editor, but user editor remains protected',async()=>{
 const f=inputReturnFixture();f.s.EditingConfigurationAppId=2399932135;const r=await f.fn(f.inputRequest);
 assert(r.ok&&r.mutated);assert.equal(f.stats.raises.length,1);assert.equal(f.stats.edits+f.stats.saves,0);
 for(const mode of ['visible','dirty','other-game']){const g=inputReturnFixture();g.s.EditingConfigurationAppId=2399932135;
  if(mode==='visible')g.window.SteamUIStore.GetFocusedWindowInstance=()=>({IsControllerConfiguratorWindow:()=>true});
  if(mode==='dirty')g.s.m_nEditNumber=1;if(mode==='other-game')g.s.EditingConfigurationAppId=945360;
  const denied=await g.fn(g.inputRequest);assert(!denied.ok&&!denied.mutated);assert.equal(g.stats.raises.length,0);}
});

await test('cached original focus with an empty Steam controller list is not an accepted input resume',async()=>{
 const f=inputReturnFixture();f.window.FocusedAppWindowStore.GetFocusedAppID=()=>2399932135;
 f.window.ControllerStore.GetControllers=()=>[];const r=await f.fn(f.inputRequest);
 assert(!r.ok&&!r.mutated);assert.equal(r.reason,'steam-controller-not-connected');assert.equal(f.stats.raises.length,0);
});

await test('uninitialized Steam controller list never falls back to a generic device',async()=>{const f=fixture({controllers:null});const r=await f.fn(f.request);assert.equal(r.reason,'live-controller-not-connected');assert.equal(f.stats.edits,0);});
await test('machine-specific paths/account/controller index/type/modeid never hardcoded',async()=>{
  for(const [index,type,account,path,modeid] of [
    [0,2,17,'C:\\Program Files (x86)\\Steam\\steamapps\\common\\Steam Controller Configs\\17\\config\\413080\\controller_neptune.vdf',25],
    [3,44,1234567890,'E:\\SteamLibrary\\steamapps\\common\\Steam Controller Configs\\1234567890\\config\\413080\\controller_neptune.vdf',901],
    [8,69,7654321,'F:\\渚挎惡Steam\\steamapps\\common\\Steam Controller Configs\\7654321\\config\\413080\\controller_neptune.vdf',6]]) {
    const f=fixture({controllerIndex:index,controllerNumericType:type,account,path,modeid});const r=await f.fn(f.request);assert(r.ok);assert.equal(r.controllerIndex,index);assert.equal(f.stats.edits,1);assert.equal(f.stats.saves,1);
  }
});
await test('SteamDeck request never changes generic controller even when its percentage matches',async()=>{
  const f=fixture({controllers:[{eControllerType:30,nControllerIndex:0}]});const r=await f.fn(f.request);assert.equal(r.reason,'live-controller-not-connected');assert.equal(f.stats.edits,0);assert.equal(f.stats.saves,0);
});
await test('unified client batch applies monitor and overlay together through Steam wrappers',async()=>{
  const f=fixture();const values={enable_overlay:true,overlay_fps_counter_corner:5,overlay_fps_counter_detail_level:4,overlay_fps_counter_scale_factor:1.4,overlay_fps_counter_saturation_factor:0,overlay_fps_counter_bgopacity:.7};
  const result=await f.fn({operation:'settings.set',account:42,values});assert(result.ok);assert.deepEqual(JSON.parse(JSON.stringify(result.values)),values);assert.equal(f.stats.overlayWrites,6);assert.equal(f.stats.edits,0);
  const read=await f.fn({operation:'settings.get',account:42,keys:Object.keys(values)});assert(read.ok);assert.deepEqual(JSON.parse(JSON.stringify(read.values)),values);
});
await test('unified settings preflights every field before mutating any field',async()=>{
  for(const invalid of [{bad_key:1},{overlay_fps_counter_corner:7},{overlay_fps_counter_scale_factor:1.5},{overlay_fps_counter_bgopacity:-.1},{overlay_fps_counter_saturation_factor:.35},{enable_overlay:1},{overlay_fps_counter_detail_level:0}]){
    const f=fixture();const result=await f.fn({operation:'settings.set',account:42,values:{enable_overlay:true,...invalid}});assert(!result.ok);assert.equal(f.stats.overlayWrites,0);assert(!result.mutated);
  }
});
await test('unified settings read/noop/account drift never mutate Steam',async()=>{
  const f=fixture();assert((await f.fn({operation:'settings.get',account:42,keys:['overlay_fps_counter_corner']})).ok);
  assert((await f.fn({operation:'settings.set',account:42,values:{enable_overlay:false}})).ok);assert.equal(f.stats.overlayWrites,0);
  assert.equal((await f.fn({operation:'settings.set',account:43,values:{enable_overlay:true}})).reason,'steam-account-changed');assert.equal(f.stats.overlayWrites,0);
});

await test('sleep inspection separates protected Steam window kinds without mutating or treating cache as OS foreground',async()=>{
 for(const [name,method] of [['vr','IsAnyVRWindow'],['gamepadOverlay','IsGamepadUIOverlayWindow'],['desktopOverlay','IsDesktopOverlayWindow'],['controllerConfigurator','IsControllerConfiguratorWindow']]){
  const active={IsAnyVRWindow:()=>false,IsGamepadUIOverlayWindow:()=>false,IsDesktopOverlayWindow:()=>false,IsControllerConfiguratorWindow:()=>false};active[method]=()=>true;
  const f=inputReturnFixture({activeWindow:active}),r=await f.fn({...f.inputRequest,operation:'sleep.inspect'}),i=r.inputContext.interactive;
  assert(r.ok&&!r.mutated&&i.available&&!i.idle&&i.overlayOrVrWindow&&i.windowInstanceObserved&&i.windowKindProbesComplete);
  assert.equal(i.focusEvidence,'steam-ui-window-instance-cache-not-os-foreground');for(const key of ['vr','gamepadOverlay','desktopOverlay','controllerConfigurator'])assert.equal(i.windowKinds[key],key===name);
  assert.equal(f.stats.raises.length,0);
 }
});
await test('sleep window-kind diagnostics distinguish missing methods from false and never skip a positive protected kind',async()=>{
 const f=inputReturnFixture({activeWindow:{IsControllerConfiguratorWindow:()=>true}}),r=await f.fn({...f.inputRequest,operation:'sleep.inspect'}),i=r.inputContext.interactive;
 assert(!i.available&&!i.idle&&!i.windowKindProbesComplete);assert.equal(i.windowKinds.controllerConfigurator,true);assert.equal(i.windowKinds.vr,null);assert.equal(f.stats.raises.length,0);
 const absent=inputReturnFixture();absent.window.SteamUIStore.m_WindowStore.m_mapDesiredWindowInstances.clear();
 const none=await absent.fn({...absent.inputRequest,operation:'sleep.inspect'});assert(!none.inputContext.interactive.available&&!none.inputContext.interactive.idle);assert.equal(none.inputContext.interactive.reason,'steam-passive-window-cache-no-match');
});
await test('nonboolean Steam window-kind results stay unknown and never authorize a RETURN',async()=>{
 for(const value of [undefined,1,'false']){
  const active={IsAnyVRWindow:()=>false,IsGamepadUIOverlayWindow:()=>false,IsDesktopOverlayWindow:()=>value,IsControllerConfiguratorWindow:()=>false};
  const f=inputReturnFixture({activeWindow:active}),r=await f.fn({...f.inputRequest,operation:'sleep.inspect'});
  assert(r.ok&&!r.mutated&&!r.inputContext.interactive.available&&!r.inputContext.interactive.idle);assert.equal(r.inputContext.interactive.windowKinds.desktopOverlay,null);
  const denied=await f.fn(f.inputRequest);assert(!denied.ok&&!denied.mutated);assert.equal(f.stats.raises.length,0);
 }
});

function capturedLocalRunningRequest(f){return {...f.inputRequest,originalTarget:{basis:'captured-original-local-running',appid:f.inputRequest.appids[0],pid:7320,created:'134361996917146698'}};}
await test('captured exact local Running4/Launching1 can RETURN despite missing UI list, without changing membership or claiming association',async()=>{
 for(const status of [4,1]){const f=inputReturnFixture();f.window.appStore.m_mapApps.get(2399932135).local_per_client_data.display_status=status;f.window.SteamUIStore.m_runningAppIDs=[];
  const r=await f.fn(capturedLocalRunningRequest(f));assert(r.ok&&r.mutated&&r.actionAcknowledged&&!r.associationVerified&&!r.healthVerified);assert.equal(r.returnBasis,'captured-original-local-running');assert.equal(r.after.overviews[0].localDisplayStatus,status);assert.equal(r.after.runningAppIDs.length,0);assert.equal(f.stats.raises.length,1);assert.equal(f.stats.scopeRuns,0);
 }
});
await test('missing-list primary authority cannot be borrowed from unbound/current association credentials or non-running/remote states',async()=>{
 for(const mode of ['bare','wrong-basis','bad-pid','bad-birth','ready11','remote','bad-gameid']){const f=inputReturnFixture();f.window.SteamUIStore.m_runningAppIDs=[];let req=capturedLocalRunningRequest(f);
  if(mode==='bare')delete req.originalTarget;if(mode==='wrong-basis')req.originalTarget.basis='current-association';if(mode==='bad-pid')req.originalTarget.pid=0;if(mode==='bad-birth')req.originalTarget.created='0';
  if(mode==='ready11')f.window.appStore.m_mapApps.get(2399932135).local_per_client_data.display_status=11;
  if(mode==='remote'){const o=f.window.appStore.m_mapApps.get(2399932135);o.local_per_client_data.display_status=11;o.selected_per_client_data={display_status:4};}
  if(mode==='bad-gameid')f.window.appStore.m_mapApps.get(2399932135).gameid='10307630032478011393';
  const r=await f.fn(req);assert(!r.ok&&!r.mutated);assert.equal(f.stats.raises.length,0);
 }
});
await test('local-running independent RETURN retains cached-focus pending with unchanged target and never fabricates secondary success',async()=>{
 const f=inputReturnFixture({noReadback:true});f.window.SteamUIStore.m_runningAppIDs=[];const r=await f.fn(capturedLocalRunningRequest(f));
 assert(!r.ok&&r.mutated&&r.actionAcknowledged&&r.verificationPending&&!r.associationVerified&&!r.healthVerified);assert.equal(f.stats.raises.length,1);assert.equal(r.after.runningAppIDs.length,0);
});

function earlyWindowRequest(f){return {...f.inputRequest,originalWindowReturn:true,preControllerOriginalWindow:true,originalTarget:{basis:'current-association',appid:f.inputRequest.appids[0],pid:7320,created:'134361996917146698'}};}
await test('current Running original-window preparation can precede controller enumeration without claiming input or changing list/status',async()=>{
 for(const focus of [413080,2399932135]){const f=inputReturnFixture({controllers:[],focused:focus}),r=await f.fn(earlyWindowRequest(f));
  assert(!r.ok&&r.mutated&&r.actionAcknowledged&&r.verificationPending&&r.windowPreparation&&r.controllerReconnectPending&&!r.runtimeAccepted&&!r.healthVerified);
  assert.equal(r.reason,'steam-original-window-prepared-input-unverified');assert.equal(f.stats.raises.length,1);assert.equal(f.stats.scopeRuns,0);assert.equal(f.stats.configReads.length,0);
  assert.equal(r.after.overviews[0].localDisplayStatus,4);assert(r.associationVerified);
 }
});
await test('empty-controller window exception requires exact internal original credential, Running4 membership and explicit window return',async()=>{
 for(const mode of ['bare','bad-pid','bad-birth','wrong-basis','no-window','launch1','ready11','missing-list','invalid-list','wrong-type','bad-gameid','menu']){
  const f=inputReturnFixture({controllers:[]});let req=earlyWindowRequest(f);
  if(mode==='bare')delete req.originalTarget;if(mode==='bad-pid')req.originalTarget.pid=0;if(mode==='bad-birth')req.originalTarget.created='0';if(mode==='wrong-basis')req.originalTarget.basis='captured-original-tracker';if(mode==='no-window')delete req.originalWindowReturn;
  if(mode==='launch1')f.window.appStore.m_mapApps.get(2399932135).local_per_client_data.display_status=1;if(mode==='ready11')f.window.appStore.m_mapApps.get(2399932135).local_per_client_data.display_status=11;
  if(mode==='missing-list')f.window.SteamUIStore.m_runningAppIDs=[];if(mode==='invalid-list')f.window.ControllerStore.GetControllers=()=>undefined;if(mode==='wrong-type')req.preControllerOriginalWindow='true';
  if(mode==='bad-gameid')f.window.appStore.m_mapApps.get(2399932135).gameid='10307630032478011393';if(mode==='menu')f.window.SteamUIStore.GetFocusedWindowInstance=()=>({IsAnyVRWindow:()=>false,IsGamepadUIOverlayWindow:()=>false,IsDesktopOverlayWindow:()=>false,IsControllerConfiguratorWindow:()=>false,MenuStore:{GetOpenSideMenu:()=>1}});
  const r=await f.fn(req);assert(!r.ok&&!r.mutated);assert.equal(f.stats.raises.length,0);
 }
});
await test('controller arrival during early window call is observed without a second action or an accepted input ACK',async()=>{
 const f=inputReturnFixture({controllers:[]});f.steamClient.Apps.RaiseWindowForGame=gameid=>{f.stats.raises.push(gameid);f.focus.appid=2399932135;f.window.ControllerStore.GetControllers=()=>[{eControllerType:2,nControllerIndex:9}];};
 const r=await f.fn(earlyWindowRequest(f));assert(!r.ok&&r.mutated&&r.windowPreparation&&r.verificationPending&&!r.controllerReconnectPending&&!r.runtimeAccepted);assert.equal(f.stats.raises.length,1);
});

await test('early window preparation rechecks real Running4 immediately before the action after async input snapshots',async()=>{
 for(const status of [1,11]){const f=inputReturnFixture({controllers:[]});let reads=0;f.window.FocusedAppWindowStore.GetFocusedAppID=()=>{if(++reads===2)f.window.appStore.m_mapApps.get(2399932135).local_per_client_data.display_status=status;return f.focus.appid;};
  const r=await f.fn(earlyWindowRequest(f));assert(!r.ok&&!r.mutated);assert.equal(f.stats.raises.length,0);
 }
});
await test('sleep account capture reads the actual UI account independently of corrupt registry account, without any writes',async()=>{
 const f=fixture(),r=await f.fn({operation:'sleep.session.inspect',account:99});assert(r.ok&&!r.mutated);assert.equal(r.observedAccount,42);assert.equal(r.source,'verified-steam-ui-account-read');
 assert.deepEqual(f.stats,{edits:0,saves:0,overlayWrites:0,loads:0});assert(!f.window.__ymccSteamLiveBusy);
});
await test('sleep account capture rejects zero, negative, fractional, out-of-range and missing IDs without writes',async()=>{
 for(const value of [0,-1,1.5,0x100000000,NaN,'42']){const f=fixture();f.settings.m_CMInterface.steamid.GetAccountID=()=>value;const r=await f.fn({operation:'sleep.session.inspect'});assert(!r.ok&&!r.mutated);assert.equal(r.reason,'steam-session-starting');assert.equal(f.stats.overlayWrites,0);}
 const f=fixture();delete f.settings.m_CMInterface.steamid;const r=await f.fn({operation:'sleep.session.inspect'});assert(!r.ok&&!r.mutated&&r.reason==='live-api-unavailable');
});
await test('sleep account capture refuses account drift and UI steamid replacement during the same read',async()=>{
 for(const swap of [false,true]){const f=fixture();let reads=0;f.settings.m_CMInterface.steamid.GetAccountID=()=>{if(swap)f.settings.m_CMInterface.steamid={GetAccountID:()=>42};return ++reads===1?42:43;};
 const r=await f.fn({operation:'sleep.session.inspect'});assert(!r.ok&&!r.mutated&&r.reason==='steam-session-changed');assert.equal(f.stats.edits,0);}
});
await test('sleep account capture supports uint32 upper bound and remains serialized with existing operations',async()=>{
 const f=fixture();f.settings.m_CMInterface.steamid.GetAccountID=()=>0xffffffff;const r=await f.fn({operation:'sleep.session.inspect'});assert(r.ok&&!r.mutated&&r.observedAccount===0xffffffff);
 f.window.__ymccSteamLiveBusy='other-owner';const busy=await f.fn({operation:'sleep.session.inspect'});assert(!busy.ok&&!busy.mutated&&busy.reason==='live-busy');assert.equal(f.stats.loads,0);
});
await test('Decky mode snapshot reads the current offline account without online/registry or business writes',async()=>{
 const f=fixture();f.window.top=f.window;f.window.location={origin:'https://steamloopback.host'};
 f.steamClient.UI={GetUIMode:async()=>4};f.steamClient.User={RegisterForLoginStateChange:()=>({unregister(){}}),RegisterForCurrentUserChanges:cb=>cb({strSteamID:'76561197960265770',bIsOfflineMode:true})};
 const r=await f.fn({operation:'ui.mode.get'});assert(r.ok&&!r.mutated);assert.equal(r.observedAccount,42);assert.equal(r.deckyAccountWatchAvailable,true);
 assert.equal(f.stats.edits+f.stats.saves+f.stats.overlayWrites,0);
 for(const value of [0,-1,1.5,0x100000000,'42']){f.settings.m_CMInterface.steamid.GetAccountID=()=>value;const bad=await f.fn({operation:'ui.mode.get'});assert(bad.ok&&!('observedAccount' in bad),'mode stays available but invalid account cannot authorize Decky');}
});
await test('Decky current-user proof rejects logged-out/stale identities and reuses one native callback offline-to-online',async()=>{
 const f=fixture();f.window.top=f.window;f.window.location={origin:'https://steamloopback.host'};f.steamClient.UI={GetUIMode:async()=>4};
 let callback,loginCallback,registrations=0;f.steamClient.User={RegisterForLoginStateChange:cb=>{loginCallback=cb;return {unregister(){}};},RegisterForCurrentUserChanges:cb=>{registrations++;callback=cb;cb({strSteamID:'76561197960265770',bIsOfflineMode:true});}};
 const first=await f.fn({operation:'ui.mode.get'});assert(first.deckyAccountWatchAvailable);const watch=f.window.__YMCC_DECKY_ACCOUNT_WATCH__;
 let changes=0;const off=watch.subscribe(()=>changes++);callback({strSteamID:'76561197960265770',bIsOfflineMode:false});assert(watch.read(42));
 await f.fn({operation:'ui.mode.get'});assert.equal(registrations,1);assert.equal(changes,1);
 loginCallback('');assert(!watch.read(42),'logout must revoke even while old UI object exists');callback({strSteamID:'0'});assert(!watch.read(42));const out=await f.fn({operation:'ui.mode.get'});assert.equal(out.deckyAccountWatchAvailable,false);
 for(const id of ['76561197960265771','invalid',42,'18446744073709551616']){callback({strSteamID:id});assert(!watch.read(42));}
 callback({strSteamID:'76561197960265770',bIsOfflineMode:true});assert(watch.read(42));off();const before=changes;callback({strSteamID:'0'});assert.equal(changes,before);
 assert.deepEqual(f.stats,{edits:0,saves:0,overlayWrites:0,loads:0});
});
await test('revoked bridge permits only exact retained-document provenance without restoring its old credentials',async()=>{
 const f=fixture();f.window.top=f.window;f.window.location={origin:'https://steamloopback.host'};f.steamClient.UI={GetUIMode:async()=>4};
 f.window.__YMCC_DECKY_FRONTEND__={active:true,buildId:'a'.repeat(64)};f.window.DeckyPluginLoader={deckyState:{publicState:()=>({versionInfo:{current:'v3.3.1'}})}};
 f.window.__YMCC_DECKY_MIRROR__={runId:'b'.repeat(32),frontendBuildId:'a'.repeat(64),revoked:true};
 const good=await f.fn({operation:'ui.mode.get'});assert.equal(good.deckyUpdateResidue?.frontendBuildId,'a'.repeat(64));assert.equal(f.window.__YMCC_DECKY_MIRROR__.token,undefined);
 f.window.__YMCC_DECKY_MIRROR__.frontendBuildId='c'.repeat(64);assert.equal((await f.fn({operation:'ui.mode.get'})).deckyUpdateResidue,undefined);
});
await test('failed account-hook installation does not accumulate callback registrations on repeated mode reads',async()=>{
 const f=fixture();f.window.top=f.window;f.window.location={origin:'https://steamloopback.host'};f.steamClient.UI={GetUIMode:async()=>4};let count=0;
 f.steamClient.User={RegisterForCurrentUserChanges:cb=>{count++;cb({strSteamID:'76561197960265770'});},RegisterForLoginStateChange:()=>{throw Error('native-hook-unavailable');}};
 for(let i=0;i<100;i++){const r=await f.fn({operation:'ui.mode.get'});assert(r.ok&&!r.deckyAccountWatchAvailable);}assert.equal(count,1);
});
}
console.log(JSON.stringify({suite:'Steam live production JS mocked',passed:results.length,cases:results,hardwareOperations:0,...directedEvidence},null,2));
