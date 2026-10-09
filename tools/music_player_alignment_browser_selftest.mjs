// Render the production music-card template, icon components and scoped CSS.
// Only the music state/native actions are mocked; no user files are accessed.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { createServer } from 'node:http';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const require = createRequire(path.join(root, 'package.json'));
const { build } = require('esbuild');
const { parse, compileScript, compileStyle } = require('vue/compiler-sfc');
const runtime = process.env.YMCC_UI_BROWSER_MODULE_DIR || 'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const { chromium } = createRequire(path.join(runtime, '_music-alignment-test.cjs'))('playwright');
const out = process.env.YMCC_UI_BROWSER_OUT || path.resolve(root, '../../Build/Validation/music-player-alignment-20261006');
fs.mkdirSync(out, { recursive: true });
const source = fs.readFileSync(path.join(root, 'src/views/QuickAppView.vue'), 'utf8');
const title = source.indexOf('<h3 class="card-title"><InlineIcon name="music"');
assert(title >= 0, 'Music card was not found');
const card = source.slice(source.lastIndexOf('<section', title), source.indexOf('</section>', title) + 10);
const css = [fs.readFileSync(path.join(root, 'src/styles/tokens.css'), 'utf8')];
const { descriptor } = parse(source);
for (const style of descriptor.styles) {
  const result = compileStyle({ source: style.content, id: 'data-v-music-alignment', scoped: style.scoped });
  assert.equal(result.errors.length, 0); css.push(result.code);
}
const fixture = `<script setup lang="ts">
  import {ref} from 'vue';import InlineIcon from './src/components/InlineIcon.vue';
  const folder=ref('C:/Music'),currentName=ref('Song 1.mp3'),hasFolder=ref(true),playing=ref(false),mode=ref('random'),musicError=ref(''),volume=ref(0.35),muted=ref(false),busy=ref(false);
  const chooseFolder=()=>{},scanFolder=()=>{},playPrev=()=>{},playNext=()=>{},onVolumeInput=()=>{},onVolumeChange=()=>{};
  const togglePlay=()=>playing.value=!playing.value,toggleMute=()=>muted.value=!muted.value,toggleMusicMode=()=>mode.value=mode.value==='random'?'sequential':'random';
</script><template>${card}</template>`;
function compile(contents, filename, resolveDir, id) {
  const { descriptor, errors } = parse(contents, { filename });
  assert.equal(errors.length, 0);
  const script = compileScript(descriptor, { id, inlineTemplate: true }).content;
  for (const style of descriptor.styles) {
    const result = compileStyle({ source: style.content, filename, id, scoped: style.scoped });
    assert.equal(result.errors.length, 0); css.push(result.code);
  }
  return { contents: script.replace('export default', 'const _component =') + `\n_component.__scopeId=${JSON.stringify(id)};export default _component;`, loader: 'ts', resolveDir };
}
const entry = compile(fixture, 'music-alignment.vue', root, 'data-v-music-alignment');
const bundle = await build({
  stdin: { ...entry, contents: entry.contents.replace('export default _component;', "import {createApp} from 'vue';createApp(_component).mount('#app');") },
  bundle: true, write: false, format: 'iife', platform: 'browser',
  alias: { '@': path.join(root, 'src') }, define: { 'process.env.NODE_ENV': '"production"' },
  plugins: [{ name: 'music-alignment-components', setup(b) {
    b.onLoad({ filter: /\.vue$/ }, args => compile(fs.readFileSync(args.path, 'utf8'), args.path, path.dirname(args.path), 'data-v-' + createHash('sha256').update(args.path).digest('hex').slice(0, 8)));
  } }],
});
const html = `<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>Music button alignment</title><style>${css.join('\n')}body{margin:0;padding:16px;background:var(--bg-solid);color:var(--text);font-family:system-ui,sans-serif;}#app{max-width:760px;margin:0 auto}</style><main id="app"></main><script src="/fixture.js"></script></html>`;
const server = createServer((request, response) => {
  response.setHeader('Content-Type', request.url === '/fixture.js' ? 'text/javascript; charset=utf-8' : 'text/html; charset=utf-8');
  response.end(request.url === '/fixture.js' ? bundle.outputFiles[0].text : html);
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const cases = [], errors = [], failures = [], screenshots = [];
let browser;
try {
  browser = await chromium.launch({ headless: true, executablePath: process.env.YMCC_UI_BROWSER_EXE || 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe' });
  const page = await browser.newPage();
  page.on('pageerror', error => errors.push(error.message));
  for (const [width, zoom] of [[420, 1], [580, 1], [1280, 1], [580, 1.25], [580, 1.5]]) {
    await page.setViewportSize({ width, height: 600 });
    await page.goto(`http://127.0.0.1:${server.address().port}`);
    await page.locator('.music-actions').waitFor();
    await page.evaluate(zoom => { document.body.style.zoom = zoom; }, zoom);
    const metrics = await page.evaluate(() => {
      const center = rect => ({ x: rect.left + rect.width / 2, y: rect.top + rect.height / 2 });
      const delta = (a, b) => ({ x: Math.abs(a.x - b.x), y: Math.abs(a.y - b.y) });
      return [...document.querySelectorAll('.music-actions button')].map(button => {
        const icon = button.querySelector('.inline-icon'), svg = icon.querySelector('svg');
        const br = button.getBoundingClientRect(), ir = icon.getBoundingClientRect(), sr = svg.getBoundingClientRect();
        const text = [...button.childNodes].find(n => n.nodeType === Node.TEXT_NODE && n.textContent.trim());
        const range = document.createRange();range.selectNodeContents(text);
        const tr = range.getBoundingClientRect();
        const groupCenter = (Math.min(ir.left, tr.left) + Math.max(ir.right, tr.right)) / 2;
        return { label: button.textContent.trim(), iconSlotDelta: delta(center(sr), center(ir)), buttonVerticalDelta: Math.abs(center(ir).y - center(br).y), textVerticalDelta: Math.abs(center(sr).y - center(tr).y), groupHorizontalDelta: Math.abs(groupCenter - center(br).x), svgHasMarkup: svg.children.length > 0 };
      });
    });
    const state = `${width}px at ${zoom * 100}%`;
    assert.equal(metrics.length, 2);
    for (const metric of metrics) {
      if (!metric.svgHasMarkup || metric.iconSlotDelta.x > 0.1 || metric.iconSlotDelta.y > 0.1 || metric.buttonVerticalDelta > 0.1 || metric.groupHorizontalDelta > 0.6 || metric.textVerticalDelta > 1.5 * zoom) failures.push({ state, ...metric });
    }
    cases.push({ state, metrics });
    await page.locator('.music-btn.play').click();
    await page.locator('.vol-btn').click();
    assert(await page.locator('.music-btn.play svg').evaluate(svg => svg.children.length > 0));
    assert(await page.locator('.vol-btn svg').evaluate(svg => svg.children.length > 0));
    const screenshot = path.join(out, `music-card-${width}px-${zoom * 100}pct.png`);
    await page.locator('.card').screenshot({ path: screenshot });screenshots.push(screenshot);
    if (width === 580 && zoom === 1) {
      const actions = path.join(out, 'music-action-buttons.png');
      await page.locator('.music-actions').screenshot({ path: actions });screenshots.push(actions);
    }
  }
  const report = { ok: failures.length === 0 && errors.length === 0, cases, failures, screenshots, errors };
  fs.writeFileSync(path.join(out, 'alignment-report.json'), JSON.stringify(report, null, 2) + '\n');
  console.log(JSON.stringify(report, null, 2));
  assert.deepEqual(errors, []);
  assert.equal(failures.length, 0, 'Player button icons/text are not centered; see alignment-report.json');
} finally {
  await browser?.close();
  await new Promise(resolve => server.close(resolve));
}
