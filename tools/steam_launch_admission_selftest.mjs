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
  const f = { launches: 0, checks: 0, arguments: [], hold: null, failAck: false, failInvoke: false, shells: [], allowAddonShell: false, shellResult:true };
  const module = { exports: {} };
  vm.runInNewContext(code, {
    module, exports: module.exports, Error, Promise,
    steamRunning: async () => { f.checks++; throw Error('duplicate frontend process check'); },
    invoke: async (cmd, args, options) => {
      assert.equal(cmd, 'steam.ui.launch'); assert.equal(args.userInitiated, true);
      assert.deepEqual(Object.keys(args), args.selectedExecutable === undefined ? ['userInitiated'] : ['userInitiated','selectedExecutable']); assert.equal(options.timeoutMs, 10000);
      f.launches++; f.arguments.push({ cmd, args, options });
      if (f.hold) await f.hold;
      if (f.failInvoke) throw Error('ack timeout');
      return { ok: !f.failAck, reason: 'fixture-refused' };
    },
    fs: new Proxy({}, { get() { throw Error('launcher file dependency forbidden'); } }),
    shell: { execute: async (file,args) => { assert.equal(f.allowAddonShell,true,'frontend shell/script fallback forbidden'); assert.equal(args.length,0); f.shells.push(file); return f.shellResult; } },
  });
  f.launch = module.exports.launchSteam; f.pending = module.exports.steamLaunchPending; f.addon = module.exports.launchSteamAddon;
  return f;
}
const flags=process.argv.slice(2).filter(v=>v.startsWith('--'));
assert(flags.length<=1&&flags.every(v=>['--selected-addon','--ordinary-addon-only','--quick-app','--shortcut-app','--shortcut-app-routing'].includes(v)),'unknown or conflicting launch selection');
const addonOnly = flags.includes('--selected-addon');
const ordinaryOnly = flags.includes('--ordinary-addon-only');
const quickOnly = flags.includes('--quick-app');
const shortcutRoutingOnly=flags.includes('--shortcut-app-routing'),shortcutOnly=shortcutRoutingOnly||flags.includes('--shortcut-app');
const cases = [], failures = [];
async function check(name, fn) {
  if (shortcutOnly && !name.startsWith('shortcut app:')) return;
  if (shortcutRoutingOnly && name !== 'shortcut app: actual request routes selected Steam through the common owner and deduplicates') return;
  if (quickOnly && !name.startsWith('quick app:')) return;
  if (ordinaryOnly && name !== 'selected addon: ordinary programs and opaque scripts retain the exact plain shell action') return;
  if (addonOnly && !name.startsWith('selected addon:')) return;
  try {
    await fn();
    cases.push(name);
  } catch (error) {
    // Each case has its own mocked state. Preserve the failure and still collect the sibling evidence.
    failures.push({ name, code: error?.code ?? null, error: error?.stack ?? String(error) });
    console.error(name + ': ' + (error?.stack ?? String(error)));
  }
}
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
const viewCode = shortcutOnly ? '' : compileView();
function compileView() {
const functions = new Set(['steamReadVisible', 'detectSteamState', 'acceptSteamState', 'retainSteamObservation',
  'steamObservationDelay', 'resumeQueuedSteamObservation', 'refreshSteamState', 'queueSteamStateRefresh',
  'queueSteamResumeRefresh', 'stopSteamConfirmation', 'finishSteamPower',
  'retireSteamObservation', 'cancelSteamPowerWait', 'confirmSteamState', 'launch', 'showNotice',
  'switchSteamMode', 'toggleSteamPower', 'steamActionText']);
const variables = new Set(['running', 'steamUiState', 'steamPendingTarget', 'steamReadEpoch', 'steamEventTimer',
  'steamObservationEpoch', 'steamStateReadBusy', 'steamObservationCancellation', 'steamObservationScopePending',
  'steamStateRefreshQueued', 'steamReadBudget', 'steamLastStatusRead', 'steamForceObservation', 'busy', 'errMsg',
  'steamRunKnown', 'steamStateStale', 'steamChecking', 'noticeTimer', 'steamConfirmation', 'steamConfirmationHint',
  'steamActive', 'steamPowerIntent', 'steamPowerPending', 'steamVisibilitySeedPending', 'STEAM_CONFIRM_TIMEOUT_MS',
  'STEAM_LAUNCH_BACKSTOPS_MS', 'STEAM_MODE_BACKSTOPS_MS', 'STEAM_CONFIRM_EVENT_GAP_MS']);
const missingFunctions = new Set(functions), missingVariables = new Set(variables);
const selected = [];
for (const node of ast.statements) {
  if (ts.isFunctionDeclaration(node) && functions.has(node.name?.text)) {
    assert(missingFunctions.delete(node.name.text), 'duplicate production function: '+node.name.text);
    selected.push(node.getText(ast));
  } else if (ts.isVariableStatement(node) && node.declarationList.declarations.some(d => variables.has(d.name.getText(ast)))) {
    for (const declaration of node.declarationList.declarations) {
      const name=declaration.name.getText(ast);
      if (variables.has(name)) assert(missingVariables.delete(name), 'duplicate production variable: '+name);
    }
    selected.push(node.getText(ast));
  }
}
assert.deepEqual([...missingFunctions, ...missingVariables], [], 'all exercised bindings must be extracted from the real component');
return transformSync(selected.join('\n') + `
export {launch, toggleSteamPower, refreshSteamState, queueSteamStateRefresh, queueSteamResumeRefresh, cancelSteamPowerWait};
export function activate(){steamActive=true;}
export function snapshot(){return {busy:busy.value, checking:steamChecking.value, pending:steamPowerPending,
 target:steamPendingTarget.value, running:running.value, known:steamRunKnown.value, state:steamUiState.value,
 notice:errMsg.value, action:steamActionText()};}
`, { loader: 'ts', format: 'cjs' }).code;
}
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
    STEAM_AUTOMATIC_READ_GAP_MS: budgetModule.exports.STEAM_AUTOMATIC_READ_GAP_MS,
    AbortController,
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
await check('unconfirmed launch uses bounded backstop reads and expires at forty seconds without relaunch', async () => {
  const f = viewFixture(); f.state = { known: false, running: false, mode: 'unknown' }; await f.view.launch();
  await f.advance(39999); assert.deepEqual(f.reads, [5000, 9000, 16000, 23000, 30000, 37000]);
  assert.equal(f.view.snapshot().checking, true); assert.equal(f.view.snapshot().pending, true);
  await f.advance(1);
  assert.equal(f.view.snapshot().checking, false); assert.equal(f.view.snapshot().pending, false);
  assert.match(f.view.snapshot().notice, /暂未检测/); await f.advance(60000); assert.equal(f.reads.length, 6); assert.equal(f.launches, 1);
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
  const f = viewFixture(); let releaseOld, releaseCurrent;
  f.ready(); // Even an old reply reporting a running process must not settle the new intent.
  f.holdRead = new Promise(resolve => releaseOld = resolve);
  const old = f.view.refreshSteamState(); await f.view.launch();
  f.holdRead = new Promise(resolve => releaseCurrent = resolve);
  try {
    await f.advance(5000); assert.deepEqual(f.reads, [0]);
    releaseOld(); assert.equal(await old, null); await flush();
    assert.deepEqual(f.reads, [0, 5000]);
    assert.equal(f.view.snapshot().known, false); assert.equal(f.view.snapshot().checking, true);
    releaseCurrent(); await f.advance(4000);
    assert.equal(f.view.snapshot().running, true); assert.equal(f.view.snapshot().checking, false);
    assert.deepEqual(f.reads, [0, 5000]); assert.equal(f.launches, 1);
  } finally { releaseOld(); releaseCurrent(); }
});
await check('failed launch creates no confirmation poll and accepts immediate explicit recovery', async () => {
  const f = viewFixture(); f.failLaunch = true; await f.view.launch(); await f.advance(60000);
  assert.deepEqual(f.reads, []); assert.equal(f.launches, 1); assert.equal(f.view.snapshot().busy, false);
  f.failLaunch = false; f.ready(); await f.view.launch(); await f.advance(5000);
  assert.equal(f.launches, 2); assert.deepEqual(f.reads, [65000]);
});

