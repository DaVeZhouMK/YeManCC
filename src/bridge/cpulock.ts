// cpulock.ts — 旧版 CPU 锁定配置兼容层
//
// CPU 调节已统一由 CPU 挡位 / CPU 浮动值负责。本文件仅保留旧 API，
// 不再被页面、App 或 autofloat 调用，旧 cpu_lock.json 不会覆盖当前 CPU 调节链路。

import { fs } from './api';
import { applyPowerParams, readPowerParams, setActiveScheme, PW, type PowerParams } from './yeman';

export const CPU_LOCK_FILE = 'C:\\SOFT\\YeMan\\PowerControl\\cpu_lock.json';

export type CpuLockSide = 'ac' | 'dc';

export interface CpuLockCfg {
  /** 旧版兼容字段；新代码请使用 acLocked / dcLocked。 */
  locked: boolean;
  acLocked: boolean;
  dcLocked: boolean;
  acFreq: number;
  dcFreq: number;
  acAggr: number;
  dcAggr: number;
  acTurbo: boolean;
  dcTurbo: boolean;
}

export interface CpuLockSideCfg {
  locked: boolean;
  freq: number;
  aggr: number;
  turbo: boolean;
}

export type CpuLockValues = Pick<CpuLockCfg, 'acFreq' | 'dcFreq' | 'acAggr' | 'dcAggr' | 'acTurbo' | 'dcTurbo'>;
export type CpuLockPatch = Partial<CpuLockValues>;
export type CpuLockSidePatch = Partial<Pick<CpuLockSideCfg, 'freq' | 'aggr' | 'turbo'>>;

const DEFAULT_LOCK: CpuLockCfg = {
  locked: false,
  acLocked: false,
  dcLocked: false,
  acFreq: 0,
  dcFreq: 0,
  acAggr: 100,
  dcAggr: 100,
  acTurbo: true,
  dcTurbo: true,
};

let cache: CpuLockCfg = { ...DEFAULT_LOCK };
let loaded = false;

const listeners = new Set<(c: CpuLockCfg) => void>();
function notify(): void {
  const snap = { ...cache };
  listeners.forEach((cb) => cb(snap));
}
export function onCpuLockUpdate(cb: (c: CpuLockCfg) => void): () => void {
  listeners.add(cb);
  return () => listeners.delete(cb);
}

function hasOwn(v: any, key: string): boolean {
  return !!v && Object.prototype.hasOwnProperty.call(v, key);
}

function sanitize(j: any): CpuLockCfg {
  const num = (v: any, d: number) => (typeof v === 'number' && isFinite(v) ? v : d);
  const bool = (v: any, d: boolean) => (typeof v === 'boolean' ? v : d);
  const legacyLocked = bool(j?.locked, false);
  const acLocked = bool(j?.acLocked, legacyLocked);
  const dcLocked = bool(j?.dcLocked, legacyLocked);
  return {
    // 旧字段按“存在任一锁定侧”表达，避免旧调用把独立锁误判成完全无锁。
    locked: acLocked || dcLocked,
    acLocked,
    dcLocked,
    acFreq: num(j?.acFreq, 0),
    dcFreq: num(j?.dcFreq, 0),
    acAggr: num(j?.acAggr, 100),
    dcAggr: num(j?.dcAggr, 100),
    acTurbo: bool(j?.acTurbo, true),
    dcTurbo: bool(j?.dcTurbo, true),
  };
}

function sideLocked(side: CpuLockSide): boolean {
  return side === 'ac' ? cache.acLocked : cache.dcLocked;
}

function sideCfg(side: CpuLockSide): CpuLockSideCfg {
  return side === 'ac'
    ? { locked: cache.acLocked, freq: cache.acFreq, aggr: cache.acAggr, turbo: cache.acTurbo }
    : { locked: cache.dcLocked, freq: cache.dcFreq, aggr: cache.dcAggr, turbo: cache.dcTurbo };
}

