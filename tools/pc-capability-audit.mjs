import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { spawnSync } from 'node:child_process';

// Offline, read-only with respect to the product and hardware. Only audit outputs
// and this tool's build artifacts are written. References must already be pinned.
const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
const option = name => { const i = args.indexOf(name); return i < 0 ? null : args[i + 1]; };
if (!option('--references') || !option('--out')) throw Error('usage: node tools/pc-capability-audit.mjs --references <pinned upstream snapshots> --out <report directory>');
const references = path.resolve(option('--references'));
const out = path.resolve(option('--out'));
fs.mkdirSync(out, { recursive: true });
const json = file => JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
const write = (name, value) => fs.writeFileSync(path.join(out, name), JSON.stringify(value, null, 2) + '\n');
const hash = file => createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const run = (name, exe, argv, required = true) => {
  const result = spawnSync(exe, argv, { cwd: repo, encoding: 'utf8', windowsHide: true, timeout: 120000, maxBuffer: 8 * 1024 * 1024 });
  const output = `${result.stdout || ''}${result.stderr || ''}${result.error ? '\n' + result.error.message : ''}`;
  fs.writeFileSync(path.join(out, name + '.log'), output);
  if (required && result.status !== 0) throw Error(`${name} failed; see ${name}.log`);
  return { exitCode: result.status, output, log: name + '.log' };
};
// Validate downloaded bytes against both the recorded digest and the Git tree blob.
const manifest = json(path.join(references, 'source-manifest.json'));
for (const item of manifest) {
  if (item.error) throw Error('incomplete reference download: ' + item.path);
  const bytes = fs.readFileSync(item.destination);
  const sha = createHash('sha256').update(bytes).digest('hex');
  const blob = createHash('sha1').update(`blob ${bytes.length}\0`).update(bytes).digest('hex');
  if (sha !== item.sha256.toLowerCase() || blob !== item.gitBlob) throw Error('reference hash mismatch: ' + item.path);
}
const sourceBefore = new Map(['native/main.cpp', 'FanLab/real-host/Program.cs', 'native/BatteryHost/battery-routes.json', 'FanLab/real-host/FanCapabilityPolicy.cs', 'FanLab/real-host/G1FanContractAdapter.cs'].map(f => [f, hash(path.join(repo, f))]));
run('extractor-build', 'dotnet', ['build', 'tools/PcCapabilityAudit/PcCapabilityAudit.csproj', '--nologo', '-v:q']);
const extractor = path.join(repo, 'tools/PcCapabilityAudit/bin/Debug/net10.0/PcCapabilityAudit.dll');
const roots = { pinned: path.join(repo, 'deps/handheldcompanion-runtime/source/Devices'), upstream: path.join(references, 'sources/hc/HandheldCompanion/Devices') };
const inventories = {}, audits = {};
for (const [label, root] of Object.entries(roots)) {
  run(label + '-inventory', 'dotnet', [extractor, root, path.join(out, label + '-inventory.json')]);
  inventories[label] = json(path.join(out, label + '-inventory.json'));
  audits[label] = {};
  for (const name of ['fan_route_coverage_audit', 'lighting_capability_coverage_audit', 'gyro_matrix_coverage_audit', 'oem_key_coverage_audit', 'input_binding_coverage_audit']) {
    const argv = ['-NoProfile', '-File', path.join(repo, 'tools', name + '.ps1'), '-HcDevicesRoot', root];
    if (name === 'fan_route_coverage_audit' || name === 'oem_key_coverage_audit') argv.push('-OutputRoot', path.join(out, label + '-' + name));
    const result = run(label + '-' + name, 'pwsh', argv, false);
    audits[label][name] = { exitCode: result.exitCode, log: result.log, findings: result.output.split(/\r?\n/).filter(l => /^(?:fan-route-missing:|lighting-set:|second-color:|led-effects:|matrix-|chord-missing:)/.test(l)) };
  }
}
const current = inventories.pinned.devices, latest = inventories.upstream.devices;
const fan = json(path.join(out, 'pinned-fan_route_coverage_audit/fan-capability-matrix.json'));
const fanRoutes = fan.routes;
if (!Array.isArray(fanRoutes)) throw Error('fan report schema changed');
const battery = json(path.join(repo, 'native/BatteryHost/battery-routes.json'));
const main = fs.readFileSync(path.join(repo, 'native/main.cpp'), 'utf8');
const resolver = main.slice(main.indexOf('static void ymccResolveFamily(MachineIdentity& id)'), main.indexOf('static const std::vector<std::string>& ymccDeviceClassLightingSet()'));
const identityClasses = [...resolver.matchAll(/"HandheldCompanion\.Devices\.([\w.]+)"/g)].map(m => m[1].split('.').at(-1));
const fixtures = [...main.matchAll(/MI_RUN\("([^"]+)",\s*(\w+),\s*YmccFamily::(\w+),\s*"([^"]*)"\)/g)].map(m => ({ name: m[1], variable: m[2], family: m[3], deviceClass: m[4].split('.').at(-1) }));
const bodyOf = signature => {
  const start = main.indexOf(signature); if (start < 0) throw Error('missing table: ' + signature);
  return main.slice(start, main.indexOf('};', start));
};
const light = [...bodyOf('ymccDeviceClassLightingSet()').matchAll(/"HandheldCompanion\.Devices\.([\w.]+)"/g)].map(m => m[1].split('.').at(-1));
const chain = (entry, catalog) => entry.chain.map(n => catalog.find(d => d.name === n)).filter(Boolean);
const lifecycle = (entry, catalog) => {
  const resolved = {};
  for (const owner of chain(entry, catalog)) for (const method of owner.methods)
    if (/^(Open|Close)$|Fan|Battery|OEM|Led|LED/.test(method.name))
      resolved[method.signature] = { owner: owner.name, file: owner.file, line: method.line, signature: method.signature, sourceSha256: method.sourceSha256 };
  return resolved;
};
const rows = latest.filter(d => d.instantiable).map(d => {
  const pinned = current.find(x => x.name === d.name);
  const routes = battery.filter(x => x.identity.deviceClass === d.name).map(r => ({ ...r, uiMinimum: Math.max(50, r.min), uiChoices: r.kind === 'firmware-fixed' ? [80, 100] : Array.from({ length: (100 - Math.max(50, r.min)) / 5 + 1 }, (_, i) => Math.max(50, r.min) + i * 5) }));
  const batteryGate = chain(d, latest).flatMap(owner => owner.methods.filter(m => m.name === 'IsBatteryProtectionSupported').map(method => ({ owner: owner.name, file: owner.file, line: method.line, constantBooleanReturn: method.constantBooleanReturn, sourceSha256: method.sourceSha256 }))).at(-1);
  // Reviewed HC X1 path: CheckIsBatteryProtectionSupported returns the virtual
  // IsBatteryProtectionSupported result or false; X2/3 explicitly return false.
  const wrapper = latest.find(x => x.name === 'OneXPlayerX1')?.methods.find(m => m.name === 'CheckIsBatteryProtectionSupported');
  const reviewedWrapper = wrapper?.returnExpressions?.length === 2 && wrapper.returnExpressions.includes('IsBatteryProtectionSupported(majorVersion, minorVersion)') && wrapper.returnExpressions.includes('false');
  const chargeGate = d.chain.includes('OneXPlayerX1') && reviewedWrapper && batteryGate?.constantBooleanReturn === false
    ? { status: 'source-disabled', ...batteryGate }
    : { status: d.declared.Capabilities.possible.includes('BatteryChargeLimit') ? 'declared-probe-still-required' : 'not-declared' };
  return {
    upstreamChargeGate: chargeGate,
    deviceClass: d.name, fullName: d.fullName, family: d.family, fallback: ['ASUS', 'DefaultDevice'].includes(d.name),
    pinnedPresent: Boolean(pinned), nativeIdentityLiteralPresent: identityClasses.includes(d.name), identityFixtures: fixtures.filter(x => x.deviceClass === d.name),
    pinnedConstructorFlags: pinned?.declared ?? null, upstreamConstructorFlags: d.declared,
    pinnedRuntimeCapabilityMutations: pinned ? chain(pinned, current).flatMap(x => x.operations.filter(o => !o.constructor).map(o => ({ owner: x.name, file: x.file, ...o }))) : [],
    upstreamRuntimeCapabilityMutations: chain(d, latest).flatMap(x => x.operations.filter(o => !o.constructor).map(o => ({ owner: x.name, file: x.file, ...o }))),
    fanRuntimePolicyNote: ['OneXPlayerApex','OneXPlayerX1AMD','OneXPlayerX1Intel','OneXPlayerX1Mini','OneXPlayerX1Pro'].includes(d.name) ? 'FanCapabilityPolicy admits only the exact inherited EC contract despite the pinned declaration wipe; source eligibility, not hardware proof' : null,
    pinnedFanRoute: fanRoutes.find(r => r.deviceClass === d.name) ?? null, nativeLightingTablePresent: light.includes(d.name), batteryRoutes: routes,
    pinnedLifecycleSources: pinned ? lifecycle(pinned, current) : null, upstreamLifecycleSources: lifecycle(d, latest),
    upstreamHardwareProperties: chain(d, latest).flatMap(x => x.properties.map(v => ({ owner: x.name, file: x.file, ...v }))),
    upstreamOemChordOperations: chain(d, latest).flatMap(x => x.oemChords.map(v => ({ owner: x.name, file: x.file, ...v }))),
    deviceEvidence: d.name === 'ClawCG3EM' ? { domain: 'battery', status: 'set-and-independent-readback', board: 'MS-1T91', bios: 'E1T91IMS.105', target: 80, evidence: path.resolve(repo, '../../../Build/Validation/BatteryCharge-20261014-Compact/field-evidence.json'), physicalCurrentCutoffMeasured: false } : { status: 'not-revalidated-in-this-audit' },
  };
});
const families = [...new Set(rows.map(r => r.family))].sort().map(family => {
  const group = rows.filter(r => r.family === family);
  const flag = name => group.filter(r => r.upstreamConstructorFlags.Capabilities.possible.includes(name)).length;
  return { family, pinnedTypes: group.filter(r => r.pinnedPresent).length, upstreamTypes: group.length, nativeIdentityLiterals: group.filter(r => r.nativeIdentityLiteralPresent).length,
    upstreamFanDeclarations: flag('FanControl'), registeredFanRoutes: group.filter(r => r.pinnedFanRoute).length, upstreamLightingDeclarations: flag('DynamicLighting'), registeredLightingEntries: group.filter(r => r.nativeLightingTablePresent).length,
    upstreamChargeDeclarations: group.filter(r => r.upstreamConstructorFlags.Capabilities.possible.includes('BatteryChargeLimit') && r.upstreamChargeGate.status !== 'source-disabled').length, registeredChargeIdentities: group.filter(r => r.batteryRoutes.length).length, upstreamOemCpuDeclarations: flag('OEMCPU'), upstreamOemGpuDeclarations: flag('OEMGPU') };
});
const ctw = json(path.join(references, 'ctw-release.json')), hc = json(path.join(references, 'hc-release.json'));
const sources = { verifiedReferenceFiles: manifest.length, pinned: json(path.join(repo, 'deps/handheldcompanion-runtime/HC-BASELINE.json')), hc: { tag: hc.tag_name, published: hc.published_at, commit: json(path.join(references, 'hc-release-commit.json')).sha },
  ctw: { tag: ctw.tag_name, published: ctw.published_at, releaseCommit: json(path.join(references, 'ctw-release-commit.json')).sha, inspectedDefaultCommit: json(path.join(references, 'ctw-tree.json')).sha, releaseHasDeviceInfo: json(path.join(references, 'ctw-release-tree.json')).tree.some(x => /Shared\/Data\/DeviceInfo\.cs$/.test(x.path)), note: 'Default-branch sources and archived 0.4.0 supplement only; never attributed to 0.4.2 binary behavior.' } };
