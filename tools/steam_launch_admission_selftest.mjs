import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import crypto from 'node:crypto';
import ts from 'typescript';
import { parse } from 'vue/compiler-sfc';
import { transformSync } from 'esbuild';

// Exercise the existing bridge and the original view functions in an inert clock.
// Native process admission/fallback is covered by steam_native_launch_regression.
const source = fs.readFileSync('src/bridge/yeman.ts', 'utf8');
const at = source.indexOf('let steamLaunchInFlight =');
assert(at >= 0, 'manual launch bridge must be present');
const code = transformSync(source.slice(at), { loader: 'ts', format: 'cjs' }).code;
function fixture() {
  const f = { launches: 0, checks: 0, arguments: [], hold: null, failAck: false, failInvoke: false };
  const module = { exports: {} };
  vm.runInNewContext(code, {
    module, exports: module.exports, Error, Promise,
    steamRunning: async () => { f.checks++; throw Error('duplicate frontend process check'); },
    invoke: async (cmd, args, options) => {
      assert.equal(cmd, 'steam.ui.launch'); assert.equal(args.userInitiated, true);
      assert.deepEqual(Object.keys(args), ['userInitiated']); assert.equal(options.timeoutMs, 10000);
      f.launches++; f.arguments.push({ cmd, args, options });
      if (f.hold) await f.hold;
      if (f.failInvoke) throw Error('ack timeout');
      return { ok: !f.failAck, reason: 'fixture-refused' };
    },
    fs: new Proxy({}, { get() { throw Error('launcher file dependency forbidden'); } }),
    shell: new Proxy({}, { get() { throw Error('frontend shell/script fallback forbidden'); } }),
  });
  f.launch = module.exports.launchSteam; f.pending = module.exports.steamLaunchPending;
  return f;
}
const cases = [];
async function check(name, fn) { await fn(); cases.push(name); }
await check('remembered or non-explicit intent performs no IO', async () => {
  const f = fixture();
  for (const intent of [undefined, {}, { userInitiated: false }]) await assert.rejects(f.launch(intent), /手动启动/);
  assert.equal(f.launches + f.checks, 0); assert.equal(f.pending(), false);
});
await check('explicit action uses only strict native IPC with a finite response timeout', async () => {
  const f = fixture(); await f.launch({ userInitiated: true });
  assert.equal(f.launches, 1); assert.equal(f.checks, 0); assert.equal(f.pending(), false);
});
await check('completed manual dispatch has no time fence and permits an immediate explicit retry', async () => {
  const f = fixture(); await f.launch({ userInitiated: true }); await f.launch({ userInitiated: true });
  assert.equal(f.launches, 2); assert.equal(f.pending(), false);
});
await check('only an actual in-flight IPC blocks overlapping dispatch', async () => {
  const f = fixture(); let done; f.hold = new Promise(resolve => done = resolve);
  const first = f.launch({ userInitiated: true }); assert.equal(f.pending(), true);
  await assert.rejects(f.launch({ userInitiated: true }), /尚未结束/);
  f.hold = null; done(); await first; assert.equal(f.launches, 1); assert.equal(f.pending(), false);
});
await check('lost IPC reply releases the gate without automatic replay or frontend fallback', async () => {
  const f = fixture(); f.failInvoke = true;
  await assert.rejects(f.launch({ userInitiated: true }), /ack timeout/);
  assert.equal(f.launches, 1); assert.equal(f.pending(), false);
  f.failInvoke = false; await f.launch({ userInitiated: true }); assert.equal(f.launches, 2);
});
await check('negative native acknowledgement stays a failure and allows a new explicit action', async () => {
  const f = fixture(); f.failAck = true;
  await assert.rejects(f.launch({ userInitiated: true }), /启动未发出/);
  assert.equal(f.pending(), false); assert.equal(f.launches, 1);
  f.failAck = false; await f.launch({ userInitiated: true }); assert.equal(f.launches, 2);
});
await check('canceled user action never reaches native dispatch', async () => {
  const f = fixture(); await assert.rejects(f.launch({ userInitiated: true, isCurrent: () => false }), /取消/);
  assert.equal(f.launches + f.checks, 0);
});
await check('last intent checkpoint rejects cancellation before IPC and releases the gate', async () => {
  const f = fixture(); let calls = 0;
  await assert.rejects(f.launch({ userInitiated: true, isCurrent: () => ++calls === 1 }), /取消/);
  assert.equal(f.launches, 0); assert.equal(f.pending(), false);
});

const view = fs.readFileSync('src/views/SteamView.vue', 'utf8');
const script = parse(view).descriptor.scriptSetup.content;
const ast = ts.createSourceFile('SteamView.ts', script, ts.ScriptTarget.Latest, true, ts.ScriptKind.TS);
const functions = new Set(['steamReadVisible', 'detectSteamState', 'acceptSteamState', 'refreshSteamState',
  'queueSteamStateRefresh', 'queueSteamResumeRefresh', 'stopSteamPolling', 'finishSteamPower',
  'cancelSteamPowerWait', 'pollSteamState', 'launch', 'showNotice', 'switchSteamMode', 'toggleSteamPower', 'steamActionText']);
