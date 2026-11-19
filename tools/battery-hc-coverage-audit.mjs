import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { assertBatteryAuditRouteRows, auditBatteryRouteIdentity } from './lib/battery_route_identity_audit.mjs';
import { projectBatteryFactoryRoutes } from './lib/battery_hc_factory_projection.mjs';
import { buildBatteryArtifacts } from './lib/hc_battery_catalog.mjs';

// Audit saved Roslyn inventories and pinned references without loading HC or opening hardware.
const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
const opt = name => { const i = args.indexOf(name); return i < 0 ? null : args[i + 1]; };
const factoryBoard = opt('--print-factory-projection');
if (args.includes('--print-factory-projection') && (!args.includes('--check') || !factoryBoard || factoryBoard.startsWith('--')))
  throw Error('--print-factory-projection requires --check and the pinned upstream MotherboardInfo.cs');
if (args.includes('--onex-oem-background') && !opt('--onex-oem-background')) throw Error('missing --onex-oem-background path');
if (!opt('--evidence') || !opt('--out')) throw Error('usage: node tools/battery-hc-coverage-audit.mjs --evidence <inventory and upstream directory> --out <report directory> [--check]');
const evidence = path.resolve(opt('--evidence')), out = path.resolve(opt('--out'));
const date = opt('--date') ?? new Date().toISOString().slice(0, 10);
const stem = opt('--stem') ?? 'hc-charge-matrix';
if (!/^\d{4}-\d{2}-\d{2}$/.test(date) || !/^[A-Za-z0-9_-]+$/.test(stem)) throw Error('invalid report date or filename');
const read = file => JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
const digest = (bytes, algorithm = 'sha256') => createHash(algorithm).update(bytes).digest('hex');
// Reviewed PcCapabilityAudit outputs. A source manifest alone cannot authenticate the parsed rows.
const inventorySha256 = {
  locked: '8709dc3c8d3008fbab98f49033a76638a9411cfa1431e437c2afe44951c19f19',
  upstream: 'e37fb7fd4ba9c9c9ee3720d2c112928e52e1b9708e1dfdbe3d57012feb4a8793',
};
const hcRepo = 'Valkirie/HandheldCompanion';
const devicePrefix = 'HandheldCompanion/Devices/';
const deviceSource = file => file.startsWith(devicePrefix) && file.endsWith('.cs');
const gitSha = /^[a-f0-9]{40}$/;
const commit = read(path.join(evidence, 'upstream/commit.json'));
const hcTree = read(path.join(evidence, 'upstream/Valkirie_HandheldCompanion-tree.json'));
const treeSha = commit.commit?.tree?.sha;
if (!gitSha.test(commit.sha) || !gitSha.test(treeSha) || ![commit.sha, treeSha].includes(hcTree.sha) || hcTree.truncated !== false)
  throw Error('HC tree is incomplete or not bound to the recorded commit');

// The API capture can use the commit SHA as its top-level response ID.
// Check the root and each directory against the actual Git tree object IDs.
const entries = new Map(), children = new Map();
const modes = {blob: ['100644', '100755', '120000'], tree: ['040000'], commit: ['160000']};
for (const entry of hcTree.tree) {
  const name = entry.path;
  if (typeof name !== 'string' || /[\\\0]/.test(name) || name.split('/').some(part => !part || part === '.' || part === '..') ||
      entries.has(name) || !gitSha.test(entry.sha) || !Object.hasOwn(modes, entry.type) || !modes[entry.type].includes(entry.mode))
    throw Error('invalid HC tree entry: ' + name);
  entries.set(name, entry);
  const dir = path.posix.dirname(name);
  if (!children.has(dir)) children.set(dir, []);
  children.get(dir).push(entry);
}
for (const [dir, refs] of children) {
  const parent = entries.get(dir);
  if (dir !== '.' && parent?.type !== 'tree') throw Error('HC tree directory missing: ' + dir);
  const sorted = refs.map(entry => {
    const name = path.posix.basename(entry.path);
    return {key: Buffer.from(name + (entry.type === 'tree' ? '/' : '')),
      bytes: Buffer.concat([Buffer.from(`${entry.mode.replace(/^0/, '')} ${name}\0`), Buffer.from(entry.sha, 'hex')])};
  }).sort((a, b) => Buffer.compare(a.key, b.key));
  const body = Buffer.concat(sorted.map(entry => entry.bytes));
  const actual = digest(Buffer.concat([Buffer.from(`tree ${body.length}\0`), body]), 'sha1');
  if (actual !== (dir === '.' ? treeSha : parent.sha)) throw Error('HC tree object mismatch: ' + dir);
}
if (!children.has('.') || [...entries.values()].some(entry => entry.type === 'tree' && !children.has(entry.path)))
  throw Error('HC tree response is missing directory contents');