const addonFunctions=ast.statements.filter(n=>ts.isFunctionDeclaration(n)&&['launchCustomAddon','launchFixedAddon'].includes(n.name?.text));
assert.equal(addonFunctions.length,2);
const addonCode=transformSync(addonFunctions.map(n=>n.getText(ast)).join('\n')+'\nexport {launchCustomAddon,launchFixedAddon};',{loader:'ts',format:'cjs'}).code;
function addonView(f){const module={exports:{}},errMsg={value:''};vm.runInNewContext(addonCode,{module,exports:module.exports,errMsg,Error,shell:{execute:async file=>f.shells.push(file)},launchSteamAddon:f.addon});return {...module.exports,errMsg};}
await check('selected addon: actual custom and fixed buttons route literal Steam through the shared owner',async()=>{
 const f=fixture(),v=addonView(f);await v.launchCustomAddon({exe:'X:\\Steam\\StEaM.ExE'});await v.launchFixedAddon('X:/Steam/steam.exe');
 assert.equal(f.launches,2);assert.equal(f.shells.length,0);assert.equal(v.errMsg.value,'');assert.equal(f.arguments[0].args.selectedExecutable,'X:\\Steam\\StEaM.ExE');assert.equal(f.arguments[1].args.selectedExecutable,'X:/Steam/steam.exe');assert.equal(f.checks,0);
});
await check('selected addon: ordinary programs and opaque scripts retain the exact plain shell action',async()=>{
 const f=fixture(),v=addonView(f);f.allowAddonShell=true;for(const exe of ['X:\\helper.exe','X:\\steam.exe.bat','X:\\steam.lnk','X:\\steam-helper.exe'])await v.launchCustomAddon({exe});
 assert.deepEqual(f.shells,['X:\\helper.exe','X:\\steam.exe.bat','X:\\steam.lnk','X:\\steam-helper.exe']);assert.equal(f.launches,0);
});
await check('selected addon: missing explicit intent or cancelled caller performs zero IO',async()=>{
 const f=fixture();for(const opts of [undefined,{}, {userInitiated:false}])await assert.rejects(f.addon('X:\\steam.exe',opts),/手动/);
 await assert.rejects(f.addon('X:\\helper.exe',{userInitiated:true,isCurrent:()=>false}),/取消/);assert.equal(f.launches+f.shells.length,0);
});
await check('selected addon: direct and custom requests share one actual in-flight gate',async()=>{
 const f=fixture();let done;f.hold=new Promise(r=>done=r);const old=f.launch({userInitiated:true});await assert.rejects(f.addon('X:\\steam.exe',{userInitiated:true}),/尚未结束/);done();await old;f.hold=null;
 await f.addon('X:\\steam.exe',{userInitiated:true});assert.equal(f.launches,2);assert.equal(f.shells.length,0);assert.equal(f.pending(),false);
});
await check('selected addon: negative or lost native acknowledgement never falls back to generic shell',async()=>{
 const f=fixture(),v=addonView(f);f.failAck=true;await v.launchCustomAddon({exe:'X:\\steam.exe'});assert.match(v.errMsg.value,/启动未发出/);f.failAck=false;f.failInvoke=true;
 await v.launchFixedAddon('X:\\steam.exe');assert.match(v.errMsg.value,/ack timeout/);assert.equal(f.launches,2);assert.equal(f.shells.length,0);assert.equal(f.pending(),false);
});
await check('selected addon: selected executable is captured before the last intent checkpoint',async()=>{
 const f=fixture(),opts={userInitiated:true,selectedExecutable:'X:\\steam.exe',isCurrent:()=>{opts.selectedExecutable='Y:\\steam.exe';return true;}};
 await f.launch(opts);assert.equal(f.arguments[0].args.selectedExecutable,'X:\\steam.exe');assert.equal(f.shells.length,0);
});


