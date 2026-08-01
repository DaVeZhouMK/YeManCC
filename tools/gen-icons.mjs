// 生成 YeManCC 全套图标的 JSON 源 + 独立 SVG 文件 + 预览页
// 风格：线性白标（Steam 设置页那种描边风格），24×24，currentColor 继承父级颜色
// 这是唯一“图标定义”源文件；src/icons.ts 从生成的 src/icons.json 重新导出
import { writeFileSync, mkdirSync, readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, resolve } from 'node:path';

const __dir = dirname(fileURLToPath(import.meta.url));
const root = resolve(__dir, '..');

const π = Math.PI;
function polar(cx, cy, r, deg) {
  return [cx + r * Math.cos((deg * π) / 180), cy + r * Math.sin((deg * π) / 180)];
}

// Steam 大屏：直接用用户给的图片抠图结果（public/icons/steam.png，透明底纯白标）
// 以 base64 内嵌，保证图标系统自包含、不依赖外部文件路径
const STEAM_PNG = resolve(root, 'public/icons/steam.png');
const steamDataUri = (() => {
  const b64 = readFileSync(STEAM_PNG).toString('base64');
  return `<image x="0" y="0" width="24" height="24" opacity="0.66" preserveAspectRatio="xMidYMid meet" href="data:image/png;base64,${b64}"/>`;
})();

// 线性齿轮：外齿廓描边 + 中心圆孔描边（不再是“太阳”）
function gearLine(cx = 12, cy = 12, teeth = 8, outerR = 11, innerR = 8, holeR = 3.4) {
  const step = 360 / teeth;
  let d = '';
  for (let i = 0; i < teeth; i++) {
    const a0 = i * step;
    const a1 = a0 + step * 0.25;
    const a2 = a0 + step * 0.5;
    const a3 = a0 + step * 0.75;
    const [x0, y0] = polar(cx, cy, innerR, a0);
    const [x1, y1] = polar(cx, cy, outerR, a1);
    const [x2, y2] = polar(cx, cy, outerR, a2);
    const [x3, y3] = polar(cx, cy, innerR, a3);
    d += (i === 0 ? `M ${x0} ${y0}` : ` L ${x0} ${y0}`);
    d += ` L ${x1} ${y1} L ${x2} ${y2} L ${x3} ${y3}`;
  }
  d += ' Z';
  return `<path d="${d}" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linejoin="round"/><circle cx="${cx}" cy="${cy}" r="${holeR}" fill="none" stroke="currentColor" stroke-width="1.6"/>`;
}

