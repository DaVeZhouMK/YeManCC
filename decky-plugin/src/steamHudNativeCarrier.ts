// Opt-in carrier adapter. Reuses Steam's original generic popup hook/factory and
// React portal; does not copy Steam implementation or create a desktop fallback.
import { uniqueExport,type ProbeConfig,type ProbeGameBinding } from './steamHudProbeState';
export interface NativePopupCapabilities {
  hook:{fn:Function;module:string;key:string}|null;
  flagsFactory:{fn:Function;module:string;key:string}|null;
  flags:Record<string,number>|null;
  notificationKind:number|null;
  available:boolean;
}
export function isNativePopupHook(fn:unknown):boolean {
  if(typeof fn!=='function')return false;const s=Function.prototype.toString.call(fn);
  return ['bNoInitialShow','bNoFocusOnShow','SetHideOnClose','.Close()'].every(k=>s.includes(k));
}
export function isNativePopupFlagFactory(fn:unknown):boolean {
  if(typeof fn!=='function')return false;const s=Function.prototype.toString.call(fn);
  return ['NotFocusable','TransparentParentWindow','ForceBrowserVisible','Notification'].every(k=>s.includes(k));
}
const requiredFlags=['NotFocusable','TransparentParentWindow','ForceBrowserVisible','NoTaskbarIcon','NoWindowShadow','NoRoundedCorners','Hidden','ApplyBrowserScaleToDimensions'] as const;
export function discoverNativePopupCapabilities(modules:unknown):NativePopupCapabilities {
  const hook=uniqueExport(modules,isNativePopupHook),factory=uniqueExport(modules,isNativePopupFlagFactory);
  const exports=factory?.exports??{};
  const enums=Object.values(exports).filter((v:any)=>v&&typeof v==='object'&&requiredFlags.every(k=>Number.isSafeInteger(v[k])&&v[k]>0));
  const kinds=Object.values(exports).filter((v:any)=>v&&typeof v==='object'&&Number.isInteger(v.Notification)&&Number.isInteger(v.Tooltip)&&Number.isInteger(v.StandaloneContextMenu));
  const flags=enums.length===1?enums[0] as Record<string,number>:null;
  const notificationKind=kinds.length===1?(kinds[0] as any).Notification:null;
  const caps={hook,flagsFactory:factory,flags,notificationKind,available:!!hook&&!!factory&&!!flags&&notificationKind!==null};
  if(caps.available){
    try{const bits=Number(factory!.fn(notificationKind));caps.available=Number.isSafeInteger(bits)&&bits>=0&&
      ['NotFocusable','TransparentParentWindow','ForceBrowserVisible'].every(k=>(bits&flags![k])===flags![k]);}catch{caps.available=false;}
  }
  return caps;
}
export function checkGameOwner(config:Pick<ProbeConfig,'carrier'|'gameBinding'>,store:any):{ok:boolean;reason:string} {
 try{
  const b=config.gameBinding;
  if(config.carrier!=='native-popup'||!b)return {ok:false,reason:'native-popup binding required'};
  const instance=store?.m_Instance,owner=instance?.BrowserWindow,info=instance?.params?.browserInfo;
  if(!owner||owner.closed||!info)return {ok:false,reason:'game owner unavailable'};
  if(typeof instance.IsGamepadUIOverlayWindow!=='function'||!instance.IsGamepadUIOverlayWindow())return {ok:false,reason:'not a game overlay owner'};
  if(String(instance.MainRunningAppID)!==String(b.appId))return {ok:false,reason:'running app changed'};
  if(String(instance.MainRunningApp?.gameid)!==b.gameId)return {ok:false,reason:'running game changed'};
  if(owner.document?.title!==b.overlayTitle)return {ok:false,reason:'overlay title changed'};
  if(info.m_unPID!==b.overlayPid||info.m_nBrowserID!==b.overlayBrowserId||info.m_eBrowserType!==b.overlayBrowserType||
    info.m_unAppID!==b.overlayNativeAppId||String(info.m_gameID)!==b.browserGameId)return {ok:false,reason:'native browser identity changed'};
  if(typeof owner.SteamClient?.Browser?.GetBrowserID!=='function'||owner.SteamClient.Browser.GetBrowserID()!==b.ownerBrowserId)
    return {ok:false,reason:'owner browser ID mismatch'};
  return {ok:true,reason:'matched game owner'};
 }catch(e){return {ok:false,reason:'owner validation failed: '+String(e)};}
}
export interface NativePopupPlan {name:string;params:any;binding:ProbeGameBinding;expiresAt:number;pin:boolean;owner:any;}
export function makeNativePopupPlan(config:Pick<ProbeConfig,'carrier'|'gameBinding'|'runId'|'expiresAt'|'popupPin'>,store:any,caps:NativePopupCapabilities,ownerKey:string):NativePopupPlan {
  if(!caps.available)throw Error('Original native popup capabilities unavailable/ambiguous');
  const guard=checkGameOwner(config,store);if(!guard.ok)throw Error(guard.reason);
  if(!/^hud-root-[a-zA-Z0-9-]+$/.test(ownerKey))throw Error('Unique owned root identity required');
  const flags=caps.flags!,bits=Number(caps.flagsFactory!.fn(caps.notificationKind))|flags.Hidden|flags.ApplyBrowserScaleToDimensions;
  const source=store.m_Instance.params.browserInfo,owner=store.m_Instance.BrowserWindow;
  // Freeze identity values instead of carrying getter-backed mutable browserInfo.
  // Match the original OverlayPopup: native factory uses its SharedJSContext opener,
  // while target_browser routes to the game. Overriding owner_window can block CEF creation.
  const target_browser={m_unPID:source.m_unPID,m_nBrowserID:source.m_nBrowserID,m_eBrowserType:source.m_eBrowserType,
    m_unAppID:source.m_unAppID,m_gameID:source.m_gameID};
  const name='YMCCSteamHudChild-'+config.runId+'-'+ownerKey;
  return {name,binding:{...config.gameBinding!},expiresAt:config.expiresAt,pin:config.popupPin!=='none',owner,
    params:{title:name,target_browser,bPinned:config.popupPin!=='none',
      eCreationFlags:bits,bNoFocusOnShow:true,bNoInitialShow:false,bHideOnClose:false,
      replace_existing_popup:false,dimensions:{width:232,height:218,left:22,top:52},
      minWidth:232,minHeight:218,maxWidth:232,maxHeight:218,
      html_class:'ymcc-native-hud-child',body_class:'ymcc-native-hud-child',popup_class:'ymcc-native-hud-child'}};
}
export interface OwnedNativeChild {
  release(reason?:string,close?:boolean):void;
  inspect():Record<string,unknown>;
  renew?(expiresAt:number):boolean;
}
// Called only for an OWNED child returned by the original hook, after portal commit.
// Separate child-local deadline survives an interrupted parent test harness.
export function ownNativeChild({child,element,plan,checkOwner,onInvalid,trace,now=()=>Date.now()}:{
  child:any;element:any;plan:NativePopupPlan;checkOwner:()=>boolean;onInvalid:(reason:string)=>void;
  trace:(event:string,detail?:Record<string,unknown>)=>void;now?:()=>number;
}):OwnedNativeChild {
  if(child===plan.owner||element?.ownerDocument?.defaultView!==child||child.document?.title!==plan.name)
    throw Error('Never manage the overlay root or another popup');
  const api=child.SteamClient,required=['ShowWindow','Close','SetKeyFocus'];
  if(required.some(k=>typeof api?.Window?.[k]!=='function')||typeof api?.Overlay?.SetWindowAlpha!=='function'||
    plan.pin&&typeof api?.Overlay?.SetWindowPinned!=='function')throw Error('Required owned-child native APIs absent');
  if(!checkOwner())throw Error('Game owner no longer matches');
  if(plan.expiresAt<=now())throw Error('Native child deadline elapsed');
  let released=false,deadline:any=null,visibleRequested=false;
  const failure=(reason:string)=>{release(reason,true);onInvalid(reason);};
  const ownerGone=()=>failure('game-owner-unload');
  const childGone=()=>{release('child-unload',false);onInvalid('child-unload');};
  const ownerVisibility=()=>{if(!checkOwner())failure('game-owner-changed');};
  function release(reason='release',close=false){
    if(released)return;released=true;
    if(deadline!==null){child.clearTimeout(deadline);deadline=null;}
    plan.owner.removeEventListener('pagehide',ownerGone);plan.owner.removeEventListener('beforeunload',ownerGone);
    plan.owner.document.removeEventListener('visibilitychange',ownerVisibility);
    child.removeEventListener('pagehide',childGone);child.removeEventListener('beforeunload',childGone);
    if(!child.closed&&reason!=='child-unload'){
      if(plan.pin)try{api.Overlay.SetWindowPinned(false);}catch(e){trace('native-child-unpin-failed',{reason,error:String(e)});}
      if(close)try{api.Window.Close();}catch(e){trace('native-child-close-failed',{reason,error:String(e)});}
    }
    trace('native-child-released',{name:plan.name,reason,closeRequested:close});
  }
  try{
    // Only our child receives these overrides. No root CSS, menu hiding or saved pin config.
    for(const el of [child.document.documentElement,child.document.body,element]){
      if(!el?.style)throw Error('Owned child paint roots absent');
      for(const [key,value] of [['background','transparent'],['margin','0'],['padding','0'],['overflow','hidden']])el.style.setProperty(key,value,'important');
    }
    api.Overlay.SetWindowAlpha(1);api.Window.SetKeyFocus(false);
    plan.owner.addEventListener('pagehide',ownerGone);plan.owner.addEventListener('beforeunload',ownerGone);
    plan.owner.document.addEventListener('visibilitychange',ownerVisibility);
    child.addEventListener('pagehide',childGone);child.addEventListener('beforeunload',childGone);
    deadline=child.setTimeout(()=>failure('child-deadline'),Math.max(1,plan.expiresAt-now()));
    api.Window.ShowWindow();visibleRequested=true;
    trace('native-child-shown-request',{name:plan.name,pin:plan.pin,flags:plan.params.eCreationFlags,hardwareCommands:0});
  }catch(e){release('child-start-failed',true);throw e;}
  return {release,renew(expiresAt:number){
    if(released||!Number.isFinite(expiresAt)||expiresAt<=now()||expiresAt>now()+8000||!checkOwner())return false;
    if(deadline!==null)child.clearTimeout(deadline);
    plan.expiresAt=expiresAt;deadline=child.setTimeout(()=>failure('child-deadline'),Math.max(1,expiresAt-now()));return true;
  },inspect(){
    let dom:any=null;try{const h=child.document.querySelector('[data-ymcc-steam-hud-probe]'),c=h?child.getComputedStyle(h):null;
      dom={count:h?1:0,text:h?.innerText??null,rect:h?.getBoundingClientRect().toJSON()??null,
        background:c?.backgroundColor??null,color:c?.color??null,opacity:c?.opacity??null,pointer:c?.pointerEvents??null};}catch(e){dom={error:String(e)};}
    return {name:plan.name,binding:{...plan.binding},released,deadline:deadline!==null,visibleRequested,
      closed:!!child.closed,hidden:child.document?.hidden??null,dom};
  }};
}
// Clean only our own descriptor when the original CEF creation returned no window.
// Original popup-hook Close cannot emit unload for a window that never existed.
export function releaseUncreatedPopupDescriptor(popupObj:any,expectedName:string,manager:any,
  trace:(event:string,detail?:Record<string,unknown>)=>void):boolean {
  try{
    if(!popupObj||popupObj.window||typeof popupObj.GetName!=='function'||
      typeof manager?.GetExistingPopup!=='function'||typeof manager?.RemoveTrackedPopup!=='function')return false;
    const name=popupObj.GetName();
    if(name!==expectedName&&!name.startsWith(expectedName+'_uid'))return false;
    if(manager.GetExistingPopup(name)!==popupObj)return false;
    manager.RemoveTrackedPopup(popupObj);trace('native-child-uncreated-descriptor-released',{name});return true;
  }catch(e){trace('native-child-descriptor-cleanup-failed',{error:String(e)});return false;}
}
