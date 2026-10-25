export const HIDHIDE_INSTALLER_NAME = 'HidHide_1.5.230_x64.exe';
export const DEFAULT_GAMEPAD_REDIST_DIRECTORY = 'C:\\SOFT\\YeMan\\PowerControl\\redist';

export interface HidHideSetupSnapshot {
  driverSetup?: number;
  hidHideSetupNotice?: number;
  driverSetupResult?: number;
  driverSetupRedistDir?: string;
  driverSetupSession?: string;
}
export interface HidHideManualInstallPrompt {
  directory: string;
  description: string;
}
export function hidHideManualInstallPrompt(state: HidHideSetupSnapshot): HidHideManualInstallPrompt | null {
  // Only native report-confirmed HidHide failures have descriptions.
  // Unverified preparation errors (notice 8) must not offer a HidHide reinstall.
  // Pending/slow preparation still owns its single installer. Do not offer a second installation.
  if (![3, 6, 7].includes(state.driverSetup ?? 0)) return null;
  const descriptions: Record<number, string> = {
    1: 'HidHide 安装结果检查未通过。请检查安装结果并按需自行安装。',
    2: '自动安装未获得管理员权限。请以管理员身份运行安装程序。',
    3: '其他控制器程序仍在运行，自动安装已跳过。请先关闭相关程序，再自行安装。',
    4: '检测到已有驱动或残留，自动安装已跳过。请自行检查、安装或修复 HidHide。',
    5: '缺少 HidHide 安装器。请先用 YMCC 完整安装包恢复安装器，再自行安装。',
    6: 'HidHide 安装器校验未通过。请先用 YMCC 完整安装包恢复正确安装器，再自行安装。',
  };
  const description = descriptions[state.hidHideSetupNotice ?? 0];
  if (!description) return null; // Ready/reboot-only and HidMaestro-only failures are not HidHide install failures.
  return { directory: state.driverSetupRedistDir || DEFAULT_GAMEPAD_REDIST_DIRECTORY, description };
}

// Keep an explicit dismissal across renderer reloads of the same native application session.
const DISMISSED_SESSION_KEY = 'ymcc.hidhide-setup.dismissed-session';
let dismissedInDocument = '';
function sessionWasDismissed(session: string): boolean {
  if (!session) return false;
  if (dismissedInDocument === session) return true;
  try { return window.sessionStorage.getItem(DISMISSED_SESSION_KEY) === session; }
  catch { return false; }
}
export function createHidHideSetupPromptGate() {
  let latest: HidHideManualInstallPrompt | null = null;
  let session = '';
  let shownSession = '';
  let phaseRank = -1;
  let shown = false;
  return {
    update(state: HidHideSetupSnapshot): void {
      if (shown) return;
      const incomingSession = state.driverSetupSession || '';
      const rank = state.driverSetup === 0 ? 0 : state.driverSetup === 1 ? 1 :
        state.driverSetup === 5 ? 2 : [2, 3, 4, 6, 7, 8, 9].includes(state.driverSetup ?? -1) ? 3 : -1;
      if (rank < 0) return;
      const sameSession = !session || !incomingSession || session === incomingSession;
      if (sameSession && rank < phaseRank) return; // A delayed pending snapshot cannot erase a terminal result.
      phaseRank = rank;
      latest = hidHideManualInstallPrompt(state);
      session = incomingSession || session;
    },
    take(ready: boolean, visible: boolean): HidHideManualInstallPrompt | null {
      if (shown || !ready || !visible || !latest || sessionWasDismissed(session)) return null;
      shown = true;
      shownSession = session;
      return latest;
    },
    dismiss(): void {
      if (!shownSession) return;
      dismissedInDocument = shownSession;
      try { window.sessionStorage.setItem(DISMISSED_SESSION_KEY, shownSession); }
      catch { /* The in-document dismissal still works when storage is unavailable. */ }
    },
  };
}
