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
const cases = [];
async function check(name, fn) { if(shortcutOnly&&!name.startsWith('shortcut app:'))return;if(shortcutRoutingOnly&&name!=='shortcut app: actual request routes selected Steam through the common owner and deduplicates')return; if(quickOnly && !name.startsWith('quick app:'))return; if(ordinaryOnly && name!=='selected addon: ordinary programs and opaque scripts retain the exact plain shell action')return; if(addonOnly && !name.startsWith('selected addon:'))return; await fn(); cases.push(name); }
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
 const runtimeRel='src/bridge/controllerShortcutRuntime.ts',actionsRel='src/bridge/controllerShortcutActions.ts';
 shortcutSources=Object.fromEntries([runtimeRel,actionsRel].map(rel=>[rel,fs.readFileSync(rel,'utf8')]));
 const moduleCode=(rel)=>{const ast=ts.createSourceFile(rel,shortcutSources[rel],ts.ScriptTarget.Latest,true,ts.ScriptKind.TS);return transformSync(ast.statements.filter(n=>!ts.isImportDeclaration(n)).map(n=>n.getText(ast)).join('\n'),{loader:'ts',format:'cjs'}).code;};
 const runtimeCode=moduleCode(runtimeRel),actionsCode=moduleCode(actionsRel);
 function shortcutFixture(f,path='X:/Steam/steam.exe'){
  const h={now:100000,apps:[{path,name:'Selected app'}],power:{generation:7,phase:'ready',hardwareWritesAllowed:true},events:new Map(),complete:[],claims:0,releases:0,holds:0,holdSettings:null};
  class Clock extends Date {static now(){return h.now;}}
  const a={exports:{}};vm.runInNewContext(actionsCode,{module:a,exports:a.exports,Error,Promise,Date:Clock,Map,Set});
  const module={exports:{}},shell={execute:async(file,args)=>{assert.equal(args.length,0);f.shells.push(file);return f.shellResult;}};
  vm.runInNewContext(runtimeCode,{module,exports:module.exports,Error,Promise,
   createControllerShortcutActions:a.exports.createControllerShortcutActions,launchSteamAddon:f.addon,
   shell,windowApi:{},powerLifecycle:{get:async()=>({...h.power})},
   on:(name,fn)=>{h.events.set(name,fn);return()=>h.events.delete(name);},onSharedPowerLifecycle:()=>()=>{},
   invoke:async(cmd,args)=>{if(cmd==='input.shortcutAction.claim'){h.claims++;return {ok:true};}assert.equal(cmd,'input.shortcutAction.complete');h.complete.push(args.outcome);return {ok:true};},
   readSettingsSection:async name=>{assert.equal(name,'quickApps');if(h.holdSettings)await h.holdSettings;return {apps:h.apps};},
   tryAcquireQuickAction:()=>{h.holds++;return()=>h.releases++;},
   createFanMirrorActions:()=>({stop(){}}),fanHostLifecycle:{},fanControlActive:{value:false},
   getFanSelectedPreset:()=>'',normalizeFanPresetCurves:()=>({}),saveFanPresetSelection:()=>{},setFanControlActive:()=>{},setFanNavigationDuty:()=>{},
   observeFanMirrorDisplay:()=>()=>{},isFanMirrorUiBusy:()=>false,recordFanHandshake:()=>{},fanFeatureEnabled:{value:false},
  });
  h.runtime=module.exports.startControllerShortcutRuntime();
  h.submit=(id='one',appPath=path)=>h.events.get('input.shortcutAction')({schemaVersion:1,id,actionId:'app.launchQuickApp',params:{appPath},issuedAtUnixMs:h.now,expiresAtUnixMs:h.now+30000,powerGeneration:7,inputRevision:3});
  h.invalidate=()=>h.events.get('power.pending')({});h.stop=()=>h.runtime.stop();return h;
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
  const f=fixture(),h=shortcutFixture(f);let release;h.holdSettings=new Promise(r=>release=r);try{h.submit();for(let i=0;i<20;i++)await Promise.resolve();h.invalidate();release();await h.runtime.idle();assert.equal(f.launches+f.shells.length,0);assert.equal(h.complete.length,0);assert.equal(h.releases,1);assert.equal(f.pending(),false);}finally{h.stop();}
 });
 await check('shortcut app: another public Steam flight rejects overlap and permits a fresh request',async()=>{
  const f=fixture(),h=shortcutFixture(f);let release;f.hold=new Promise(r=>release=r);const first=f.launch({userInitiated:true});try{h.submit();await h.runtime.idle();assert.equal(f.launches,1);assert.equal(h.complete.length,1);assert.equal(h.complete[0].state,'failed');assert.equal(f.shells.length,0);f.hold=null;release();await first;h.submit('two');await h.runtime.idle();assert.equal(f.launches,2);assert.equal(h.complete.length,2);assert.equal(h.complete[1].state,'applied');assert.equal(h.releases,2);}finally{release();h.stop();}
 });
 await check('shortcut app: native rejection and lost acknowledgement do not replay or claim success',async()=>{
  const f=fixture(),h=shortcutFixture(f);try{f.failAck=true;h.submit();await h.runtime.idle();f.failAck=false;f.failInvoke=true;h.submit('two');await h.runtime.idle();assert.equal(h.complete.length,2);assert(h.complete.every(x=>x.state==='failed'));assert.equal(f.launches,2);assert.equal(f.shells.length,0);assert.equal(f.pending(),false);assert.equal(h.releases,2);}finally{h.stop();}
 });
}

const sha256 = text => crypto.createHash('sha256').update(text).digest('hex');
const result = { ok: true, passed: cases.length, cases,
  sourceHashes: { 'src/bridge/yeman.ts': sha256(source), 'src/views/SteamView.vue': sha256(view), 'src/bridge/steamReadBudget.ts': sha256(budgetSource), ...(quickSource ? {'src/views/QuickAppView.vue':sha256(quickSource)} : {}), ...(shortcutSources?Object.fromEntries(Object.entries(shortcutSources).map(([rel,text])=>[rel,sha256(text)])):{}) },
  scope: 'actual bridge and AST-extracted original Vue functions; deterministic timers; native admission checked separately',
  actualSteamActions: 0, actualBATChanges: 0 };
const output=process.argv.slice(2).find(v=>!v.startsWith('--'));
if (output) fs.writeFileSync(output, JSON.stringify(result, null, 2), {flag:'wx'});
console.log(JSON.stringify(result, null, 2));
