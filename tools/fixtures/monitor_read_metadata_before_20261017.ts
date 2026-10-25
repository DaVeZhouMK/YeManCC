import { advanceMonitorTdpDisplay } from './monitorAppliedTdp';
import { freezeCoreUsageValues, freezeCoreUsageLabels, markCoreUsageLabels } from './monitorCoreTiles';
import { createMonitorPayloadDecoder } from './monitorPayloadDecoder';
import { createMonitorSnapshotBudget } from './monitorSnapshotBudget';
import { fpsHardwareProofFresh, hasFpsHardwareProof } from './monitorFpsHardware';
// monitorData.ts — 196/197/198：前三端统一监控数据入口
//
// 数据所有权：native 单采集者（约 1Hz）→ 有界内存快照（monitor.snapshot）→ 本入口。
// 消费端：TopMonitorBar（展示）、PerformanceScheduleView（展示，经共享 ref）、autofloat
// （独立控制节拍）。本入口只负责：能力协商、在途请求合并、生命周期（会话/代际/序号）校验、
// 需求登记（把三端需求合并为 monitor.start/stop 的 {top,fps} 标志）。不新增轮询；不缓存长期
// TTL；500ms 图表刷新只读本入口已取到的数据，不触发新的硬件采集。
//
// 198 状态契约（按 198 §3A）：
//   · 每个标志三态：期望(desired) / 已确认(confirmed) / 已发出未确认(pending=结果未知)。
//     超时或拒绝都只进 pending，不得当作成功；**释放时若存在 pending 的 start，也必须发幂等
//     stop**（start 可能已真实执行），确保最终能真正停止；反之 stop 未确认时同样收敛。
//   · 回执校验：仅 `ok===true` 才算确认（ok:false 属拒绝，进 pending 并按预算重试）。
//   · 读回校准：快照携带 topActive/fpsActive（unavailable 也带）⇒ 观察到活跃即视为已确认、
//     观察到不活跃即清 confirmed/pending（不依赖回执即可收敛）。
//   · 会话身份：响应携带 native 会话号（与采集代际分开）。**只有真实会话变化**才重置
//     confirmed/pending/takeover 与代际/序号基准并重新协调仍有的需求；同会话低代际/乱序一律
//     拒收——不再把"代际变小"当作重启证明。
//   · 意图串行：pump 单跑者 + 每次 await 后重算需求 ⇒ 旧意图不会覆盖新意图；
//     日常 read 只按退避推进（不绕退避耗预算），acquire/release 作为新意图可立即推进（含必要停止）。
//   · takeover：需 ok && cleaned 才算完成、绑定会话与 relay epoch，旧会话/旧 epoch 的迟到回执
//     不得把新会话标成 done；失败按读取事件有界重试，且不偷读残留文件。
//   · 停止失效按**字段自身时间戳**比较（不用四文件最大时间掩盖旧 payload）。
//
// 兼容矩阵：新前端 + 旧 native（无 monitor.snapshot ⇒ unknown）→ 本会话回退四文件通路；
// 新 native + 旧前端 → native 默认继续写四文件（takeover 以 native 会话绑定，重载/恢复复位）；
// 无 session 字段的过渡版快照 native ⇒ 仅按代际/序号单调校验（保守：回退即拒收，标注为兼容防护）。
import { invoke } from './ipc';
// Software configuration is exposed by the same YMCC monitor facade, with no sensor reader.
export { subscribeMonitorTdpState } from './monitorAppliedTdp';
export type { TdpRuntimeSnapshot } from './tdpRuntimeState';
import { createLatestDisplayFeed, monitorSnapshotDisplayKey } from './monitorDisplayFeed';
import { monitorSnapshotHasFreshData } from './monitorHudFreshness';
import { fs } from './api';
import type { TopMonData } from './topmon';
import { presentedFrameTime, hasExplicitFpsMetadata, MONITOR_CURVE_DISPLAY_MS } from './monitorFrameTime';

export interface MonitorFpsPayload {
  ts: number;
  fps: number;
  fps1: number;
  fps1Source?: 'hwinfo-presented-1percent-low';
  fps1ObservedAt?: number;
  fps1ValidUntil?: number;
  fps1PollPeriodMs?: number;
  gpu: number | null;
  packagePower: number;
  hardwareObservedAt?: number;
  hardwareValidUntil?: number;
  hardwarePollPeriodMs?: number;
  frametime?: number;
  frameTimeSource?: 'hwinfo-presented-avg' | 'rtss-current-frame';
  fpsSource?: 'hwinfo-presented' | 'rtss';
  fpsObservedAt?: number;
  fpsValidUntil?: number;
  fpsPollPeriodMs?: number;
  processCreated?: string;
  frameTimeTs?: number;
  frameTimeValidUntil?: number;
  frameTimePollPeriodMs?: number;
  frameTimes?: Array<number | null>;
  game: string | null;
  pid: number;
}

export interface MonitorSnapshotView {
  schema: number;
  /** native 会话号（null=对端未提供，走兼容单调校验） */
  session: number | null;
  /** relay 所有权代数（takeover/release 绑定用；null=对端未提供） */
  relayEpoch: number | null;
  generation: number;
  seq: number;
  collectedAt: number;
  /** 监控心跳（fps 闩锁期间刷新）；null = 无心跳（语义同 fps-monitor.hb 缺失） */
  heartbeatAt: number | null;
  /** HWiNFO 健康位时间；null = 不健康/未启动（语义同 hwinfo-ok 缺失） */
  hwinfoOkAt: number | null;
  top: TopMonData | null;
  fps: MonitorFpsPayload | null;
  topActive: boolean;
  fpsActive: boolean;
  hwDown: boolean;
  source: 'snapshot' | 'files';
}

export interface MonitorHubDeps {
  invoke: (cmd: string, args?: object, options?: { timeoutMs?: number }) => Promise<unknown>;
  readTextFile: (path: string, maxBytes?: number) => Promise<string>;
  now: () => number;
  /** 测试注入：默认全局 setTimeout/clearTimeout（仅用于有界收尾重试，不建高频轮询） */
  setTimer?: (fn: () => void, ms: number) => unknown;
  clearTimer?: (handle: unknown) => void;
}

export type MonitorTransportMode = 'unknown' | 'snapshot' | 'files';
export type MonitorTakeoverState = 'idle' | 'pending' | 'done';
export type MonitorFlagState = { top: boolean; fps: boolean };

const DEFAULT_PC_DIR = 'C:\\SOFT\\YeMan\\PowerControl';
let pcDir = DEFAULT_PC_DIR;