const manifest = read(path.join(evidence, 'upstream/manifest.json'));
const reviewedDevices = new Map();
for (const item of manifest) {
  const bytes = fs.readFileSync(item.destination);
  const blob = digest(Buffer.concat([Buffer.from(`blob ${bytes.length}\0`), bytes]), 'sha1');
  const sha256 = digest(bytes);
  if (sha256 !== item.sha256.toLowerCase() || blob !== item.blobSha1) throw Error('pinned reference mismatch: ' + item.path);
  if (item.repo !== hcRepo) continue;
  const entry = entries.get(item.path);
  if (item.commit !== commit.sha || entry?.type !== 'blob' || blob !== entry.sha || blob !== item.gitBlob)
    throw Error('HC manifest does not match the recorded commit tree: ' + item.path);
  if (deviceSource(item.path)) {
    const file = item.path.slice(devicePrefix.length);
    if (reviewedDevices.has(file)) throw Error('duplicate HC device source: ' + file);
    reviewedDevices.set(file, sha256);
  }
}
const devicePaths = hcTree.tree.filter(entry => entry.type === 'blob' && deviceSource(entry.path));
if (!devicePaths.length || devicePaths.length !== reviewedDevices.size ||
    devicePaths.some(entry => !reviewedDevices.has(entry.path.slice(devicePrefix.length))))
  throw Error('not all upstream HC device sources were downloaded');
const runtimePath = path.join(repo, 'native/BatteryHost/battery-routes.json');
const routeBytes = fs.readFileSync(runtimePath);
const routes = JSON.parse(routeBytes.toString('utf8'));
const cataloguePath = path.join(repo, 'capabilities/hc-device-catalog.json');
const catalogue = read(cataloguePath), battery = catalogue.domains?.battery;
const routeHash = digest(routeBytes);
assertBatteryAuditRouteRows(battery, routeBytes);
const generatedBattery = buildBatteryArtifacts({repo, catalog: catalogue});
if (JSON.stringify(generatedBattery.metadata) !== JSON.stringify(battery)) throw Error('generated battery metadata differs');
for (const [file, expected] of Object.entries(generatedBattery.artifacts)) {
  if (fs.readFileSync(path.join(repo, file), 'utf8') !== expected) throw Error('generated battery source differs: ' + file);
}
const fieldInput = opt('--field-evidence');
if (args.includes('--field-evidence') && (!fieldInput || fieldInput.startsWith('--')))
  throw Error('BATTERY_COVERAGE_FIELD_SUMMARY_PATH_REQUIRED');
