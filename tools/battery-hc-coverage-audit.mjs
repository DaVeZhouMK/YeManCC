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
const date = opt('--date') ?? new Date().toISOString().slice(0, 10);
const stem = opt('--stem') ?? 'hc-charge-matrix';
if (!/^\d{4}-\d{2}-\d{2}$/.test(date) || !/^[A-Za-z0-9_-]+$/.test(stem)) throw Error('invalid report date or filename');
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
const cataloguePath = path.join(repo, 'capabilities/hc-device-catalog.json');
const catalogue = read(cataloguePath), battery = catalogue.domains?.battery;
const routeHash = digest(fs.readFileSync(runtimePath));
if (battery.ruleDocumentSha256 !== routeHash || battery.rows.length !== routes.length) throw Error('sole catalogue battery rules are stale');
for (const [index, r] of routes.entries()) {
  const row = battery.rows[index];
  if (row.provider !== r.route || row.deviceClass !== r.identity.deviceClass || JSON.stringify(row.identity) !== JSON.stringify(r.identity) || JSON.stringify(row.firmwareGate) !== JSON.stringify(r.firmwareGate) || row.kind !== r.kind || row.min !== r.min) throw Error('sole catalogue row differs: ' + index);
}
const rawField = opt('--field-evidence') ? read(path.resolve(opt('--field-evidence'))) : null;
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

