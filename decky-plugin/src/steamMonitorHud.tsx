// Formal Steam carrier: the store-plugin SDK/router and Steam's ORIGINAL popup
// hook are reused. No root-document HUD, desktop fallback, HWiNFO or fixtures.
import type { MirrorClient } from './mirrorClient';
import { uniqueExport, isCompositionHook } from './steamHudProbeState';
import { discoverNativePopupCapabilities, checkGameOwner, makeNativePopupPlan, ownNativeChild, releaseUncreatedPopupDescriptor, type OwnedNativeChild } from './steamHudNativeCarrier';
import { admitHudFrame, HUD_LEASE_MS, validHudGameBinding, type HudFrame, type HudGameBinding, type HudClientStatus } from '../../src/bridge/monitorHudProtocol';
import { mountMonitorHud } from '../../src/bridge/monitorHudRenderer';
import { hudCarrierPlacement } from '../../src/bridge/monitorHudViewport';
interface HudDeps {host:any;react:any;reactDom:any;ui:any;routerHook:any;client:MirrorClient;}
export function installSteamMonitorHud({host,react:React,reactDom,ui,routerHook,client}:HudDeps):()=>void {
  const property='__YMCC_STEAM_MONITOR_HUD__';if(host[property])throw Error('Steam monitor HUD already owned');
  const caps=discoverNativePopupCapabilities(ui.modules),portal=reactDom?.createPortal;
  const composition=uniqueExport(ui.modules,isCompositionHook);
  const getter=uniqueExport(ui.modules,fn=>{const s=Function.prototype.toString.call(fn);return s.length<220&&/\.CompositionStateStore;?\}$/.test(s.replace(/\s/g,''));});
  const getters=getter?Object.values(getter.exports).filter((f:any)=>typeof f==='function'&&/^(?:\(\)|function\s*\w*\(\))/.test(f.toString())&&f.toString().length<100&&f.toString().includes('.useContext(')):[];
  const useMode=getters.length===1?getters[0] as Function:null;
  const enums=getter?Object.values(getter.exports).filter((v:any)=>v&&typeof v==='object'&&Number.isInteger(v.Full)&&Number.isInteger(v.Overlay)&&Number.isInteger(v.Headless)&&v.Full!==v.Overlay):[];
  const modes=enums.length===1?enums[0] as any:null;
  const pixelFlags=['IgnoreSteamDisplayScale','DisableDPIScale','NoScalePosition','NoScaleSize'];
  const capable=pixelFlags.every(key=>Number.isSafeInteger(caps.flags?.[key])&&caps.flags![key]>0)&&!!caps.available&&typeof portal==='function'&&!!composition&&!!getter&&!!useMode&&!!modes&&ui.EUIComposition?.Notification===1&&typeof routerHook?.addGlobalComponent==='function'&&typeof routerHook?.removeGlobalComponent==='function';
  const registered='YMCCSteamMonitorHud';
  const roots=new Map<string,{store:any;mode:number;binding:HudGameBinding|null}>();
  const children=new Map<string,{window:any;lease:OwnedNativeChild;renderer:ReturnType<typeof mountMonitorHud>;binding:HudGameBinding;layoutKey:string;prepareViewport:()=>void}>();
  const descriptors=new Map<string,any>(),descriptorBindings=new Map<string,HudGameBinding>(),listeners=new Set<()=>void>();
  const traces:Array<{event:string;at:number;detail?:Record<string,unknown>}>=[];
  let disposed=false,frame:HudFrame|null=null,lastSeq=-1,leaseTimer:any=null,closeTimer:any=null,lastStatus='',protocol=false,closing=false,draining=false;const retiredSessions=new Set<string>();
  const trace=(event:string,detail?:Record<string,unknown>)=>{if(traces.length>=80)traces.shift();traces.push({event,at:Date.now(),detail});};
  const signal=()=>{for(const listener of listeners){try{listener();}catch{}}};
  function liveGame(){const candidates=[...roots.values()].filter(r=>r.mode===modes?.Overlay&&r.binding&&checkGameOwner({carrier:'native-popup',gameBinding:r.binding},r.store).ok);return candidates.length===1?candidates[0]:null;}
  function bindingFrom(store:any):HudGameBinding|null {
    try{
      const i=store?.m_Instance,w=i?.BrowserWindow,b=i?.params?.browserInfo;
      if(!i?.IsGamepadUIOverlayWindow?.()||!w||w.closed||!b)return null;
      const v={appId:Number(i.MainRunningAppID),gameId:String(i.MainRunningApp?.gameid),browserGameId:String(b.m_gameID),overlayTitle:w.document?.title,
        overlayPid:b.m_unPID,overlayBrowserId:b.m_nBrowserID,ownerBrowserId:w.SteamClient?.Browser?.GetBrowserID?.(),overlayBrowserType:b.m_eBrowserType,overlayNativeAppId:b.m_unAppID};
      return validHudGameBinding(v)?v:null;
    }catch{return null;}
  }
  function status():HudClientStatus {
    const remaining=remainingOwned();
    const game=liveGame()?.binding??(children.size?[...children.values()][0].binding:[...descriptorBindings.values()][0]??null);
    const shown=children.size>0||remaining.some((p:any)=>p.window&&!p.window.closed);
    return {capable,game,shown,hiddenConfirmed:children.size===0&&remaining.length===0,session:frame?.session??null,operation:frame?.operation??0,
      reason:capable?game?'Steam 游戏绑定可用':'没有当前游戏的 Steam 叠加层承载':'Steam 原生 popup 能力不可用或有歧义'};
  }
  function report(command:'hud.status'|'hud.ack'='hud.status',ok?:boolean){
    if(!protocol||disposed)return;
    const value=status(),hash=JSON.stringify([command,ok,value]);if(command==='hud.status'&&hash===lastStatus)return;
    if(client.sendHud(command,value,ok))lastStatus=hash;
  }
  function clearLease(){if(leaseTimer!==null)host.clearTimeout(leaseTimer);leaseTimer=null;}
  function closeChildren(reason:string){
    for(const [owner,child]of children){try{child.renderer.dispose();}catch{}try{child.lease.release(reason,true);}catch{}children.delete(owner);}
    for(const [name,popup]of descriptors)releaseUncreatedPopupDescriptor(popup,name,host.g_PopupManager,trace);
  }
  function remainingOwned(){
    // Closing is confirmed by Steam's original popup registry / actual child
    // closed flags, never by merely deleting our JS map.
    try{const names=new Set(descriptors.keys());return [...host.g_PopupManager.GetPopups()].filter((p:any)=>names.has(p.GetName())&&(!p.window||!p.window.closed));}
    catch{return descriptors.size?[...descriptors.values()].filter(p=>p?.window&&!p.window.closed):[];}
  }
  function acknowledgeClose(operation:number,session:string,attempt=0){
    if(disposed||frame?.operation!==operation||frame.session!==session)return;
    if(closeTimer!==null)host.clearTimeout(closeTimer);closeTimer=null;
    if(remainingOwned().length){
      if(attempt>=60){trace('close-not-confirmed');report('hud.ack',false);return;}
      closeTimer=host.setTimeout(()=>acknowledgeClose(operation,session,attempt+1),32);return;
    }
    descriptors.clear();descriptorBindings.clear();closing=false;report('hud.ack',true);
  }
  function waitForOldRelease(attempt=0){
    if(disposed)return;
    if(closeTimer!==null)host.clearTimeout(closeTimer);closeTimer=null;
    if(remainingOwned().length){
      if(attempt>=60){draining=false;trace('old-child-release-unconfirmed');report('hud.ack',false);return;}
      closeTimer=host.setTimeout(()=>waitForOldRelease(attempt+1),32);return;
    }
    descriptors.clear();descriptorBindings.clear();draining=false;closing=false;signal();report();
  }
  function revoke(reason:string,ack=false){
    clearLease();if(closeTimer!==null)host.clearTimeout(closeTimer);closeTimer=null;
    closing=true;closeChildren(reason);signal();
    // With no owned popup, close confirmation needs neither a timer nor a retry.
    if(!remainingOwned().length){if(ack&&frame)acknowledgeClose(frame.operation,frame.session);else waitForOldRelease();return;}
    if(ack&&frame)closeTimer=host.setTimeout(()=>acknowledgeClose(frame!.operation,frame!.session),0);
    else {report();closeTimer=host.setTimeout(()=>waitForOldRelease(),0);}
  }
  function accept(raw:unknown){
    const boot=host.__YMCC_DECKY_MIRROR__;if(!protocol||!boot||disposed)return;
    const value=raw as any;
    if(value?.type==='hud-ready'&&value.protocol===1&&value.runId===boot.runId){lastStatus='';report();return;}
    if(retiredSessions.has(value?.session))return;
    const newSession=frame?.session!==value?.session;
    const next=admitHudFrame(raw,boot.runId,Date.now(),newSession?-1:lastSeq);if(!next)return;
    if(newSession&&frame){retiredSessions.add(frame.session);if(retiredSessions.size>16){trace('session-limit');revoke('session-limit');return;}}
    lastSeq=next.seq;const changed=frame?.session!==next.session;frame=next;
    if(changed&&descriptors.size){closing=true;draining=true;closeChildren('hud-session-changed');closeTimer=host.setTimeout(()=>waitForOldRelease(),0);}
    clearLease();
    if(!next.show){revoke('explicit-hide',true);return;}
    const game=liveGame();if(!game||JSON.stringify(game.binding)!==JSON.stringify(next.game)){revoke('game-owner-unavailable');report('hud.ack',false);return;}
    closing=draining;
    leaseTimer=host.setTimeout(()=>{if(frame?.seq===next.seq){frame={...next,show:false};revoke('data-lease-expired');}},Math.max(1,next.expiresAt-Date.now()));
    for(const child of children.values()){
      if(JSON.stringify(child.binding)!==JSON.stringify(game.binding)||!child.lease.renew?.(next.expiresAt)){revoke('child-owner-or-lease-invalid');report('hud.ack',false);return;}
      const layoutKey=JSON.stringify([next.preferences.ymcc.crosshair,next.preferences.ymcc.position,next.preferences.ymcc.layout,next.preferences.ymcc.scale,next.preferences.ymcc.gap,next.preferences.ymcc.fields,
        child.window.devicePixelRatio,liveGame()?.store?.m_Instance?.BrowserWindow?.innerWidth,liveGame()?.store?.m_Instance?.BrowserWindow?.innerHeight]);
      if(layoutKey!==child.layoutKey){child.layoutKey=layoutKey;child.prepareViewport();}
      child.renderer.update(next.preferences,next.data);
    }
    signal();
    // An existing renderer accepts appearance/data without recreating popup.
    if(children.size)report('hud.ack',true);
  }
  function NativeLease({store,owner,binding}:any){
    (composition!.fn as any)(ui.EUIComposition.Notification,owner);
    const paintContainer=React.useRef(null);
    const ownFrame=frame!;
    const plan=React.useMemo(()=>{
      const p=makeNativePopupPlan({carrier:'native-popup',gameBinding:binding,runId:'product-'+ownFrame.session,expiresAt:ownFrame.expiresAt,popupPin:'creation'},store,caps,owner);
      const dpr=p.owner.devicePixelRatio||1,w=Math.round((p.owner.innerWidth||1280)*dpr),h=Math.round((p.owner.innerHeight||720)*dpr);
      p.params.eCreationFlags=(p.params.eCreationFlags|caps.flags!.IgnoreSteamDisplayScale|caps.flags!.DisableDPIScale|caps.flags!.NoScalePosition|caps.flags!.NoScaleSize)&~caps.flags!.ApplyBrowserScaleToDimensions;
      p.params.dimensions={width:w,height:h,left:12,top:12};
      p.params.minWidth=1;p.params.minHeight=1;p.params.maxWidth=w;p.params.maxHeight=h;
      return p;
    },[ownFrame.session,owner]);
    const callbacks=React.useMemo(()=>({onClose:()=>{trace('native-popup-external-close',{owner});if(frame?.show){frame={...frame,show:false};revoke('native-popup-external-close');}else host.setTimeout(()=>report(),0);}}),[owner]);
    const result=(caps.hook!.fn as any)(plan.name,plan.params,callbacks);
    React.useEffect(()=>{
      if(result.popupObj){descriptors.set(plan.name,result.popupObj);descriptorBindings.set(plan.name,binding);}
      return()=>{releaseUncreatedPopupDescriptor(result.popupObj,plan.name,host.g_PopupManager,trace);};
    },[result.popupObj]);
    React.useEffect(()=>{
      const child=result.popup,element=result.element;
      if(!child||!element||!paintContainer.current)return;
      if(!frame?.show||frame.session!==ownFrame.session||!checkGameOwner({carrier:'native-popup',gameBinding:binding},store).ok){try{child.SteamClient.Window.Close();}catch{}return;}
      let live=true,owned:OwnedNativeChild|null=null,renderer:ReturnType<typeof mountMonitorHud>|null=null,shown=false,lastGeometry='';
      try{
        for(const el of [child.document.documentElement,child.document.body,element])for(const [key,value]of [['background','transparent'],['margin','0'],['padding','0'],['overflow','hidden'],['pointer-events','none']])el.style.setProperty(key,value,'important');
        const api=child.SteamClient?.Window;
        if(['MoveTo','ResizeTo','SetMinSize','SetMaxSize'].some(key=>typeof api?.[key]!=='function'))throw Error('Owned native positioning APIs unavailable');
        const viewport=()=>({width:Math.round((plan.owner.innerWidth||1280)*(plan.owner.devicePixelRatio||1)),height:Math.round((plan.owner.innerHeight||720)*(plan.owner.devicePixelRatio||1))});
        api.SetMinSize(1,1);api.SetMaxSize(viewport().width,viewport().height);
        const prepareViewport=()=>{if(!live)return;const v=viewport();api.SetMaxSize(v.width,v.height);if(paintContainer.current)paintContainer.current.style.width=v.width+'px';renderer?.setViewport(v);};
        // Keep a stable layout capacity; do not flash-resize the popup to full screen.
        prepareViewport();
        renderer=mountMonitorHud(paintContainer.current,{preferences:frame.preferences,data:frame.data,viewport:viewport(),onError:error=>{trace('native-paint-failed',{error:String(error)});if(frame)frame={...frame,show:false};revoke('native-paint-failed');report('hud.ack',false);},onSize:(width,height)=>{
          if(!live||!frame?.show||frame.session!==ownFrame.session)return;
          const viewportWidth=viewport().width,viewportHeight=viewport().height;
          if(width>viewportWidth||height>viewportHeight){trace('hud-viewport-overflow');if(frame)frame={...frame,show:false};revoke('hud-viewport-overflow');report('hud.ack',false);return;}
          const position=hudCarrierPlacement(viewport(),width,height,frame.preferences.ymcc.position,frame.preferences.ymcc.crosshair);
          const geometry=JSON.stringify(position);
          if(geometry!==lastGeometry){api.ResizeTo(position.width,position.height);api.MoveTo(position.left,position.top);lastGeometry=geometry;}
          if(!shown){
            owned=ownNativeChild({child,element,plan:{...plan,expiresAt:frame.expiresAt},checkOwner:()=>!!frame?.show&&frame.session===ownFrame.session&&checkGameOwner({carrier:'native-popup',gameBinding:binding},store).ok,
              onInvalid:reason=>{if(live){trace('native-child-invalid',{reason});if(frame)frame={...frame,show:false};revoke(reason);}},trace});
            children.set(owner,{window:child,lease:owned!,renderer:renderer!,binding,prepareViewport,
              layoutKey:JSON.stringify([frame.preferences.ymcc.crosshair,frame.preferences.ymcc.position,frame.preferences.ymcc.layout,frame.preferences.ymcc.scale,frame.preferences.ymcc.gap,frame.preferences.ymcc.fields,
                child.devicePixelRatio,plan.owner.innerWidth,plan.owner.innerHeight])});shown=true;report('hud.ack',true);
          }
        }});
      }catch(error){trace('native-child-create-failed',{error:String(error)});renderer?.dispose();try{child.SteamClient.Window.Close();}catch{}report('hud.ack',false);}
      return()=>{live=false;renderer?.dispose();owned?.release('react-unmount',true);children.delete(owner);report();};
    },[result.popup,result.element,ownFrame.session,owner]);
    return result.element?portal(React.createElement('div',{ref:paintContainer,'data-ymcc-production-hud-host':ownFrame.session,style:{width:'100%',pointerEvents:'none'}}),result.element):null;
  }
  function Root(){
    const ownerRef=React.useRef(null);if(!ownerRef.current)ownerRef.current='hud-root-product-'+host.crypto.randomUUID();const owner=ownerRef.current;
    const store=(getter!.fn as any)(),mode=(useMode! as any)();
    const [,render]=React.useState(0);const binding=bindingFrom(store),identity=JSON.stringify(binding);
    React.useEffect(()=>{
      roots.set(owner,{store,mode,binding});report();const listener=()=>render((n:number)=>n+1);listeners.add(listener);
      return()=>{listeners.delete(listener);roots.delete(owner);report();};
    },[store,mode,identity]);
    const selected=liveGame(),paint=frame?.show&&frame.expiresAt>Date.now()&&!closing&&selected?.store===store&&mode===modes.Overlay&&binding&&JSON.stringify(binding)===JSON.stringify(frame.game);
    return paint?React.createElement(NativeLease,{key:frame!.session+'-'+binding.overlayPid,store,owner,binding}):null;
  }
  function syncBootstrap(){
    const enabled=host.__YMCC_DECKY_MIRROR__?.hudProtocol===1;
    if(enabled===protocol)return;protocol=enabled;lastStatus='';
    client.setPassiveVisible(enabled);if(!enabled){frame=null;lastSeq=-1;retiredSessions.clear();revoke('host-protocol-unavailable');}
  }
  const offFrame=client.subscribeExtra(accept);
  let previousConnected=false,previousRun='';
  const offClient=client.subscribe(state=>{
    const run=host.__YMCC_DECKY_MIRROR__?.runId??'';
    if(!state.connected){if(previousConnected){frame=null;lastSeq=-1;retiredSessions.clear();revoke('authenticated-link-lost');}previousConnected=false;return;}
    if(!previousConnected||run!==previousRun){frame=null;lastSeq=-1;retiredSessions.clear();lastStatus='';previousRun=run;previousConnected=true;report();}
  }, {connectionOnly:true});
  const bootstrap=()=>{syncBootstrap();};host.addEventListener('ymcc-decky-bootstrap',bootstrap);
  if(capable)routerHook.addGlobalComponent(registered,Root);
  syncBootstrap();
  const diagnostic=Object.freeze({protocol:1,inspect:()=>({capable,capabilities:{popup:caps.available,portal:typeof portal==='function',composition:!!composition,context:!!getter,modeGetter:!!useMode,modes:!!modes,pixelFlags:pixelFlags.every(key=>Number.isSafeInteger(caps.flags?.[key])&&caps.flags![key]>0)},protocol,registered:capable&&!disposed,roots:roots.size,children:children.size,
    visible:children.size>0,frame:frame?{session:frame.session,seq:frame.seq,operation:frame.operation,source:frame.data.source}:null,
    inputPolicy:'Steam pinned notification / NotFocusable / no DOM hit targets',physicalMousePassVerified:false,desktopFallback:false,traces:structuredClone(traces)})});
  Object.defineProperty(host,property,{value:diagnostic,configurable:true,writable:false});
  return()=>{
    if(disposed)return;frame=null;revoke('plugin-unload');disposed=true;
    if(closeTimer!==null)host.clearTimeout(closeTimer);clearLease();offFrame();offClient();host.removeEventListener('ymcc-decky-bootstrap',bootstrap);
    if(capable)routerHook.removeGlobalComponent(registered);listeners.clear();roots.clear();descriptors.clear();descriptorBindings.clear();client.setPassiveVisible(false);
    if(host[property]===diagnostic)delete host[property];
  };
}
