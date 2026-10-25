import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
import { createServer } from 'node:http';
import { build } from 'esbuild';

const root = process.cwd();
const require = createRequire(path.join(root, 'package.json'));
const { parse, compileScript, compileStyle } = require('vue/compiler-sfc');
const { chromium } = createRequire('C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/_mode-test.cjs')('playwright');
const out = path.resolve(process.argv[2] || 'G:/YeManCC-Work/Deliverables/Steam-Diagnostics-Move-20261020/standalone');
fs.mkdirSync(out, { recursive: true });

const mocks = {
  '@/bridge/sleepRecovery': `export const getAutomaticSleepRecovery=async()=>({ok:true,enabled:true,phase:'idle',generation:0,healthVerified:false});export const setAutomaticSleepRecovery=async(enabled)=>{window.__fixture.recoveryWrites++;return {ok:true,enabled,phase:'idle',generation:0,healthVerified:false}};`,
  '@/bridge/ipc': `export const on=()=>()=>{};`,
  '@/bridge/yeman': `
    export const STEAM_ADDONS=[];
    export const steamAddonExists=async()=>false;
    export const steamAddonSet=async()=>{};
    export const steamCustomAddonAdd=async()=>({});
    export const steamCustomAddonRemove=async()=>{};
    export const steamCustomAddonSet=async()=>{};
    export const steamCustomAddons=async()=>[];
    export const steamExeExists=async()=>true;
    export const launchSteam=async()=>{window.__fixture.launches++;return {ok:true}};
  `,
  '@/bridge/api': `export const dialog={openFile:async()=>null,confirm:async()=>true};export const shell={execute:async()=>{throw Error('unexpected shell write')}}`,
  '@/bridge/uiLifecycle': `export const isUiVisible=()=>true;export const getUiVisibilityState=()=>({documentVisible:true,nativeWindowVisible:true,powerReady:true});export const onUiVisibilityChange=()=>()=>{}`,
  '@/bridge/deckySidebar': `export const deckySidebar={get:async()=>({enabled:true,revision:1,phase:'ready'}),subscribe:()=>()=>{},setEnabled:async()=>({enabled:true,revision:2,phase:'ready'}),environment:async()=>({})};export const deckySidebarDescription=()=>'';export const deckySidebarSetupFailure=()=>''`,
  '@/bridge/steamAppearance': `export const steamAppearance={start:async()=>({backendReady:true})}`,
  '@/bridge/steamUi': `export const steamUi={read:async()=>({known:true,running:true,mode:'bigpicture',pid:111,birth:'222',reason:''}),subscribe:()=>()=>{},switchMode:async()=>{window.__fixture.modeSwitches++;return {ok:true}}}`,
  '@/bridge/steamGameLink': `export async function getSteamGameLink(){window.__fixture.linkReads++;return JSON.parse(JSON.stringify(window.__fixture.link))}`,
  '@/bridge/customSteamLibrary': `export const readCustomSteamLibrarySummary=async()=>({waiting:0,joined:0,needs:0,excluded:0});export const launchCustomSteamLibrary=async()=>({ok:true})`,
  '@/components/SteamAppearanceCard.vue': `export default {template:'<div data-fixture="appearance"></div>'}`,
  '@/components/InlineIcon.vue': `export default {props:['name'],template:'<span aria-hidden="true"></span>'}`,
  '@/components/Toggle.vue': `export default {props:['modelValue','label'],template:'<div class="fixture-toggle">{{label}}</div>'}`,
};

const styles = [];
const entry = `import {createApp} from 'vue';import Diagnostics from './src/components/SteamRecoveryDiagnostics.vue';createApp(Diagnostics).mount('#app');`;
const result = await build({
  stdin: { contents: entry, loader: 'ts', resolveDir: root },
  bundle: true,
  write: false,
  platform: 'browser',
  format: 'iife',
  alias: { '@': path.join(root, 'src') },
  define: { 'process.env.NODE_ENV': '"production"' },
  plugins: [{
    name: 'steam-contact-fixture',
    setup(buildApi) {
      buildApi.onResolve({ filter: /.*/ }, (args) => args.path in mocks ? { path: args.path, namespace: 'fixture' } : undefined);
      buildApi.onLoad({ filter: /.*/, namespace: 'fixture' }, (args) => ({ contents: mocks[args.path], loader: 'ts' }));
      buildApi.onLoad({ filter: /\.vue$/ }, (args) => {
        const { descriptor, errors } = parse(fs.readFileSync(args.path, 'utf8'), { filename: args.path });
        assert.equal(errors.length, 0);
        const id = 'data-v-steam-contact';
        const code = compileScript(descriptor, { id, inlineTemplate: true });
        for (const style of descriptor.styles) styles.push(compileStyle({ source: style.content, filename: args.path, id, scoped: style.scoped }).code);
        return { contents: code.content.replace('export default', 'const component=') + `;component.__scopeId=${JSON.stringify(id)};export default component;`, loader: 'ts', resolveDir: path.dirname(args.path) };
      });
    },
  }],
});

