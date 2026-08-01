// cpuctl.ts — CCD 核心控制 + CPU 降压（Undervolt）桥层
//
// 设计（来自 docs/UNDERVOLT-PLAN.md）：
//  - 打开页面自动验证支持；不支持则整块隐藏（不置灰）。
//  - CCD 走 native cpu.topology / cpu.setCcdMode；降压走 YeManTdpCtl uv 命令族。

import { shell } from './api';
import { invoke } from './ipc';

const TDPCTL_EXE = 'C:\\SOFT\\YeMan\\PowerControl\\pawnio\\YeManTdpCtl.exe';

// ── 支持探测 ──
export interface CcdProbe {
  supported: boolean;
  l3Domains: number;
  ccdMasks: string[];
  physicalCores: number;
  logical: number;
}
export interface UvProbe {
  supported: boolean;
  vendor: 'amd' | 'intel' | '';
  current: number;
  reason?: string;
}

export async function probeCcd(): Promise<CcdProbe> {
  try {
    const r = await invoke<any>('cpu.topology', {});
    const l3 = Number(r.l3Domains ?? 0);
    return {
      supported: l3 >= 2,
      l3Domains: l3,
      ccdMasks: Array.isArray(r.ccdMasks) ? r.ccdMasks : [],
      physicalCores: Number(r.physicalCores ?? 0),
      logical: Number(r.logical ?? 0),
    };
  } catch {
    return { supported: false, l3Domains: 0, ccdMasks: [], physicalCores: 0, logical: 0 };
  }
}

export async function probeUndervolt(): Promise<UvProbe> {
  try {
    const r = await shell.run(TDPCTL_EXE, ['uv', 'probe'], 30000);
    if (r.exitCode !== 0) {
      return { supported: false, vendor: '', current: 0, reason: 'probe_failed' };
    }
    // 找到最后一行 JSON（前面可能有日志）
    const line = r.stdout.trim().split('\n').pop() || '{}';
    const j = JSON.parse(line);
    const vendor = j.vendor === 'intel' ? 'intel' : j.vendor === 'amd' ? 'amd' : '';
    return {
      supported: !!j.supported,
      vendor,
      current: Number(j.current ?? 0),
      reason: j.reason,
    };
  } catch {
    return { supported: false, vendor: '', current: 0 };
  }
}

// ── CCD 核心控制（层 1：全局亲和，需 native cpu.setCcdMode） ──
// 0=全核；1..N=仅第 N-1 个 CCD。N 由 native 拓扑动态决定。
export type CcdMode = number;
export async function setCcdMode(mode: CcdMode): Promise<{ ok: boolean; msg: string }> {
  try {
    if (!Number.isInteger(mode) || mode < 0) {
      return { ok: false, msg: 'CCD 模式参数无效' };
    }
    await invoke('cpu.setCcdMode', { mode });
    return { ok: true, msg: '' };
  } catch (e: any) {
    return { ok: false, msg: e?.message ?? 'native 未编译（需重编译壳）' };
  }
}

// ── CPU 降压（Undervolt，走 YeManTdpCtl uv 命令族） ──
export const UV_PRESETS = [
  { key: 'off', label: '关闭', amd: 0, intel: 0 },
  { key: 'safe', label: '安全', amd: -8, intel: -25 },
  { key: 'balance', label: '平衡', amd: -14, intel: -45 },
  { key: 'risk', label: '风险', amd: -24, intel: -75 },
];
export function uvPresetValue(vendor: 'amd' | 'intel' | '', key: string): number {
  const p = UV_PRESETS.find((x) => x.key === key);
  if (!p) return 0;
  return vendor === 'intel' ? p.intel : p.amd;
}

export async function setUndervolt(v: number): Promise<{ ok: boolean; msg: string }> {
  const r = await shell
    .run(TDPCTL_EXE, ['uv', 'set', String(v)], 20000)
    .catch(() => ({ exitCode: 1, stdout: '', stderr: '' }));
  const text = `${r.stderr || ''}\n${r.stdout || ''}`.trim();
  return {
    ok: r.exitCode === 0,
    msg: r.exitCode === 0 ? '' : text || `uv 执行失败（退出码 ${r.exitCode}）`,
  };
}
