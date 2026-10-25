// Independent browser proof: actual status component/IPC/lifecycle, and the two
// views' exact persona/power control fragments. Does not run the full-page gate.
// Usage: node tools/usbip_upgrade_status_browser_selftest.mjs --out <fresh-output-directory>
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import { build, stop } from 'esbuild';
import { parse, compileScript, compileStyle } from 'vue/compiler-sfc';
const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
assert.equal(process.argv.length, 4, 'Usage: --out <fresh-output-directory>');
assert.equal(process.argv[2], '--out');
const out = path.resolve(process.argv[3]);
assert(!fs.existsSync(out), 'Use a fresh output directory and retain failed evidence');
assert(!out.startsWith(repo + path.sep), 'Keep evidence outside the shared repository');
fs.mkdirSync(out, { recursive: true });
const runtime = process.env.YMCC_UI_BROWSER_MODULE_DIR || 'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const { chromium } = createRequire(path.join(runtime, '_usbip-browser.cjs'))('playwright');
const receipt = { schemaVersion: 1, status: 'running', startedAt: new Date().toISOString(),
  scope: 'Actual USBIP component, bridge IPC and visibility lifecycle; exact view control fragments with memory actions',
  realYmcc: false, deviceWrites: false, steamUsed: false, physicalGamepad: false, fullPageNavigationGateRun: false,
  sources: [], checks: [], errors: [] };
