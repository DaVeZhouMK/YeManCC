// 构建后把「非 vite 产物」补回 dist/，使其保持完整可运行快照：
//   - native/YeManCC.exe  （原生壳，vite 不碰）
//   - app.config.json     （位于项目根，vite 不碰）
// 这样 dist/ 既是前端构建产物，也是独立部署目录，避免「dist 缺 exe」的疏漏。
import { copyFileSync, existsSync, mkdirSync } from 'fs';
import { dirname, join } from 'path';
import { fileURLToPath } from 'url';

const root = dirname(dirname(fileURLToPath(import.meta.url)));
const dist = join(root, 'dist');
if (!existsSync(dist)) mkdirSync(dist, { recursive: true });

const copies = [
  ['native/YeManCC.exe', 'dist/YeManCC.exe'],
  ['app.config.json', 'dist/app.config.json'],
];

for (const [src, dst] of copies) {
  const s = join(root, src);
  const d = join(root, dst);
  if (existsSync(s)) {
    copyFileSync(s, d);
    console.log(`[postbuild] copied ${src} -> ${dst}`);
  } else {
    console.log(`[postbuild] skip ${src} (not found; 先编译 native 再 build 即可)`);
  }
}