export function setMonitorDataPowerControlDir(dir: string): void {
  pcDir = dir.replace(/\//g, '\\').replace(/\\+$/, '');
}

function clampPercent(value: unknown): number {
  const parsed = Number(value);
  return Number.isFinite(parsed) ? Math.min(100, Math.max(0, parsed)) : 0;
}

// Consumer-only optional projection; no new transport or acquisition. Older
// publishers retain their exact shape, and malformed/missing values are NOT 0.
const optionalTopNumberKeys = ['cpuVoltageV', 'gpuVoltageV', 'gpuTempC', 'gpuUsagePct', 'gpuMemoryUsedMb', 'gpuMemoryTotalMb', 'physicalMemoryUsedMb', 'physicalMemoryTotalMb','physicalMemoryAvailableMb','physicalMemoryLoadPct','gpuMemoryAvailableMb','gpuMemoryLoadPct','networkDownMBps','networkUpMBps','fileReadMBps','fileWriteMBps'] as const;
type OptionalTopNumbers = Partial<Record<typeof optionalTopNumberKeys[number], number | null>>;
const EMPTY_OPTIONAL_TOP_NUMBERS: OptionalTopNumbers = {};
function optionalTopNumbers(raw: Partial<TopMonData>): OptionalTopNumbers {
  let result: OptionalTopNumbers | undefined;
  const source=raw.gpuMemorySource, d3d=source==='hwinfo-d3d-dedicated'||source==='hwinfo-d3d-shared';
  const unavailable=source!==undefined&&(raw.gpuSelectionPresent!==true||(source!=='hwinfo'&&!d3d));
  for (const key of optionalTopNumberKeys) {
    // raw is produced by JSON.parse: no inherited optional field can be
    // admitted, and JSON cannot carry an own property whose value is undefined.
    if (raw[key] === undefined) continue;
    result ??= {};
    if (raw.gpuSelectionPresent === false && key.startsWith('gpu')) { result[key] = null; continue; }
    if((unavailable||(d3d&&key!=='gpuMemoryUsedMb'))&&key.startsWith('gpuMemory')){result[key]=null;continue;}
    const value = raw[key];
    const voltage = key === 'cpuVoltageV' || key === 'gpuVoltageV';
    const load = key === 'physicalMemoryLoadPct' || key === 'gpuMemoryLoadPct' || key === 'gpuUsagePct';
    const temperature = key === 'gpuTempC';
    const rate = key === 'networkDownMBps' || key === 'networkUpMBps' || key === 'fileReadMBps' || key === 'fileWriteMBps';
    result[key] = typeof value === 'number' && Number.isFinite(value) && (voltage ? value >= .05 && value <= 3 : value >= 0 && value <= (load ? 100 : temperature ? 150 : rate ? 1e6 : 1e9)) ? value : null;
  }
  return result ?? EMPTY_OPTIONAL_TOP_NUMBERS;
}
// Legacy finite numeric strings remain compatible; null/booleans/empty text
// are missing measurements. Keep numeric storage defaults, with invalid proof
// for consumers instead of silently presenting those defaults as idle zero.
function legacyTopNumber(value: unknown, min: number, max: number, exclusiveMax = false): number | null {
  if (typeof value !== 'number' && !(typeof value === 'string' && value.trim() !== '')) return null;
  const n = Number(value);
  return Number.isFinite(n) && n >= min && (exclusiveMax ? n < max : n <= max) ? n : null;
}
function parseTopPayload(raw: string): TopMonData | null {
  try {
    const r = JSON.parse(raw) as Partial<TopMonData>;
    if (!r || typeof r !== 'object' || Array.isArray(r)) return null;
    const coreUsage = r.coreUsage === undefined ? [] : freezeCoreUsageValues(r.coreUsage);
    const cpuPower = legacyTopNumber(r.tdpW, 0, 2000, true), cpuClock = legacyTopNumber(r.freqMhz, 0, 10000);
    const cpuTemp = legacyTopNumber(r.tempC, 0, 150), cpuLoad = legacyTopNumber(r.cpuUsage, 0, 100);
    const legacyPower = legacyTopNumber(r.chargeW, -250, 250), legacyTime = legacyTopNumber(r.remainMin, 0, 10080);
    const hasPowerProof = r.batteryPowerValid !== undefined || r.batteryPowerSource !== undefined || r.batteryPowerTs !== undefined || r.batteryPowerEstimated !== undefined || r.batteryPowerWindowMs !== undefined;
    const hasTimeProof = r.batteryTimeValid !== undefined || r.batteryTimeSource !== undefined || r.batteryTimeTs !== undefined || r.batteryTimeEstimated !== undefined;
    const percent = r.batteryStatusTs === undefined
      ? legacyTopNumber(r.batteryPercent, 0, 100) ?? -1
      : typeof r.batteryPercent === 'number' && Number.isFinite(r.batteryPercent) ? r.batteryPercent : -1;
    const batteryPercent = percent >= 0 && percent <= 100 ? percent : -1;
    const legacyPowerValid = r.hasBattery === true && batteryPercent >= 0 && legacyPower !== null;
    // parseTopPayload receives JSON.parse objects; these keys are never inherited.
    // Undefined means absent, while null remains an explicit field.
    const selectedGpuPacket = r.gpuUsagePct !== undefined || r.gpuName !== undefined || r.gpuSelectionPresent !== undefined;
    const knownPowerSource = ['hwinfo','windows-rate','energy-delta','package-power-model'].includes(String(r.batteryPowerSource));
    const sourceValid = r.batteryPowerSource === undefined || knownPowerSource;
    const powerValid = sourceValid && r.hasBattery === true && batteryPercent >= 0 && r.batteryPowerValid === true
      && typeof r.chargeW === 'number' && Number.isFinite(r.chargeW) && Math.abs(r.chargeW) <= 250;
    const hasFpsLimit = r.fpsLimit !== undefined || r.fpsLimitValid !== undefined || r.fpsLimitTs !== undefined || r.fpsLimitSource !== undefined;
    const fpsLimitValid = r.fpsLimitValid === true && r.fpsLimitSource === 'rtss-global'
      && typeof r.fpsLimit === 'number' && Number.isFinite(r.fpsLimit) && r.fpsLimit >= 0 && r.fpsLimit <= 1000000
      && typeof r.fpsLimitTs === 'number' && Number.isSafeInteger(r.fpsLimitTs) && r.fpsLimitTs > 0;
    const hasFan = r.fanEnabled !== undefined || r.fanSpeedPercent !== undefined || r.fanSpeedValid !== undefined || r.fanSpeedTs !== undefined || r.fanSpeedSource !== undefined;
    const fanSpeedValid = r.fanEnabled === true && r.fanSpeedValid === true && r.fanSpeedSource === 'ymcc-fan-target'
      && typeof r.fanSpeedPercent === 'number' && Number.isFinite(r.fanSpeedPercent) && r.fanSpeedPercent >= 0 && r.fanSpeedPercent <= 100
      && typeof r.fanSpeedTs === 'number' && Number.isSafeInteger(r.fanSpeedTs) && r.fanSpeedTs > 0;
    const timeSourceValid = r.batteryTimeSource === undefined || r.batteryTimeSource === 'energy-power' || r.batteryTimeSource === 'sensor-estimate';
    const timeValid = timeSourceValid && r.hasBattery === true && r.batteryTimeValid === true && powerValid && r.ac === 0 && Number(r.chargeW) < -.05
      && typeof r.remainMin === 'number' && Number.isFinite(r.remainMin) && r.remainMin >= 0 && r.remainMin <= 10080;

    const legacyTimeValid = !hasTimeProof && r.hasBattery === true && batteryPercent >= 0
      && legacyTopNumber(r.ac, 0, 1) === 0 && (hasPowerProof ? powerValid : legacyPower !== null)
      && legacyPower !== null && legacyPower < -.05 && legacyTime !== null;
    return {
      ts: Number(r.ts) || 0,
      ...(hasFpsLimit ? { fpsLimit: fpsLimitValid ? r.fpsLimit as number : null, fpsLimitValid,
        fpsLimitTs: fpsLimitValid ? r.fpsLimitTs as number : 0, fpsLimitSource: fpsLimitValid ? 'rtss-global' as const : 'unavailable' as const } : {}),
      ...(hasFan ? { fanEnabled: r.fanEnabled === true, fanSpeedPercent: fanSpeedValid ? r.fanSpeedPercent as number : null,
        fanSpeedValid, fanSpeedTs: fanSpeedValid ? r.fanSpeedTs as number : 0, fanSpeedSource: fanSpeedValid ? 'ymcc-fan-target' as const : 'unavailable' as const } : {}),
      ...(r.fileSystemTs === undefined && r.fileSystemSource === undefined ? {} : {
        fileSystemTs: typeof r.fileSystemTs === 'number' && Number.isSafeInteger(r.fileSystemTs) && r.fileSystemTs > 0 ? r.fileSystemTs : 0,
        fileSystemSource: r.fileSystemSource === 'windows-filesystem-disk-activity' ? 'windows-filesystem-disk-activity' as const : 'unavailable' as const }),
      ...(r.cpuUsageValid === undefined && r.cpuUsageTs === undefined && cpuLoad !== null ? {} : {
        cpuUsageValid: r.cpuUsageValid === true && typeof r.cpuUsage === 'number' && Number.isFinite(r.cpuUsage) && r.cpuUsage >= 0 && r.cpuUsage <= 100,
        cpuUsageTs: typeof r.cpuUsageTs === 'number' && Number.isSafeInteger(r.cpuUsageTs) && r.cpuUsageTs > 0 ? r.cpuUsageTs : 0,
      }),
      ...(r.cpuPowerValid === undefined && cpuPower !== null ? {} : { cpuPowerValid: r.cpuPowerValid === true && typeof r.tdpW === 'number' && Number.isFinite(r.tdpW) && r.tdpW >= 0 && r.tdpW < 2000 }),
      ...(r.cpuClockValid === undefined && cpuClock !== null ? {} : { cpuClockValid: r.cpuClockValid === true && typeof r.freqMhz === 'number' && Number.isFinite(r.freqMhz) && r.freqMhz >= 0 && r.freqMhz <= 10000 }),
      ...(r.cpuTempValid === undefined && cpuTemp !== null ? {} : { cpuTempValid: r.cpuTempValid === true && typeof r.tempC === 'number' && Number.isFinite(r.tempC) && r.tempC >= 0 && r.tempC <= 150 }),
      tdpW: cpuPower ?? 0,
      freqMhz: cpuClock ?? 0,
      tempC: cpuTemp ?? 0,
      ac: Number(r.ac) === 0 ? 0 : 1,
      hasBattery: r.hasBattery === true,
      batteryPercent,
      ...(r.batteryStatusTs === undefined ? {} : { batteryStatusTs: typeof r.batteryStatusTs === 'number' && Number.isFinite(r.batteryStatusTs) && r.batteryStatusTs > 0 ? r.batteryStatusTs : 0 }),
      chargeW: hasPowerProof ? powerValid ? r.chargeW as number : 0 : legacyPowerValid ? legacyPower! : 0,
      ...(!hasPowerProof && legacyPowerValid ? {} : {
        batteryPowerValid: powerValid,
        batteryPowerEstimated: powerValid && (r.batteryPowerEstimated === true || r.batteryPowerSource === 'energy-delta' || r.batteryPowerSource === 'package-power-model'),
        batteryPowerTs: typeof r.batteryPowerTs === 'number' && Number.isFinite(r.batteryPowerTs) && r.batteryPowerTs > 0 ? r.batteryPowerTs : 0,
        batteryPowerSource: (['hwinfo','windows-rate','energy-delta','package-power-model','unavailable'] as const).find(source => source === r.batteryPowerSource) || 'unavailable',
        batteryPowerWindowMs: typeof r.batteryPowerWindowMs === 'number' && Number.isFinite(r.batteryPowerWindowMs) && r.batteryPowerWindowMs >= 0 ? r.batteryPowerWindowMs : 0,
      }),
      ...(!hasTimeProof && legacyTimeValid ? {} : {
        batteryTimeValid: timeValid,
        batteryTimeEstimated: timeValid && (r.batteryTimeEstimated === true || r.batteryTimeSource === 'energy-power' || r.batteryTimeSource === 'sensor-estimate'),
        batteryTimeTs: typeof r.batteryTimeTs === 'number' && Number.isFinite(r.batteryTimeTs) && r.batteryTimeTs > 0 ? r.batteryTimeTs : 0,
        batteryTimeSource: (['energy-power','sensor-estimate','unavailable'] as const).find(source => source === r.batteryTimeSource) || 'unavailable',
      }),
      remainMin: hasTimeProof ? timeValid ? r.remainMin as number : -1 : legacyTimeValid ? legacyTime! : -1,
      cpuUsage: cpuLoad ?? 0,
      // Keep legacy payload shape intact when the sole publisher has no core fields.
      ...(r.coreUsage === undefined ? {} : {
        coreUsage, coreLabels: freezeCoreUsageLabels(r.coreLabels, coreUsage.length),
        ...(r.coreUsageTs === undefined ? {} : { coreUsageTs:
          typeof r.coreUsageTs === 'number' && Number.isFinite(r.coreUsageTs) && r.coreUsageTs > 0 ? r.coreUsageTs : 0 }),
      }),
      gpuPowerW: selectedGpuPacket ? (r.gpuSelectionPresent !== false && typeof r.gpuPowerW === 'number' && Number.isFinite(r.gpuPowerW) && r.gpuPowerW >= 0 && r.gpuPowerW < 2000 ? r.gpuPowerW : null) : legacyTopNumber(r.gpuPowerW, 0, 2000, true),
      gpuClockMhz: selectedGpuPacket ? (r.gpuSelectionPresent !== false && typeof r.gpuClockMhz === 'number' && Number.isFinite(r.gpuClockMhz) && r.gpuClockMhz >= 0 && r.gpuClockMhz <= 10000 ? r.gpuClockMhz : null) : legacyTopNumber(r.gpuClockMhz, 0, 10000),
      ...optionalTopNumbers(r),
      ...(r.gpuMemorySource===undefined?{}:{gpuMemorySource:r.gpuSelectionPresent===true&&(r.gpuMemorySource==='hwinfo'||r.gpuMemorySource==='hwinfo-d3d-dedicated'||r.gpuMemorySource==='hwinfo-d3d-shared')?r.gpuMemorySource:'unavailable'}),
      ...(selectedGpuPacket ? { gpuUsagePct: r.gpuSelectionPresent !== false && typeof r.gpuUsagePct === 'number' && Number.isFinite(r.gpuUsagePct) && r.gpuUsagePct >= 0 && r.gpuUsagePct <= 100 ? r.gpuUsagePct : null, gpuName: typeof r.gpuName === 'string' ? r.gpuName.slice(0, 128) : null, gpuSelectionPinned: r.gpuSelectionPinned === true, gpuSelectionPresent: r.gpuSelectionPresent === true } : {}),
      thermalThrottleFound: r.thermalThrottleFound === true,
      thermalThrottleMax: Number(r.thermalThrottleMax) || 0,
      virtualMemoryCommittedFound: r.virtualMemoryCommittedFound === true,
      virtualMemoryCommittedMb: Number(r.virtualMemoryCommittedMb) || 0,
      virtualMemoryLoadFound: r.virtualMemoryLoadFound === true,
      virtualMemoryLoadPct: clampPercent(r.virtualMemoryLoadPct),
      hwDown: !!r.hwDown,
    };
  } catch {
    return null;
  }
}

// 与 fps-status.json 历史字段默认值一致。
function parseFpsPayload(raw: string): MonitorFpsPayload | null {
  try {
    const r = JSON.parse(raw) as Partial<MonitorFpsPayload>;
    if (!r || typeof r !== 'object' || Array.isArray(r)) return null;
    // An absent/invalid main reading is not a measured zero. Do not admit it
    // to either display or control consumers as a fabricated active packet.
    if (typeof r.fps !== 'number' || !Number.isFinite(r.fps) || r.fps<0 || r.fps>1000000) return null;
    // Modern observations bind to one real Windows process lifetime. Do not
    // silently drop a malformed birth token or coerce a string PID and then
    // present the reading under a weaker identity. Legacy packets without
    // observation/identity metadata retain their bounded compatibility path.
    const modern = hasExplicitFpsMetadata(r);
    if (modern && (typeof r.pid !== 'number' || !Number.isSafeInteger(r.pid) || r.pid <= 0 || r.pid > 0xffffffff ||
        typeof r.processCreated !== 'string' || !/^[1-9][0-9]{0,19}$/.test(r.processCreated) ||
        (r.processCreated.length === 20 && r.processCreated > '18446744073709551615'))) return null;
    return {
      ...(presentedFrameTime(r, true) ?? {}),
      ...(r.fpsSource === undefined && r.fpsObservedAt === undefined ? {} : {
        fpsSource: r.fpsSource === 'rtss' ? 'rtss' as const : r.fpsSource === 'hwinfo-presented' ? 'hwinfo-presented' as const : undefined,
        fpsObservedAt: typeof r.fpsObservedAt === 'number' && Number.isSafeInteger(r.fpsObservedAt) ? r.fpsObservedAt : 0,
      }),
      ...(r.fpsValidUntil === undefined && r.fpsPollPeriodMs === undefined ? {} : {fpsValidUntil:r.fpsValidUntil, fpsPollPeriodMs:r.fpsPollPeriodMs}),
      ...(typeof r.processCreated === 'string' && /^[0-9]{1,20}$/.test(r.processCreated) ? { processCreated: r.processCreated } : {}),
      ...(r.fps1Source === undefined && r.fps1ObservedAt === undefined && r.fps1ValidUntil === undefined && r.fps1PollPeriodMs === undefined ? {} : {fps1Source: r.fps1Source, fps1ObservedAt: r.fps1ObservedAt, fps1ValidUntil: r.fps1ValidUntil, fps1PollPeriodMs: r.fps1PollPeriodMs}),
      ...(hasFpsHardwareProof(r) ? {
        hardwareObservedAt: typeof r.hardwareObservedAt === 'number' && Number.isSafeInteger(r.hardwareObservedAt) ? r.hardwareObservedAt : 0,
        hardwareValidUntil: typeof r.hardwareValidUntil === 'number' && Number.isSafeInteger(r.hardwareValidUntil) ? r.hardwareValidUntil : 0,
        hardwarePollPeriodMs: typeof r.hardwarePollPeriodMs === 'number' && Number.isSafeInteger(r.hardwarePollPeriodMs) ? r.hardwarePollPeriodMs : -1,
      } : {}),
      ts: Number(r.ts) || 0,
      fps: r.fps,
      fps1: typeof r.fps1 === 'number' && Number.isFinite(r.fps1) && r.fps1>=0 && r.fps1<=1000000 ? r.fps1 : -1,
      gpu: typeof r.gpu === 'number' && Number.isFinite(r.gpu) && r.gpu>=0 && r.gpu<=100 ? r.gpu : null,
      packagePower: typeof r.packagePower === 'number' && Number.isFinite(r.packagePower) && r.packagePower >= 0 && r.packagePower <= 2000 ? r.packagePower : -1,
      game: typeof r.game === 'string' ? r.game : null,
      pid: Number(r.pid) || 0,
    };
  } catch {
    return null;
  }
}

function isUnknownCommand(err: unknown): boolean {
  const message = err instanceof Error ? err.message : String(err);
  return /^unknown:/i.test(message.trim());
}

export interface MonitorDataHub {
  admitDisplayPublication: (raw:unknown) => MonitorSnapshotView | null | undefined;
  read: () => Promise<MonitorSnapshotView | null>;
  acquire: (owner: string, need: { top?: boolean; fps?: boolean }) => () => void;
  transport: () => MonitorTransportMode;
  /** 已确认（ok 回执或读回观察到活跃）的需求标志（测试/诊断） */
  applied: () => MonitorFlagState;
  /** 已发出但结果未知（超时/拒绝）的标志（测试/诊断） */
  pending: () => MonitorFlagState;
  /** 期望需求（测试/诊断） */
  desired: () => MonitorFlagState;
  /** 当前 native 会话号（null=未知/对端未提供，测试/诊断） */
  session: () => number | null;
  /** takeover 状态：idle=未尝试 / pending=失败待重试 / done=已确认（测试/诊断） */
  takeover: () => MonitorTakeoverState;
  /** 已发生的 start/stop/takeover 调用次数（有界重试观察，测试/诊断） */
  attempts: () => { start: number; stop: number; takeover: number };
  pendingReads: () => number;
}

const SNAPSHOT_SCHEMA = 1;
const START_RETRY_BUDGET = 6;
const STOP_RETRY_BUDGET = 6;
const TAKEOVER_RETRY_BUDGET = 5;

interface MonitorReadSourceReceipt {
  view: MonitorSnapshotView;
  floorRevision: number;
  // null means the read source belongs to a retired publisher; undefined only skips display.
  readonly finish: (copy: MonitorSnapshotView, receipt: MonitorReadSourceReceipt, display: boolean) => MonitorSnapshotView | null | undefined;
}
interface MonitorDisplayFloor {
  revision(): number;
  refresh(view: MonitorSnapshotView | null): MonitorSnapshotView | null;
}
export function createMonitorDataHub(deps: MonitorHubDeps): MonitorDataHub {
  return createMonitorDataHubOwner(deps);
}
// The production owner can capture a read's exact internal result before it is
// copied for policy/control clients. This capability is not a public read API.
function createMonitorDataHubOwner(deps: MonitorHubDeps,
  captureReadSource?: (copy: MonitorSnapshotView, source: MonitorReadSourceReceipt) => void,
  bindDisplayFloor?: (floor: MonitorDisplayFloor) => void): MonitorDataHub {
  const setTimer =
    deps.setTimer ?? ((fn: () => void, ms: number) => setTimeout(fn, ms) as unknown);
  const clearTimer =
    deps.clearTimer ?? ((handle: unknown) => clearTimeout(handle as ReturnType<typeof setTimeout>));

  let transport: MonitorTransportMode = 'unknown';
  let inflight: Promise<MonitorSnapshotView | null> | null = null;
  let pendingReadCount = 0;
  const snapshotBudget = createMonitorSnapshotBudget();
  // RTSS curve packets often repeat the 1Hz top payload byte-for-byte. Parse
  // once per change. Each repeated packet still gets a fresh outer object for
  // stop-floor/lifecycle edits, while bounded arrays are frozen and shared
  // because all consumers read them only. This removes 128-core/120-frame
  // array copies without sharing mutable scalar state.
  const readonlyArray = <T>(value: readonly T[] | undefined): T[] | undefined =>
    value === undefined ? undefined : Object.freeze(value as T[]) as unknown as T[];
  const decodeTop = createMonitorPayloadDecoder<TopMonData>({parse: parseTopPayload, cacheInvalid: true, copy: value => ({ ...value,
    ...(value.coreUsage === undefined ? {} : {coreUsage: readonlyArray(value.coreUsage)}),
    ...(value.coreLabels === undefined ? {} : {coreLabels: markCoreUsageLabels(readonlyArray(value.coreLabels) as string[])}) })});
  const decodeFps = createMonitorPayloadDecoder<MonitorFpsPayload>({parse: parseFpsPayload, cacheInvalid: true,
    copy: value => ({ ...value, ...(value.frameTimes === undefined ? {} : {frameTimes: readonlyArray(value.frameTimes)}) }),
    // Legacy metadata is deliberately not tightened by this optimization.
    // Check only the six fields that can retain an unusual legacy object;
    // frameTimes is the one intentional array and is already normalized/frozen.
    cacheable: value => !looseFpsMetadata.some(key => {
      const field = value[key as keyof MonitorFpsPayload];
      return field !== null && typeof field === 'object';
    }) });

  // 需求登记：同 owner 每次登记独立 token（旧 disposer 只释放自己的登记）。
  const demands = new Map<string, { token: number; top: boolean; fps: boolean }>();
  let nextToken = 1;

  // 每个标志三态：confirmed（明确成功/读回活跃）、pending（已发出未确认）。
  const confirmed: MonitorFlagState = { top: false, fps: false };
  const pending: MonitorFlagState = { top: false, fps: false };

  // 停止失效（按字段自身时间戳比较；旧在途回复不得复活已释放数据）。
  const lastStopAt = { top: 0, fps: 0 };
  let stopFloorRevision = 0;
  let sessionFloorRevision = 0;
  // Only production reads need a handoff stamp; native pushes and public hub
  // instances do not allocate per-publication bookkeeping.
  const readSourceFloors = captureReadSource ? new WeakMap<MonitorSnapshotView, number>() : null;

  // 199：命令/读取的版本关联。读回只能确认它**确实观察到**的命令状态：
  //   · intentEpoch 在每次"发出"启停命令前自增，并记录到对应标志（flagIntent）；
  //   · 读请求在**发起前**捕获 doneEpoch（必须先于 pump，因为 pump 会同步发出命令）；
  //   · 读回校准时，只有 flagIntent[flag] <= doneEpoch（该标志的读请求发出后没有再发命令）
  //     才允许校准。于是"较早发出、较晚返回"的旧回复抹不掉更新的 pending，
  //     也不会在停止收敛后又被旧 active 读回重新触发命令。
  // 说明（与 native 的配合）：native 的 topActive/fpsActive 是**响应时刻的活闩锁**，且 IPC 按
  // 队列顺序处理，因此"读请求晚于命令发出"的观察一定晚于该命令被执行 ⇒ 该观察可安全校准。
  let intentEpoch = 0;
  const flagIntent = { top: 0, fps: 0 };

  // 生命周期基准：会话号（真实会话变化才重置）+ 采集代际/序号（同会话单调）。
  let expectedSession: number | null = null;
  // First identity discovery keeps initial requests; only replacement retires them.
  let controlSessionEpoch = 0;
  const retiredSessions = new Set<number>();
  let expectedGeneration = 0;
  let expectedSeq = 0;
  let expectedRelayEpoch: number | null = null;

  // 有界重试：intent（acquire/release）立即推进；poll/timer 仅按退避推进（read 不绕退避）。
  let startBudget = START_RETRY_BUDGET;
  let stopBudget = STOP_RETRY_BUDGET;
  let startRetryNextAt = 0; // 启动侧退避门
  let stopRetryNextAt = 0;  // 停止侧退避门（与启动分开：启动失败不得阻塞停止收尾）
  let retryTimer: unknown = null;
  let pumpRunning = false;
  let pumpQueued = false;
  const attempts = { start: 0, stop: 0, takeover: 0 };

  // takeover：明确回执（ok && cleaned）才算完成；绑定会话与 relay epoch。
  let takeoverState: MonitorTakeoverState = 'idle';
  let takeoverBudget = TAKEOVER_RETRY_BUDGET;
  let takeoverInFlight = false;

  function desired(): MonitorFlagState {
    const want = { top: false, fps: false };
    for (const need of demands.values()) {
      if (need.top) want.top = true;
      if (need.fps) want.fps = true;
    }
    return want;
  }

  function backoffMs(remaining: number, budgetMax: number): number {
    const used = Math.max(0, budgetMax - remaining);
    return Math.min(8000, 500 * Math.pow(2, used));
  }

  function hasUnsettledWork(): boolean {
    const want = desired();
    return (
      (want.top && !confirmed.top) ||
      (want.fps && !confirmed.fps) ||
      (!want.top && (confirmed.top || pending.top)) ||
      (!want.fps && (confirmed.fps || pending.fps))
    );
  }

  function nextRetryAt(): number {
    const want = desired();
    const stopWork =
      (!want.top && (confirmed.top || pending.top)) || (!want.fps && (confirmed.fps || pending.fps));
    const startWork = (want.top && !confirmed.top) || (want.fps && !confirmed.fps);
    const gates: number[] = [];
    if (stopWork) gates.push(stopRetryNextAt);
    if (startWork) gates.push(startRetryNextAt);
    return gates.length ? Math.min(...gates) : 0;
  }

  function scheduleRetry(): void {
    if (!hasUnsettledWork()) return;
    if (startBudget <= 0 && stopBudget <= 0) return;
    if (retryTimer !== null) return;
    const wait = Math.max(0, nextRetryAt() - deps.now());
    retryTimer = setTimer(() => {
      retryTimer = null;
      void pump('timer');
    }, wait > 0 ? wait : 500);
  }

  function cancelRetry(): void {
    if (retryTimer !== null) {
      clearTimer(retryTimer);
      retryTimer = null;
    }
  }

  // 真实会话变化：重置确认/接管与代际/序号基准，并重新协调仍有的需求。
  function acceptNewSession(session: number): void {
    if (expectedSession !== null) {
      retiredSessions.add(expectedSession);
      if (retiredSessions.size > 16) retiredSessions.delete(retiredSessions.values().next().value!);
    }
    expectedSession = session;
    ++controlSessionEpoch; cancelRetry();
    sessionFloorRevision = ++stopFloorRevision;
    lastStopAt.top = lastStopAt.fps = 0; // New publisher identity, not an old wall-clock stop floor.
    expectedGeneration = 0;
    expectedSeq = 0;
    confirmed.top = false;
    confirmed.fps = false;
    pending.top = false;
    pending.fps = false;
    takeoverState = 'idle';
    takeoverBudget = TAKEOVER_RETRY_BUDGET;
    startBudget = START_RETRY_BUDGET;
    stopBudget = STOP_RETRY_BUDGET;
    startRetryNextAt = 0;
    stopRetryNextAt = 0;
    void pump('intent');
  }

  // 需求与回执对齐：停止先于启动；单跑者 + 每次 await 后重算（旧意图不覆盖新意图）。
  // trigger: intent=新意图（立即推进）；poll/timer=按退避推进（日常 read 不绕退避耗预算）。
  async function pump(trigger: 'intent' | 'poll' | 'timer'): Promise<void> {
    if (pumpRunning) {
      pumpQueued = true;
      return;
    }
    // 新意图只允许"立即推进一次"：清掉两侧退避门一次；同一次 pass 内的后续重试仍必须服从退避。
    if (trigger === 'intent') {
      startRetryNextAt = 0;
      stopRetryNextAt = 0;
    }
    pumpRunning = true;
    try {
      for (let guard = 0; guard < 8; guard++) {
        pumpQueued = false;
        const want = desired();
        const needStartTop = want.top && !confirmed.top;
        const needStartFps = want.fps && !confirmed.fps;
        const needStopTop = !want.top && (confirmed.top || pending.top);
        const needStopFps = !want.fps && (confirmed.fps || pending.fps);
        if (!needStartTop && !needStartFps && !needStopTop && !needStopFps) {
          cancelRetry();
          break;
        }
        // 退避门按工作类型分别判断（停止失败不阻塞启动重试，反之亦然）。
        if (needStopTop || needStopFps) {
          if (stopBudget <= 0) break;
          if (deps.now() < stopRetryNextAt) {
            scheduleRetry();
            break;
          }
          // 期望已释放：先落失效门（即使回执失败，旧在途回复也不得复活数据）。
          ++stopFloorRevision;
          if (needStopTop) lastStopAt.top = deps.now();
          if (needStopFps) lastStopAt.fps = deps.now();
          attempts.stop += 1;
          stopBudget -= 1;
          pending.top = pending.top || needStopTop;
          pending.fps = pending.fps || needStopFps;
          // 199：命令版本——发出一刻即记账，早于该命令发出的读回不得清掉它。
          const stopSessionEpoch = controlSessionEpoch;
          const stopIssueEpoch = ++intentEpoch;
          if (needStopTop) flagIntent.top = stopIssueEpoch;
          if (needStopFps) flagIntent.fps = stopIssueEpoch;
          try {
            const res = (await deps.invoke('monitor.stop', {
              top: needStopTop,
              fps: needStopFps,
            })) as Record<string, unknown> | null;
            if (stopSessionEpoch !== controlSessionEpoch) continue;
            if (res && res.ok === true) {
              if (needStopTop) {
                confirmed.top = false;
                pending.top = false;
              }
              if (needStopFps) {
                confirmed.fps = false;
                pending.fps = false;
              }
              stopRetryNextAt = 0;
            } else {
              stopRetryNextAt = deps.now() + backoffMs(stopBudget, STOP_RETRY_BUDGET);
            }
          } catch {
            if (stopSessionEpoch !== controlSessionEpoch) continue;
            // 超时/异常 = 结果未知：可能已执行 ⇒ 保持 pending，按预算重试或由读回校准收敛。
            stopRetryNextAt = deps.now() + backoffMs(stopBudget, STOP_RETRY_BUDGET);
          }
          scheduleRetry();
          continue; // 重算最新需求，旧意图不覆盖新意图
        }
        if (needStartTop || needStartFps) {
          if (startBudget <= 0) break;
          if (deps.now() < startRetryNextAt) {
            scheduleRetry();
            break;
          }
          attempts.start += 1;
          startBudget -= 1;
          pending.top = pending.top || needStartTop;
          pending.fps = pending.fps || needStartFps;
          // 199：命令版本——发出一刻即记账（见 flagIntent 注释）。
          const startSessionEpoch = controlSessionEpoch;
          const startIssueEpoch = ++intentEpoch;
          if (needStartTop) flagIntent.top = startIssueEpoch;
          if (needStartFps) flagIntent.fps = startIssueEpoch;
          try {
            const res = (await deps.invoke('monitor.start', {
              top: needStartTop,
              fps: needStartFps,
            })) as Record<string, unknown> | null;
            if (startSessionEpoch !== controlSessionEpoch) continue;
            if (res && res.ok === true) {
              if (needStartTop) {
                confirmed.top = true;
                pending.top = false;
              }
              if (needStartFps) {
                confirmed.fps = true;
                pending.fps = false;
              }
              startRetryNextAt = 0;
            } else {
              // 拒绝（ok:false）：不得当作成功；保持 pending（可能已执行）并退避。
              startRetryNextAt = deps.now() + backoffMs(startBudget, START_RETRY_BUDGET);
            }
          } catch {
            if (startSessionEpoch !== controlSessionEpoch) continue;
            startRetryNextAt = deps.now() + backoffMs(startBudget, START_RETRY_BUDGET);
          }
          scheduleRetry();
          continue;
        }
      }
    } finally {
      pumpRunning = false;
      if (pumpQueued) void pump('poll');
    }
  }

  // 读回校准：不依赖回执即可收敛。仅当对端**显式携带**布尔闩锁时才校准
// （字段缺失=信息不足，不得按"不活跃"处理，否则会对过渡版 native 反复重发 start）。
// 199：只校准"读请求发出后没有再发命令"的标志（flagIntent <= doneEpoch）——旧回复不得
// 清掉较新命令的 pending/confirmed；被跳过的标志由有界重试或更晚的有效读回收敛。
  function reconcileFromView(active: { top: boolean | null; fps: boolean | null },
                             doneEpoch: number): void {
    const topCalibratable = flagIntent.top <= doneEpoch;
    const fpsCalibratable = flagIntent.fps <= doneEpoch;
    if (topCalibratable) {
      if (active.top === true) confirmed.top = true;
      if (active.top === false) {
        confirmed.top = false;
        pending.top = false;
      }
    }
    if (fpsCalibratable) {
      if (active.fps === true) confirmed.fps = true;
      if (active.fps === false) {
        confirmed.fps = false;
        pending.fps = false;
      }
    }
    // 校准产生新的未决工作（如需停止/需重发）时按退避推进一次（成功的路径 retryNextAt=0 ⇒ 立即）。
    if (hasUnsettledWork()) void pump('poll');
  }

  // takeover：需 ok && cleaned、绑定发起时的会话与 relay epoch；迟到回执不得标记新会话。
  function maybeTakeover(): void {
    if (transport === 'files' || takeoverState === 'done' || takeoverInFlight) return;
    if (takeoverBudget <= 0) return;
    takeoverInFlight = true;
    attempts.takeover += 1;
    takeoverBudget -= 1;
    takeoverState = 'pending'; // 已发起未确认：不得当作 idle（更不得当作 done）
    const sessionAtIssue = expectedSession;
    const epochAtIssue = expectedRelayEpoch;
    const args: Record<string, unknown> = {};
    if (epochAtIssue !== null) args.epoch = epochAtIssue;
    void (async () => {
      try {
        const res = (await deps.invoke('monitor.takeover', args)) as Record<string, unknown> | null;
        if (expectedSession !== sessionAtIssue || expectedRelayEpoch !== epochAtIssue) return; // 旧会话/旧 epoch
        takeoverState = res && res.ok === true && res.cleaned === true ? 'done' : 'pending';
      } catch {
        if (expectedSession === sessionAtIssue && expectedRelayEpoch === epochAtIssue) {
          takeoverState = 'pending';
        }
      } finally {
        takeoverInFlight = false;
      }
    })();
  }

  // 生命周期 + 数据校验：只有真实会话变化才重置；同会话低代际/乱序拒收。
  function acceptLifecycle(r: Record<string, unknown>): 'ok' | 'stale' | 'new-session' {
    const sessionRaw = Number(r.session);
    const hasSession = Number.isFinite(sessionRaw) && sessionRaw > 0;
    const generation = Number(r.generation);
    const seq = Number(r.seq);
    const relayRaw = Number(r.relayEpoch);
    if (!Number.isSafeInteger(generation) || generation < 0) return 'stale';
    if (!Number.isSafeInteger(seq) || seq < 0) return 'stale';
    if (r.session != null && sessionRaw !== 0 && (!Number.isSafeInteger(sessionRaw) || sessionRaw < 0)) return 'stale';
    if (hasSession && retiredSessions.has(sessionRaw)) return 'stale';
    if (hasSession) {
      if (expectedSession === null) {
        // 首次"学会"会话号不是会话变化：只建立基准，不重置确认/接管。
        expectedSession = sessionRaw;
        expectedGeneration = generation;
        expectedSeq = seq;
        if (Number.isFinite(relayRaw)) expectedRelayEpoch = relayRaw;
        return 'ok';
      }
      if (sessionRaw !== expectedSession) {
        // 真实会话变化：重置确认/接管与代际/序号基准，并重新协调仍有的需求。
        acceptNewSession(sessionRaw);
        expectedGeneration = generation;
        expectedSeq = seq;
        if (Number.isFinite(relayRaw)) expectedRelayEpoch = relayRaw;
        return 'new-session';
      }
    }
    // 同会话（或对端无 session 的兼容情形）：严格单调。
    if (generation < expectedGeneration) return 'stale';
    if (generation === expectedGeneration && seq < expectedSeq) return 'stale';
    expectedGeneration = generation;
    expectedSeq = seq;
    if (Number.isFinite(relayRaw)) expectedRelayEpoch = relayRaw;
    return 'ok';
  }

  function normalizeSnapshot(res: unknown): MonitorSnapshotView | null {
    if (!res || typeof res !== 'object') return null;
    const r = res as Record<string, unknown>;
    if (r.available !== true) return null;
    if (Number(r.schema) !== SNAPSHOT_SCHEMA) return null;
    const collectedAt = Number(r.collectedAt);
    if (!Number.isSafeInteger(collectedAt) || collectedAt <= 0 || collectedAt > deps.now()) return null;
    // Apply the native wire-byte budget before inner JSON decoding or lifecycle
    // admission. Oversized packets cannot consume a seq/session or renew data.
    if (!snapshotBudget(r.top, r.fps)) return null;
    const top = typeof r.top === 'string' ? decodeTop(r.top) : null;
    const fps = typeof r.fps === 'string' ? decodeFps(r.fps) : null;
    if ((r.top != null && !top) || (r.fps != null && !fps)) return null;
    const lifecycle = acceptLifecycle(r);
    if (lifecycle === 'stale') return null; // Reject before updating data, after validating envelope.
    const heartbeat = Number(r.heartbeatAt);
    const hwinfoOk = Number(r.hwinfoOkAt);
    const sessionRaw = Number(r.session);
    const relayRaw = Number(r.relayEpoch);
    return {
      schema: SNAPSHOT_SCHEMA,
      session: Number.isFinite(sessionRaw) && sessionRaw > 0 ? sessionRaw : null,
      relayEpoch: Number.isFinite(relayRaw) ? relayRaw : null,
      generation: Number(r.generation) || 0,
      seq: Number(r.seq) || 0,
      collectedAt,
      heartbeatAt: Number.isFinite(heartbeat) && heartbeat > 0 ? heartbeat : null,
      hwinfoOkAt: Number.isFinite(hwinfoOk) && hwinfoOk > 0 ? hwinfoOk : null,
      top,
      fps,
      topActive: r.topActive === true,
      fpsActive: r.fpsActive === true,
      hwDown: r.hwDown === true,
      source: 'snapshot',
    };
  }

  // 停止失效：按各字段自身时间戳比较（文件兼容分支同样适用）。不晚于停止意图的都不复活。
  function applyStaleFloor(view: MonitorSnapshotView, rememberRead = false): MonitorSnapshotView {
    if (view.top && lastStopAt.top > 0) {
      const top = view.top, floor = lastStopAt.top;
      const newer = (ts: unknown) => typeof ts === 'number' && Number.isFinite(ts) && ts > floor && ts <= deps.now();
      const hwStopped = top.ts > 0 && top.ts <= floor;
      if (top.cpuUsageTs !== undefined && !newer(top.cpuUsageTs)) { top.cpuUsageTs = 0; top.cpuUsageValid = false; }
      if (top.fpsLimitTs !== undefined && !newer(top.fpsLimitTs)) {
        top.fpsLimitTs = 0; top.fpsLimit = null; top.fpsLimitValid = false; top.fpsLimitSource = 'unavailable';
      }
      if (top.fileSystemTs !== undefined && !newer(top.fileSystemTs)) {
        top.fileSystemTs=0;top.fileSystemSource='unavailable';top.fileReadMBps=top.fileWriteMBps=null;
      }
      if (top.fanSpeedTs !== undefined && !newer(top.fanSpeedTs)) {
        top.fanSpeedTs = 0; top.fanSpeedPercent = null; top.fanSpeedValid = false; top.fanEnabled = false; top.fanSpeedSource = 'unavailable';
      }
      if (top.coreUsageTs !== undefined && !newer(top.coreUsageTs)) {
        top.coreUsageTs = 0; top.coreUsage = []; top.coreLabels = [];
      }
      if (top.batteryStatusTs !== undefined && !newer(top.batteryStatusTs)) {
        top.batteryStatusTs = 0; top.batteryPercent = -1;
      }
      if (top.batteryPowerTs !== undefined && !newer(top.batteryPowerTs)) {
        top.batteryPowerTs = 0; top.batteryPowerValid = false; top.batteryPowerEstimated = false;
        top.batteryPowerSource = 'unavailable'; top.batteryPowerWindowMs = 0; top.chargeW = 0;
      }
      if (top.batteryTimeTs !== undefined && (!newer(top.batteryTimeTs) || top.batteryPowerValid === false)) {
        top.batteryTimeTs = 0; top.batteryTimeValid = false; top.batteryTimeEstimated = false;
        top.batteryTimeSource = 'unavailable'; top.remainMin = -1;
      }
      if (hwStopped) {
        const independent = (top.cpuUsageValid === true && newer(top.cpuUsageTs)) || (top.fileSystemSource === 'windows-filesystem-disk-activity' && newer(top.fileSystemTs)) || (top.fanSpeedValid === true && newer(top.fanSpeedTs)) || (top.fpsLimitValid === true && newer(top.fpsLimitTs)) || newer(top.coreUsageTs) || newer(top.batteryStatusTs) || newer(top.batteryPowerTs) || newer(top.batteryTimeTs);
        if (!independent) view.top = null;
        else { top.ts = 0; top.hwDown = true; } // Preserve new Windows fields; never promote old CPU data.
      }
    }
    if (view.fps && view.fps.ts > 0 && view.fps.ts <= lastStopAt.fps) view.fps = null;
    if (view.heartbeatAt !== null && view.heartbeatAt <= lastStopAt.fps) view.heartbeatAt = null;
    if (view.hwinfoOkAt !== null && view.hwinfoOkAt <= Math.max(lastStopAt.top, lastStopAt.fps)) {
      view.hwinfoOkAt = null;
    }
    if (view.fps && !fpsHardwareProofFresh(view.fps, deps.now(), view.hwDown || view.top?.hwDown === true, lastStopAt.fps)) {
      view.fps.gpu = null; view.fps.packagePower = -1;
    }
    if (rememberRead) readSourceFloors?.set(view, stopFloorRevision);
    return view;
  }

  async function readViaSnapshot(doneEpoch: number): Promise<MonitorSnapshotView | null> {
    const sessionAtRead = expectedSession;
    const res = await deps.invoke('monitor.snapshot', {}, { timeoutMs: 2000 });
    if (!res || typeof res !== 'object' || Array.isArray(res)) return null;
    const raw = res as Record<string, unknown>;
    if (Number(raw.schema) !== SNAPSHOT_SCHEMA) return null;
    if (expectedSession !== sessionAtRead && Number(raw.session) !== expectedSession) return null;
    const observed = { top: typeof raw.topActive === 'boolean' ? raw.topActive : null,
                       fps: typeof raw.fpsActive === 'boolean' ? raw.fpsActive : null };
    if (raw.available === false) {
      if (acceptLifecycle(raw) !== 'stale') reconcileFromView(observed, doneEpoch);
      return null;
    }
    // Validate the complete available envelope before advancing lifecycle.
    const view = normalizeSnapshot(raw);
    if (!view) return null;
    reconcileFromView(observed, doneEpoch); // Missing flags are not a false read receipt.
    maybeTakeover();
    return applyStaleFloor(view, true);
  }

  // 兼容通路：旧 native 无快照命令时按原四文件读取，字段语义不变。
  async function readViaFiles(): Promise<MonitorSnapshotView | null> {
    const readSafe = async (path: string, maxBytes: number): Promise<string | null> => {
      try {
        return await deps.readTextFile(path, maxBytes);
      } catch {
        return null;
      }
    };

    const [topRaw, fpsRaw, hbRaw, hwinfoRaw] = await Promise.all([
      readSafe(pcDir + '\\topmon.json', 8192), readSafe(pcDir + '\\fps-status.json', 8192),
      readSafe(pcDir + '\\fps-monitor.hb', 512), readSafe(pcDir + '\\hwinfo-ok', 64),
    ]);
    const top = topRaw ? decodeTop(topRaw) : null;
    const fps = fpsRaw ? decodeFps(fpsRaw) : null;
    let heartbeatAt: number | null = null;
    if (hbRaw) {
      try {
        const ts = Number((JSON.parse(hbRaw) as { ts?: number }).ts);
        if (Number.isFinite(ts) && ts > 0) heartbeatAt = ts;
      } catch {
        heartbeatAt = null;
      }
    }
    let hwinfoOkAt: number | null = null;
    if (hwinfoRaw !== null) {
      const ts = Number(hwinfoRaw.trim());
      if (Number.isFinite(ts) && ts > 0) hwinfoOkAt = ts;
    }
    if (!top && !fps && heartbeatAt === null && hwinfoOkAt === null) return null;
    const collectedAt = Math.max(top?.ts ?? 0, fps?.ts ?? 0, heartbeatAt ?? 0, hwinfoOkAt ?? 0);
    return applyStaleFloor({
      schema: 0,
      session: null,
      relayEpoch: null,
      generation: 0,
      seq: 0,
      collectedAt,
      heartbeatAt,
      hwinfoOkAt,
      top,
      fps,
      topActive: top !== null,
      fpsActive: fps !== null,
      hwDown: top?.hwDown === true,
      source: 'files',
    }, true);
  }

  function finishReadSource(copy: MonitorSnapshotView, receipt: MonitorReadSourceReceipt, display: boolean): MonitorSnapshotView | null | undefined {
    // Session replacement clears wall-clock stop floors. The read stamp must
    // still reject the retired publisher, including a switch inside callbacks.
    if (receipt.floorRevision < sessionFloorRevision) return null;
    if (receipt.floorRevision === stopFloorRevision) return display ? receipt.view : undefined;
    // A stop may happen after parsing/cloning but before the public read
    // continuation. Reuse the existing family-specific floor, never zeros.
    applyStaleFloor(copy);
    receipt.floorRevision = stopFloorRevision;
    if (!display) return undefined;
    if (readSourceFloors?.get(receipt.view) === stopFloorRevision) return receipt.view;
    if (!Object.isFrozen(receipt.view)) return applyStaleFloor(receipt.view, true);
    // Another reader may already have published/frozen this shared source.
    // Only this changed-floor race needs an additional detached display copy.
    const current = structuredClone(copy);
    readSourceFloors?.set(current, stopFloorRevision);
    receipt.view = current;
    return current;
  }

  // Parsed monitor payloads own and freeze their bounded arrays at admission.
  // Copy only the mutable snapshot shells for control readers; cloning 128 core
  // values and 120 frame-time samples every read adds avoidable work. The
  // defensive slice keeps the isolation contract if a future decoder supplies
  // an unfrozen array.
  function cloneReadResult(view: MonitorSnapshotView | null): MonitorSnapshotView | null {
    if (!view) return null;
    // Public/test hubs retain the historical fully mutable deep copy. Only the
    // production owner has the decoder-owned frozen arrays that can be shared.
    const copy: MonitorSnapshotView = !captureReadSource ? structuredClone(view) : {
      ...view,
      top: view.top ? {
        ...view.top,
        ...(view.top.coreUsage && !Object.isFrozen(view.top.coreUsage) ? { coreUsage: view.top.coreUsage.slice() } : {}),
        ...(view.top.coreLabels && !Object.isFrozen(view.top.coreLabels) ? { coreLabels: view.top.coreLabels.slice() } : {}),
      } : null,
      fps: view.fps ? {
        ...view.fps,
        ...(view.fps.frameTimes && !Object.isFrozen(view.fps.frameTimes) ? { frameTimes: view.fps.frameTimes.slice() } : {}),
      } : null,
    };
    if (captureReadSource) captureReadSource(copy, {
      view, floorRevision: readSourceFloors?.get(view) ?? -1, finish: finishReadSource,
    });
    return copy;
  }
  async function read(): Promise<MonitorSnapshotView | null> {
    if (inflight) return cloneReadResult(await inflight);
    // 199：先捕获"读取版本"再让 pump 可能同步发出命令——本读回只校准版本不晚于它的命令。
    const doneEpoch = intentEpoch;
    // 读取=仅按退避推进未决工作（不绕退避耗预算）；真正的新意图由 acquire/release 立即推进。
    void pump('poll');
    pendingReadCount += 1;
    inflight = (async () => {
      try {
        if (transport === 'files') return await readViaFiles();
        try {
          const view = await readViaSnapshot(doneEpoch);
          if (transport === 'unknown') transport = 'snapshot';
          return view;
        } catch (err) {
          if (transport === 'unknown' && isUnknownCommand(err)) {
            transport = 'files';
            return await readViaFiles();
          }
          // 已确认支持快照但超时/失效（或未知 schema）⇒ 返回不可用，不偷偷回读残留文件。
          return null;
        }
      } catch {
        return null;
      } finally {
        pendingReadCount -= 1;
        inflight = null;
      }
    })();
    return cloneReadResult(await inflight);
  }

  // 需求登记：同 owner 每次登记独立 token；旧 disposer 只释放自己的登记。
  function acquire(owner: string, need: { top?: boolean; fps?: boolean }): () => void {
    const token = nextToken++;
    const prev = demands.get(owner);
    const top = need.top === true;
    const fps = need.fps === true;
    const isNewIntent = !prev || prev.top !== top || prev.fps !== fps;
    demands.set(owner, { token, top, fps });
    if (isNewIntent) {
      // 新的需求意图：重置有界预算（重复同意图不重置，避免重启风暴）。
      startBudget = START_RETRY_BUDGET;
      stopBudget = STOP_RETRY_BUDGET;
      startRetryNextAt = 0;
      stopRetryNextAt = 0;
      // 未完成的接管声明随新意图恢复重试（已 done 则不重置）。
      if (takeoverState !== 'done') takeoverBudget = TAKEOVER_RETRY_BUDGET;
    }
    // Token replacement for identical demand must retain the retry deadline.
    void pump(isNewIntent ? 'intent' : 'poll');
    let released = false;
    return () => {
      if (released) return;
      released = true;
      const cur = demands.get(owner);
      if (!cur || cur.token !== token) return; // 旧 disposer：不误停新订阅
      demands.delete(owner);
      // 释放=新意图：若曾发出未确认的 start，也必须推进幂等 stop（可能已真实执行）。
      startBudget = START_RETRY_BUDGET;
      stopBudget = STOP_RETRY_BUDGET;
      startRetryNextAt = 0;
      stopRetryNextAt = 0;
      void pump('intent');
    };
  }

  function admitDisplayPublication(raw:unknown):MonitorSnapshotView|null|undefined {
    if(!raw||typeof raw!=='object'||Number((raw as Record<string,unknown>).schema)!==SNAPSHOT_SCHEMA)return undefined;
    const value=raw as Record<string,unknown>;
    // Authenticated native cache events reuse the exact lifecycle/payload
    // admission. They do not pump commands or acknowledge in-flight writes.
    if(value.available===false)return acceptLifecycle(value)==='stale'?undefined:null;
    const view=normalizeSnapshot(raw);return view?applyStaleFloor(view):undefined;
  }
  bindDisplayFloor?.({
    revision: () => stopFloorRevision,
    refresh: view => view ? applyStaleFloor(Object.isFrozen(view) ? structuredClone(view) : view) : null,
  });
  return {
    admitDisplayPublication,
    read,
    acquire,
    transport: () => transport,
    applied: () => ({ ...confirmed }),
    pending: () => ({ ...pending }),
    desired,
    session: () => expectedSession,
    takeover: () => takeoverState,
    attempts: () => ({ ...attempts }),
    pendingReads: () => pendingReadCount,
  };
}

// Only this production boundary can grant display-cache ownership. Hub.read()
// remains a mutable isolated control copy; no public marker accepts raw inputs.
const ownedDisplaySnapshots = new WeakSet<object>();
let lastDisplayKeyView: MonitorSnapshotView | null = null, lastDisplayKey = '';
let lastDisplayFreshView: MonitorSnapshotView | null = null, lastDisplayFreshAt = Number.NaN, lastDisplayFresh = false;
const looseFpsMetadata = ['fpsValidUntil','fpsPollPeriodMs','fps1Source','fps1ObservedAt','fps1ValidUntil','fps1PollPeriodMs'] as const;
function ownDisplaySnapshot(view: MonitorSnapshotView | null): MonitorSnapshotView | null {
  if (!view || ownedDisplaySnapshots.has(view)) return view;
  // Preserve unusual legacy metadata exactly, but never grant it memoization.
  const cacheable = !view.fps || !looseFpsMetadata.some(key => typeof view.fps![key] === 'object' && view.fps![key] !== null);
  if (view.top) {
    if (view.top.coreUsage) Object.freeze(view.top.coreUsage);
    if (view.top.coreLabels) Object.freeze(view.top.coreLabels);
    Object.freeze(view.top);
  }
  if (view.fps) { if (view.fps.frameTimes) Object.freeze(view.fps.frameTimes); Object.freeze(view.fps); }
  Object.freeze(view); if (cacheable) ownedDisplaySnapshots.add(view); return view;
}
function displaySnapshotKey(view: MonitorSnapshotView | null): string {
  if (!view || !ownedDisplaySnapshots.has(view)) return monitorSnapshotDisplayKey(view);
  if (view !== lastDisplayKeyView) { lastDisplayKey = monitorSnapshotDisplayKey(view); lastDisplayKeyView = view; }
  return lastDisplayKey;
}
function displaySnapshotHasFreshData(view: MonitorSnapshotView | null, now: number): boolean {
  if (!view || !ownedDisplaySnapshots.has(view)) return monitorSnapshotHasFreshData(view,now);
  if (view !== lastDisplayFreshView || now !== lastDisplayFreshAt) {
    lastDisplayFresh = monitorSnapshotHasFreshData(view,now); lastDisplayFreshView = view; lastDisplayFreshAt = now;
  }
  return lastDisplayFresh;
}

// Weak read receipts are consumed before returning the control copy to callers.
// Only the production owner can create them; native pushes need no receipts.
const readDisplaySources = new WeakMap<MonitorSnapshotView, MonitorReadSourceReceipt>();
let productionDisplayFloor: MonitorDisplayFloor | null = null;
// ── 生产单例（touch/autofloat 共用同一在途请求）──
const productionHub = createMonitorDataHubOwner({
  invoke: (cmd, args, options) => invoke(cmd, args, options),
  readTextFile: (path, maxBytes) => fs.readTextFile(path, maxBytes),
  now: () => Date.now(),
}, (copy, source) => { readDisplaySources.set(copy, source); }, floor => { productionDisplayFloor = floor; });

// Passive display subscribers share snapshots already read by TopMonitor/autofloat.
// A preview subscriber does not start a second polling loop or hardware collector.
const displayFeed = createLatestDisplayFeed<MonitorSnapshotView | null>(null, {
  now: () => performance.now(), key: displaySnapshotKey,
  unavailable: snapshot => !displaySnapshotHasFreshData(snapshot, Date.now()),
  setTimer: (fn, ms) => setTimeout(fn, ms), clearTimer: id => clearTimeout(id as ReturnType<typeof setTimeout>),
});
// Same admitted publisher cache, separate display budget for the curve. This
// subscription neither polls nor starts any independent data acquisition.
const hudDisplayFeed = createLatestDisplayFeed<MonitorSnapshotView | null>(null, {
  now: () => performance.now(), key: displaySnapshotKey, intervalMs: MONITOR_CURVE_DISPLAY_MS,
  unavailable: snapshot => !displaySnapshotHasFreshData(snapshot, Date.now()),
  setTimer: (fn, ms) => setTimeout(fn, ms), clearTimer: id => clearTimeout(id as ReturnType<typeof setTimeout>),
});
export function subscribeMonitorHudSnapshot(listener: (snapshot: MonitorSnapshotView | null) => void): () => void {
  return hudDisplayFeed.subscribe(listener);
}
export function subscribeMonitorSnapshot(listener: (snapshot: MonitorSnapshotView | null) => void): () => void {
  return displayFeed.subscribe(listener);
}
let publicationVersion=0;
function publishDisplaySnapshot(view: MonitorSnapshotView | null, version: number): void {
  if (!view) { lastDisplayKeyView = lastDisplayFreshView = null; lastDisplayKey = ''; lastDisplayFreshAt = Number.NaN; }
  const floor = productionDisplayFloor;
  let floorRevision = floor?.revision(), owned = ownDisplaySnapshot(view);
  displayFeed.push(owned);
  if (publicationVersion !== version) return;
  // Callbacks can stop a family after the first lane received this snapshot.
  // Recheck only the fixed revision; full filtering/copying is a rare stop edge.
  if (floor && floor.revision() !== floorRevision) {
    owned = ownDisplaySnapshot(floor.refresh(owned)); floorRevision = floor.revision();
    displayFeed.push(owned);
    if (publicationVersion !== version) return;
  }
  hudDisplayFeed.push(owned);
  if (publicationVersion !== version) return;
  if (floor && floor.revision() !== floorRevision) {
    owned = ownDisplaySnapshot(floor.refresh(owned));
    displayFeed.push(owned);
    if (publicationVersion === version) hudDisplayFeed.push(owned);
  }
}
export function acceptPublishedMonitorSnapshot(raw:unknown):void {
  const view=productionHub.admitDisplayPublication(raw);
  if(view!==undefined){++publicationVersion;publishDisplaySnapshot(view,publicationVersion);}
  displayFeed.advance();hudDisplayFeed.advance();advanceMonitorTdpDisplay();
}
export const readMonitorSnapshot = async (): Promise<MonitorSnapshotView | null> => {
  const version=publicationVersion;
  const snapshot = await productionHub.read();
  // Reuse this read's private source, never the caller's mutable copy or a
  // sequence-only cache match. Drop the receipt even if a newer push won.
  const receipt = snapshot ? readDisplaySources.get(snapshot) : undefined;
  if (snapshot) readDisplaySources.delete(snapshot);
  const display = version === publicationVersion;
  // Finish even if a newer publication won: the control copy must also respect
  // a stop issued during the final promise handoff. Receipt cleanup stays first.
  const source = snapshot && receipt ? receipt.finish(snapshot, receipt, display) : undefined;
  if (source === null) return null; // A newer session owns publication; never overwrite it here.
  if (snapshot && !receipt) productionDisplayFloor?.refresh(snapshot);
  if (display) publishDisplaySnapshot(snapshot ? source ?? structuredClone(snapshot) : null, version);
  if (snapshot && receipt && productionDisplayFloor && receipt.floorRevision !== productionDisplayFloor.revision()) {
    if (receipt.finish(snapshot, receipt, false) === null) return null;
  } else if (snapshot && !receipt) productionDisplayFloor?.refresh(snapshot);
  return snapshot; // Policy/control clients are not throttled by display publication.
};
export const acquireMonitorDemand = (owner: string, need: { top?: boolean; fps?: boolean }): (() => void) =>
  productionHub.acquire(owner, need);
export const monitorTransport = (): MonitorTransportMode => productionHub.transport();
/** Existing stop/session fence; observational only, without a read or demand. */
export const monitorSnapshotEpoch = (): number => productionDisplayFloor?.revision() ?? 0;
/** 已确认启动结果（autofloat 等据此区分"已登记"与"守护已启动"）。 */
export const monitorDemandConfirmed = (): MonitorFlagState => productionHub.applied();
/** takeover 状态（诊断：idle/pending/done）。 */
export const monitorTakeoverState = (): MonitorTakeoverState => productionHub.takeover();