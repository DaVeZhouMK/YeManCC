import { DEFAULT_GAMEPAD_REDIST_DIRECTORY, HIDHIDE_INSTALLER_NAME, type HidHideSetupSnapshot } from './hidHideSetupPrompt';

export interface GamepadEnvironmentNotice {
  readonly title: string;
  readonly description: string;
  readonly tone: 'neutral' | 'warning';
}

export const LEGACY_HIDHIDE_TARGET_MESSAGE = 'HidHide 版本过旧，已阻止启用虚拟手柄。请更新 HidHide 并重启 Windows。';

// Presentation only. Only the backend's explicit version decision blocks virtual starts.
export function gamepadEnvironmentNotice(snapshot: Readonly<HidHideSetupSnapshot>): GamepadEnvironmentNotice | null {
  const status = snapshot.driverSetup ?? 0;
  const old = snapshot.hidHideUnsupportedVersion;
  const notice = snapshot.hidHideSetupNotice;
  if (notice !== undefined && notice >= 10 && notice <= 18) {
    const code = snapshot.hidHideMaintenanceExit;
    const error = typeof code === 'number' && code > 0 ? `（错误码 ${code}）` : '';
    const directory = snapshot.driverSetupRedistDir || DEFAULT_GAMEPAD_REDIST_DIRECTORY;
    const installer = directory.replace(/[\\/]+$/, '') + '\\' + HIDHIDE_INSTALLER_NAME;
    const manual = '请退出 YMCC 和其他手柄工具。在 Windows「设置 → 应用 → 已安装的应用」中卸载 HidHide，重启后运行 ' + installer + '，安装完成后再重启。若 HidHide 已不在列表或卸载报错，请导出日志，不要手动删除驱动或手柄归还记录。';
    const messages: Record<number, [string, string]> = {
      10: [`HidHide 自动卸载失败${error}`, '本次未安装新版，也不会反复卸载。' + manual],
      11: [`HidHide 卸载后仍有残留${error}`, '旧组件未确认清除，已停止补装。' + manual],
      12: ['HidHide 旧组件残留，未能自动卸载', '找不到唯一可验证的官方卸载入口，未覆盖安装。' + manual],
      13: [`HidHide 新版安装未确认${error}`, '请退出 YMCC，运行 ' + installer + ' 修复，然后重启 Windows。若仍失败，请导出日志。'],
      14: ['HidHide 卸载后需要重启', '请重启 Windows，再打开 YMCC。程序核验旧组件清除后会自动安装新版。'],
      15: ['HidHide 新版安装后需要重启', '请重启 Windows，再打开 YMCC。新版核验通过前不会启用虚拟手柄。'],
      16: ['HidHide 自动升级未确认', '本次没有重复操作。请先重启 Windows 并重新打开 YMCC；若仍显示此提示，请导出日志。'],
      17: ['HidHide 自动升级等待其他手柄工具退出', '请退出其他手柄工具或正在运行的 HidHide 配置客户端，再重新打开 YMCC。'],
      18: ['HidHide 自动升级需要管理员权限', '请以管理员身份重新运行 YMCC，再检查升级状态。'],
    };
    const [title, description] = messages[notice];
    return { title, description, tone: 'warning' };
  }
  if (old && (status === 1 || status === 5)) {
    return { title: `正在检查旧版 HidHide ${old}`, description: '本次准备结束前，虚拟手柄不会启用。', tone: 'neutral' };
  }
  if (old && (notice === 5 || notice === 6)) {
    const problem = notice === 5 ? '缺少新版 HidHide 安装器' : '新版 HidHide 安装器校验失败';
    return { title: problem, description: '自动升级未执行，虚拟手柄仍暂停。请用完整安装包恢复随包安装器，再重新打开 YMCC。', tone: 'warning' };
  }
  if (old || (status === 3 && (notice === 4 || notice === 9))) {
    const directory = snapshot.driverSetupRedistDir || DEFAULT_GAMEPAD_REDIST_DIRECTORY;
    const installer = directory.replace(/[\\/]+$/, '') + '\\' + HIDHIDE_INSTALLER_NAME;
    const title = old ? `HidHide 驱动 ${old} 过旧，虚拟手柄未启用`
      : notice === 9 ? 'HidHide 驱动版本过旧' : 'HidHide 安装不完整，未自动补装';
    return { title, tone: 'warning', description:
      '请保留本机手柄，退出 YMCC 和其他手柄工具。运行 ' + installer + ' 更新或修复 HidHide；若安装器要求先卸载，请在 Windows「设置 → 应用 → 已安装的应用」中卸载 HidHide，重启后再安装。安装完成后重启 Windows，再回此页重试。若卸载报错，请保留错误码或截图，不要手动删除驱动文件。' };
  }
  if (status === 0 || status === 2) return null;
  if (status === 1) {
    return { title: '正在准备手柄环境', description: '后台正在检查并按需准备手柄驱动环境，可以继续尝试启用虚拟手柄。若本次创建失败，请等准备结束后重试。', tone: 'neutral' };
  }
  if (status === 4) {
    return { title: '手柄驱动安装后需要重启', description: '可以继续尝试启用虚拟手柄，但屏蔽或输出可能尚未生效。请保存工作并重启 Windows；程序不会自动重启。', tone: 'warning' };
  }
  // Exit 12/13 alone does not identify HidHide. Native notice 5/6 requires
  // a current report that confirms the dependency; mismatches stay advisory.
  if ((status === 6 && snapshot.hidHideSetupNotice === 5) || (status === 7 && snapshot.hidHideSetupNotice === 6)) {
    const directory = snapshot.driverSetupRedistDir || DEFAULT_GAMEPAD_REDIST_DIRECTORY;
    const installer = directory.replace(/[\\/]+$/, '') + '\\' + HIDHIDE_INSTALLER_NAME;
    return status === 6
      ? { title: '缺少 HidHide 安装器', description: '可以继续尝试启用虚拟手柄。请用完整安装包恢复 ' + installer + '，然后重启 YMCC 以自动补装。', tone: 'warning' }
      : { title: 'HidHide 安装器校验失败', description: '自动补装已跳过，可以继续尝试启用虚拟手柄。请用完整安装包恢复 ' + installer + '，然后重启 YMCC。', tone: 'warning' };
  }
  if (status === 6 || status === 7) {
    return { title: '手柄环境准备未确认', description: '后台准备结果未能确认，不能据此判断已安装的驱动是否可用。可以继续尝试启用虚拟手柄；实际创建失败时会显示后端错误，准备详情见日志。', tone: 'neutral' };
  }
  if (status === 9 && snapshot.hidHideSetupNotice === 0) {
    // Routine maintenance deferral remains in diagnostics; no controller-page banner.
    return null;
  }
  if (status === 8) {
    return { title: '手柄环境提示', description: '驱动清单格式暂未识别，自动维护未执行。当前连接是否正常，以实际创建和输入结果为准。', tone: 'neutral' };
  }
  if (status === 3 || status === 5) {
    const detail = status === 5 ? '后台环境准备耗时较长，仍在等待本次准备结束。' : '后台环境检查未能确认结果，这不代表已安装的驱动不可用；详情已记录在日志中。';
    return { title: '手柄环境提示', description: detail + '可以继续尝试启用虚拟手柄；实际创建失败时会显示后端错误。', tone: 'neutral' };
  }
  return { title: '手柄环境状态未确认', description: '可以继续尝试启用虚拟手柄，请导出日志查看环境检查结果。', tone: 'neutral' };
}