// The field renderer describes the reviewed APEX rejection, not arbitrary session summaries.
let rawField = null;
if (fieldInput) {
  const bytes = fs.readFileSync(path.resolve(fieldInput));
  if (digest(bytes) !== 'b54c5ab77a9c6de26e7a224650b5444c51df7e02d361020e1b22351adfa52884')
    throw Error('BATTERY_COVERAGE_FIELD_SUMMARY_NEEDS_REVIEW: ' + fieldInput);
  rawField = JSON.parse(bytes.toString('utf8').replace(/^\uFEFF/, ''));
}
const fields = rawField ? Object.fromEntries(Object.entries(rawField).filter(([key]) => key !== 'results')) : null;
// The optional Windows profile is audit evidence only. Verify each preserved artifact,
// and retain the explicit native-contract/admission refusal instead of promoting data.
const windowsOem = fields?.upstreamComparison?.windowsOem ?? null;
if (windowsOem) {
  if (windowsOem.exactProductLabel !== 'ONEXPLAYER APEX' || fields.actualIdentity?.boardProduct !== windowsOem.exactProductLabel || windowsOem.nativeImplementationRecovered !== false || windowsOem.runtimeAdmissionChanged !== false) throw Error('Windows OEM review is not an exact source-only APEX profile');
  for (const [name, ref] of Object.entries(windowsOem.references ?? {})) {
    if (!ref?.path || !/^[0-9a-f]{64}$/i.test(ref.sha256 ?? '') || !Number.isSafeInteger(ref.bytes)) throw Error('incomplete Windows OEM reference: ' + name);
    const bytes = fs.readFileSync(ref.path);
    if (bytes.length !== ref.bytes || digest(bytes) !== ref.sha256.toLowerCase()) throw Error('Windows OEM reference mismatch: ' + name);
  }
  const refs = windowsOem.references;
  if (!refs?.installer || !refs.authenticodeObservation || !refs.packageMetadata || !refs.backgroundSource || !refs.profileBindingObservation || !refs.nativeRecoveryLimit || !refs.correction) throw Error('missing Windows OEM evidence component');
  const signature = read(refs.authenticodeObservation.path);
  if (signature.status !== 'Valid' || !/One-Netbook/i.test(signature.signerSubject ?? '') || path.resolve(signature.file) !== path.resolve(refs.installer.path)) throw Error('Windows OEM recorded signer/file identity mismatch');
  if (read(refs.packageMetadata.path).version !== windowsOem.version || read(refs.nativeRecoveryLimit.path).protocolAdmitted !== false) throw Error('Windows OEM version or native-contract boundary mismatch');
}
const auxiliaryNative = windowsOem?.auxiliaryNativeContracts ?? null;
if (auxiliaryNative) {
  if (auxiliaryNative.schemaVersion !== 1 || auxiliaryNative.status !== 'READABLE_GENERIC_PROTOCOL_ONLY_APEX_BACKEND_LINK_UNRECOVERED' || auxiliaryNative.wmiContractReadable !== true || auxiliaryNative.apexChargingBackendCallSiteRecovered !== false || auxiliaryNative.runtimeAdmissionChanged !== false || auxiliaryNative.runtimeProviderAdded !== false || auxiliaryNative.motorLevelReadbackKnown !== false) throw Error('auxiliary native evidence must not promote APEX runtime admission');
  const refs = windowsOem.references;
  for (const name of ['auxiliaryDeviceComm', 'auxiliaryContractReview', 'auxiliaryAddressSource', 'auxiliaryWmiSource', 'auxiliaryParameterPacketSource', 'auxiliaryIlVerification']) if (!refs[name]) throw Error('missing auxiliary native source: ' + name);
  const review = read(refs.auxiliaryContractReview.path);
  if (review.status !== auxiliaryNative.status || review.source?.sha256 !== refs.auxiliaryDeviceComm.sha256 || review.apexChargingBackendCallSiteRecovered !== false || review.runtimeAdmissionChanged !== false || review.targetCodeExecuted !== false || review.nativeWmi?.class !== 'SuRwECRegInterface') throw Error('auxiliary native review identity or scope mismatch');
}
const apexDifference = windowsOem
  ? 'HC/current YMCC inherits X1 direct EC16 0x04A3 and component gate; signed OEM ' + windowsOem.version + ' exact APEX JavaScript profile initializes 0x04E5/0x04E6/0x04E7. Native Windows access/transaction not recovered; Linux ACPI8 0xE5 is separate; field rejected before provider, no register/gate change'
  : 'HC inherits X1 direct EC16 0x04A3 and component EC gate; Linux Apex uses ACPI8 0xE5/0xE6; field rejected before provider, no cross-transport substitution';

