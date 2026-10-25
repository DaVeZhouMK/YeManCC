// Missing-only resource repair. Never edits settings, link files, live themes,
// environment variables, native binaries, BAT/VBS, or starts any program.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
export function planSteamAppearanceResources(sourcePowerControl, targetPowerControl) {
  const source = path.resolve(sourcePowerControl), target = path.resolve(targetPowerControl);
  assert(fs.statSync(source).isDirectory() && fs.statSync(target).isDirectory(), 'Explicit source and target directories must exist');
  assert(!fs.lstatSync(target).isSymbolicLink() && fs.realpathSync(target).toLowerCase() === target.toLowerCase(), 'Do not deploy into an indirect/reparse target');
  const manifest = JSON.parse(fs.readFileSync(path.join(source, 'steam-appearance/manifest.json'), 'utf8').replace(/^\uFEFF/, ''));
  assert(Array.isArray(manifest.files) && manifest.files.length > 0, 'Invalid theme manifest');
  const listed = ['steam-appearance.ps1', 'steam-appearance/manifest.json', 'steam-appearance/preset.ps1', 'steam-appearance/idle.ps1', ...manifest.files.map(row => 'steam-appearance/themes/' + row.path)];
  assert(new Set(listed.map(p => p.toLowerCase())).size === listed.length, 'Duplicate manifest paths');
  assert(listed.every(relative => !relative.split(/[\\/]/).some(p => p === '..' || p === '.' || !p) && !path.isAbsolute(relative) && !relative.includes(':')), 'Unsafe resource path');
  const sha = data => createHash('sha256').update(data).digest('hex');
  const files = listed.map(relative => {
    assert(!relative.split(/[\\/]/).some(p => p === '..' || p === '.' || !p) && !path.isAbsolute(relative) && !relative.includes(':'), 'Unsafe resource path');
    const sourcePath = path.join(source, relative), targetPath = path.join(target, relative);
    assert(targetPath.toLowerCase().startsWith(target.toLowerCase() + path.sep), 'Resource outside explicit target');
    for (let p = targetPath; p.toLowerCase() !== target.toLowerCase(); p = path.dirname(p)) {
      if (fs.existsSync(p)) assert(!fs.lstatSync(p).isSymbolicLink(), 'Reparse destination: ' + p);
    }
    const bytes = fs.readFileSync(sourcePath), hash = sha(bytes);
    const row = manifest.files.find(r => relative === 'steam-appearance/themes/' + r.path);
    if (row) assert(hash === row.sha256.toLowerCase() && bytes.length === row.bytes, 'Source resource does not match manifest: ' + relative);
    const exists = fs.existsSync(targetPath);
    if (exists) assert(fs.statSync(targetPath).isFile() && sha(fs.readFileSync(targetPath)) === hash, 'A different destination component already exists; controlled update required: ' + targetPath);
    return { relative, sourcePath, targetPath, sha256: hash, action: exists ? 'unchanged' : 'copy-missing' };
  });
  return { source, target, files };
}
export function repairSteamAppearanceResources(plan) {
  const installed = [];
  for (const item of plan.files) {
    if (item.action !== 'copy-missing') continue;
    // Re-plan before each copy to fail closed if directories/content changed.
    const current = planSteamAppearanceResources(plan.source, plan.target).files.find(f => f.relative === item.relative);
    if (current.action === 'unchanged') continue;
    assert(current.sha256 === item.sha256, 'Source changed after planning');
    fs.mkdirSync(path.dirname(item.targetPath), { recursive: true });
    fs.copyFileSync(item.sourcePath, item.targetPath, fs.constants.COPYFILE_EXCL);
    installed.push(item.targetPath);
  }
  const verified = planSteamAppearanceResources(plan.source, plan.target);
  assert(verified.files.every(f => f.action === 'unchanged'), 'Incomplete resource repair');
  return { ok: true, installed, verifiedFiles: verified.files.length };
}
if (process.argv[1] && path.resolve(process.argv[1]).toLowerCase() === path.resolve(import.meta.filename).toLowerCase()) {
  const [source, target, out, mode] = process.argv.slice(2);
  assert(source && target && out, 'Usage: node tool <source PowerControl> <explicit target PowerControl> <audit JSON> [--apply]');
  const plan = planSteamAppearanceResources(source, target);
  const result = mode === '--apply' ? repairSteamAppearanceResources(plan) : { dryRun: true };
  fs.writeFileSync(path.resolve(out), JSON.stringify({ checkedAt: new Date().toISOString(), scope: 'Missing-only Steam appearance resources; no live Steam/CSS actions', plan, result }, null, 2));
  console.log(JSON.stringify({ target: plan.target, files: plan.files.length, missing: plan.files.filter(f => f.action === 'copy-missing').length, ...result }, null, 2));
}
