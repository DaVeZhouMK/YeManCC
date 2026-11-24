import { normalizeMonitorOutput, type MonitorOutput, type MonitorOutputPreferences } from './monitorHudModel';
export type AppliedMonitorOutput = MonitorOutput | 'off' | 'unknown';
export interface OutputAck { confirmed: boolean; reason?: string; }
export type MonitorOutputCurrent = (() => Promise<boolean>) & { readonly bootContext?: import('./bootModeGlobalPreferences').BootPreferenceContext };
export interface MonitorOutputDeps {
  // Existing adapter registration only; no capability or availability IO.
  ownsYmcc?(): boolean;
  capabilities(preferences:MonitorOutputPreferences): Promise<{ steam: boolean; ymcc: boolean; steamAbsent?:boolean; ymccOwned?:boolean; ymccGlobal?:boolean }>;
  ensureRtss(current?: MonitorOutputCurrent): Promise<void>;
  setSteam(position: number, current?: MonitorOutputCurrent): Promise<OutputAck>;
  setRtss(show: boolean, preferences: MonitorOutputPreferences, current?: MonitorOutputCurrent): Promise<OutputAck>;
  setYmcc(show: boolean, preferences: MonitorOutputPreferences, current?: MonitorOutputCurrent): Promise<OutputAck>;
}
export interface MonitorOutputApplyResult { ok: boolean; applied: AppliedMonitorOutput; reason: string; phase: string; }
// Game-output switching retains strict break-before-make acknowledgements.
// Global desktop scope owns only its native desktop window: it has no Steam
// prerequisite and does not forge hidden/application ACKs for game overlays.
// RTSS is still requested as a background backend, but cannot veto desktop visibility.
export function createMonitorOutputCoordinator(deps: MonitorOutputDeps) {
  let queue: Promise<unknown> = Promise.resolve();
  const run = async (raw: MonitorOutputPreferences, current?: MonitorOutputCurrent): Promise<MonitorOutputApplyResult> => {
    const prefs = normalizeMonitorOutput(raw); let phase = 'preflight', changed = false;
    const requireCurrent=async()=>{if(current&&!await current())throw Error('MONITOR_OUTPUT_REQUEST_EXPIRED');};
    const owned=<T>(operation:()=>Promise<T>):Promise<T>=>current?(async()=>{await requireCurrent();const result=await operation();await requireCurrent();return result;})():operation();
    let ymccClose:OutputAck|undefined;
    try {
      if(current)await requireCurrent();
      if(!prefs.enabled&&deps.ownsYmcc?.()){
        // The registered owner can reclaim its descriptor without waiting for
        // unrelated capability IPC. Keep its ACK for this request; do not retry.
        phase='hide-ymcc';changed=true;
        try{ymccClose=await owned(()=>deps.setYmcc(false,prefs,current));}
        catch(error){ymccClose={confirmed:false,reason:String(error instanceof Error?error.message:error)};}
      }
      phase='preflight';
      const caps = await owned(()=>deps.capabilities(prefs));
      const globalScope=prefs.output==='ymcc'&&caps.ymccGlobal===true;
      if (prefs.enabled && prefs.output === 'ymcc' && !caps.ymcc) return { ok: false, applied: 'unknown', phase,
        reason: 'YMCC 模板已保存；原生 YMCC 输出承载尚未就绪，未改变当前显示。' };
      if(globalScope){
        // Desktop visibility is the primary operation. Neither Steam nor RTSS
        // startup/OSD acknowledgement may prevent this local window appearing.
        const ack=(result:OutputAck)=>{if(!result.confirmed)throw Error(result.reason||'原生桌面 HUD 未确认');};
        phase='hide-ymcc';changed=true;
        let hudCloseFailure='';
        try { ack(ymccClose??await owned(()=>deps.setYmcc(false,prefs,current))); }
        catch(error){
          if(prefs.enabled)throw error;
          if(current)await requireCurrent();
          hudCloseFailure=String(error instanceof Error ? error.message : error);
        }
        if(prefs.enabled){phase='show-ymcc';ack(await owned(()=>deps.setYmcc(true,prefs,current)));}
        const warnings = new Set<string>();
        // Save OFF before a cold launch, so the lock backend cannot start by
        // displaying the last RTSS template. OSD errors do not stop lock startup.
        phase='hide-rtss';
        try {
          const hidden = await owned(()=>deps.setRtss(false,prefs,current));
          if (!hidden.confirmed) warnings.add(hidden.reason || 'RTSS 游戏 OSD 关闭未确认。');
        } catch (error) { warnings.add(String(error instanceof Error ? error.message : error)); }
        if(current)await requireCurrent();
        if(!prefs.enabled){
          if(hudCloseFailure || warnings.size)return {ok:false,applied:'unknown',phase:hudCloseFailure?'hide-ymcc':phase,
            reason:[hudCloseFailure?'YMCC 全局监控关闭未确认：'+hudCloseFailure:'YMCC 全局监控已关闭',
              warnings.size?'RTSS 游戏 OSD 关闭未确认：'+[...warnings].join('；'):'RTSS 游戏 OSD 已关闭'].join('；')};
          return {ok:true,applied:'off',phase:'done',reason:'YMCC 全局监控和 RTSS 游戏 OSD 已关闭；不修改 Steam，不启停 RTSS 后台。'};
        }
        try { await owned(()=>deps.ensureRtss(current)); }
        catch (error) { warnings.add(String(error instanceof Error ? error.message : error)); }
        if(current)await requireCurrent();
        return {ok:true,applied:'ymcc',phase:'done',reason:(prefs.ymcc.visibility==='game-running'?'自定义监控已启用，游戏开启时显示。':prefs.ymcc.visibility==='game-focused'?'自定义监控已启用，游戏对焦时显示。':'自定义监控已开启，常驻显示。')+(warnings.size?' RTSS 状态未全部确认：'+[...warnings].join('；'):' RTSS 后台保留，游戏 OSD 已关闭。')};
      }
      if(!prefs.enabled){
        // OFF has no target to enable. Release owned windows even when another
        // backend cannot acknowledge hiding, and never start RTSS just to close.
        const failures:string[]=[];let failedPhase='';
        const hide=async(nextPhase:string,operation:()=>Promise<OutputAck>)=>{
          phase=nextPhase;changed=true;
          try{const result=await owned(operation);if(!result.confirmed)throw Error(result.reason||nextPhase+'未确认');}
          catch(error){if(!failedPhase)failedPhase=nextPhase;failures.push(String(error instanceof Error?error.message:error));}
        };
        if(ymccClose||caps.ymcc||caps.ymccOwned)await hide('hide-ymcc',async()=>ymccClose??await deps.setYmcc(false,prefs,current));
        if(!caps.steamAbsent){
          if(caps.steam)await hide('hide-steam',()=>deps.setSteam(0,current));
          else{failedPhase=failedPhase||'hide-steam';failures.push('Steam 实时状态不可确认');}
        }
        await hide('hide-rtss',()=>deps.setRtss(false,prefs,current));
        return failures.length?{ok:false,applied:'unknown',phase:failedPhase,reason:'已尝试关闭可用输出；仍有关闭状态未确认：'+failures.join('；')}:
          {ok:true,applied:'off',phase:'done',reason:'监控显示已关闭；不启动或停止 RTSS 后台，不改变锁帧。'};
      }
      if (!globalScope&&((!caps.steam&&!caps.steamAbsent)||(prefs.enabled&&prefs.output==='steam'&&!caps.steam))) return { ok: false, applied: 'unknown', phase, reason: 'Steam 实时设置不可确认；为避免多路重叠，未切换输出。' };
      phase = 'rtss-backend'; await owned(()=>deps.ensureRtss(current));
      const ack = (result: OutputAck, label: string) => { if (!result.confirmed) throw Error(result.reason || label + '未确认'); };
      phase = 'hide-steam'; changed = true; if(!globalScope&&!caps.steamAbsent)ack(await owned(()=>deps.setSteam(0,current)), 'Steam 隐藏');
      // Load RTSS targets directly after the other outputs acknowledge hiding.
      // Pre-clearing to Empty would strand RTSS if a later close/load fails.
      if (prefs.output !== 'rtss') {
        phase = 'hide-rtss'; ack(await owned(()=>deps.setRtss(false, prefs,current)), 'RTSS 隐藏');
      }
      if (caps.ymcc || caps.ymccOwned) { phase = 'hide-ymcc'; ack(await owned(()=>deps.setYmcc(false, prefs,current)), 'YMCC 隐藏'); }
      phase = 'show-' + prefs.output;
      let carrierReason='';
      if (prefs.output === 'steam') ack(await owned(()=>deps.setSteam(prefs.steamPosition,current)), 'Steam 显示');
      else if (prefs.output === 'rtss') ack(await owned(()=>deps.setRtss(true, prefs,current)), 'RTSS 显示');
      else {const result=await owned(()=>deps.setYmcc(true,prefs,current));ack(result,'YMCC 显示');carrierReason=result.reason??'';}
      return { ok: true, applied: prefs.output, phase: 'done', reason: globalScope?'YMCC 全局监控已开启：桌面也显示，不依赖游戏或 Steam；RTSS 后台保留。':'已确认单路输出；RTSS 后台保留。'+(carrierReason?' '+carrierReason:'') };
    } catch (error) {
      return { ok: false, applied: 'unknown', phase, reason: (!prefs.enabled?'关闭未全部确认；':changed ? '切换中断，当前显示状态待核对；未继续启用目标。' : '未切换输出。') + String(error instanceof Error ? error.message : error) };
    }
  };
  return { apply(raw: MonitorOutputPreferences, current?: MonitorOutputCurrent) {
    // Snapshot at enqueue: a later slider or toggle must not mutate this intent.
    const prefs = normalizeMonitorOutput(raw);
    const result = queue.then(() => run(prefs,current)); queue = result.catch(() => {}); return result;
  } };
}