const variables = new Set(['running', 'steamUiState', 'steamPendingTarget', 'steamReadEpoch', 'steamEventTimer',
  'steamReadBudget', 'steamForceObservation', 'busy', 'errMsg', 'steamRunKnown', 'steamStateStale', 'steamChecking',
  'noticeTimer', 'steamPollTimer', 'steamPollDeadlineTimer', 'steamFirstPollTimer', 'steamPollBusy',
  'steamPollGeneration', 'steamActive', 'steamPowerIntent', 'STEAM_FIRST_LAUNCH_CHECK_MS',
  'STEAM_POLL_INTERVAL_MS', 'STEAM_POLL_TIMEOUT_MS']);
const selected = [];
for (const node of ast.statements) {
  if (ts.isFunctionDeclaration(node) && functions.has(node.name?.text)) {
    functions.delete(node.name.text); selected.push(node.getText(ast));
  } else if (ts.isVariableStatement(node) && node.declarationList.declarations.some(d => variables.has(d.name.getText(ast)))) {
    selected.push(node.getText(ast));
  }
}
assert.equal(functions.size, 0, 'all exercised functions must be extracted from the real component');
const viewCode = transformSync(selected.join('\n') + `
export {launch, toggleSteamPower, refreshSteamState, queueSteamStateRefresh, queueSteamResumeRefresh, cancelSteamPowerWait};
export function activate(){steamActive=true;}
export function snapshot(){return {busy:busy.value, checking:steamChecking.value, pending:steamPowerPending,
 target:steamPendingTarget.value, running:running.value, known:steamRunKnown.value, state:steamUiState.value,
 notice:errMsg.value, action:steamActionText()};}
`, { loader: 'ts', format: 'cjs' }).code;
const budgetSource = fs.readFileSync('src/bridge/steamReadBudget.ts', 'utf8');
const budgetModule = { exports: {} };
vm.runInNewContext(transformSync(budgetSource, { loader: 'ts', format: 'cjs' }).code,
  { module: budgetModule, exports: budgetModule.exports });
