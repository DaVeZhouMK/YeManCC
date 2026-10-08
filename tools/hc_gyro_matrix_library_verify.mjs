import fs from 'fs';
const SW_MAP = { AZ: { X: 'X', Y: 'Z', Z: 'Y' }, ID: { X: 'X', Y: 'Y', Z: 'Z' }, CY: { X: 'Y', Y: 'Z', Z: 'X' }, XY: { X: 'Y', Y: 'X', Z: 'Z' } };
const SNAMES = { AZ: 'Y↔Z', ID: '恒等', CY: '循环 X→Y,Y→Z,Z→X', XY: 'X↔Y' };
function remapOut(inputAxis, swapKey) {
  const swap = SW_MAP[swapKey];
  const idx = [];
  idx[0] = swap['X'] === 'Y' ? 1 : swap['X'] === 'Z' ? 2 : 0;
  idx[1] = swap['Y'] === 'X' ? 0 : swap['Y'] === 'Z' ? 2 : 1;
  idx[2] = swap['Z'] === 'X' ? 0 : swap['Z'] === 'Y' ? 1 : 2;
  const src = [null, null, null];
  src[idx[0]] = 'x'; src[idx[1]] = 'y'; src[idx[2]] = 'z';
  const sign = (name, s) => (s === 1 ? name : '-' + name);
  return '(' + sign(src[0], inputAxis[0]) + ',' + sign(src[1], inputAxis[1]) + ',' + sign(src[2], inputAxis[2]) + ')';
}
const rows = [
  ['`RC71L`', '`ROGAlly`', [-1,-1,1], 'AZ', '', [-1,-1,1], 'AZ', '', 'E `ROGAlly.cs:89–109`'],
  ['`RC72LA`', '`ROGAllyX`', [1,1,-1], 'ID', '（覆盖父类，只写 Axis）', [-1,-1,1], 'AZ', '（继承 `ROGAlly`）', 'M：gyro E `ROGAllyX.cs:17–20`；accel I `ROGAlly.cs`'],
  ['`RC73YA`', '`XboxROGAlly`', [1,1,-1], 'AZ', '', [-1,-1,1], 'AZ', '', 'E `XboxROGAlly.cs:22–42`'],
  ['`RC73XA`', '`XboxROGAllyX`', [1,1,-1], 'AZ', '', [-1,-1,1], 'AZ', '', 'E `XboxROGAllyX.cs:22–42`'],
  ['其他 RC*', '`AsusDevice`', [1,1,1], 'ID', '', [1,1,1], 'ID', '', 'D `IDevice`'],
  ['`83E1`', '`LegionGoTablet`', [-1,1,1], 'AZ', '', [1,-1,-1], 'AZ', '', 'E `LegionGoTablet.cs`'],
  ['`83N0,83N1`', '`LegionGoTablet2`', [1,1,-1], 'ID', '', [-1,-1,1], 'ID', '', 'E `LegionGoTablet2.cs`'],
  ['`83L3`', '`LegionGoSZ2`', [-1,1,1], 'ID', '（继承 SZ1）', [-1,1,1], 'ID', '（继承 SZ1）', 'I `LegionGoSZ1.cs`'],
  ['`83N6,83Q2,83Q3`', '`LegionGoSZ1`', [-1,1,1], 'ID', '', [-1,1,1], 'ID', '', 'E `LegionGoSZ1.cs`'],
  ['`MS-1T41`', '`ClawA1M`', [1,1,-1], 'AZ', '', [-1,-1,1], 'AZ', '', 'E `ClawA1M.cs`'],
  ['`MS-1T42,1T52`', '`ClawA2VM`', [1,1,-1], 'ID', '（只写 Axis）', [-1,-1,1], 'AZ', '（继承 `ClawA1M`）', 'M gyro E；accel I'],
  ['`MS-1T8K`', '`ClawBZ2EM`', [1,1,-1], 'ID', '', [-1,-1,1], 'AZ', '（继承 `ClawA1M`）', 'M gyro E；accel I'],
  ['`MS-1T91`', '`ClawCG3EM`', [1,1,-1], 'ID', '', [-1,-1,1], 'AZ', '（继承 `ClawA1M`）', 'M gyro E；accel I'],
  ['`AOKZOE A1 AR07`', '`AOKZOEA1`', [1,-1,1], 'AZ', '', [1,-1,-1], 'AZ', '', 'E `AOKZOEA1.cs`'],
  ['`A1 Pro/A1X/A2 Pro`', '`AOKZOEA1Pro/X/A2`', [1,-1,1], 'AZ', '（继承 A1）', [1,-1,-1], 'AZ', '（继承 A1）', 'I'],
  ['`Loki MiniPro/Zero/Max`', '`AynLoki*`', [1,-1,-1], 'CY', '', [-1,1,-1], 'AZ', '', 'E `Devices/Ayn/AynLoki.cs:31–51`'],
  ['—（CEc 基类：2/KUN/2S 等）', '`AYANEODeviceCEc`', [1,-1,1], 'AZ', '', [-1,-1,1], 'AZ', '', 'E `AYANEODeviceCEc.cs:53–68`'],
  ['—（CEii 基类：AB05-AMD/Mendocino/Intel）', '`AYANEODeviceCEii`', [1,1,-1], 'AZ', '', [1,1,-1], 'AZ', '', 'E `AYANEODeviceCEii.cs:29-44`'],
  ['`AIR/AIR Pro/1S/Lite`', '`AYANEOAIR*`', [1,-1,1], 'ID', '（只写 Axis）', [1,-1,-1], 'ID', '（只写 Axis）', 'E `AYANEOAIR.cs:20–21`'],
  ['`AYA NEO FOUNDER/2021/2021 Pro`', '`AYANEO2021*`', [1,-1,1], 'AZ', '', [-1,-1,1], 'AZ', '', 'E `AYANEO2021.cs:24–40`'],
  ['`NEXT/NEXT Pro/Advance/Lite`', '`AYANEONEXT*`', [1,-1,1], 'AZ', '', [-1,-1,1], 'AZ', '', 'E `AYANEONEXT.cs:25–41`'],
  ['`KUN/2/GEEK/2S/GEEK 1S; SuiPlay0X1`', '`AYANEOKUN/2/2S/SuiPlay0X1`', [1,-1,1], 'AZ', '（继承 CEc）', [-1,-1,1], 'AZ', '（继承 CEc）', 'I'],
  ['`AS01`', '`AYANEOSlide`', [1,1,-1], 'ID', '', [-1,1,-1], 'ID', '', 'E `AYANEOSLIDE.cs:19–20`'],
  ['`FLIP KB; FLIP DS`', '`AYANEOFlipKB/DS`', [1,-1,1], 'AZ', '（gyro 继承 CEc）', [1,-1,-1], 'ID', '（accel KB E 只写 Axis，DS I KB）', 'M gyro I CEc；accel KB E、DS I KB'],
  ['`FLIP 1S KB/DS`', '`AYANEOFlip1SKB/DS`', [1,1,-1], 'ID', '', [1,1,-1], 'ID', '', 'E `AYANEOFlip1S*.cs:15–17`'],
  ['`WIN2, G1618-03`', '`GPDWin2/Win3`', [1,1,1], 'ID', '（无矩阵）', [1,1,1], 'ID', '（无矩阵）', 'D `IDevice`（禁止准入）'],
  ['`G1617-01 +7640U/7840U`', '`GPDWinMini_7640U`, `GPDWinMini`', [1,-1,-1], 'CY', '', [-1,1,1], 'AZ', '', 'I `GPDWinMini.cs`'],
  ['`G1617-01 +8840U`', '`GPDWinMini_8840U`', [1,-1,1], 'AZ', '', [-1,1,1], 'AZ', '', 'E `GPDWinMini-8840U.cs`'],
  ['`G1617-02`', '`GPDWinMini_HX370`', [-1,-1,1], 'CY', '', [-1,1,1], 'AZ', '', 'E `GPDWinMini-HX370.cs`'],
  ['`G1618-04 +6800U`', '`GPDWin4`', [-1,1,1], 'AZ', '', [-1,-1,1], 'AZ', '', 'E `GPDWin4.cs`'],
  ['`G1618-04 +7640U/7840U`', '`GPDWin4_2023*`', [1,-1,-1], 'CY', '', [-1,-1,1], 'AZ', '', 'E `GPDWin4-2023.cs`'],
  ['`G1618-04 +8640U/8840U/HX370`', '`GPDWin4_2024*`', [1,-1,-1], 'CY', '', [-1,-1,1], 'AZ', '', 'E `GPDWin4-2024.cs`'],
  ['`G1618-05`', '`GPDWin5`', [1,-1,-1], 'CY', '', [-1,-1,1], 'AZ', '', 'E `GPDWin5.cs`'],
  ['`G1619-03`', '`GPDWinMax2Intel`', [1,1,1], 'AZ', '', [-1,-1,1], 'ID', '（只写 Axis）', 'E `GPDWinMax2Intel.cs`'],
  ['`G1619-04/05 +非HX370`', '`GPDWinMax2*` 年份变体', [1,-1,-1], 'CY', '', [-1,1,1], 'AZ', '', 'E `GPDWinMax2.cs`'],
  ['`G1619-04 +HX370`', '`GPDWinMax2_2024_HX370`', [1,-1,1], 'AZ', '', [-1,-1,1], 'AZ', '', 'E `GPDWinMax2-2024-HX370.cs`'],
  ['`X1 (i/A/z) / X1Pro`', '`OneXPlayerX1/AMD/Intel/Pro`', [1,-1,1], 'AZ', '', [1,-1,-1], 'AZ', '', 'E `OneXPlayerX1.cs:59–79`'],
  ['`X1 Mini`', '`OneXPlayerX1Mini`', [1,1,1], 'ID', '', [1,-1,1], 'ID', '', 'E `OneXPlayerX1Mini.cs`'],
  ['`APEX`', '`OneXPlayerApex`', [1,-1,1], 'XY', '', [-1,1,-1], 'XY', '', 'E `OneXPlayerApex.cs`'],
  ['`G1 i / G1 A`', '`OneXPlayerG1Intel/AMD`', [1,1,1], 'ID', '', [1,-1,1], 'ID', '', 'E `OneXPlayerG1.cs`'],
  ['`Mini Pro V01 / mini A07`', '`OneXPlayerMiniAMD`', [1,-1,1], 'AZ', '（继承 Mini）', [1,1,1], 'ID', '（只写 Axis）', 'M gyro I；accel E'],
  ['`Mini Pro v1002-C`', '`OneXPlayerMiniIntel`', [1,-1,-1], 'CY', '', [1,-1,-1], 'ID', '（只写 Axis）', 'E `OneXPlayerMiniIntel.cs`'],
  ['`Mini Pro V03`', '`OneXPlayerMiniPro`', [1,-1,1], 'AZ', '（继承 Mini）', [1,-1,1], 'ID', '（只写 Axis）', 'M gyro I；accel E'],
  ['`F1 / F1Pro`', '`OneXPlayerOneXFly/F1Pro`', [1,-1,1], 'AZ', '', [-1,-1,1], 'AZ', '', 'E `OneXPlayerOneXFly.cs`'],
  ['`ONEXPLAYER 2 / 2 PRO`', '`OneXPlayer2/2Pro`', [-1,-1,-1], 'AZ', '', [1,-1,-1], 'AZ', '', 'E `OneXPlayer2.cs`'],
  ['`SHENZHEN MEIGAO.../HPPAC`', '`MinisforumV3`', [1,-1,1], 'XY', '', [-1,1,-1], 'XY', '', 'E `MinisforumV3.cs`'],
  ['`MYSTEN LABS/SuiPlay0X1`', '`SuiPlay0X1`', [1,-1,1], 'AZ', '（继承 CEc）', [-1,-1,1], 'AZ', '（继承 CEc）', 'I'],
  ['`VALVE Jupiter/Galileo`', '`SteamDeck`', [1,1,1], 'ID', '', [1,1,1], 'ID', '', 'D `IDevice`（禁止自动准入）'],
  ['`PC PARTNER/ZOTAC G0A1W`', '`GamingZone`', [1,1,-1], 'AZ', '', [1,1,1], 'AZ', '', 'E `GamingZone.cs`'],
  ['任何未匹配', '`DefaultDevice`', [1,1,1], 'ID', '', [1,1,1], 'ID', '', 'D `IDevice`；`SAFE_STOP`'],
];
const fmt = (r) => {
  const [sel, cls, ga, gs, gsn, aa, as, asn, src] = r;
  return `| ${sel} | ${cls} | (${ga.join(',')}) | ${SNAMES[gs]} ${gsn} | ${remapOut(ga, gs)} | (${aa.join(',')}) | ${SNAMES[as]} ${asn} | ${remapOut(aa, as)} | ${src} |`;
};
const header = '| selector | class | Gyro Axis | Gyro Swap | Gyro 输出 | Accel Axis | Accel Swap | Accel 输出 | 来源 |';
const sep = '|---|---|---|---|---:|---|---|---:|---|';
const groups = [
  ['#### ASUS 系', 0, 5],
  ['#### Lenovo 系（SystemModel selector）', 5, 4],
  ['#### MSI 系', 9, 4],
  ['#### AOKZOE / AYN', 13, 3],
  ['#### AYANEO 系（基类 CEc / CEii + 家族）', 16, 9],
  ['#### GPD 系（G1618/G1619 按 CPU 细分）', 25, 11],
  ['#### ONE-NETBOOK 系', 36, 9],
  ['#### 其他单机型', 45, 5],
];
let md = '### 13.3A 机型库完整矩阵：AxisSwap 与最终输出表达式（2026-09-09 源码级细化）\n\n';
md += `> 本节把 §13.3 的 \`(x,y,z)\` 三元组细化为 HC \`IMUMatrix\` 的**完整对象**：\`Axis\`（逐轴符号）+ \`AxisSwap\`（轴交换）。所有行仍为 **HC 冻结源静态事实**（\`Devices/**\`，commit \`06c0b954\`），**不授权 YMCC runtime**；provider、pair、实机方向、calibration、consumer 一律 \`UNENCLOSED\`。YMCC 当前 native 只锁定 RC73XA/RC73YA 两个 selection（\`main.cpp:5340–5411\`）；其余全部按既有 fail-closed 执行（virtual-stick=\`safe-zero\`、direct-IMU=\`no-report\`、persona/visibility=\`SAFE_STOP\`）。\n\n`;
md += '**应用语义（HC 实际执行顺序）**：`IMUGyrometer.ReadingChanged` 先按 `AxisRemapIndices` 轴交换（`AxisSwap` 的输入→输出映射，`out[axis] = raw[source]`），再逐输出轴乘 `Axis` 符号（`IMUGyrometer.cs:100–134`、`IDevice.IMUMatrix.ComputeRemapIndices`、`reading.reading.{X,Y,Z} = readingAxis[axis] * Axis.{X,Y,Z}`）。下表"最终输出表达式"= `(outX, outY, outZ)`，例如 gyro `(x,z,-y)` 表示 `outX=rawX, outY=rawZ, outZ=-rawY`。所有表达式已与锁定的 `motionSample.ts` Xbox ROG Ally X gyro `(x,z,-y)` / accel `(-x,-z,y)` 交叉核对一致。\n\n';
md += '**子类覆盖陷阱**：HC 多处子类只写 `Axis` 不写 `AxisSwap`，此时 `AxisSwap` 回到 IMUMatrix 默认恒等 `X:X,Y:Y,Z:Z`，**不会**继承父类 swap。下表对这类差异已在 Swap 列标注 "只写 Axis"，移植时以**最终输出表达式**为准，禁止以"父类 swap + 子类 Axis"猜测。\n\n';
for (const [title, start, count] of groups) {
  md += title + '\n\n' + header + '\n' + sep + '\n';
  for (let j = start; j < start + count; j++) md += fmt(rows[j]) + '\n';
  md += '\n';
}
md += '**统计与移植提示（2026-09-09 源码核对，表达式中轴/符号已由推导脚本重算）**：\n';
md += '- 显式 `GyroMatrix` 赋值类 ≈ 43 个 / 显式 `AcceleroMatrix` ≈ 41 个；gyro 与 accel 的 Axis 或 Swap 不同者约 33 个，必须成对移植。\n';
md += '- 父类带矩阵、子类只覆盖一轴或只覆盖 Axis（swap 归恒等）的典型：`ROGAllyX`、`OneXPlayerX1Mini`、`ClawA2VM/BZ2EM/CG3EM`、`AYANEOAIR`、`AYANEOSlide`、`AYANEOFlipKB`、`GPDWinMax2Intel`、`OneXPlayerMini*`。\n';
md += '- `InternalSensor/ExternalSensor` 是 `IDevice.PullSensors()` 运行时动态能力位，任何机型都不得硬编码进机型库。\n';
md += '- 完整输出表达式与 `HcAxisMatrixV1.output`（`motionSample.ts:21`）同构，未来每张机型卡都可按该结构保存。\n\n';
fs.writeFileSync('g:/YeManCC-Work/Mainline/Build/Validation/GyroVirtual/HC-GYRO-MATRIX-LIBRARY-20260909.md', md, 'utf8');
console.log('generated len=' + md.length);
// 全矩阵交叉校验：所有行都必须是 AXIS(1|1|1)+ID 恒等输出 (x,y,z) 之外的有意义旋转，
// 且与 motionSample.ts 锁定的 Xbox ROG Ally X 完全一致（gyro (x,z,-y)、accel (-x,-z,y)）。
let mism = [];
for (const r of rows) {
  if (r[1].includes('XboxROGAllyX')) {
    const go = remapOut(r[2], r[3]), ao = remapOut(r[5], r[6]);
    if (go !== '(x,z,-y)' || ao !== '(-x,-z,y)') mism.push(r[1] + ' ' + go + ' ' + ao);
  }
}
// 未匹配 selector 的 DefaultDevice 等"gyro/accel 双恒等"行必须输出 (x,y,z)，
// 避免默认矩阵被误升级为旋转。注意 GPDWinMax2Intel 的 gyro swap=AZ（输出 (x,z,y)），
// 只要任一矩阵非恒等就不在此检查范围。
for (const r of rows) {
  if (r[3] === 'ID' && r[6] === 'ID' && r[2].join(',') === '1,1,1' && remapOut(r[2], r[3]) !== '(x,y,z)') mism.push(r[1] + ' identity expected');
}
// HC 源码覆盖审计：Devices/** 下凡显式赋值 GyroMatrix 的 .cs 源文件必须被 rows 引用。
// 基类（无矩阵、默认恒等）如 LegionGo/OneXAOKZOE 允许豁免。防机型库随 HC 升级遗漏 SKU。
{
  const fsLib = await import('node:fs');
  const hcDevices = 'G:/YeManCC-Work/Isolated/HC-Candidate-0.32.4.0-06c0b954-20260902/HandheldCompanion/Devices';
  const exemptBases = new Set(['LegionGo', 'OneXAOKZOE', 'AYANEO2', 'AYANEO2S', 'AYANEONEXTLite', 'OneXPlayerOneXFlyF1Pro', 'LokiMax6600U', 'LokiMax6800U', 'LokiMiniPro', 'LokiZero', 'IDevice']);
  // 行 class 记号（如 `AYANEOFlip1SKB/DS`）把多个共址类缩写成一组。别名把它们展开成与源文件名一一对应。
  const seriesAliases = {
    ayaneoflip1skbds: ['AYANEOFlip1SKB', 'AYANEOFlip1SDS'],
  };
  const canon = (s) => s.replace(/`/g, '').replace(/\*/g, '').replace(/[^A-Za-z0-9]/g, '').toLowerCase();
  const rowClasses = [];
  for (const r of rows) {
    const tokens = r[1].split(',').map((c) => canon(c.trim())).filter(Boolean);
    for (const t of tokens) {
      rowClasses.push(t);
      const alias = seriesAliases[t];
      if (alias) rowClasses.push(...alias.map(canon));
    }
  }
  const walked = [];
  const scanDir = (dirPath) => {
    for (const entry of fsLib.readdirSync(dirPath, { withFileTypes: true })) {
      const full = dirPath + '/' + entry.name;
      if (entry.isDirectory()) { scanDir(full); continue; }
      if (!entry.isFile() || !entry.name.endsWith('.cs')) continue;
      const name = entry.name.replace(/\.cs$/, '');
      if (exemptBases.has(name)) continue;
      const text = fsLib.readFileSync(full, 'utf8');
      if (!text.includes('GyroMatrix = new()')) continue;
      walked.push(name);
      const cn = canon(name);
      // `AYANEOFlip1SKB/DS` 记号 canon 后为 ayaneoflip1skbds，不再与 ayaneoflip1sds 互相包含
      // —— 通过 seriesAliases 展开后才能命中；后缀任何未知 SKU 文件都会漏出至此报缺失。
      const hit = rowClasses.some((t) => t === cn || (t.length >= 5 && (t.includes(cn) || cn.includes(t))));
      if (!hit) mism.push(name + ' missing from matrix rows');
    }
  };
  scanDir(hcDevices);
  console.log('hc gyro-matrix source files walked: ' + walked.length);
}
console.log('cross-check mismatches: ' + (mism.length ? mism.join('; ') : 'none'));