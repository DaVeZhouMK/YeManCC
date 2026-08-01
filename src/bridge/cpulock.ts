// cpulock.ts — CPU 主频/积极性「锁定」桥层
//
// 需求：AC/DC CPU最大主频 卡片右侧的锁图标。锁定后：
//   ① 把 AC/DC 的「最大主频」和「调度积极性」（含睿频状态）写入
//      C:\SOFT\YeMan\PowerControl\cpu_lock.json（持久化，跨重启保留）
//   ② 关闭自动CPU浮动优化 时自动套用锁定值（而不是恢复快照）
//   ③ 拔/插电源（AC/DC 切换）且浮动优化未开启时自动套用锁定值
//
// 【优先级铁律】自动CPU浮动优化开启 = 最高优先：锁定不影响浮动，浮动照常联动
// 改写 AC/DC 主频/积极性，滑块跟随浮动。锁定值只在浮动关闭时生效。
//
// 设计原则：配置真相源是 JSON（不受权限影响、持久化），写 JSON 永远先行；
// 套用（powercfg 写入 + 重新激活方案）失败也不影响锁定状态本身。

import { fs } from './api';
import { applyPowerParams, setActiveScheme, PW } from './yeman';

export const CPU_LOCK_FILE = 'C:\\SOFT\\YeMan\\PowerControl\\cpu_lock.json';

export interface CpuLockCfg {
  locked: boolean;
  acFreq: number;
  dcFreq: number;
  acAggr: number;
  dcAggr: number;
  acTurbo: boolean;
  dcTurbo: boolean;
}

const DEFAULT_LOCK: CpuLockCfg = {
  locked: false,
  acFreq: 0,
  dcFreq: 0,
  acAggr: 100,
  dcAggr: 100,
  acTurbo: true,
  dcTurbo: true,
};

// 模块级缓存：autofloat 每秒 tick 需要同步查询是否锁定，不能每次都读盘
let cache: CpuLockCfg = { ...DEFAULT_LOCK };
let loaded = false;

const listeners = new Set<(c: CpuLockCfg) => void>();
function notify() {
  const snap = { ...cache };
  listeners.forEach((cb) => cb(snap));
}
export function onCpuLockUpdate(cb: (c: CpuLockCfg) => void): () => void {
  listeners.add(cb);
  return () => listeners.delete(cb);
}

function sanitize(j: any): CpuLockCfg {
  const num = (v: any, d: number) => (typeof v === 'number' && isFinite(v) ? v : d);
  const bool = (v: any, d: boolean) => (typeof v === 'boolean' ? v : d);
  return {
    locked: bool(j?.locked, false),
    acFreq: num(j?.acFreq, 0),
    dcFreq: num(j?.dcFreq, 0),
    acAggr: num(j?.acAggr, 100),
    dcAggr: num(j?.dcAggr, 100),
    acTurbo: bool(j?.acTurbo, true),
    dcTurbo: bool(j?.dcTurbo, true),
  };
}

// 同步读取缓存（autofloat tick / UI 判定用）
export function isCpuLocked(): boolean {
  return cache.locked;
}
export function getCpuLock(): CpuLockCfg {
  return { ...cache };
}

// 从磁盘载入（App / CpuView 挂载时调用一次）
export async function loadCpuLock(): Promise<CpuLockCfg> {
  try {
    const txt = await fs.readTextFile(CPU_LOCK_FILE);
    cache = sanitize(JSON.parse(txt));
  } catch {
    cache = { ...DEFAULT_LOCK };
  }
  loaded = true;
  notify();
  return { ...cache };
}

// 锁定：把「当前（非自动优化）」的四个值（+睿频）写入 JSON。
// apply=true 时立即套用一次（调用方约定：仅在自动优化关闭状态下传 true；
// 自动优化开启中锁定只写配置，浮动继续优先接管，等浮动关闭时再套用锁定值）。
export async function setCpuLock(v: Omit<CpuLockCfg, 'locked'>, apply: boolean): Promise<void> {
  cache = { ...sanitize(v), locked: true };
  loaded = true;
  try {
    await fs.writeTextFile(CPU_LOCK_FILE, JSON.stringify(cache, null, 2));
  } catch { /* 写盘失败不阻塞：内存态仍生效 */ }
  notify();
  if (apply) await applyCpuLock().catch(() => {});
}

// 锁定态下手动改滑块：同步更新锁定值（不重复套用，滑块 commit 自己会写）
export async function updateCpuLock(v: Omit<CpuLockCfg, 'locked'>): Promise<void> {
  if (!cache.locked) return;
  cache = { ...sanitize(v), locked: true };
  try {
    await fs.writeTextFile(CPU_LOCK_FILE, JSON.stringify(cache, null, 2));
  } catch { /* 忽略 */ }
  notify();
}

// 解锁：只翻 locked 位，保留数值方便下次一键锁回
export async function clearCpuLock(): Promise<void> {
  cache = { ...cache, locked: false };
  try {
    await fs.writeTextFile(CPU_LOCK_FILE, JSON.stringify(cache, null, 2));
  } catch { /* 忽略 */ }
  notify();
}

// 套用锁定值到电源方案（写入 + 重新激活野蛮方案使其真正生效）
export async function applyCpuLock(): Promise<boolean> {
  if (!loaded) await loadCpuLock();
  if (!cache.locked) return false;
  try {
    await applyPowerParams({
      acFreq: cache.acFreq,
      dcFreq: cache.dcFreq,
      acTurbo: cache.acTurbo,
      dcTurbo: cache.dcTurbo,
      acAggr: cache.acAggr,
      dcAggr: cache.dcAggr,
    });
    await setActiveScheme(PW.YEMAN).catch(() => {});
    return true;
  } catch {
    return false;
  }
}
