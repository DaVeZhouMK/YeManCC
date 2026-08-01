// trayResident.ts — 任务栏常驻偏好：持久化 + 应用
//
// 语义（与用户确认，已修正此前反转）：
//  · 默认不常驻（resident=false）：无任务栏按钮，仅托盘图标；关闭窗口后只存在于托盘。
//  · 开启（resident=true）：显示任务栏按钮（而非托盘）。
//  · 与 Xbox 游戏模式相互独立，由前端「任务栏常驻」开关独立控制。
//
// 偏好存于 C:\SOFT\YeMan\PowerControl\tray_resident.json：{ "resident": bool }
// 应用启动期（App.vue onMounted）调用 applyTrayResident() 套用，与各视图解耦；
// PowerView 负责 UI 展示、门控与写入。

import { fs, tray } from './api';

const TRAY_CFG = 'C:\\SOFT\\YeMan\\PowerControl\\tray_resident.json';

// 读取偏好；缺省/异常均视为关（默认不常驻）
export async function readTrayResident(): Promise<boolean> {
  try {
    const txt = await fs.readTextFile(TRAY_CFG);
    const j = JSON.parse(txt) as { resident?: boolean };
    return j.resident === true;
  } catch {
    return false;
  }
}

// 仅写入持久化（不发 IPC，供纯保存场景）
export async function writeTrayResident(v: boolean): Promise<void> {
  await fs.writeTextFile(TRAY_CFG, JSON.stringify({ resident: v }));
}

// 应用偏好：true→任务栏按钮(移除托盘)；false→仅托盘（默认）
export async function applyTrayResident(): Promise<void> {
  try {
    await tray.setResident(await readTrayResident());
  } catch {
    /* ignore */
  }
}

// 切换并即时应用（UI 开关变更时调用）
export async function setTrayResident(v: boolean): Promise<void> {
  await writeTrayResident(v);
  try {
    await tray.setResident(v);
  } catch {
    /* ignore */
  }
}
