import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { createRequire } from 'node:module';
import { createServer } from 'node:http';
import { createHash, randomUUID } from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { calibrationToolchainPreamble } from './fixtures/hc_reviewed_calibration/toolchain.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const out = path.resolve(process.argv[2] || path.join(root, '../../../_scratch/hidhide-setup-prompt-20261013'));
fs.mkdirSync(out, { recursive: true });
const req = createRequire(path.join(root, 'package.json'));
const { build, transformSync } = req('esbuild');
const { parse, compileScript, compileStyle } = req('vue/compiler-sfc');
const runtime = process.env.YMCC_UI_BROWSER_MODULE_DIR || 'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const { chromium } = createRequire(path.join(runtime, '_hidhide-ui-selftest.cjs'))('playwright');
const cases = [], screenshots = [], errors = [];

const module = { exports: {} };
vm.runInNewContext(transformSync(fs.readFileSync(path.join(root, 'src/bridge/hidHideSetupPrompt.ts'), 'utf8'), { loader: 'ts', format: 'cjs' }).code,
  { module, exports: module.exports });
const policy = module.exports;
assert.equal(policy.DEFAULT_GAMEPAD_REDIST_DIRECTORY, String.raw`C:\SOFT\YeMan\PowerControl\redist`);
for (const driverSetup of [0, 1, 2, 4, 5, 8, 9]) assert.equal(policy.hidHideManualInstallPrompt({ driverSetup, hidHideSetupNotice: 1 }), null);
for (const hidHideSetupNotice of [0, 7, 8]) assert.equal(policy.hidHideManualInstallPrompt({ driverSetup: 3, hidHideSetupNotice }), null);
for (const hidHideSetupNotice of [1, 2, 3, 4, 5, 6]) assert(policy.hidHideManualInstallPrompt({ driverSetup: 3, hidHideSetupNotice }));
cases.push('native result distinctions preserved by UI policy');
const gate = policy.createHidHideSetupPromptGate();
gate.update({ driverSetup: 3, hidHideSetupNotice: 2 });
assert.equal(gate.take(false, true), null); assert.equal(gate.take(true, false), null);
assert(gate.take(true, true)); assert.equal(gate.take(true, true), null);
gate.update({ driverSetup: 3, hidHideSetupNotice: 3 }); assert.equal(gate.take(true, true), null);
cases.push('one-session prompt gate waits for ready/visible and suppresses repeated state');

const inventoryGate = policy.createHidHideSetupPromptGate();
inventoryGate.update({ driverSetup: 3, hidHideSetupNotice: 2, driverSetupSession: 'inventory-session' });
inventoryGate.update({ driverSetup: 8, hidHideSetupNotice: 8, driverSetupSession: 'inventory-session' });
inventoryGate.update({ driverSetup: 1, hidHideSetupNotice: 2, driverSetupSession: 'inventory-session' });
assert.equal(inventoryGate.take(true, true), null);
cases.push('terminal inventory advisory clears a deferred installer prompt and rejects delayed pending state');
const deferredGate=policy.createHidHideSetupPromptGate();
deferredGate.update({driverSetup:3,hidHideSetupNotice:2,driverSetupSession:'deferred-session'});
deferredGate.update({driverSetup:9,hidHideSetupNotice:0,driverSetupSession:'deferred-session'});
deferredGate.update({driverSetup:1,hidHideSetupNotice:2,driverSetupSession:'deferred-session'});
assert.equal(deferredGate.take(true,true),null);
cases.push('maintenance-deferred terminal clears obsolete installation prompt and rejects late pending state');
const noticeModule={exports:{}};
vm.runInNewContext(transformSync(fs.readFileSync(path.join(root,'src/bridge/gamepadEnvironmentNotice.ts'),'utf8'),{loader:'ts',format:'cjs'}).code,
 {module:noticeModule,exports:noticeModule.exports,require:()=>policy});
