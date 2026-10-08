// Render the real QuickAppIcon.vue component into a visual preview sheet.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { createServer } from 'node:http';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const req = createRequire(path.join(root, 'package.json'));
const { build } = req('esbuild');
const { parse, compileScript, compileStyle } = req('vue/compiler-sfc');
const runtime = process.env.YMCC_UI_BROWSER_MODULE_DIR || 'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const { chromium } = createRequire(path.join(runtime, '_quick-app-icon-preview.cjs'))('playwright');
const out = process.env.YMCC_ICON_PREVIEW_OUT || path.resolve(root, '../../Build/Validation/quick-app-icons-20261007');
fs.mkdirSync(out, { recursive: true });

const styles = [];
const entry = `
  import { createApp, h } from 'vue';
  import QuickAppIcon from './src/components/QuickAppIcon.vue';
  import { QUICK_APP_ICON_PREVIEWS, quickAppIconVariant } from './src/bridge/quickAppIcons';
  const items = QUICK_APP_ICON_PREVIEWS.flatMap((item) => item.variants.map((variant, index) => ({
    ...item,
    variant,
    variantLabel: (quickAppIconVariant({ name: item.appName }) === variant ? '当前选用 · ' : '') + (index === 0 ? '方案 A' : '方案 B'),
  })));
  const app = createApp({
    render() {
      return h('main', { class: 'preview-page' }, [
        h('header', { class: 'preview-header' }, [
          h('div', { class: 'eyebrow' }, 'YEMAN CONTROL CENTER · QUICK APPS'),
          h('h1', '快捷应用单色图标预览'),
          h('p', '默认图标：此电脑 B、浏览器 B，其余 A；新增应用优先使用自身图标的灰度版。'),
        ]),
        h('section', { class: 'icon-grid', 'aria-label': '快捷应用图标方案' }, items.map((item) => h('article', { class: 'icon-card', key: item.kind + item.variant }, [
          h('div', { class: 'icon-card-head' }, [
            h('strong', item.label),
            h('span', { class: 'variant-pill' }, item.variantLabel),
          ]),
          h('div', { class: 'icon-showcase' }, [
            h('div', { class: 'icon-tile' }, [h(QuickAppIcon, { kind: item.kind, variant: item.variant })]),
            h('div', { class: 'icon-meta' }, [
              h('span', { class: 'meta-title' }, item.appName),
              h('span', { class: 'meta-detail' }, item.kind === 'generic' ? '仅在原图标读取失败时使用' : '内置默认应用图标'),
            ]),
          ]),
        ]))),
        h('footer', { class: 'preview-footer' }, '默认图标使用单色线条；新增应用保留自身图标并转成灰度；读取失败才回退到备用图标。'),
      ]);
    },
  });
  app.mount('#app');
`;

