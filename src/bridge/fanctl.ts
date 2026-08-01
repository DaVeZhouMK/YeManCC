// fanctl.ts — 风扇控制桥层 (通用 + 专用)
//
// 通用 (台式机): 经 FanControl 的 WebServer 插件 (HTTP/JSON) 控制主板风扇。
// 专用 (GPD Win5): HandheldCompanion GPDWin5.cs 寄存器映射 (CC BY-NC-SA 4.0),
//   走 YeManTdpCtl fan 命令族 (PawnIO LpcIO 模块做 EC 端口 I/O)。
// 两者共用同一线性 温度->占空比 模型; 不支持的机器由后端 detect 给出 available=false。

import { shell } from './api';

const TDPCTL_EXE = 'C:\\SOFT\\YeMan\\PowerControl\\pawnio\\YeManTdpCtl.exe';

export type FanMode = 'dedicated' | 'generic';

export interface FanProbe {
  mode: FanMode;
  supported: boolean;
  available: boolean;
  isGPDWin5: boolean;
  rpm: number;
  hint?: string;
  url?: string;
  fans?: { id: string; name: string }[];
  sensors?: { id: string; name: string; type: string }[];
  tempSensorId?: string;
  fanId?: string;
}

function parseJson(stdout: string): any {
  try {
    const line = stdout.trim().split('\n').pop() || '{}';
    return JSON.parse(line);
  } catch {
    return {};
  }
}

export async function probeFan(): Promise<FanProbe> {
  try {
    const r = await shell.run(TDPCTL_EXE, ['fan', 'detect'], 15000);
    if (r.exitCode !== 0) return { mode: 'generic', supported: false, available: false, isGPDWin5: false, rpm: 0 };
    const j = parseJson(r.stdout);
    return {
      mode: j.mode === 'dedicated' ? 'dedicated' : 'generic',
      supported: !!j.supported,
      available: !!j.available,
      isGPDWin5: !!j.isGPDWin5,
      rpm: Number(j.rpm ?? 0),
      hint: j.hint,
      url: j.url,
      fans: j.fans,
      sensors: j.sensors,
      tempSensorId: j.tempSensorId,
      fanId: j.fanId,
    };
  } catch {
    return { mode: 'generic', supported: false, available: false, isGPDWin5: false, rpm: 0 };
  }
}

export async function fanGetTemp(): Promise<{ temp: number; source?: string }> {
  try {
    const r = await shell.run(TDPCTL_EXE, ['fan', 'temp'], 8000);
    if (r.exitCode !== 0) return { temp: 0 };
    const j = parseJson(r.stdout);
    return { temp: Number(j.temp ?? 0), source: j.source };
  } catch {
    return { temp: 0 };
  }
}

export async function fanSetAuto(): Promise<{ ok: boolean; msg: string; raw?: string }> {
  const r = await shell.run(TDPCTL_EXE, ['fan', 'set-auto'], 15000).catch(() => ({ exitCode: 1, stdout: '', stderr: '' }));
  if (r.exitCode !== 0) return { ok: false, msg: r.stderr || '风扇自动模式失败' };
  const j = parseJson(r.stdout);
  return { ok: !!j.ok, msg: j.ok ? '' : (j.error || '自动模式失败'), raw: j.raw };
}

export async function fanSetDuty(pct: number): Promise<{ ok: boolean; msg: string; percent?: number; raw?: string }> {
  const r = await shell.run(TDPCTL_EXE, ['fan', 'set', String(pct)], 15000).catch(() => ({ exitCode: 1, stdout: '', stderr: '' }));
  if (r.exitCode !== 0) return { ok: false, msg: r.stderr || '风扇占空比设置失败' };
  const j = parseJson(r.stdout);
  return { ok: !!j.ok, msg: j.ok ? '' : (j.error || '设置失败'), percent: Number(j.percent ?? pct), raw: j.raw };
}

export async function fanReadRpm(): Promise<{ rpm: number }> {
  const r = await shell.run(TDPCTL_EXE, ['fan', 'rpm'], 15000).catch(() => ({ exitCode: 1, stdout: '', stderr: '' }));
  if (r.exitCode !== 0) return { rpm: 0 };
  const j = parseJson(r.stdout);
  return { rpm: Number(j.rpm ?? 0) };
}