const data = { schema: 1, auditedOn: date, role: 'derived audit inside the sole YMCC capabilities library; not a registration or admission input', runtimeConsumer: false, registrationAuthority: 'capabilities/hc-battery-policy.json -> native/BatteryHost/battery-routes.json -> capabilities/hc-device-catalog.json -> HcBatteryCapabilityCatalog.generated.cs', evidenceLevel: 'source and recorded logs; no hardware probes or writes', runtimeRouteSha256: routeHash, catalogueSha256: digest(fs.readFileSync(cataloguePath)), verifiedReferenceFiles: manifest.length, previousSourceComparison: fs.existsSync(path.join(evidence, 'upstream/delta-since-f2e58468.json')) ? read(path.join(evidence, 'upstream/delta-since-f2e58468.json')) : null, upstream: { repository: hcRepo, commit: commit.sha, treeSha, files: devicePaths.length, inventorySourceBinding: 'device C# hashes match the downloaded blobs in the reconstructed Git tree' }, fieldEvidence: fields, datasets: {} };
for (const label of ['locked', 'upstream']) {
  const inventoryBytes = fs.readFileSync(path.join(evidence, label + '-inventory.json'));
  if (digest(inventoryBytes) !== inventorySha256[label]) throw Error('BATTERY_COVERAGE_INVENTORY_NEEDS_REVIEW: ' + label);
  const inventory = JSON.parse(inventoryBytes.toString('utf8').replace(/^\uFEFF/, ''));
  if (inventory.issues.length) throw Error(label + ': extractor issues');
  const actualFiles = fs.readdirSync(inventory.sourceRoot, {recursive: true}).filter(f => f.endsWith('.cs')).map(f => f.replaceAll('\\', '/')).sort();
  if (JSON.stringify(actualFiles) !== JSON.stringify(inventory.files.map(f => f.file).sort())) throw Error(label + ': inventory file coverage changed');
  if (label === 'upstream') {
    if (inventory.files.length !== reviewedDevices.size) throw Error('upstream inventory does not cover the recorded HC device tree');
    for (const file of inventory.files) if (reviewedDevices.get(file.file) !== file.sha256)
      throw Error('upstream inventory source differs from the recorded HC commit: ' + file.file);
  }
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
    const protocol = !declaration || disabled ? null : d.chain.includes('AYANEODeviceCEii') ? 'ayaneo-ceii-software-bypass' : d.chain.includes('AYANEODeviceCEc') ? 'ayaneo-cec-software-bypass' : d.chain.includes('ClawA1M') ? 'msi-legacy-wmi' : d.chain.includes('LegionGo') ? 'lenovo-fixed-conservation' : oneX ? (d.name === 'OneXPlayer3' && gate?.constantBooleanReturn === true ? 'onex3-wmi-ec-threshold-and-bypass' : 'onex-direct-ec-threshold-and-bypass') : d.chain.includes('ROGAlly') ? 'asus-rsoc-ack-only' : d.name === 'SteamDeck' ? 'steamdeck-firmware-threshold' : 'unclassified';
    if (protocol === 'unclassified') throw Error('unclassified battery implementation: ' + d.name);
    return {
      deviceClass: d.name, family: d.family, fallback: ['ASUS', 'DefaultDevice'].includes(d.name), chain: d.chain,
      source: { file: d.file, line: d.line, sha256: d.sourceSha256 }, declared,
      identityEvidence: inventory.identities.filter(i => i.deviceClass === d.name),
      consumerEvidence: ymcc.map(r => ({ ruleId: battery.rows.find(row => row.provider === r.route && row.deviceClass === d.name)?.ruleId, kind: r.kind, readback: r.route === 'asus-ally-acpi' ? 'write-ack-only; current threshold unknown' : r.kind === 'software-bypass-threshold' ? 'pause/resume byte readback; threshold is a software target' : 'independent hardware threshold readback required', floor: r.kind === 'firmware-fixed' ? 80 : Math.max(50, r.min) })),
      fieldStatus: fields?.actualIdentity?.boardProduct === 'ONEXPLAYER APEX' && d.name === 'OneXPlayerApex' ? 'FIELD_FAILED_FIRMWARE_GATE_BEFORE_PROVIDER; NOT_REPAIRED' : ['ROGAlly','ROGAllyX','XboxROGAlly','XboxROGAllyX','ClawA1M','ClawA2VM','ClawBZ2EM','ClawCG3EM'].includes(d.name) ? 'user-reports-family-basically-working; no new per-model hardware acceptance' : 'not-hardware-validated-in-this-audit',
      effectiveStatus: disabled ? 'source-disabled' : declaration ? 'declared-runtime-probe-required' : 'not-declared',
      protocol, firmwareGate: gate ?? null, effectiveChargeMethods: [...effective.values()],
      ...(d.name === 'OneXPlayerApex' && windowsOem ? { windowsOemProfile: windowsOem } : {}),
      runtimeCapabilityMutations: chain.flatMap(owner => owner.operations.filter(o => !o.constructor && o.values.some(v => /^Battery/.test(v))).map(o => ({ owner: owner.name, file: owner.file, ...o }))),
      ymccRoutes: ymcc.map(r => ({ ...r, uiChoices: r.kind === 'firmware-fixed' ? [80, 100] : Array.from({ length: (100 - Math.max(50, r.min)) / 5 + 1 }, (_, i) => Math.max(50, r.min) + 5 * i) })),
       coverage: ymcc.length ? (disabled ? 'separate-ymcc-route-upstream-inheritance-disabled' : 'registered-exact-identity-with-runtime-gates') : disabled ? 'upstream-deliberately-disabled' : !declaration ? 'no-hc-charge-implementation' : d.name === 'SuiPlay0X1' ? 'inherited-protocol-windows-not-admitted' : 'gap-needs-review',
      knownDifference: d.name === 'ClawCG3EM' ? 'YMCC uses field-verified selector-0 Get_AP/Set_AP with independent readback; HC inherits legacy Get_Data/Set_Data' : d.name === 'OneXPlayerApex' ? apexDifference : d.name === 'OneXPlayer3' ? 'YMCC reviewed newer HC WMI banked 0x04A3 override is independent of frozen false gate; 0x04A4 modes 0x00/0x01/0x0B are not product-exposed' : oneX && !disabled ? 'YMCC registers direct EC16 0x04A3 threshold only; HC 0x04A4 modes 0x00/0x01/0x03 are not product-exposed' : d.name === 'SteamDeck' ? 'YMCC writes one threshold byte; HC setter builds an Int32 byte array' : null,
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
data.runtime = { identityRules: routes.length, frozenIdentityRules: battery.frozenModelRules, reviewedExtensionRules: battery.reviewedExtensionRules, protocolRoutes: [...new Set(routes.map(r => r.route))], externalSourceRoutes: routes.filter(r => !data.datasets.upstream.rows.some(d => d.deviceClass === r.identity.deviceClass)).map(r => ({...r, sourceStatus: 'reviewed non-HC extension; see sole battery policy and source overlay', softwareStatus: 'see source-qualified INTEGRATION-STATUS; not counted as an HC class'})) };
// Class inventories are not exhaustive views of independently reviewed YMCC routes.
data.runtime.registrationRows = battery.rows.map(({ruleId, deviceClass, provider, identity, firmwareGate, kind, min}) =>
  ({ruleId, deviceClass, provider, identity, firmwareGate, kind, min}));
data.runtime.identityAudit = {
  method: 'Static current DMI predicate pairs; witnesses are synthetic. Does not execute C# or authorize writes.',
  sourceSha256: Object.fromEntries(['capabilities/HcBatteryCapabilityCatalog.cs', 'native/BatteryHost/Policy.cs', ...Object.keys(generatedBattery.artifacts)]
    .map(file => [file, digest(fs.readFileSync(path.join(repo, file)))])),
  ...auditBatteryRouteIdentity(battery.rows),
};
const oemPath = opt('--onex-oem-background');
if (oemPath) {
  const { auditOneXConsoleBattery } = await import('./lib/battery_onex_oem_audit.mjs');
  data.independentOemReview = { status: 'source-checked', ...auditOneXConsoleBattery(path.resolve(oemPath), battery.rows) };
} else data.independentOemReview = { status: 'not-checked', reason: 'No pinned OEM member supplied; HC coverage does not establish OEM coverage.' };
const lines = [`# YMCC 全家族充电能力复核（${date}）`, '', '逐类解析继承链、充电声明、最终固件判断方法和 YMCC 精确身份规则。候选仍需运行时验证，不等于硬件支持或实测通过。本文件在唯一 capabilities 总库内，只记录审计，不提供另一套运行注册。完整方法、身份、行号、哈希、条件和档位见同名 JSON。', ''];
lines.push(data.independentOemReview.status === 'source-checked'
  ? '> 证据范围：锁定 HC、固定上游 HC、当前运行规则、固定 OneXConsole OEM 分支。其他 OEM 未因本报告而获得覆盖或排除结论。'
  : '> 仅作 HC 对账：未核独立 OEM 来源，不能据此宣称全机型覆盖。', '');
lines.push(`当前运行规则 ${routes.length} 组，协议 ${data.runtime.protocolRoutes.length} 条；联网 HC 提交 ${data.upstream.commit}，完整设备源码 ${devicePaths.length} 文件，解析清单逐文件匹配该提交树。`, '', '## 运行规则重叠', '');
const {pairsExamined, pairs} = data.runtime.identityAudit;
lines.push(`静态检查 ${pairsExamined} 对规则：同字段 ${pairs.filter(p => p.kind === 'same-field').length} 对、跨字段 ${pairs.filter(p => p.kind === 'cross-field').length} 对存在候选交集。下表为人工组合的 DMI 反例；匹配多条规则时不选路，不按注册顺序回退。不能据此认定现场出现过这些身份；单一匹配也不等于通过固件或 provider 门。`, '');
if (pairs.length) {
  lines.push('| 厂商 | boardProduct | systemProduct | 交集规则 | 静态选择 |', '| --- | --- | --- | --- | --- |');
  for (const p of pairs) lines.push(`| ${p.witness.manufacturer} | ${p.witness.boardProduct} | ${p.witness.systemProduct} | ${p.rules.join(', ')} | ${p.selection.status}: 无路由 |`);
}
lines.push('', '## OneX 优先结论', '', '| 型号 | 当前充电路径 / 档位 | 来源与剩余限制 |', '| --- | --- | --- |');
for (const r of data.datasets.upstream.rows.filter(r => r.family === 'OneXPlayer')) lines.push(`| ${r.deviceClass} | ${r.ymccRoutes.map(v => v.route + '，' + v.uiChoices.join('/') + '%').join('; ') || '未登记'} | ${r.knownDifference ?? (r.coverage === 'separate-ymcc-route-upstream-inheritance-disabled' ? 'HC 拒绝继承 X1；YMCC 已有独立路由，表中档位不代表实机读回' : r.effectiveStatus === 'source-disabled' ? 'HC 拒绝继承 X1；另核原厂' : r.protocol ?? 'HC 无充电实现')} |`);
for (const r of data.runtime.externalSourceRoutes) lines.push(`| ${r.identity.deviceClass} | ${r.route}，${Math.max(50,r.min)}–100% | 独立于 HC 的已审查来源；仅限本行的身份、固件门和 provider，实机尚未证明 |`);
if (windowsOem) lines.push('', '## APEX Windows 原厂配置补证', '', `已保全 OneXConsole ${windowsOem.version} 签名安装包及包内 JavaScript；原记录签名为 Valid，全部 ${Object.keys(windowsOem.references).length} 项引用的大小/SHA 已核。精确 APEX 配置将 0x04E5/0x04E6/0x04E7 送入 BatteryService.initEc，与 YMCC 当前默认 0x04A3 不同。`, '', '该证据只证明原厂配置及来源身份。实际 native EC/Battery/PawnIO 方法仅显示异常占位，未恢复访问宽度、地址编码或整事务合同；不是硬件通过，也不放开 EC0.12 门、不替换寄存器、不调用 OEM 接口。', '', `原生限制记录：${windowsOem.references.nativeRecoveryLimit.path}；待修正项：${windowsOem.references.correction.path}。`, '');
if (auxiliaryNative) lines.push('## 可读的原厂辅助原生合同', '', '同一签名安装包另附 DeviceComm.dll；其真实 IL 已恢复 Windows root\\WMI / SuRwECRegInterface 的 ReadECReg、WriteECReg 地址打包和八字节回执规范。它也明确了 RXU 1A86:FE00 的 MI_02 与 B3 版本查询帧；这些是辅助工具合同，不是固件操作或新能力准入。', '', '现有 Intel OXP3 WMI 读取器的逗号解析与原厂允许的连续十六进制回执有源码兼容差异，原案例的独立候选验证另行保全。APEX 充电后端是否走该辅助 WMI 合同仍未证明；主后端 EC/Battery/PawnIO 方法及该机固件准入未恢复，不替换地址、不借用 OXP3 WMI、不改变运行路线。', '', `辅助合同：${windowsOem.references.auxiliaryContractReview.path}。`, '');
lines.push('', 'YMCC 当前继承的 OneX X1 AMD/Mini/Pro/Apex 门：EC major>=1 且 minor>=3；Intel：major>=0 且 minor>=67，拒绝未知255。APEX 当前沿用此门不等于其新原厂协议已核验；条件来自HC原式，不擅自改成版本号字典序。', '', 'OneXPlayer 3 的独立 WMI 协议已适配，当前已不是 10 月 16 日的“HC 禁用”；其 0x04A4 旁路模式与 X1 不同。Super X 的 75–100% 双传输路线是已有源码工作，本轮核对并补齐总库状态，不重复算新增。', '');
if (fields) lines.push('## 本次现场', '', `压缩包命名 ${fields.userArchiveName}；实际 DMI：${fields.actualIdentity.manufacturer} / ${fields.actualIdentity.boardProduct} / ${fields.actualIdentity.systemProduct}。客户端 ${fields.requestCount} 次结果，操作 ${JSON.stringify(fields.operations)}；电池存在，但全部在固件条件前置检查失败，没有写入证据。`, '', `错误：${fields.reason}。${fields.firmwareValuesCaptured ? `最新会话 ${fields.latestSessionTime}，PID ${fields.latestPid}，构建 ${fields.clientBuild}；BIOS=${fields.bios}，EC=${fields.ecMajor}.${fields.ecMinor}。EC 主版本 ${fields.ecMajor} 不满足 >=1，次版本 ${fields.ecMinor} 满足 >=3；providerOpened=false，未进入 EC 读写。该版本不被现有 X1 规则接纳，不能因此推断 APEX 固件过旧或应该刷成 X1 版本。旧会话结果单独保存，不累加为新构建请求。` : '原日志没有 EC major/minor 或 BIOS 数值，不能猜成某一具体版本；当前 InProcessEntry 已记录这些身份字段，但旧安装包日志不能补造成新日志。'}`, '', windowsOem ? 'HC/APEX当前沿用X1 EC16 0x04A3；原厂Windows JavaScript的精确APEX配置0x04E5已证，Linux ACPI8 0xE5另记。实际Windows原生EC事务未恢复；当前现场仍是provider打开前的准入拒绝，未改门/寄存器，实机失败保持未修复。' : 'HC 的 Apex 沿用 X1 Windows EC16 0x04A3；Linux 官方 Apex 为 ACPI8 0xE5/0xE6。当前已证是准入拒绝，跨接口协议仍需核证；不取消门、不把 APEX 当 OP3、不混用地址空间。此实机失败保持未修复。', '');
const oem = data.independentOemReview;
lines.push('## 独立原厂证据对账', '');
if (oem.status === 'source-checked') {
  const unmatched = oem.offerings.filter(o => !o.matchingLiterals.length);
  lines.push(`固定 OneXConsole 0.10.4-fix3 共 ${oem.branches.length} 个启用分支，${oem.offerings.length} 个精确 Product 标签，其中 ${unmatched.length} 个没有同字面量运行规则。另有 ${oem.manufacturerOnly.length} 个只定位到厂商范围的分支。`, '',
    '字面量相符不是 DMI、原生协议或实体通过证明；表中候选不自动登记。', '',
    '| 厂商 | 原厂标签 | 条件 | 当前规则 |', '| --- | --- | --- | --- |');
  for (const item of oem.offerings) lines.push(`| ${item.manufacturer} | ${item.product} | ${item.firmwareConditions.join('; ').replaceAll('|', '\\|') || '未在本分支提取固件条件'} | ${item.matchingLiterals.map(m => m.ruleId + ' (' + m.field + ')').join(', ') || '无同字面量规则，需核身份与协议'} |`);
  for (const item of oem.manufacturerOnly) lines.push('', `厂商级线索：${item.manufacturer}，偏移 ${item.offset}；不得扩展为该厂全机型。`);
} else lines.push('未提供已固定原厂成员，本表只覆盖 HC 源码和当前运行表，不能排除原厂支持而 HC 未声明的机型。');
lines.push('', '## 全家族边界', '', '- ROG / MSI：ASUS 写入回执不是上限回读；MSI 旧 WMI 与 G3E AP 分开，历史有效回读不覆盖其他固件。', '- Lenovo：固定 80/100%；AYANEO：CEc 与 CEii 软件阈值 50–95%，100% 释放，不能称固件保存阈值。', '- Steam Deck：精确板号/固件和驱动门，单字节阈值；GPD：只登记 Mini 2025 HX370，规则声明精确 BIOS/EC，实际比较与拒绝先于打开 provider 需另按 HC224 核验。Win5 原厂 BIOS 可有阈值，不等于已有 YMCC Windows 事务。', '- SuiPlay：继承源码仍未开放 Windows；HC 的 X2/X2MiniPro 明确不继承 X1 支持，这不是对原厂能力的否定。', '- 本报告的已登记/候选均是源码层级，不证明普通包已带入、provider 已执行或实机停充。', '');
const miniPro = data.runtime.registrationRows.find(r => r.provider === 'oxp-x2-mini-pro-direct-ec');
if (miniPro) lines.push(`**X2 Mini Pro：** 登记的是主板 \`${miniPro.identity.values.join(' / ')}\` 单独路线，声明 BIOS \`${miniPro.firmwareGate.bios}\`、EC \`${miniPro.firmwareGate.ecMajor}.${miniPro.firmwareGate.ecMinor}\`，${Math.max(50, miniPro.min)}–100%，每 5% 一档。当前路线仅设置阈值，不写相邻旁路或强充地址。与 Intel OneXPlayer 3、APEX 路线不同；不把旧包查询拒绝、模拟回读或原厂配置当作新普通包实机证据。`, '');
for (const label of ['locked', 'upstream']) {
  const { summary, rows } = data.datasets[label];
  lines.push(`## ${label === 'locked' ? 'YMCC 锁定 HC' : '联网固定的上游 HC'}`, '', `工厂可识别 ${summary.factoryClasses} 类（含 ${summary.fallbackClasses} 兜底）；充电声明 ${summary.batteryDeclarations} 类，明确禁用 ${summary.explicitlyDisabled.length} 类，待运行时探测候选 ${summary.candidatesRequiringRuntimeProbe} 类。`, '', '| 类 | HC 充电实现 | YMCC 充电规则 | 差异 / 边界 |', '| --- | --- | --- | --- |');
  for (const r of rows) lines.push(`| ${r.deviceClass} | ${r.effectiveStatus === 'source-disabled' ? '最终判断返回 false' : r.protocol ?? '无充电声明/协议'} | ${r.ymccRoutes.map(v => v.route).join(', ') || '未登记'} | ${r.knownDifference ?? (r.deviceClass === 'GPDWin5' ? 'G1618-05 无协议；不能套 Mini 2025' : r.coverage === 'inherited-protocol-windows-not-admitted' ? 'SuiPlay Windows 未开放' : r.fallback ? '兜底类，不是机型' : '')} |`);
  lines.push('');
}
lines.push('## 复现', '', '复用 tools/PcCapabilityAudit 生成 locked/upstream-inventory.json；审计不加载 HC、不开设备。', '', '```powershell', `node tools/battery-hc-coverage-audit.mjs --evidence ${evidence.replaceAll('\\','/')} --out ${out.replaceAll('\\','/')} --date ${date} --stem ${stem}${fieldInput ? ' --field-evidence ' + path.resolve(fieldInput).replaceAll('\\','/') : ''}${oemPath ? ' --onex-oem-background "' + path.resolve(oemPath).replaceAll('\\','/') + '"' : ''}`, '```', '');
const reports = {json: JSON.stringify(data, null, 2) + '\n', md: lines.join('\n') + '\n'};
if (args.includes('--check')) {
  const stale = Object.entries(reports).filter(([ext, text]) => {
    const file = path.join(out, `${stem}.${ext}`);
    return !fs.existsSync(file) || fs.readFileSync(file, 'utf8').replaceAll('\r\n', '\n') !== text;
  }).map(([ext]) => `${stem}.${ext}`);
  if (stale.length) throw Error(`stale battery coverage report: ${stale.join(', ')}`);
} else {
  fs.mkdirSync(out, {recursive: true});
  for (const [ext, text] of Object.entries(reports)) fs.writeFileSync(path.join(out, `${stem}.${ext}`), text);
}
let output = { locked: data.datasets.locked.summary, upstream: data.datasets.upstream.summary, runtime: data.runtime, upstreamOnly: data.upstreamOnly };
if (factoryBoard) {
  const boardTreePath = 'HandheldCompanion/Misc/MotherboardInfo.cs';
  const lockedBoardPath = path.join(repo, 'deps/handheldcompanion-runtime/source/Misc/MotherboardInfo.cs');
  const lockedBoardSha = digest(fs.readFileSync(lockedBoardPath));
  const upstreamBoardPath = path.resolve(factoryBoard);
  const upstreamBytes = fs.readFileSync(upstreamBoardPath);
  const upstreamBlob = digest(Buffer.concat([Buffer.from(`blob ${upstreamBytes.length}\0`), upstreamBytes]), 'sha1');
  const upstreamBoardSha = digest(upstreamBytes);
  // Fixed DMI member mappings previously reviewed with the HC factories.
  if (lockedBoardSha !== '0b30eb3d65ef1ba14112c24d5311b54e04953e4f97931793d4456ea5b61a0c41' ||
      upstreamBoardSha !== '42b6fcf99721e978c04dd05530ef2a37c477a63a61b46e5fbdffe864aef0a1bc' ||
      entries.get(boardTreePath)?.sha !== upstreamBlob || entries.get(boardTreePath)?.type !== 'blob')
    throw Error('BATTERY_FACTORY_DMI_SOURCE_NEEDS_REVIEW: MotherboardInfo.cs');
  output = {
    schemaVersion: 1, runtimeRouteSha256: routeHash, upstreamCommit: commit.sha,
    scope: 'Static projection of factory literals for HC charging declarations and registered classes. Not observed DMI, firmware/provider admission, packaging, or hardware evidence.',
    dmiSource: {
      locked: { path: lockedBoardPath, sha256: lockedBoardSha },
      upstream: { path: upstreamBoardPath, sha256: upstreamBoardSha, gitBlob: upstreamBlob },
    },
    inventorySha256,
    datasets: Object.fromEntries(Object.entries(data.datasets).map(([label, {rows, summary}]) =>
      [label, { classCounts: { candidates: summary.candidatesRequiringRuntimeProbe, withAnyRoute: summary.candidatesWithYmccRoute },
        ...projectBatteryFactoryRoutes(rows, battery.rows) }])),
  };
}
console.log(JSON.stringify(output, null, 2));