const bundle = await build({
  stdin: { resolveDir: root, loader: 'ts', contents: entry },
  bundle: true,
  write: false,
  format: 'iife',
  platform: 'browser',
  alias: { '@': path.join(root, 'src') },
  define: { 'process.env.NODE_ENV': '"production"' },
  plugins: [{
    name: 'quick-app-icon-preview-sfc',
    setup(b) {
      b.onLoad({ filter: /\.vue$/ }, args => {
        const { descriptor, errors } = parse(fs.readFileSync(args.path, 'utf8'), { filename: args.path });
        assert.equal(errors.length, 0);
        const id = 'data-v-' + createHash('sha256').update(args.path).digest('hex').slice(0, 8);
        const compiled = compileScript(descriptor, { id, inlineTemplate: true });
        for (const style of descriptor.styles) {
          const css = compileStyle({ source: style.content, filename: args.path, id, scoped: style.scoped });
          assert.equal(css.errors.length, 0);
          styles.push(css.code);
        }
        return { contents: compiled.content.replace('export default', 'const _component =') + `\n_component.__scopeId=${JSON.stringify(id)};export default _component;`, loader: 'ts', resolveDir: path.dirname(args.path) };
      });
    },
  }],
});
const js = bundle.outputFiles[0].text;
const css = `
:root { color-scheme: dark; font-family: "Segoe UI", "Microsoft YaHei", system-ui, sans-serif; --bg: #0a1019; --panel: #111b28; --panel-2: #172437; --border: #2a3c51; --text: #eef5ff; --muted: #91a5bb; --accent: #2ea6ff; }
* { box-sizing: border-box; }
body { margin: 0; min-width: 900px; background: radial-gradient(circle at 12% 0%, #152c43 0, transparent 34%), var(--bg); color: var(--text); }
.preview-page { width: min(1120px, calc(100vw - 48px)); margin: 0 auto; padding: 42px 0 34px; }
.preview-header { margin-bottom: 24px; }
.eyebrow { color: var(--accent); font-size: 11px; letter-spacing: 2px; font-weight: 800; }
h1 { margin: 8px 0 6px; font-size: 30px; letter-spacing: .3px; }
.preview-header p { margin: 0; color: var(--muted); font-size: 14px; }
.icon-grid { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 14px; }
.icon-card { min-height: 132px; padding: 17px 18px; border: 1px solid var(--border); border-radius: 13px; background: linear-gradient(145deg, rgba(23,36,55,.98), rgba(14,23,36,.98)); box-shadow: 0 10px 24px rgba(0,0,0,.18); }
.icon-card-head { display: flex; align-items: center; justify-content: space-between; gap: 12px; margin-bottom: 14px; }
.icon-card-head strong { font-size: 15px; }
.variant-pill { padding: 3px 8px; border: 1px solid rgba(46,166,255,.5); border-radius: 99px; color: #8fd2ff; font-size: 11px; font-weight: 800; }
.icon-showcase { display: flex; align-items: center; gap: 15px; }
.icon-tile { width: 72px; height: 72px; display: grid; place-items: center; flex: 0 0 auto; color: #e7f2ff; border: 1px solid #46627d; border-radius: 12px; background: #0b1420; box-shadow: inset 0 0 0 1px rgba(255,255,255,.03); }
.icon-tile svg { width: 48px !important; height: 48px !important; }
.icon-meta { display: flex; flex-direction: column; gap: 5px; min-width: 0; }
.meta-title { font-size: 13px; font-weight: 700; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.meta-detail { color: var(--muted); font-size: 11px; }
.preview-footer { margin-top: 22px; color: var(--muted); font-size: 12px; }
${styles.join('\n')}
`;
const html = `<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><title>YeManCC 快捷应用单色图标预览</title><style>${css}</style></head><body><div id="app"></div><script src="/preview.js"></script></body></html>`;
const server = createServer((request, response) => {
  response.setHeader('Content-Type', request.url === '/preview.js' ? 'text/javascript; charset=utf-8' : 'text/html; charset=utf-8');
  response.end(request.url === '/preview.js' ? js : html);
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
let browser;
try {
  browser = await chromium.launch({ headless: true, executablePath: process.env.YMCC_UI_BROWSER_EXE || 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe' });
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 }, deviceScaleFactor: 1 });
  const pageErrors = [];
  page.on('pageerror', error => pageErrors.push(error.message));
  await page.goto(`http://127.0.0.1:${server.address().port}`);
  await page.waitForFunction(() => document.querySelectorAll('.icon-card').length === 12 && document.querySelectorAll('.quick-app-icon').length === 12);
  assert.deepEqual(pageErrors, []);
  assert.equal(await page.locator('.quick-app-icon').count(), 12);
  const screenshot = path.join(out, 'quick-app-icons-preview.png');
  await page.screenshot({ path: screenshot, fullPage: true });
  const results = {
    suite: 'Quick app monochrome icon preview',
    variants: 12,
    screenshot,
    semantics: ['此电脑', 'Edge 浏览器', '任务管理器', '关屏下载', '复古键盘', '读取失败时的备用图标'],
    pageErrors,
    scope: 'Real QuickAppIcon.vue + real quickAppIcons.ts rendered in Edge; no installed config changes',
  };
  fs.writeFileSync(path.join(out, 'results.json'), JSON.stringify(results, null, 2));
  fs.writeFileSync(path.join(out, 'quick-app-icons-preview.html'), html);
  console.log(JSON.stringify(results, null, 2));
} finally {
  if (browser) await browser.close();
  await new Promise(resolve => server.close(resolve));
}

