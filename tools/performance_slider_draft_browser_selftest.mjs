// Actual Vue Panel + extracted production engine, with memory-only configuration boundaries.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { createRequire } from 'node:module';
import { createPerformancePreviewServer } from './lib/continuousPerformancePreview.mjs';

const root = path.resolve(import.meta.dirname, '..');
const args = process.argv.slice(2);
const startedAt = performance.now();
const positional = [];
for (let index = 0; index < args.length; index++) {
  if (args[index] === '--out' || args[index] === '--baseline-panel') { index++; continue; }
  if (args[index] === '--baseline-only') continue;
  assert(!args[index].startsWith('--'), 'Unknown option: ' + args[index]);
  positional.push(args[index]);
}
assert(positional.length <= 1, 'Usage: node tools/performance_slider_draft_browser_selftest.mjs [outDir] [--out outDir] [--baseline-panel file] [--baseline-only]');
function option(name, fallback) {
  const index = args.indexOf(name);
  if (index < 0) return fallback;
  assert(args[index + 1] && !args[index + 1].startsWith('--'), name + ' requires a value');
  return path.resolve(args[index + 1]);
}
const evidenceRoot = 'G:/YeManCC-Work/Build/Validation/Performance-Slider-Cadence-20261021';
const out = path.resolve(option('--out', positional[0] || path.join(evidenceRoot, 'browser')));
const requestedBaseline = option('--baseline-panel', null);
const baseline = requestedBaseline || path.join(out, 'negative-refresh-panel.vue');
const baselineOnly = args.includes('--baseline-only');
const require = createRequire(path.join(root, 'package.json'));
let chromium;
try { ({ chromium } = require('playwright')); }
catch { ({ chromium } = createRequire('C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/package.json')('playwright')); }
fs.mkdirSync(out, { recursive: true });
const digest = file => ({ path: file, sha256: createHash('sha256').update(fs.readFileSync(file)).digest('hex') });
const report = {
  pass: false, count: 0, mode: baselineOnly ? 'baseline-only' : 'candidate-regression', date: new Date().toISOString(),
  scope: '1280x720/800; actual ContinuousPerformancePanel.vue + Slider.vue and extracted production engine; browser memory only; stubbed native edit boundaries; no YMCC, Steam, real native IPC or hardware',
  sources: ['src/components/ContinuousPerformancePanel.vue', 'src/components/Slider.vue', 'src/gamepad/engine.ts', 'src/gamepad/sliderRepeat.ts', 'tools/lib/continuousPerformancePreview.mjs', 'tools/performance_slider_draft_browser_selftest.mjs'].map(file => digest(path.join(root, file))),
  baseline: null, cases: [], errors: [],
};
const tdpSelector = '.continuous-performance input[aria-label="TDP"]';
const evidenceState = page => page.evaluate(() => ({
  editing: window.__readEdit?.(),
  active: document.activeElement?.getAttribute('aria-label'),
  events: window.__sliderEvents || [],
  nativeEvents: window.__nativeEditEvents || [],
  cadence: window.__cadenceClock?.snapshot(),
  sliders: [...document.querySelectorAll('.continuous-performance input[type=range]')].map(input => ({ label: input.getAttribute('aria-label'), value: Number(input.value), disabled: input.disabled, repeatIntervalMs: input.dataset.gpRepeatIntervalMs })),
  fixture: structuredClone(window.__fixture),
}));
const settle = page => page.evaluate(() => window.__settle());
async function press(page, action, repeat) {
  await page.evaluate(({ action, repeat }) => window.__press(action, repeat), { action, repeat });
  await settle(page);
}
async function begin(page, selector = tdpSelector) {
  await page.locator(selector).focus();
  await press(page, 'confirm');
  assert.equal(await page.evaluate(() => window.__readEdit()), true, 'A starts editing');
}
const value = (page, selector = tdpSelector) => page.locator(selector).inputValue().then(Number);
const writes = page => page.evaluate(() => window.__fixture.writes);
async function noWrites(page) { assert.deepEqual(await writes(page), [], 'An unconfirmed edit must not write'); }
async function waitHeld(page, id) {
  await page.waitForFunction(id => window.__fixture.delays.some(delay => delay.id === id && delay.state === 'held'), id);
}
async function release(page, id) { await page.evaluate(id => window.__releaseDelay(id), id); await settle(page); }
async function draft(page, selector = tdpSelector) {
  const initial = await value(page, selector);
  await begin(page, selector);
  await press(page, 'nav-left', { repeat: false, source: 'native' });
  const edited = await value(page, selector);
  assert(edited < initial, 'Direction changes the visible draft');
  return { initial, edited };
}
// Keep long holds away from clamps, using a supported in-memory TDP ceiling.
async function primeCadenceRange(page) {
  await page.evaluate(() => { window.__fixture.controls.ac.tdpCeiling = 150; window.__refresh(); });
  await settle(page);
  assert.equal(await page.locator(tdpSelector).getAttribute('max'), '150', 'Cadence fixture has enough range for every observed single step');
}
function assertNativeBoundaries(events, expected) {
  assert(events.every(event => event.type === 'gamepad.slider-edit'), 'Only edit boundary notifications reach the native stub');
  const active = events.map(event => event.payload.active);
  for (let index = 0; index < active.length; index++) assert.equal(active[index], index % 2 === 0, 'Native notifications alternate start/end, without duplicate boundaries');
  if (expected) assert.deepEqual(active, expected, 'Exactly one native start/end per edit session, without tick IPC');
  return active;
}
const nativeEvents = page => page.evaluate(() => window.__nativeEditEvents);
// Advance only browser timers in memory. Vue is flushed after every callback,
// so a value setter sample represents each actual rendered keyboard step.
async function installCadenceClock(page, selector = tdpSelector) {
  await page.evaluate(selector => {
    const input = document.querySelector(selector);
    const valueProperty = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value');
    window.__cadenceInput = input;
    const ownValue = Object.getOwnPropertyDescriptor(input, 'value');
    const ownNow = Object.getOwnPropertyDescriptor(performance, 'now');
    const realSet = window.setTimeout.bind(window), realClear = window.clearTimeout.bind(window);
    let now = performance.now(), nextId = -1;
    const timers = new Map(), samples = [], scheduled = [];
    Object.defineProperty(performance, 'now', { configurable: true, value: () => now });
    Object.defineProperty(input, 'value', { configurable: true,
      get() { return valueProperty.get.call(this); },
      set(value) {
        const before = Number(valueProperty.get.call(this));
        valueProperty.set.call(this, value);
        const after = Number(valueProperty.get.call(this));
        if (before !== after) samples.push({ at: now, before, value: after, delta: after - before });
      },
    });
    window.setTimeout = (callback, delay = 0, ...args) => {
      if (typeof callback !== 'function') return realSet(callback, delay, ...args);
      const id = nextId--, ms = Math.max(0, Number(delay) || 0);
      timers.set(id, { at: now + ms, callback, args, ms });
      scheduled.push({ at: now, delay: ms });
      return id;
    };
    window.clearTimeout = id => { if (!timers.delete(id)) realClear(id); };
    const snapshot = () => ({ now, value: Number(input.value), samples: structuredClone(samples), scheduled: structuredClone(scheduled), pending: [...timers.values()].map(timer => ({ at: timer.at, delay: timer.ms })) });
    window.__cadenceClock = { snapshot,
      async advance(ms) {
        const end = now + ms;
        let count = 0;
        while (true) {
          const due = [...timers.entries()].filter(([, timer]) => timer.at <= end).sort((a, b) => a[1].at - b[1].at || b[0] - a[0])[0];
          if (!due) break;
          if (++count > 10000) throw Error('Browser timer did not settle');
          const [id, timer] = due;
          timers.delete(id); now = timer.at;
          timer.callback(...timer.args);
          await window.__flushVue();
        }
        now = end;
        await window.__flushVue();
        return snapshot();
      },
      restore() {
        window.setTimeout = realSet; window.clearTimeout = realClear;
        if (ownNow) Object.defineProperty(performance, 'now', ownNow); else delete performance.now;
        if (ownValue) Object.defineProperty(input, 'value', ownValue); else delete input.value;
        timers.clear();
      },
    };
  }, selector);
}
const cadence = page => page.evaluate(() => window.__cadenceClock.snapshot());
const advance = (page, ms) => page.evaluate(ms => window.__cadenceClock.advance(ms), ms);
function assertOneStep(samples, direction = 1, step = 1) {
  assert(samples.length > 0, 'The hold must produce actual value updates');
  for (const sample of samples) assert.equal(sample.delta, direction * step, 'Every timer tick changes exactly one slider step');
}
async function hideOrCancel(page, boundary) {
  await page.evaluate(({ boundary, selector }) => {
    const input = document.querySelector(selector);
    if (boundary === 'back') window.__press('back');
    else if (boundary === 'reset') input.dispatchEvent(new CustomEvent('gp:slider-edit-reset'));
    else if (boundary === 'blur') input.blur();
    else if (boundary === 'document-hidden') {
      Object.defineProperty(document, 'visibilityState', { configurable: true, value: 'hidden' });
      Object.defineProperty(document, 'hidden', { configurable: true, value: true });
      document.dispatchEvent(new Event('visibilitychange'));
    } else if (boundary === 'deactivated') window.__setPanelActive(false);
    else if (boundary === 'unmounted') window.__unmountPreview();
    else window.dispatchEvent(new CustomEvent(boundary));
  }, { boundary, selector: tdpSelector });
  await settle(page);
}
let browser;
const servers = [];
async function server(options) {
  const result = await createPerformancePreviewServer(0, { realNavigation: true, draftHarness: true, ...options });
  servers.push(result);
  return 'http://127.0.0.1:' + result.address().port;
}
async function open(base, height, scope = 'global') {
  const page = await browser.newPage({ viewport: { width: 1280, height } });
  page.setDefaultTimeout(6000);
  page.on('pageerror', error => report.errors.push(error.stack || String(error)));
  await page.goto(base + '/?scope=' + scope);
  await page.waitForSelector(tdpSelector);
  await page.evaluate(() => {
    window.__sliderEvents = [];
    for (const type of ['input', 'change', 'pointerdown', 'pointerup', 'gp:slider-edit-start', 'gp:slider-edit-end', 'gp:slider-edit-cancel', 'gp:slider-edit-invalidated']) {
      document.addEventListener(type, event => {
        if (event.target.closest?.('[data-gp-performance-panel]')) window.__sliderEvents.push({ type, at: performance.now(), trusted: event.isTrusted, label: event.target.getAttribute('aria-label'), value: event.target.value });
      }, true);
    }
  });
  await settle(page);
  return page;
}
try {
  if (!requestedBaseline) {
    // Re-enable the exact stale-refresh defect in the actual consumer, so
    // future repository runs need no external historical checkout/artifact.
    let negative = fs.readFileSync(path.join(root, 'src/components/ContinuousPerformancePanel.vue'), 'utf8');
    for (const [from, to] of [
      ['if ((editingInput || saving.value) && !checkContext)', 'if (saving.value && !checkContext)'],
      ['else if (editingInput || saving.value || revision !== draftRevision)', 'else if (saving.value)'],
    ]) {
      assert.equal(negative.split(from).length, 2, 'Negative-control anchor must match once: ' + from);
      negative = negative.replace(from, to);
    }
    fs.writeFileSync(baseline, negative);
  }
  report.baselineKind = requestedBaseline ? 'explicit-historical-source' : 'mutated-production-refresh-guards';
  assert(fs.existsSync(baseline), 'Baseline Panel is required before candidate testing: ' + baseline);
  browser = await chromium.launch({ channel: 'msedge', headless: true });
  // This must run before candidate cases, using a source override with original import resolution.
  const baselinePage = await open(await server({ panelSourceFile: baseline }), 720);
  try {
    const before = await draft(baselinePage);
    await noWrites(baselinePage);
    await baselinePage.evaluate(() => window.__refresh());
    await settle(baselinePage);
    const after = await value(baselinePage);
    const originalRollback = after === before.initial && after !== before.edited;
    report.baseline = { pass: false, failure: 'Unconfirmed draft reverted on same-context refresh', expectedFailure: originalRollback, assertion: 'Same-context refresh retains an unconfirmed draft', source: digest(baseline), before, after, state: await evidenceState(baselinePage) };
    await baselinePage.screenshot({ path: path.join(out, 'baseline-rollback.png'), fullPage: true });
    fs.writeFileSync(path.join(out, 'baseline-fail.json'), JSON.stringify(report.baseline, null, 2));
    assert(originalRollback, 'Baseline must reproduce the original rollback before testing the candidate');
    console.log('EXPECTED FAIL baseline: same-context refresh reverted ' + before.edited + ' to ' + after);
  } finally { await baselinePage.close(); }
  if (!baselineOnly) {
    const candidate = await server({});
    const simulationCandidate = await server({draftHarness:false,realNavigation:false});
    for (const height of [720, 800]) {
      async function test(name, callback, scope = 'global') {
        const caseStartedAt = performance.now();
        const entry = { name, viewport: { width: 1280, height }, scope, pass: false };
        let page;
        try {
          page = await open(candidate, height, scope);
          entry.details = await callback(page);
          entry.state = await evidenceState(page);
          assert.deepEqual(entry.state.fixture.rejectedWrites, [], 'No stale-owner commit attempts');
          assertNativeBoundaries(entry.state.nativeEvents);
          entry.pass = true;
        } catch (error) {
          entry.error = error.stack || String(error);
          if (page) {
            entry.state = await evidenceState(page).catch(() => null);
            await page.screenshot({ path: path.join(out, height + '-' + name + '-FAIL.png'), fullPage: true }).catch(() => {});
          }
        } finally {
          entry.elapsedMs = Math.round(performance.now() - caseStartedAt);
          report.cases.push(entry); report.count = report.cases.length;
          fs.writeFileSync(path.join(out, 'results.json'), JSON.stringify(report, null, 2));
          if (page) await page.close();
          console.log((entry.pass ? 'PASS ' : 'FAIL ') + height + ' ' + name + (entry.error ? ': ' + entry.error.split('\n')[0] : ''));
        }
      }
      await test('core-range-uses-configured-watts-runtime-is-readonly',async page=>{
        const core=page.locator('.core-slider input');
        await page.evaluate(()=>window.__externalTdp('global','ac',30));await settle(page);
        assert.equal(Number(await core.getAttribute('max')),30);
        const initial=Number(await core.inputValue()),count=(await writes(page)).length;
        await page.evaluate(()=>{for(const watts of [24,14,28,16])for(const fn of window.__tdpRuntimeListeners||[])fn({status:'applied',mode:'ac',watts});});await settle(page);
        assert.equal(Number(await core.getAttribute('max')),30);assert.equal(Number(await core.inputValue()),initial);assert.equal((await writes(page)).length,count);
        await page.evaluate(()=>window.__externalTdp('global','ac',10));await settle(page);
        assert.equal(Number(await core.getAttribute('max')),10);assert.equal(Number(await core.inputValue()),10);
        assert.equal(await page.evaluate(()=>window.__fixture.controls.ac.auto.coreSplit),initial);
        assert.ok((await page.locator('.core-heading').innerText()).includes('范围外'));
        return {savedBoundary:initial,range:10,writes:(await writes(page)).length};
      });
      await test('simulation-reset-keeps-system-until-explicit-stop',async page=>{
        await page.goto(simulationCandidate+'/?simulation=1');
        const child=page.frameLocator('.performance-simulation-window iframe');
        await child.locator('#session-reset').waitFor();
        const contentFrame=await(await page.locator('.performance-simulation-window iframe').elementHandle()).contentFrame();
        await contentFrame.waitForFunction(()=>window.ymccEnergyDemo?.viewState().sourceReady);
        const model=await contentFrame.evaluate(()=>{ymccEnergyDemo.pause();ymccEnergyDemo.advance(12);const s=ymccEnergyDemo.snapshot();return {controller:s.controller.optimizerVariant,source:ymccEnergyDemo.viewState().sourcePolicy.policySource,max:ymccEnergyDemo.viewState().sourcePolicy.auto.maxMHz}});
        assert.equal(model.controller,'production-controller');assert.equal(model.source,'production-pure-controller');assert.equal(model.max,4000);
        await child.locator('#session-reset').click();
        await child.locator('#session-reset-defaults').click();
        assert.equal(await page.evaluate(()=>window.__fixture.systemActions?.length||0),0);
        await child.locator('#session-reset').click();await child.locator('#session-stop-schedule').click();
        await child.locator('#session-stop-status').filter({hasText:'性能调度已关闭'}).waitFor();
        assert.deepEqual(await page.evaluate(()=>window.__fixture.systemActions),['stop-and-balanced']);
        assert.equal(await page.evaluate(()=>window.__fixture.windowsScheme),'balanced');
        const dialog=await child.locator('#session-reset-dialog').boundingBox();assert.ok(dialog.height<=height);
        await page.screenshot({path:path.join(out,height+'-stop-and-balanced.png')});
        return {actions:await page.evaluate(()=>window.__fixture.systemActions),dialog};
      });
      await test('draft-refresh-confirm-cancel', async page => {
        const before = await draft(page);
        await noWrites(page);
        for (let i = 0; i < 3; i++) { await page.evaluate(() => window.__refresh()); await settle(page); assert.equal(await value(page), before.edited, 'Refresh must preserve the draft'); }
        await page.screenshot({ path: path.join(out, height + '-draft-retained.png'), fullPage: true });
        await press(page, 'confirm');
        assert.equal((await writes(page)).length, 1, 'A commits exactly once');
        assert.equal((await writes(page))[0].patch.tdp, before.edited);
        await draft(page);
        await press(page, 'back');
        assert.equal(await value(page), before.edited);
        assert.equal((await writes(page)).length, 1, 'B adds no write');
        assert.equal(await page.evaluate(() => window.__readEdit()), false);
        // No-op A/A must also release Slider editing and permit refreshes.
        await begin(page); await press(page, 'confirm');
        const count = (await writes(page)).length;
        await page.evaluate(() => { window.__fixture.controls.ac.tdp -= 1; window.__refresh(); });
        await settle(page);
        assert.equal(await value(page), before.edited - 1, 'A without movement releases the edit lock');
        assert.equal((await writes(page)).length, count);
        return before;
      });
      for (const direction of ['nav-up', 'nav-down']) await test(direction + '-cancels', async page => {
        const before = await draft(page);
        await press(page, direction);
        assert.equal(await page.evaluate(() => window.__readEdit()), false);
        assert.equal(await value(page), before.initial);
        await noWrites(page);
        return before;
      });
      await test('old-read-new-input', async page => {
        const id = await page.evaluate(() => { const id = window.__delayNextRead(); window.__refresh(); return id; });
        await waitHeld(page, id);
        const before = await draft(page);
        await press(page, 'nav-left', { repeat: false });
        const edited = await value(page);
        await release(page, id);
        assert.equal(await value(page), edited, 'An old captured read cannot overwrite newer input');
        await noWrites(page);
        await press(page, 'confirm');
        assert.equal((await writes(page)).length, 1);
        assert.equal((await writes(page))[0].patch.tdp, edited);
        return { ...before, edited, delay: id };
      });
      await test('late-commit-ack-new-draft', async page => {
        const first = await draft(page);
        const id = await page.evaluate(() => window.__delayNextCommitAck());
        await press(page, 'confirm'); await waitHeld(page, id);
        assert.equal(await page.evaluate(() => window.__fixture.controls.ac.tdp), first.edited, 'Storage updates before ACK is released');
        const second = await draft(page);
        assert.equal((await writes(page)).length, 1, 'Second draft is unconfirmed');
        await release(page, id);
        assert.equal(await value(page), second.edited, 'Previous commit ACK cannot overwrite the next draft');
        assert.equal(await page.evaluate(() => window.__readEdit()), true);
        assert.equal((await writes(page)).length, 1);
        await press(page, 'confirm');
        assert.equal((await writes(page)).length, 2);
        assert.equal((await writes(page))[1].patch.tdp, second.edited);
        return { first, second, delay: id };
      });
      for (const context of ['power', 'scope', 'game', 'effective-owner']) await test(context + '-invalidates-edit', async page => {
        const before = await draft(page);
        const untouched = await page.evaluate(() => ({ global: structuredClone(window.__fixture.controls), games: structuredClone(window.__fixture.games) }));
        await page.evaluate(context => {
          if (context === 'power') window.__setPower('dc');
          if (context === 'scope') window.__setScope('dedicated');
          if (context === 'game') window.__setGame({ name: 'b.exe', path: 'C:/Games/b.exe', pid: 88, processCreated: '100088' });
          if (context === 'effective-owner') window.__setEffectiveScope('dedicated');
        }, context);
        await settle(page);
        assert.equal(await page.evaluate(() => window.__readEdit()), false, 'Context change clears the actual engine edit mode');
        const loaded = await value(page);
        await press(page, 'slider-decrease', { repeat: false });
        assert.equal(await value(page), loaded, 'An old edit cannot continue into another owner');
        await noWrites(page);
        const next = await draft(page); await press(page, 'confirm');
        const saved = await writes(page);
        assert.equal(saved.length, 1);
        assert.equal(saved[0].scope, ['scope', 'game', 'effective-owner'].includes(context) ? 'dedicated' : 'global');
        assert.equal(saved[0].side, context === 'power' ? 'dc' : 'ac');
        assert.equal(saved[0].exe, context === 'game' ? 'b.exe' : 'a.exe');
        assert.equal(saved[0].patch.tdp, next.edited);
        if (context === 'game') assert.deepEqual(await page.evaluate(() => window.__fixture.games['a.exe']), untouched.games['a.exe'], 'Original game remains unchanged');
        else if (context !== 'power') assert.deepEqual(await page.evaluate(() => window.__fixture.controls), untouched.global, 'Global bank remains unchanged');
        else assert.deepEqual(await page.evaluate(() => window.__fixture.controls.ac), untouched.global.ac, 'AC remains unchanged');
        return { before, next, owner: saved[0] };
      }, context === 'game' ? 'dedicated' : context === 'effective-owner' ? 'effective' : 'global');
      await test('same-game-status-retains-draft', async page => {
        const before = await draft(page);
        await page.evaluate(() => window.__setGame(structuredClone(window.__fixture.game)));
        await settle(page);
        assert.equal(await value(page), before.edited, 'A repeated status for the same game retains the draft');
        assert.equal(await page.evaluate(() => window.__readEdit()), true);
        await noWrites(page); await press(page, 'back');
        assert.equal(await value(page), before.initial);
        return before;
      }, 'dedicated');
      for (const context of ['power', 'scope', 'game', 'effective-owner']) {
        for (const boundary of ['read', 'ack']) await test(boundary + '-held-' + context + '-change', async page => {
          let id, old;
          if (boundary === 'read') {
            id = await page.evaluate(() => { const id = window.__delayNextRead(); window.__refresh(); return id; });
            await waitHeld(page, id); old = await draft(page);
          } else {
            old = await draft(page);
            id = await page.evaluate(() => window.__delayNextCommitAck());
            await press(page, 'confirm'); await waitHeld(page, id);
            await draft(page);
          }
          await page.evaluate(context => {
            if (context === 'power') window.__setPower('dc');
            if (context === 'scope') window.__setScope('dedicated');
            if (context === 'effective-owner') window.__setEffectiveScope('dedicated');
            if (context === 'game') window.__setGame({ name: 'b.exe', path: 'C:/Games/b.exe', pid: 88, processCreated: '100088' });
          }, context);
          await settle(page);
          assert.equal(await page.evaluate(() => window.__readEdit()), false, 'Context invalidates editing even with an in-flight operation');
          await press(page, 'slider-decrease', { repeat: false });
          assert.equal((await writes(page)).length, boundary === 'ack' ? 1 : 0);
          await release(page, id);
          const loaded = await value(page);
          assert.equal(await page.evaluate(() => window.__readEdit()), false);
          assert.equal(loaded, await page.evaluate(context => {
            const bank = context !== 'power' ? window.__fixture.dedicatedControls : window.__fixture.controls;
            return bank[context === 'power' ? 'dc' : 'ac'].tdp;
          }, context), 'A released old read/ACK cannot repaint the previous owner');
          const next = await draft(page); await press(page, 'confirm');
          const saved = await writes(page), last = saved.at(-1);
          assert.equal(saved.length, boundary === 'ack' ? 2 : 1);
          assert.equal(last.scope, context === 'power' ? 'global' : 'dedicated');
          assert.equal(last.side, context === 'power' ? 'dc' : 'ac');
          assert.equal(last.exe, context === 'game' ? 'b.exe' : 'a.exe');
          assert.equal(last.patch.tdp, next.edited);
          return { boundary, context, old, next, delay: id };
        }, context === 'effective-owner' ? 'effective' : context === 'game' ? 'dedicated' : 'global');
      }
      for (const hidden of ['ipc:window.hidden', 'ipc:window.minimized', 'document-hidden', 'deactivated']) await test(hidden.replaceAll(':', '-') + '-cancels', async page => {
        const before = await draft(page);
        await page.evaluate(hidden => {
          if (hidden === 'document-hidden') {
            Object.defineProperty(document, 'visibilityState', { configurable: true, value: 'hidden' });
            document.dispatchEvent(new Event('visibilitychange'));
          } else if (hidden === 'deactivated') window.__setPanelActive(false);
          else window.dispatchEvent(new CustomEvent(hidden));
        }, hidden);
        await settle(page);
        assert.equal(await page.evaluate(() => window.__readEdit()), false, 'Hiding cancels the actual engine editing state');
        await noWrites(page);
        if (hidden === 'deactivated') { await page.evaluate(() => window.__setPanelActive(true)); await settle(page); }
        assert.equal(await value(page), before.initial, 'Hidden draft is discarded');
        await page.locator(tdpSelector).focus(); await press(page, 'confirm');
        await noWrites(page); // The first A after showing must only enter a fresh edit.
        assert.equal(await page.evaluate(() => window.__readEdit()), true);
        await press(page, 'back'); await noWrites(page);
        return before;
      });
      await test('tap-and-linear-hold', async page => {
        const initial = await value(page);
        await begin(page);
        const deltas = await page.evaluate(async () => {
          const input = document.querySelector('input[aria-label="TDP"]');
          const original = Object.getOwnPropertyDescriptor(performance, 'now');
          const origin = performance.now(); let elapsed = 0; const deltas = [];
          Object.defineProperty(performance, 'now', { configurable: true, value: () => origin + elapsed });
          try {
            for (const heldMs of [0, 500, 900, 1300, 2000, 10000]) {
              elapsed = heldMs; const before = Number(input.value);
              window.__press('slider-decrease', { repeat: heldMs > 0, heldMs, source: 'native' });
              await window.__settle(); deltas.push(before - Number(input.value));
            }
          } finally { if (original) Object.defineProperty(performance, 'now', original); else delete performance.now; }
          return deltas;
        });
        assert.deepEqual(deltas, [1, 1, 1, 1, 1, 1], 'Every native event advances exactly one step, including arbitrarily long holds');
        const beforeReverse = await value(page);
        await press(page, 'slider-increase', { repeat: true, heldMs: 2000, source: 'native' });
        assert.equal(await value(page), beforeReverse + 1, 'Direction reversal remains exactly one step');
        await press(page, 'slider-increase', { repeat: false, source: 'native' });
        assert.equal(await value(page), beforeReverse + 2, 'A fresh tap stays precise');
        await noWrites(page);
        await press(page, 'back'); assert.equal(await value(page), initial);
        return { initial, deltas };
      });
      await test('all-performance-slider-drafts', async page => {
        const controls = [];
        for (const mode of ['auto', 'manual']) {
          if (mode === 'manual') {
            await page.locator('.control-modes button').nth(1).click(); await settle(page);
            // Exercise the enabled frequency slider through its real toggle when needed.
            const frequency = page.locator('input[aria-label="CPU 频率上限"]');
            if (await frequency.isDisabled()) { await page.locator('.manual-actions-grid button').filter({ hasText: '不限频率' }).click(); await settle(page); }
          }
          const labels = await page.locator('.continuous-performance input[type=range]:enabled').evaluateAll(inputs => inputs.map(input => input.getAttribute('aria-label')));
          for (const label of labels) {
            const selector = '.continuous-performance input[aria-label=' + JSON.stringify(label) + ']';
            const input = page.locator(selector);
            assert.equal(await input.getAttribute('data-gp-repeat-interval-ms'), '55', label + ' opts into the 55ms edit cadence');
            const initial = await value(page, selector), count = (await writes(page)).length;
            const direction = await input.evaluate(input => Number(input.value) > Number(input.min) ? 'nav-left' : 'nav-right');
            await begin(page, selector); await press(page, direction, { repeat: false });
            const edited = await value(page, selector);
            assert.notEqual(edited, initial, label + ' changes draft');
            await page.evaluate(() => window.__refresh()); await settle(page);
            assert.equal(await value(page, selector), edited, label + ' retains draft through refresh');
            assert.equal((await writes(page)).length, count);
            await press(page, 'back'); assert.equal(await value(page, selector), initial, label + ' cancels');
            assert.equal((await writes(page)).length, count);
            controls.push({ mode, label, initial, edited });
          }
        }
        return controls;
      });
      await test('keyboard-tap-and-refresh', async page => {
        const initial = await value(page);
        await page.locator(tdpSelector).focus();
        await installCadenceClock(page);
        await page.keyboard.down('ArrowLeft'); await settle(page);
        assert.equal(await value(page), initial - 1, 'Keyboard tap remains precise');
        await noWrites(page);
        await page.evaluate(() => window.__refresh()); await settle(page);
        assert.equal(await value(page), initial - 1);
        await page.keyboard.up('ArrowLeft'); await settle(page);
        assert.equal((await writes(page)).length, 1, 'Keyboard release commits once');
        return { initial, saved: initial - 1 };
      });
      await test('native-edit-session-boundaries', async page => {
        await primeCadenceRange(page);
        const initial = await value(page);
        await begin(page);
        // A repeated start for the same input must not cause another IPC.
        await page.locator(tdpSelector).dispatchEvent('gp:slider-edit-start', { bubbles: true });
        for (let index = 0; index < 12; index++) await press(page, 'slider-increase', { source: 'native', repeat: index > 0, heldMs: index * 300 });
        assert.equal(await value(page), initial + 12);
        assertNativeBoundaries(await nativeEvents(page), [true]);
        await noWrites(page);
        await press(page, 'confirm');
        assertNativeBoundaries(await nativeEvents(page), [true, false]);
        assert.equal((await writes(page)).length, 1);
        await draft(page); await press(page, 'back');
        assertNativeBoundaries(await nativeEvents(page), [true, false, true, false]);
        assert.equal((await writes(page)).length, 1, 'Cancel does not write');
        return { tickCount: 12, notifications: await nativeEvents(page) };
      });
      for (const accelerated of [true, false]) await test('browser-frame-single-step-' + (accelerated ? 'accelerated' : 'fixed'), async page => {
        await primeCadenceRange(page);
        const initial = await value(page);
        await begin(page);
        const trace = await page.evaluate(async ({ selector, accelerated }) => {
          const input = document.querySelector(selector), samples = [];
          if (!accelerated) input.dataset.gpAccelerate = 'false';
          const frame = async (at, left, right, phase) => {
            const before = Number(input.value), count = window.__sliderEvents.filter(event => event.type === 'input').length;
            window.__microAdjustSlider(at, left, right, false, false);
            await window.__flushVue();
            samples.push({ at, phase, before, value: Number(input.value), delta: Number(input.value) - before, inputCount: window.__sliderEvents.filter(event => event.type === 'input').length - count });
          };
          for (let at = 1000; at <= 3400; at += 5) await frame(at, false, true, 'hold');
          await frame(3401, true, false, 'reverse');
          await frame(3455, true, false, 'reverse-54ms');
          await frame(3456, true, false, 'reverse-55ms');
          await frame(3457, false, false, 'release');
          await frame(3458, true, false, 'fresh');
          await frame(3512, true, false, 'fresh-54ms');
          await frame(3513, true, false, 'fresh-55ms');
          await frame(10000, true, false, 'stalled');
          await frame(10000, true, false, 'same-frame');
          await frame(10024, true, false, 'stalled-24ms');
          await frame(accelerated ? 10025 : 10055, true, false, 'stalled-next');
          await frame(11000, false, false, 'released-final');
          return samples;
        }, { selector: tdpSelector, accelerated });
        for (const sample of trace) {
          assert(sample.inputCount <= 1, 'A frame never catches up multiple steps');
          if (sample.inputCount) assert.equal(Math.abs(sample.delta), 1, 'Every frame event moves exactly one step');
          else assert.equal(sample.delta, 0);
        }
        const hold = trace.filter(sample => sample.phase === 'hold' && sample.inputCount);
        assert.equal(hold[0].at, 1000, 'Initial press responds immediately');
        assert.equal(hold[1].at - hold[0].at, 55);
        const gaps = hold.slice(1).map((sample, index) => sample.at - hold[index].at);
        assert.equal(gaps.at(-1), accelerated ? 25 : 55);
        if (accelerated) assert(gaps.at(-1) < gaps[0], 'Held frame cadence becomes denser');
        else assert(gaps.every(gap => gap === 55), 'Disabled acceleration retains a fixed cadence');
        const delta = phase => trace.find(sample => sample.phase === phase).delta;
        for (const phase of ['reverse', 'reverse-55ms', 'fresh', 'fresh-55ms', 'stalled', 'stalled-next']) assert.equal(delta(phase), -1, phase + ' is exactly one step');
        for (const phase of ['reverse-54ms', 'release', 'fresh-54ms', 'same-frame', 'stalled-24ms', 'released-final']) assert.equal(delta(phase), 0, phase + ' produces no step');
        await noWrites(page);
        assertNativeBoundaries(await nativeEvents(page), [true]);
        await press(page, 'back');
        assert.equal(await value(page), initial);
        await noWrites(page);
        assertNativeBoundaries(await nativeEvents(page), [true, false]);
        return { initial, accelerated, gaps, trace };
      });
      await test('keyboard-hold-single-step-cadence', async page => {
        await primeCadenceRange(page);
        const initial = await value(page);
        await page.locator(tdpSelector).focus();
        await installCadenceClock(page);
        const start = (await cadence(page)).now;
        await page.keyboard.down('ArrowRight'); await settle(page);
        let state = await cadence(page);
        assert.equal(state.value, initial + 1, 'Initial keydown is one immediate step');
        assert.equal(state.pending.length, 1, 'Exactly one timer exists while held');
        await advance(page, 20);
        const beforeRepeat = await cadence(page);
        await page.locator(tdpSelector).dispatchEvent('keydown', { key: 'ArrowRight', repeat: true });
        await settle(page);
        assert.deepEqual(await cadence(page), beforeRepeat, 'OS autorepeat neither advances a step nor creates another timer');
        await advance(page, 2280);
        state = await cadence(page);
        assertOneStep(state.samples);
        const delays = state.scheduled.map(timer => timer.delay);
        assert.equal(delays[0], 55); assert.equal(delays.at(-1), 25);
        assert(delays.every((delay, index) => delay >= 25 && delay <= 55 && (!index || delay <= delays[index - 1])), 'Timer delays shorten monotonically from 55ms to 25ms');
        const early = state.samples.filter(sample => sample.at - start <= 400).length;
        const late = state.samples.filter(sample => sample.at - start > 1900).length;
        assert(late > early * 1.5, 'The later equal-length window contains more single steps');
        assert.equal(state.pending.length, 1);
        await noWrites(page); assertNativeBoundaries(await nativeEvents(page), [true]);
        await page.keyboard.up('ArrowRight'); await settle(page);
        const released = await cadence(page);
        assert.equal(released.pending.length, 0, 'Keyup removes the timer');
        await advance(page, 1000);
        assert.deepEqual((await cadence(page)).samples, released.samples, 'Release prevents further value changes');
        assert.equal((await writes(page)).length, 1, 'Release commits once');
        assert.equal((await writes(page))[0].patch.tdp, released.value);
        assertNativeBoundaries(await nativeEvents(page), [true, false]);
        return { initial, early, late, samples: state.samples, delays, released: released.value, sampleSource: 'actual HTMLInputElement.value setter' };
      });
      await test('keyboard-direction-release-reset', async page => {
        await primeCadenceRange(page);
        await page.locator(tdpSelector).focus(); await installCadenceClock(page);
        await page.keyboard.down('ArrowRight'); await advance(page, 2100);
        const held = await cadence(page);
        await page.keyboard.down('ArrowLeft'); await settle(page);
        const reversed = await cadence(page);
        assert.equal(reversed.value, held.value - 1, 'Reversal immediately moves one step');
        assert.equal(reversed.pending.length, 1); assert.equal(reversed.pending[0].delay, 55, 'Reversal resets cadence');
        await page.keyboard.up('ArrowRight'); await settle(page);
        assert.equal((await cadence(page)).pending.length, 1, 'Releasing the previous direction keeps the current hold');
        assert.equal((await writes(page)).length, 0);
        await advance(page, 54); assert.equal((await cadence(page)).value, reversed.value);
        await advance(page, 1); assert.equal((await cadence(page)).value, reversed.value - 1);
        await page.keyboard.up('ArrowLeft'); await settle(page);
        assert.equal((await cadence(page)).pending.length, 0);
        await page.keyboard.down('ArrowLeft'); await settle(page);
        const fresh = await cadence(page);
        assert.equal(fresh.value, reversed.value - 2, 'Fresh hold starts with one step');
        assert.equal(fresh.pending[0].delay, 55, 'Release resets cadence for the next hold');
        await page.keyboard.up('ArrowLeft'); await settle(page);
        assert.equal((await cadence(page)).pending.length, 0);
        assert.equal((await writes(page)).length, 2, 'Only the two deliberate releases commit');
        assertNativeBoundaries(await nativeEvents(page), [true, false, true, false]);
        return { held, reversed, fresh };
      });
      for (const boundary of ['back', 'reset', 'blur', 'ipc:window.hidden', 'ipc:window.minimized', 'document-hidden', 'deactivated', 'unmounted']) await test('keyboard-held-' + boundary.replaceAll(':', '-') + '-stops', async page => {
        await primeCadenceRange(page);
        const initial = await value(page);
        await begin(page); await installCadenceClock(page);
        await page.keyboard.down('ArrowRight'); await advance(page, 750);
        const held = await cadence(page); assertOneStep(held.samples);
        assert.equal(held.pending.length, 1); await noWrites(page);
        await hideOrCancel(page, boundary);
        const canceled = await cadence(page);
        assert.equal(canceled.pending.length, 0, boundary + ' clears the pending timer immediately');
        await advance(page, 1000);
        assert.deepEqual((await cadence(page)).samples, canceled.samples, boundary + ' prevents further input');
        // An OS repeat arriving after cancellation must not resurrect the hold.
        await page.evaluate(() => window.__cadenceInput.dispatchEvent(new KeyboardEvent('keydown', { key: 'ArrowRight', repeat: true, bubbles: true, cancelable: true })));
        await advance(page, 500);
        assert.equal((await cadence(page)).pending.length, 0, 'OS repeat after cancellation cannot restart the timer');
        assert.deepEqual((await cadence(page)).samples, canceled.samples, 'OS repeat after cancellation cannot change a value');
        await page.keyboard.up('ArrowRight');
        await page.evaluate(() => window.__cadenceInput.dispatchEvent(new KeyboardEvent('keyup', { key: 'ArrowRight', bubbles: true })));
        await settle(page); await noWrites(page);
        if (boundary === 'deactivated') { await page.evaluate(() => window.__setPanelActive(true)); await settle(page); }
        if (boundary === 'reset') await press(page, 'back');
        if (boundary !== 'unmounted') assert.equal(await value(page), initial, 'Canceled keyboard draft restores its original value');
        await noWrites(page);
        assertNativeBoundaries(await nativeEvents(page), [true, false]);
        return { boundary, initial, held, canceled };
      });
      await test('pointer-drag-and-refresh', async page => {
        const initial = await value(page);
        const input = page.locator(tdpSelector), box = await input.boundingBox();
        const count = (await writes(page)).length;
        await page.mouse.move(box.x + box.width * 0.3, box.y + box.height / 2);
        await page.mouse.down(); await page.mouse.move(box.x + box.width * 0.6, box.y + box.height / 2, { steps: 3 }); await settle(page);
        const dragged = await value(page);
        assert.notEqual(dragged, initial, 'The pointer actually moves the range');
        assert.equal((await writes(page)).length, count, 'Pointer drag is uncommitted before release');
        await page.evaluate(() => window.__refresh()); await settle(page); assert.equal(await value(page), dragged);
        await page.mouse.up(); await settle(page);
        assert.equal((await writes(page)).length, count + 1, 'Pointer release commits once');
        await page.locator(tdpSelector).dispatchEvent('change'); await settle(page);
        assert.equal((await writes(page)).length, count + 1, 'A later native change event cannot duplicate the pointer commit');
        // The CPU/GPU slider must remain a real pointer-operated control.
        const bias = page.locator('.continuous-performance input[aria-label="CPU / GPU 倾向"]');
        await bias.scrollIntoViewIfNeeded(); const biasBox = await bias.boundingBox(), beforeBias = (await writes(page)).length;
        await page.mouse.move(biasBox.x + biasBox.width * .5, biasBox.y + biasBox.height / 2);
        await page.mouse.down(); await page.mouse.move(biasBox.x + biasBox.width - 1, biasBox.y + biasBox.height / 2, {steps:4}); await settle(page);
        assert.equal(Number(await bias.inputValue()),100,'Dragging to GPU priority reaches 100');
        assert.equal((await writes(page)).length,beforeBias,'Dragging does not write per pointer tick');
        await page.evaluate(()=>window.__refresh()); await settle(page); assert.equal(Number(await bias.inputValue()),100);
        await page.mouse.up(); await settle(page); assert.equal((await writes(page)).length,beforeBias+1);
        assert.equal((await writes(page)).at(-1).patch.auto.bias,100); assert.equal(await page.evaluate(()=>window.__fixture.controls.ac.auto.bias),100);
        const cpuText=await page.locator('.parameter-summary').innerText();assert.ok(cpuText.includes('1.2'),'GPU priority updates the displayed CPU policy');
        await bias.dispatchEvent('change'); await settle(page);assert.equal((await writes(page)).length,beforeBias+1);
        return { initial, dragged, bias:100, cpuText };
      });
    }
  }
  assert.deepEqual(report.errors, [], 'Browser must not throw');
  assert(report.cases.every(test => test.pass), 'Candidate failures: ' + report.cases.filter(test => !test.pass).map(test => test.viewport.height + '/' + test.name).join(', '));
  for (const source of report.sources) assert.deepEqual(digest(source.path), source, 'Source changed during testing; rerun for exact evidence');
  report.pass = true;
} catch (error) { report.failure = error.stack || String(error); process.exitCode = 1; }
finally {
  report.elapsedMs = Math.round(performance.now() - startedAt);
  report.passedCount = report.cases.filter(test => test.pass).length;
  fs.writeFileSync(path.join(out, baselineOnly ? 'baseline-results.json' : 'results.json'), JSON.stringify(report, null, 2));
  if (!baselineOnly && !report.pass) fs.writeFileSync(path.join(out, 'candidate-fail.json'), JSON.stringify(report, null, 2));
  if (browser) await browser.close();
  await Promise.all(servers.map(server => new Promise(resolve => server.close(resolve))));
}
console.log(JSON.stringify({ pass: report.pass, mode: report.mode, count: report.count, elapsedMs: report.elapsedMs, baselineExpectedFailure: report.baseline?.expectedFailure, cases: report.cases.length, failures: report.cases.filter(test => !test.pass).length, errors: report.errors.length, out }));
if (report.failure) console.error(report.failure);
