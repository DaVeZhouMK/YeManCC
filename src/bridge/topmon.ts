// topmon.ts — 顶部监控条桥层
//
// 196：数据源改为**统一监控数据入口**（monitorData.ts）——新 native 走内存快照
// （monitor.snapshot），旧 native 自动回退原 TopMonitor 四文件通路，字段语义不变。
// 本桥只保留：共享 ref 所有权、6s 新鲜度门、展示需求登记（monitor.start/stop 合并）。
import { ref, watch } from 'vue';
import { powerSourceMode } from './powerSource';
import { acquireMonitorDemand, readMonitorSnapshot } from './monitorData';

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
export async function startTopMonitor(): Promise<void> {
  topbarMounts += 1;
  if (topbarMounts === 1) {
    topbarDemandRelease = acquireMonitorDemand('topbar', { top: true });
  }
}

// 释放展示需求（全端释放后 native 才停对应标志）
export async function stopTopMonitor(): Promise<void> {
  topbarMounts = Math.max(0, topbarMounts - 1);
  if (topbarMounts === 0 && topbarDemandRelease) {
    topbarDemandRelease();
    topbarDemandRelease = null;
  }
}

// 读取最新状态：统一入口取数；ts 新鲜（<6s）→ 返回数据；否则 null（未就绪/已停）。
export async function readTopMonitor(): Promise<TopMonData | null> {
  const view = await readMonitorSnapshot();
  const top = view?.top ?? null;
  if (!top) return null;
  if (!top.ts || Date.now() - top.ts > 6000) return null; // 过期视为无数据
  return top;
}