const flush = async () => { for (let i = 0; i < 20; i++) await Promise.resolve(); };
function viewFixture() {
  const f = { now: 0, visible: true, launches: 0, reads: [], holdRead: null, failLaunch: false,
    state: { known: true, running: false, mode: 'stopped', pid: 0, birth: '0', reason: '' } };
  let nextId = 1; const timers = new Map();
  const schedule = (fn, delay, interval = false) => {
    const id = nextId++; timers.set(id, { fn, at: f.now + delay, repeat: interval ? delay : 0 }); return id;
  };
  f.advance = async ms => {
    const end = f.now + ms; let count = 0;
    for (;;) {
      const [id, timer] = [...timers].filter(([, t]) => t.at <= end).sort((a, b) => a[1].at - b[1].at || a[0] - b[0])[0] || [];
      if (!timer) break;
      assert(++count < 1000, 'finite timer budget'); f.now = timer.at;
      if (timer.repeat) timer.at += timer.repeat; else timers.delete(id);
      timer.fn(); await flush();
    }
    f.now = end; await flush();
  };
  const module = { exports: {} };
  vm.runInNewContext(viewCode, {
    module, exports: module.exports, ref: value => ({ value }),
    createSteamReadBudget: budgetModule.exports.createSteamReadBudget,
    performance: { now: () => f.now },
    setTimeout: (fn, delay) => schedule(fn, delay), clearTimeout: id => timers.delete(id),
    setInterval: (fn, delay) => schedule(fn, delay, true), clearInterval: id => timers.delete(id),
    isUiVisible: () => f.visible,
    getUiVisibilityState: () => ({ documentVisible: f.visible, nativeWindowVisible: f.visible }),
    steamUi: {
      read: async () => { f.reads.push(f.now); const state = { ...f.state }; if (f.holdRead) await f.holdRead; return state; },
      switchMode: async () => { throw Error('cold launch must not switch modes'); },
    },
    launchSteam: async options => {
      assert.equal(options.userInitiated, true); assert.equal(options.isCurrent(), true);
      f.launches++; if (f.failLaunch) throw Error('fixture-refused');
    }, Error, Math,
  });
  f.view = module.exports; f.view.activate();
  f.ready = () => { f.state = { known: true, running: true, mode: 'bigpicture', pid: 123, birth: '456', reason: '' }; };
  return f;
}
await check('actual view cold launch has no preflight read and leaves the retry button usable after acknowledgement', async () => {
  const f = viewFixture(); await f.view.launch();
  assert.equal(f.launches, 1); assert.deepEqual(f.reads, []);
  const s = f.view.snapshot(); assert.equal(s.busy, false); assert.equal(s.checking, true);
  assert.equal(s.action, '再次启动 Steam');
  assert.match(view, /class="steam-power-button"[\s\S]*?:disabled="busy"/);
});
await check('first launch read occurs at 5000ms, never immediately or at 4999ms', async () => {
  const f = viewFixture(); f.ready(); await f.view.launch(); await f.advance(4999);
  assert.deepEqual(f.reads, []); await f.advance(1); assert.deepEqual(f.reads, [5000]);
  assert.equal(f.view.snapshot().running, true); assert.equal(f.view.snapshot().checking, false);
  await f.advance(60000); assert.deepEqual(f.reads, [5000]); assert.equal(f.launches, 1);
});
await check('unconfirmed launch uses bounded four-second reads and expires at forty seconds without relaunch', async () => {
  const f = viewFixture(); f.state = { known: false, running: false, mode: 'unknown' }; await f.view.launch();
  await f.advance(40000); assert.deepEqual(f.reads, [5000, 9000, 13000, 17000, 21000, 25000, 29000, 33000, 37000]);
  assert.equal(f.view.snapshot().checking, false); assert.equal(f.view.snapshot().pending, false);
  assert.match(f.view.snapshot().notice, /暂未检测/); await f.advance(60000); assert.equal(f.reads.length, 9); assert.equal(f.launches, 1);
});
await check('retry before the first read cancels the old timer and starts a fresh five-second observation', async () => {
  const f = viewFixture(); await f.view.toggleSteamPower(); await f.advance(1000); await f.view.toggleSteamPower();
  f.ready(); await f.advance(4999); assert.deepEqual(f.reads, []);
  await f.advance(1); assert.deepEqual(f.reads, [6000]); assert.equal(f.launches, 2);
  await f.advance(50000); assert.deepEqual(f.reads, [6000]);
});
await check('resume and UI event storms cannot bypass the first five-second delay or issue launches', async () => {
  const f = viewFixture(); f.ready(); await f.view.launch();
  for (let i = 0; i < 100; i++) { f.view.queueSteamStateRefresh(); f.view.queueSteamResumeRefresh(); await f.view.refreshSteamState({ automatic: true }); }
  await f.advance(4999); assert.deepEqual(f.reads, []); await f.advance(1); assert.deepEqual(f.reads, [5000]);
  await f.advance(4999); assert.deepEqual(f.reads, [5000]);
  await f.advance(1); assert.deepEqual(f.reads, [5000, 10000]); assert.equal(f.launches, 1);
  await f.advance(60000); assert.equal(f.reads.length, 2);
});
await check('hide or page leave cancels the delayed read and the finite deadline', async () => {
  const f = viewFixture(); await f.view.launch(); f.visible = false; f.view.cancelSteamPowerWait();
  await f.advance(60000); assert.deepEqual(f.reads, []); assert.equal(f.launches, 1);
  assert.equal(f.view.snapshot().busy, false); assert.equal(f.view.snapshot().checking, false);
});
await check('a held read from before launch cannot confirm the new intent or replace its state', async () => {
  const f = viewFixture(); let release;
  f.holdRead = new Promise(resolve => release = resolve);
  const old = f.view.refreshSteamState(); await f.view.launch(); f.ready(); await f.advance(5000);
  f.holdRead = null; release(); await old; await flush();
  assert.equal(f.view.snapshot().known, false); assert.equal(f.view.snapshot().checking, true);
  await f.advance(4000); assert.equal(f.view.snapshot().running, true); assert.equal(f.launches, 1);
});
await check('failed launch creates no confirmation poll and accepts immediate explicit recovery', async () => {
  const f = viewFixture(); f.failLaunch = true; await f.view.launch(); await f.advance(60000);
  assert.deepEqual(f.reads, []); assert.equal(f.launches, 1); assert.equal(f.view.snapshot().busy, false);
  f.failLaunch = false; f.ready(); await f.view.launch(); await f.advance(5000);
  assert.equal(f.launches, 2); assert.deepEqual(f.reads, [65000]);
});
const sha256 = text => crypto.createHash('sha256').update(text).digest('hex');
const result = { ok: true, passed: cases.length, cases,
  sourceHashes: { 'src/bridge/yeman.ts': sha256(source), 'src/views/SteamView.vue': sha256(view), 'src/bridge/steamReadBudget.ts': sha256(budgetSource) },
  scope: 'actual bridge and AST-extracted original Vue functions; deterministic timers; native admission checked separately',
  actualSteamActions: 0, actualBATChanges: 0 };
if (process.argv[2]) fs.writeFileSync(process.argv[2], JSON.stringify(result, null, 2));
console.log(JSON.stringify(result, null, 2));