function setSideCfg(side: CpuLockSide, patch: CpuLockSidePatch, locked?: boolean): void {
  if (side === 'ac') {
    if (patch.freq !== undefined) cache.acFreq = patch.freq;
    if (patch.aggr !== undefined) cache.acAggr = patch.aggr;
    if (patch.turbo !== undefined) cache.acTurbo = patch.turbo;
    if (locked !== undefined) cache.acLocked = locked;
  } else {
    if (patch.freq !== undefined) cache.dcFreq = patch.freq;
    if (patch.aggr !== undefined) cache.dcAggr = patch.aggr;
    if (patch.turbo !== undefined) cache.dcTurbo = patch.turbo;
    if (locked !== undefined) cache.dcLocked = locked;
  }
  cache.locked = cache.acLocked || cache.dcLocked;
}

function sideValues(side: CpuLockSide): CpuLockSidePatch {
  return side === 'ac'
    ? { freq: cache.acFreq, aggr: cache.acAggr, turbo: cache.acTurbo }
    : { freq: cache.dcFreq, aggr: cache.dcAggr, turbo: cache.dcTurbo };
}

async function persist(): Promise<void> {
  cache.locked = cache.acLocked || cache.dcLocked;
  await fs.writeTextFileAtomic(CPU_LOCK_FILE, JSON.stringify(cache, null, 2));
}

// 同步读取缓存。无参数保持旧 API 语义：只要任一侧锁定即返回 true。
export function isCpuLocked(): boolean;
export function isCpuLocked(side: CpuLockSide): boolean;
export function isCpuLocked(side?: CpuLockSide): boolean {
  return side ? sideLocked(side) : cache.acLocked || cache.dcLocked;
}

export function getCpuLock(): CpuLockCfg;
export function getCpuLock(side: CpuLockSide): CpuLockSideCfg;
export function getCpuLock(side?: CpuLockSide): CpuLockCfg | CpuLockSideCfg {
  return side ? sideCfg(side) : { ...cache };
}

// 显式的按侧别名，便于新 UI/autofloat 调用方避免依赖重载。
export function isCpuSideLocked(side: CpuLockSide): boolean {
  return isCpuLocked(side);
}
export function getCpuLockSide(side: CpuLockSide): CpuLockSideCfg {
  return sideCfg(side);
}

// 从磁盘载入；旧 locked 字段会迁移成 acLocked/dcLocked。
export async function loadCpuLock(): Promise<CpuLockCfg> {
  let legacy = false;
  try {
    const txt = await fs.readTextFile(CPU_LOCK_FILE);
    const raw = JSON.parse(txt);
    legacy = !hasOwn(raw, 'acLocked') || !hasOwn(raw, 'dcLocked');
    cache = sanitize(raw);
  } catch {
    cache = { ...DEFAULT_LOCK };
  }
  loaded = true;
  if (legacy) await persist();
  notify();
  return { ...cache };
}

async function ensureLoaded(): Promise<void> {
  if (!loaded) await loadCpuLock();
}

function fullPatch(v: CpuLockPatch): CpuLockSidePatch {
  return { freq: v.acFreq, aggr: v.acAggr, turbo: v.acTurbo };
}

// 旧 API：传完整 AC/DC 值时同时锁定两侧。
export async function setCpuLock(v: CpuLockPatch, apply?: boolean): Promise<void>;
// 新 API：只设置并锁定指定侧。
export async function setCpuLock(side: CpuLockSide, v: CpuLockSidePatch, apply?: boolean): Promise<void>;
export async function setCpuLock(
  sideOrValues: CpuLockSide | CpuLockPatch,
  valuesOrApply: CpuLockSidePatch | boolean = {},
  apply = false,
): Promise<void> {
  await ensureLoaded();
  if (typeof sideOrValues === 'string') {
    setSideCfg(sideOrValues, valuesOrApply as CpuLockSidePatch, true);
  } else {
    const v = sideOrValues;
    setSideCfg('ac', fullPatch(v), true);
    setSideCfg('dc', { freq: v.dcFreq, aggr: v.dcAggr, turbo: v.dcTurbo }, true);
  }
  await persist();
  notify();
  if (typeof valuesOrApply === 'boolean' ? valuesOrApply : apply) await applyCpuLock().catch(() => {});
}

