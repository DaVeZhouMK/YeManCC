// Native desktop HUD ownership. Consume the same sample source as Steam;
// native owns foreground/geometry tracking and a renderer-independent deadman.
import type {MouseModePresentation} from './mouseModePresentation';
import { admitHudDragPlacement, type HudDragPlacement } from './monitorHudDrag';
import { normalizeMonitorOutput, switchMonitorHudTemplate, type HudTemplateId, type HudData, type MonitorOutputPreferences } from './monitorHudModel';
import type { DesktopHudFrame } from './desktopHudProtocol';
import { hudWirePreferences, hudWireData } from './monitorHudWire';
import type { OutputAck } from './monitorOutputCoordinator';
export interface DesktopHudTarget {pid:number;processCreated:string;}
export interface DesktopHudEvent {id:number;nonce:string;reason?:string;seq?:number;visible?:boolean;armed?:boolean;visibilityRevision?:number;placement?:unknown;template?:unknown;revision?:unknown;}
export interface DesktopHudOwnerDeps {
  scope?:'global'|'game';target():DesktopHudTarget|null;nonce():string;now():number;sample():HudData;acquire():()=>void;
  // Read the resident YMCC policy owner's protected state; never detect here.
  policyGame?():DesktopHudTarget|null;
  mouseMode?():MouseModePresentation|null;
  invoke<T>(command:string,args:unknown):Promise<T>;
  observe(listener:(name:string,event:DesktopHudEvent)=>void):()=>void;
  setTimer(fn:()=>void,ms:number):unknown;clearTimer(id:unknown):void;
  placementChanged?(value:HudDragPlacement):void;
  templateChanged?(template:HudTemplateId,revision:number):void;
  changed?():void;
  externalClock?:boolean;
  acquireClock?():()=>void;
}
export function createDesktopHudOwner(deps:DesktopHudOwnerDeps){
  let ended=false,epoch=0,prefs=normalizeMonitorOutput(),nativeAvailable=false;
  let owned:{id:number;nonce:string;target:DesktopHudTarget|null;ready:boolean;painted:boolean;prepared:boolean;actualVisible:boolean;visibilityRevision:number;seq:number;sending:boolean;offSource:(()=>void)|null}|null=null;
  let opening:{nonce:string;id:number|null;earlyReady:number|null;epoch:number;timer:unknown;deadline:number;releaseClock:()=>void;resolve:(ack:OutputAck)=>void}|null=null;
  let timer:unknown=null,uncertain=true,creating=0,lastSend=-Infinity;
  const changed=()=>{try{deps.changed?.();}catch{}};
  function stopTick(){if(timer!==null)deps.clearTimer(timer);timer=null;owned?.offSource?.();if(owned)owned.offSource=null;}
  function settle(ack:OutputAck){if(!opening)return;const p=opening;opening=null;deps.clearTimer(p.timer);p.releaseClock();p.resolve(ack);}
  // Owner-bounded native calls. The main IPC must retain late creation receipts:
  // browser-only IPC timeouts would discard the id needed for precise cleanup.
  const calls=new Set<{deadline:number;expire:(reason:string)=>void}>();
  function call<T>(command:string,args:unknown,late?:(value:T)=>void,rawSettled?:()=>void):Promise<T>{
    let releaseClock=()=>{},clockError:string|undefined;
    try{releaseClock=deps.acquireClock?.()??(()=>{});}catch{clockError='原生 HUD 时钟不可用。';}
    return new Promise<T>((resolve,reject)=>{
      let done=false,fallback:unknown=null;
      const request={deadline:deps.now()+5000,expire:(reason:string)=>{if(finish())reject(Error(reason));}};
      function finish(){if(done)return false;done=true;calls.delete(request);if(fallback!==null)deps.clearTimer(fallback);releaseClock();return true;}
      calls.add(request);
      fallback=deps.setTimer(()=>request.expire('原生 HUD 请求超时：'+command),5000);
      if(clockError)request.expire(clockError);
      // Even on clock failure a close may be sent best-effort, but only its
      // actual id/nonce-bound reply (not dispatch) can establish hidden state.
      void deps.invoke<T>(command,args).then(value=>{
        rawSettled?.();if(finish())resolve(value);else late?.(value);
      }).catch(error=>{rawSettled?.();if(finish())reject(error);});
    });
  }
  function acknowledgeClose(own:{id:number;nonce:string},closed:boolean){
    if(!closed)return;
    if(owned&&owned.id===own.id&&owned.nonce===own.nonce){stopTick();owned=null;}
    if(!owned&&!creating){uncertain=false;changed();}
  }
  function reclaim(result:{id:number;nonce:string},nonce:string){
    if(!result||!Number.isSafeInteger(result.id)||result.id<1||result.nonce!==nonce)return;
    uncertain=true;
    void deps.invoke<boolean>('desktopHud.close',{hudId:result.id,nonce}).then(closed=>acknowledgeClose(result,closed)).catch(()=>{});
  }
  function expireOpening(reason='独立 HUD 创建/绘制回执超时。'){
    if(!opening)return;
    uncertain=true;settle({confirmed:false,reason});void closeOwn(reason);changed();
  }
  async function closeOwn(reason:string):Promise<OutputAck>{
    let releaseClock=()=>{};
    // Keep one control lease across sample release, status and native close.
    try{releaseClock=deps.acquireClock?.()??(()=>{});}catch{}
    try{
    ++epoch;if(owned||creating)uncertain=true;stopTick();settle({confirmed:false,reason});
    const own=owned;
    if(!own){
      if(creating){uncertain=true;return {confirmed:false,reason:'旧独立 HUD 创建请求仍在途，未确认关闭。'};}
      const state=await call<{available:boolean;id:number;nonce?:string}>('desktopHud.status',{}).catch(()=>null);
      if(state?.id===0){uncertain=false;return {confirmed:true};}
      // Main-renderer recovery: only the native carrier's own descriptor may be
      // reclaimed. A JS owner's empty map is never proof of a closed HWND.
      if(state&&state.id>0&&typeof state.nonce==='string'){
        const closed=await call<boolean>('desktopHud.close',{hudId:state.id,nonce:state.nonce},closed=>acknowledgeClose({id:state.id,nonce:state.nonce!},closed)).catch(()=>false);
        uncertain=!closed;changed();return {confirmed:closed,reason:closed?undefined:'重载前的独立 HUD 关闭未确认。'};
      }
      uncertain=true;return {confirmed:false,reason:'原生独立 HUD 是否存在无法确认。'};
    }
    try{
      const closed=await call<boolean>('desktopHud.close',{hudId:own.id,nonce:own.nonce},closed=>acknowledgeClose(own,closed));
      if(!closed){uncertain=true;return {confirmed:false,reason:'原生 HUD 关闭未确认。'};}
      if(owned===own)owned=null;uncertain=false;changed();return {confirmed:true};
    }catch{uncertain=true;changed();return {confirmed:false,reason:'原生 HUD 关闭回执丢失；没有开启另一输出。'};}
    }finally{releaseClock();}
  }
  async function tick(){
    if(ended||!owned?.ready||!owned.offSource||owned.sending)return;
    const own=owned,at=epoch;own.sending=true;
    try{
      const data=deps.sample();if(data.source!=='ymcc-uniform')throw Error('Fixture is forbidden in desktop product HUD');
      const frame:DesktopHudFrame={type:'desktop-hud',protocol:1,hudId:own.id,nonce:own.nonce,seq:++own.seq,
        expiresAt:deps.now()+6000,show:true,preferences:hudWirePreferences(prefs),data:hudWireData(data,prefs)};
      const mouse=deps.mouseMode?.();if(mouse)frame.mouseMode=mouse;
      if(deps.scope==='global'&&prefs.ymcc.visibility!=='always'){
        const game=deps.policyGame?.();
        frame.policyGame=game?{pid:game.pid,processCreated:game.processCreated}:null;
      }
      lastSend=deps.now();
      const accepted=await call<boolean>('desktopHud.frame',{hudId:own.id,nonce:own.nonce,frame});
      if(!accepted&&owned===own){await closeOwn('独立 HUD 数据/目标身份不再有效。');return;}
    }catch{if(owned===own)await closeOwn('独立 HUD 数据发送未确认。');}
    finally{own.sending=false;if(!deps.externalClock&&owned===own&&!ended&&epoch===at)timer=deps.setTimer(()=>{timer=null;void tick();},1000);}
  }
  const off=deps.observe((name,event)=>{
    if(ended)return;
    if(name==='desktopHud.ready'&&opening?.nonce===event.nonce){
      if(opening.id===null){opening.earlyReady=event.id;return;}
      if(owned?.id===event.id&&owned.nonce===event.nonce&&!owned.ready){owned.ready=true;if(!owned.offSource)owned.offSource=deps.acquire();void tick();}
    }
    if(!owned||owned.id!==event.id||owned.nonce!==event.nonce)return;
    if(name==='desktopHud.template'&&owned.ready&&typeof event.seq==='number'&&Number.isSafeInteger(event.seq)&&event.seq>0&&event.seq<=owned.seq){
      const revision=event.revision;
      if((event.template==='A'||event.template==='B')&&typeof revision==='number'&&Number.isSafeInteger(revision)&&revision>(prefs.ymccTemplateRevision??0)){
        prefs=switchMonitorHudTemplate(prefs,event.template,revision);deps.templateChanged?.(event.template,revision);void tick();
      }
      return;
    }
    if(name==='desktopHud.placement'&&owned.ready&&typeof event.seq==='number'&&Number.isSafeInteger(event.seq)&&event.seq>0&&event.seq<=owned.seq){
      const p=admitHudDragPlacement(event.placement);
      if(p&&p.dragRevision>(prefs.ymcc.dragRevision??0)){prefs={...prefs,ymcc:{...prefs.ymcc,...p}};deps.placementChanged?.(p);}
      return;
    }
    const visibilityReceipt=owned.ready&&owned.offSource&&typeof event.seq==='number'&&Number.isSafeInteger(event.seq)&&event.seq>0&&event.seq<=owned.seq&&
      typeof event.visibilityRevision==='number'&&Number.isSafeInteger(event.visibilityRevision)&&event.visibilityRevision>owned.visibilityRevision;
    if(name==='desktopHud.prepared'&&visibilityReceipt&&prefs.ymcc.visibility!=='always'&&event.armed===true&&event.visible===false){
      const transition=!owned.prepared||owned.actualVisible||uncertain;owned.prepared=true;owned.actualVisible=false;owned.visibilityRevision=event.visibilityRevision!;uncertain=false;
      settle({confirmed:true,reason:'自定义监控已就绪，等待所选游戏显示条件。'});if(transition)changed();
    }
    if(name==='desktopHud.visibility'&&visibilityReceipt&&owned.prepared&&event.armed===true&&event.visible===true){
      const transition=!owned.actualVisible||uncertain;owned.actualVisible=true;owned.visibilityRevision=event.visibilityRevision!;uncertain=false;if(transition)changed();
    }
    if(name==='desktopHud.painted'&&owned.ready&&owned.offSource&&typeof event.seq==='number'&&Number.isSafeInteger(event.seq)&&event.seq>0&&event.seq<=owned.seq){
      if(event.armed===false||(event.visible!==undefined&&typeof event.visible!=='boolean'))return;
      // Once native visibility has a revision, a legacy/invalid paint cannot
      // overwrite it or poison comparisons used by future focus receipts.
      if(event.visibilityRevision===undefined ? owned.visibilityRevision>0 :
        !Number.isSafeInteger(event.visibilityRevision)||event.visibilityRevision<0||event.visibilityRevision<owned.visibilityRevision)return;
      // A later size/paint receipt is transport progress, not an availability
      // transition. Repeated success must not restart host status/clock calls.
      const transition=!owned.painted||uncertain;owned.painted=true;owned.prepared=true;owned.actualVisible=event.visible!==false;if(event.visibilityRevision!==undefined)owned.visibilityRevision=event.visibilityRevision;uncertain=false;settle({confirmed:true});if(transition)changed();
    }
    if(name==='desktopHud.closed'){stopTick();owned=null;uncertain=false;settle({confirmed:false,reason:event.reason??'独立 HUD 已关闭。'});changed();}
    if(name==='desktopHud.unavailable'||name==='desktopHud.expired')void closeOwn(event.reason??'独立 HUD 失效。');
  });
  return {
    advance(){
      for(const request of [...calls])if(deps.now()>=request.deadline)request.expire('原生 HUD 请求回执超时。');
      if(opening&&deps.now()>=opening.deadline)expireOpening();
      if(deps.externalClock&&deps.now()-lastSend>=(prefs.ymcc.fields.frametimeGraph?200:1000))void tick();
    },
    clockUnavailable(reason:string){
      const hadOutput=!!owned||!!opening||creating!==0;
      ++epoch;uncertain=true;stopTick();settle({confirmed:false,reason});
      for(const request of [...calls])request.expire(reason);
      if(owned)reclaim({id:owned.id,nonce:owned.nonce},owned.nonce);
      // A failing availability probe must not invalidate itself forever.
      if(hadOutput)changed();
    },
    async available(){
      if(ended)return false;
      const state=await call<{available:boolean;id:number}>('desktopHud.status',{}).catch(()=>null);
      if(!owned&&!creating)uncertain=state?.id!==0;
      nativeAvailable=state?.available===true;return nativeAvailable&&(deps.scope==='global'||!!deps.target());
    },
    visible:()=>!!owned?.actualVisible||uncertain,
    waiting:()=>!uncertain&&!creating&&!!owned?.prepared&&!owned.actualVisible,
    state:():'shown'|'hidden'|'unknown'=>uncertain||creating?'unknown':owned?.actualVisible?'shown':'hidden',
    async setVisible(show:boolean,value:MonitorOutputPreferences):Promise<OutputAck>{
      if(!show)return closeOwn('监控输出切换。');
      if(ended||opening||owned||uncertain||creating)return {confirmed:false,reason:'独立 HUD 仍在创建/关闭或状态不明。'};
      const target=deps.scope==='global'?null:deps.target();if((deps.scope!=='global'&&!target)||!nativeAvailable)return {confirmed:false,reason:'原生桌面 HUD 不可用。'};
      const nonce=deps.nonce();if(!/^[a-zA-Z0-9-]{16,80}$/.test(nonce))throw Error('Invalid desktop HUD nonce');
      let releaseClock:()=>void;
      try{releaseClock=deps.acquireClock?.()??(()=>{});}catch{return {confirmed:false,reason:'原生 HUD 时钟不可用。'};}
      const at=++epoch;creating=at;prefs={...normalizeMonitorOutput(value),output:'ymcc',enabled:true};
      return new Promise(resolve=>{
        const timeout=deps.setTimer(()=>{if(opening?.epoch===at)expireOpening();},12000);
        opening={nonce,id:null,earlyReady:null,epoch:at,timer:timeout,deadline:deps.now()+12000,releaseClock,resolve};
        void call<{id:number;nonce:string}>('desktopHud.create',deps.scope==='global'?{scope:'global',nonce}:{...target,nonce},result=>reclaim(result,nonce),()=>{if(creating===at)creating=0;}).then(async result=>{
          if(!Number.isSafeInteger(result.id)||result.id<1||result.nonce!==nonce)throw Error('Invalid native HUD creation receipt');
          if(ended||epoch!==at||opening?.epoch!==at){reclaim(result,nonce);return;}
          owned={id:result.id,nonce,target,ready:false,painted:false,prepared:false,actualVisible:false,visibilityRevision:0,seq:0,sending:false,offSource:null};opening.id=result.id;
          if(opening.earlyReady===result.id){owned.ready=true;owned.offSource=deps.acquire();void tick();}
        }).catch(()=>{if(opening?.epoch===at){uncertain=true;settle({confirmed:false,reason:'原生 HUD 创建失败或回执丢失。'});void closeOwn('原生 HUD 创建失败。');changed();}});
      });
    },
    updateAppearance(value:MonitorOutputPreferences){if(owned||opening){
      let normalized=normalizeMonitorOutput(value);
      if((prefs.ymccTemplateRevision??0)>(normalized.ymccTemplateRevision??0))return;
      let next=normalized.ymcc;
      if((prefs.ymcc.dragRevision??0)>(next.dragRevision??0)){const p=admitHudDragPlacement(prefs.ymcc);if(p)next={...next,...p};}
      prefs={...normalized,output:prefs.output,enabled:prefs.enabled,ymcc:next};
    }},
    async dispose(){if(ended)return;const ack=await closeOwn('YMCC 输出所有者释放。');ended=true;off();return ack;},
    inspect:()=>({nativeAvailable,opening:!!opening,creating:!!creating,owned:owned?{id:owned.id,scope:deps.scope??'game',target:owned.target?{...owned.target}:null,ready:owned.ready,painted:owned.painted,prepared:owned.prepared,actualVisible:owned.actualVisible}:null,uncertain,timer:timer!==null,pendingCalls:calls.size}),
  };
}
