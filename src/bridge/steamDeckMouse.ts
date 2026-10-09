import { invoke } from './ipc';

export interface SteamDeckMouseState {
  ok: boolean;
  available: boolean;
  percent: number;
  pending: boolean;
  desiredPercent?: number;
  waitingAccount?: boolean;
  steamRunning: boolean;
  liveAvailable?: boolean;
  appliedLive?: boolean;
  liveReason?: string;
  reason?: string;
  error?: string;
}
export const STEAM_DECK_MOUSE_DEFAULT = 100;
export function steamDeckMouseGet(): Promise<SteamDeckMouseState> {
  return invoke<SteamDeckMouseState>('steam.settings.get',{scope:'mouse'});
}
export function steamDeckMouseSet(percent: number): Promise<SteamDeckMouseState> {
  if (!Number.isInteger(percent) || percent < 1 || percent > 300) {
    return Promise.reject(new Error('灵敏度必须为 1%–300% 的整数'));
  }
  return invoke<SteamDeckMouseState>('steam.settings.set',{mousePercent:percent});
}
export function steamDeckMouseMessage(reason?: string): string {
  switch (reason) {
    case 'steam-not-found': return '未找到 Steam 安装目录。';
    case 'steam-account-not-found': return '请先登录 Steam，再重新进入控制器页面。';
    case 'desktop-autosave-not-selected': return '尚未确认 SteamDeck 桌面自定义布局。请在 Steam 桌面布局中调整并保存一次右摇杆灵敏度，然后刷新。';
    case 'desktop-autosave-ambiguous': return '多个 Steam 库中存在 SteamDeck 桌面布局，无法确认当前使用哪份；未覆盖任何配置。';
    case 'invalid-steam-libraries':
    case 'ambiguous-steam-libraries': return 'Steam 库路径信息未通过校验，未修改配置，请刷新后重试。';
    case 'steamdeck-not-enabled': return '请先开启 SteamDeck 虚拟手柄。';
    case 'live-read-unavailable':
    case 'steam-account-changed':
    case 'steam-session-starting':
    case 'steam-session-changed': return '等待 Steam 连接后应用。';
    case 'steam-account-waiting': return '待办已保留，等待原 Steam 账号连接。';
    case 'desktop-sensitivity-changed':
    case 'desktop-binding-changed':
    case 'desktop-file-changed': return '等待期间 Steam 桌面布局已变更，未覆盖其修改，请重新调整。';
    case 'live-debug-unavailable': return 'Steam 未开放本地实时接口；不会自动开启调试或重启 Steam。';
    case 'live-debug-not-loopback':
    case 'live-debug-owner-mismatch': return '实时接口的监听进程或地址未通过校验，未修改 Steam。';
    case 'live-api-unavailable':
    case 'live-context-unavailable': return '当前 Steam 版本未提供可用的实时接口。';
    case 'live-controller-not-connected': return 'Steam 尚未识别到 SteamDeck 手柄。';
    case 'live-editor-busy':
    case 'live-busy': return 'Steam 布局编辑器正在使用或有未保存修改，请先保存或退出编辑器。';
    case 'live-editor-changed': return '操作期间 Steam 编辑器发生其他修改，未代为保存；请在 Steam 中确认布局。';
    case 'live-layout-mismatch':
    case 'live-binding-mismatch':
    case 'live-controller-ambiguous':
    case 'live-controller-changed': return 'Steam 当前手柄或桌面布局不匹配，未覆盖配置。';
    case 'live-value-not-accepted': return 'Steam 未接受此数值，未保存布局；请在 Steam 编辑器中确认其允许范围。';
    case 'live-file-not-confirmed': return 'Steam 已接受实时设置，但尚未确认文件保存，请刷新核对。';
    case 'live-timeout':
    case 'live-transport-uncertain':
    case 'live-readback-failed': return '实时调用未完成确认，可能已生效；未追加文件写入，请在 Steam 中核对。';
    case 'live-discovery-timeout': return 'Steam 实时接口发现超时，未修改配置，请刷新重试。';
    case 'live-api-error':
    case 'live-response-invalid':
    case 'live-targets-invalid':
    case 'live-context-ambiguous':
    case 'live-transport-failed': return 'Steam 实时接口异常，未追加文件写入，请刷新重试。';
    case 'backup-failed': return '原布局备份失败，未修改 Steam 配置。';
    case 'settings-write-failed':
    case 'write-failed': return '配置保存失败，请检查文件权限后重试。';
    case 'readback-failed': return 'Steam 配置读回未通过，无法确认已保存。';
    case 'isolated-session': return '隔离测试不会读取或修改本机 Steam 配置。';
    default: return '未确认可安全修改的 SteamDeck 桌面右摇杆布局，滑块暂不可用。';
  }
}
