// Isolated browser test: real Vue menu + extracted production semantic Y/A.
// chrome.webview below is an IN-MEMORY fixture, never an installed native bridge.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';
import { createServer } from 'node:http';
import { createHash } from 'node:crypto';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const require = createRequire(path.join(root, 'package.json'));
const { build } = require('esbuild'), ts = require('typescript');
const { parse, compileScript, compileStyle } = require('vue/compiler-sfc');
const runtime = process.env.YMCC_UI_BROWSER_MODULE_DIR || 'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const browserRequire = createRequire(path.join(runtime, '_task-view-menu.cjs'));
const { chromium } = browserRequire('playwright');
const evidenceRoot = path.resolve(root, '../../../Build/Validation/TaskView-Api-20261015/menu-browser');
const baseline = path.join(evidenceRoot, 'baseline-before-focus-fix');
const out = path.join(evidenceRoot, 'focus-fix-' + new Date().toISOString().replaceAll(':', '-').replaceAll('.', '-'));
fs.mkdirSync(out, { recursive: true });
const sha = value => createHash('sha256').update(value).digest('hex');
const sources = new Map();
function read(file) {
  const absolute = path.resolve(root, file);
  if (!sources.has(absolute)) sources.set(absolute, fs.readFileSync(absolute));
  return sources.get(absolute).toString('utf8');
}
// Import only mock dictionary expressions; do not execute either existing suite.
const shared = read('tools/independent_frame_rate_ui_selftest.mjs');
const start = shared.indexOf('const mocks={'), end = shared.indexOf('const result=', start);
assert(start >= 0 && end > start);
const mocks = vm.runInNewContext(`(()=>{${shared.slice(start, end)}return mocks})()`);
const top = read('tools/top_menu_power_group_browser_selftest.mjs');
const a = top.indexOf('Object.assign(mocks,{'), b = top.indexOf('const entry=', a);
assert(a >= 0 && b > a);
vm.runInNewContext(top.slice(a, b), { mocks, focusCode: '', backCode: '' });
mocks['@/bridge/yeman'] += 'export const getTdpExecutionCapability=async()=>null;export const formatTdpTarget=value=>String(value)+" W";';
// These newer menu dependencies are outside the TaskView action under test.
// Fail if reached; their imports must not prevent this isolated menu from building.
mocks['@/bridge/gameQuickSession'] += 'export const captureAndLockGameTarget=()=>{throw Error("Unexpected trainer action in TaskView test")};';
mocks['@/bridge/gameTrainer'] += 'export const openCheatEngine=()=>{throw Error("Unexpected Cheat Engine action in TaskView test")};';