let quickSource;
if(quickOnly){
 quickSource=fs.readFileSync('src/views/QuickAppView.vue','utf8');
 const qScript=parse(quickSource).descriptor.scriptSetup.content,qAst=ts.createSourceFile('QuickApp.ts',qScript,ts.ScriptTarget.Latest,true,ts.ScriptKind.TS);
 const qNodes=qAst.statements.filter(n=>ts.isFunctionDeclaration(n)&&n.name?.text==='launchApp');assert.equal(qNodes.length,1);
 const qCode=transformSync(qNodes[0].getText(qAst)+'\nexport {launchApp};',{loader:'ts',format:'cjs'}).code;
 function quickView(f,path='X:/Steam/steam.exe'){
  const module={exports:{}},launchBusy={value:false},launchAppsReady={value:true},errMsg={value:''},statusMsg={value:''},launchApps={value:[{path,name:'Chosen app'}]};
  let closes=0;vm.runInNewContext(qCode,{module,exports:module.exports,launchBusy,launchAppsReady,launchApps,errMsg,statusMsg,Error,closeMenu:()=>closes++,launchSteamAddon:f.addon,shell:{execute:async file=>f.shells.push(file)}});
  return {...module.exports,launchBusy,launchAppsReady,launchApps,errMsg,statusMsg,closes:()=>closes};
 }
 await check('quick app: actual literal Steam button reaches the shared typed owner with its selected path',async()=>{
  const f=fixture(),v=quickView(f);await v.launchApp(0);assert.equal(f.launches,1);assert.equal(f.arguments[0].args.selectedExecutable,'X:/Steam/steam.exe');assert.equal(f.shells.length,0);assert.equal(f.checks,0);assert.equal(v.launchBusy.value,false);assert.equal(v.errMsg.value,'');assert.match(v.statusMsg.value,/已发送启动请求/);
 });
 await check('quick app: ordinary programs and scripts keep their exact plain action',async()=>{
  const f=fixture();f.allowAddonShell=true;for(const p of ['X:/helper.exe','X:/start.cmd']){const v=quickView(f,p);await v.launchApp(0);assert.equal(v.errMsg.value,'');assert.equal(v.launchBusy.value,false);}assert.deepEqual(f.shells,['X:/helper.exe','X:/start.cmd']);assert.equal(f.launches,0);
 });
 await check('quick app: busy unready and invalid index cannot dispatch',async()=>{
  const f=fixture(),v=quickView(f);v.launchBusy.value=true;await v.launchApp(0);v.launchBusy.value=false;v.launchAppsReady.value=false;await v.launchApp(0);v.launchAppsReady.value=true;await v.launchApp(-1);await v.launchApp(99);assert.equal(f.launches+f.shells.length,0);assert.equal(v.launchBusy.value,false);
 });
 await check('quick app: another Steam caller holding the same flight cannot trigger shell replay',async()=>{
  const f=fixture(),v=quickView(f);let release;f.hold=new Promise(r=>release=r);const first=f.launch({userInitiated:true});await v.launchApp(0);assert.match(v.errMsg.value,/尚未结束/);assert.equal(f.launches,1);assert.equal(f.shells.length,0);assert.equal(v.launchBusy.value,false);f.hold=null;release();await first;await v.launchApp(0);assert.equal(f.launches,2);assert.equal(v.errMsg.value,'');
 });
 await check('quick app: native rejection and lost acknowledgement leave failure and no success claim',async()=>{
  const f=fixture(),v=quickView(f);f.failAck=true;await v.launchApp(0);assert.match(v.errMsg.value,/启动未发出/);assert.equal(v.statusMsg.value,'');f.failAck=false;f.failInvoke=true;await v.launchApp(0);assert.match(v.errMsg.value,/ack timeout/);assert.equal(v.statusMsg.value,'');assert.equal(f.launches,2);assert.equal(f.shells.length,0);assert.equal(v.launchBusy.value,false);assert.equal(f.pending(),false);
 });
 await check('quick app: repeated own clicks keep one pending command and release busy on completion',async()=>{
  const f=fixture(),v=quickView(f);let release;f.hold=new Promise(r=>release=r);const first=v.launchApp(0);assert.equal(v.launchBusy.value,true);await v.launchApp(0);assert.equal(f.launches,1);assert.equal(v.closes(),1);f.hold=null;release();await first;assert.equal(v.launchBusy.value,false);assert.equal(f.pending(),false);assert.equal(f.shells.length,0);
 });
}

