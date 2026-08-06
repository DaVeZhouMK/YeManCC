// tdpAutoApply.ts — 启动 / 唤醒自动应用 TDP 最大值
//
// 语义（与用户约定一致）：这两个开关都不优先于「帧数目标浮动优化」(autofloat)。
// 只要帧数目标浮动优化处于启用状态(enabled)，TDP 由浮动接管，这里直接返回、不生效；
// 仅当浮动未接管(floating off)且对应开关开启时，才把程序记录的 TDP 最大值下发硬件。
//
// 持久化文件：C:\SOFT\YeMan\PowerControl\tdp-auto-apply.json = { boot, wake }
// 默认两者都开启，保留「开机 / 唤醒后应用 TDP」的既有行为；用户可在 TDP 页关闭。

import { fs } from '@/bridge/api';
import { readTdp, setTdp } from '@/bridge/yeman';
import { getFloatInfo } from '@/bridge/autofloat';

const FILE = 'C:\\SOFT\\YeMan\\PowerControl\\tdp-auto-apply.json';

export interface TdpAutoApply {
  boot: boolean;
  wake: boolean;
}

// 读取持久化开关；文件缺失或格式无效时回退默认（两者都开）
export async function readTdpAutoApply(): Promise<TdpAutoApply> {
  try {
    const txt = await fs.readTextFile(FILE);
    const j = JSON.parse(txt) as { boot?: boolean; wake?: boolean };
    return {
      boot: typeof j.boot === 'boolean' ? j.boot : true,
      wake: typeof j.wake === 'boolean' ? j.wake : true,
    };
  } catch {
    return { boot: true, wake: true };
  }
}

export async function writeTdpAutoApply(v: TdpAutoApply): Promise<void> {
  await fs.writeTextFileAtomic(
    FILE,
    JSON.stringify({ boot: !!v.boot, wake: !!v.wake }),
  );
}

// 开机 / 唤醒时按开关应用 TDP 最大值。
// 关键优先级：帧数目标浮动优化开启(floating)时它接管 TDP，这里不覆盖、不冲突。
export async function applyAutoTdpIfNeeded(kind: 'boot' | 'wake'): Promise<void> {
  // 浮动优化优先：开启时由 autofloat 管理 TDP，开关此时不生效
  if (getFloatInfo().enabled) return;
  const cfg = await readTdpAutoApply();
  if (!cfg[kind]) return;
  const v = await readTdp('ac').catch(() => null);
  if (v != null) {
    await setTdp('ac', v, { apply: true, save: false }).catch(() => {});
  }
}