const engine = read('src/gamepad/engine.ts');
const ast = ts.createSourceFile('engine.ts', engine, ts.ScriptTarget.Latest, true, ts.ScriptKind.TS);
const functions = new Map(ast.statements.filter(ts.isFunctionDeclaration).map(node => [node.name?.text, node]));
const activate = functions.has('activateCurrent') ? 'activateCurrent' : 'activate';
const extractedNames = ['focusables', 'focusFirst', 'isRangeInput', activate, 'handleGamepadEditGame'];
const extracted = extractedNames.map(name => { assert(functions.has(name), `Missing production ${name}`); return functions.get(name).getText(ast); }).join('\n');
const focusable = ast.statements.find(node => ts.isVariableStatement(node) && node.declarationList.declarations.some(d => d.name.getText(ast) === 'FOCUSABLE'));
assert(focusable, 'Missing production FOCUSABLE selector');
Object.assign(mocks, {
  '@/gamepad/engine': `import {focusGamepadElement,setGamepadFocused} from './src/gamepad/focus.ts';
    import {withGamepadActivation} from './src/gamepad/activationEvidence.ts';
    ${focusable.getText(ast)} let sliderEditMode=false;const log=()=>{};
    const commitSlider=()=>{throw Error('Unexpected slider activation in TaskView test')};
    ${extracted}
    export const isMouseModeSuppressed=()=>false;
    export const testControllerY=handleGamepadEditGame,testActivateCurrent=${activate};`,
  '@/bridge/ipc': `export * from './src/bridge/ipc.ts';`,
  '@/gamepad/focus': `import {focusGamepadElement as actualFocus} from './src/gamepad/focus.ts';
    export * from './src/gamepad/focus.ts';
    export function focusGamepadElement(el){window.__fixture.trace('focus-helper',{control:el?.getAttribute('data-gp-game-control'),connected:!!el?.isConnected});return actualFocus(el)};`,
  '@/bridge/api': `import {shell as realShell,windowApi as realWindow} from './src/bridge/api.ts';
    const unexpected=()=>{throw Error('Unexpected non-TaskView API')};
    const observe=(name,evidence,run)=>{const f=window.__fixture;f.trace('api-call',{name,evidence:structuredClone(evidence)});
      return run().then(value=>{f.trace('api-resolve',{name,value});return value},error=>{f.trace('api-reject',{name,error:error.message});throw error})};
    export const dialog={openFile:unexpected,confirm:unexpected},keyboard={open:unexpected},proc={kill:unexpected};
    export const shell={open:unexpected,taskViewWithEvidence:e=>observe('shell.taskViewWithEvidence',e,()=>realShell.taskViewWithEvidence(e))};
    export const windowApi={minimizeWithEvidence:e=>observe('windowApi.minimizeWithEvidence',e,()=>realWindow.minimizeWithEvidence(e))};`,
  '@/bridge/gamedetect': `const f=()=>window.__fixture;
    export const detectedGameName=game=>game?.name||'',detectGame=async()=>f().game;
    export const refreshGameStatus=async()=>f().game,refreshGameStatusStrict=async()=>f().game;
    export function subscribeGameStatus(cb){f().gameSubscriptions++;cb(f().game);return()=>{f().gameSubscriptions--}};`,
  '@/bridge/gameQuickSession': `${mocks['@/bridge/gameQuickSession']}
    export const isGameTargetSame=(a,b)=>!!a&&!!b&&a.pid===b.pid&&a.processCreated===b.processCreated;`,
});

// Runs before any product module: no native bridge, OS input or hardware query.
function memoryFixture() {
  const f = window.__fixture = {
    game: { pid: 101, processCreated: '1001', path: 'R:/memory/a.exe', name: 'Fixture game' },
    side: 'ac', events: {}, global: {}, custom: { version: 1, entries: {}, rtss: {} },
    schedule: { profiles: { ac: {}, dc: {} }, active: {} }, globalWrites: [], customWrites: [],
    performanceWrites: [], applied: [], coreWrites: 0, joyCloses: 0, ruleWrites: 0,
    gameSubscriptions: 0, taskMode: 'hold', minimizeMode: 'hold', requests: [], traceRows: [], hardwareQueries: [],
  };
  f.trace = (kind, data = {}) => f.traceRows.push({ kind, at: performance.now(), ...data });
  const receivers = new Set();
  f.deliver = data => { for (const cb of receivers) cb({ data }); };
  f.replyId = (id, value = true, error) => {
    const request = f.requests.find(r => r.id === id); if (!request) throw Error('Unknown fixture request');
    request.responded = true; f.trace('ipc-reply', { id, cmd: request.cmd, value, error });
    f.deliver(error ? { id, error } : { id, result: value });
  };
  f.reply = (cmd, value = true, error) => {
    const request = f.requests.findLast(r => r.cmd === cmd && !r.responded);
    if (!request) throw Error('No pending fixture request: ' + cmd);
    f.replyId(request.id, value, error);
  };
  window.chrome ||= {};
  Object.defineProperty(window.chrome, 'webview', { value: {
    addEventListener(type, cb) { if (type !== 'message') throw Error('Unexpected event'); receivers.add(cb); },
    postMessage(message) {
      const r = structuredClone(message); r.at = performance.now(); f.requests.push(r); f.trace('ipc-request', r);
      if (!['shell.taskView', 'window.minimize'].includes(r.cmd)) throw Error('Unexpected native IPC: ' + r.cmd);
      const mode = r.cmd === 'shell.taskView' ? f.taskMode : f.minimizeMode;
      if (mode === 'accept' || mode === 'false' || mode === 'reject') queueMicrotask(() =>
        f.replyId(r.id, mode !== 'false', mode === 'reject' ? 'fixture API rejected: ' + r.cmd : undefined));
      // hold/timeout deliberately withhold the reply; production invoke owns its timer.
    },
  }, configurable: false });
  const denied = name => function () { f.hardwareQueries.push(name); throw Error('Hardware query forbidden: ' + name); };
  Object.defineProperty(navigator, 'getGamepads', { value: denied('getGamepads') });
  Object.defineProperty(navigator, 'webkitGetGamepads', { value: denied('webkitGetGamepads') });
  for (const name of ['hid', 'usb', 'serial', 'bluetooth', 'gpu', 'xr', 'mediaDevices']) {
    Object.defineProperty(navigator, name, { get: denied(name), configurable: true });
  }
}
const styles = [];
const entry = `import {createApp} from 'vue';import Menu from './src/components/GameRecognitionControl.vue';
  import {testControllerY,testActivateCurrent} from '@/gamepad/engine';
  const app=createApp(Menu);window.__semanticY=()=>testControllerY({});window.__semanticA=()=>testActivateCurrent({});
  window.__unmount=()=>app.unmount();app.mount('#app');window.__ready=true;`;