const css = fs.readFileSync('src/styles/tokens.css', 'utf8') + '\n' + styles.join('\n') + '\nbody{margin:0;padding:12px;background:#0b0e13;color:#f2f4fa;font-family:Segoe UI,sans-serif}.page{display:grid;gap:12px}.card{padding:14px;border-radius:14px;background:#141b26}';
const html = `<!doctype html><html><head><meta charset="utf-8"><style>${css}</style></head><body><div id="app"></div><script>window.__fixture={link:{schemaVersion:1,phase:'observed',reason:'fixture',readOnly:true,linked:false,transportConnected:true,active:true,fresh:true,repairAuthorized:false,gameInputVerified:false,continueButtonVerified:false,steamWrites:0,gameLaunches:0,focusWrites:0,deviceWrites:0,sleepWrites:0},linkReads:0,recoveryWrites:0,launches:0,modeSwitches:0};</script><script src="/fixture.js"></script></body></html>`;
const server = createServer((req, res) => {
  res.setHeader('Content-Type', req.url === '/fixture.js' ? 'text/javascript' : 'text/html; charset=utf-8');
  res.end(req.url === '/fixture.js' ? result.outputFiles[0].text : html);
});
await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
let browser;
const cases = [];
const errors = [];
try {
  browser = await chromium.launch({ headless: true, executablePath: 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe' });
  const page = await browser.newPage({ viewport: { width: 720, height: 1000 } });
  page.on('pageerror', (error) => errors.push(error.message));
  await page.goto(`http://127.0.0.1:${server.address().port}`);
  await page.waitForFunction(() => document.querySelector('.steam-recovery-diagnostics')?.textContent.includes('已连通'));
  const initial = await page.locator('.steam-recovery-diagnostics').innerText();
  assert.match(initial, /Steam 传输[\s\S]*已连通/);
  assert.match(initial, /原实例联合[\s\S]*未确认/);
  assert.doesNotMatch(initial, /Steam 已恢复|手柄输入已成功|Continue 已成功/);
  assert.equal(await page.locator('.recovery-link-states button, .recovery-link-states input, .recovery-link-states select, .recovery-link-states a').count(), 0);
  cases.push('transportConnected and linked are shown separately in a non-focusable read-only area');

  const readsBeforeManual = await page.evaluate(() => window.__fixture.linkReads);
  await page.getByRole('button',{name:'检查状态',exact:true}).click();
  await page.waitForFunction((before) => window.__fixture.linkReads > before, readsBeforeManual);
  cases.push('diagnostics status button refreshes only the read-only contact');

  await page.evaluate(() => {
    window.__fixture.link = {...window.__fixture.link, linked:true, transportConnected:false, fresh:false};
    window.dispatchEvent(new Event('ipc:power.resumed'));
  });
  await page.waitForFunction(() => document.querySelector('.steam-recovery-diagnostics')?.textContent.includes('当前联系状态待复核'));
  const stale = await page.locator('.steam-recovery-diagnostics').innerText();
  assert.match(stale, /Steam 传输[\s\S]*待复核/);
  assert.match(stale, /原实例联合[\s\S]*待复核/);
  assert.match(stale, /旧联系不会继续沿用/);
  cases.push('fresh=false suppresses both stale values and explains that the old contact is not reused');

  await page.evaluate(() => {
    window.__fixture.link = {...window.__fixture.link, linked:true, transportConnected:true, fresh:true};
    window.dispatchEvent(new Event('ipc:power.resumed'));
  });
  await page.waitForFunction(() => {
    const text = document.querySelector('.steam-recovery-diagnostics')?.textContent || '';
    return text.includes('已连通') && text.includes('已联合确认');
  });
  cases.push('a later read updates both independent contact fields without changing Steam mode');
  assert.equal(await page.evaluate(() => window.__fixture.launches + window.__fixture.modeSwitches), 0);
  assert.ok(await page.evaluate(() => window.__fixture.linkReads >= 3));
  assert.equal(await page.evaluate(() => window.__fixture.recoveryWrites),0);
  cases.push('read-only contact refresh performs no Steam launch or mode write');
  await page.screenshot({ path: path.join(out, 'steam-contact-ui.png') });
  assert.equal(errors.length, 0);
  const record = { ok: true, passed: cases.length, cases, errors, scope: 'Actual SteamRecoveryDiagnostics with memory-only steamGameLink/sleepRecovery fixtures; no live Steam, game input, or Continue claim' };
  fs.writeFileSync(path.join(out, 'steam-contact-ui.json'), JSON.stringify(record, null, 2));
  console.log(JSON.stringify(record, null, 2));
} finally {
  await browser?.close();
  await new Promise((resolve) => server.close(resolve));
}
