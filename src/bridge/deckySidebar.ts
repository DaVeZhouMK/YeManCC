import { invoke, on } from './ipc';

export interface DeckySidebarState {
  recovery?: {phase: 'idle'|'recovering'|'manual-required';attempted:boolean;reason?:string;uncertain?:boolean};
  connectionRevision?: number;
  mirrorConnected?: boolean;
  powerReady?: boolean;
  enabled: boolean;
  steamKnown: boolean;
  steamRunning: boolean;
  pid: number;
  revision: number;
  phase: 'disabled' | 'waiting-steam' | 'waiting-manual-steam' | 'starting' | 'unavailable' | 'failed' | 'loader-started' | 'reconnecting' | 'frontend-attached';
  reason?: string;
  error?: number;
  bindingRetry?: boolean;
  bindingAttempts?: number;
  launchStage?:string;
  launchMode?:string;
  admissionRetry?: boolean;
  admissionAttempts?: number;
  admissionDetail?: string;
  admissionElapsedMs?: number;
  admissionNextRetryMs?: number;
  admissionRecovery?: boolean;
}

export interface DeckySidebarEnvironment {
  ready: boolean; bundled: boolean; pythonRequired: boolean; nodeRequired: boolean;
  createdSteamDebugMarker: boolean; reason: string; error?: number;
  steamDirectory?:string;resourceDirectory?:string;loaderPath?:string;pluginPath?:string;missingPath?:string;
  powerControlDirectory?:string;directorySource?:string;configuredOverride?:string;logPath?:string;
}
export const deckySidebar = {
  environment: () => invoke<DeckySidebarEnvironment>('deckySidebar.environment', {}, { timeoutMs: 5000 }),
  get: () => invoke<DeckySidebarState>('deckySidebar.get', {}, { timeoutMs: 3000 }),
  repairConnection: (pid:number,birth:string,revision:number) => invoke<{ok:boolean;reason?:string;mutated?:boolean;requestDispatched?:boolean}>('deckySidebar.repairConnection', {userInitiated:true,pid,birth,revision}, {timeoutMs:20000}),
  setEnabled: (enabled: boolean) => invoke<DeckySidebarState>('deckySidebar.setEnabled', { enabled }, { timeoutMs: 5000 }),
  subscribe: (listener: (state: DeckySidebarState) => void) => on<DeckySidebarState>('deckySidebar.updated', listener),
};

// No owned Loader: retained YMCC transport is not official store readiness.
// The native consumer still checks resources, ports and exact Steam identity.
export function deckySidebarCanRepair(state: DeckySidebarState | null): boolean {
  return !!state?.enabled && ((state.pid > 0 && state.phase === 'loader-started') || (state.pid === 0 &&
    ((state.phase === 'waiting-manual-steam' && state.reason === 'decky-context-already-loaded') ||
     (state.phase === 'frontend-attached' && state.reason === 'retained-frontend-connected') ||
     (state.phase === 'unavailable' && state.reason === 'mirror-bootstrap-session-changed' && state.steamKnown && state.steamRunning))));
}

export function deckyConnectionSummary(state: DeckySidebarState | null): {label:string;detail:string;tone:string} {
  if(!state)return {label:'正在检查连接',detail:'连接异常时自动恢复，也可手动修复',tone:'pending'};
  if(!state.enabled)return {label:'已关闭',detail:deckySidebarSafetyReason(state.reason)||'开启后自动连接 Steam',tone:'off'};
  if(state.recovery?.phase==='recovering')return {label:'正在恢复连接',detail:'恢复完成后自动可用',tone:'pending'};
  if(state.recovery?.phase==='manual-required')return {label:'需要手动修复',detail:state.recovery.reason==='steam-ui-refresh-requires-user'?'插件后台未连接，请点击修复连接':'自动恢复未完成，请重试',tone:'warning'};
  if(state.phase==='unavailable'&&state.reason==='mirror-bootstrap-session-changed')return {label:'连接已中断',detail:'Steam 会话校验发生变化，请手动修复并重新验证',tone:'warning'};
  if(state.phase==='frontend-attached')return {label:'正在接续服务',detail:'侧栏已接通，插件服务尚未就绪',tone:'pending'};
  if(state.phase==='loader-started'&&state.mirrorConnected){
    if(state.powerReady===false)return {label:'等待唤醒完成',detail:'已连接 YMCC，控制暂不可用',tone:'pending'};
    return {label:'已连接 YMCC',detail:'侧栏通信正常 · 可手动修复连接',tone:'connected'};
  }
  if(state.phase==='loader-started')return {label:'服务已启动',detail:'等待面板连接，可手动修复',tone:'pending'};
  if(state.phase==='failed'||state.phase==='unavailable')return {label:'连接暂不可用',detail:deckySidebarDescription(state),tone:'warning'};
  if(state.phase==='waiting-steam')return {label:'等待 Steam',detail:'Steam 启动后自动连接',tone:'off'};
  return {label:'正在检查连接',detail:'连接异常时自动恢复，也可手动修复',tone:'pending'};
}