const result = await build({ stdin: { contents: entry, resolveDir: root, loader: 'ts' }, bundle: true,
  write: false, format: 'iife', platform: 'browser', metafile: true, alias: { '@': path.join(root, 'src') },
  define: { 'process.env.NODE_ENV': '"production"' }, plugins: [{ name: 'task-view-memory-fixture', setup(builder) {
    builder.onResolve({ filter: /.*/ }, args => args.path in mocks ? { path: args.path, namespace: 'fixture' } : undefined);
    builder.onLoad({ filter: /.*/, namespace: 'fixture' }, args => ({ contents: mocks[args.path], loader: 'ts', resolveDir: root }));
    builder.onLoad({ filter: /\.vue$/ }, args => {
      const { descriptor, errors } = parse(read(args.path), { filename: args.path }); assert.deepEqual(errors, []);
      const id = 'data-v-' + sha(args.path).slice(0, 8), script = compileScript(descriptor, { id, inlineTemplate: true });
      for (const style of descriptor.styles) {
        const compiled = compileStyle({ source: style.content, filename: args.path, id, scoped: style.scoped });
        assert.deepEqual(compiled.errors, []); styles.push(compiled.code);
      }
      return { contents: script.content.replace('export default', 'const _component=') + `\n_component.__scopeId=${JSON.stringify(id)};export default _component;`, loader: 'ts', resolveDir: path.dirname(args.path) };
    });
    builder.onLoad({ filter: /[\\/]src[\\/].*\.(ts|js)$/ }, args => ({ contents: read(args.path), loader: args.path.endsWith('.ts') ? 'ts' : 'js', resolveDir: path.dirname(args.path) }));
  } }] });
const html = '<!doctype html><meta charset="utf-8"><title>TaskView isolated semantic menu test</title><style>' +
  read('src/styles/tokens.css') + '\n' + styles.join('\n') +
  '\nbody{margin:0;padding:20px;background:var(--bg-solid);color:var(--text);font-family:system-ui}#app{max-width:700px}</style>' +
  '<button id="outside">Fixture background sentinel</button><main id="app"></main><script>(' + memoryFixture.toString() +
  ')()</script><script src="/fixture.js"></script>';