const environment=noticeModule.exports.gamepadEnvironmentNotice;
const deferredNotice=environment({driverSetup:9,hidHideSetupNotice:0,driverSetupResult:19});
assert.equal(deferredNotice,null);
for(const snapshot of [{driverSetup:3,hidHideSetupNotice:0,driverSetupResult:11},
 {driverSetup:9,hidHideSetupNotice:8,driverSetupResult:19}, {driverSetup:8,hidHideSetupNotice:8,driverSetupResult:18}]) {
 const notice=environment(snapshot);assert(notice);assert.doesNotMatch(notice.description,/已检测到当前或更新版本的驱动包/);
}
for(const driverSetup of [0,2])assert.equal(environment({driverSetup}),null);
for(const driverSetup of [1,5])assert.doesNotMatch(environment({driverSetup}).description,/自动补装仍在|观察同一次安装/);
cases.push('actual environment formatter distinguishes installed packages, maintenance refusal and live binding without claiming runtime success');

// Compile and exercise the actual Windows setup launcher against a harmless test child.
const nativeDir = path.join(out, 'native'); fs.mkdirSync(nativeDir, { recursive: true });
const vcvars = process.env.YMCC_VCVARS64 || 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
const fixture = path.join(nativeDir, 'fixture.exe'), compile = path.join(nativeDir, 'compile.cmd');
const mainSource = fs.readFileSync(path.join(root, 'native/main.cpp'), 'utf8');
const helperStart = mainSource.indexOf('static json gamepadDriverSetupSnapshot() {');
const publicationParts = ['static json gamepadDriverSetupSnapshot()', 'static bool gamepadDriverSetupPollAndPublish()'].map(signature => {
  const start = mainSource.indexOf(signature + ' {');
  const end = mainSource.indexOf('\n}', start);
  assert(start >= 0 && end > start, 'Missing production status function: ' + signature);
  return mainSource.slice(start, end + 2);
});
assert(helperStart >= 0);
const publicationSource = publicationParts.join('\n');
fs.writeFileSync(path.join(nativeDir, 'hidhide_setup_publication_fixture.h'), publicationSource);
const publicationWitness = {file:'native/main.cpp',line:mainSource.slice(0,helperStart).split('\n').length,sha256:createHash('sha256').update(publicationSource).digest('hex')};
fs.writeFileSync(compile, calibrationToolchainPreamble(vcvars) + `cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /I"${nativeDir}" /I"${path.join(root, 'native')}" /I"${path.join(root, 'deps/json')}" "${path.join(root, 'tools/hidhide_setup_notice_selftest.cpp')}" /Fo"${path.join(nativeDir, 'fixture.obj')}" /Fe"${fixture}"\r\nexit /b %errorlevel%\r\n`.replaceAll('\\r\\n', '\r\n'));
const compiled = spawnSync('cmd.exe', ['/d', '/c', compile], { encoding: 'utf8', timeout: 90000 });
fs.writeFileSync(path.join(nativeDir, 'compile.log'), (compiled.stdout || '') + (compiled.stderr || ''));
assert.equal(compiled.status, 0, compiled.stdout + compiled.stderr);
const fixtureRun = path.join(nativeDir, 'fixtures-' + randomUUID());
const nativeRun = spawnSync(fixture, [fixtureRun], { encoding: 'utf8', timeout: 45000 });
assert.equal(nativeRun.status, 0, nativeRun.stdout + nativeRun.stderr);
const nativeResults = JSON.parse(fs.readFileSync(path.join(fixtureRun, 'native-results.json'), 'utf8'));
fs.writeFileSync(path.join(nativeDir, 'native-results.json'), JSON.stringify(nativeResults, null, 2));
console.log(nativeRun.stdout.trim());

