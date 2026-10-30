import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import assert from 'node:assert/strict';
import { fileURLToPath } from 'node:url';
import ts from 'typescript';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const source = fs.readFileSync(path.join(root, 'CustomSteamLibrary/workspace-ui/app.js'), 'utf8');
function section(begin, end) {
  const first = source.indexOf(begin);
  const last = source.indexOf(end, first + begin.length);
  assert(first >= 0 && last > first, `Missing production section: ${begin}`);
  return source.slice(first, last);
}
const declarations = section('let steamAccountDiscovery =', 'let steamRuntimeLastCheck =');
const production = [
  declarations,
  section('let steamRuntimeState =', '// Cached userdata'),
  section('let steamRuntimeLastCheck =', 'let selectedGameDirectories ='),
  section('function applySteamRuntimeState(', 'function renderSteamTarget('),
  section('function isWorkspaceRecord(', 'function isBuiltinExcludedSteamTool('),
  section('function steamAccounts()', 'function defaultSelectedDirectories()'),
  section("let steamInstallPromptStatus = '';", 'function applySteamRuntimeState('),
  section('function renderSteamTarget()', 'function renderArtworkRefreshControl()'),
  section('async function openLibraryView()', "$('#busy-cancel').addEventListener"),
  section("window.addEventListener('focus', () => {", '// Startup is a fast local snapshot refresh'),
].join('\n');
// Execute the original complete startup expression without later UI initializers.
const syntax = ts.createSourceFile('app.js', source, ts.ScriptTarget.Latest, true, ts.ScriptKind.JS);
assert.equal(syntax.parseDiagnostics.length, 0, 'Production workspace JavaScript must parse');
function isStartupCall(expression) {
  if (!ts.isCallExpression(expression)) return false;
  if (ts.isPropertyAccessExpression(expression.expression)) return isStartupCall(expression.expression.expression);
  return ts.isIdentifier(expression.expression) && expression.expression.text === 'invoke' &&
    expression.arguments.length === 1 && ts.isStringLiteral(expression.arguments[0]) &&
    expression.arguments[0].text === 'startup';
}
const startupStatements = syntax.statements.filter(statement =>
  ts.isExpressionStatement(statement) && isStartupCall(statement.expression));
