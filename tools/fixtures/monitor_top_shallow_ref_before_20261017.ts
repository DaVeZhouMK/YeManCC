// topmon.ts — 顶部监控条桥层
//
// 196：数据源改为**统一监控数据入口**（monitorData.ts）——新 native 走内存快照
// （monitor.snapshot），旧 native 自动回退原 TopMonitor 四文件通路，字段语义不变。
// 本桥只保留：共享 ref 所有权、6s 新鲜度门、展示需求登记（monitor.start/stop 合并）。
import { ref, watch } from 'vue';
import { powerSourceMode } from './powerSource';
import { acquireMonitorDemand, readMonitorSnapshot } from './monitorData';
import { projectTopMonitorView } from './monitorTopProjection';
import { isUiVisible, onUiVisibilityChange } from './uiLifecycle';

export interface TopMonData {
  // Optional fields owned by the sole YMCC data publisher, never a HUD reader.
  coreUsage?: Array<number | null>; // Actual logical/core items, not a fixed 16 slots.
  coreLabels?: string[]; // Publisher identities; positions are kept even if values are null.
  coreUsageTs?: number; // Own collection time if independent of HWiNFO health.
  // Observed RTSS Profiles/Global configuration, not proof of hook effectiveness.
  fpsLimit?: number | null;
  fpsLimitValid?: boolean;
  fpsLimitTs?: number;
  fanEnabled?: boolean; // Actual YMCC active-control receipt, never saved toggle.
  fanSpeedPercent?: number | null; // Successful current target, NOT physical RPM.
  fanSpeedValid?: boolean;
  fanSpeedTs?: number;
  fanSpeedSource?: 'ymcc-fan-target' | 'unavailable';
  fpsLimitSource?: 'rtss-global' | 'unavailable';
  ts: number;
  cpuPowerValid?: boolean; // Explicit producer proof; missing sensors are not measured zero.
  cpuClockValid?: boolean;
  cpuTempValid?: boolean;
  tdpW: number; // CPU Package Power W
  freqMhz: number; // CPU 当前主频 MHz
  tempC: number; // CPU (Tctl/Tdie) °C
  ac: number; // 1=AC 0=DC
  hasBattery: boolean; // true=电池设备；false=台式机
  batteryPercent: number; // 电池百分比；无电池/未知=-1
  chargeW: number; // Charge Rate W（正=充电 负=放电）
  batteryPowerValid?: boolean; // Explicit sole-publisher validity; unknown is not idle 0 W.
  batteryPowerEstimated?: boolean; // Capacity average or package-power model, NOT instantaneous sensor power.
  batteryPowerTs?: number; // Own Windows/native freshness, independent of HWiNFO.
  batteryStatusTs?: number; // Confirmed Windows battery percentage timestamp.
  batteryPowerSource?: 'hwinfo' | 'windows-rate' | 'energy-delta' | 'package-power-model' | 'unavailable';
  batteryPowerWindowMs?: number;
  batteryTimeValid?: boolean;
  batteryTimeEstimated?: boolean; // Includes forecasts computed from a real discharge rate.
  batteryTimeTs?: number;
  batteryTimeSource?: 'energy-power' | 'sensor-estimate' | 'unavailable';
  remainMin: number; // HWiNFO Estimated Remaining Time 分钟；无数据=-1
  cpuUsage: number; // 系统 CPU 总占用 %（Total CPU Usage）
  cpuUsageValid?: boolean; // Windows delta proof; warm-up/failure is not measured idle.
  cpuUsageTs?: number; // Sole sampler time, independent of HWiNFO.
  gpuPowerW: number | null; // YMCC 本会话已选 GPU 的实时功耗；无有效读数=null
  gpuClockMhz: number | null; // 与功耗同一卡、同一组的实时频率，不跨卡取最大值
  // Optional extensions from the existing sole publisher. Missing/null = unknown,
  // never read sensors here. Memory values are MiB, not virtual committed memory.
  cpuVoltageV?: number | null; // Measured HWiNFO core rail; never VID/requested voltage.
  gpuVoltageV?: number | null;
  gpuTempC?: number | null;
  gpuUsagePct?: number | null;
  gpuName?: string | null;
  gpuSelectionPinned?: boolean;
  gpuSelectionPresent?: boolean;
  gpuMemorySource?: 'hwinfo' | 'hwinfo-d3d-dedicated' | 'hwinfo-d3d-shared' | 'unavailable';
  gpuMemoryUsedMb?: number | null;
  gpuMemoryTotalMb?: number | null;
  physicalMemoryUsedMb?: number | null;
  physicalMemoryTotalMb?: number | null;
  physicalMemoryAvailableMb?: number | null;
  physicalMemoryLoadPct?: number | null;
  gpuMemoryAvailableMb?: number | null;
  gpuMemoryLoadPct?: number | null;
  networkDownMBps?: number | null;
  networkUpMBps?: number | null;
  fileReadMBps?: number | null;
  fileWriteMBps?: number | null;
  fileSystemTs?: number;
  fileSystemSource?: 'windows-filesystem-disk-activity' | 'unavailable';
  thermalThrottleFound: boolean; // 两种过热降频传感器至少存在一个
  thermalThrottleMax: number; // HWiNFO Yes/No 最大值（0=否，1=是）
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
  const mode = powerSourceMode.value;
  // Old HWiNFO/file snapshots may still carry pre-sleep AC/DC. Only that
  // display field is projected from the foreground/native source snapshot.
  topMonitorData.value = data && mode ? { ...data, ac: mode === 'ac' ? 1 : 0 } : data;
}

watch(powerSourceMode, () => {
  if (topMonitorData.value) setTopMonitorData(topMonitorData.value);
}, { flush: 'sync' });

// 展示需求登记（196/197）：按挂载计数；重叠挂载时旧页面卸载不得取消新页面的订阅。
let topbarMounts = 0;
let topbarDemandRelease: (() => void) | null = null;
let topbarVisibilityRelease: (() => void) | null = null;

function reconcileTopMonitorDemand(): void {
  // Pausing the UI reader alone leaves the sole native collector doing top
  // work. Release only this display owner; HUD/preview/policy owners stay live.
  if (topbarMounts > 0 && isUiVisible()) {
    if (!topbarDemandRelease) topbarDemandRelease = acquireMonitorDemand('topbar', { top: true });
  } else if (topbarDemandRelease) {
    const release = topbarDemandRelease;
    topbarDemandRelease = null;
    release();
  }
}

export async function startTopMonitor(): Promise<void> {
  topbarMounts += 1;
  if (topbarMounts === 1) {
    // The existing visibility source calls back immediately, including when a
    // component is first mounted while hidden or power is not ready.
    topbarVisibilityRelease = onUiVisibilityChange(reconcileTopMonitorDemand);
    reconcileTopMonitorDemand(); // Also supports an inert/non-window caller.
  }
}

// 释放展示需求（全端释放后 native 才停对应标志）
export async function stopTopMonitor(): Promise<void> {
  topbarMounts = Math.max(0, topbarMounts - 1);
  if (topbarMounts !== 0) return;
  topbarVisibilityRelease?.();
  topbarVisibilityRelease = null;
  reconcileTopMonitorDemand();
}

// 读取最新状态：统一入口取数；ts 新鲜（<6s）→ 返回数据；否则 null（未就绪/已停）。
export async function readTopMonitor(): Promise<TopMonData | null> {
  return projectTopMonitorView(await readMonitorSnapshot(), Date.now());
}