export function deckySidebarSafetyReason(reason?:string):string {
  if(reason==='decky-frontend-incompatible')return 'Decky 界面初始化失败，可能与当前 Steam 不兼容，已自动关闭。请更新 YMCC 到新版后重试。';
  if(reason==='decky-render-failed')return '检测到 Decky 自身界面的致命渲染错误，已自动关闭。请更新 YMCC 到新版后重试。';
  if(reason==='loader-crash-loop')return 'Decky 加载器连续异常退出，已自动关闭。请检查最近安装的插件，或更新 YMCC 后重试。';
  return '';
}
// Always active in YMCC, even when the Steam settings page is not mounted.
// One existing nonmodal system notification per fault; opening the page retains the full reason.
export function startDeckySafetyNotifications():()=>void {
  let shown='';
  return deckySidebar.subscribe(state=>{
    if(state.enabled){shown='';return;}
    const message=deckySidebarSafetyReason(state.reason);
    if(!message||shown===state.reason)return;shown=state.reason!;
    void invoke<boolean>('notification.show',{title:'Decky 已关闭',body:message},{timeoutMs:3000}).catch(()=>{});
  });
}

export function deckySidebarDescription(state: DeckySidebarState | null): string {
  if (!state) return '与 Steam 联动启动；状态尚未确认';
  if (!state.enabled) return deckySidebarSafetyReason(state.reason)||'开启后自动准备所需环境，与 Steam 联动；无需手动安装 Decky/Python/Node';
  if (state.phase === 'waiting-steam') return '已开启，等待 Steam 启动';
  if (state.reason === 'decky-context-already-loaded') return 'Steam 中仍有已加载的插件界面，连接尚未接续。可点击“修复连接”刷新界面，Steam 和游戏保持运行';
  if (state.phase === 'waiting-manual-steam') {
    if (state.reason === 'decky-context-state-pending') return '当前 Steam 插件状态待确认；确认安全后再连接，不会自动重开大屏';
    if (state.reason === 'steam-ui-state-pending')
      return state.admissionRetry
        ? state.admissionRecovery
          ? '已开启，Steam 大屏状态尚未就绪，正在自动恢复；无需重复开关'
          : '已开启，Steam 大屏状态尚未就绪，正在自动复查；无需重复开关'
        : '已开启，Steam 大屏状态仍未确认；等待界面变化或点击 Steam 模式按钮后再次确认';
    if (state.reason === 'steam-bigpicture-required')
      return state.admissionRetry
        ? '已开启，当前为 Steam 桌面模式；切换大屏后自动连接'
        : '已开启，当前为 Steam 桌面模式；切换大屏后联动';
    return '已记住设置 · 重开 YMCC 不重新注入；点击 Steam 启动／模式按钮后联动';
  }
  if (state.phase === 'reconnecting') return '正在恢复当前 Steam 面板到 YMCC 的数据连接…';
  if (state.phase === 'frontend-attached') return '侧栏已连接 YMCC';
  if (state.phase === 'starting') return '正在启动侧栏加载器…';
  if(state.reason==='live-debug-unavailable'&&state.bindingRetry)return 'Steam 本地界面接口正在启动，正在有限连接重试；无需重复开关';
  const messages: Record<string, string> = {
    'resource-missing': '侧栏加载器资源尚未部署',
    'resource-hash-mismatch': 'Decky 加载器校验失败；请通过 YMCC 整包更新修复',
    'resource-package-mismatch': 'Decky 配套插件缺失或损坏；请通过 YMCC 整包更新修复',
    'resource-package-invalid': 'Decky 托管清单异常，已停止加载；请通过 YMCC 整包更新修复',
    'resource-upgrade-required': 'Decky 与当前 YMCC 版本不匹配，请更新 YMCC 到配套新版',
    'resource-unsafe-path': 'Decky 资源路径异常，已停止加载并保留现有内容',
    'resource-worker-deadline': 'Decky 资源自检超时，已停止本次启动；下次恢复时重新检查',
    'resource-worker-cleanup-incomplete': 'Decky 修复进程尚未确认回收，已停止本次启动',
    'resource-worker-read-failed': 'Decky 资源自检连接中断，已停止本次启动',
    'resource-worker-unavailable': 'Decky 资源自检进程不可用，请检查 YMCC 日志',
    'plugin-missing': 'YMCC 侧栏页面尚未部署',
    'mirror-bootstrap-session-changed': 'Steam 会话校验发生变化，旧连接已停止；请点击“修复连接”重新验证当前会话',
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
    'launch-failed': '加载器降权启动失败',
    'ownership-failed': '加载器进程隔离未完成，已停止本次启动',
    'environment-failed': '加载器启动环境准备失败',
    'loader-exited': '加载器已退出，正在有限重试恢复',
    'loader-crash-loop': '加载器连续短时退出，已停止自动重启；请检查 Decky 日志与最近安装的插件，处理后关闭并重新开启控制台重试',
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
  // A linked-token error describes the caller's session, not missing admin rights.
  // Keep exact Win32 stage/error in the suffix so remote diagnostics stay useful.
  const launchMessages:Record<string,string>={
    'linked-token-query':'无法取得启动所需的关联令牌，请检查 YMCC 启动方式与 Windows 登录会话',
    'caller-token-elevation-type':'无法确认 YMCC 启动令牌类型',
    'shell-token-elevated':'Windows 桌面也在以管理员权限运行，当前没有可用的普通权限桌面令牌',
    'shell-token-integrity':'Windows 桌面令牌不满足普通权限启动条件',
    'shell-token-identity':'Windows 桌面与 YMCC 的用户或登录会话不同，已停止加载器启动',
  };
  const message=(state.reason==='launch-failed'?launchMessages[state.launchStage??'']:undefined)??messages[state.reason ?? ''] ?? `侧栏暂不可用：${state.reason || state.phase}`;
  return message+(state.error?`（Windows 错误码 ${state.error}${state.launchStage?'；阶段 '+state.launchStage:''}）`:state.launchStage?'（阶段 '+state.launchStage+'）':'');
}




export function deckySidebarEnvironmentDescription(environment: DeckySidebarEnvironment | null): string {
  if (!environment) return '环境检测中';
  if (environment.ready) return '自带加载器、插件与环境文件已就绪；无需另装 Decky、Python 或 Node';
  const reason = environment.reason;
  if (reason === 'steam-debug-missing') return '已检测到 Steam 和自带加载器；首次启用自动准备本地连接';
  if (reason === 'steam-restart-required') return '环境已补齐，方便时正常重启 Steam 即可，不会强关游戏';
  if (reason === 'resource-missing' || reason === 'plugin-missing') return 'Decky 配套资源不完整；请使用 YMCC 完整更新包修复';
  if (['resource-package-mismatch','resource-package-invalid','resource-upgrade-required','resource-unsafe-path','resource-hash-mismatch'].includes(reason)) return deckySidebarDescription({enabled:true,steamKnown:false,steamRunning:false,pid:0,revision:0,phase:'unavailable',reason});
  if (reason === 'steam-not-installed') return '未检测到 Steam，请先安装 Steam';
  if (reason === 'steam-debug-access-denied') return 'Steam 安装目录不可写，需要使用 YMCC 原管理员启动方式';
  return `环境未就绪：${reason}`;
}

/** Error rendering must preserve the original reason when a read-only follow-up fails. */
export function deckySidebarSetupFailure(error:unknown,environment?:DeckySidebarEnvironment|null):string {
  const raw=error instanceof Error?error.message:String(error);
  const reason=environment && !environment.ready?environment.reason:raw;
  const message=deckySidebarDescription({enabled:true,steamKnown:false,steamRunning:false,pid:0,revision:0,phase:'unavailable',reason});
  const parts=['YMCC 控制台设置失败：'+message];
  if(environment?.missingPath)parts.push('缺失路径：'+environment.missingPath);
  else if(environment?.resourceDirectory && ['resource-missing','plugin-missing','resource-hash-mismatch','resource-package-mismatch','resource-package-invalid','resource-upgrade-required','resource-unsafe-path'].includes(reason))parts.push('检查目录：'+environment.resourceDirectory);
  if(environment?.directorySource==='environment-override')parts.push('当前使用 YEMAN_POWER_CONTROL_DIR 测试目录；请先确认现有配置，不要直接切换数据目录');
  if(environment?.error)parts.push('Windows 错误码：'+environment.error);
  if(environment?.logPath)parts.push('诊断日志：'+environment.logPath);
  return parts.join('；');
}