// 新 API 的语义化名称。
export async function lockCpuSide(side: CpuLockSide, values?: CpuLockSidePatch, apply = false): Promise<void> {
  await ensureLoaded();
  if (!values) {
    const current = await readPowerParams();
    values = current
      ? side === 'ac'
        ? { freq: current.acFreq, aggr: current.acAggr, turbo: current.acTurbo }
        : { freq: current.dcFreq, aggr: current.dcAggr, turbo: current.dcTurbo }
      : sideValues(side);
  }
  setSideCfg(side, values, true);
  await persist();
  notify();
  if (apply) await applyCpuLock().catch(() => {});
}

// 锁定态下更新指定侧的锁定值；未锁定侧不会被写回。
export async function updateCpuLock(v: CpuLockPatch): Promise<void>;
export async function updateCpuLock(side: CpuLockSide, v: CpuLockSidePatch): Promise<void>;
export async function updateCpuLock(sideOrValues: CpuLockSide | CpuLockPatch, values?: CpuLockSidePatch): Promise<void> {
  await ensureLoaded();
  if (typeof sideOrValues === 'string') {
    if (!sideLocked(sideOrValues)) return;
    setSideCfg(sideOrValues, values ?? {});
  } else {
    if (cache.acLocked) setSideCfg('ac', fullPatch(sideOrValues));
    if (cache.dcLocked) setSideCfg('dc', { freq: sideOrValues.dcFreq, aggr: sideOrValues.dcAggr, turbo: sideOrValues.dcTurbo });
    if (!cache.acLocked && !cache.dcLocked) return;
  }
  await persist();
  notify();
}

export async function updateCpuLockSide(side: CpuLockSide, values: CpuLockSidePatch): Promise<void> {
  return updateCpuLock(side, values);
}

// 解锁指定侧；无参数保持旧 API，解锁两侧但保留数值。
export async function clearCpuLock(): Promise<void>;
export async function clearCpuLock(side: CpuLockSide): Promise<void>;
export async function clearCpuLock(side?: CpuLockSide): Promise<void> {
  await ensureLoaded();
  if (side) setSideCfg(side, {}, false);
  else {
    cache.acLocked = false;
    cache.dcLocked = false;
    cache.locked = false;
  }
  await persist();
  notify();
}

export async function unlockCpuSide(side: CpuLockSide): Promise<void> {
  return clearCpuLock(side);
}

function toPowerParams(values: CpuLockValues): PowerParams {
  return {
    acFreq: values.acFreq,
    dcFreq: values.dcFreq,
    acTurbo: values.acTurbo,
    dcTurbo: values.dcTurbo,
    acAggr: values.acAggr,
    dcAggr: values.dcAggr,
  };
}

// 应用锁定侧。仅把锁定侧的缓存值合并进当前电源方案，并只写入锁定侧；
// 未锁定侧不使用锁定 JSON 中的旧值，也不向 powercfg 回写。
export async function applyCpuLock(): Promise<boolean> {
  await ensureLoaded();
  const sides: CpuLockSide[] = [];
  if (cache.acLocked) sides.push('ac');
  if (cache.dcLocked) sides.push('dc');
  if (sides.length === 0) return false;

  let merged: PowerParams;
  if (sides.length === 2) {
    merged = toPowerParams(cache);
  } else {
    const current = await readPowerParams();
    if (!current) return false;
    merged = { ...current };
    if (cache.acLocked) {
      merged.acFreq = cache.acFreq;
      merged.acAggr = cache.acAggr;
      merged.acTurbo = cache.acTurbo;
    }
    if (cache.dcLocked) {
      merged.dcFreq = cache.dcFreq;
      merged.dcAggr = cache.dcAggr;
      merged.dcTurbo = cache.dcTurbo;
    }
  }

  try {
    await applyPowerParams({ ...merged, sides });
    await setActiveScheme(PW.YEMAN).catch(() => {});
    return true;
  } catch {
    return false;
  }
}