fs.writeFileSync(path.join(out, 'fixture.js'), result.outputFiles[0].text);
fs.writeFileSync(path.join(out, 'fixture.html'), html);
const server = createServer((req, res) => {
  if (req.url === '/fixture.js') { res.setHeader('Content-Type', 'text/javascript'); res.end(result.outputFiles[0].text); }
  else if (req.url === '/') { res.setHeader('Content-Type', 'text/html; charset=utf-8'); res.end(html); }
  else { res.writeHead(404); res.end(); }
});
const cases = [], errors = [], blockedRequests = [], focus = [], images = [], boundaryResults = [], skipped = [];
const startedAt = new Date().toISOString();
let browser, page, failure, browserVersion;
const check = (name, detail = {}) => cases.push({ name, ...detail });
try {
  await new Promise((resolve, reject) => { server.once('error', reject); server.listen(0, '127.0.0.1', resolve); });
  const origin = 'http://127.0.0.1:' + server.address().port;
  const edge = 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe';
  browser = await chromium.launch({ headless: true, ...(process.env.YMCC_UI_BROWSER_EXECUTABLE ? { executablePath: process.env.YMCC_UI_BROWSER_EXECUTABLE } : fs.existsSync(edge) ? { executablePath: edge } : {}) });
  browserVersion = browser.version();
  const context = await browser.newContext({ viewport: { width: 980, height: 1160 }, serviceWorkers: 'block' });
  await context.route('**/*', route => {
    if ([origin + '/', origin + '/fixture.js', origin + '/favicon.ico'].includes(route.request().url())) return route.continue();
    blockedRequests.push(route.request().url()); return route.abort();
  });
  page = await context.newPage(); page.on('pageerror', error => errors.push(error.message));
  page.on('console', message => { if (message.type() === 'error' && !message.text().includes('404')) errors.push(message.text()); });
  await page.goto(origin); await page.bringToFront(); await page.waitForFunction(() => window.__ready);
  page.setDefaultTimeout(2500);
  const button = page.locator('[data-gp-game-control="switch-program"]');
  const counts = () => page.evaluate(() => ({ task: window.__fixture.requests.filter(r => r.cmd === 'shell.taskView').length, min: window.__fixture.requests.filter(r => r.cmd === 'window.minimize').length }));
  const observeFocus = async stage => focus.push(await page.evaluate(stage => ({ stage, tag: document.activeElement?.tagName, control: document.activeElement?.getAttribute('data-gp-game-control'), visualFocus: document.querySelector('[data-gp-game-control="switch-program"]')?.classList.contains('focused'), hasFocus: document.hasFocus(), visible: document.visibilityState }), stage));
  const screenshot = async name => { const file = path.join(out, name + '.png'); await page.screenshot({ path: file, fullPage: true }); images.push(file); };
  const mode = (task, min = 'accept') => page.evaluate(({ task, min }) => { window.__fixture.taskMode = task; window.__fixture.minimizeMode = min; }, { task, min });
  const reply = (cmd, error) => page.evaluate(({cmd,error}) => window.__fixture.reply(cmd, true, error), {cmd,error});
  const frames = () => page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
  const settle = async text => { await page.waitForFunction(() => document.querySelector('[data-gp-game-control="switch-program"]')?.disabled === false, {}, {timeout:1500}); if (text) await page.locator('.game-recognition-message').filter({ hasText: text }).waitFor(); await frames(); };
  const semanticA = () => page.evaluate(() => window.__semanticA());
  const helperCount = () => page.evaluate(() => window.__fixture.traceRows.filter(r => r.kind === 'focus-helper' && r.control === 'switch-program').length);
  async function requireFocus(stage) {
    await observeFocus(stage); const state=focus.at(-1);
    assert.equal(state.hasFocus,true,stage+': fixture document must really own focus'); assert.equal(state.visible,'visible');
    assert.equal(state.control,'switch-program',stage+': no warm-up A or forced focus allowed'); assert.equal(state.visualFocus,true);
  }
  async function once(stage) {
    await requireFocus(stage); const before=await counts(); await semanticA();
    assert.equal((await counts()).task,before.task+1,stage+': one A must issue the request immediately');
    return {before,request:await page.evaluate(()=>window.__fixture.requests.findLast(r=>r.cmd==='shell.taskView'))};
  }
  async function freshMenu() {
    if(await button.count())await page.getByRole('button',{name:/关闭页面/}).click();
    await page.evaluate(()=>window.__semanticY()); await button.waitFor(); await frames(); await requireFocus('fresh-Y');
  }
  assert.equal(await button.count(), 0); await page.locator('#outside').focus();
  await freshMenu(); assert.equal((await button.innerText()).trim(),'切换程序'); assert.deepEqual(await counts(),{task:0,min:0});
  await screenshot('semantic-Y-focus'); check('Semantic Y opens real Vue menu and gives 切换程序 DOM/visual focus without IPC');
  await page.evaluate(()=>{window.__semanticA();window.__semanticA();const el=document.querySelector('[data-gp-game-control="switch-program"]');for(let i=0;i<12;i++)el.dispatchEvent(new MouseEvent('click',{bubbles:true}));});
  await frames(); assert.deepEqual(await counts(),{task:1,min:0}); assert.equal(await button.isDisabled(),true); await observeFocus('task-busy');
  await reply('shell.taskView'); await page.waitForFunction(()=>window.__fixture.requests.some(r=>r.cmd==='window.minimize'));
  await button.dispatchEvent('click'); assert.deepEqual(await counts(),{task:1,min:1});
  await observeFocus('before-successful-handover'); assert.equal(focus.at(-1).tag,'BODY'); assert.equal(focus.at(-1).visualFocus,true);
  const successHelpers=await helperCount(); await reply('window.minimize'); await settle('已请求切换程序'); await observeFocus('successful-handover');
  assert.equal(await helperCount(),successHelpers,'Successful handover must not call the controller focus helper');
  assert.notEqual(focus.at(-1).control,'switch-program','Success must not reclaim the switch button');
  const trace=await page.evaluate(()=>window.__fixture.traceRows),calls=trace.filter(r=>r.kind==='api-call');
  assert.deepEqual(calls.map(r=>r.name),['shell.taskViewWithEvidence','windowApi.minimizeWithEvidence']);
  assert.deepEqual(calls[0].evidence,calls[1].evidence); assert.equal(calls[0].evidence.diagnosticAction,'game-recognition-switch-program');
  assert.equal(calls[0].evidence.diagnosticActivation,'gamepad-engine');assert.equal(calls[0].evidence.diagnosticEventTrusted,false);
  assert.match(calls[0].evidence.diagnosticActionId,/^switch-program:/);assert(Number.isFinite(calls[0].evidence.requestedAtUnixMs));
  assert(trace.findIndex(r=>r.kind==='api-resolve'&&r.name==='shell.taskViewWithEvidence')<trace.findIndex(r=>r.kind==='api-call'&&r.name==='windowApi.minimizeWithEvidence'));
  check('A calls TaskView once before minimize with identical evidence; same-turn A/click burst and pending minimize click do not duplicate');
  check('Accepted TaskView plus true minimize never restores controller focus even when DOM focus is body and visual highlight remains');
  await freshMenu(); await mode('accept');const mouseBefore=await counts();await button.click();await settle('已请求切换程序');
  assert.deepEqual(await counts(),{task:mouseBefore.task+1,min:mouseBefore.min+1});
  const mouseEvidence=await page.evaluate(()=>window.__fixture.requests.findLast(r=>r.cmd==='shell.taskView').args);assert.equal(mouseEvidence.diagnosticActivation,'trusted-mouse');assert.equal(mouseEvidence.diagnosticEventTrusted,true);
  check('Mouse uses the same evidence API path once; real mouse origin differs from semantic controller');

  await freshMenu();await mode('hold');const rejected=await once('before-held-rejection');await frames();assert.equal(await button.isDisabled(),true);
  await reply('shell.taskView','本次程序切换未取得前台授权，已保留 YMCC，请稍后重试');await settle('切换 Windows 任务视图失败：本次程序切换未取得前台授权，已保留 YMCC，请稍后重试');
  assert.deepEqual(await counts(),{task:rejected.before.task+1,min:rejected.before.min});await requireFocus('after-rejection');
  await mode('accept');await once('one-A-after-rejection');await settle('已请求切换程序');
  check('Held TaskView rejection restores focus after busy; exactly one subsequent semantic A requests again');

  await freshMenu();await mode('timeout');const timedOut=await once('before-timeout');
  await page.locator('.game-recognition-message').filter({hasText:'IPC command timed out: shell.taskView'}).waitFor({timeout:9000});await settle();
  const elapsed=await page.evaluate(at=>performance.now()-at,timedOut.request.at);assert(elapsed>=5900&&elapsed<9000);
  assert.deepEqual(await counts(),{task:timedOut.before.task+1,min:timedOut.before.min});await requireFocus('after-timeout');await screenshot('task-timeout-one-A-retry');
  await mode('hold');await once('one-A-after-timeout');await page.evaluate(id=>window.__fixture.replyId(id),timedOut.request.id);await frames();
  assert.equal(await button.isDisabled(),true);assert.deepEqual(await counts(),{task:timedOut.before.task+2,min:timedOut.before.min});
  await reply('shell.taskView');await settle('已请求切换程序');
  assert.deepEqual(await counts(),{task:timedOut.before.task+2,min:timedOut.before.min+1});
  check('Real 6000ms timeout restores focus; one A retries; stale old reply cannot finish or minimize the newer request',{observedElapsedMs:Math.round(elapsed)});
  for(const min of ['false','reject','timeout']) {
    await freshMenu();await mode('accept',min);const failed=await once('before-minimize-'+min);
    const detail=min==='false'?'窗口未接受收起请求':min==='reject'?'fixture API rejected: window.minimize':'IPC command timed out: window.minimize';
    await page.locator('.game-recognition-message').filter({hasText:'已请求切换程序，但 YMCC 未能收起：'+detail}).waitFor({timeout:4000});await settle();
    assert.deepEqual(await counts(),{task:failed.before.task+1,min:failed.before.min+1});await requireFocus('after-minimize-'+min);
    if(min==='timeout')await screenshot('minimize-timeout-one-A-retry');
    await mode('accept');await once('one-A-after-minimize-'+min);await settle('已请求切换程序');
    check('Minimize '+min+' restores eligible focus; exactly one A retries without warm-up or forced focus');
  }

  // Boundary input deliberately selects a different real control; retries above never force focus.
  await freshMenu();await mode('hold');await once('before-other-control');await frames();
  const cancel=page.getByRole('button',{name:/关闭页面/});await cancel.focus();const otherHelpers=await helperCount();
  assert.equal(await button.evaluate(el=>el.classList.contains('focused')),true,'Keep old visual highlight so activeElement guard is exercised');
  await reply('shell.taskView','fixture boundary rejection');await settle('fixture boundary rejection');
  assert.equal(await cancel.evaluate(el=>el===document.activeElement),true);assert.equal(await helperCount(),otherHelpers);
  await observeFocus('other-control-preserved');boundaryResults.push({name:'other-control',passed:true});check('Failure does not steal focus from a newly selected real close control');

  await freshMenu();await mode('hold');await once('before-close-menu');await frames();
  await page.evaluate(()=>{window.__oldSwitch=document.querySelector('[data-gp-game-control="switch-program"]')});
  await page.getByRole('button',{name:/关闭页面/}).click();await button.waitFor({state:'detached'});const closedHelpers=await helperCount();
  await reply('shell.taskView','fixture closed-menu rejection');await frames();
  assert.equal(await button.count(),0);assert.equal(await page.evaluate(()=>window.__oldSwitch.isConnected),false);assert.equal(await helperCount(),closedHelpers);
  boundaryResults.push({name:'menu-closed',passed:true});check('A pending failure never reopens the menu or focuses its disconnected old switch button');

  await freshMenu();await mode('hold');await once('before-visual-focus-revoked');await frames();
  await button.evaluate(el=>el.classList.remove('focused'));const revokedHelpers=await helperCount();
  await reply('shell.taskView','fixture revoked-highlight rejection');await settle('fixture revoked-highlight rejection');
  assert.equal(await helperCount(),revokedHelpers);assert.notEqual(await button.evaluate(el=>el===document.activeElement),true);
  boundaryResults.push({name:'visual-highlight-revoked',passed:true});check('Failure does not restore a button whose controller highlight was revoked while pending');

  await freshMenu();await mode('hold');await once('before-document-blur');await frames();
  const secondary=await context.newPage();await secondary.goto('about:blank');await secondary.bringToFront();
  const unfocused=await page.evaluate(()=>({hasFocus:document.hasFocus(),visibility:document.visibilityState}));
  const blurHelpers=await helperCount();await reply('shell.taskView','fixture unfocused-document rejection');
  // RAF is throttled in real background tabs; evaluate a short browser timer instead.
  await page.evaluate(()=>new Promise(resolve=>setTimeout(resolve,80)));
  if(!unfocused.hasFocus) {
    assert.equal(await page.evaluate(()=>document.hasFocus()),false);assert.equal(await helperCount(),blurHelpers);
    assert.notEqual(await button.evaluate(el=>el===document.activeElement),true);
    boundaryResults.push({name:'document-unfocused',passed:true,state:unfocused});check('Real background document does not regain switch-button focus on failure');
  } else skipped.push({name:'document-unfocused',reason:'Browser keeps document.hasFocus true across headless tabs; no spoofed property used',state:unfocused});
  await secondary.close();await page.bringToFront();await freshMenu();
  const allCalls = await page.evaluate(() => window.__fixture.traceRows.filter(r => r.kind === 'api-call'));
  const taskIds = allCalls.filter(r => r.name === 'shell.taskViewWithEvidence').map(r => r.evidence.diagnosticActionId);
  assert.equal(new Set(taskIds).size, taskIds.length);
  for (const call of allCalls.filter(r => r.name === 'windowApi.minimizeWithEvidence')) assert(taskIds.includes(call.evidence.diagnosticActionId));
  const unmountBefore = await counts(); await page.evaluate(() => { window.__unmount(); window.__semanticY(); });
  assert.equal(await button.count(), 0); assert.deepEqual(await counts(), unmountBefore);
  assert.equal(await page.evaluate(() => window.__fixture.gameSubscriptions), 0);
  assert.deepEqual(await page.evaluate(() => window.__fixture.hardwareQueries), []);
  assert.equal(await page.evaluate(() => window.__fixture.requests.every(r => ['shell.taskView', 'window.minimize'].includes(r.cmd))), true);
  assert.deepEqual(blockedRequests, []); assert.deepEqual(errors, []);
  check('Correlation IDs are unique across requests; unmount removes menu subscription and Y listener');
  check('No browser errors, hardware queries, unexpected native IPC or external network requests');
} catch (error) {
  failure = error.stack || String(error); if (page) { try { await page.screenshot({ path: path.join(out, 'failure.png'), fullPage: true }); } catch {} }
} finally {
  let fixture;
  if (page && !page.isClosed()) fixture = await page.evaluate(() => ({ requests: window.__fixture?.requests, trace: window.__fixture?.traceRows, hardwareQueries: window.__fixture?.hardwareQueries })).catch(() => null);
  if (browser) await browser.close(); if (server.listening) await new Promise(resolve => server.close(resolve));
  const sourceHashes = [...sources].map(([file, bytes]) => ({ path: path.relative(root, file).replaceAll('\\', '/'), sha256: sha(bytes) }));
  sourceHashes.push({ path: 'tools/task_view_menu_browser_selftest.mjs', sha256: sha(fs.readFileSync(fileURLToPath(import.meta.url))) });
  const report = { suite: 'TaskView real Vue menu / simulated semantic gamepad', ok: !failure, passed: cases.length, cases, focus, images, errors, blockedRequests, failure,
    scope: 'Real GameRecognitionControl/GameQuickActions + production Y/activate/focus + production API/IPC deadlines. All native IPC goes to in-memory chrome.webview. No physical MSI/ROG/controller, actual Shell API/helper, foreground handover or consumer acceptance tested.',
    startedAt, finishedAt: new Date().toISOString(), browserVersion, nodeVersion: process.version, playwrightVersion: browserRequire('playwright/package.json').version,
    engineFunctions: extractedNames, sourceHashes, mockModules: Object.keys(mocks), fixture, serverClosed: !server.listening, browserClosed: !browser?.isConnected(),
    boundaryResults, skipped, baseline: {directory:baseline,resultsSha256:sha(fs.readFileSync(path.join(baseline,'results.json'))),toolSha256:sha(fs.readFileSync(path.join(baseline,'task_view_menu_browser_selftest.mjs')))},
    findings: focus.filter(f => /^after-(timeout|rejection|minimize-)/.test(f.stage) && f.control !== 'switch-program').map(f => ({ kind:'focus-not-restored',...f })),
    hashes: { fixtureJs: sha(result.outputFiles[0].text), fixtureHtml: sha(html) } };
  fs.writeFileSync(path.join(out, 'results.json'), JSON.stringify(report, null, 2) + '\n');
  const toolHash = sourceHashes.find(s => s.path === 'tools/task_view_menu_browser_selftest.mjs').sha256;
  const summary = [
    '**TaskView 局部焦点修复 — 隔离浏览器验证（2026-10-15）**','',
    '运行：node tools/task_view_menu_browser_selftest.mjs；结果 '+(report.ok?'PASS':'FAIL')+'，'+cases.length+' 项检查通过。','',
    '旧 run 完整保留于 ../baseline-before-focus-fix，包括旧工具、results、报告、截图与证据哈希；其中失败后第一次 A 仅选中、第二次 A 才请求的旧现象未覆盖。本轮使用独立时间戳输出目录。','',
    '真实 GameRecognitionControl + GameQuickActions、生产 handleGamepadEditGame/'+activate+'/focus、真实 API/IPC 6000/1500ms 定时器，native 传输完全内存 mock。focusGamepadElement 仅透明记录后调用原实现，不修改产品源码。','',
    '失败后断言：TaskView 拒绝、长 6000ms 超时，minimize false/reject/1500ms 超时，解除 busy 后 DOM/视觉焦点须自动回到原 switch-program；下一次语义 A 必须立即增加一次请求。没有预热 A 或强制 focus。迟到旧回执不影响重试。','',
    '边界：已选另一真实 control、菜单关闭并卸载原 button、原视觉高亮撤销时均不得调用该 button 的 focus helper。实际文档失焦边界结果见 boundaryResults；若 headless 不能产生真实失焦则明确记 skipped，不伪造 hasFocus。API 接受且 minimize=true 时即使焦点落 body 且高亮保留，也不得主动重聚焦。','',
    '已通过边界：'+boundaryResults.map(x=>x.name).join('、')+'；跳过：'+JSON.stringify(skipped)+'。','',
    '仍覆盖 Y 默认 DOM/视觉焦点、一次 A/鼠标请求、诊断参数关联、busy 重复点击抑制、成功/失败文案及卸载清理。成功仅证明 UI/模拟回执行为，不证明实体 MSI/ROG、系统 Shell helper、最终前台或消费端验收。','',
    '浏览器错误/硬件查询/未知 IPC/外网请求均须为零；不运行产品、不注入 OS 输入、不改设置。浏览器与 localhost server 已清理。','',
    '详见 results.json、截图和 SHA256SUMS.txt；源码快照 SHA 在 results.json.sourceHashes。本工具 SHA256：'+toolHash+'。',
    failure?'\n失败：'+failure:'',
  ].join('\n');
  fs.writeFileSync(path.join(out, 'REPORT.md'), summary + '\n');
  const artifactNames = ['results.json', 'REPORT.md', 'fixture.js', 'fixture.html', ...images.map(f => path.basename(f))];
  fs.writeFileSync(path.join(out, 'SHA256SUMS.txt'), artifactNames.map(name => sha(fs.readFileSync(path.join(out, name))) + '  ' + name).join('\n') + '\n');
  console.log(JSON.stringify({ ok: report.ok, passed: report.passed, cases, focus, findings: report.findings, failure, scriptSha256: toolHash, evidence: out }, null, 2));
}
if (failure) process.exitCode = 1;