const ICONS = {
  // 野蛮系统品牌标：圆角方块 + Y 字符（线性）
  yeman: `<rect x="3" y="3" width="18" height="18" rx="5"/><path d="M8 7.5 12 13.5 16 7.5"/><path d="M12 13.5V16.5"/>`,

  // Steam 大屏：用抠出的真标（图片）
  steam: steamDataUri,

  // TDP 功耗：闪电
  tdp: `<path d="M13 2 4 14h6l-1 8 9-12h-6z"/>`,

  // CPU 调度：芯片
  cpu: `<rect x="7" y="7" width="10" height="10" rx="1.5"/><rect x="10" y="10" width="4" height="4"/><path d="M10 7V4M14 7V4M10 20v-3M14 20v-3M7 10H4M7 14H4M20 10h-3M20 14h-3"/>`,

  // 监控/锁帧（RTSS）：屏幕 + 叠加条
  rtss: `<rect x="3" y="4" width="18" height="13" rx="2"/><path d="M9 21h6"/><path d="M7.5 8.5h9M7.5 11.5h6"/>`,

  // 开机启动：电源插头（替代原来的开关，语义正确）
  power: `<path d="M9 2v6M15 2v6"/><rect x="6" y="8" width="12" height="7" rx="2"/><path d="M12 15v6"/>`,

  // 开机启动：电脑主体 + 右下角时钟（导航专用）
  startup: `<rect x="3" y="3.5" width="11" height="9" rx="1.2"/><path d="M6 16h5M8.5 12.5V16"/><circle cx="16.5" cy="15.5" r="5"/><path d="M16.5 12.8v2.9l2 1.2"/>`,

  // 睡眠优化：月牙（保留上一版，用户认可）
  sleep: `<path d="M20 14.5A8 8 0 1 1 10.5 4 6.5 6.5 0 0 0 20 14.5Z"/>`,

  // 设置：线性齿轮
  settings: gearLine(),

  // 快捷应用：启动器网格
  quick: `<rect x="4" y="4" width="6.5" height="6.5" rx="1.5"/><rect x="13.5" y="4" width="6.5" height="6.5" rx="1.5"/><rect x="4" y="13.5" width="6.5" height="6.5" rx="1.5"/><rect x="13.5" y="13.5" width="6.5" height="6.5" rx="1.5"/>`,

  // 最小化：横线
  minimize: `<path d="M5 12h14"/>`,

  // 退出：登出箭头
  logout: `<path d="M14 4h4a2 2 0 0 1 2 2v12a2 2 0 0 1-2 2h-4"/><path d="M9 12h11"/><path d="m16 8 4 4-4 4"/>`,

  // ===== 程序内其它小标 =====
  // 电源插头（独立使用）
  plug: `<path d="M9 3v3.5M15 3v3.5M7 6.5h10v4.2c0 2.6-2 4.3-5 4.3s-5-1.7-5-4.3V6.5M12 15v3c0 1.5-1 2.5-2.2 2.5"/>`,

  // 电池
  battery: `<rect x="3" y="8" width="15" height="9" rx="2"/><path d="M21 11v3"/><path d="M6.5 11v3"/>`,

  // 自动 CPU 浮动优化：芯片 + 循环箭头
  cpufloat: `<rect x="7" y="7" width="10" height="10" rx="1.5"/><path d="M10 7V4M14 7V4M10 20v-3M14 20v-3M7 10H4M7 14H4M20 10h-3M20 14h-3"/><path d="M12 9.4a2.6 2.6 0 1 1-2.3 1.4" stroke-width="1.4"/><path d="M9.4 9.1 9.7 7.1 11.6 8.1" stroke-width="1.4"/>`,

  // ===== 内容页小标 =====
  play: `<path d="M5 3l16 9-16 9z"/>`,
  pause: `<path d="M9 3v18M15 3v18"/>`,
  close: `<path d="M6 6l12 12M18 6l-12 12"/>`,
  mouse: `<rect x="8" y="2" width="8" height="14" rx="4"/><path d="M12 6v4"/>`,
  gamepad: `<path d="M6 8h12a4 4 0 0 1 4 4v2a4 4 0 0 1-4 4H6a4 4 0 0 1-4-4v-2a4 4 0 0 1 4-4z"/><path d="M8 11v4M6 13h4"/><circle cx="16" cy="13" r="1"/><circle cx="19" cy="13" r="1"/>`,
  rocket: `<path d="M12 2.2c3.3 1.8 5.1 5.5 4.6 9.2-.4 3.2-2 6.2-4.6 8.4-2.6-2.2-4.2-5.2-4.6-8.4-.5-3.7 1.3-7.4 4.6-9.2Z"/><path d="M8.1 10.1 5.3 13.9l2.8-.8M15.9 10.1l2.8 3.8-2.8-.8"/><circle cx="12" cy="9.2" r="1.7"/>`,
  bolt: `<path d="M13 2 4 14h6l-1 8 9-12h-6z"/>`,
  speed: `<path d="M5 5l6 7-6 7z"/><path d="M13 5l6 7-6 7z"/>`,
  link: `<path d="M10 13a4 4 0 0 1 0-6l3-3a4 4 0 0 1 6 0 4 4 0 0 1 0 6l-1.5 1.5"/><path d="M14 11a4 4 0 0 1 0 6l-3 3a4 4 0 0 1-6 0 4 4 0 0 1 0-6l1.5-1.5"/>`,
  bed: `<path d="M2 16h20"/><path d="M3 12h12v4H3z"/><path d="M19 10v6"/>`,
  lock: `<rect x="6" y="11" width="12" height="10" rx="2"/><path d="M8 11V7a4 4 0 0 1 8 0v4"/><circle cx="12" cy="16" r="1.2" fill="currentColor" stroke="none"/>`,
  unlock: `<rect x="6" y="11" width="12" height="10" rx="2"/><path d="M8 11V7a4 4 0 0 1 7.5-1.5"/><circle cx="12" cy="16" r="1.2" fill="currentColor" stroke="none"/>`,
  home: `<path d="M3 10l9-8 9 8"/><path d="M5 10v9a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2v-9"/>`,
  package: `<path d="M12 2l9 5v10l-9 5-9-5V7z"/><path d="M12 12 21 7"/><path d="M12 12V22"/>`,
  target: `<circle cx="12" cy="12" r="8"/><path d="M12 4v16M4 12h16"/>`,
  monitor: `<rect x="3" y="4" width="18" height="13" rx="2"/><path d="M9 21h6"/>`,
  warning: `<path d="M12 3l10 18H2z"/><path d="M12 10v5"/><circle cx="12" cy="18" r="0.8" fill="currentColor" stroke="none"/>`,
  check: `<path d="M5 12l5 5 9-9"/>`,
  cross: `<path d="M6 6l12 12M18 6l-12 12"/>`,
  edit: `<path d="M14 3l5 5-10 10H4v-5z"/>`,
  fan: `<path d="M12 2c2 3 2 6 0 9-2-3-2-6 0-9z"/><path d="M20 12c-3 2-6 2-9 0 3-2 6-2 9 0z"/><path d="M12 20c-2-3-2-6 0-9 2 3 2 6 0 9z"/><path d="M4 12c3-2 6-2 9 0-3 2-6 2-9 0z"/><circle cx="12" cy="12" r="2"/>`,
  core: `<rect x="4" y="4" width="16" height="16" rx="2"/><path d="M12 4v16M4 12h16"/>`,
  globe: `<circle cx="12" cy="12" r="9"/><path d="M3 12h18"/><path d="M12 3a15 15 0 0 1 0 18 15 15 0 0 1 0-18"/>`,
  wrench: `<path d="M14.5 3.5a5 5 0 0 1 1.8 6.8L8.5 18.1a2.5 2.5 0 1 1-3.6-3.6l7.8-7.8a5 5 0 0 1 1.8-3.2z"/><circle cx="7" cy="17" r="1.5"/>`,
  fullscreen: `<path d="M4 9V5h4M15 5h4v4M4 15v4h4M15 19h4v-4"/>`,
  search: `<circle cx="11" cy="11" r="7"/><path d="M16 16l5 5"/>`,
  fire: `<path d="M12 3c2 4 6 6 4 10 2-1 3-4 2-6 3 3 3 9-1 12a6 6 0 0 1-10 0c-3-3-3-8 0-11-1 2 0 5 2 6-2-4 2-6 4-10z"/>`,
  balance: `<path d="M12 2v4"/><path d="M5 6h14l-2 10H7z"/>`,
  trash: `<path d="M4 7h16"/><path d="M7 7v12a2 2 0 0 0 2 2h6a2 2 0 0 0 2-2V7"/><path d="M10 4h4"/>`,

  // ===== 内页还缺的小标（按用户截图补充） =====
  star: `<path d="M12 2l2.5 7h7.5l-6 4.5 2.5 7-6-4.5-6 4.5 2.5-7-6-4.5h7.5z"/>`,
  keyboard: `<rect x="3" y="6" width="18" height="12" rx="2"/><path d="M6 10h2M10 10h2M14 10h2M18 10h2M6 13h2M10 13h2M14 13h2M18 13h2M8 16h8"/>`,
  calendar: `<rect x="4" y="5" width="16" height="15" rx="2"/><path d="M4 10h16"/><path d="M8 3v4M16 3v4"/>`,
  clock: `<circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 3"/>`,
  leaf: `<path d="M12 2c5 3 7 9 4 14-1.5 2.5-4 4-7 3 3-1.5 5-4 5-7 0-3.5-2-6-5-8 1 .3 2.3.3 3-2z"/>`,
  memory: `<rect x="4" y="7" width="16" height="10" rx="1"/><path d="M7 17v3M10 17v3M14 17v3M17 17v3"/><path d="M7 10h4M14 10h3"/>`,
  speaker: `<path d="M4 9h3l6-4v14l-6-4H4z"/><path d="M16 9a4 4 0 0 1 0 6"/>`,
  headphones: `<path d="M4 12v5a2 2 0 0 0 2 2h1"/><path d="M20 12v5a2 2 0 0 1-2 2h-1"/><path d="M4 12a8 8 0 0 1 16 0"/>`,
  broom: `<path d="M18 2l-3 9"/><path d="M7 22l4-11h5l-1 3-8 8z"/>`,
  gpu: `<rect x="3" y="8" width="18" height="9" rx="2"/><path d="M6 17v3M18 17v3"/><circle cx="15" cy="12" r="2"/><path d="M6 11h5"/>`,
  refresh: `<path d="M21 12a9 9 0 1 1-3-6.7"/><path d="M21 5v5h-5"/>`,
  rotate: `<path d="M16 4h4v4"/><path d="M20 5.5A9 9 0 0 0 4 12"/><path d="M8 20H4v-4"/><path d="M4 18.5A9 9 0 0 0 20 12"/>`,
  list: `<path d="M5 7h14M5 12h14M5 17h14"/>`,
};