const sources = new Map(), styles = [];
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
function source(file) {
  const bytes = fs.readFileSync(file); sources.set(file, sha(bytes));
  const target = path.join(out, 'source', path.relative(repo, file));
  fs.mkdirSync(path.dirname(target), { recursive: true }); fs.writeFileSync(target, bytes);
  return bytes.toString('utf8');
}
source(fileURLToPath(import.meta.url));
function viewFragment(file, className) {
  const text = source(path.join(repo, file)); const { descriptor, errors } = parse(text, { filename: file });
  assert.deepEqual(errors, []);
  let control, status, ancestors;
  function walk(node, parents = []) {
    if (node.type === 1) {
      if (node.props.some(p => p.type === 6 && p.name === 'class' && p.value?.content === className)) control = node;
      if (node.tag === 'UsbipUpgradeStatus') { assert(!status, 'One status per page'); status = node; ancestors = parents; }
      for (const child of node.children || []) walk(child, [...parents, node]);
    } else for (const child of node.children || []) walk(child, parents);
  }
  walk(descriptor.template.ast); assert(control && status);
  compileScript(descriptor, { id: 'usbip-full-view-compile', inlineTemplate: true });
  // No physical-device/power condition can prevent the actual status insertion rendering.
  for (const node of [...ancestors, status]) for (const p of node.props || [])
    if (p.type === 7 && ['if', 'show'].includes(p.name)) assert.doesNotMatch(p.exp?.content || '', /connected|physical|enabled|power/i);
  assert.doesNotMatch(text, /usbipPrompt|backendInstallPrompt|usbip-prompt/);
  for (const style of descriptor.styles) {
    const result = compileStyle({ source: style.content, filename: file, id: 'fragment', scoped: false });
    assert.deepEqual(result.errors, []); styles.push(result.code);
  }
  if (file.endsWith('SettingsView.vue')) return control.loc.source;
  return [control, status].sort((a, b) => a.loc.start.offset - b.loc.start.offset).map(n => n.loc.source).join('\n');
}
const controllerFragment = viewFragment('src/views/ButtonMappingView.vue', 'persona-actions');
const gyroFragment = viewFragment('src/views/GyroMotionView.vue', 'gyro-power-row');
const settingsFragment = viewFragment('src/views/SettingsView.vue', 'card usbip-settings-card');
const imports = `import {ref} from 'vue';import UsbipUpgradeStatus from '@/components/UsbipUpgradeStatus.vue';import InlineIcon from '@/components/InlineIcon.vue';`;
const fragments = {
  'settings-fragment.vue': `<script setup>${imports}const active=true;</script><template><div data-view="settings">${settingsFragment}</div></template>`,
  'controller-fragment.vue': `<script setup>${imports}
const visiblePersonaOptions=[{value:'disabled',label:'本机手柄'},{value:'steamdeck',label:'Steam Deck'},{value:'dualsense-edge',label:'PS5'},{value:'xbox360',label:'Xbox'}];
const displayPersona=ref('disabled'),gameLocked=false,targetBusy=false,startProtectionLocked=false,externalPriorityBusy=false,profileRemovalBusy=false,snapshot={},virtualTargetPolicy={};
const virtualTargetSelectionError=()=>'';function onPersona(value){displayPersona.value=value;window.f.actions.push(value)};
</script><template><section class="controller-page controller-paged card" data-view="controller"><h2>控制器 · 内存 IPC</h2><p>未连接物理手柄</p><div class="persona-block">${controllerFragment}</div></section></template>`,
  'gyro-fragment.vue': `<script setup>${imports}
const gyroPowerState='unknown',gyroPowerText='陀螺仪未识别',gyroPowerClass='',controlsLocked=false,virtualPadLink=ref(true);
function toggleGyroPower(){window.f.actions.push('gyro')};function toggleVirtualPadLink(){virtualPadLink.value=!virtualPadLink.value;window.f.actions.push('link')};
</script><template><section class="gyro-integrated-page card" data-view="gyro"><h2>陀螺仪 · 内存 IPC</h2><p>未连接物理手柄</p>${gyroFragment}</section></template>`,
};
const entry = `import {createApp,h,KeepAlive,ref,nextTick} from 'vue';
import Controller from 'controller-fragment.vue';import Gyro from 'gyro-fragment.vue';import Settings from 'settings-fragment.vue';
window.f.refreshBaseline=window.f.counts().refreshListeners;
let app,shown;const Empty={render:()=>h('div',{'data-empty':''},'其他页面')};
window.mount=async view=>{app?.unmount();shown=ref(true);window.__fixtureView=view;const component={controller:Controller,gyro:Gyro,settings:Settings}[view];
app=createApp({setup(){return()=>h(KeepAlive,null,()=>shown.value?h(component,{key:'status'}):h(Empty,{key:'empty'}))}});app.mount('#app');await nextTick();await nextTick()};
window.show=async value=>{shown.value=value;await nextTick();await nextTick()};
window.unmount=async()=>{app?.unmount();app=null;await nextTick();await nextTick()};`;
const prelude = `
const activeTimers=new Set(),tracked=new Map(),nativeListeners=[],progressTimers=new Map(),abortCallbacks=new Set();
let nextProgressTimer=-1,progressElapsed=0,wallClockOffset=0;
const realWallNow=Date.now.bind(Date),realPerformanceNow=performance.now.bind(performance);
Date.now=()=>realWallNow()+wallClockOffset;
// The consumer uses the monotonic clock; timer due dates and virtual advancement share it.
Object.defineProperty(performance,'now',{configurable:true,value:()=>realPerformanceNow()+progressElapsed});
const signalAdd=AbortSignal.prototype.addEventListener,signalRemove=AbortSignal.prototype.removeEventListener;
AbortSignal.prototype.addEventListener=function(name,fn,options){if(name==='abort')abortCallbacks.add(fn);signalAdd.call(this,name,fn,options)};
AbortSignal.prototype.removeEventListener=function(name,fn,options){if(name==='abort')abortCallbacks.delete(fn);signalRemove.call(this,name,fn,options)};
const rawSet=window.setTimeout.bind(window),rawClear=window.clearTimeout.bind(window);
window.setTimeout=(fn,ms,...args)=>{if(ms>1500){const id=nextProgressTimer--;progressTimers.set(id,{at:performance.now()+ms,fn:()=>fn(...args)});activeTimers.add(id);return id}let id=rawSet(()=>{activeTimers.delete(id);fn(...args)},ms);activeTimers.add(id);return id};
window.clearTimeout=id=>{activeTimers.delete(id);if(progressTimers.delete(id))return;rawClear(id)};
const rawAdd=window.addEventListener.bind(window),rawRemove=window.removeEventListener.bind(window);
window.addEventListener=(name,fn,options)=>{if((['ipc:gamepad.driverSetup','ui:visibility','focus'].includes(name)||name.startsWith('ipc:window.'))){if(!tracked.has(name))tracked.set(name,new Set());tracked.get(name).add(fn)}rawAdd(name,fn,options)};
window.removeEventListener=(name,fn,options)=>{tracked.get(name)?.delete(fn);rawRemove(name,fn,options)};
window.f={shiftWallClock(ms){wallClockOffset+=ms},tickProgress(ms){progressElapsed+=ms;for(const[id,timer]of [...progressTimers])if(timer.at<=performance.now()){progressTimers.delete(id);activeTimers.delete(id);timer.fn()}},requests:[],actions:[],auto:true,session:'fixture',sequence:0,
 snapshot(phase='current',extra={}){return{driverSetupSession:this.session,usbipUpgrade:{schemaVersion:1,phase,installedVersion:'0.9.7',targetVersion:'0.9.8',detail:'',sequence:++this.sequence,...extra}}},
 reply(index,result){const request=this.requests[index];for(const listener of nativeListeners)listener({data:{id:request.id,result}})},
 event(event,data={}){for(const listener of nativeListeners)listener({data:{event,data}})},
 emit(phase,extra={}){this.event('gamepad.driverSetup',this.snapshot(phase,extra))},
 counts(){return{status:tracked.get('ipc:gamepad.driverSetup')?.size||0,visibility:tracked.get('ui:visibility')?.size||0,timers:activeTimers.size,reads:this.requests.length,abortCallbacks:abortCallbacks.size,refreshListeners:[...tracked].filter(([name])=>name==='focus'||name.startsWith('ipc:window.')).reduce((sum,[,listeners])=>sum+listeners.size,0)-(this.refreshBaseline||0)}}};
window.chrome={webview:{addEventListener(name,listener){if(name==='message')nativeListeners.push(listener)},postMessage(message){if(message.cmd!=='gamepad.driverSetup.get')throw Error('Unexpected IPC '+JSON.stringify(message));const index=f.requests.push(message)-1;if(f.auto)queueMicrotask(()=>f.reply(index,f.snapshot()))}}};`;
const plugin = { name: 'usbip-independent-browser', setup(b) {
  b.onResolve({ filter: /^(controller|gyro|settings)-fragment\.vue$/ }, args => ({ path: args.path, namespace: 'fragment' }));
  async function loadVue(file, text) {
    const { descriptor, errors } = parse(text, { filename: file }); assert.deepEqual(errors, []);
    const id = 'data-v-' + sha(Buffer.from(file)).slice(0, 8);
    const script = compileScript(descriptor, { id, inlineTemplate: true });
    for (const style of descriptor.styles) { const result = compileStyle({ source: style.content, filename: file, id, scoped: style.scoped });
      assert.deepEqual(result.errors, []); styles.push(result.code); }
    return { contents: script.content.replace('export default', 'const Component =') + '\nComponent.__scopeId=' + JSON.stringify(id) + ';export default Component;', loader: 'ts', resolveDir: path.isAbsolute(file) ? path.dirname(file) : repo };
  }
  b.onLoad({ filter: /.*/, namespace: 'fragment' }, args => loadVue(args.path, fragments[args.path]));
  b.onLoad({ filter: /\.vue$/ }, args => loadVue(args.path, source(args.path)));
  b.onLoad({ filter: /\.ts$/ }, args => ({ contents: source(args.path), loader: 'ts', resolveDir: path.dirname(args.path) }));
} };
let browser;
const save = () => { receipt.sources = [...sources].map(([file, sha256]) => ({ file: path.relative(repo, file).replaceAll('\\', '/'), sha256 }));
  fs.writeFileSync(path.join(out, 'result.json'), JSON.stringify(receipt, null, 2) + '\n'); };