assert.equal(startupStatements.length, 1, 'Expected exactly one top-level startup call chain');
const startup = startupStatements[0].getText(syntax);
const ready = (count = 1) => ({
  steamStatus: 'ready',
  accounts: Array.from({ length: count }, (_, i) => ({ accountId: String(1000 + i), steamRoot: 'G:\\FixtureSteam' })),
});
const negative = (status = 'installed-no-account') => ({ steamStatus: status, accounts: [] });
function deferred() {
  let resolve, reject;
  const promise = new Promise((yes, no) => { resolve = yes; reject = no; });
  return { promise, resolve, reject };
}
async function flush() { for (let i = 0; i < 10; ++i) await Promise.resolve(); }
function element(hidden = false) {
  const classes = new Set(hidden ? ['hidden'] : []);
  return {
    textContent: '', innerHTML: '',
    classList: { add: name => classes.add(name), remove: name => classes.delete(name), contains: name => classes.has(name) },
    querySelector: () => ({ addEventListener() {} }),
  };
}
function harness(cached = negative(), accountReply = ready(), runtimeReply = () => Promise.resolve({steamRunning:false,steamPath:''})) {
  const elements = new Map([
    ['#steam-target-status', element()], ['#in-steam-limitation', element(true)],
    ['#entry-view', element()], ['#library-view', element(true)],
  ]);
  const calls = [], prompts = [], timers = [], events = new Map(), errors = [];
  let now = 10000, focused = false;
  const doc = {hidden:false,hasFocus:()=>focused,addEventListener:(name,fn)=>addEvent(name,fn)};
  const addEvent=(name,fn)=>{const previous=events.get(name);events.set(name,()=>{previous?.();fn();});};
  const schedule=fn=>timers.push(fn),cancel=id=>{if(id)timers[id-1]=null;};
  const context = vm.createContext({
    snapshot: { steamAccounts: structuredClone(cached) },
    selectedSteamTarget: { accountId: '', steamRoot: '' },
    $: selector => elements.get(selector),
    libraryNotice: elements.get('#in-steam-limitation'),
    Date: { now: () => now },
    document:doc,
    window: { setTimeout:schedule, clearTimeout:cancel, addEventListener:addEvent },
    invoke: command => {
      calls.push(command);
      if (command === 'steamAccounts') return typeof accountReply === 'function' ? accountReply() : Promise.resolve(accountReply);
      if (command === 'steamCommitPreflight') return runtimeReply();
      if (command === 'startup' || command === 'openLibrary') return Promise.resolve({ steamAccounts: structuredClone(cached) });
      throw new Error(`Unexpected command: ${command}`);
    },
    askConfirm: async (...args) => { prompts.push(args); return false; },
    showError: error => errors.push(error.message),
    renderSteamRuntimeState() {}, refreshSteamRuntimeState() {},
    renderEntry() {}, clearNotice() {}, scheduleLibraryFocusRepair() {},
  });
  vm.runInContext(production, context);
  context.startSteamRuntimeWatch();
  context.renderLibrary = () => context.renderSteamTarget();
  return {
    context, calls, prompts, timers, events, errors,
    copy: elements.get('#steam-target-status'), limitation: elements.get('#in-steam-limitation'),
    page: elements.get('#library-view'),
    tick: ms => { now += ms; },
    doc, focus:value=>{focused=value;events.get(value?'focus':'blur')?.();},
    activeTimers:()=>timers.filter(Boolean).length,
    fireTimer:async()=>{const index=timers.findIndex(Boolean);assert(index>=0,'Expected one scheduled refresh');const fn=timers[index];timers[index]=null;now+=3000;fn();await flush();},
    get: expression => vm.runInContext(expression, context),
  };
}
const cases = [];
async function run(name, action) {
  try { await action(); cases.push({ name, passed: true }); }
  catch (error) { cases.push({ name, passed: false, error: error.stack }); }
}
for (const status of ['installed-no-account', 'not-installed', 'error']) {
  await run(`startup-does-not-publish-cached-${status}`, () => {
    const h = harness(negative(status)); h.context.renderSteamTarget();
    assert.equal(h.copy.textContent, '正在检测 Steam 账户');
    assert.equal(h.limitation.classList.contains('hidden'), true);
    assert.equal(h.timers.length, 0);
  });
}
await run('cached-ready-is-not-presented-as-live-login-proof', () => {
  const h = harness(ready()); h.context.renderSteamTarget();
  assert.equal(h.copy.textContent, '正在检测 Steam 账户');
});
await run('initial-open-detects-without-click-or-game-scan-completion', async () => {
  const reply = deferred(), h = harness(negative(), reply.promise);
  const opening = h.context.openLibraryView(); await opening;
  assert.equal(h.page.classList.contains('hidden'), false);
  assert.equal(h.copy.textContent, '正在检测 Steam 账户');
  assert.equal(h.calls.filter(x => x === 'steamAccounts').length, 1);
  assert.equal(h.get('steamAccountRequest !== null'), true);
  reply.resolve(ready()); await flush();
  assert.match(h.copy.textContent, /已发现 1 个 Steam 大屏用户/);
  assert.equal(h.errors.length, 0);
});
await run('real-startup-chain-autodetects-stale-negative-cache', async () => {
  const reply = deferred(), h = harness(negative(), reply.promise);
  await vm.runInContext(startup, h.context);
  assert.deepEqual(h.calls, ['startup', 'openLibrary', 'steamAccounts']);
  assert.equal(h.copy.textContent, '正在检测 Steam 账户');
  assert.equal(h.timers.length, 0);
  reply.resolve(ready()); await flush();
  assert.match(h.copy.textContent, /已发现 1 个/);
});
await run('concurrent-and-forced-queries-coalesce', async () => {
  const reply = deferred(), h = harness(negative(), reply.promise);
  const first = h.context.refreshSteamAccountDiscovery();
  const second = h.context.refreshSteamAccountDiscovery(true);
  assert.equal(first, second); await flush();
  assert.equal(h.calls.length, 1); reply.resolve(ready()); await first;
  assert.equal(h.get('steamAccountRequest'), null);
});
await run('older-game-snapshot-cannot-restore-false-negative', async () => {
  const h = harness(); await h.context.refreshSteamAccountDiscovery();
  h.context.snapshot = { steamAccounts: negative() }; h.context.renderSteamTarget();
  assert.match(h.copy.textContent, /已发现 1 个/);
  assert.equal(h.context.steamAccounts().length, 1);
  assert.equal(h.limitation.classList.contains('hidden'), true);
});
await run('no-account-directory-is-not-a-login-or-install-warning', async () => {
  const h = harness(negative(), negative()); await h.context.refreshSteamAccountDiscovery();
  assert.match(h.copy.textContent, /暂未检测到可用的账户目录/);
  assert.doesNotMatch(h.copy.textContent, /未登录|尚未登录/);
  assert.match(h.limitation.textContent, /这不代表 Steam 当前未登录/);
  assert.equal(h.timers.length, 0); assert.equal(h.prompts.length, 0);
});
await run('confirmed-not-installed-retains-install-help', async () => {
  const h = harness(negative(), negative('not-installed')); await h.context.refreshSteamAccountDiscovery();
  assert.match(h.copy.textContent, /未检测到 Steam/);
  assert.equal(h.timers.length, 1); await h.timers[0]();
  assert.equal(h.prompts.length, 1); assert.equal(h.prompts[0][0], '未检测到 Steam');
});
await run('scheduled-install-prompt-is-discarded-after-fresh-ready', async () => {
  let reply = negative('not-installed'); const h = harness(negative(), () => Promise.resolve(reply));
  await h.context.refreshSteamAccountDiscovery(); reply = ready();
  await h.context.refreshSteamAccountDiscovery(true); await h.timers[0]();
  assert.equal(h.prompts.length, 0); assert.match(h.copy.textContent, /已发现 1 个/);
});
await run('focus-before-startup-does-not-start-a-second-command', () => {
  const h = harness(); h.events.get('focus')(); assert.equal(h.calls.length, 0);
});
await run('focus-refresh-is-rate-limited-and-recovers-after-login', async () => {
  let reply = negative(); const h = harness(negative(), () => Promise.resolve(reply));
  await h.context.refreshSteamAccountDiscovery(); h.events.get('focus')(); await flush();
  assert.equal(h.calls.length, 1);
  reply = ready(); h.tick(5001); h.events.get('focus')(); await flush();
  assert.equal(h.calls.length, 2); assert.match(h.copy.textContent, /已发现 1 个/);
});
await run('manual-retry-bypasses-focus-rate-limit', async () => {
  const h = harness(); await h.context.refreshSteamAccountDiscovery();
  await h.context.refreshSteamAccountDiscovery(true); assert.equal(h.calls.length, 2);
});
await run('multiple-account-targets-retain-all-users', async () => {
  const h = harness(negative(), ready(2)); await h.context.refreshSteamAccountDiscovery();
  assert.equal(h.context.steamAccounts().length, 2); assert.match(h.copy.textContent, /已发现 2 个/);
});
await run('worker-error-is-displayed-as-error-not-signed-out', async () => {
  const h = harness(negative(), { ...negative('error'), error: 'Fixture access denied' });
  await h.context.refreshSteamAccountDiscovery();
  assert.match(h.copy.textContent, /检测失败/); assert.equal(h.limitation.textContent, 'Fixture access denied');
  assert.equal(h.timers.length, 0);
});
await run('rejected-query-is-contained-and-can-be-retried', async () => {
  let reject = true; const h = harness(negative(), () => reject ? Promise.reject(new Error('Fixture timeout')) : Promise.resolve(ready()));
  await h.context.refreshSteamAccountDiscovery(); assert.equal(h.get('steamAccountRequest'), null);
  assert.equal(h.limitation.textContent, 'Fixture timeout'); reject = false;
  await h.context.refreshSteamAccountDiscovery(true); assert.match(h.copy.textContent, /已发现 1 个/);
});
const malformed = [null, [], {}, { accounts: null, steamStatus: 'ready' }, { accounts: [], steamStatus: 'ready' },
  { accounts: [{ accountId: 123 }], steamStatus: 'ready' }, { accounts: [{ accountId: ' ' }], steamStatus: 'ready' },
  { ...ready(), steamStatus: 'installed-no-account' }, { accounts: [], steamStatus: 'unknown' }];
