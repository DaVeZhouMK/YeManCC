// topmon.ts — 顶部监控条桥层
//
// 数据源：PowerControl\TopMonitor.ps1 常驻守护（独立于 FPS-Monitor.ps1，
// 无论有无游戏都每 1 秒写 topmon.json），本桥负责拉起/停止守护 + 轮询解析。
//
// topmon.json 字段（守护侧照搬 FPS-Monitor.ps1 的 HWiNFO 共享内存读取范式）：
//   ts        = 写入时间戳（<6s 视为新鲜，新鲜度即守护存活判据）
//   tdpW      = CPU Package Power (W)
//   freqMhz   = CPU 当前主频（多核 Core Clock 取最大值, MHz）
//   tempC     = CPU (Tctl/Tdie) 温度（无则回退 CPU Package, °C）
//   ac        = ACLineStatus (1=AC 0=DC)
//   chargeW   = Battery Charge Rate（正=充电 负=放电, W；无传感器=0）
//   remainMin = 放电剩余时间（Win32_Battery.EstimatedRunTime 分钟；充电/无电池=-1）
//   thermalThrottle* = Core Thermal Throttling / Thermal Throttling (HTC) 融合结果
//   virtualMemory*   = Virtual Memory Committed (MB) / Virtual Memory Load (%)
//   hwDown    = HWiNFO 共享内存不可用（守护已尝试自动修复仍失败）
import { ref } from 'vue';
import { fs } from './api';
import { invoke } from './ipc';

const PC_DIR = 'C:\\SOFT\\YeMan\\PowerControl';
const TOPMON_JSON = PC_DIR + '\\topmon.json';

export interface TopMonData {
  ts: number;
  tdpW: number; // CPU Package Power W
  freqMhz: number; // CPU 当前主频 MHz
  tempC: number; // CPU (Tctl/Tdie) °C
  ac: number; // 1=AC 0=DC
  hasBattery: boolean; // true=电池设备；false=台式机
  batteryPercent: number; // 电池百分比；无电池/未知=-1
  chargeW: number; // Charge Rate W（正=充电 负=放电）
  remainMin: number; // HWiNFO Estimated Remaining Time 分钟；无数据=-1
  cpuUsage: number; // 系统 CPU 总占用 %（Total CPU Usage）
  gpuPowerW: number; // 显卡瓦数 W（多 GPU 取功耗最高者）
  gpuClockMhz: number; // 显卡主频 MHz（多 GPU 取频率最高者）
  thermalThrottleFound: boolean; // 两种过热降频传感器至少存在一个
  thermalThrottleMax: number; // HWiNFO Yes/No 最大值（0=否，1=是）
  thermalThrottleAvgPct: number; // 历史平均触发占比 %
  virtualMemoryCommittedFound: boolean; // Virtual Memory Committed 可读
  virtualMemoryCommittedMb: number; // 已提交虚拟内存 MB
  virtualMemoryLoadFound: boolean; // Virtual Memory Load 可读
  virtualMemoryLoadPct: number; // 虚拟内存使用率 %
  hwDown: boolean; // HWiNFO 共享内存不可用
}

// 拉起守护（隐藏窗口，无 VBS 中转，照搬 autofloat startMonitor）
// The monitor bar is the single low-frequency reader. Share its latest
// snapshot with app policies instead of starting another hardware poll.
export const topMonitorData = ref<TopMonData | null>(null);

export function setTopMonitorData(data: TopMonData | null): void {
  topMonitorData.value = data;
}

function clampPercent(value: unknown): number {
  const parsed = Number(value);
  return Number.isFinite(parsed) ? Math.min(100, Math.max(0, parsed)) : 0;
}

export async function startTopMonitor(): Promise<void> {
  try {
    await invoke('monitor.start', { top: true });
  } catch {
    /* 拉起失败由轮询侧兜底显示 -- */
  }
}

// 停止守护（写停止标志，守护 1~3 秒内自清理退出；照搬 autofloat stopMonitor）
export async function stopTopMonitor(): Promise<void> {
  try {
    await invoke('monitor.stop', { top: true });
  } catch {
    /* 忽略 */
  }
}

// 读取最新状态：json 存在且 ts 新鲜（<6s）→ 返回数据；否则 null（守护未就绪/已死）
export async function readTopMonitor(): Promise<TopMonData | null> {
  try {
    const txt = await fs.readTextFile(TOPMON_JSON, 8192);
    const raw = JSON.parse(txt) as Partial<TopMonData>;
    const ts = Number(raw.ts) || 0;
    if (!ts || Date.now() - ts > 6000) return null; // 过期视为无数据
    return {
      ts,
      tdpW: Number(raw.tdpW) || 0,
      freqMhz: Number(raw.freqMhz) || 0,
      tempC: Number(raw.tempC) || 0,
      ac: Number(raw.ac) === 0 ? 0 : 1,
      hasBattery: raw.hasBattery === true,
      batteryPercent: Number.isFinite(Number(raw.batteryPercent)) ? Number(raw.batteryPercent) : -1,
      chargeW: Number(raw.chargeW) || 0,
      remainMin: Number.isFinite(Number(raw.remainMin)) ? Number(raw.remainMin) : -1,
      cpuUsage: Number(raw.cpuUsage) || 0,
      gpuPowerW: Number(raw.gpuPowerW) || 0,
      gpuClockMhz: Number(raw.gpuClockMhz) || 0,
      thermalThrottleFound: raw.thermalThrottleFound === true,
      thermalThrottleMax: Number(raw.thermalThrottleMax) || 0,
      thermalThrottleAvgPct: clampPercent(raw.thermalThrottleAvgPct),
      virtualMemoryCommittedFound: raw.virtualMemoryCommittedFound === true,
      virtualMemoryCommittedMb: Number(raw.virtualMemoryCommittedMb) || 0,
      virtualMemoryLoadFound: raw.virtualMemoryLoadFound === true,
      virtualMemoryLoadPct: clampPercent(raw.virtualMemoryLoadPct),
      hwDown: !!raw.hwDown,
    };
  } catch {
    return null; // 无文件 / JSON 损坏 / 读取失败
  }
}