// 1) 写入 JSON 源，供前端 TS 导入
writeFileSync(resolve(root, 'src/icons.json'), JSON.stringify(ICONS, null, 2) + '\n', 'utf8');

// 2) 生成独立 SVG 到 public/icons（原生壳/托盘/EXE 用）
const outDir = resolve(root, 'public/icons');
mkdirSync(outDir, { recursive: true });
for (const [name, inner] of Object.entries(ICONS)) {
  const svg = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#e9eef5" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round">${inner}</svg>\n`;
  writeFileSync(resolve(outDir, `${name}.svg`), svg, 'utf8');
}

// 3) 预览页
const cards = Object.entries(ICONS)
  .map(
    ([name, inner]) => `
    <div class="card">
      <svg viewBox="0 0 24 24" fill="none" stroke="#e9eef5" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round">${inner}</svg>
      <code>${name}</code>
    </div>`
  )
  .join('');

const html = `<!doctype html><html lang="zh"><head><meta charset="utf-8"/>
<title>YeManCC 图标集</title>
<style>
  body{margin:0;background:#0c111a;color:#e9eef5;font-family:system-ui,'Segoe UI',sans-serif;}
  h1{padding:24px 24px 0;font-size:18px;font-weight:700;}
  p{padding:0 24px;color:#8b96a8;font-size:13px;}
  .grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(120px,1fr));gap:16px;padding:24px;}
  .card{background:#131c28;border:1px solid #1c2533;border-radius:10px;padding:18px;display:flex;flex-direction:column;align-items:center;gap:12px;}
  .card svg{width:40px;height:40px;}
  .card code{font-size:12px;color:#8b96a8;}
  .accent{color:#2ea6ff;}
</style></head><body>
<h1>YeManCC 图标集 <span class="accent">· Steam 大屏风（线性版）</span></h1>
<p>白色线性 / 24×24 / currentColor。导航内自动继承高亮色；独立 SVG 见 public/icons/。</p>
<div class="grid">${cards}</div>
</body></html>`;

writeFileSync(resolve(root, 'icon-gallery.html'), html, 'utf8');
console.log('generated src/icons.json +', Object.keys(ICONS).length, 'icons + gallery');