for (const [i, reply] of malformed.entries()) {
  await run(`malformed-detection-${i}-is-not-login-warning`, async () => {
    const h = harness(negative(), reply); await h.context.refreshSteamAccountDiscovery();
    assert.match(h.copy.textContent, /检测失败/); assert.match(h.limitation.textContent, /结果无效/);
    assert.equal(h.timers.length, 0);
  });
}
await run('manual-scan-has-forced-account-recheck-hook', () => {
  const handler = section("scanLibraryButton.addEventListener('click', async () => {", "$('#pick-root-button').addEventListener");
  assert.match(handler, /void refreshSteamAccountDiscovery\(true\)/);
});
await run('background-render-and-events-never-scan-or-retain-a-timer', async () => {
  const h=harness();h.page.classList.remove('hidden');
  for(let i=0;i<100;i++){h.context.renderSteamTarget();h.events.get('blur')();}
  await flush();assert.equal(h.calls.filter(x=>x==='steamCommitPreflight').length,0);assert.equal(h.activeTimers(),0);
});
await run('focus-edge-refreshes-once-and-foreground-retains-one-timer', async () => {
  const h=harness();h.page.classList.remove('hidden');h.focus(true);await flush();
  assert.equal(h.calls.filter(x=>x==='steamCommitPreflight').length,1);assert.equal(h.activeTimers(),1);
  for(let i=0;i<100;i++){h.events.get('focus')();h.context.renderSteamTarget();}
  await flush();assert.equal(h.calls.filter(x=>x==='steamCommitPreflight').length,1);assert.equal(h.activeTimers(),1);
  await h.fireTimer();assert.equal(h.calls.filter(x=>x==='steamCommitPreflight').length,2);assert.equal(h.activeTimers(),1);
});
await run('blur-cancels-timer-and-refocus-rereads-steam-state-once', async () => {
  let running=false;const h=harness(negative(),ready(),()=>Promise.resolve({steamRunning:running}));h.page.classList.remove('hidden');h.focus(true);await flush();
  h.focus(false);assert.equal(h.activeTimers(),0);const count=h.calls.filter(x=>x==='steamCommitPreflight').length;
  for(let i=0;i<100;i++)h.context.renderSteamTarget();await flush();assert.equal(h.calls.filter(x=>x==='steamCommitPreflight').length,count);
  running=true;h.focus(true);await flush();assert.equal(h.get('steamRuntimeState.running'),true);assert.equal(h.calls.filter(x=>x==='steamCommitPreflight').length,count+1);assert.equal(h.activeTimers(),1);
});
await run('hidden-document-cancels-even-without-window-blur', async () => {
  const h=harness();h.page.classList.remove('hidden');h.focus(true);await flush();h.doc.hidden=true;h.events.get('visibilitychange')();
  assert.equal(h.activeTimers(),0);h.context.renderSteamTarget();await flush();assert.equal(h.calls.filter(x=>x==='steamCommitPreflight').length,1);
  h.doc.hidden=false;h.events.get('visibilitychange')();await flush();assert.equal(h.calls.filter(x=>x==='steamCommitPreflight').length,2);assert.equal(h.activeTimers(),1);
});
await run('inflight-read-completes-in-background-without-restarting-polling', async () => {
  const pending=deferred(),h=harness(negative(),ready(),()=>pending.promise);h.page.classList.remove('hidden');h.focus(true);h.focus(false);
  assert.equal(h.activeTimers(),0);pending.resolve({steamRunning:true});await flush();assert.equal(h.activeTimers(),0);assert.equal(h.calls.filter(x=>x==='steamCommitPreflight').length,1);
});
await run('slow-read-coalesces-focus-storm-and-all-render-requests', async () => {
  const pending=deferred(),h=harness(negative(),ready(),()=>pending.promise);h.page.classList.remove('hidden');h.focus(true);
  for(let i=0;i<100;i++){h.focus(false);h.focus(true);h.context.renderSteamTarget();}
  assert.equal(h.calls.filter(x=>x==='steamCommitPreflight').length,1);assert.equal(h.activeTimers(),0);
  pending.resolve({steamRunning:false});await flush();assert.equal(h.activeTimers(),1);
});
await run('pagehide-and-late-read-stay-dormant-until-pageshow', async () => {
  let pending=deferred();const h=harness(negative(),ready(),()=>pending.promise);h.page.classList.remove('hidden');h.focus(true);h.events.get('pagehide')();pending.resolve({steamRunning:false});await flush();
  assert.equal(h.activeTimers(),0);h.events.get('focus')();await flush();assert.equal(h.calls.filter(x=>x==='steamCommitPreflight').length,1);
  pending=deferred();h.events.get('pageshow')();assert.equal(h.calls.filter(x=>x==='steamCommitPreflight').length,2);pending.resolve({steamRunning:true});await flush();assert.equal(h.activeTimers(),1);
});
await run('manual-forced-refresh-in-background-is-one-shot', async () => {
  const h=harness();h.page.classList.remove('hidden');await h.context.refreshSteamRuntimeState(true);await flush();
  assert.equal(h.calls.filter(x=>x==='steamCommitPreflight').length,1);assert.equal(h.activeTimers(),0);
});
await run('failed-foreground-read-retains-one-normal-interval', async () => {
  const h=harness(negative(),ready(),()=>Promise.reject(Error('fixture unavailable')));h.page.classList.remove('hidden');h.focus(true);await flush();
  assert.equal(h.get('steamRuntimeState.running'),null);assert.equal(h.activeTimers(),1);h.focus(false);assert.equal(h.activeTimers(),0);
});
await run('hidden-library-page-stops-the-polling-owner', async () => {
  const h=harness();h.page.classList.remove('hidden');h.focus(true);await flush();h.page.classList.add('hidden');h.context.renderSteamTarget();
  assert.equal(h.activeTimers(),0);
});
const failed = cases.filter(item => !item.passed);
const report = { allPassed: failed.length === 0, caseCount: cases.length, failedCount: failed.length, cases,
  realSteamFilesModified: false, steamStoppedOrLaunched: false, networkUsed: false };
if (process.argv[2]) {
  const output = path.resolve(process.argv[2]); fs.mkdirSync(path.dirname(output), { recursive: true });
  fs.writeFileSync(output, JSON.stringify(report, null, 2));
}
console.log(JSON.stringify(report, null, 2));
if (!report.allPassed) process.exitCode = 1;
