/**
 * User-confirmed five-app baseline captured on 2026-10-07.
 * Used for new/missing configuration only: persisted arrays (including []) win.
 * Return fresh objects so callers cannot mutate the product defaults.
 */
export function createDefaultQuickApps(): { apps: Array<{ name: string; path: string }> } {
  return {
    apps: [
      { name: "此电脑", path: "C:\\Windows\\explorer.exe" },
      { name: "edge浏览器", path: "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe" },
      { name: "任务管理器", path: "C:\\Windows\\System32\\Taskmgr.exe" },
      { name: "关屏下载", path: "C:\\SOFT\\YeMan\\PowerControl\\关闭屏幕切为节能.vbs" },
      { name: "复古键盘", path: "C:\\Windows\\system32\\osk.exe" },
    ],
  };
}
