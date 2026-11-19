import { invoke, on } from './ipc';
import { steamRunning } from './yeman';
export type SteamUiMode = 'desktop' | 'bigpicture' | 'unknown' | 'stopped';
export interface SteamUiState { known: boolean; running: boolean; mode: SteamUiMode; pid: number; birth: string; reason: string; observedAccount?: number; modeEpoch?: string; }
interface Dependencies { invoke<T>(cmd: string, args: object, options: { timeoutMs: number; signal?: AbortSignal }): Promise<T>; running(): Promise<boolean>; }
type SteamUiReadOptions = ({ automatic?: true } | { publishedOnly: true }) & { signal?: AbortSignal };
export function validSteamUiState(value: any): value is SteamUiState {
  return !!value && typeof value.known === 'boolean' && typeof value.running === 'boolean'
    && ['desktop','bigpicture','unknown','stopped'].includes(value.mode)
    && Number.isSafeInteger(value.pid) && value.pid >= 0 && value.pid <= 0xffffffff
    && typeof value.birth === 'string' && /^(0|[1-9]\d{0,19})$/.test(value.birth) && typeof value.reason === 'string'
    && (!['desktop','bigpicture'].includes(value.mode) || value.known && value.running && value.pid > 0 && value.birth !== '0')
    && (value.known || value.mode === 'unknown')
    && (!value.known || value.running || value.mode === 'stopped')
    && (value.mode !== 'stopped' || value.known && !value.running);
}
export function createSteamUi(deps: Dependencies) {
  return {
    async showBigPicture(state: SteamUiState, intent: { userInitiated: true }): Promise<void> {
      if (intent?.userInitiated !== true) throw new Error('显示 Steam 大屏只允许本次手动点击');
      if (!validSteamUiState(state) || !state.known || !state.running || state.mode !== 'bigpicture')
        throw new Error('当前 Steam 大屏状态未确认，请重新检测');
      const result = await deps.invoke<{ok:boolean;reason?:string}>('steam.ui.showBigPicture', {pid:state.pid,birth:state.birth,userInitiated:true}, {timeoutMs:10000});
      if (result?.ok !== true) throw new Error(result?.reason === 'steam-session-changed' ? 'Steam 会话已改变，请重新检测后重试' : result?.reason === 'steam-bigpicture-required' ? 'Steam 已不在大屏模式，请按当前状态重试' : 'Steam 大屏显示请求未成功：' + (result?.reason ?? '返回无效'));
    },
    async shutdown(state:SteamUiState,intent:{userInitiated:true}):Promise<void>{
      if(intent?.userInitiated!==true)throw Error('Steam 退出只允许本次手动确认');
      if(!validSteamUiState(state)||!state.known||!state.running||!state.pid||state.birth==='0')throw Error('Steam 实例身份未确认，请重新检测');
      const result=await deps.invoke<{ok:boolean;reason?:string}>('steam.ui.stop',{pid:state.pid,birth:state.birth,userInitiated:true},{timeoutMs:10000});
      if(!result||result.ok!==true)throw Error(result?.reason==='steam-session-changed'?'Steam 会话已改变，请重新检测后重试':'Steam 退出请求未成功：'+(result?.reason??'返回无效'));
    },
    async read(options?: SteamUiReadOptions): Promise<SteamUiState> {
      const requestOptions = options?.signal ? { timeoutMs: 8000, signal: options.signal } : { timeoutMs: 8000 };
      if (options && 'publishedOnly' in options) {
        const state = await deps.invoke<SteamUiState>('steam.uiState', { publishedOnly: true }, requestOptions);
        if (!validSteamUiState(state)) throw new Error('Steam 界面状态返回无效');
        return state;
      }
      const automatic = options && 'automatic' in options && options.automatic === true;
      try {
        let state: SteamUiState;
        try { state = await deps.invoke<SteamUiState>('steam.uiState', automatic ? {} : { force: true }, requestOptions); }
        catch (error) {
          // Older native builds accept an empty argument object. Compatibility
          // is one explicit retry only, never a timeout/IO fallback loop.
          if (automatic || options?.signal?.aborted || String((error as Error).message) !== 'Invalid Steam UI read') throw error;
          state = await deps.invoke<SteamUiState>('steam.uiState', {}, requestOptions);
        }
        if (!validSteamUiState(state)) throw new Error('Steam 界面状态返回无效');
        return state;
      } catch (error) {
        // Only a missing handler merits legacy compatibility. IO/transport failure
        // is unknown, never evidence of desktop mode or permission to stop Steam.
        const signal = options?.signal;
        if (signal?.aborted || !/^(?:unknown|unknown ipc cmd): steam\.uiState$/i.test(String((error as Error)?.message))) throw error;
        const running = signal ? await new Promise<boolean>((resolve, reject) => {
          const abort = () => reject(new Error('IPC request retired'));
          signal.addEventListener('abort', abort, { once: true });
          Promise.resolve().then(() => {
            if (signal.aborted) throw new Error('IPC request retired');
            return deps.running();
          }).then(resolve, reject).finally(() => signal.removeEventListener('abort', abort));
        }) : await deps.running();
        return { known: true, running, mode: running ? 'unknown' : 'stopped', pid: 0, birth: '0', reason: 'native-ui-mode-unavailable' };
      }
    },
    async switchMode(state: SteamUiState, mode: 'desktop' | 'bigpicture', intent: { userInitiated: true }): Promise<void> {
      if (intent?.userInitiated !== true) throw new Error('Steam 模式只允许本次手动点击切换，不自动恢复或纠正模式');
      if (!validSteamUiState(state) || !state.known || !state.running || !state.pid || state.birth === '0' || !['desktop','bigpicture'].includes(state.mode))
        throw new Error('Steam 界面模式未确认，请重新检测；旧版原生程序需要更新');
      if (mode !== 'desktop' && mode !== 'bigpicture') throw new Error('无效的 Steam 界面模式');
      if (state.mode === mode) return;
      const result = await deps.invoke<{ ok: boolean; reason?: string }>('steam.uiMode.set', { mode, pid: state.pid, birth: state.birth, userInitiated: true }, { timeoutMs: 10000 });
      if (!result || result.ok !== true) throw new Error(result?.reason === 'steam-session-changed' ? 'Steam 会话已改变，请重新检测后重试' : 'Steam 模式切换请求未成功：' + (result?.reason ?? '返回无效'));
    },
  };
}
const service = createSteamUi({ invoke, running: steamRunning });
const notificationReads = new WeakMap<Event, Promise<SteamUiState>>();
let activeNotification: Event | undefined;
export const steamUi = {
  ...service,
  async read(options?: SteamUiReadOptions): Promise<SteamUiState> {
    // Share only within the synchronous subscribers of this exact dispatch.
    // A later notification, timer, or action confirmation reads independently.
    if (!options || 'publishedOnly' in options || !options.automatic || options.signal || !activeNotification) return service.read(options);
    const notification = activeNotification;
    let pending = notificationReads.get(notification);
    if (!pending) {
      pending = service.read(options);
      notificationReads.set(notification, pending);
    }
    return { ...await pending };
  },
  subscribe(handler: (published?: SteamUiState) => void, options?: { publishedMediaState: true }): () => void {
    return on<{mediaStateV1?: unknown}>('steam.uiStateChanged', (data, notification) => {
      const previous = activeNotification;
      activeNotification = notification;
      try {
        const state = options?.publishedMediaState ? data?.mediaStateV1 : undefined;
        if(validSteamUiState(state) && state.reason === 'published-media-notice' &&
            (state.mode === 'desktop' || state.mode === 'bigpicture') &&
            (state.observedAccount === undefined || Number.isSafeInteger(state.observedAccount) &&
             state.observedAccount > 0 && state.observedAccount <= 0xffffffff) &&
            (state.modeEpoch === undefined || typeof state.modeEpoch === 'string' && state.modeEpoch.length <= 96)) {
          handler(state);
        } else handler();
      } finally { activeNotification = previous; }
    }, true);
  },
};
