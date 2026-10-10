// Controller visual-navigation regression: real Vue pages and exact production moveFocus slice.
// Native actions and persistent configuration are the existing memory fixtures.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { createRequire } from 'node:module';
import { createHash } from 'node:crypto';
import { createServer } from 'node:http';
const root = process.cwd();
const out = process.env.YMCC_VISUAL_NAV_OUT || path.resolve(root,'../../Build/Validation/gamepad-visual-navigation-20261010/browser');
fs.mkdirSync(out,{recursive:true});
const require = createRequire(path.join(root, 'package.json'));
const { build } = require('esbuild');
const { parse, compileScript, compileStyle } = require('vue/compiler-sfc');
const runtime = process.env.YMCC_UI_BROWSER_MODULE_DIR || 'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const { chromium } = createRequire(path.join(runtime, '_visual-navigation.cjs'))('playwright');
const monitorOnly = process.argv.includes('--monitor');
const shared = fs.readFileSync(monitorOnly ? 'tools/monitor_steam_ui_selftest.mjs' : 'tools/performance_schedule_frame_layout_browser_selftest.mjs', 'utf8');
const fixtureStart = shared.indexOf(monitorOnly ? 'const mocks={' : 'const mocks = {');
const fixtureEnd = shared.indexOf(monitorOnly ? 'const result=' : 'const styles = []', fixtureStart);
assert(fixtureStart >= 0 && fixtureEnd > fixtureStart);
const { mocks, entry: originalEntry } = monitorOnly ? {
  mocks: vm.runInNewContext('(function(){' + shared.slice(fixtureStart, fixtureEnd) + 'return mocks;})()'),
  entry: `import {createApp} from 'vue';import Page from './src/views/RtssView.vue';window.__fixture={events:{},calls:[],rtss:true,monitor:true,limit:120,limitReads:0,frames:{ac:{fps:120,ceiling:120,lastFps:120},dc:{fps:60,ceiling:60,lastFps:60}},autoMode:'never',rtssWrites:[],overlayWrites:[],zoomWrites:[],limitWrites:[],fpsWrites:[],steam:{ok:true,available:window.__auditSteamAvailable!==false,pending:false,position:1,detail:1,scale:.8,saturation:1,opacity:1}};const app=createApp(Page);app.mount('#app');window.__unmount=()=>app.unmount();`,
} : vm.runInNewContext('(function(){' + shared.slice(fixtureStart, fixtureEnd) + 'return {mocks,entry};})()');
const engine = fs.readFileSync('src/gamepad/engine.ts', 'utf8');
const focusStart = engine.indexOf('function focusables(): HTMLElement[] {');
const focusEnd = engine.indexOf('function isRangeInput(', focusStart);
assert(focusStart >= 0 && focusEnd > focusStart);
const focusCode = engine.slice(focusStart, focusEnd);
mocks['@/gamepad/engine'] = `import {focusGamepadElement,setGamepadFocused} from './src/gamepad/focus.ts';import {spatialNavigationTarget} from './src/gamepad/spatial.ts';const FOCUSABLE='button:not([disabled]):not([tabindex="-1"]):not([data-gp-ignore]), input:not([disabled]):not([tabindex="-1"]):not([data-gp-ignore]), select:not([disabled]):not([tabindex="-1"]):not([data-gp-ignore]), [tabindex]:not([tabindex="-1"]):not([data-gp-ignore])';const activeFanNodeEditor=()=>null;export const isMouseModeSuppressed=()=>false;${focusCode}\nexport const testMoveFocus=moveFocus;export const testFocusables=focusables;`;
if (!monitorOnly) mocks['@/bridge/yeman'] = mocks['@/bridge/yeman'].replace('heterogeneous:true', 'heterogeneous:window.__auditHeterogeneous!==false');
if(monitorOnly) mocks['@/bridge/gamedetect']=mocks['@/bridge/gamedetect'].replace('return null','return window.__fixture.game || null');
const entry = originalEntry + `\nimport {testMoveFocus,testFocusables} from '@/gamepad/engine';import {focusGamepadElement,getGamepadViewportSafeArea} from '@/gamepad/focus';window.__move=testMoveFocus;window.__auditFocusables=testFocusables;window.__auditFocus=focusGamepadElement;window.__auditSafe=getGamepadViewportSafeArea;`;
const styles = [];
const built = await build({ stdin: { contents: entry, resolveDir: root, loader: 'ts' }, bundle: true, write: false, platform: 'browser', format: 'iife', alias: { '@': path.join(root, 'src') }, define: { 'process.env.NODE_ENV': '"production"' }, plugins: [{ name: 'visual-nav-fixture', setup(builder) {
  builder.onResolve({ filter: /.*/ }, args => args.path === './ipc' && args.importer.endsWith('steamSettings.ts') ? { path:'@/bridge/ipc',namespace:'fixture' } : args.path in mocks ? { path: args.path, namespace: 'fixture' } : undefined);
  builder.onLoad({ filter: /.*/, namespace: 'fixture' }, args => ({ contents: mocks[args.path], loader: 'ts', resolveDir: root }));
  builder.onLoad({ filter: /\.vue$/ }, args => {
    const { descriptor, errors } = parse(fs.readFileSync(args.path, 'utf8'), { filename: args.path });
    assert.deepEqual(errors, []);
    const id = 'data-v-' + createHash('sha256').update(args.path).digest('hex').slice(0, 8);
    const script = compileScript(descriptor, { id, inlineTemplate: true });
    for (const style of descriptor.styles) {
      const result = compileStyle({ source: style.content, filename: args.path, id, scoped: style.scoped });
      assert.deepEqual(result.errors, []); styles.push(result.code);
    }
    return { contents: script.content.replace('export default', 'const _component=') + '\n_component.__scopeId=' + JSON.stringify(id) + ';export default _component;', loader: 'ts', resolveDir: path.dirname(args.path) };
  });
} }] });
const css = fs.readFileSync('src/styles/tokens.css', 'utf8') + '\n' + styles.join('\n');
const html = '<!doctype html><meta charset="utf-8"><title>Read-only controller visual navigation audit</title><style>' + css + '\n*{box-sizing:border-box}body{margin:0;background:var(--bg-solid);color:var(--text);font-family:system-ui;overflow:hidden}.app-stage{height:100vh;overflow:hidden}.app-content{height:100%;min-height:0;overflow-y:auto;scrollbar-gutter:stable;padding:12px 12px calc(var(--gamepad-safe-bottom,24px) + var(--gamepad-clip-bottom,0px));scroll-padding:24px 0 var(--gamepad-safe-bottom,24px)}#app{height:auto;max-width:720px;margin:auto}</style><div class="app-stage"><main class="app-content"><div id="app"></div></main></div><script src="/fixture.js"></script>';
const server = createServer((req, res) => { res.setHeader('Content-Type', req.url === '/fixture.js' ? 'text/javascript' : 'text/html; charset=utf-8'); res.end(req.url === '/fixture.js' ? built.outputFiles[0].text : html); });
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const reports = [], pageErrors = [], screenshots = [];
const scenarios = monitorOnly ? [
  {state:'monitor',width:720,zoom:1},
  {state:'monitor',width:580,zoom:1},
  {state:'monitor',width:420,zoom:1},
  {state:'monitor',width:720,zoom:1.25},
  {state:'monitor',width:720,zoom:1.5},
  {state:'steam-unavailable',width:580,zoom:1},
  {state:'monitor-reset-confirm',width:580,zoom:1},
  {state:'monitor-game-warning',width:580,zoom:1},
] : [
  { state: 'auto-monitor-on', width: 720, zoom: 1 },
  { state: 'auto-monitor-on', width: 580, zoom: 1 },
  { state: 'auto-monitor-on', width: 420, zoom: 1 },
  { state: 'auto-monitor-off', width: 580, zoom: 1 },
  { state: 'editor-monitor-on', width: 720, zoom: 1 },
  { state: 'editor-monitor-on', width: 580, zoom: 1 },
  { state: 'editor-monitor-on', width: 420, zoom: 1 },
  { state: 'editor-monitor-off', width: 580, zoom: 1 },
  { state: 'editor-monitor-on', width: 720, zoom: 1.25 },
  { state: 'editor-monitor-on', width: 720, zoom: 1.5 },
  { state: 'editor-homogeneous', width: 580, zoom: 1 },
  { state: 'editor-cpu-float', width: 580, zoom: 1 },
  { state: 'manual-monitor-on', width: 580, zoom: 1 },
  { state: 'ac-unlocked', width: 580, zoom: 1 },
];
let browser;
try {
  browser = await chromium.launch({ headless: true, executablePath: 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe' });
  for (const scenario of scenarios) {
    const page = await browser.newPage({ viewport: { width: scenario.width, height: 720 } });
    page.on('pageerror', error => pageErrors.push({ scenario, error: error.message }));
    await page.addInitScript(heterogeneous => window.__auditHeterogeneous = heterogeneous, scenario.state !== 'editor-homogeneous');
    await page.addInitScript(available => window.__auditSteamAvailable = available, scenario.state !== 'steam-unavailable');
    await page.goto('http://127.0.0.1:' + server.address().port);
    if (monitorOnly) await page.waitForFunction(() => document.querySelector('.steam-monitor .dd-trigger')?.disabled === (window.__auditSteamAvailable===false));
    else await page.waitForFunction(() => document.querySelector('.schedule-card .mode-picker button')?.disabled === false);
    await page.evaluate(zoom => {
      const stage = document.querySelector('.app-stage');
      stage.style.zoom = zoom; stage.style.width = (innerWidth / zoom) + 'px'; stage.style.height = (innerHeight / zoom) + 'px';
      document.documentElement.style.setProperty('--ui-scale', String(zoom));
      stage.style.setProperty('--gamepad-safe-bottom', (Math.max(32, Math.min(64, innerHeight * .06)) / zoom) + 'px');
    }, scenario.zoom);
    if (scenario.state.endsWith('off')) await page.getByRole('button', { name: '监控已打开', exact: true }).click();
    if (scenario.state.startsWith('editor')) { await page.getByRole('button', { name: '编辑性能组合', exact: true }).click(); await page.waitForSelector('.editor-card'); await page.waitForTimeout(180); }
    if (scenario.state === 'editor-cpu-float') {
      const cpu=page.locator('.editor-grid label').filter({hasText:'CPU 浮动值'}).locator('button');
      await cpu.click();await page.getByRole('option',{name:'小浮动',exact:false}).click();
      await page.waitForSelector('[role=listbox]',{state:'detached'});
    }
    if (scenario.state === 'monitor-reset-confirm') {
      await page.getByRole('button',{name:'复位 RTSS 全部设置',exact:true}).click();await page.waitForSelector('.confirm-bar');
    }
    if (scenario.state === 'monitor-game-warning') {
      await page.evaluate(()=>window.__fixture.game={name:'Fixture Game',pid:42});
      await page.locator('.monitor-template-head .switch').first().click();await page.waitForSelector('.game-warn-bar');
    }
    if (scenario.state.startsWith('manual')) { await page.getByRole('button', { name: '切换为手动模式', exact: true }).click(); await page.waitForSelector('.power-mode-row.frames-only'); }
    if (scenario.state === 'ac-unlocked') { await page.getByRole('button', { name: '插电锁帧上限', exact: true }).click(); await page.getByRole('option', { name: '不锁帧', exact: true }).click(); await page.waitForFunction(() => document.querySelector('.power-mode-row.ac input').disabled); await page.waitForSelector('[role=listbox]',{state:'detached'}); }
    const graph = await page.evaluate(zoom => {
      const content = document.querySelector('.app-content'); content.scrollTop = 0;
      const els = window.__auditFocusables();
      const metadata = el => {
        const side = el.closest('.power-mode-row')?.classList.contains('ac') ? 'AC' : el.closest('.power-mode-row')?.classList.contains('dc') ? 'DC' : '';
        const region = el.closest('.steam-monitor') ? 'Steam监控' : el.closest('.monitor-template-head,.template-tabs') ? 'RTSS' : el.closest('.editor-card') ? '编辑器' : el.closest('.schedule-tools') ? '工具行' : side || 'RTSS';
        const label = el.getAttribute('aria-label') || el.closest('.toggle-row')?.querySelector('.toggle-label')?.textContent || el.closest('label')?.querySelector('span')?.textContent || el.closest('.slider')?.querySelector('.slider-label')?.textContent || el.textContent;
        const r = el.getBoundingClientRect();
        return { id: el.dataset.auditNode, name: region + '/' + (label || el.tagName).replace(/\s+/g, ' ').trim(), region, row: el.dataset.gpRow ?? null, col: el.dataset.gpCol ?? null, tag: el.tagName, rect: { x: r.x, y: r.y, width: r.width, height: r.height, cx: r.x + r.width / 2, cy: r.y + r.height / 2 } };
      };
      els.forEach((el, i) => el.dataset.auditNode = String(i));
      const nodes = els.map(metadata), edges = [], unsafeFocus = [];
      const directions = [{ key: 'left', dx: -1, dy: 0 }, { key: 'right', dx: 1, dy: 0 }, { key: 'up', dx: 0, dy: -1 }, { key: 'down', dx: 0, dy: 1 }];
      for (const el of els) for (const dir of directions) {
        window.__auditFocus(el); window.__move(dir.dx, dir.dy);
        const target = document.activeElement;
        const from = nodes.find(n => n.id === el.dataset.auditNode), to = nodes.find(n => n.id === target?.dataset.auditNode);
        if (!to) { edges.push({ from: from.id, dir: dir.key, to: null, violation: 'focus-outside-candidate-list' }); continue; }
        const deltaX = to.rect.cx - from.rect.cx, deltaY = to.rect.cy - from.rect.cy;
        const changed = to.id !== from.id;
        const opposite = changed && (dir.dx ? dir.dx * deltaX <= 1 : dir.dy * deltaY <= 1);
        const crossRow = changed && dir.dx !== 0 && Math.abs(deltaY) > 42 * zoom;
        edges.push({ from: from.id, fromName: from.name, dir: dir.key, to: to.id, toName: to.name, changed, deltaX, deltaY, opposite, crossRow });
        const r = target.getBoundingClientRect(), cr = content.getBoundingClientRect();
        const safeBottom = Math.min(innerHeight, cr.bottom) - parseFloat(getComputedStyle(content).scrollPaddingBottom) * zoom;
        if (r.top < Math.max(0, cr.top) - 1 || r.bottom > safeBottom + 1 || r.left < -1 || r.right > innerWidth + 1) unsafeFocus.push({ from: from.name, dir: dir.key, to: to.name, rect: { x: r.x, y: r.y, right: r.right, bottom: r.bottom }, safeBottom });
      }
      const adjacency = new Map(nodes.map(n => [n.id, edges.filter(e => e.from === n.id && e.to != null && e.to !== n.id).map(e => e.to)]));
      const reached = start => { const seen = new Set([start]), queue = [start]; while (queue.length) for (const next of adjacency.get(queue.shift()) || []) if (!seen.has(next)) { seen.add(next); queue.push(next); } return seen; };
      const firstReach = reached(nodes[0].id);
      const regions = [...new Set(nodes.map(n => n.region))];
      const trapped = nodes.filter(n => regions.length > 1 && [...reached(n.id)].every(id => nodes.find(other => other.id === id).region === n.region)).map(n => n.name);
      const coordinates = new Map();
      for (const node of nodes) if (node.row != null && node.col != null) { const key = node.row + ',' + node.col; coordinates.set(key, [...(coordinates.get(key) || []), node.name]); }
      return { nodes, edges, reverseDirection: edges.filter(e => e.opposite), horizontalCrossRow: edges.filter(e => e.crossRow), unreachableFromFirst: nodes.filter(n => !firstReach.has(n.id)).map(n => n.name), regionTraps: trapped, duplicateCoordinates: [...coordinates].filter(([,names]) => names.length > 1).map(([key,names]) => ({ key,names })), unsafeFocus, horizontalOverflow: document.documentElement.scrollWidth > innerWidth };
    }, scenario.zoom);
    const edge=(from,dir,to)=>{
      const actual=graph.edges.find(item=>item.fromName===from && item.dir===dir);
      assert.equal(actual?.toName,to,scenario.state+': '+from+' '+dir);
    };
    if (monitorOnly && scenario.state !== 'steam-unavailable') {
      edge('Steam监控/Steam监控位置','up','RTSS/横版监控');
      if (scenario.state !== 'monitor-reset-confirm') edge('Steam监控/背景不透明度','down','RTSS/复位 RTSS 全部设置');
    } else if (!monitorOnly && !scenario.state.startsWith('manual')) {
      edge('AC/AC 性能档位','down','DC/DC 性能档位');
      edge('DC/DC 性能档位','up','AC/AC 性能档位');
      edge('DC/帧率上限','right','DC/电池锁帧上限');
      edge('DC/电池锁帧上限','left','DC/帧率上限');
    }
    for(const key of ['reverseDirection','horizontalCrossRow','unreachableFromFirst','regionTraps','duplicateCoordinates','unsafeFocus'])assert.deepEqual(graph[key],[],JSON.stringify(scenario)+' '+key);
    assert.equal(graph.horizontalOverflow,false,JSON.stringify(scenario)+' horizontal overflow');
    const alignment=await page.evaluate(()=>[...document.querySelectorAll('.frame-rate-controls')].map(row=>{const track=row.querySelector('input[type=range]').getBoundingClientRect(),ceiling=row.querySelector('.dd-trigger').getBoundingClientRect();return Math.abs(track.y+track.height/2-ceiling.y-ceiling.height/2)}));
    assert(alignment.every(delta=>delta<=1),'Frame track/dropdown must align at '+scenario.zoom+'x: '+JSON.stringify(alignment));
    reports.push({ ...scenario, ...graph, alignment });
    if ((['auto-monitor-on', 'editor-monitor-on', 'manual-monitor-on'].includes(scenario.state) || monitorOnly) && scenario.width === 580) {
      await page.evaluate(monitor => { document.querySelector('.app-content').scrollTop = 0; window.__auditFocus(document.querySelector(monitor ? '.monitor-template-head .switch' : '.power-mode-row.dc input')); window.__move(monitor ? -1 : 1, 0); }, monitorOnly);
      const screenshot = path.join(out, scenario.state + '-' + scenario.width + (monitorOnly ? '-left-from-rtss.png' : '-right-from-dc.png'));
      await page.screenshot({ path: screenshot, animations: 'disabled' }); screenshots.push(screenshot);
    }
    console.log(JSON.stringify({ state: scenario.state, width: scenario.width, zoom: scenario.zoom, nodes: graph.nodes.length, reverseDirection: graph.reverseDirection.length, crossRow: graph.horizontalCrossRow.length, unreachable: graph.unreachableFromFirst.length, trapped: graph.regionTraps.length, unsafeFocus: graph.unsafeFocus.length, duplicates: graph.duplicateCoordinates.length }));
    await page.evaluate(()=>window.__unmount());
    await page.close();
  }
  const report = { title: monitorOnly ? 'Monitor controller visual movement audit' : 'Schedule controller visual movement audit', date: '2026-10-10', scope: 'Current uncommitted mainline Vue page and exact production focus/navigation code; memory configuration/native fixtures; simulated focus directions, not a physical-controller/installed-app acceptance test; app-content scroll/zoom fixture', scenarioCount: reports.length, directionCount: reports.reduce((total,item) => total + item.edges.length, 0), passed:reports.length,pageErrors, screenshots, reports };
  fs.writeFileSync(path.join(out, monitorOnly ? 'monitor-results.json' : 'results.json'), JSON.stringify(report, null, 2));
  assert.deepEqual(pageErrors, []);
  console.log('Saved ' + path.join(out, monitorOnly ? 'monitor-results.json' : 'results.json'));
} finally { if (browser) await browser.close(); await new Promise(resolve => server.close(resolve)); }