async function check(name, run) {
  try { await run(); receipt.checks.push({ name, status: 'PASS' }); }
  catch (error) { receipt.checks.push({ name, status: 'FAIL', error: error.stack }); throw error; }
  finally { save(); }
}
try {
  const bundle = await build({ stdin: { contents: entry, resolveDir: repo, loader: 'ts' }, bundle: true, write: false,
    platform: 'browser', format: 'iife', loader: { '.png': 'dataurl' }, alias: { '@': path.join(repo, 'src') },
    define: { 'process.env.NODE_ENV': '"production"' }, plugins: [plugin] });
  const tokens = source(path.join(repo, 'src/styles/tokens.css'));
  const html = '<!doctype html><meta charset="utf-8"><style>' + tokens + styles.join('\n') +
    'body{margin:20px;background:#101823;color:#e8edf3;font:14px system-ui}.card{max-width:800px;margin:auto;padding:18px;border-radius:12px;background:#172333;border:1px solid #435163}h2{margin-top:0}.gyro-power-row{display:flex;gap:8px}button{min-height:38px;cursor:pointer}</style><main id="app"></main><script>' + prelude + '</script><script src="/fixture.js"></script>';
  fs.writeFileSync(path.join(out, 'fixture.js'), bundle.outputFiles[0].contents); fs.writeFileSync(path.join(out, 'index.html'), html);
  browser = await chromium.launch({ headless: true, executablePath: process.env.YMCC_UI_BROWSER_EXE || 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe' });
  const page = await browser.newPage({ viewport: { width: 1280, height: 720 } });
  page.on('pageerror', error => receipt.errors.push(error.message));
  // Mount the isolated component fixture without navigating a page or product route.
  await page.setContent(html.replace('<script src="/fixture.js"></script>', ''));
  await page.addScriptTag({ content: bundle.outputFiles[0].text });
  const phase = () => page.locator('[data-usbip-phase]').getAttribute('data-usbip-phase');
  const waitPhase = async expected => {
    await page.evaluate(async () => { for (let i=0;i<8;++i) await Promise.resolve(); });
    await page.waitForFunction(p => {
      const node=document.querySelector('[data-usbip-phase]');
      return window.__fixtureView==='settings' || ['failed','admin-required','reboot-required'].includes(p)
        ? node?.getAttribute('data-usbip-phase')===p : !node;
    }, expected);
  };
  for (const height of [720, 800]) for (const view of ['controller', 'gyro']) {
    const label = view + '@1280x' + height;
    await page.setViewportSize({ width: 1280, height });
    await check('One initial read but healthy status occupies no inline space: ' + label, async () => {
      const before = await page.evaluate(() => f.requests.length);
      await page.evaluate(async view => { f.auto = true; f.session = view + '-' + f.requests.length; f.sequence = 0; await mount(view); }, view);
      await waitPhase('current'); const counts = await page.evaluate(() => f.counts());
      assert.equal(counts.reads, before + 1); assert.equal(counts.status, 1); assert.equal(counts.visibility, 1); assert.equal(counts.timers, 0);
      assert.equal(await page.locator('[role="status"]').count(), 0); assert(await page.getByText('未连接物理手柄', { exact: true }).isVisible());
      await page.screenshot({path:path.join(out,view+'-healthy-'+height+'.png'),fullPage:true});
    });
    const actionable = view === 'controller' ? page.getByRole('button', { name: 'Steam Deck', exact: true }) : page.getByRole('button', { name: /虚拟手柄联动/ });
    await actionable.focus();
    await check('All 14 phases show only actual errors and preserve controller focus: ' + label, async () => {
      for (const p of ['checking', 'current', 'newer-preserved', 'not-installed', 'update-found', 'removing', 'installing', 'verifying', 'complete', 'admin-required', 'busy', 'reboot-required', 'failed', 'unverified']) {
        await page.evaluate(p => f.emit(p), p); await waitPhase(p);
        assert(await actionable.isEnabled()); assert(await actionable.evaluate(node => node === document.activeElement));
        assert.equal(await page.locator('[role="dialog"],[data-gp-modal],.usbip-prompt-backdrop').count(), 0);
        assert.equal(await page.locator('.usbip-upgrade-status button,.usbip-upgrade-status a,.usbip-upgrade-status [tabindex]').count(), 0);
        const error=['failed','admin-required','reboot-required'].includes(p);
        assert.equal(await page.locator('[role="status"]').count(), error?1:0);
        assert.equal(await page.locator('.usbip-upgrade-versions').count(), 0);
        if(error) assert.equal(await page.locator('[data-usbip-tone]').getAttribute('data-usbip-tone'),'warning');
      }
      await actionable.click(); assert((await page.evaluate(() => f.actions)).includes(view === 'controller' ? 'steamdeck' : 'link'));
    });
    await check('Only driverSetup updates status; no gamepad.state dependency: ' + label, async () => {
      await page.evaluate(() => { f.emit('failed'); for (let i = 0; i < 100; ++i) f.event('gamepad.state', f.snapshot('complete')); });
      await waitPhase('failed'); assert.equal(await phase(), 'failed');
    });
    await check('Unknown phases and malformed packets remove success and text is escaped: ' + label, async () => {
      await page.evaluate(() => f.emit('complete')); await waitPhase('complete');
      await page.evaluate(() => f.emit('future-success')); await waitPhase('unverified');
      assert.equal(await page.locator('[role="status"]').count(),0);
      await page.evaluate(() => f.emit('failed', { detail: '<img src=x onerror="window.bad=true">' })); await waitPhase('failed');
      assert.equal(await page.locator('.usbip-upgrade-status img').count(), 0); assert.equal(await page.evaluate(() => window.bad), undefined);
      await page.evaluate(() => f.event('gamepad.driverSetup', null)); await waitPhase('failed');
    });
    await check('Window hide/show performs one bounded read; repeated shown edge does not reread: ' + label, async () => {
      const before = await page.evaluate(() => f.requests.length);
      await page.evaluate(() => { f.event('window.hidden'); f.event('window.shown'); f.event('window.shown'); });
      await waitPhase('current'); assert.equal(await page.evaluate(() => f.requests.length), before + 1);
    });
    await check('KeepAlive deactivation and unmount release subscriptions and timers: ' + label, async () => {
      await page.evaluate(async () => { f.auto = false; f.event('window.hidden'); f.event('window.shown'); await show(false); });
      let counts = await page.evaluate(() => f.counts()); assert.equal(counts.status, 0); assert.equal(counts.visibility, 0); assert.equal(counts.timers, 0);
      const readCount = counts.reads;
      await page.evaluate(() => { f.emit('complete'); f.event('window.hidden'); f.event('window.shown'); });
      assert.equal(await page.evaluate(() => f.requests.length), readCount);
      await page.evaluate(async () => { f.auto = true; await show(true); }); await waitPhase('current');
      counts = await page.evaluate(() => f.counts()); assert.equal(counts.status, 1); assert.equal(counts.reads, readCount + 1);
      await page.evaluate(async () => { await unmount(); }); counts = await page.evaluate(() => f.counts());
      assert.equal(counts.status, 0); assert.equal(counts.visibility, 0); assert.equal(counts.timers, 0);
    });
  }
  await check('Settings keeps all 14 states and version details; successful older-version runtime stays silent inline', async () => {
    for(const height of [720,800]) {
      await page.setViewportSize({width:1280,height});
      await page.evaluate(async()=>{f.auto=true;f.session='settings-details-'+f.requests.length;f.sequence=0;await mount('settings')});
      for(const p of ['checking','current','newer-preserved','not-installed','update-found','removing','installing','verifying','complete','admin-required','busy','reboot-required','failed','unverified']) {
        await page.evaluate(p=>f.emit(p),p);await waitPhase(p);
        assert(await page.locator('[role="status"]').isVisible());assert.equal(await page.locator('.usbip-upgrade-versions').count(),1);
      }
      await page.evaluate(()=>f.emit('current'));await waitPhase('current');
      await page.screenshot({path:path.join(out,'settings-details-'+height+'.png'),fullPage:true});
      await page.evaluate(async()=>unmount());
    }
    for(const view of ['controller','gyro']) {
      await page.evaluate(async view=>{f.auto=true;f.session='working-older-'+view;f.sequence=0;await mount(view);
        f.emit('update-found',{installedVersion:'0.9.7.7',targetVersion:'0.9.8.1'});
        f.event('gamepad.state',{connected:true,virtualReady:true});},view);await waitPhase('update-found');
      assert.equal(await page.locator('[role="status"]').count(),0);
      await page.evaluate(()=>f.emit('failed',{detail:'USB/IP 安装失败，请导出日志。'}));await waitPhase('failed');
      assert.match(await page.locator('[role="status"]').innerText(),/USB.IP 安装失败/);
      await page.evaluate(()=>f.emit('current'));await waitPhase('current');assert.equal(await page.locator('[role="status"]').count(),0);
      await page.evaluate(async()=>unmount());
    }
  });
  await check('Settings re-entry and native focus refresh current inventory even when visibility remains true', async () => {
    await page.evaluate(async()=>{f.auto=true;f.session='focus-refresh';f.sequence=0;await mount('settings');});await waitPhase('current');
    await page.evaluate(()=>f.emit('failed',{detail:'USB/IP 升级已停止：Access to the path is denied'}));await waitPhase('failed');const before=await page.evaluate(()=>f.requests.length);
    await page.evaluate(()=>{for(let i=0;i<100;i++)f.event('window.focus');});await waitPhase('current');assert.equal(await page.evaluate(()=>f.requests.length),before+1);assert.equal(await page.evaluate(()=>f.requests.at(-1).args.refreshUsbip),true);
    await page.evaluate(()=>f.emit('failed'));await waitPhase('failed');await page.evaluate(async()=>show(false));assert.equal((await page.evaluate(()=>f.counts())).refreshListeners,0);await page.evaluate(async()=>show(true));await waitPhase('current');
    assert.equal(await page.evaluate(()=>f.requests.length),before+2);await page.evaluate(async()=>unmount());const counts=await page.evaluate(()=>f.counts());assert.equal(counts.refreshListeners,0);assert.equal(counts.timers,0);
    const stopped=counts.reads;await page.evaluate(()=>{f.event('window.focus');window.dispatchEvent(new Event('focus'));});assert.equal(await page.evaluate(()=>f.requests.length),stopped);
  });
  await check('Actual IPC 1500ms timeout ignores late complete and has no idle read loop', async () => {
    await page.evaluate(async () => { f.auto = false; await mount('settings'); });
    const index = await page.evaluate(() => f.requests.length - 1);
    await page.waitForFunction(() => document.querySelector('[role="status"]')?.textContent.includes('1500ms'));
    await page.evaluate(index => f.reply(index, f.snapshot('complete')), index);
    assert.equal(await phase(), 'unverified'); assert.equal((await page.evaluate(() => f.counts())).timers, 0);
    await page.waitForTimeout(1750); assert.equal(await page.evaluate(() => f.requests.length), index + 1);
  });
  await check('Event session and sequence fence actual late IPC, old sessions and lower sequences', async () => {
    await page.evaluate(() => { f.event('window.hidden'); f.event('window.shown'); });
    const index = await page.evaluate(() => f.requests.length - 1);
    await page.evaluate(() => { f.session = 'new-session'; f.sequence = 0; f.emit('installing'); }); await waitPhase('installing');
    await page.evaluate(index => f.reply(index, { driverSetupSession: 'old-session', usbipUpgrade: { schemaVersion: 1,
      phase: 'complete', sequence: 999, installedVersion: '1.0', targetVersion: '1.0', detail: '' } }), index);
    assert.equal(await phase(), 'installing');
    await page.evaluate(() => { f.emit('complete'); f.event('gamepad.driverSetup', { driverSetupSession: 'new-session',
      usbipUpgrade: { schemaVersion: 1, phase: 'checking', sequence: 0, installedVersion: '', targetVersion: '', detail: '' } }); });
    await waitPhase('complete');
    await page.evaluate(() => { f.session = 'latest-session'; f.sequence = 0; f.emit('checking'); f.event('gamepad.driverSetup', {
      driverSetupSession: 'new-session', usbipUpgrade: { schemaVersion: 1, phase: 'complete', sequence: 1000,
        installedVersion: '1.0', targetVersion: '1.0', detail: '' } }); }); await waitPhase('checking');
  });
  await check('Verified matching versions plus native uninstall restart detail never become complete or target-on', async () => {
    const actions = await page.evaluate(() => f.actions.length);
    await page.evaluate(() => f.emit('reboot-required', { installedVersion: '0.9.8', targetVersion: '0.9.8',
      detail: '配套版本已安装通过读取核对，卸载要求重启' })); await waitPhase('reboot-required');
    const text = await page.locator('[role="status"]').innerText();
    assert.match(text, /配套版本已安装通过读取核对，卸载要求重启/);
    assert.match(text, /重启 Windows/); assert.doesNotMatch(text, /升级完成|虚拟手柄已经启动/);
    assert.equal(await page.locator('[data-usbip-tone]').getAttribute('data-usbip-tone'), 'warning');
    assert.equal(await page.evaluate(() => f.actions.length), actions);
    await page.screenshot({ path: path.join(out, 'native-reboot-required-1280x800.png'), fullPage: true });
  });
  await check('Representative recovery states fit 1280x720/800 without horizontal overflow', async () => {
    for (const height of [720, 800]) for (const view of ['controller', 'gyro']) {
      await page.setViewportSize({ width: 1280, height });
      await page.evaluate(async view => { f.auto = true; f.session = 'screenshot-' + view + f.requests.length; f.sequence = 0; await mount(view); f.emit('failed'); }, view);
      await waitPhase('failed');
      assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
      const rect = await page.locator('[role="status"]').boundingBox(); assert(rect && rect.y >= 0 && rect.y + rect.height <= height);
      await page.screenshot({ path: path.join(out, view + '-1280x' + height + '.png'), fullPage: true });
    }
  });
  await check('Review counterexample 1: checking/installing watchdog expires without renewing on duplicate events or visibility reads', async () => {
    const beforeActions = await page.evaluate(() => f.actions.length);
    await page.evaluate(async () => { f.auto = false; f.session = 'review-watchdog'; f.sequence = 0; await mount('settings');
      f.held = f.snapshot('checking'); f.reply(f.requests.length - 1, f.held); }); await waitPhase('checking');
    await page.evaluate(() => { f.tickProgress(179000); f.event('gamepad.driverSetup', f.held); f.event('window.hidden'); f.event('window.shown');
      f.reply(f.requests.length - 1, f.held); }); await waitPhase('checking');
    const reads = await page.evaluate(() => f.requests.length);
    await page.evaluate(() => f.tickProgress(2000)); await waitPhase('unverified');
    assert.match(await page.locator('[role="status"]').innerText(), /原任务仍在后台/);
    await page.evaluate(() => { f.event('gamepad.driverSetup', f.held); f.tickProgress(86400000); });
    assert.equal(await phase(), 'unverified'); assert.equal((await page.evaluate(() => f.counts())).timers, 0);
    assert.equal(await page.evaluate(() => f.requests.length), reads);
    await page.evaluate(() => f.emit('installing')); await waitPhase('installing');
    await page.evaluate(() => f.tickProgress(180001)); await waitPhase('unverified');
    assert.equal(await page.evaluate(() => f.actions.length), beforeActions);
    await page.evaluate(async () => unmount());
  });
  await check('Review counterexample 2: bad packets preserve verified evidence and authoritative same-sequence read repairs attributed damage', async () => {
    await page.evaluate(async () => { f.auto = false; f.session = 'review-authoritative'; f.sequence = 1; await mount('settings');
      f.held = f.snapshot('complete'); f.reply(f.requests.length - 1, f.held); }); await waitPhase('complete');
    await page.evaluate(() => f.event('gamepad.driverSetup', null)); assert.equal(await phase(), 'complete');
    await page.evaluate(() => f.event('gamepad.driverSetup', { ...f.held, usbipUpgrade: { ...f.held.usbipUpgrade, phase: 'future-phase' } })); await waitPhase('unverified');
    await page.evaluate(() => f.event('gamepad.driverSetup', f.held)); assert.equal(await phase(), 'unverified');
    await page.evaluate(() => { f.event('window.hidden'); f.event('window.shown'); f.reply(f.requests.length - 1, f.held); }); await waitPhase('complete');
  });
  await check('Review counterexample 3: invalid schema/new-session and absent session cannot retire the verified session', async () => {
    await page.evaluate(() => {
      f.event('gamepad.driverSetup', { driverSetupSession: 'invalid-B', usbipUpgrade: { schemaVersion: 99, sequence: 999 } });
      f.event('gamepad.driverSetup', { ...f.held, driverSetupSession: '' });
      f.event('gamepad.driverSetup', { ...f.held, usbipUpgrade: { ...f.held.usbipUpgrade, schemaVersion: 99, sequence: 1000 } });
    }); assert.equal(await phase(), 'complete');
    await page.evaluate(() => { f.event('window.hidden'); f.event('window.shown'); f.reply(f.requests.length - 1, f.snapshot('complete')); }); await waitPhase('complete');
  });
  await check('Review counterexample 4: higher same-session read completes despite an intervening duplicate event', async () => {
    await page.evaluate(async () => { f.auto = false; f.session = 'review-read-order'; f.sequence = 1; await mount('settings');
      f.held = f.snapshot('installing'); f.reply(f.requests.length - 1, f.held); }); await waitPhase('installing');
    await page.evaluate(() => { f.event('window.hidden'); f.event('window.shown'); f.event('gamepad.driverSetup', f.held);
      f.reply(f.requests.length - 1, f.snapshot('complete')); }); await waitPhase('complete');
    assert.equal((await page.evaluate(() => f.counts())).timers, 0);
  });
  await check('KeepAlive stops all watchdog resources and reactivation retains the original progress deadline', async () => {
    await page.evaluate(async () => { f.auto = false; f.session = 'review-keepalive'; f.sequence = 1; await mount('settings');
      f.held = f.snapshot('installing'); f.reply(f.requests.length - 1, f.held); }); await waitPhase('installing');
    await page.evaluate(async () => { f.tickProgress(60000); f.shiftWallClock(-3600000); await show(false); });
    let counts = await page.evaluate(() => f.counts()); assert.equal(counts.status, 0); assert.equal(counts.visibility, 0); assert.equal(counts.timers, 0); assert.equal(counts.abortCallbacks, 0);
    await page.evaluate(async () => { f.tickProgress(60000); await show(true); f.reply(f.requests.length - 1, f.held); }); await waitPhase('installing');
    await page.evaluate(() => f.tickProgress(61000)); await waitPhase('unverified');
    await page.evaluate(async () => { f.shiftWallClock(3600000); await unmount(); }); counts = await page.evaluate(() => f.counts()); assert.equal(counts.timers, 0); assert.equal(counts.abortCallbacks, 0);
  });
  await check('Actual component and IPC 200 lifecycle cycles release watchdog/read timers, subscriptions and abort callbacks', async () => {
    receipt.actualIpc200 = await page.evaluate(async () => {
      const first = f.requests.length, actions = f.actions.length; let watchdogCycles = 0, pendingReadCycles = 0;
      for (let i = 0; i < 200; ++i) {
        const view=['settings','controller','gyro'][i%3];
        f.auto = false; f.session = 'ipc200-' + i; f.sequence = 0; await mount(view);
        if (i % 2 === 0) { f.reply(f.requests.length - 1, f.snapshot('installing')); for (let j = 0; j < 8; ++j) await Promise.resolve(); }
        const running = f.counts();
        if (i % 2 === 0) {
          if (running.timers !== 1 || running.abortCallbacks !== 0 || (view==='settings' ? document.querySelector('[data-usbip-phase]')?.getAttribute('data-usbip-phase')!=='installing' : !!document.querySelector('[data-usbip-phase]'))) throw Error('Watchdog was not armed before stop at cycle ' + i + ': ' + JSON.stringify(running));
          ++watchdogCycles;
        } else {
          if (running.timers !== 2 || running.abortCallbacks !== 1) throw Error('Read was not pending before stop at cycle ' + i);
          ++pendingReadCycles;
        }
        await unmount(); const c = f.counts();
        if (c.status || c.visibility || c.timers || c.abortCallbacks || c.refreshListeners) throw Error('Resource leak at cycle ' + i + ': ' + JSON.stringify(c));
      }
      const last = f.requests.length;
      for (let i = first; i < last; ++i) f.reply(i, f.snapshot('complete'));
      f.emit('complete'); f.tickProgress(86400000);
      if (f.requests.length !== last || f.actions.length !== actions || document.querySelector('[data-usbip-phase]')) throw Error('Late callback revived component or caused an action');
      return { cycles: 200, watchdogCycles, pendingReadCycles, lateReplies: last - first, resources: f.counts(), nativeListeners: nativeListeners.length };
    });
    assert.equal(receipt.actualIpc200.watchdogCycles, 100); assert.equal(receipt.actualIpc200.pendingReadCycles, 100);
    assert.equal(receipt.actualIpc200.lateReplies, 200); assert.equal(receipt.actualIpc200.nativeListeners, 1);
  });
  await page.evaluate(async () => unmount());
  const final = await page.evaluate(() => f.counts()); assert.equal(final.status, 0); assert.equal(final.visibility, 0); assert.equal(final.timers, 0);
  receipt.finalResources = final; assert.deepEqual(receipt.errors, []); receipt.status = 'PASS';
} catch (error) { receipt.status = 'FAIL'; receipt.error = error.stack; process.exitCode = 1; }
finally { if (browser) await browser.close(); stop(); receipt.completedAt = new Date().toISOString(); save(); }
console.log(`${receipt.status}: ${receipt.checks.length} checks; ${out}`);
