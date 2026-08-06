// topmon.ts — 顶部监控条桥层
//
// 数据源：PowerControl\TopMonitor.ps1 常驻守护（独立于 FPS-Monitor.ps1，
// 无论有无游戏都每 2 秒写 topmon.json），本桥负责拉起/停止守护 + 轮询解析。
//
// topmon.json 字段（守护侧照搬 FPS-Monitor.ps1 的 HWiNFO 共享内存读取范式）：
//   ts        = 写入时间戳（<6s 视为新鲜，新鲜度即守护存活判据）
//   tdpW      = CPU Package Power (W)
//   freqMhz   = CPU 当前主频（多核 Core Clock 取最大值, MHz）
//   tempC     = CPU (Tctl/Tdie) 温度（无则回退 CPU Package, °C）
//   ac        = ACLineStatus (1=AC 0=DC)
//   chargeW   = Battery Charge Rate（正=充电 负=放电, W；无传感器=0）
//   remainMin = 放电剩余时间（Win32_Battery.EstimatedRunTime 分钟；充电/无电池=-1）
//   hwDown    = HWiNFO 共享内存不可用（守护已尝试自动修复仍失败）
import { shell, fs } from './api';

const PC_DIR = 'C:\\SOFT\\YeMan\\PowerControl';
const TOPMON_PS1 = PC_DIR + '\\TopMonitor.ps1';
const TOPMON_JSON = PC_DIR + '\\topmon.json';
const TOPMON_STOP = PC_DIR + '\\topmon.stop';

export interface TopMonData {
  ts: number;
  tdpW: number; // CPU Package Power W
  freqMhz: number; // CPU 当前主频 MHz
  tempC: number; // CPU (Tctl/Tdie) °C
  ac: number; // 1=AC 0=DC
  hasBattery: boolean; // true=电池设备；false=台式机
  chargeW: number; // Charge Rate W（正=充电 负=放电）
  remainMin: number; // HWiNFO Estimated Remaining Time 分钟；无数据=-1
  cpuUsage: number; // 系统 CPU 总占用 %（Total CPU Usage）
  gpuPowerW: number; // 显卡瓦数 W（多 GPU 取功耗最高者）
  gpuClockMhz: number; // 显卡主频 MHz（多 GPU 取频率最高者）
  hwDown: boolean; // HWiNFO 共享内存不可用
}

// 拉起守护（隐藏窗口，无 VBS 中转，照搬 autofloat startMonitor）
export async function startTopMonitor(): Promise<void> {
  try {
    await fs.remove(TOPMON_STOP).catch(() => {}); // 清旧停止标志，避免刚启动就被自己停掉
    await shell.hidden('powershell.exe', [
      '-NoProfile', '-ExecutionPolicy', 'Bypass',
      '-WindowStyle', 'Hidden', '-File', TOPMON_PS1,
    ]);
  } catch {
    /* 拉起失败由轮询侧兜底显示 -- */
  }
}

// 停止守护（写停止标志，守护 1~3 秒内自清理退出；照搬 autofloat stopMonitor）
export async function stopTopMonitor(): Promise<void> {
  try {
    await fs.writeTextFile(TOPMON_STOP, '1');
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
      chargeW: Number(raw.chargeW) || 0,
      remainMin: Number.isFinite(Number(raw.remainMin)) ? Number(raw.remainMin) : -1,
      cpuUsage: Number(raw.cpuUsage) || 0,
      gpuPowerW: Number(raw.gpuPowerW) || 0,
      gpuClockMhz: Number(raw.gpuClockMhz) || 0,
      hwDown: !!raw.hwDown,
    };
  } catch {
    return null; // 无文件 / JSON 损坏 / 读取失败
  }
}
