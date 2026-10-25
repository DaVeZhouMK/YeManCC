import { DEFAULT_GAMEPAD_REDIST_DIRECTORY, HIDHIDE_INSTALLER_NAME, type HidHideSetupSnapshot } from './hidHideSetupPrompt';

export interface GamepadEnvironmentNotice {
  readonly title: string;
  readonly description: string;
  readonly tone: 'neutral' | 'warning';
}

// Presentation only. Preparation status never changes target admission or runtime feedback.
export function gamepadEnvironmentNotice(snapshot: Readonly<HidHideSetupSnapshot>): GamepadEnvironmentNotice | null {
  const status = snapshot.driverSetup ?? 0;
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
    return { title: '手柄驱动维护已暂缓', description: '已检测到当前或更新版本的驱动包；控制器程序正在运行，本次自动维护已跳过。驱动包存在不代表当前设备已绑定或输入正常，请以实际创建和输入结果为准；占用详情见日志。', tone: 'neutral' };
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