const ctwSourceRoot = path.join(references, 'sources/ctw-default');
const ctwArchiveRoot = path.resolve(repo, '../../../Archives/ClawTweaks/ClawTweaks-0.4.0');
const ctwFiles = ['Shared/Data/DeviceInfo.cs', 'Shared/Data/ClawHardwareId.cs', 'Shared/Enums/DeviceType.cs', 'XboxGamingBar/Features/Devices/GamingWidget.MsiFanControl.cs', 'XboxGamingBar/Features/Devices/GamingWidget.MsiClawSettings.cs'];
const ctwComparisons = ctwFiles.map(file => ({ file, defaultSha256: hash(path.join(ctwSourceRoot, file)), archived040Sha256: fs.existsSync(path.join(ctwArchiveRoot, file)) ? hash(path.join(ctwArchiveRoot, file)) : null }));
const info = fs.readFileSync(path.join(ctwSourceRoot, 'Shared/Data/DeviceInfo.cs'), 'utf8');
const ctwDeclaredDefaults = [...info.matchAll(/public\s+(bool|int)\s+(\w+)\s*\{\s*get;\s*set;\s*\}\s*=\s*([^;]+);/g)].map(m => ({ type: m[1], name: m[2], default: m[3].trim(), line: info.slice(0, m.index).split('\n').length }));
const ctwSupplement = { sources: sources.ctw, files: ctwComparisons, declaredDefaults: ctwDeclaredDefaults, defaultsAreNotDetection: true,
  identity: { modelsInResolver: ['A2VM','Ex'], excludedBoard: 'MS-1T41', ladder: ['MSI manufacturer plus explicit product', 'board/SKU 1T52/1T42/1T91', 'CPU platform plus physical controller evidence'], note: 'DeviceType.MSIClaw comment includes A1M, but ClawHardwareId deliberately excludes it; use executable resolver evidence.' },
  limits: ['private hardware helper implementation not supplied by these source trees', 'default-true CPU/driver controls are UI defaults, not hardware support proof', 'EX gyro world-space default is a preference, not an absent gyroscope', 'frontend charge/fan UI and max-PL values cannot establish protocol readback or original OEM restore', '0.4.2 release tree does not contain the inspected DeviceInfo/ClawHardwareId sources'] };