const styles = [];
const mocks = {
  '@/bridge/performanceSchedule': 'export async function loadPerformanceSchedule(){return {quickMode:false}};',
  '@/bridge/yeman': 'export async function summonGet(){return {enabled:true}};',
  '@/robust/frontendDiagnostics': 'export function reportFrontendTrace(){};',
  '@/router': "export const ROUTES=[{path:'/controller'}];",
};
const built = await build({ stdin: { resolveDir: root, loader: 'ts', contents: `
import {createApp, nextTick} from 'vue';
import Prompt from './src/components/HidHideInstallPrompt.vue';
import {startGamepad} from './src/gamepad/engine';
const fixtureState=window.__fixture;
if(fixtureState.lateReady)window.__YMCC_STARTUP_READY__={generation:'fixture',navigationId:'1'};
if(fixtureState.cachedState)fixtureState.message({event:'gamepad.state',data:fixtureState.cachedState});
let app=createApp(Prompt);app.mount('#app');
startGamepad({router:{currentRoute:{value:{path:'/controller'}},push:async()=>{}},onAction(){}});
fixtureState.ready=()=>window.dispatchEvent(new Event('app-startup-ready'));
fixtureState.action=(action)=>fixtureState.message({event:'gamepad.ui-input',data:{action,uiSeq:++fixtureState.sequence,provenance:{session:'fixture'}}});
fixtureState.unmount=()=>app.unmount();
fixtureState.remount=()=>{app.unmount();app=createApp(Prompt);app.mount('#app')};
nextTick(()=>fixtureState.mounted=true);
` }, bundle: true, write: false, format: 'iife', platform: 'browser', loader: { '.png': 'dataurl' }, alias: { '@': path.join(root, 'src') }, define: { 'process.env.NODE_ENV': '"production"' }, plugins: [{ name: 'hidhide-isolated-native', setup(b) {
  b.onResolve({ filter: /.*/ }, args => args.path in mocks ? { path: args.path, namespace: 'fixture' } : undefined);
  b.onLoad({ filter: /.*/, namespace: 'fixture' }, args => ({ contents: mocks[args.path], loader: 'ts' }));
  b.onLoad({ filter: /\.vue$/ }, args => {
    const { descriptor, errors } = parse(fs.readFileSync(args.path, 'utf8'), { filename: args.path }); assert.equal(errors.length, 0);
    const id='data-v-'+createHash('sha256').update(args.path).digest('hex').slice(0,8);
    const component=compileScript(descriptor,{id,inlineTemplate:true});
    for(const style of descriptor.styles){const css=compileStyle({source:style.content,filename:args.path,id,scoped:style.scoped});assert.equal(css.errors.length,0);styles.push(css.code)}
    return {contents:component.content.replace('export default','const _component =')+`\n_component.__scopeId=${JSON.stringify(id)};export default _component;`,loader:'ts',resolveDir:path.dirname(args.path)};
  });
} }] });
const js = built.outputFiles[0].text;
const css = fs.readFileSync(path.join(root,'src/styles/tokens.css'),'utf8')+'\n'+styles.join('\n');
const bridgePrelude = `
const query=new URLSearchParams(location.search);
const f=window.__fixture={opened:[],requests:[],openFail:false,openThrow:false,openHold:query.has('open-hold'),readHold:query.has('read-hold'),lateReady:query.has('late-ready'),sequence:0,cachedState:query.has('cached')?{driverSetup:3,hidHideSetupNotice:2,driverSetupSession:query.get('session')||''}:null,heldReads:[],heldOpens:[]};
if(query.has('cached-pending'))f.cachedState={driverSetup:1,hidHideSetupNotice:0,driverSetupSession:'cached-pending-session'};
f.setupState=query.has('cached-pending')?{driverSetup:3,hidHideSetupNotice:2,driverSetupSession:'cached-pending-session'}:f.cachedState||{driverSetup:0,hidHideSetupNotice:0};
const listeners=[];
f.message=(data)=>{for(const listener of listeners)listener({data})};
f.emit=(state)=>{f.setupState=state;f.message({event:'gamepad.state',data:state})};
f.emitSetup=(state)=>{f.setupState=state;f.message({event:'gamepad.driverSetup',data:state})};
f.releaseRead=()=>{for(const reply of f.heldReads.splice(0))f.message(reply)};
f.releaseOpen=(result=true)=>{for(const id of f.heldOpens.splice(0))f.message({id,result})};
const chromeObject=window.chrome||(window.chrome={});
chromeObject.webview={addEventListener(event,listener){if(event==='message')listeners.push(listener)},postMessage(message){
  f.requests.push(message);
  if(message.cmd==='gamepad.driverSetup.get'){
    const reply={id:message.id,result:structuredClone(f.setupState)};
    if(f.readHold)f.heldReads.push(reply);else queueMicrotask(()=>f.message(reply));return;
  }
  if(message.cmd==='shell.open'){
    f.opened.push(message.args.url);
    if(f.openThrow)throw Error('fixture transport failure');
    if(f.openHold)f.heldOpens.push(message.id);else queueMicrotask(()=>f.message({id:message.id,result:!f.openFail}));return;
  }
  if(typeof message.id==='number')queueMicrotask(()=>f.message({id:message.id,result:false}));
}};
`;
const html = `<!doctype html><meta charset="utf-8"><title>HidHide install prompt actual IPC validation</title><style>${css}\nbody{margin:0;background:var(--bg-solid)}#app{padding:20px}.focused{box-shadow:var(--focus-ring)}</style><div class="app-content"><button id="previous-focus">原页面按钮</button><main id="app"></main></div><script>${bridgePrelude}</script><script src="/fixture.js"></script>`;
const server = createServer((q,r)=>{r.setHeader('Content-Type',q.url==='/fixture.js'?'text/javascript; charset=utf-8':'text/html; charset=utf-8');r.end(q.url==='/fixture.js'?js:html)});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
let browser;
try {
  browser=await chromium.launch({headless:true,executablePath:process.env.YMCC_UI_BROWSER_EXE||'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
  const page=await browser.newPage();page.on('pageerror',e=>errors.push(e.message));
  const url='http://127.0.0.1:'+server.address().port;
  const dialog=page.getByRole('alertdialog');
  const confirm=page.getByRole('button',{name:'确定',exact:true});
  const cancel=page.getByRole('button',{name:'取消',exact:true});
  async function fresh(query=''){await page.goto(url+query);await page.waitForFunction(()=>window.__fixture?.mounted);await page.locator('#previous-focus').focus()}
  async function emit(state){await page.evaluate(state=>window.__fixture.emit(state),state)}
  async function ready(){await page.evaluate(()=>window.__fixture.ready())}
  async function action(action){await page.evaluate(action=>window.__fixture.action(action),action)}
  const failure={driverSetup:3,hidHideSetupNotice:2,driverSetupRedistDir:policy.DEFAULT_GAMEPAD_REDIST_DIRECTORY};
  await fresh('?cached=1');assert.equal(await dialog.count(),0);await ready();await dialog.waitFor();
  assert.equal(await confirm.evaluate(el=>el===document.activeElement),true);
  cases.push('cached native state waits for app startup and enters dialog focus');
  await action('nav-left');await page.waitForFunction(()=>document.activeElement?.textContent==='取消');
  await action('nav-right');await page.waitForFunction(()=>document.activeElement?.textContent==='确定');
  await action('confirm');await dialog.waitFor({state:'hidden'});
  assert.deepEqual(await page.evaluate(()=>window.__fixture.opened),[policy.DEFAULT_GAMEPAD_REDIST_DIRECTORY]);
  cases.push('real YMCC gamepad engine left/right and A opens exact redist folder');
  await emit(failure);assert.equal(await dialog.count(),0);cases.push('dismissed startup failure is not repeated');

  await fresh();await emit(failure);await page.evaluate(()=>window.dispatchEvent(new Event('ipc:window.hidden')));await ready();
  assert.equal(await dialog.count(),0);await page.evaluate(()=>window.dispatchEvent(new Event('ipc:window.shown')));await dialog.waitFor();
  await action('back');await dialog.waitFor({state:'hidden'});assert.equal(await page.locator('#previous-focus').evaluate(el=>el===document.activeElement),true);
  assert.deepEqual(await page.evaluate(()=>window.__fixture.opened),[]);
  cases.push('hidden window defers prompt; real B cancels and restores prior focus');

  for(const notice of [1,2,3,4,5,6]){
    await fresh();await ready();await emit({...failure,hidHideSetupNotice:notice});await dialog.waitFor();
    const text=await dialog.textContent();assert(text.includes(policy.HIDHIDE_INSTALLER_NAME));assert(text.includes(policy.DEFAULT_GAMEPAD_REDIST_DIRECTORY));
    if(notice===5)assert(text.includes('完整安装包恢复安装器'));
    if(notice===6)assert(text.includes('恢复正确安装器'));
    await cancel.click();await dialog.waitFor({state:'hidden'});
    cases.push('distinct manual-install reason '+notice);
  }
  await fresh();await ready();
  for(const state of [{driverSetup:9,hidHideSetupNotice:0,driverSetupResult:19},{driverSetup:9,hidHideSetupNotice:2,driverSetupResult:19},{driverSetup:8,hidHideSetupNotice:8,driverSetupResult:18},{driverSetup:8,hidHideSetupNotice:2,driverSetupResult:18},{driverSetup:1,hidHideSetupNotice:1},{driverSetup:5,hidHideSetupNotice:1},{driverSetup:2,hidHideSetupNotice:0},{driverSetup:3,hidHideSetupNotice:0},{driverSetup:4,hidHideSetupNotice:7},{driverSetup:3,hidHideSetupNotice:8,driverSetupResult:2}]){
    await fresh();await ready();await emit(state);assert.equal(await dialog.count(),0);
  }
  cases.push('pending, slow, healthy, HidMaestro-only failure, required reboot and desktop missing-tool result 2 do not prompt');

  await fresh();await ready();await page.evaluate(()=>window.__fixture.openFail=true);await emit(failure);await dialog.waitFor();
  await confirm.click();await page.getByRole('status').waitFor();assert.equal(await dialog.count(),1);assert.equal(await confirm.isDisabled(),false);
  await page.evaluate(()=>{window.__fixture.openFail=false;window.__fixture.openThrow=true});await confirm.click();assert.equal(await dialog.count(),1);
  await page.evaluate(()=>window.__fixture.openThrow=false);await confirm.click();await dialog.waitFor({state:'hidden'});
  assert.equal((await page.evaluate(()=>window.__fixture.opened)).length,3);
  cases.push('open-folder false/exception keep retryable prompt; successful retry closes');

  await fresh();await ready();const custom=String.raw`D:\Apps\YeMan\PowerControl\redist`;
  await emit({...failure,driverSetupRedistDir:custom});await confirm.waitFor();await confirm.press('Enter');await dialog.waitFor({state:'hidden'});
  assert.deepEqual(await page.evaluate(()=>window.__fixture.opened),[custom]);cases.push('custom install root and keyboard confirmation');

  await fresh('?cached-pending=1&late-ready=1');await dialog.waitFor();assert((await dialog.textContent()).includes('管理员权限'));await cancel.click();await dialog.waitFor({state:'hidden'});
  cases.push('cached pending replay establishes baseline before current terminal-state query');

  await fresh();await ready();await page.evaluate(()=>window.dispatchEvent(new Event('ipc:window.hidden')));
  const orderedFailure={...failure,driverSetupSession:'out-of-order-terminal'};
  await page.evaluate(state=>window.__fixture.emitSetup(state),orderedFailure);
  await emit({driverSetup:1,hidHideSetupNotice:0,driverSetupSession:orderedFailure.driverSetupSession});
  await emit({driverSetup:5,hidHideSetupNotice:0,driverSetupSession:orderedFailure.driverSetupSession});
  await page.evaluate(()=>window.dispatchEvent(new Event('ipc:window.shown')));await dialog.waitFor();await cancel.click();await dialog.waitFor({state:'hidden'});
  cases.push('terminal setup event survives delayed pending/slow controller snapshots while hidden');

  await fresh();await ready();await page.evaluate(()=>window.dispatchEvent(new Event('ipc:window.hidden')));
  await page.evaluate(()=>window.__fixture.emitSetup({driverSetup:2,hidHideSetupNotice:0,driverSetupSession:'out-of-order-healthy'}));
  await emit({driverSetup:1,hidHideSetupNotice:0,driverSetupSession:'out-of-order-healthy'});
  await page.evaluate(()=>window.dispatchEvent(new Event('ipc:window.shown')));assert.equal(await dialog.count(),0);
  cases.push('healthy terminal result also rejects delayed nonterminal snapshots');

  // Feed actual native fixture outputs into the actual frontend IPC receiver and prompt.
  for(const snapshot of nativeResults.completedSnapshots){
    await fresh();await ready();await emit(snapshot);
    const expected=policy.hidHideManualInstallPrompt(snapshot);
    if(expected){await dialog.waitFor();await confirm.click();await dialog.waitFor({state:'hidden'});assert.deepEqual(await page.evaluate(()=>window.__fixture.opened),[snapshot.driverSetupRedistDir]);}
    else assert.equal(await dialog.count(),0);
    cases.push('native completed report -> publication JSON -> actual IPC -> UI '+snapshot.driverSetupSession);
  }

  await fresh('?read-hold=1');await ready();await page.evaluate(()=>window.dispatchEvent(new Event('ipc:window.hidden')));
  await page.evaluate(state=>window.__fixture.emitSetup(state),failure);assert.equal(await dialog.count(),0);
  await page.evaluate(()=>window.__fixture.releaseRead());await page.evaluate(()=>window.dispatchEvent(new Event('ipc:window.shown')));await dialog.waitFor();await cancel.click();await dialog.waitFor({state:'hidden'});
  cases.push('dedicated setup event works without any controller state; stale initial reply cannot clear it');

  await fresh('?cached=1&read-hold=1');await page.evaluate(()=>window.dispatchEvent(new Event('ipc:window.hidden')));await ready();
  await page.evaluate(()=>window.__fixture.emitSetup({driverSetup:2,hidHideSetupNotice:0}));await page.evaluate(()=>window.__fixture.releaseRead());
  await page.evaluate(()=>window.dispatchEvent(new Event('ipc:window.shown')));assert.equal(await dialog.count(),0);
  cases.push('late old failure reply cannot overwrite a newer healthy event');

  await fresh('?late-ready=1');await emit(failure);await dialog.waitFor();await cancel.click();await dialog.waitFor({state:'hidden'});
  cases.push('already completed or degraded startup receipt allows late component mount');

  await fresh('?cached=1&late-ready=1&session=persistence-A');await dialog.waitFor();await cancel.click();await dialog.waitFor({state:'hidden'});
  await page.evaluate(()=>window.__fixture.remount());assert.equal(await dialog.count(),0);
  await page.reload();await page.waitForFunction(()=>window.__fixture?.mounted);assert.equal(await dialog.count(),0);
  cases.push('explicit cancel is remembered across remount and renderer document reload of one native session');
  await fresh('?cached=1&late-ready=1&session=persistence-B');await dialog.waitFor();await cancel.click();await dialog.waitFor({state:'hidden'});
  cases.push('new native startup session is allowed to report its own failure');
  await fresh('?cached=1&late-ready=1&session=undismissed-C');await dialog.waitFor();await page.reload();await dialog.waitFor();await cancel.click();await dialog.waitFor({state:'hidden'});
  cases.push('renderer reload retains an undismissed failure for the user to see');

  await fresh('?late-ready=1');await page.evaluate(()=>{window.__fixture.storageDescriptor=Object.getOwnPropertyDescriptor(window,'sessionStorage');Object.defineProperty(window,'sessionStorage',{configurable:true,get(){throw Error('fixture storage denied')}})});
  await emit({...failure,driverSetupSession:'storage-denied-D'});await dialog.waitFor();await cancel.click();await dialog.waitFor({state:'hidden'});await page.evaluate(()=>window.__fixture.remount());assert.equal(await dialog.count(),0);
  await page.evaluate(()=>Object.defineProperty(window,'sessionStorage',window.__fixture.storageDescriptor));
  cases.push('storage denial does not break cancel or in-document dismissal');

  await fresh();await ready();await page.evaluate(()=>window.__fixture.message({event:'power.suspending',data:{}}));await emit(failure);assert.equal(await dialog.count(),0);
  await page.evaluate(()=>window.__fixture.message({event:'power.resumed',data:{}}));await dialog.waitFor();await cancel.click();await dialog.waitFor({state:'hidden'});
  cases.push('power transition defers prompt until shared frontend lifecycle is ready');

  await fresh();await ready();await emit(failure);await dialog.waitFor();
  await page.evaluate(()=>{const hidden=document.createElement('div');hidden.dataset.gpModal='';hidden.hidden=true;hidden.id='hidden-modal-fixture';document.body.append(hidden)});
  await action('back');await dialog.waitFor({state:'hidden'});cases.push('a hidden newer modal cannot swallow B cancel');

  await fresh();await ready();await emit(failure);await dialog.waitFor();
  await page.evaluate(()=>{const newer=document.createElement('div');newer.id='newer-modal-fixture';newer.dataset.gpModal='';newer.style='position:fixed;left:20px;top:20px;width:240px;height:90px;z-index:2000';newer.innerHTML='<button id="newer-focus">新弹窗按钮</button>';document.body.append(newer);document.getElementById('newer-focus').focus();window.dispatchEvent(new Event('ipc:window.hidden'));window.dispatchEvent(new Event('ipc:window.shown'))});
  assert.equal(await page.locator('#newer-focus').evaluate(el=>el===document.activeElement),true);
  await action('back');assert.equal(await dialog.count(),1);await page.locator('#newer-modal-fixture').evaluate(el=>el.remove());await action('back');await dialog.waitFor({state:'hidden'});
  cases.push('a newer visible modal retains focus and B ownership on restore');

  await fresh();await ready();await emit(failure);await dialog.waitFor();await cancel.click();await dialog.waitFor({state:'hidden'});
  await page.evaluate(state=>{for(let i=0;i<1000;++i){window.__fixture.emit({...state,axes:[i,0,0,0]});window.__fixture.emitSetup(state)}},failure);
  assert.equal(await dialog.count(),0);assert.equal(await page.evaluate(()=>window.__fixture.requests.filter(r=>r.cmd==='shell.open').length),0);
  cases.push('2000 duplicate/noisy state events cannot reopen a dismissed prompt or launch anything');

  const timeoutPage=await browser.newPage();timeoutPage.on('pageerror',e=>errors.push(e.message));await timeoutPage.clock.install();
  await timeoutPage.goto(url+'?open-hold=1');await timeoutPage.waitForFunction(()=>window.__fixture?.mounted);await timeoutPage.evaluate(()=>window.__fixture.ready());await timeoutPage.evaluate(state=>window.__fixture.emit(state),failure);
  const timeoutDialog=timeoutPage.getByRole('alertdialog');await timeoutDialog.waitFor();
  await timeoutPage.evaluate(()=>{const button=document.querySelector('[data-gp-scope="hidhide-install"] button.confirm');button.click();button.click()});
  assert.equal(await timeoutPage.evaluate(()=>window.__fixture.opened.length),1);
  await timeoutPage.clock.fastForward(10001);await timeoutPage.getByRole('status').waitFor();
  assert.equal(await timeoutPage.getByRole('button',{name:'确定',exact:true}).isDisabled(),false);
  await timeoutPage.evaluate(()=>window.__fixture.releaseOpen());assert.equal(await timeoutDialog.count(),1);
  await timeoutPage.evaluate(()=>window.__fixture.openHold=false);await timeoutPage.getByRole('button',{name:'确定',exact:true}).click();await timeoutDialog.waitFor({state:'hidden'});
  cases.push('double click starts one directory request; actual IPC timeout recovers buttons; expired late reply is ignored and retry succeeds');

  await timeoutPage.goto(url+'?read-hold=1');await timeoutPage.waitForFunction(()=>window.__fixture?.mounted);await timeoutPage.evaluate(()=>window.__fixture.ready());await timeoutPage.clock.fastForward(1501);
  await timeoutPage.evaluate(state=>window.__fixture.emitSetup(state),failure);await timeoutPage.getByRole('alertdialog').waitFor();await timeoutPage.getByRole('button',{name:'取消',exact:true}).click();await timeoutPage.getByRole('alertdialog').waitFor({state:'hidden'});
  cases.push('initial status-query timeout preserves the authoritative setup-event fallback');

  await timeoutPage.goto(url+'?open-hold=1');await timeoutPage.waitForFunction(()=>window.__fixture?.mounted);await timeoutPage.evaluate(()=>window.__fixture.ready());await timeoutPage.evaluate(state=>window.__fixture.emit(state),failure);await timeoutPage.getByRole('alertdialog').waitFor();await timeoutPage.getByRole('button',{name:'确定',exact:true}).click();
  await timeoutPage.evaluate(()=>window.__fixture.message({event:'webview.recovering',data:{reason:'fixture renderer recovery'}}));await timeoutPage.getByRole('status').waitFor();
  assert.equal(await timeoutPage.getByRole('button',{name:'确定',exact:true}).isDisabled(),false);await timeoutPage.getByRole('button',{name:'取消',exact:true}).click();await timeoutPage.getByRole('alertdialog').waitFor({state:'hidden'});
  cases.push('actual IPC recovery rejects pending folder request without locking the prompt');await timeoutPage.close();

  for(const [width,height] of [[1280,720],[1280,800],[1920,1080],[420,800],[320,480]]){
    for(const theme of ['blue-black','red-black','cyberpunk']){
      await page.setViewportSize({width,height});await fresh();await ready();await page.evaluate(theme=>document.documentElement.dataset.theme=theme,theme);
      await emit(failure);await dialog.waitFor();
      const layout=await dialog.evaluate(el=>{const r=el.getBoundingClientRect(),buttons=[...el.querySelectorAll('button')].map(b=>{const q=b.getBoundingClientRect();return {top:q.top,left:q.left,right:q.right,bottom:q.bottom}});return {top:r.top,bottom:r.bottom,left:r.left,right:r.right,overflow:document.documentElement.scrollWidth>innerWidth,buttons}});
      assert(!layout.overflow);assert(layout.left>=19&&layout.right<=width-19);assert(layout.top>=19&&layout.bottom<=height-19);
      assert(Math.abs(layout.buttons[0].top-layout.buttons[1].top)<.5);assert(layout.buttons.every(b=>b.bottom<=height-19));
      assert.equal(await confirm.evaluate(el=>el===document.activeElement),true);
      if(width===1280&&height===720&&theme==='blue-black'){await page.waitForFunction(()=>getComputedStyle(document.querySelector('.hidhide-install-shade')).opacity==='1');const shot=path.join(out,'hidhide-install-prompt-1280x720.png');await page.screenshot({path:shot});screenshots.push(shot)}
      await dialog.press('Escape');await dialog.waitFor({state:'hidden'});
      cases.push(`theme/layout/escape ${theme} ${width}x${height}`);
    }
  }
  await fresh();await ready();await page.evaluate(()=>window.__fixture.unmount());await emit(failure);assert.equal(await dialog.count(),0);
  cases.push('unmount releases event subscriptions');
  assert.deepEqual(errors,[]);
} finally {await browser?.close();await new Promise(resolve=>server.close(resolve))}
const app=fs.readFileSync(path.join(root,'src/App.vue'),'utf8');
assert(app.includes('<HidHideInstallPrompt v-if="!isStandaloneEditor" />'));
const files=['native/gamepad_driver_setup.h','native/gamepad_driver_setup_notice.h','src/bridge/hidHideSetupPrompt.ts','src/components/HidHideInstallPrompt.vue','tools/hidhide_setup_notice_selftest.cpp','tools/hidhide_setup_prompt_selftest.mjs'];
files.push('src/bridge/gamepadEnvironmentNotice.ts','tools/fixtures/hc_reviewed_calibration/toolchain.mjs');
const witnesses=files.map(file=>({file,sha256:createHash('sha256').update(fs.readFileSync(path.join(root,file))).digest('hex')}));
fs.writeFileSync(path.join(out,'results.json'),JSON.stringify({passed:true,nativeChecks:nativeResults.checks,browserAndPolicyChecks:cases.length,publicationWitness,cases,screenshots,errors,witnesses,deviceOperations:0,limitations:'Actual production setup status functions/native launcher, actual IPC bridge and gamepad engine; external transport alone is a fixture. No real driver installation, elevation or production deployment.'},null,2));
console.log(`HIDHIDE_SETUP_PROMPT_PASS native=${nativeResults.checks} browserAndPolicy=${cases.length} deviceOperations=0`);
