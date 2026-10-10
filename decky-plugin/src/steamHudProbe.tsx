// Opt-in test build only. Adapts MagicBlack's Steam composition mechanism;
// the existing YMCC console, broker, settings, hardware owners and Loader are untouched.
import { isCompositionHook,makeProbeState,uniqueExport,MAX_PROBE_MS,probePaint,type ProbeConfig } from './steamHudProbeState';
export interface ProbeDeps { host:any; react:any; ui:any; routerHook:any; }
export function installSteamHudProbe({host,react:React,ui,routerHook}:ProbeDeps):()=>void {
  const property='__YMCC_STEAM_HUD_PROBE__';
  if(host[property])throw Error('An existing HUD probe owns the diagnostic slot');
  const state=makeProbeState(),instances=new Map<string,Record<string,unknown>>(),stores=new Map<string,any>();
  let disposed=false,registered:string|null=null,deadline:any=null,freshness:any=null;
  const composition=uniqueExport(ui.modules,fn=>isCompositionHook(fn));
  const getter=uniqueExport(ui.modules,fn=>{
    const s=Function.prototype.toString.call(fn);
    return s.length<220&&/\.CompositionStateStore;?\}$/.test(s.replace(/\s/g,''));
  });
  const modeGetters=getter?Object.values(getter.exports).filter((f:any)=>typeof f==='function'&&/^(?:\(\)|function\s*\w*\(\))/.test(f.toString())&&f.toString().length<100&&f.toString().includes('.useContext(')):[];
  const useMode=modeGetters.length===1?modeGetters[0] as Function:null;
  const modeEnums=getter?Object.values(getter.exports).filter((v:any)=>v&&typeof v==='object'&&Number.isInteger(v.Full)&&Number.isInteger(v.Overlay)&&Number.isInteger(v.Headless)&&v.Full!==v.Overlay):[];
  const modes=modeEnums.length===1?modeEnums[0] as any:null;
  const capabilities={composition:composition?{module:composition.module,key:composition.key}:null,context:getter?{module:getter.module,key:getter.key}:null,
    modeGetter:!!useMode,modes:modes?{Full:modes.Full,Overlay:modes.Overlay}:null,router:typeof routerHook?.addGlobalComponent==='function'&&typeof routerHook?.removeGlobalComponent==='function',notification:ui.EUIComposition?.Notification===1};
  const available=!!composition&&!!getter&&!!useMode&&!!modes&&capabilities.router&&capabilities.notification;
  const value=(n:number|null|undefined,precision=0)=>n==null?'—':n.toFixed(precision);
  const requestCounts=(store:any)=>{const m=store?.m_mapCompositionStateRequests;return m instanceof Map?Object.fromEntries(m):null;};
  function Lease({store,owner,config,data,appearance}:any){
    // Unconditional hook in this separately mounted component; never switch React hook order.
    (composition!.fn as any)(ui.EUIComposition.Notification,owner);
    React.useEffect(()=>{
      state.trace('lease-mounted',{owner,counts:requestCounts(store),composition:store.GetCompositionState?.()});
      return()=>{state.trace('lease-unmounted',{owner,counts:requestCounts(store),composition:store.GetCompositionState?.()});};
    },[]);
    const fresh=data&&Date.now()-data.collectedAt<=6000;
    const metric=fresh?data:null;
    const source=data&&!fresh?'数据已过期':metric?.source==='ymcc-uniform'?'YMCC 统一接口':metric?.source==='fixture'?'示例数据':'实时数据未连接';
    const paint=probePaint(appearance);
    // Reuse the user's existing H5 corner-block structure; carrier stays the original Steam hook.
    if(appearance.layout==='corner')return React.createElement('div',{
      'data-ymcc-steam-hud-probe':config.runId,'data-hud-layout':'corner',
      'data-background-alpha':appearance.backgroundAlpha,'data-text-alpha':appearance.textAlpha,
      style:{position:'fixed',top:22,left:22,zIndex:7002,pointerEvents:'none',opacity:1,
        color:paint.text,fontFamily:'Segoe UI, Microsoft YaHei UI, sans-serif',fontSize:13,lineHeight:1.4,
        background:paint.background,borderRadius:10,padding:'12px',width:176,maxWidth:'calc(100vw - 44px)',display:'grid',gap:8}},
      React.createElement('div',{style:{display:'flex',justifyContent:'space-between',alignItems:'center'}},
        React.createElement('span',{style:{color:paint.accent,fontWeight:600}},'YMCC'),React.createElement('span',{style:{fontSize:10,color:paint.secondary}},'MONITOR · TEST')),
      React.createElement('div',{style:{display:'flex',alignItems:'baseline',gap:10}},
        React.createElement('span',{style:{fontSize:11,color:paint.accent}},'FPS'),React.createElement('strong',{style:{fontSize:28,fontWeight:600}},value(metric?.fps))),
      ...[['CPU',value(metric?.cpuPowerW,1)+' W / '+value(metric?.cpuTempC)+' °C'],['GPU',value(metric?.gpuUsage)+' %']].map(([label,text])=>
        React.createElement('div',{key:label,style:{display:'flex',justifyContent:'space-between',gap:8}},
          React.createElement('span',{style:{fontSize:11,color:paint.accent}},label),React.createElement('span',null,text))),
      React.createElement('div',{style:{fontSize:10,color:paint.secondary}},source+' · 底板 '+Math.round(appearance.backgroundAlpha*100)+'%'),
      React.createElement('div',{style:{fontSize:10,color:paint.warning}},config.scope==='main-window-test'?'主大屏测试 · 非游戏验收':'游戏内接入测试 · 非硬件控制'));

    return React.createElement('div',{'data-ymcc-steam-hud-probe':config.runId,'data-background-alpha':appearance.backgroundAlpha,'data-text-alpha':appearance.textAlpha,style:{position:'fixed',top:22,left:22,zIndex:7002,pointerEvents:'none',
      opacity:1,color:paint.text,fontFamily:'Segoe UI, Microsoft YaHei UI, sans-serif',fontSize:13,lineHeight:1.4,background:paint.background,borderRadius:12,padding:'10px 14px',maxWidth:'calc(100vw - 44px)',
      display:'flex',gap:18,alignItems:'center',flexWrap:'wrap'}},
      React.createElement('div',null,React.createElement('div',{style:{color:paint.accent,fontWeight:600}},'YMCC · 接入探针'),React.createElement('div',{style:{fontSize:11,color:paint.secondary}},source+' · 背景 '+Math.round(appearance.backgroundAlpha*100)+'%')),
      ...[['FPS',value(metric?.fps)],['CPU',value(metric?.cpuPowerW,1)+' W / '+value(metric?.cpuTempC)+' °C'],['GPU',value(metric?.gpuUsage)+' %']].map(([label,text])=>
        React.createElement('div',{key:label},React.createElement('div',{style:{fontSize:11,color:paint.accent}},label),React.createElement('div',null,text))),
      React.createElement('div',{style:{fontSize:11,color:paint.warning}},config.scope==='main-window-test'?'主大屏挂载测试 · 非游戏验收':'游戏内接入测试 · 非硬件控制'));
  }
  function Root(){
    const [view,setView]=React.useState(state.snapshot),owner=React.useRef('hud-root-'+host.crypto.randomUUID()).current;
    let store:any=null,mode:any=null,error:string|null=null;
    try{store=getter?.fn();mode=useMode?.()?.mode;}catch(e){error=String(e);}
    const instance=store?.m_Instance;
    const info={owner,mode:mode??null,storePresent:!!store,storeName:typeof store?.GetName==='function'?store.GetName():null,
      gameId:instance?.MainRunningAppID??null,windowComposition:typeof instance?.BrowserWindow?.SteamClient?.Window?.SetComposition,
      overlayState:typeof host.SteamClient?.Overlay?.SetOverlayState,counts:requestCounts(store),error};
    React.useEffect(()=>{instances.set(owner,info);stores.set(owner,store);state.trace('root-mounted',info);const release=state.subscribe(()=>setView(state.snapshot()));return()=>{release();instances.delete(owner);stores.delete(owner);state.trace('root-unmounted',{owner});};},[]);
    React.useEffect(()=>{instances.set(owner,info);stores.set(owner,store);},[view,mode,store]);
    const config=view.config;
    const validStore=store&&typeof store.AddMinimumCompositionStateRequest==='function'&&typeof store.RemoveMinimumCompositionStateRequest==='function';
    const allowed=config&&config.expiresAt>Date.now()&&(config.scope==='overlay-only'?mode===modes.Overlay:mode===modes.Full);
    return available&&validStore&&allowed?React.createElement(Lease,{store,owner,config,data:view.data,appearance:view.appearance}):null;
  }
  function stop(runId:string,reason='explicit-stop'){
    const stopped=state.stop(runId,reason);if(!stopped)return false;
    if(deadline){host.clearTimeout(deadline);deadline=null;}if(freshness){host.clearTimeout(freshness);freshness=null;}
    if(registered){const owned=registered;registered=null;try{routerHook.removeGlobalComponent(owned);}catch(e){state.trace('unregister-failed',{error:String(e)});}}
    return true;
  }
  const api={signature:'ymcc-steam-hud-probe-v1',dispose,
    start(config:unknown){
      if(disposed||!available||!state.start(config))return false;
      const v=config as ProbeConfig;registered='YMCCSteamHudProbe-'+v.runId;
      try{routerHook.addGlobalComponent(registered,Root);}catch(e){state.trace('registration-failed',{error:String(e)});stop(v.runId,'registration-failed');return false;}
      deadline=host.setTimeout(()=>stop(v.runId,'expired'),Math.min(MAX_PROBE_MS,Math.max(1,v.expiresAt-Date.now())));return true;
    },
    update(data:unknown){if((data as any)?.source!=='fixture')return false;const ok=state.update(data);if(!ok)return false;if(freshness)host.clearTimeout(freshness);
      freshness=host.setTimeout(()=>{freshness=null;state.trace('data-expired');state.signal();},Math.max(1,6001-(Date.now()-(state.snapshot().data?.collectedAt??Date.now()))));return true;},
    setAppearance(runId:string,value:unknown){return !disposed&&state.setAppearance(runId,value);},
    stop,
    diagnostics(){return {capabilities,available,registered,disposed,view:state.snapshot(),instances:[...instances.values()].map(i=>({...i,counts:requestCounts(stores.get(String(i.owner))),composition:stores.get(String(i.owner))?.GetCompositionState?.(),compositionCache:Object.fromEntries(Object.entries(stores.get(String(i.owner))??{}).filter(([k,v])=>/LastPushed|LatestComposition/.test(k)&&(v==null||['string','number','boolean'].includes(typeof v)))),lastPushed:(()=>{const c=stores.get(String(i.owner))?.m_eLastPushedToWebHelperCompositionState;return c?{mode:c.eCompositionMode,queue:Array.isArray(c.appidCompositionQueue)?[...c.appidCompositionQueue]:null,windowID:c.windowID}:null;})(),route:stores.get(String(i.owner))?.m_Instance?.LocationPathName??null})),traces:state.traces(),counts:state.counts(),timers:{deadline:!!deadline,freshness:!!freshness},hardwareCommands:0,settingsWrites:0,uniformDataConnected:false};},
  };
  Object.defineProperty(host,property,{value:Object.freeze(api),configurable:true,writable:false});
  function dispose(){if(disposed)return;const v=state.snapshot();if(v.config)stop(v.config.runId,'dispose');disposed=true;host.removeEventListener('pagehide',dispose);if(host[property]===api)delete host[property];}
  host.addEventListener('pagehide',dispose);
  return dispose;
}
