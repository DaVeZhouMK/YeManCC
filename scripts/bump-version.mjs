// scripts/bump-version.mjs
// 维护更新的唯一入口：改版本号 + 重新生成 src/version.ts / native/version.h + 打印发布命令。
//
// 用法：
//   node scripts/bump-version.mjs                 # 自动 patch+1（0.0.2 -> 0.0.3）
//   node scripts/bump-version.mjs 0.0.2          # 指定版本
//   node scripts/bump-version.mjs 0.0.2 --notes "本次更新说明"
//
// 之后按脚本打印的 git 命令执行即可；CI（release.yml）会在打 v* tag 后自动构建并发布
// YeManCC.zip（含程序本体 + PowerControl 依赖包），并把新 sha256 写回 main/version.json。
import { readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, resolve } from 'node:path';
import { execFileSync } from 'node:child_process';

const __dirname = dirname(fileURLToPath(import.meta.url));
const root = resolve(__dirname, '..');
const verPath = resolve(root, 'version.json');

const argv = process.argv.slice(2);
let newVersion = null;
let notes = null;
for (let i = 0; i < argv.length; i++) {
  if (argv[i] === '--notes') { notes = argv[++i] ?? ''; }
  else if (!newVersion) { newVersion = argv[i]; }
}

const pkg = JSON.parse(readFileSync(verPath, 'utf8'));
const cur = String(pkg.version || '0.0.0');
if (!newVersion) {
  const parts = cur.split('.').map((n) => parseInt(n, 10) || 0);
  parts[2] = (parts[2] || 0) + 1;
  newVersion = parts.join('.');
}

// 简单校验 semver
if (!/^\d+\.\d+\.\d+$/.test(newVersion)) {
  console.error('版本号格式应为 x.y.z，收到: ' + newVersion);
  process.exit(1);
}

// 更新 version.json：写入新版本与说明；sha256/publishedAt 留空，由 CI 构建后回填
pkg.version = newVersion;
if (notes != null) pkg.notes = notes;
pkg.sha256 = '';
pkg.publishedAt = '';
writeFileSync(verPath, JSON.stringify(pkg, null, 2) + '\n', 'utf8');
console.log(`[bump] version.json -> ${newVersion}`);

// 重新生成 src/version.ts 与 native/version.h（保证打进 exe 的版本号一致）
try {
  execFileSync(process.execPath, [resolve(root, 'scripts/write-version.mjs')], { stdio: 'inherit' });
} catch (e) {
  console.error('[bump] write-version.mjs 执行失败，请手动运行 scripts/write-version.mjs');
  process.exit(1);
}

// 打印发布步骤（CI 在 push tag 后自动构建发布）
const today = new Date().toISOString().slice(0, 10);
console.log('\n== 下一步（复制执行）==');
console.log(`git add version.json            # 版本真相源（src/version.ts 与 native/version.h 由 CI 自动重新生成，已被 .gitignore 忽略）`);
console.log(`git commit -m "chore: 发布 v${newVersion}"`);
console.log(`git tag v${newVersion}`);
console.log(`git push origin main --tags`);
console.log(`\n提示：首次发布还需把本次改造一并提交 ——`);
console.log(`  git add PowerControl .github/workflows/release.yml native/main.cpp scripts/bump-version.mjs package.json`);
console.log(`  （可与上面的 version.json 合并在一次 commit/tag 中）`);
console.log(`\nCI 会自动构建 YeManCC.zip（程序 + PowerControl）并发布到 GitHub Release v${newVersion}，`);
console.log(`再把 sha256/publishedAt 写回 main/version.json。本机稍后打开 YeManCC 即可在「版本和更新」中检测并安装。`);