write('ctw-supplement.json', ctwSupplement);
const modified = [...sourceBefore].filter(([file, before]) => hash(path.join(repo, file)) !== before).map(([file]) => file);
if (modified.length) throw Error('concurrent source change; rerun against one snapshot: ' + modified.join(', '));
const report = { schemaVersion: 1, generatedAt: new Date().toISOString(), priority: ['HC', 'CTW'], scope: 'PC/OEM model-specific declarations, identity literals/fixtures, fan/light/gyro/OEM-key static audits, charge routes, lifecycle source anchors; not end-to-end device validation', sourceHashes: Object.fromEntries(sourceBefore), sources, ctwSupplement, audits, families, rows,
  newTypes: rows.filter(r => !r.pinnedPresent).map(r => r.deviceClass), declarationChanges: rows.filter(r => r.pinnedPresent && JSON.stringify(r.pinnedConstructorFlags) !== JSON.stringify(r.upstreamConstructorFlags)).map(r => r.deviceClass),
  chargeDeclarationWithoutRoute: rows.filter(r => r.upstreamConstructorFlags.Capabilities.possible.includes('BatteryChargeLimit') && r.upstreamChargeGate.status !== 'source-disabled' && !r.batteryRoutes.length).map(r => r.deviceClass) };
write('pc-capability-matrix.json', report);
const symbol = b => b ? '有' : '—';
const md = ['# PC 能力库全量静态审计（HC → CTW）', '', `生成时间：${report.generatedAt}。固定源码来源、哈希和逐项证据见同目录 JSON。`, '',
'本表中的“有”表示源码声明或已登记路由；不等同设备读写、退出恢复、睡眠恢复或物理效果验证。已知历史实机证据保留在各域能力册；本轮仅新增 G3E 充电成功证据的交叉索引。', '',
`主线可识别 ${current.filter(d => d.instantiable).length} 类；新版 ${rows.length} 类（其中 ASUS、DefaultDevice 为兜底类）。新增 ${report.newTypes.length} 类。充电登记 ${battery.length} 条身份规则。`, '',
'## 家族总表', '', '| 家族 | 主线/新版类 | HC 风扇/主线路由 | HC 灯光/主线表 | HC 充电声明/主线身份 | HC OEM CPU/GPU |', '|---|---:|---:|---:|---:|---:|',
...families.map(f => `| ${f.family} | ${f.pinnedTypes}/${f.upstreamTypes} | ${f.upstreamFanDeclarations}/${f.registeredFanRoutes} | ${f.upstreamLightingDeclarations}/${f.registeredLightingEntries} | ${f.upstreamChargeDeclarations}/${f.registeredChargeIdentities} | ${f.upstreamOemCpuDeclarations}/${f.upstreamOemGpuDeclarations} |`), '',
'HC 列取构造期可能声明，包含固件条件；充电列额外排除 IsBatteryProtectionSupported 明确返回 false 的 OneX 3/X2/X2MiniPro。Generic=通用兜底。GPD 的充电路线来自独立证据，故主线路线可以多于 HC 声明。OEM CPU/GPU 指厂家接口声明，不表示其余设备没有通用 TDP 控制。', '',
'## 分域检查', '', '| 域 | 主线固定源 | 新版 HC 对比 |', '|---|---|---|',
...Object.keys(audits.pinned).map(n => `| ${n} | [${audits.pinned[n].exitCode === 0 ? '静态 PASS' : '发现问题'}](${audits.pinned[n].log}) | [${audits.upstream[n].exitCode === 0 ? '静态 PASS' : '发现差异'}](${audits.upstream[n].log}) |`), '',
'## 逐机型入口', '', '| 家族 | 类型 | 主线HC | 原生身份文字/fixture数 | 风扇路线 | 灯光表 | 充电身份 |', '|---|---|---|---|---|---|---|',
...rows.map(r => `| ${r.family} | ${r.deviceClass} | ${symbol(r.pinnedPresent)} | ${symbol(r.nativeIdentityLiteralPresent)}/${r.identityFixtures.length} | ${symbol(r.pinnedFanRoute)} | ${symbol(r.nativeLightingTablePresent)} | ${r.batteryRoutes.map(x => x.kind).join(', ') || '—'} |`), '',
'身份文字命中不是执行识别用例；fixture 计数也不是本轮实际运行通过。完整 JSON 同时保留命名空间、继承链、动态能力修改、固件门、可选充电数值、生命周期方法及 TDP/EC 原始声明。', '',
'## 差异集合', '', `- 新类型：${report.newTypes.join('、')}。`, `- 既有类型构造能力变化：${report.declarationChanges.join('、')}。`, `- HC 充电声明但没有 YMCC 充电路线：${report.chargeDeclarationWithoutRoute.join('、')}。`, '',
'默认仅输出报告。上游差异不会自动扩大产品硬件写入范围；各域日志中的 FAIL 保留真实差异，不能改成全量兼容 PASS。', ''];
fs.writeFileSync(path.join(out, 'pc-capability-matrix.md'), md.join('\n'));
console.log(JSON.stringify({ report: path.join(out, 'pc-capability-matrix.md'), sourceFiles: manifest.length, currentTypes: current.filter(d => d.instantiable).length, upstreamTypes: rows.length, newTypes: report.newTypes, changedDeclarations: report.declarationChanges, chargeDeclarationWithoutRoute: report.chargeDeclarationWithoutRoute }, null, 2));
// Upstream gaps are expected audit findings. A failure of the pinned gates is a regression.
if (Object.values(audits.pinned).some(a => a.exitCode !== 0)) process.exitCode = 1;
