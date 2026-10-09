import { invoke, on } from './ipc';

export interface DeckySidebarState {
  enabled: boolean;
  steamKnown: boolean;
  steamRunning: boolean;
  pid: number;
  revision: number;
  phase: 'disabled' | 'waiting-steam' | 'starting' | 'unavailable' | 'failed' | 'loader-started';
  reason?: string;
  error?: number;
  bindingRetry?: boolean;
  bindingAttempts?: number;
}

export interface DeckySidebarEnvironment {
  ready: boolean; bundled: boolean; pythonRequired: boolean; nodeRequired: boolean;
  createdSteamDebugMarker: boolean; reason: string; error?: number;
}
export const deckySidebar = {
  environment: () => invoke<DeckySidebarEnvironment>('deckySidebar.environment', {}, { timeoutMs: 5000 }),
  get: () => invoke<DeckySidebarState>('deckySidebar.get', {}, { timeoutMs: 3000 }),
  setEnabled: (enabled: boolean) => invoke<DeckySidebarState>('deckySidebar.setEnabled', { enabled }, { timeoutMs: 5000 }),
  subscribe: (listener: (state: DeckySidebarState) => void) => on<DeckySidebarState>('deckySidebar.updated', listener),
};

export function deckySidebarDescription(state: DeckySidebarState | null): string {
  if (!state) return '与 Steam 联动启动；状态尚未确认';
  if (!state.enabled) return '开启后自动准备所需环境，与 Steam 联动；无需手动安装 Decky/Python/Node';
  if (state.phase === 'waiting-steam') return '已开启，等待 Steam 启动';
  if (state.phase === 'starting') return '正在启动侧栏加载器…';
  const messages: Record<string, string> = {
    'resource-missing': '侧栏加载器资源尚未部署',
    'resource-hash-mismatch': '侧栏加载器资源校验失败，未启动',
    'plugin-missing': 'YMCC 侧栏页面尚未部署',
    'port-1337-in-use': 'Decky 端口已被其它程序占用，未另起加载器',
    'port-state-unknown': '无法确认 Decky 端口状态，未启动',
    'steam-debug-disabled': 'Steam 本地调试未开启，重新开启控制台可自动准备',
    'steam-debug-missing': '首次开启将自动准备 Steam 本地调试',
    'steam-restart-required': '所需环境已补齐；请在方便时正常重启 Steam，不会强关游戏',
    'steam-debug-access-denied': 'Steam 目录不可写，请用现有管理员启动方式重试 YMCC',
    'steam-debug-unsafe-path': 'Steam 调试标记路径异常，未覆盖任何文件',
    'steam-not-installed': '未检测到 Steam；请先安装 Steam',
    'steam-debug-create-failed': '无法准备 Steam 本地调试，未启动加载器',
    'environment-check-failed': '环境检测失败，未启动加载器',
    'loader-exited': '加载器已退出；关闭后重新开启可重试',
    'steam-state-unknown': 'Steam 状态未知，未启动加载器',
    'mirror-bootstrap-context-unavailable': 'Steam 界面暂未就绪',
    'live-debug-unavailable': 'Steam 本地连接尚不可用；首次准备后请正常重启 Steam，不会强关游戏',
    'live-context-unavailable': 'Steam 界面暂未就绪',
    'mirror-bootstrap-context-exited': 'Steam 界面上下文已退出',
    'mirror-bootstrap-context-watch-unavailable': '无法观察 Steam 界面进程；镜像未确认',
    'mirror-bootstrap-canceled': '本次镜像连接已取消',
    'mirror-bootstrap-deadline': 'Steam 镜像连接超时',
  };
  if (state.bindingRetry === true) {
    const count = state.bindingAttempts ?? 1;
    return `${state.pid ? '加载器保留' : '尚未启动加载器'}；${messages[state.reason ?? ''] ?? state.reason ?? '镜像未确认'}；有限重试中（${count}/5）`;
  }
  if (state.phase === 'loader-started') {
    if (state.reason && state.reason !== 'mirror-handshake-pending')
      return `加载器保留；镜像未确认：${messages[state.reason] ?? state.reason}`;
    return '加载器已启动，等待 YMCC 镜像连接确认';
  }
  return messages[state.reason ?? ''] ?? `侧栏暂不可用：${state.reason || state.phase}`;
}




export function deckySidebarEnvironmentDescription(environment: DeckySidebarEnvironment | null): string {
  if (!environment) return '环境检测中';
  if (environment.ready) return '自带加载器、插件与环境文件已就绪；无需另装 Decky、Python 或 Node';
  const reason = environment.reason;
  if (reason === 'steam-debug-missing') return '已检测到 Steam 和自带加载器；首次启用自动准备本地连接';
  if (reason === 'steam-restart-required') return '环境已补齐，方便时正常重启 Steam 即可，不会强关游戏';
  if (reason === 'resource-missing' || reason === 'plugin-missing') return 'YMCC 分发资源不完整；请部署完整的 PowerControl/decky 包，不会下载未校验的加载器';
  if (reason === 'steam-not-installed') return '未检测到 Steam，请先安装 Steam';
  if (reason === 'steam-debug-access-denied') return 'Steam 安装目录不可写，需要使用 YMCC 原管理员启动方式';
  return `环境未就绪：${reason}`;
}