const data = { schema: 1, auditedOn: date, role: 'derived audit inside the sole YMCC capabilities library; not a registration or admission input', runtimeConsumer: false, registrationAuthority: 'capabilities/hc-battery-policy.json -> native/BatteryHost/battery-routes.json -> capabilities/hc-device-catalog.json -> HcBatteryCapabilityCatalog.generated.cs', evidenceLevel: 'source and recorded logs; no hardware probes or writes', runtimeRouteSha256: routeHash, catalogueSha256: digest(fs.readFileSync(cataloguePath)), verifiedReferenceFiles: manifest.length, previousSourceComparison: fs.existsSync(path.join(evidence, 'upstream/delta-since-f2e58468.json')) ? read(path.join(evidence, 'upstream/delta-since-f2e58468.json')) : null, upstream: { repository: 'Valkirie/HandheldCompanion', commit: manifest.find(x => x.repo === 'Valkirie/HandheldCompanion')?.commit, treeSha: hcTree.sha, files: devicePaths.length }, fieldEvidence: fields, datasets: {} };
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
      coverage: ymcc.length ? 'registered-exact-identity-with-runtime-gates' : disabled ? 'upstream-deliberately-disabled' : !declaration ? 'no-hc-charge-implementation' : d.name === 'SuiPlay0X1' ? 'inherited-protocol-windows-not-admitted' : 'gap-needs-review',
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
fs.mkdirSync(out, {recursive: true});
fs.writeFileSync(path.join(out, stem + '.json'), JSON.stringify(data, null, 2) + '\n');
const lines = [`# YMCC 全家族充电能力复核（${date}）`, '', '逐类解析继承链、充电声明、最终固件判断方法和 YMCC 精确身份规则。候选仍需运行时验证，不等于硬件支持或实测通过。本文件在唯一 capabilities 总库内，只记录审计，不提供另一套运行注册。完整方法、身份、行号、哈希、条件和档位见同名 JSON。', ''];
lines.push(`当前运行规则 ${routes.length} 组，协议 ${data.runtime.protocolRoutes.length} 条；联网 HC 提交 ${data.upstream.commit}，完整设备源码 ${devicePaths.length} 文件。`, '', '## OneX 优先结论', '', '| 型号 | 当前充电路径 / 档位 | 来源与剩余限制 |', '| --- | --- | --- |');
for (const r of data.datasets.upstream.rows.filter(r => r.family === 'OneXPlayer')) lines.push(`| ${r.deviceClass} | ${r.ymccRoutes.map(v => v.route + '，' + v.uiChoices.join('/') + '%').join('; ') || '未登记'} | ${r.knownDifference ?? (r.effectiveStatus === 'source-disabled' ? '上游明确 false，不继承启用' : r.protocol ?? 'HC 无充电实现')} |`);
for (const r of data.runtime.externalSourceRoutes) lines.push(`| ${r.identity.deviceClass} | ${r.route}，${Math.max(50,r.min)}–100% | 独立已审查来源；沿原双传输消费者，实机尚未证明 |`);
if (windowsOem) lines.push('', '## APEX Windows 原厂配置补证', '', `已保全 OneXConsole ${windowsOem.version} 签名安装包及包内 JavaScript；原记录签名为 Valid，全部 ${Object.keys(windowsOem.references).length} 项引用的大小/SHA 已核。精确 APEX 配置将 0x04E5/0x04E6/0x04E7 送入 BatteryService.initEc，与 YMCC 当前默认 0x04A3 不同。`, '', '该证据只证明原厂配置及来源身份。实际 native EC/Battery/PawnIO 方法仅显示异常占位，未恢复访问宽度、地址编码或整事务合同；不是硬件通过，也不放开 EC0.12 门、不替换寄存器、不调用 OEM 接口。', '', `原生限制记录：${windowsOem.references.nativeRecoveryLimit.path}；待修正项：${windowsOem.references.correction.path}。`, '');
if (auxiliaryNative) lines.push('## 可读的原厂辅助原生合同', '', '同一签名安装包另附 DeviceComm.dll；其真实 IL 已恢复 Windows root\\WMI / SuRwECRegInterface 的 ReadECReg、WriteECReg 地址打包和八字节回执规范。它也明确了 RXU 1A86:FE00 的 MI_02 与 B3 版本查询帧；这些是辅助工具合同，不是固件操作或新能力准入。', '', '现有 Intel OXP3 WMI 读取器的逗号解析与原厂允许的连续十六进制回执有源码兼容差异，原案例的独立候选验证另行保全。APEX 充电后端是否走该辅助 WMI 合同仍未证明；主后端 EC/Battery/PawnIO 方法及该机固件准入未恢复，不替换地址、不借用 OXP3 WMI、不改变运行路线。', '', `辅助合同：${windowsOem.references.auxiliaryContractReview.path}。`, '');
lines.push('', 'YMCC 当前继承的 OneX X1 AMD/Mini/Pro/Apex 门：EC major>=1 且 minor>=3；Intel：major>=0 且 minor>=67，拒绝未知255。APEX 当前沿用此门不等于其新原厂协议已核验；条件来自HC原式，不擅自改成版本号字典序。', '', 'OneXPlayer 3 的独立 WMI 协议已适配，当前已不是 10 月 16 日的“HC 禁用”；其 0x04A4 旁路模式与 X1 不同。Super X 的 75–100% 双传输路线是已有源码工作，本轮核对并补齐总库状态，不重复算新增。', '');
if (fields) lines.push('## 本次现场', '', `压缩包命名 ${fields.userArchiveName}；实际 DMI：${fields.actualIdentity.manufacturer} / ${fields.actualIdentity.boardProduct} / ${fields.actualIdentity.systemProduct}。客户端 ${fields.requestCount} 次结果，操作 ${JSON.stringify(fields.operations)}；电池存在，但全部在固件条件前置检查失败，没有写入证据。`, '', `错误：${fields.reason}。${fields.firmwareValuesCaptured ? `最新会话 ${fields.latestSessionTime}，PID ${fields.latestPid}，构建 ${fields.clientBuild}；BIOS=${fields.bios}，EC=${fields.ecMajor}.${fields.ecMinor}。EC 主版本 ${fields.ecMajor} 不满足 >=1，次版本 ${fields.ecMinor} 满足 >=3；providerOpened=false，未进入 EC 读写。该版本不被现有 X1 规则接纳，不能因此推断 APEX 固件过旧或应该刷成 X1 版本。旧会话结果单独保存，不累加为新构建请求。` : '原日志没有 EC major/minor 或 BIOS 数值，不能猜成某一具体版本；当前 InProcessEntry 已记录这些身份字段，但旧安装包日志不能补造成新日志。'}`, '', windowsOem ? 'HC/APEX当前沿用X1 EC16 0x04A3；原厂Windows JavaScript的精确APEX配置0x04E5已证，Linux ACPI8 0xE5另记。实际Windows原生EC事务未恢复；当前现场仍是provider打开前的准入拒绝，未改门/寄存器，实机失败保持未修复。' : 'HC 的 Apex 沿用 X1 Windows EC16 0x04A3；Linux 官方 Apex 为 ACPI8 0xE5/0xE6。当前已证是准入拒绝，跨接口协议仍需核证；不取消门、不把 APEX 当 OP3、不混用地址空间。此实机失败保持未修复。', '');
lines.push('## 全家族边界', '', '- ROG / MSI：用户反馈基本可用，保留现有 ASUS 回执语义与 G3E 独立 AP 回读；本轮不扩大成全部型号实机通过。', '- Lenovo：固定 80/100%；AYANEO：CEc 与 CEii 软件阈值 50–95%，100% 释放，不能称固件保存阈值。', '- Steam Deck：精确板号/固件和驱动门，单字节阈值；GPD：仅 Mini 2025 HX370 精确 BIOS/EC，Win5 仍无 HC 充电协议。', '- SuiPlay：继承源码仍未开放 Windows；X2/X2MiniPro 明确禁用；其他无声明机型不能因家族相近补寄存器。', '- 本报告的已登记/候选均是源码层级；下方每个类列出来源和运行边界。', '');
for (const label of ['locked', 'upstream']) {
  const { summary, rows } = data.datasets[label];
  lines.push(`## ${label === 'locked' ? 'YMCC 锁定 HC' : '联网固定的上游 HC'}`, '', `工厂可识别 ${summary.factoryClasses} 类（含 ${summary.fallbackClasses} 兜底）；充电声明 ${summary.batteryDeclarations} 类，明确禁用 ${summary.explicitlyDisabled.length} 类，待运行时探测候选 ${summary.candidatesRequiringRuntimeProbe} 类。`, '', '| 类 | HC 充电实现 | YMCC 充电规则 | 差异 / 边界 |', '| --- | --- | --- | --- |');
  for (const r of rows) lines.push(`| ${r.deviceClass} | ${r.effectiveStatus === 'source-disabled' ? '最终判断返回 false' : r.protocol ?? '无充电声明/协议'} | ${r.ymccRoutes.map(v => v.route).join(', ') || '未登记'} | ${r.knownDifference ?? (r.deviceClass === 'GPDWin5' ? 'G1618-05 无协议；不能套 Mini 2025' : r.coverage === 'inherited-protocol-windows-not-admitted' ? 'SuiPlay Windows 未开放' : r.fallback ? '兜底类，不是机型' : '')} |`);
  lines.push('');
}
lines.push('## 复现', '', '复用 tools/PcCapabilityAudit 生成 locked/upstream-inventory.json；审计不加载 HC、不开设备。', '', '```powershell', `node tools/battery-hc-coverage-audit.mjs --evidence ${evidence.replaceAll('\\','/')} --out ${out.replaceAll('\\','/')} --date ${date} --stem ${stem}${opt('--field-evidence') ? ' --field-evidence ' + path.resolve(opt('--field-evidence')).replaceAll('\\','/') : ''}`, '```', '');
fs.writeFileSync(path.join(out, stem + '.md'), lines.join('\n') + '\n');
console.log(JSON.stringify({ locked: data.datasets.locked.summary, upstream: data.datasets.upstream.summary, runtime: data.runtime, upstreamOnly: data.upstreamOnly }, null, 2));
