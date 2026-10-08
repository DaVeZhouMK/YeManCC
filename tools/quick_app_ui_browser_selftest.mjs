// Render the real QuickAppView in Chromium; all storage/native operations are mocked.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
import { createServer } from 'node:http';
import { createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const req = createRequire(path.join(root, 'package.json'));
const { build } = req('esbuild');
const { parse, compileScript, compileStyle } = req('vue/compiler-sfc');
const runtime = process.env.YMCC_UI_BROWSER_MODULE_DIR || 'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const { chromium } = createRequire(path.join(runtime, '_quick-app-ui-selftest.cjs'))('playwright');
const out = process.env.YMCC_UI_BROWSER_OUT || path.resolve(root, '../../Build/Validation/quick-app-ui-20261006');
fs.mkdirSync(out, { recursive: true });
const source = fs.readFileSync(path.join(root, 'src/views/QuickAppView.vue'), 'utf8');
for (const handler of ['onLaunchLs', 'onOptiScalerCurrent', 'onSpeedApply', 'onSpeedOff']) {
  assert(source.includes(`async function ${handler}(`), `Hidden function implementation was removed: ${handler}`);
}
const styles = [];
const mocks = {
  '@/bridge/quickapp': `
    export const LS_PRIMARY='C:/fixture/LS.exe',OPTISCALER_CLIENT_DIR='C:/fixture/OPT',OPTISCALER_CLIENT_EXE='OPT.exe',OPTISCALER_CLIENT_URL='about:blank';
    export const oneClickFrameGen=async()=>{},oneClickOptiScaler=async()=>{},dirnameOf=p=>p.slice(0,p.lastIndexOf('/')),
      optiscalerAnalyze=async()=>({}),optiConsoleInstalled=async()=>false,openOptiConsole=async()=>{};
  `,
  '@/bridge/gamedetect': `import {ref} from 'vue';export const detectedGameName=ref('');export async function detectGame(){return null};export async function refreshGameStatus(){return null};export function subscribeGameStatus(cb){cb(null);return ()=>{}}`,
  '@/bridge/speedhack': `export const SPEED_PRESETS=[1,1.5,2,3,5,10];export const applyGameSpeed=async()=>{},clearGameSpeed=async()=>{},isMinecraftTarget=()=>false`,
  '@/bridge/gameproc': `export const closeGame=async()=>{},waitForProcessExit=async()=>{}`,
  '@/bridge/quickActionLock': `export function tryAcquireQuickAction(){return {release(){}}}`,
  '@/bridge/api': `
    export const fs={getFileIcon:async(p)=>{
      const f=window.__fixture;f.iconReads??=[];f.iconReads.push(p);f.activeIcons=(f.activeIcons||0)+1;f.maxActiveIcons=Math.max(f.maxActiveIcons||0,f.activeIcons);
      try{if(f.holdIconPaths?.includes(p))await new Promise(r=>(f.releaseIcons??={})[p]=r);if(f.failIconPaths?.includes(p))throw Error('mock icon read denied');return f.iconResults?.[p]??null;}finally{f.activeIcons--;}
    },exists:async(p)=>Object.hasOwn(window.__fixture.files,p),readTextFile:async(p)=>{if((window.__fixture.failRead&&p===window.__settingsFile)||window.__fixture.readDeniedPaths?.includes(p))throw Error('mock read denied');if(!Object.hasOwn(window.__fixture.files,p))throw Error('ENOENT '+p);return window.__fixture.files[p]},rename:async(a,b)=>{window.__fixture.files[b]=window.__fixture.files[a];delete window.__fixture.files[a];return true}};
    export const settingsStore={write:async(p,content)=>{const f=window.__fixture;if(f.holdSave)await new Promise(r=>f.releaseSave=r);if(f.failSave)return false;const doc=JSON.parse(content);if(f.files[p])f.files[p+'.bak']=f.files[p];f.files[p]=content;sessionStorage.setItem('quick-app-test-disk',JSON.stringify(f.files));f.apps=doc.quickApps.apps;f.writes.push(doc);return true}};
    export const shell={execute:async(p,args)=>{window.__fixture.executions.push({path:p,args})},open:async()=>{},run:async()=>({stdout:''})};
    export const dialog={openFile:async()=>window.__fixture.picked};
    export const registry={read:async()=>96,write:async()=>true};
    export const display={getModes:async()=>({current:'fixture',modes:[{id:'fixture',width:1920,height:1080,refresh:60,orientation:0}]}),setMode:async(m)=>m,setTopology:async()=>{}};
  `,
  '@/bridge/music': `
    import {ref} from 'vue';export const folder=ref(''),baseUrl=ref(''),tracks=ref([]),index=ref(0),playing=ref(false),mode=ref('order'),error=ref(''),currentName=ref(''),hasFolder=ref(false),volume=ref(1),muted=ref(false);
    export const chooseFolder=async()=>{},scanFolder=async()=>{},togglePlay=()=>{},playNext=()=>{},playPrev=()=>{},setMode=v=>mode.value=v,setVolume=v=>volume.value=v,persistVolume=()=>{},toggleMute=()=>muted.value=!muted.value;
  `,
  '@/gamepad/focus': `export const focusGamepadElement=n=>n?.focus();export function getGamepadPopupPlacement(rect,width,height){return {style:{position:'fixed',left:Math.max(8,Math.min(rect.left,window.innerWidth-width-8))+'px',top:Math.max(8,Math.min(rect.bottom+8,window.innerHeight-height-8))+'px'},above:false}}`,
};
const bundle = await build({
  stdin: { resolveDir: root, loader: 'ts', contents: `
    import {createApp,h,KeepAlive,reactive} from 'vue';import Page from './src/views/QuickAppView.vue';import * as repository from './src/bridge/settingsRepository';import {getQuickAppFileIcon} from './src/bridge/quickAppFileIcons';window.__getQuickAppFileIcon=getQuickAppFileIcon;import {quickAppIconVariant} from './src/bridge/quickAppIcons';window.__repository=repository;window.__reactive=reactive;window.__quickAppIconVariant=quickAppIconVariant;window.__settingsFile=repository.SETTINGS_FILE;
    // Synthetic six-item fixture only: tests saved-list preservation, never used as product defaults.
    window.__fixture={apps:Array.from({length:6},(_,i)=>({name:'测试应用 '+(i+1),path:'C:/fixture/app'+(i+1)+'.exe',futureField:{keep:i}})),writes:[],executions:[],picked:null,iconResults:JSON.parse(sessionStorage.getItem('quick-app-test-icons')||'{}')};
    const savedDisk=sessionStorage.getItem('quick-app-test-disk');window.__fixture.files=savedDisk?JSON.parse(savedDisk):{[repository.SETTINGS_FILE]:JSON.stringify(repository.normalizeSettings({quickApps:{apps:window.__fixture.apps}}))};
    let app;window.__mountPage=()=>{app=createApp({render:()=>h(KeepAlive,null,{default:()=>h(Page)})});app.mount('#app')};window.__remountPage=()=>{app.unmount();window.__mountPage()};window.__mountPage();
  ` },
  bundle: true, write: false, format: 'iife', platform: 'browser',
  alias: { '@': path.join(root, 'src') }, define: { 'process.env.NODE_ENV': '"production"' },
  plugins: [{ name: 'quick-app-ui-fixture', setup(b) {
    b.onResolve({ filter: /.*/ }, args => {if(args.path==='./api'&&['settingsRepository.ts','quickAppFileIcons.ts'].some(name=>args.importer.endsWith(name)))return {path:'@/bridge/api',namespace:'fixture'};return args.path in mocks ? { path: args.path, namespace: 'fixture' } : null;});
    b.onLoad({ filter: /.*/, namespace: 'fixture' }, args => ({ contents: mocks[args.path], loader: 'ts', resolveDir: root }));
    b.onLoad({ filter: /\.vue$/ }, args => {
      const { descriptor, errors } = parse(fs.readFileSync(args.path, 'utf8'), { filename: args.path });
      assert.equal(errors.length, 0);
      const id = 'data-v-' + createHash('sha256').update(args.path).digest('hex').slice(0, 8);
      const compiled = compileScript(descriptor, { id, inlineTemplate: true });
      for (const style of descriptor.styles) {
        const css = compileStyle({ source: style.content, filename: args.path, id, scoped: style.scoped });
        assert.equal(css.errors.length, 0); styles.push(css.code);
      }
      return { contents: compiled.content.replace('export default', 'const _component =') + `\n_component.__scopeId=${JSON.stringify(id)};export default _component;`, loader: 'ts', resolveDir: path.dirname(args.path) };
    });
  } }],
});
const js = bundle.outputFiles[0].text;
const css = fs.readFileSync(path.join(root, 'src/styles/tokens.css'), 'utf8') + '\n' + styles.join('\n');
const html = `<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>Quick app UI isolated validation</title><style>${css}\nbody{margin:0;background:var(--bg-solid);padding:16px;font-family:system-ui,sans-serif;}#app{max-width:760px;margin:0 auto}.card{padding:16px;margin-bottom:16px;border:1px solid var(--border);border-radius:var(--radius-card);background:var(--bg-panel)}</style><main id="app"></main><script src="/fixture.js"></script></html>`;
const server = createServer((request, response) => {
  response.setHeader('Content-Type', request.url === '/fixture.js' ? 'text/javascript; charset=utf-8' : 'text/html; charset=utf-8');
  response.end(request.url === '/fixture.js' ? js : html);
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const cases = [], screenshots = [], errors = [];
let browser;
try {
  browser = await chromium.launch({ headless: true, executablePath: process.env.YMCC_UI_BROWSER_EXE || 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe' });
  const page = await browser.newPage();
  page.on('pageerror', error => errors.push(error.message));
  for (const [width, height] of [[580,800], [420,800], [1280,720]]) {
    await page.setViewportSize({ width, height });
    await page.goto(`http://127.0.0.1:${server.address().port}`);
    await page.waitForFunction(() => document.querySelectorAll('.launch-card').length === 6);
    assert.equal(await page.locator('.quick-functions-card').count(), 0, 'Hidden quick-function card must not be mounted');
    assert.equal(await page.locator('.speed-grid').count(), 0, 'Hidden speed buttons must not be mounted');
    assert.equal(await page.getByRole('button', { name: /小黄鸭|FSR4|XeSS|OPT自动导入/ }).count(), 0);
    cases.push(`${width}x${height}: quick functions/game speed absent from DOM and keyboard/controller reachability`);
    const layout = await page.evaluate(() => {
      const apps = document.querySelector('.launch-apps-card'), music = [...document.querySelectorAll('.card-title')].find(n => n.textContent.includes('音乐播放'))?.closest('section');
      return { firstCard: document.querySelector('.page > section') === apps, appsBottom: apps.getBoundingClientRect().bottom, musicTop: music.getBoundingClientRect().top, overflow: document.documentElement.scrollWidth > window.innerWidth };
    });
    assert(layout.firstCard && layout.appsBottom < layout.musicTop, 'Custom apps must be first in DOM AND visually above music');
    assert.equal(layout.overflow, false, 'Page overflowed horizontally');
    assert.equal(await page.evaluate(() => window.__fixture.writes.length), 0, 'Reordering page must not overwrite user app list');
    cases.push(`${width}x${height}: six existing apps preserved above music, no load-time write/overflow`);
    const screenshot = path.join(out, `quick-app-${width}x${height}.png`);
    await page.screenshot({ path: screenshot, fullPage: true }); screenshots.push(screenshot);
  }
  await page.setViewportSize({ width: 580, height: 800 });
  await page.locator('.launch-card').first().focus();
  await page.locator('.launch-card').first().press('Enter');
  await page.waitForSelector('.launch-menu');
  assert.equal(await page.locator('.launch-menu-item').count(), 4);
  await page.getByRole('button', { name: '启动', exact: true }).click();
  assert.deepEqual(await page.evaluate(() => window.__fixture.executions), [{ path: 'C:/fixture/app1.exe', args: [] }]);
  cases.push('Existing app keyboard activation/menu launch uses unchanged executable path');
  await page.evaluate(() => { window.__fixture.picked = 'C:/fixture/added.exe'; });
  await page.getByRole('button', { name: '+ 添加应用', exact: true }).click();
  await page.waitForFunction(() => document.querySelectorAll('.launch-card').length === 7);
  assert.equal(await page.evaluate(() => window.__fixture.writes.length), 1);
  assert.equal(await page.evaluate(async()=> (await window.__repository.readSettingsSection('quickApps')).apps.length),7,'Saved reactive apps must remain readable from repository cache');
  const preserved = await page.evaluate(() => window.__fixture.apps.slice(0, 6));
  assert.deepEqual(preserved, Array.from({ length: 6 }, (_, i) => ({ name: '测试应用 ' + (i + 1), path: 'C:/fixture/app' + (i + 1) + '.exe', futureField: { keep: i } })));
  cases.push('Add app retains all six saved entries and unknown fields, writes once');
  await page.getByRole('button', { name: '+ 添加应用', exact: true }).click();
  assert.equal(await page.locator('.launch-card').count(), 7);
  assert.equal(await page.evaluate(() => window.__fixture.writes.length), 1);
  cases.push('Duplicate add remains rejected without extra write');
  await page.evaluate(() => { window.prompt = () => '改名测试'; });
  await page.locator('.launch-card').first().click();
  await page.getByRole('button', { name: '重命名', exact: true }).click();
  assert.equal(await page.locator('.launch-name').first().textContent(), '改名测试');
  assert.deepEqual(await page.evaluate(() => window.__fixture.apps[0].futureField), { keep: 0 });
  assert.equal(await page.evaluate(()=>JSON.parse(window.__fixture.files[window.__settingsFile]).quickApps.apps[0].name),'改名测试');
  cases.push('Rename preserves path and unknown app fields and is durably saved');
  await page.locator('.launch-card').last().click();
  await page.getByRole('button', { name: '删除', exact: true }).click();
  await page.waitForFunction(() => document.querySelectorAll('.launch-card').length === 6);
  assert.equal(await page.evaluate(()=>JSON.parse(window.__fixture.files[window.__settingsFile]).quickApps.apps.length),6);
  cases.push('Delete app still works after card relocation and is durably saved');
  await page.evaluate(() => {const doc=JSON.parse(window.__fixture.files[window.__settingsFile]);doc.quickApps.apps=[];window.__fixture.files[window.__settingsFile]=JSON.stringify(doc);window.__repository.clearSettingsCache();window.__remountPage();});
  await page.waitForFunction(() => document.querySelectorAll('.launch-card').length === 0);
  assert.equal(await page.getByRole('button', { name: '+ 添加应用', exact: true }).count(), 1);
  cases.push('Explicitly empty saved app list stays empty, add entry stays accessible');
  // Exercise the actual repository with Vue arrays and a disk surviving F5.
  for (let i=1;i<=6;i++) {
    await page.evaluate(i=>{window.__fixture.picked='C:/fixture/new'+i+'.exe';},i);
    await page.getByRole('button',{name:'+ 添加应用',exact:true}).click();
    await page.waitForFunction(i=>document.querySelectorAll('.launch-card').length===i,i);
    assert.equal(await page.evaluate(()=>JSON.parse(window.__fixture.files[window.__settingsFile]).quickApps.apps.length),i);
    assert.equal(await page.evaluate(async()=> (await window.__repository.readSettingsSection('quickApps')).apps.length),i);
  }
  const savedSix=await page.evaluate(()=>JSON.parse(window.__fixture.files[window.__settingsFile]).quickApps.apps);
  cases.push('Six consecutive adds remain durable/readable through real Vue/settings cache, no DataCloneError');
  await page.reload();
  await page.waitForFunction(()=>document.querySelectorAll('.launch-card').length===6);
  assert.deepEqual(await page.evaluate(async()=> (await window.__repository.readSettingsSection('quickApps')).apps),savedSix);
  cases.push('Real page reload/F5 recovers all six from persisted disk, not recreated fixture defaults');
  await page.evaluate(async()=>{
    await window.__repository.saveSettingsSection('ui',{quickAppPersistenceProbe:{keep:true}});
    structuredClone(await window.__repository.loadSettings());
  });
  assert.deepEqual(await page.evaluate(()=>JSON.parse(window.__fixture.files[window.__settingsFile]).quickApps.apps),savedSix);
  cases.push('Later unrelated section save/clone does not wipe six applications or poison cache');

  const diskBeforeFailure=await page.evaluate(()=>window.__fixture.files[window.__settingsFile]);
  await page.evaluate(()=>{window.__fixture.failSave=true;window.__fixture.picked='C:/fixture/not-saved.exe';});
  await page.getByRole('button',{name:'+ 添加应用',exact:true}).click();
  await page.waitForFunction(()=>document.querySelector('.err-bar')?.textContent.includes('保存自定义应用失败'));
  assert.equal(await page.locator('.launch-card').count(),6);
  assert.equal(await page.locator('.ok-bar').count(),0);
  assert.equal(await page.evaluate(()=>window.__fixture.files[window.__settingsFile]),diskBeforeFailure);
  cases.push('Failed add reports error, keeps old UI/disk and never claims success');
  const nameBefore=await page.locator('.launch-name').first().textContent();
  await page.evaluate(()=>{window.prompt=()=> '未保存改名';});
  await page.locator('.launch-card').first().click();
  await page.getByRole('button',{name:'重命名',exact:true}).click();
  await page.waitForFunction(()=>document.querySelector('.err-bar')?.textContent.includes('保存自定义应用失败'));
  assert.equal(await page.locator('.launch-name').first().textContent(),nameBefore);
  assert.equal(await page.evaluate(()=>window.__fixture.files[window.__settingsFile]),diskBeforeFailure);
  assert.equal(await page.locator('.ok-bar').count(),0);
  cases.push('Failed rename preserves old name/path/disk and never claims success');
  await page.locator('.launch-card').first().click();
  await page.getByRole('button',{name:'删除',exact:true}).click();
  await page.waitForFunction(()=>document.querySelector('.err-bar')?.textContent.includes('保存自定义应用失败'));
  assert.equal(await page.locator('.launch-card').count(),6);
  assert.equal(await page.evaluate(()=>window.__fixture.files[window.__settingsFile]),diskBeforeFailure);
  assert.equal(await page.locator('.ok-bar').count(),0);
  cases.push('Failed delete preserves app and disk, no phantom deletion');

  await page.evaluate(()=>{window.__fixture.failSave=false;window.__fixture.holdSave=true;});
  await page.getByRole('button',{name:'+ 添加应用',exact:true}).click();
  await page.waitForFunction(()=>typeof window.__fixture.releaseSave==='function');
  assert.equal(await page.locator('.launch-card').count(),6);
  assert.equal(await page.getByRole('button',{name:'+ 添加应用',exact:true}).isDisabled(),true);
  assert.equal(await page.locator('.launch-card:disabled').count(),6);
  assert.equal(await page.locator('.ok-bar').count(),0);
  await page.getByRole('button',{name:'+ 添加应用',exact:true}).evaluate(node=>node.click());
  await page.evaluate(()=>{window.__fixture.holdSave=false;window.__fixture.releaseSave();});
  await page.waitForFunction(()=>document.querySelectorAll('.launch-card').length===7);
  assert.equal(await page.evaluate(()=>JSON.parse(window.__fixture.files[window.__settingsFile]).quickApps.apps.length),7);
  cases.push('Pending save blocks duplicate/add/menu operations and updates UI only after write ACK');

  // Access failure must NOT archive a valid file or initialize [] over it.
  const savedSevenFile=await page.evaluate(()=>window.__fixture.files[window.__settingsFile]);
  await page.evaluate(()=>{window.__fixture.failRead=true;window.__repository.clearSettingsCache();window.__remountPage();});
  await page.waitForFunction(()=>document.querySelector('.err-bar')?.textContent.includes('读取自定义应用失败'));
  assert.equal(await page.getByRole('button',{name:'+ 添加应用',exact:true}).isDisabled(),true);
  assert.equal(await page.evaluate(()=>window.__fixture.files[window.__settingsFile]),savedSevenFile);
  assert.equal(await page.evaluate(()=>Object.keys(window.__fixture.files).filter(p=>p.includes('.corrupt-')).length),0);
  await page.evaluate(()=>{window.__fixture.failRead=false;});
  await page.getByRole('button',{name:'重试读取',exact:true}).click();
  await page.waitForFunction(()=>document.querySelectorAll('.launch-card').length===7);
  cases.push('Existing unreadable config is not emptied/archived; add is blocked and retry restores list');

  const reactiveSnapshot=await page.evaluate(async()=>{
    const apps=window.__reactive([{name:'提交时名称',path:'C:/fixture/snapshot.exe',future:{keep:1}}]);
    const saving=window.__repository.saveSettingsSection('quickApps',{apps});
    apps[0].name='提交后突变';apps[0].future.keep=9;apps.push({name:'突变新增',path:'C:/fixture/later.exe'});
    await saving;
    const section=await window.__repository.readSettingsSection('quickApps');
    section.apps[0].name='读回突变';
    return (await window.__repository.readSettingsSection('quickApps')).apps;
  });
  assert.deepEqual(reactiveSnapshot,[{name:'提交时名称',path:'C:/fixture/snapshot.exe',future:{keep:1}}]);
  cases.push('Reactive queued payload/readback are detached snapshots; later mutations cannot alter saved apps');

  const legacyFile=await page.evaluate(()=>window.__settingsFile.replace('yeman-settings.json','launch_apps.json'));
  const legacySix=Array.from({length:6},(_,i)=>({name:'旧版应用 '+(i+1),path:'C:/legacy/app'+(i+1)+'.exe',unknownField:{keep:i}}));
  for(const [label,legacyRaw] of [['bare array',JSON.stringify(legacySix)],['BOM array','\uFEFF'+JSON.stringify(legacySix)],['wrapped object',JSON.stringify({apps:legacySix})]]){
    await page.evaluate(({legacyFile,legacyRaw})=>{
      const f=window.__fixture;f.failSave=false;f.failRead=false;f.files={[legacyFile]:legacyRaw};f.writes=[];
      window.__repository.clearSettingsCache();window.__remountPage();
    },{legacyFile,legacyRaw});
    await page.waitForFunction(()=>document.querySelectorAll('.launch-card').length===6);
    assert.deepEqual(await page.evaluate(()=>JSON.parse(window.__fixture.files[window.__settingsFile]).quickApps.apps),legacySix);
    assert.equal(await page.evaluate(p=>window.__fixture.files[p],legacyFile),legacyRaw);
    cases.push(`Legacy ${label} migrates all six and unknown fields without changing original file`);
  }
  await page.evaluate(({legacyFile,legacySix})=>{
    const f=window.__fixture;f.files={
      [window.__settingsFile]:JSON.stringify(window.__repository.normalizeSettings({quickApps:{apps:[]}})),
      [legacyFile]:JSON.stringify(legacySix),
    };window.__repository.clearSettingsCache();window.__remountPage();
  },{legacyFile,legacySix});
  await page.waitForFunction(()=>document.querySelectorAll('.launch-card').length===0&&!document.querySelector('.launch-apps-card .add-app-btn:last-child')?.disabled);
  assert.deepEqual(await page.evaluate(async()=> (await window.__repository.readSettingsSection('quickApps')).apps),[]);
  cases.push('Explicit empty unified list wins over old six-item legacy file (no deleted-app resurrection)');
  await page.evaluate(legacySix=>{
    const f=window.__fixture;f.files={
      [window.__settingsFile]:'{invalid-main-json',
      [window.__settingsFile+'.bak']:JSON.stringify(window.__repository.normalizeSettings({quickApps:{apps:legacySix}})),
    };window.__repository.clearSettingsCache();window.__remountPage();
  },legacySix);
  await page.waitForFunction(()=>document.querySelectorAll('.launch-card').length===6);
  assert.deepEqual(await page.evaluate(()=>JSON.parse(window.__fixture.files[window.__settingsFile]).quickApps.apps),legacySix);
  assert.deepEqual(await page.evaluate(()=>Object.entries(window.__fixture.files).filter(([p])=>p.includes('.corrupt-')).map(([,v])=>v)),['{invalid-main-json']);
  cases.push('Malformed main recovers six from real backup path and retains corrupt original');
  await page.evaluate(({legacyFile,legacySix})=>{
    const f=window.__fixture;f.files={
      [window.__settingsFile]:JSON.stringify(window.__repository.normalizeSettings({quickApps:{apps:legacySix}})),
      [legacyFile]:JSON.stringify([{name:'stale legacy',path:'C:/legacy/stale.exe'}]),
    };f.readDeniedPaths=[legacyFile];window.__repository.clearSettingsCache();window.__remountPage();
  },{legacyFile,legacySix});
  await page.waitForFunction(()=>document.querySelectorAll('.launch-card').length===6);
  assert.deepEqual(await page.evaluate(async()=> (await window.__repository.readSettingsSection('quickApps')).apps),legacySix);
  assert.equal(await page.locator('.err-bar').count(),0);
  cases.push('Complete modern settings load independently of stale/unreadable legacy file');

  // Independent expected values captured from the user's updated five-app default,
  // not imported from the defaults implementation or synthetic UI fixture.
  const expectedUserPresets = [
  {
    "name": "此电脑",
    "path": "C:\\Windows\\explorer.exe"
  },
  {
    "name": "edge浏览器",
    "path": "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe"
  },
  {
    "name": "任务管理器",
    "path": "C:\\Windows\\System32\\Taskmgr.exe"
  },
  {
    "name": "关屏下载",
    "path": "C:\\SOFT\\YeMan\\PowerControl\\关闭屏幕切为节能.vbs"
  },
  {
    "name": "复古键盘",
    "path": "C:\\Windows\\system32\\osk.exe"
  },
];
  assert.deepEqual(await page.evaluate(() => window.__repository.normalizeSettings({}).quickApps.apps), expectedUserPresets);
  cases.push('Missing quickApps normalization uses the updated five names/paths without CE');
  const missingApps = await page.evaluate(() => window.__repository.normalizeSettings({ quickApps: { futureSectionField: { keep: true } } }).quickApps);
  assert.deepEqual(missingApps, { apps: expectedUserPresets, futureSectionField: { keep: true } });
  cases.push('Missing apps field backfills five without removing unknown section fields');
  assert.deepEqual(await page.evaluate(() => {
    const first = window.__repository.normalizeSettings({});
    first.quickApps.apps[0].name = 'not a default';
    first.quickApps.apps.push({ name: 'not a default', path: 'C:/fixture/poison.exe' });
    return window.__repository.normalizeSettings({}).quickApps.apps;
  }), expectedUserPresets);
  cases.push('Default objects/arrays are detached across independent new configurations');
  const customSaved = [{ name: '用户改名', path: 'C:/fixture/custom.exe', futureField: { keep: true } }];
  assert.deepEqual(await page.evaluate(apps => window.__repository.normalizeSettings({ quickApps: { apps } }).quickApps.apps, customSaved), customSaved);
  cases.push('Any saved custom list wins over the five defaults; no append or overwrite');
  assert.deepEqual(await page.evaluate(() => window.__repository.normalizeSettings({ quickApps: { apps: [] } }).quickApps.apps), []);
  cases.push('Saved empty arrays override nonempty product defaults');

  // True first-run path: no unified, backup or legacy file, and no injected list.
  await page.evaluate(() => {
    const f = window.__fixture;
    f.files = {}; f.writes = []; f.failSave = false; f.failRead = false; f.readDeniedPaths = [];
    window.__repository.clearSettingsCache(); window.__remountPage();
  });
  await page.waitForFunction(() => document.querySelectorAll('.launch-card').length === 5 && !document.querySelector('.launch-apps-card .add-app-btn:last-child')?.disabled);
  assert.deepEqual(await page.evaluate(() => JSON.parse(window.__fixture.files[window.__settingsFile]).quickApps.apps), expectedUserPresets);
  assert.deepEqual(await page.locator('.launch-name').allTextContents(), expectedUserPresets.map(app => app.name));
  assert.equal(await page.evaluate(() => window.__fixture.writes.length), 1);
  cases.push('Actual first run persists/displays all five presets once without an injected config');
  const defaultsScreenshot = path.join(out, 'quick-app-confirmed-defaults-580x800.png');
  await page.screenshot({ path: defaultsScreenshot, fullPage: true }); screenshots.push(defaultsScreenshot);
  const restoreButton = page.getByRole('button', { name: '恢复默认', exact: true });
  assert.equal(await restoreButton.count(), 1);
  assert((await restoreButton.getAttribute('class')).includes('restore-default-btn'));
  await restoreButton.focus();
  assert.equal(await page.evaluate(() => document.activeElement?.textContent?.trim()), '恢复默认');
  assert.equal(await page.locator('.launch-icon svg').count(), 5);
  assert.deepEqual(await page.evaluate(() => ['此电脑', 'edge浏览器', '任务管理器', '关屏下载', '复古键盘'].map(name => window.__quickAppIconVariant({ name }))), ['b', 'b', 'a', 'a', 'a']);
  assert.equal(await page.locator('.launch-icon[style]').count(), 0, 'quick-app icons use CSS monochrome styling, not per-app color');
  cases.push('Default five apps use reachable monochrome icon buttons and a red keyboard-focusable restore button');

  // Adding beyond the original two rows must grow the grid and the page scroll area.
  await page.evaluate(() => { window.__fixture.picked = 'C:/fixture/new-app-6.exe'; });
  await page.getByRole('button', { name: '+ 添加应用', exact: true }).click();
  await page.waitForFunction(() => document.querySelectorAll('.launch-card').length === 6);
  await page.evaluate(() => { window.__fixture.picked = 'C:/fixture/new-app-7.exe'; });
  await page.getByRole('button', { name: '+ 添加应用', exact: true }).click();
  await page.waitForFunction(() => document.querySelectorAll('.launch-card').length === 7);
  const rowGeometry = await page.locator('.launch-card').evaluateAll(nodes => nodes.map(node => {
    const rect = node.getBoundingClientRect(); return { top: rect.top, height: rect.height };
  }));
  assert(rowGeometry[6].top > rowGeometry[3].top, 'seventh app is placed on a newly-created third row');
  const gridBox = await page.locator('.launch-grid').boundingBox();
  assert(gridBox && gridBox.height > rowGeometry[0].height * 2, 'launch grid expands instead of clipping at two rows');
  const expandedScreenshot = path.join(out, 'quick-app-seven-apps-expanded.png');
  await page.screenshot({ path: expandedScreenshot, fullPage: true }); screenshots.push(expandedScreenshot);
  cases.push('Adding a sixth and seventh app automatically creates more row space and remains visible');

  await restoreButton.click();
  await page.waitForFunction(() => document.querySelectorAll('.launch-card').length === 5);
  assert.deepEqual(await page.evaluate(() => JSON.parse(window.__fixture.files[window.__settingsFile]).quickApps.apps), expectedUserPresets);
  cases.push('Restore default removes added apps and restores exactly the five-app baseline');
  await page.reload();
  await page.waitForFunction(() => document.querySelectorAll('.launch-card').length === 5 && !document.querySelector('.launch-apps-card .add-app-btn:last-child')?.disabled);
  assert.deepEqual(await page.evaluate(async () => (await window.__repository.readSettingsSection('quickApps')).apps), expectedUserPresets);
  assert.equal(await page.evaluate(() => window.__fixture.writes.length), 0);
  cases.push('First-run five survive real F5/reload and are not rewritten on subsequent load');
  for (let remaining = 4; remaining >= 0; remaining--) {
    await page.locator('.launch-card').first().click();
    await page.getByRole('button', { name: '删除', exact: true }).click();
    await page.waitForFunction(count => document.querySelectorAll('.launch-card').length === count, remaining);
  }
  assert.deepEqual(await page.evaluate(() => JSON.parse(window.__fixture.files[window.__settingsFile]).quickApps.apps), []);
  await page.reload();
  await page.waitForFunction(() => document.querySelectorAll('.launch-card').length === 0 && !document.querySelector('.launch-apps-card .add-app-btn:last-child')?.disabled);
  assert.deepEqual(await page.evaluate(async () => (await window.__repository.readSettingsSection('quickApps')).apps), []);
  cases.push('Deleting all five through the real UI remains empty after F5 (no preset resurrection)');

  // Native startup can write a partial document before WebView initialization.
  await page.evaluate(() => {
    const f = window.__fixture;
    f.files = { [window.__settingsFile]: JSON.stringify({ fan: { configured: false }, extensions: { nativeBootstrap: { keep: true } } }) };
    f.writes = [];
    window.__repository.clearSettingsCache(); window.__remountPage();
  });
  await page.waitForFunction(() => document.querySelectorAll('.launch-card').length === 5 && !document.querySelector('.launch-apps-card .add-app-btn:last-child')?.disabled);
  const bootstrapped = await page.evaluate(() => JSON.parse(window.__fixture.files[window.__settingsFile]));
  assert.deepEqual(bootstrapped.quickApps.apps, expectedUserPresets);
  assert.deepEqual(bootstrapped.extensions.nativeBootstrap, { keep: true });
  assert.equal(bootstrapped.fan.configured, false);
  cases.push('Partial native bootstrap gets five defaults and preserves existing settings/unknown fields');
  await page.evaluate(apps => {
    const f = window.__fixture;
    f.files = { [window.__settingsFile]: JSON.stringify({ quickApps: { apps, futureSectionField: { keep: true } } }) };
    window.__repository.clearSettingsCache(); window.__remountPage();
  }, customSaved);
  await page.waitForFunction(() => document.querySelectorAll('.launch-card').length === 1 && !document.querySelector('.launch-apps-card .add-app-btn:last-child')?.disabled);
  assert.deepEqual(await page.evaluate(() => JSON.parse(window.__fixture.files[window.__settingsFile]).quickApps), { apps: customSaved, futureSectionField: { keep: true } });
  cases.push('Partial modern document preserves saved list and fields while other sections migrate');
  await page.evaluate(legacyFile => {
    const f = window.__fixture; f.files = { [legacyFile]: '[]' };
    window.__repository.clearSettingsCache(); window.__remountPage();
  }, legacyFile);
  await page.waitForFunction(() => document.querySelectorAll('.launch-card').length === 0 && !document.querySelector('.launch-apps-card .add-app-btn:last-child')?.disabled);
  assert.deepEqual(await page.evaluate(() => JSON.parse(window.__fixture.files[window.__settingsFile]).quickApps.apps), []);
  cases.push('Explicitly empty bare legacy array remains empty instead of becoming five defaults');

  // Prefer actual monochrome PNGs generated by the native extraction selftest;
  // fall back to deterministic canvas fixtures so this browser suite stands alone.
  const nativeFixturePath=path.resolve(root,'../../Build/Validation/quick-app-file-icons-20261007/file-icon-fixtures.json');
  const iconFixtures=fs.existsSync(nativeFixturePath)?JSON.parse(fs.readFileSync(nativeFixturePath,'utf8')):await page.evaluate(()=>{
    const make=(flip)=>{const canvas=document.createElement('canvas');canvas.width=canvas.height=32;const c=canvas.getContext('2d');c.fillStyle='#dadada';c.fillRect(flip?4:8,4,flip?24:16,24);c.clearRect(11,10,10,8);return canvas.toDataURL('image/png');};
    return {first:make(false),second:make(true),shortcut:make(true)};
  });
  const extraIconApps=[
    {name:'应用原图标 1',path:'C:/fixture/source-icon-1.exe',futureField:{keep:true}},
    {name:'应用原图标 2',path:'C:/fixture/source-icon-2.exe'},
    {name:'自定义图标快捷方式',path:"C:/fixture/选择' 快捷方式.lnk"},
    {name:'此电脑',path:'C:/fixture/same-name-different-file.exe'},
    {name:'图标读取失败',path:'C:/fixture/icon-read-failure.exe'},
    {name:'文件无图标',path:'C:/fixture/no-icon.vbs'},
    {name:'无效图标返回',path:'C:/fixture/unsafe-icon.exe'},
    {name:'损坏的 PNG',path:'C:/fixture/corrupt-icon.exe'},
  ];
  await page.evaluate(({apps,extra,icons})=>{
    const f=window.__fixture;f.files={[window.__settingsFile]:JSON.stringify(window.__repository.normalizeSettings({quickApps:{apps:[...apps,...extra]}}))};f.writes=[];f.iconReads=[];
    f.iconResults={
      [extra[0].path]:icons.first,[extra[1].path]:icons.second,[extra[2].path]:icons.shortcut,[extra[3].path]:icons.second,
      [extra[6].path]:'https://example.invalid/no-external-icon.png',[extra[7].path]:'data:image/png;base64,bm90LXBuZw==',
    };
    f.failIconPaths=[extra[4].path];window.__repository.clearSettingsCache();window.__remountPage();
  },{apps:expectedUserPresets,extra:extraIconApps,icons:iconFixtures});
  await page.waitForFunction(()=>document.querySelectorAll('.launch-card').length===13&&document.querySelectorAll('.quick-app-file-icon').length===4&&Array.from(document.querySelectorAll('.quick-app-file-icon')).every(img=>img.complete&&img.naturalWidth>0)&&window.__fixture.activeIcons===0);
  const iconSources=await page.locator('.quick-app-file-icon').evaluateAll(nodes=>nodes.map(img=>({src:img.getAttribute('src'),filter:getComputedStyle(img).filter})));
  assert.equal(iconSources[0].src,iconFixtures.first);assert.equal(iconSources[1].src,iconFixtures.second);assert.notEqual(iconSources[0].src,iconSources[1].src);
  assert(iconSources.every(img=>img.filter==='grayscale(1)'));
  cases.push('Existing custom EXEs/shortcut load their different source PNGs automatically and keep grayscale styling');
  assert.deepEqual(await page.evaluate(apps=>window.__fixture.iconReads.filter(p=>apps.some(app=>app.path===p)),expectedUserPresets),[]);
  assert.equal(await page.locator('.launch-card').nth(8).locator('img.quick-app-file-icon').count(),1);
  cases.push('Five defaults keep selected line icons; a custom file named like a default still uses its own actual icon');
  for(let i=9;i<13;i++)assert.equal(await page.locator('.launch-card').nth(i).locator('svg.quick-app-icon').count(),1);
  assert.equal(await page.locator('.err-bar').count(),0);assert.equal(await page.evaluate(()=>window.__fixture.writes.length),0);
  cases.push('Icon read failure/missing file/unsafe URL/corrupt PNG fall back without settings writes or page errors');
  assert((await page.evaluate(()=>window.__fixture.maxActiveIcons))<=2);
  cases.push('File icon reads never exceed two concurrent jobs');
  const actualIconScreenshot=path.join(out,'quick-app-actual-file-icons.png');
  await page.screenshot({path:actualIconScreenshot,fullPage:true});screenshots.push(actualIconScreenshot);

  const customReadsBefore=await page.evaluate(extra=>extra.slice(0,4).map(app=>window.__fixture.iconReads.filter(p=>p===app.path).length),extraIconApps);
  await page.evaluate(()=>window.__remountPage());
  await page.waitForFunction(()=>document.querySelectorAll('.quick-app-file-icon').length===4&&window.__fixture.activeIcons===0);
  assert.deepEqual(await page.evaluate(extra=>extra.slice(0,4).map(app=>window.__fixture.iconReads.filter(p=>p===app.path).length),extraIconApps),customReadsBefore);
  cases.push('Successful icons are reused across real page remounts instead of re-extracting every time');

  // Icon IO must not participate in the durable settings transaction or block add.
  await page.evaluate(icons=>{
    const f=window.__fixture;f.picked='C:/fixture/delayed-new-icon.exe';f.iconResults[f.picked]=icons.first;f.holdIconPaths=[f.picked];
  },iconFixtures);
  await page.getByRole('button',{name:'+ 添加应用',exact:true}).click();
  await page.waitForFunction(()=>document.querySelectorAll('.launch-card').length===14&&!document.querySelector('.launch-apps-card .add-app-btn:last-child').disabled&&window.__fixture.releaseIcons?.['C:/fixture/delayed-new-icon.exe']);
  assert.equal(await page.locator('.launch-card').last().locator('svg').count(),1);
  assert.equal(await page.evaluate(()=>JSON.parse(window.__fixture.files[window.__settingsFile]).quickApps.apps.length),14);
  assert((await page.locator('.ok-bar').textContent()).includes('已添加'));
  cases.push('Adding durably saves and re-enables controls before a delayed icon read finishes');
  await page.evaluate(()=>window.__fixture.releaseIcons['C:/fixture/delayed-new-icon.exe']());
  await page.waitForFunction(()=>document.querySelectorAll('.quick-app-file-icon').length===5&&window.__fixture.activeIcons===0);
  cases.push('Delayed icon replaces only its temporary fallback after the application is saved');

  // A deleted/remounted card must not receive an old asynchronous completion.
  await page.evaluate(icons=>{
    const f=window.__fixture;f.picked='C:/fixture/deleted-before-icon.exe';f.iconResults[f.picked]=icons.second;f.holdIconPaths=[f.picked];
  },iconFixtures);
  await page.getByRole('button',{name:'+ 添加应用',exact:true}).click();
  await page.waitForFunction(()=>document.querySelectorAll('.launch-card').length===15&&window.__fixture.releaseIcons?.['C:/fixture/deleted-before-icon.exe']);
  await page.locator('.launch-card').last().click();await page.getByRole('button',{name:'删除',exact:true}).click();
  await page.waitForFunction(()=>document.querySelectorAll('.launch-card').length===14);
  await page.evaluate(()=>window.__fixture.releaseIcons['C:/fixture/deleted-before-icon.exe']());
  await page.waitForFunction(()=>window.__fixture.activeIcons===0);
  assert.equal(await page.locator('.quick-app-file-icon').count(),5);
  cases.push('Deleting an app while its icon is pending cannot resurrect a card or corrupt another icon');

  // Cache uses case-insensitive Windows paths and deduplicates in-flight reads.
  await page.evaluate(icons=>{
    const f=window.__fixture,p='C:/fixture/deduplicated-icon.exe';f.iconResults[p]=icons.first;f.holdIconPaths=[p];
    window.__pendingIconA=window.__getQuickAppFileIcon(p);window.__pendingIconB=window.__getQuickAppFileIcon('c:\\fixture\\DEDUPLICATED-icon.exe');
  },iconFixtures);
  await page.waitForFunction(()=>window.__fixture.releaseIcons?.['C:/fixture/deduplicated-icon.exe']);
  assert.equal(await page.evaluate(()=>window.__fixture.iconReads.filter(p=>p==='C:/fixture/deduplicated-icon.exe').length),1);
  assert.equal(await page.evaluate(()=>window.__pendingIconA===window.__pendingIconB),true);
  await page.evaluate(()=>window.__fixture.releaseIcons['C:/fixture/deduplicated-icon.exe']());
  assert.deepEqual(await page.evaluate(async()=>[await window.__pendingIconA,await window.__pendingIconB]),[iconFixtures.first,iconFixtures.first]);
  cases.push('In-flight icon reads deduplicate case/slash variants of the same Windows file');

  const diskApps=await page.evaluate(()=>JSON.parse(window.__fixture.files[window.__settingsFile]).quickApps.apps);
  assert.equal(diskApps.length,14);assert.deepEqual(diskApps[5].futureField,{keep:true});
  assert(diskApps.every(app=>!Object.keys(app).some(key=>/icon/i.test(key))));
  await page.evaluate(()=>sessionStorage.setItem('quick-app-test-icons',JSON.stringify(window.__fixture.iconResults)));
  await page.reload();
  await page.waitForFunction(()=>document.querySelectorAll('.launch-card').length===14&&document.querySelectorAll('.quick-app-file-icon').length===5&&window.__fixture.activeIcons===0);
  assert.deepEqual(await page.evaluate(()=>JSON.parse(window.__fixture.files[window.__settingsFile]).quickApps.apps),diskApps);
  assert.equal(await page.evaluate(()=>window.__fixture.writes.length),0);
  cases.push('F5 reload re-derives source icons without persisting image data or modifying existing apps/unknown fields');

  assert.deepEqual(errors, []);
  const report = { suite: 'Quick app UI browser', passed: cases.length, cases, screenshots, pageErrors: errors, scope: 'Real QuickAppView + real settingsRepository and Vue reactivity; synthetic disk/native-write ACK only mocked; save failures, reload and legacy/backup recovery tested; no installed config changes' };
  fs.writeFileSync(path.join(out, 'browser-results.json'), JSON.stringify(report, null, 2));
  console.log(JSON.stringify(report, null, 2));
} finally {
  if (browser) await browser.close();
  await new Promise(resolve => server.close(resolve));
}



