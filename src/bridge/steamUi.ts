import { invoke, on } from './ipc';
import { steamRunning } from './yeman';
export type SteamUiMode = 'desktop' | 'bigpicture' | 'unknown' | 'stopped';
export interface SteamUiState { known: boolean; running: boolean; mode: SteamUiMode; pid: number; birth: string; reason: string; }
interface Dependencies { invoke<T>(cmd: string, args: object, options: { timeoutMs: number }): Promise<T>; running(): Promise<boolean>; }
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
    async read(options?: { automatic: true }): Promise<SteamUiState> {
      try {
        let state: SteamUiState;
        try { state = await deps.invoke<SteamUiState>('steam.uiState', options?.automatic ? {} : { force: true }, { timeoutMs: 8000 }); }
        catch (error) {
          // Older native builds accept an empty argument object. Compatibility
          // is one explicit retry only, never a timeout/IO fallback loop.
          if (options?.automatic || String((error as Error).message) !== 'Invalid Steam UI read') throw error;
          state = await deps.invoke<SteamUiState>('steam.uiState', {}, { timeoutMs: 8000 });
        }
        if (!validSteamUiState(state)) throw new Error('Steam 界面状态返回无效');
        return state;
      } catch (error) {
        // Only a missing handler merits legacy compatibility. IO/transport failure
        // is unknown, never evidence of desktop mode or permission to stop Steam.
        if (!/^unknown:\s*steam\.uiState$|unknown.*(?:command|ipc|cmd)|unsupported.*(?:command|ipc|cmd)|no handler/i.test(String((error as Error).message))) throw error;
        const running = await deps.running();
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
export const steamUi = { ...createSteamUi({ invoke, running: steamRunning }), subscribe: (handler: () => void) => on('steam.uiStateChanged', handler) };
