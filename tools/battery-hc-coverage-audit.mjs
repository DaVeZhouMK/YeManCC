import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';

// Audit saved Roslyn inventories and pinned references without loading HC or opening hardware.
const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
const opt = name => { const i = args.indexOf(name); return i < 0 ? null : args[i + 1]; };
if (!opt('--evidence') || !opt('--out')) throw Error('usage: node tools/battery-hc-coverage-audit.mjs --evidence <inventory and upstream directory> --out <report directory>');
const evidence = path.resolve(opt('--evidence')), out = path.resolve(opt('--out'));
const read = file => JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
const digest = (bytes, algorithm = 'sha256') => createHash(algorithm).update(bytes).digest('hex');
const manifest = read(path.join(evidence, 'upstream/manifest.json'));
for (const item of manifest) {
  const bytes = fs.readFileSync(item.destination);
  const blob = digest(Buffer.concat([Buffer.from(`blob ${bytes.length}\0`), bytes]), 'sha1');
  if (digest(bytes) !== item.sha256.toLowerCase() || blob !== item.blobSha1) throw Error('pinned reference mismatch: ' + item.path);
}
const hcTree = read(path.join(evidence, 'upstream/Valkirie_HandheldCompanion-tree.json'));
if (hcTree.truncated !== false) throw Error('HC tree is incomplete');
const devicePaths = hcTree.tree.filter(x => x.type === 'blob' && /^HandheldCompanion\/Devices\/.*\.cs$/.test(x.path)).map(x => x.path).sort();
const downloadedDevices = manifest.filter(x => x.repo === 'Valkirie/HandheldCompanion' && /^HandheldCompanion\/Devices\/.*\.cs$/.test(x.path)).map(x => x.path).sort();
if (JSON.stringify(devicePaths) !== JSON.stringify(downloadedDevices)) throw Error('not all upstream HC device sources were downloaded');
const runtimePath = path.join(repo, 'native/BatteryHost/battery-routes.json');
const routes = read(runtimePath);
const data = { schema: 1, evidenceLevel: 'source and recorded logs; no hardware probes or writes', runtimeRouteSha256: digest(fs.readFileSync(runtimePath)), verifiedReferenceFiles: manifest.length, datasets: {} };
for (const label of ['locked', 'upstream']) {
  const inventory = read(path.join(evidence, label + '-inventory.json'));
  if (inventory.issues.length) throw Error(label + ': extractor issues');
  const actualFiles = fs.readdirSync(inventory.sourceRoot, {recursive: true}).filter(f => f.endsWith('.cs')).map(f => f.replaceAll('\\', '/')).sort();
  if (JSON.stringify(actualFiles) !== JSON.stringify(inventory.files.map(f => f.file).sort())) throw Error(label + ': inventory file coverage changed');
  for (const file of inventory.files) if (digest(fs.readFileSync(path.join(inventory.sourceRoot, file.file))) !== file.sha256) throw Error(label + ': stale inventory: ' + file.file);
  const byName = new Map(inventory.devices.map(d => [d.name, d]));
  const rows = inventory.devices.filter(d => d.instantiable).map(d => {
    const chain = d.chain.map(name => { const owner = byName.get(name); if (!owner) throw Error('missing parent ' + name); return owner; });
    const declared = d.declared.Capabilities.possible.filter(name => /^Battery/.test(name));
    const effective = new Map();
    for (const owner of chain) for (const method of owner.methods.filter(m => /Battery|Charge/.test(m.name))) effective.set(method.signature, { owner: owner.name, file: owner.file, ...method });
    const gate = [...effective.values()].find(m => m.name === 'IsBatteryProtectionSupported');
    const wrapper = [...effective.values()].find(m => m.name === 'CheckIsBatteryProtectionSupported');
    const oneX = d.chain.includes('OneXPlayerX1');
    if (oneX && (!wrapper?.returnExpressions.includes('IsBatteryProtectionSupported(majorVersion, minorVersion)') || !wrapper.returnExpressions.includes('false'))) throw Error('unreviewed OneX wrapper');
    const disabled = oneX && gate?.constantBooleanReturn === false;
    const ymcc = routes.filter(r => r.identity.deviceClass === d.name);
    const declaration = declared.includes('BatteryChargeLimit');
    const protocol = !declaration || disabled ? null : d.chain.includes('AYANEODeviceCEii') ? 'ayaneo-ceii-software-bypass' : d.chain.includes('AYANEODeviceCEc') ? 'ayaneo-cec-software-bypass' : d.chain.includes('ClawA1M') ? 'msi-legacy-wmi' : d.chain.includes('LegionGo') ? 'lenovo-fixed-conservation' : oneX ? 'onex-firmware-threshold-and-direct-bypass' : d.chain.includes('ROGAlly') ? 'asus-rsoc-ack-only' : d.name === 'SteamDeck' ? 'steamdeck-firmware-threshold' : 'unclassified';
    if (protocol === 'unclassified') throw Error('unclassified battery implementation: ' + d.name);
    return {
      deviceClass: d.name, family: d.family, fallback: ['ASUS', 'DefaultDevice'].includes(d.name), chain: d.chain,
      source: { file: d.file, line: d.line, sha256: d.sourceSha256 }, declared,
      effectiveStatus: disabled ? 'source-disabled' : declaration ? 'declared-runtime-probe-required' : 'not-declared',
      protocol, firmwareGate: gate ?? null, effectiveChargeMethods: [...effective.values()],
      runtimeCapabilityMutations: chain.flatMap(owner => owner.operations.filter(o => !o.constructor && o.values.some(v => /^Battery/.test(v))).map(o => ({ owner: owner.name, file: owner.file, ...o }))),
      ymccRoutes: ymcc.map(r => ({ ...r, uiChoices: r.kind === 'firmware-fixed' ? [80, 100] : Array.from({ length: (100 - Math.max(50, r.min)) / 5 + 1 }, (_, i) => Math.max(50, r.min) + 5 * i) })),
      coverage: ymcc.length ? 'registered-exact-identity-with-runtime-gates' : disabled ? 'upstream-deliberately-disabled' : !declaration ? 'no-hc-charge-implementation' : d.name === 'SuiPlay0X1' ? 'inherited-protocol-windows-not-admitted' : 'gap-needs-review',
      knownDifference: d.name === 'ClawCG3EM' ? 'YMCC uses field-verified selector-0 Get_AP/Set_AP with independent readback; HC inherits legacy Get_Data/Set_Data' : oneX && !disabled ? 'YMCC registers 0x04A3 threshold only; HC 0x04A4 direct bypass modes are not exposed' : d.name === 'SteamDeck' ? 'YMCC writes one threshold byte; HC setter builds an Int32 byte array' : null,
    };
  });
  const summary = {
    files: inventory.files.length, classes: inventory.devices.length, factoryClasses: rows.length,
    fallbackClasses: rows.filter(r => r.fallback).length,
    batteryDeclarations: rows.filter(r => r.declared.includes('BatteryChargeLimit')).length,
    explicitlyDisabled: rows.filter(r => r.effectiveStatus === 'source-disabled').map(r => r.deviceClass),
    candidatesRequiringRuntimeProbe: rows.filter(r => r.effectiveStatus === 'declared-runtime-probe-required').length,
    candidatesWithYmccRoute: rows.filter(r => r.effectiveStatus === 'declared-runtime-probe-required' && r.ymccRoutes.length).length,
    omittedCandidates: rows.filter(r => r.effectiveStatus === 'declared-runtime-probe-required' && !r.ymccRoutes.length).map(r => r.deviceClass),
    nonHcSupplementRoutes: rows.filter(r => r.effectiveStatus === 'not-declared' && r.ymccRoutes.length).map(r => r.deviceClass),
    families: [...new Set(rows.map(r => r.family))].sort().map(family => {const group = rows.filter(r => r.family === family);return {family, classes: group.length, candidates: group.filter(r => r.effectiveStatus === 'declared-runtime-probe-required').length, ymccRegistered: group.filter(r => r.ymccRoutes.length).length};}),
  };
  data.datasets[label] = { summary, rows };
}
const old = new Map(data.datasets.locked.rows.map(r => [r.deviceClass, r]));
data.upstreamOnly = data.datasets.upstream.rows.filter(r => !old.has(r.deviceClass)).map(r => ({ deviceClass: r.deviceClass, charge: r.effectiveStatus, ymcc: r.coverage }));
data.runtime = { identityRules: routes.length, protocolRoutes: [...new Set(routes.map(r => r.route))], unrepresentedClasses: routes.filter(r => !data.datasets.upstream.rows.some(d => d.deviceClass === r.identity.deviceClass)).map(r => r.identity.deviceClass) };
fs.mkdirSync(out, {recursive: true});
fs.writeFileSync(path.join(out, 'hc-charge-matrix.json'), JSON.stringify(data, null, 2) + '\n');
const lines = ['# HC 逐类充电矩阵（2026-10-16）', '', '逐类解析继承链、充电声明、最终固件判断方法和 YMCC 精确身份规则。候选仍需运行时验证，不等于硬件支持或实测通过。生成命令见 README；完整方法、行号、哈希、条件和档位见同目录 JSON。', ''];
for (const label of ['locked', 'upstream']) {
  const { summary, rows } = data.datasets[label];
  lines.push(`## ${label === 'locked' ? 'YMCC 锁定 HC' : '联网固定的上游 HC'}`, '', `工厂可识别 ${summary.factoryClasses} 类（含 ${summary.fallbackClasses} 兜底）；充电声明 ${summary.batteryDeclarations} 类，明确禁用 ${summary.explicitlyDisabled.length} 类，待运行时探测候选 ${summary.candidatesRequiringRuntimeProbe} 类。`, '', '| 类 | HC 充电实现 | YMCC 充电规则 | 差异 / 边界 |', '| --- | --- | --- | --- |');
  for (const r of rows) lines.push(`| ${r.deviceClass} | ${r.effectiveStatus === 'source-disabled' ? '最终判断返回 false' : r.protocol ?? '无充电声明/协议'} | ${r.ymccRoutes.map(v => v.route).join(', ') || '未登记'} | ${r.knownDifference ?? (r.deviceClass === 'GPDWin5' ? 'G1618-05 无协议；不能套 Mini 2025' : r.coverage === 'inherited-protocol-windows-not-admitted' ? 'SuiPlay Windows 未开放' : r.fallback ? '兜底类，不是机型' : '')} |`);
  lines.push('');
}
fs.writeFileSync(path.join(out, 'hc-charge-matrix.md'), lines.join('\n') + '\n');
console.log(JSON.stringify({ locked: data.datasets.locked.summary, upstream: data.datasets.upstream.summary, runtime: data.runtime, upstreamOnly: data.upstreamOnly }, null, 2));