let shortcutSources;
if(shortcutOnly){
 const runtimeRel='src/bridge/controllerShortcutRuntime.ts',actionsRel='src/bridge/controllerShortcutActions.ts',fanRel='src/bridge/deckyFanActions.ts',powerRel='src/bridge/powerLifecycleState.ts';
 shortcutSources=Object.fromEntries([runtimeRel,actionsRel,fanRel,powerRel].map(rel=>[rel,fs.readFileSync(rel,'utf8')]));
 const moduleCode=(rel)=>{const ast=ts.createSourceFile(rel,shortcutSources[rel],ts.ScriptTarget.Latest,true,ts.ScriptKind.TS);return transformSync(ast.statements.filter(n=>!ts.isImportDeclaration(n)).map(n=>n.getText(ast)).join('\n'),{loader:'ts',format:'cjs'}).code;};
 const runtimeCode=moduleCode(runtimeRel),actionsCode=moduleCode(actionsRel);
 const fan={exports:{}};vm.runInNewContext(moduleCode(fanRel),{module:fan,exports:fan.exports});
 const powerModule={exports:{}};vm.runInNewContext(moduleCode(powerRel),{module:powerModule,exports:powerModule.exports,queueMicrotask,setTimeout,clearTimeout});
 function shortcutFixture(f,path='X:/Steam/steam.exe',realPower=false){
  const h={now:100000,apps:[{path,name:'Selected app'}],power:{generation:7,known:true,phase:'ready',inputReady:true,hardwareWritesAllowed:true,suspendSerial:1,observedNotificationSequence:1,publicationRevision:1},events:new Map(),complete:[],fanCommands:[],fanSaves:[],claims:0,releases:0,holds:0,settingsReads:0,powerReads:0,powerSignals:[],holdPower:null,holdPowerAt:1,holdSettings:null,holdFanWrite:null,holdWindow:null,taskViews:0,windowReads:0,minimizes:0};
  const powerEvents=new Map(),shared=realPower?powerModule.exports.createSharedPowerLifecycleSource({
   read:async()=>({...h.power}),listen(name,fn){powerEvents.set(name,fn);return()=>powerEvents.delete(name);},
  }):null;
  if(shared)h.publishPower=patch=>{h.power={...h.power,...patch};powerEvents.get('power.lifecycleChanged')?.(h.power);};
  h.getSharedPower=shared?.getSharedPowerLifecycle??(()=>h.power);
  const frames={side:'ac',actualSide:'ac',identity:{pid:0},frameRates:{ac:{fps:45,lastFps:45}},frameRateApplied:true};
  h.frameReads=[];h.frameWrites=[];h.frameApplied=[];
  class Clock extends Date {static now(){return h.now;}}
  const a={exports:{}};vm.runInNewContext(actionsCode,{module:a,exports:a.exports,Error,Promise,Date:Clock,Map,Set,AbortController});
  const module={exports:{}},shell={execute:async(file,args)=>{assert.equal(args.length,0);f.shells.push(file);return f.shellResult;}};
  vm.runInNewContext(runtimeCode,{module,exports:module.exports,Error,Promise,
   createControllerShortcutActions:a.exports.createControllerShortcutActions,launchSteamAddon:f.addon,
   shell:{...shell,taskView:async()=>{h.taskViews++;return true;}},windowApi:{getState:async signal=>{h.windowReads++;h.windowSignal=signal;if(h.holdWindow)await h.holdWindow;return {visible:true,minimized:false};},minimize:async()=>{h.minimizes++;return true;}},powerLifecycle:{get:async(_timeout,signal)=>{h.powerReads++;h.powerSignals.push(signal);const result={...h.power,...h.nativePower?.(h.powerReads)};if(h.holdPower&&h.powerReads===h.holdPowerAt)await h.holdPower;return result;}},
   on:(name,fn)=>{h.events.set(name,fn);return()=>h.events.delete(name);},onSharedPowerLifecycle:shared?.onSharedPowerLifecycle??((fn,options)=>{fn(h.power);h.publishPower=patch=>{const prior=h.power;h.power={...prior,...patch};if(Object.keys(patch).some(key=>prior[key]!==h.power[key]&&key!=='publicationRevision'&&(key!=='suspendSerial'||options?.observeSuspendSerial)&&(key!=='observedNotificationSequence'||options?.observeNotificationSequence)))fn(h.power);};return()=>{};}),
   beginSharedPowerLifecycleReadback:shared?.beginSharedPowerLifecycleReadback??(()=>()=>{}),getSharedPowerLifecycle:h.getSharedPower,
   isSharedPowerLifecycleAdmitted:powerModule.exports.isSharedPowerLifecycleAdmitted,
   invoke:async(cmd,args,options)=>{if(cmd==='input.shortcutAction.claim'){h.claims++;return h.onClaim?.(args.id,options,h.claims)??{ok:true};}assert.equal(cmd,'input.shortcutAction.complete');h.complete.push(args.outcome);return h.onComplete?.(args.id,options)??{ok:true};},
   readSettingsSection:async name=>{assert(['quickApps','fan'].includes(name));h.settingsReads++;if(h.holdSettings)await h.holdSettings;return name==='fan'?{preset:'soft'}:{apps:h.apps};},
   tryAcquireQuickAction:()=>{h.holds++;return()=>h.releases++;},
   getPolicyGame:()=>null,powerSourceMode:{value:'ac'},frameRateSetting:value=>value,
   effectiveFrameRates:async(_current,access)=>{h.frameReads.push(['effective',access]);await h.holdFrameEffective;return {identity:'',dedicated:false};},
   getContinuousPerformanceSnapshot:async(scope,_side,_timing,access)=>{h.frameReads.push([scope,access]);await h.holdFrameSnapshot;return frames;},
   updateContinuousPerformanceControls:async(identity,patch,admit)=>{
    assert.equal(identity,frames.identity);h.frameWrites.push(patch.frameRate.fps);
    await h.holdFrameBeforeCommit;
    if(admit&&!admit())throw Error('outdated');
    h.frameApplied.push(patch.frameRate.fps);await h.holdFrameAfterCommit;
    return {...frames,frameRates:{ac:patch.frameRate}};
   },
   createFanMirrorActions:fan.exports.createFanMirrorActions,
   fanHostLifecycle:{hasControlIntent:true,controlReady:true,state:'ready',recoveryActive:false,
    async start(){return {allowed:true,writeReady:true};},async applyPreset(preset){h.fanCommands.push(preset);if(h.holdFanWrite)await h.holdFanWrite;}},fanControlActive:{value:true},
   getFanSelectedPreset:()=>h.fanSaves.at(-1)??'soft',normalizeFanPresetCurves:()=>({soft:[],balanced:[],aggressive:[]}),saveFanPresetSelection:async preset=>{h.fanSaves.push(preset);},setFanControlActive:()=>{},setFanNavigationDuty:()=>{},
   observeFanMirrorDisplay:()=>()=>{},isFanMirrorUiBusy:()=>false,recordFanHandshake:()=>{},fanFeatureEnabled:{value:true},
  });
  h.runtime=module.exports.startControllerShortcutRuntime();
  const submit=(id,actionId,params,powerScope)=>h.events.get('input.shortcutAction')({schemaVersion:1,id,actionId,params,issuedAtUnixMs:h.now,expiresAtUnixMs:h.now+30000,powerGeneration:powerScope?.[0]??7,inputRevision:3,...(powerScope===undefined?{}:{powerScope})});
  h.submit=(id='one',appPath=path,powerScope)=>submit(id,'app.launchQuickApp',{appPath},powerScope);
  h.submitFan=(id,preset)=>submit(id,'fan.switchPreset',{preset});
  h.submitSwitch=id=>submit(id,'os.switchProgram',{});
  h.submitFrame=(id,fps='60')=>submit(id,'display.frameRateControl',{operation:'set',fps});
  h.invalidate=()=>h.publishPower({notificationPending:true});h.stop=()=>h.runtime.stop();return h;
 }
 await check('shortcut app: actual request routes selected Steam through the common owner and deduplicates',async()=>{
  const f=fixture(),h=shortcutFixture(f);try{assert.equal(h.submit(),true);for(let i=0;i<100;i++)assert.equal(h.submit(),false);await h.runtime.idle();assert.equal(f.launches,1);assert.equal(f.arguments[0].args.selectedExecutable,'X:/Steam/steam.exe');assert.equal(f.shells.length,0);assert.equal(f.checks,0);assert.equal(h.complete.length,1);assert.equal(h.complete[0].state,'applied');assert.equal(h.holds,1);assert.equal(h.releases,1);}finally{h.stop();}
 });
 await check('shortcut app: ordinary program and opaque script preserve their plain result',async()=>{
  const f=fixture();f.allowAddonShell=true;for(const path of ['X:/helper.exe','X:/start.cmd']){const h=shortcutFixture(f,path);try{h.submit();await h.runtime.idle();assert.equal(h.complete.length,1);assert.equal(h.complete[0].state,'applied');assert.equal(h.releases,1);}finally{h.stop();}}assert.deepEqual(f.shells,['X:/helper.exe','X:/start.cmd']);assert.equal(f.launches,0);
 });
 await check('shortcut app: false ordinary shell outcome still produces failure',async()=>{
  const f=fixture();f.allowAddonShell=true;f.shellResult=false;const h=shortcutFixture(f,'X:/helper.exe');try{h.submit();await h.runtime.idle();assert.equal(h.complete.length,1);assert.equal(h.complete[0].state,'failed');assert.deepEqual(f.shells,['X:/helper.exe']);assert.equal(f.launches,0);assert.equal(h.releases,1);}finally{h.stop();}
 });
 await check('shortcut app: removed library member cannot dispatch',async()=>{
  const f=fixture(),h=shortcutFixture(f);h.apps=[];try{h.submit();await h.runtime.idle();assert.equal(f.launches+f.shells.length,0);assert.equal(h.complete.length,1);assert.equal(h.complete[0].state,'failed');assert.equal(h.releases,1);}finally{h.stop();}
 });
 await check('shortcut app: changed power generation cannot dispatch',async()=>{
  const f=fixture(),h=shortcutFixture(f);h.power.generation=8;try{h.submit();await h.runtime.idle();assert.equal(f.launches+f.shells.length,0);assert.equal(h.complete.length,1);assert.equal(h.complete[0].state,'failed');assert.equal(h.holds,1);assert.equal(h.releases,1);}finally{h.stop();}
 });
 await check('shortcut app: invalidation during settings wait retires old request without replay',async()=>{
  const f=fixture(),h=shortcutFixture(f);let release;h.holdSettings=new Promise(r=>release=r);try{h.submit();await new Promise(resolve=>setImmediate(resolve));assert.equal(h.settingsReads,1);h.invalidate();release();await h.runtime.idle();assert.equal(f.launches+f.shells.length,0);assert.equal(h.complete.length,0);assert.equal(h.releases,1);assert.equal(f.pending(),false);}finally{release();h.stop();}
 });
 await check('shortcut app: merged Ready retires each old claim wait before its reply',async()=>{
  const flush=()=>new Promise(resolve=>setImmediate(resolve)),beforeReply=[];
  for(const holdAt of [1,2,3]){
   const f=fixture(),h=shortcutFixture(f,undefined,true);
   let release,entered=false,signal;
   const oldReply=new Promise(resolve=>release=resolve);
   h.onClaim=(id,options,attempt)=>{
    if(id==='old'&&attempt===holdAt){entered=true;signal=options?.signal;return oldReply;}
    return {ok:true};
   };
   try{
    await flush();assert(h.submit('old'));await flush();assert(entered,'old request must enter claim '+holdAt);
    assert(h.submit('queued-old'));
    h.publishPower({publicationRevision:2});await flush();
    assert.equal(signal?.aborted??false,false,'metadata alone must not retire the original claim');
    assert.equal(h.claims,holdAt);assert.equal(f.launches,0);
    h.publishPower({publicationRevision:3,suspendSerial:2,observedNotificationSequence:2});
    assert(h.submit('current'));await flush();
    beforeReply.push({holdAt,aborted:signal?.aborted??false,launches:f.launches,results:h.complete.length,releases:h.releases,claims:h.claims-holdAt});
    release({ok:true});await h.runtime.idle();
    assert.equal(f.launches,1,'late successful old claim cannot start a second launch');
    assert.equal(h.complete.length,1);
    assert.equal(h.holds,h.releases);
   }finally{release({ok:true});h.stop();await h.runtime.idle();}
  }
  assert.deepEqual(beforeReply,[1,2,3].map(holdAt=>({holdAt,aborted:true,launches:1,results:1,releases:holdAt===1?1:2,claims:3})));
 });
 await check('shortcut app: settled business action releases only its old feedback wait on merged Ready',async()=>{
  const flush=()=>new Promise(resolve=>setImmediate(resolve)),beforeReply=[];
  for(const failed of [false,true]){
   const f=fixture(),h=shortcutFixture(f,undefined,true),sent=[];
   let release,signal;
   const oldReply=new Promise(resolve=>release=resolve);
   h.onComplete=(id,options)=>{sent.push(id);if(id==='old'){signal=options?.signal;return oldReply;}return {ok:true};};
   try{
    await flush();f.failAck=failed;assert(h.submit('old'));await flush();
    assert.equal(f.launches,1);assert.deepEqual(sent,['old']);assert.equal(h.complete[0].state,failed?'failed':'applied');
    h.publishPower({publicationRevision:2});await flush();assert.equal(signal?.aborted??false,false);
    h.publishPower({publicationRevision:3,suspendSerial:2,observedNotificationSequence:2});
    f.failAck=false;assert(h.submit('current'));await flush();
    beforeReply.push({failed,aborted:signal?.aborted??false,launches:f.launches,sent:[...sent]});
    release({ok:true});await h.runtime.idle();
    assert.equal(f.launches,2);assert.deepEqual(sent,['old','current']);assert.equal(h.complete[1].state,'applied');
   }finally{release({ok:true});h.stop();await h.runtime.idle();}
  }
  assert.deepEqual(beforeReply,[false,true].map(failed=>({failed,aborted:true,launches:2,sent:['old','current']})));
 });
 await check('shortcut app: a newer notification releases both stale settings reads before their replies',async()=>{
  const flush=()=>new Promise(resolve=>setImmediate(resolve));
  const beforeReply={};
  for(const section of ['quickApps','fan']){
   const f=fixture(),h=shortcutFixture(f);let release,settled=false;
   h.holdSettings=new Promise(r=>release=r);
   try{
    if(section==='quickApps')h.submit();else h.submitFan('one','aggressive');
    await flush();assert.equal(h.settingsReads,1);
    h.publishPower({publicationRevision:2});await flush();
    assert.equal(h.releases,0,'a publication revision alone leaves the current settings read in place');
    h.publishPower({publicationRevision:3,observedNotificationSequence:2});
    h.holdSettings=null;assert(h.submitSwitch('after'));
    void h.runtime.idle().then(()=>{settled=true;});await flush();
    beforeReply[section]={settled,taskViews:h.taskViews,releases:h.releases};
    release();await h.runtime.idle();
    assert.equal(f.launches+f.shells.length+h.fanSaves.length+h.fanCommands.length,0,'a retired request performs none of its actions');
    assert.equal(h.taskViews,1);assert.equal(h.minimizes,1);
    assert.equal(h.complete.length,1);assert.equal(h.complete[0].state,'applied');assert.equal(h.releases,2);
   }finally{release();h.stop();await h.runtime.idle();}
  }
  assert.deepEqual(beforeReply,{quickApps:{settled:true,taskViews:1,releases:2},fan:{settled:true,taskViews:1,releases:2}},'old settings reads must not occupy the shared shortcut queue');
 });
 await check('shortcut app: merged suspend releases stale power read before the old reply',async()=>{
  const f=fixture(),h=shortcutFixture(f);let release,settled=false;h.holdPower=new Promise(r=>release=r);
  const flush=()=>new Promise(resolve=>setImmediate(resolve));
  try{h.submit();await flush();assert.equal(h.powerReads,1);h.publishPower({suspendSerial:2});h.holdPower=null;
   h.submit('after');h.events.get('power.pending')?.({generation:7,suspendSerial:1});
   void h.runtime.idle().then(()=>{settled=true;});await flush();
   assert.equal(settled,true,'old shortcut read blocked the current request');assert.equal(f.launches,1);assert.equal(h.releases,2);assert.equal(h.complete.length,1);
   release();await flush();assert.equal(f.launches,1);assert.equal(h.complete.length,1);
  }finally{release();h.stop();await h.runtime.idle();}
 });
 await check('shortcut app: stale FPS preflight releases both reads before their replies',async()=>{
  const flush=()=>new Promise(resolve=>setImmediate(resolve)),beforeReply={};
  for(const stage of ['holdFrameEffective','holdFrameSnapshot']){
   const h=shortcutFixture(fixture());let release;
   h[stage]=new Promise(resolve=>release=resolve);
   try{
    assert(h.submitFrame('old'));await flush();
    assert.equal(h.frameReads.length,stage==='holdFrameEffective'?1:2);
    h.publishPower({publicationRevision:2});await flush();assert.equal(h.releases,0);
    h.publishPower({publicationRevision:3,observedNotificationSequence:2});
    h[stage]=null;assert(h.submitSwitch('fresh'));await flush();
    beforeReply[stage]={taskViews:h.taskViews,releases:h.releases};
    release();await h.runtime.idle();
    assert.deepEqual(h.frameWrites,[],'the late preflight must not reach the write owner');
    assert.equal(h.taskViews,1);assert.equal(h.complete.length,1);
   }finally{release();h.stop();await h.runtime.idle();}
  }
  assert.deepEqual(beforeReply,{holdFrameEffective:{taskViews:1,releases:2},holdFrameSnapshot:{taskViews:1,releases:2}});
 });
 await check('shortcut app: queued FPS write rechecks scope while an issued write keeps its receipt',async()=>{
  const flush=()=>new Promise(resolve=>setImmediate(resolve));
  for(const phase of ['holdFrameBeforeCommit','holdFrameAfterCommit']){
   const h=shortcutFixture(fixture());let release;
   h[phase]=new Promise(resolve=>release=resolve);
   try{
    assert(h.submitFrame('old'));await flush();assert.deepEqual(h.frameWrites,[60]);
    h.publishPower({suspendSerial:2,observedNotificationSequence:2});
    assert(h.submitFrame('fresh','45'));await flush();assert.deepEqual(h.frameWrites,[60]);assert.equal(h.releases,0);
    h[phase]=null;release();await h.runtime.idle();
    assert.deepEqual(h.frameWrites,[60,45]);
    assert.deepEqual(h.frameApplied,phase==='holdFrameBeforeCommit'?[45]:[60,45]);
    assert.equal(h.complete.length,1);assert.equal(h.complete[0].state,'applied');assert.equal(h.releases,2);
    assert.deepEqual(h.frameReads,[['effective','read-only'],['global','read-only'],['effective','read-only'],['global','read-only']]);
   }finally{release();h.stop();await h.runtime.idle();}
  }
 });
 await check('shortcut app: a committed launch keeps the queue across a merged suspend',async()=>{
  const f=fixture(),h=shortcutFixture(f);let release;f.hold=new Promise(r=>release=r);
  const flush=()=>new Promise(resolve=>setImmediate(resolve));
  try{h.submit();await flush();assert.equal(f.launches,1);h.publishPower({suspendSerial:2});h.submit('after');await flush();
   assert.equal(f.launches,1);assert.equal(h.settingsReads,1);assert.equal(h.releases,0);
   f.hold=null;release();await h.runtime.idle();assert.equal(f.launches,2);assert.equal(h.complete.length,1);assert.equal(h.complete[0].state,'applied');
  }finally{release();h.stop();await h.runtime.idle();}
 });
 await check('shortcut app: Native sees a newer scope before the broadcast at each power read',async()=>{
  const rows={};
  for(const [stage,at] of [['first',1],['before-launch',2],['fan-before-save',3],['fan-before-write',5]]){
   const f=fixture(),h=shortcutFixture(f,undefined,true),isFan=stage.startsWith('fan');
   try{
    await new Promise(resolve=>setImmediate(resolve));
    assert.equal(h.getSharedPower()?.observedNotificationSequence,1);
    h.nativePower=read=>read>=at?{suspendSerial:2,observedNotificationSequence:2,publicationRevision:2}:{};
    if(isFan)assert(h.submitFan('old','soft'));else assert(h.submit());
    await h.runtime.idle();
    rows[stage]={scope:h.getSharedPower()?.observedNotificationSequence,launches:f.launches,saves:h.fanSaves.length,writes:h.fanCommands.length,feedback:h.complete.length};
    assert(h.submitSwitch('fresh'));await h.runtime.idle();assert.equal(h.taskViews,1);
    assert.equal(h.releases,2);
   }finally{h.stop();}
  }
  assert.deepEqual(rows,{
   first:{scope:2,launches:0,saves:0,writes:0,feedback:0},
   'before-launch':{scope:2,launches:0,saves:0,writes:0,feedback:0},
   'fan-before-save':{scope:2,launches:0,saves:0,writes:0,feedback:0},
   'fan-before-write':{scope:2,launches:0,saves:1,writes:0,feedback:0},
  });
 });
 await check('shortcut app: a current Native ticket survives its own ahead-of-broadcast readback',async()=>{
  const rows=[],flush=()=>new Promise(resolve=>setImmediate(resolve));
  for(const generation of [7,8]){
   const f=fixture(),h=shortcutFixture(f,undefined,true);
   let release;
   const held=new Promise(resolve=>release=resolve);
   try{
    await flush();assert.equal(h.getSharedPower()?.suspendSerial,1);
    h.holdPower=held;
    assert(h.submit('fresh',undefined,[generation,2,2]));await flush();
    assert.equal(h.powerReads,1);
    h.power={...h.power,generation,suspendSerial:2,observedNotificationSequence:2,publicationRevision:2};
    h.holdPower=null;release();await h.runtime.idle();
    // The first read captured old facts. A fresh claim in this case must not
    // grant the old range even if its reply was already in flight.
    assert.equal(f.launches,0);
   }finally{release();h.stop();await h.runtime.idle();}
   const f2=fixture(),h2=shortcutFixture(f2,undefined,true);
   try{
    await flush();assert.equal(h2.getSharedPower()?.suspendSerial,1);
    h2.power={...h2.power,generation,suspendSerial:2,observedNotificationSequence:2,publicationRevision:2};
    assert(h2.submit('fresh',undefined,[generation,2,2]));await h2.runtime.idle();
    rows.push({generation,serial:h2.getSharedPower()?.suspendSerial,launches:f2.launches,feedback:h2.complete.map(value=>value.state)});
   }finally{h2.stop();}
  }
  assert.deepEqual(rows,[7,8].map(generation=>({generation,serial:2,launches:1,feedback:['applied']})));
 });
 await check('shortcut app: one ordered Ready retires old scoped work and preserves newer queued tickets',async()=>{
  const flush=()=>new Promise(resolve=>setImmediate(resolve)),f=fixture(),h=shortcutFixture(f,undefined,true),claims=[],sent=[];
  let release,oldSignal;
  const oldReply=new Promise(resolve=>release=resolve);
  h.onClaim=(id,options)=>{claims.push(id);if(id==='old'){oldSignal=options?.signal;return oldReply;}return {ok:true};};
  h.onComplete=id=>{sent.push(id);return {ok:true};};
  try{
   await flush();assert(h.submit('old',undefined,[7,1,1]));await flush();assert.deepEqual(claims,['old']);
   assert(h.submit('queued-old',undefined,[7,1,1]));assert(h.submit('next',undefined,[8,3,3]));
   h.publishPower({suspendSerial:2,observedNotificationSequence:2,publicationRevision:2});
   h.power={...h.power,generation:8,suspendSerial:3,observedNotificationSequence:3,publicationRevision:3};
   await flush();assert.equal(oldSignal?.aborted,true);assert.equal(f.launches,1);assert.deepEqual(sent,['next']);
   assert.equal(claims.includes('queued-old'),false);
   release({ok:true});await h.runtime.idle();assert.equal(f.launches,1);assert.deepEqual(sent,['next']);
   assert.equal(h.submit('next',undefined,[8,3,3]),false);
  }finally{release({ok:true});h.stop();await h.runtime.idle();}
 });
 await check('shortcut app: a scoped ticket cannot borrow a different or closed public scope',async()=>{
  const flush=()=>new Promise(resolve=>setImmediate(resolve));
  for(const [supplied,readback] of [
   [[7,1,2],{suspendSerial:2,observedNotificationSequence:2}],
   [[7,2,1],{suspendSerial:2,observedNotificationSequence:2}],
   [[7,2,2],{suspendSerial:2,observedNotificationSequence:2,inputReady:false}],
   [[7,2,2],{suspendSerial:2,observedNotificationSequence:3}],
  ]){
   const f=fixture(),h=shortcutFixture(f,undefined,true);
   try{
    await flush();h.power={...h.power,...readback,publicationRevision:2};
    assert(h.submit('mismatch',undefined,supplied));await h.runtime.idle();assert.equal(f.launches,0);
    assert.equal(h.complete.some(value=>value.state==='applied'),false);
    h.publishPower({inputReady:true,publicationRevision:3});
    assert(h.submit('current',undefined,[7,h.power.suspendSerial,h.power.observedNotificationSequence]));
    await h.runtime.idle();assert.equal(f.launches,1);assert.equal(h.complete.at(-1)?.state,'applied');
   }finally{h.stop();}
  }
 });
 await check('shortcut app: stale Native scope is rejected while a same-scope revision can proceed',async()=>{
  const f=fixture(),h=shortcutFixture(f,undefined,true);
  try{
   await new Promise(resolve=>setImmediate(resolve));
   h.publishPower({suspendSerial:2,observedNotificationSequence:2,publicationRevision:2});
   h.nativePower=()=>({suspendSerial:1,observedNotificationSequence:1,publicationRevision:1});
   assert(h.submit());await h.runtime.idle();assert.equal(f.launches,0);
   assert.equal(h.getSharedPower()?.suspendSerial,2);
   h.nativePower=()=>({publicationRevision:3});assert(h.submit('fresh'));await h.runtime.idle();
   assert.equal(f.launches,1);assert.equal(h.complete.at(-1)?.state,'applied');
   assert.equal(h.getSharedPower()?.publicationRevision,3);
  }finally{h.stop();}
 });
 await check('shortcut app: the shared action queue releases a stale fan admission read on notification advance',async()=>{
  const flush=()=>new Promise(resolve=>setImmediate(resolve));
  for(const at of [1,3,4,5]){
   const h=shortcutFixture(fixture());let release;h.holdPowerAt=at;h.holdPower=new Promise(r=>release=r);
   try{
    assert(h.submitFan('old-fan','soft'));await flush();assert.equal(h.powerReads,at);
    h.publishPower({publicationRevision:2});await flush();assert.equal(h.releases,0,'publication revision alone must keep the current read');
    h.holdPower=null;h.publishPower({publicationRevision:3,observedNotificationSequence:2});assert(h.submitFan('current-fan','aggressive'));await flush();
    assert.deepEqual(h.fanCommands,['aggressive'],`power read ${at} must release the queue before its reply`);
    assert.equal(h.powerSignals[at-1]?.aborted,true);assert.equal(h.releases,2);assert.equal(h.complete.length,1);
    assert.deepEqual(h.fanSaves,at>=4?['soft','aggressive']:['aggressive']);
    release();await flush();assert.deepEqual(h.fanCommands,['aggressive']);assert.equal(h.complete.length,1);
   }finally{release();h.stop();await h.runtime.idle();}
  }
 });
 await check('shortcut app: an issued fan write keeps the shared action queue across sleep',async()=>{
  const h=shortcutFixture(fixture());let release;h.holdFanWrite=new Promise(r=>release=r);
  const flush=()=>new Promise(resolve=>setImmediate(resolve));
  try{
   assert(h.submitFan('old-fan','soft'));await flush();assert.deepEqual(h.fanCommands,['soft']);
   h.publishPower({suspendSerial:2,observedNotificationSequence:2});assert(h.submitFan('current-fan','aggressive'));await flush();
   assert.deepEqual(h.fanCommands,['soft']);assert.equal(h.releases,0);assert.equal(h.complete.length,0);
   h.holdFanWrite=null;release();await h.runtime.idle();assert.deepEqual(h.fanCommands,['soft','aggressive']);assert.equal(h.releases,2);assert.equal(h.complete.length,1);
  }finally{release();h.stop();await h.runtime.idle();}
 });
 await check('shortcut app: merged Ready releases the old Task View state read before its reply',async()=>{
  const f=fixture(),h=shortcutFixture(f);let release;h.holdWindow=new Promise(r=>release=r);
  const flush=()=>new Promise(resolve=>setImmediate(resolve));
  try{
   assert(h.submitSwitch('old-switch'));await flush();
   assert.equal(h.taskViews,1);assert.equal(h.windowReads,1);assert.equal(h.releases,0);
   h.publishPower({publicationRevision:2});await flush();assert.equal(h.releases,0);
   h.publishPower({publicationRevision:3,suspendSerial:2,observedNotificationSequence:2});
   assert(h.submit('current-app'));await flush();
   assert.equal(f.launches,1,'retired window read blocked the current shortcut');
   assert.equal(h.windowSignal?.aborted,true);
   assert.equal(h.releases,2);assert.equal(h.complete.length,1);
   release();await h.runtime.idle();
   assert.equal(h.taskViews,1);assert.equal(h.minimizes,0);assert.equal(h.complete.length,1);
   h.holdWindow=null;assert(h.submitSwitch('current-switch'));await h.runtime.idle();
   assert.equal(h.taskViews,2);assert.equal(h.minimizes,1);assert.equal(h.complete.length,2);assert.equal(h.releases,3);
  }finally{release();h.stop();await h.runtime.idle();}
 });
 await check('shortcut app: another public Steam flight rejects overlap and permits a fresh request',async()=>{
  const f=fixture(),h=shortcutFixture(f);let release;f.hold=new Promise(r=>release=r);const first=f.launch({userInitiated:true});try{h.submit();await h.runtime.idle();assert.equal(f.launches,1);assert.equal(h.complete.length,1);assert.equal(h.complete[0].state,'failed');assert.equal(f.shells.length,0);f.hold=null;release();await first;h.submit('two');await h.runtime.idle();assert.equal(f.launches,2);assert.equal(h.complete.length,2);assert.equal(h.complete[1].state,'applied');assert.equal(h.releases,2);}finally{release();h.stop();}
 });
 await check('shortcut app: native rejection and lost acknowledgement do not replay or claim success',async()=>{
  const f=fixture(),h=shortcutFixture(f);try{f.failAck=true;h.submit();await h.runtime.idle();f.failAck=false;f.failInvoke=true;h.submit('two');await h.runtime.idle();assert.equal(h.complete.length,2);assert(h.complete.every(x=>x.state==='failed'));assert.equal(f.launches,2);assert.equal(f.shells.length,0);assert.equal(f.pending(),false);assert.equal(h.releases,2);}finally{h.stop();}
 });
}

const sha256 = text => crypto.createHash('sha256').update(text).digest('hex');
const result = { ok: failures.length === 0, executed: cases.length + failures.length, passed: cases.length, cases, failed: failures.length, failures,
  sourceHashes: { 'src/bridge/yeman.ts': sha256(source), 'src/views/SteamView.vue': sha256(view), 'src/bridge/steamReadBudget.ts': sha256(budgetSource), ...(quickSource ? {'src/views/QuickAppView.vue':sha256(quickSource)} : {}), ...(shortcutSources?Object.fromEntries(Object.entries(shortcutSources).map(([rel,text])=>[rel,sha256(text)])):{}) },
  scope: 'actual bridge and AST-extracted original Vue functions; deterministic timers; native admission checked separately',
  actualSteamActions: 0, actualBATChanges: 0 };
const output=process.argv.slice(2).find(v=>!v.startsWith('--'));
if (output) fs.writeFileSync(output, JSON.stringify(result, null, 2), {flag:'wx'});
console.log(JSON.stringify(result, null, 2));
if (!result.ok) process.exitCode = 1;
