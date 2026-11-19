// Native desktop HUD ownership. Consume the same sample source as Steam;
// native owns foreground/geometry tracking and a renderer-independent deadman.
import type {MouseModePresentation} from './mouseModePresentation';
import { admitHudDragPlacement, type HudDragPlacement } from './monitorHudDrag';
import { normalizeMonitorOutput, switchMonitorHudTemplate, type HudTemplateId, type HudData, type MonitorOutputPreferences } from './monitorHudModel';
import type { DesktopHudFrame } from './desktopHudProtocol';
import { hudWirePreferences, hudWireData } from './monitorHudWire';
import type { OutputAck, MonitorOutputCurrent } from './monitorOutputCoordinator';
export interface DesktopHudTarget {pid:number;processCreated:string;}
export interface DesktopHudEvent {id:number;nonce:string;reason?:string;seq?:number;visible?:boolean;armed?:boolean;visibilityRevision?:number;placement?:unknown;template?:unknown;revision?:unknown;}
export interface DesktopHudOwnerDeps {
  scope?:'global'|'game';target():DesktopHudTarget|null;nonce():string;now():number;sample():HudData;acquire():()=>void;
  // Read the resident YMCC policy owner's protected state; never detect here.
  policyGame?():DesktopHudTarget|null;
  mouseMode?():MouseModePresentation|null;
  visibilityProblem?(reason:'missing'|'hidden',repairs:number):void;
  availabilityRecovered?():void;
  visibilityTerminal?():void;
  visibilityAllowed?():boolean; // Existing host intent/shared-power admission; no detector.
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
  let ended=false,epoch=0,prefs=normalizeMonitorOutput(),nativeAvailable=false,recoveryBlocked=false;
  let owned:{id:number;nonce:string;target:DesktopHudTarget|null;ready:boolean;painted:boolean;prepared:boolean;actualVisible:boolean;visibilityRevision:number;seq:number;sending:boolean;current?:MonitorOutputCurrent;offSource:(()=>void)|null}|null=null;
  let opening:{nonce:string;id:number|null;earlyReady:number|null;epoch:number;timer:unknown;deadline:number;current?:MonitorOutputCurrent;checking?:boolean;releaseClock:()=>void;resolve:(ack:OutputAck)=>void}|null=null;
  let timer:unknown=null,uncertain=true,creating=0,lastSend=-Infinity;
  const visibilityInterval=10000,maxVisibilityRepairs=3;
  let retirementRevision=0;
  let visibilityExpected=false,nextVisibilityCheck=Infinity,visibilityChecking=false,visibilityFailures=0,visibilityRepairs=0,visibilityRecoveryRevision=0;
  const changed=()=>{try{deps.changed?.();}catch{}};
  const expired='MONITOR_OUTPUT_REQUEST_EXPIRED';
  const currentNow=async(current?:MonitorOutputCurrent)=>{try{return !current||await current();}catch{return false;}};
  const scopedArgs=(args:Record<string,unknown>,current?:MonitorOutputCurrent)=>current?.bootContext?{...args,origin:'boot-mode',bootContext:structuredClone(current.bootContext)}:args;
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
    if(owned&&owned.id===own.id&&owned.nonce===own.nonce){++retirementRevision;stopTick();owned=null;}
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
  async function closeOwn(reason:string,current?:MonitorOutputCurrent):Promise<OutputAck>{
    if(current&&!await currentNow(current))return {confirmed:false,reason:expired};
    let releaseClock=()=>{};
    // Keep one control lease across sample release, status and native close.
    try{releaseClock=deps.acquireClock?.()??(()=>{});}catch{}
    try{
    ++epoch;if(owned||creating)uncertain=true;stopTick();settle({confirmed:false,reason});
    const own=owned;
    if(!own){
      if(creating){uncertain=true;return {confirmed:false,reason:'旧独立 HUD 创建请求仍在途，未确认关闭。'};}
      const state=await call<{available:boolean;id:number;nonce?:string}>('desktopHud.status',{}).catch(()=>null);
      if(current&&!await currentNow(current))return {confirmed:false,reason:expired};
      if(state?.id===0){uncertain=false;return {confirmed:true};}
      // Main-renderer recovery: only the native carrier's own descriptor may be
      // reclaimed. A JS owner's empty map is never proof of a closed HWND.
      if(state&&state.id>0&&typeof state.nonce==='string'){
        const closed=await call<boolean>('desktopHud.close',scopedArgs({hudId:state.id,nonce:state.nonce},current),closed=>acknowledgeClose({id:state.id,nonce:state.nonce!},closed)).catch(()=>false);
        acknowledgeClose({id:state.id,nonce:state.nonce},closed);
        if(current&&!await currentNow(current))return {confirmed:false,reason:expired};
        uncertain=!closed;changed();return {confirmed:closed,reason:closed?undefined:'重载前的独立 HUD 关闭未确认。'};
      }
      uncertain=true;return {confirmed:false,reason:'原生独立 HUD 是否存在无法确认。'};
    }
    try{
      if(current&&!await currentNow(current))return {confirmed:false,reason:expired};
      const closed=await call<boolean>('desktopHud.close',scopedArgs({hudId:own.id,nonce:own.nonce},current),closed=>acknowledgeClose(own,closed));
      acknowledgeClose(own,closed);
      if(current&&!await currentNow(current))return {confirmed:false,reason:expired};
      if(!closed){uncertain=true;return {confirmed:false,reason:'原生 HUD 关闭未确认。'};}
      if(owned===own)owned=null;uncertain=false;changed();return {confirmed:true};
    }catch{uncertain=true;changed();return {confirmed:false,reason:'原生 HUD 关闭回执丢失；没有开启另一输出。'};}
    }finally{releaseClock();}
  }
  async function tick(){
    if(ended||!owned?.ready||!owned.offSource||owned.sending)return;
    if(!deps.externalClock)void verifyVisibility();
    const own=owned,at=epoch;own.sending=true;
    try{
      const current=own.current;
      if(current&&!await currentNow(current)){if(owned===own&&own.current===current)await closeOwn(expired);return;}
      if(ended||owned!==own||epoch!==at)return;
      const data=deps.sample();if(data.source!=='ymcc-uniform')throw Error('Fixture is forbidden in desktop product HUD');
      const frame:DesktopHudFrame={type:'desktop-hud',protocol:1,hudId:own.id,nonce:own.nonce,seq:++own.seq,
        expiresAt:deps.now()+6000,show:true,preferences:hudWirePreferences(prefs),data:hudWireData(data,prefs)};
      const mouse=deps.mouseMode?.();if(mouse)frame.mouseMode=mouse;
      if(deps.scope==='global'&&prefs.ymcc.visibility!=='always'){
        const game=deps.policyGame?.();
        frame.policyGame=game?{pid:game.pid,processCreated:game.processCreated}:null;
      }
      lastSend=deps.now();
      const accepted=await call<boolean>('desktopHud.frame',scopedArgs({hudId:own.id,nonce:own.nonce,frame},current));
      if(current&&own.current===current&&!await currentNow(current)){if(owned===own&&own.current===current)await closeOwn(expired);return;}
      if(!accepted&&owned===own){await closeOwn('独立 HUD 数据/目标身份不再有效。');return;}
    }catch{if(owned===own)await closeOwn('独立 HUD 数据发送未确认。');}
    finally{own.sending=false;if(!deps.externalClock&&owned===own&&!ended&&epoch===at)timer=deps.setTimer(()=>{timer=null;void tick();},1000);}
  }
  // A read-only check on the existing native pulse. No resident timer, process
  // scan or extra sensor demand. Two consecutive confirmed mismatches are
  // required; a failed/unknown reply is not permission to destroy a window.
  async function verifyVisibility(){
    const own=owned,at=epoch;
    if(ended||(!own&&!visibilityExpected)||(own&&!own.prepared)||opening||creating||visibilityChecking||deps.now()<nextVisibilityCheck||recoveryBlocked)return;
    nextVisibilityCheck=deps.now()+visibilityInterval;
    if(deps.visibilityAllowed?.()===false){visibilityFailures=0;return;}
    visibilityChecking=true;
    try{
      const state=await call<{available:boolean;id:number;nonce?:string;ready?:boolean;armed?:boolean;visible?:boolean;
        presentationAllowed?:boolean;layoutReady?:boolean;controllerVisible?:boolean|null;recoveryBlocked?:boolean}>('desktopHud.status',{});
      if(ended||owned!==own||epoch!==at||opening||deps.visibilityAllowed?.()===false)return;
      const wasBlocked=recoveryBlocked;recoveryBlocked ||= state.recoveryBlocked===true;
      if(recoveryBlocked||state.available!==true){nativeAvailable=false;visibilityFailures=0;if(recoveryBlocked&&!wasBlocked){try{deps.visibilityTerminal?.();}catch{}}return;}
      const becameAvailable=!nativeAvailable;nativeAvailable=true;
      // Only a positive native readiness edge may reopen exhausted startup
      // attempts. Repeated id=0 is not a refund of failed create operations.
      if(becameAvailable){try{deps.availabilityRecovered?.();}catch{}return;}
      const missing=state.id===0;
      const same=!!own&&state.id===own.id&&state.nonce===own.nonce;
      const hidden=same&&state.presentationAllowed===true&&
        (state.ready===false||state.armed===false||state.visible===false||state.layoutReady===false||state.controllerVisible===false);
      if(!missing&&!hidden){
        visibilityFailures=0;
        // Only a positively healthy matching surface replenishes this separate
        // incident budget. Policy holds and unknown replies are not health.
        if(same&&state.presentationAllowed===true&&state.ready===true&&state.armed===true&&state.visible===true&&state.layoutReady===true&&state.controllerVisible===true)visibilityRepairs=0;
        return;
      }
      if(++visibilityFailures<2)return;
      if(visibilityRepairs>=maxVisibilityRepairs){visibilityFailures=0;return;}
      visibilityFailures=0;++visibilityRepairs;++visibilityRecoveryRevision;
      try{deps.visibilityProblem?.(missing?'missing':'hidden',visibilityRepairs);}catch{}
      // No global hide/show, SetForegroundWindow or unowned close. A missing
      // descriptor needs no native close; a hidden owned one uses normal ACKs.
      if(missing){++epoch;if(own)acknowledgeClose(own,true);else{++retirementRevision;uncertain=false;changed();}}
      else await closeOwn('监控可见性连续两次核验异常。');
    }catch{if(owned===own&&epoch===at)visibilityFailures=0;}
    finally{visibilityChecking=false;}
  }
  function receive(name:string,event:DesktopHudEvent){
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
      owned.current=undefined;
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
      const transition=!owned.painted||uncertain;owned.painted=true;owned.prepared=true;owned.actualVisible=event.visible!==false;if(event.visibilityRevision!==undefined)owned.visibilityRevision=event.visibilityRevision;uncertain=false;owned.current=undefined;settle({confirmed:true});if(transition)changed();
    }
    if(name==='desktopHud.closed'){++retirementRevision;stopTick();owned=null;uncertain=false;settle({confirmed:false,reason:event.reason??'独立 HUD 已关闭。'});changed();}
    if(name==='desktopHud.unavailable'||name==='desktopHud.expired')void closeOwn(event.reason??'独立 HUD 失效。');
  }
  const off=deps.observe((name,event)=>{
    const attempt=opening,own=owned,current=own?.current??attempt?.current;
    if(!current){receive(name,event);return;}
    // A closed receipt always remains usable for descriptor-specific cleanup.
    if(name==='desktopHud.closed'){receive(name,event);return;}
    if(event.nonce!==(own?.nonce??attempt?.nonce)||own&&event.id!==own.id)return;
    const snapshot=structuredClone(event);
    void currentNow(current).then(valid=>{
      if(ended||attempt!==opening||own&&own!==owned)return;
      if(!valid){expireOpening(expired);return;}
      receive(name,snapshot);
    });
  });
  function checkPending(){
    const attempt=opening;if(!attempt?.current||attempt.checking)return;
    attempt.checking=true;
    void currentNow(attempt.current).then(valid=>{
      if(opening===attempt&&!valid)expireOpening(expired);
    }).finally(()=>{attempt.checking=false;});
  }
  return {
    setVisibilityExpected(value:boolean){
      if(visibilityExpected===value)return;visibilityExpected=value;
      nextVisibilityCheck=value?deps.now()+visibilityInterval:Infinity;visibilityFailures=0;
    },
    advance(){
      checkPending();
      void verifyVisibility();
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
      const state=await call<{available:boolean;id:number;recoveryBlocked?:boolean}>('desktopHud.status',{}).catch(()=>null);
      if(!owned&&!creating)uncertain=state?.id!==0;
      recoveryBlocked ||= state?.recoveryBlocked===true;
      nativeAvailable=state?.available===true;return nativeAvailable&&(deps.scope==='global'||!!deps.target());
    },
    visible:()=>!!owned?.actualVisible||uncertain,
    waiting:()=>!uncertain&&!creating&&!!owned?.prepared&&!owned.actualVisible,
    state:():'shown'|'hidden'|'unknown'=>uncertain||creating?'unknown':owned?.actualVisible?'shown':'hidden',
    async setVisible(show:boolean,value:MonitorOutputPreferences,current?:MonitorOutputCurrent):Promise<OutputAck>{
      if(current&&!await currentNow(current))return {confirmed:false,reason:expired};
      if(!show)return closeOwn('监控输出切换。',current);
      if(ended||opening||owned||uncertain||creating)return {confirmed:false,reason:'独立 HUD 仍在创建/关闭或状态不明。'};
      const target=deps.scope==='global'?null:deps.target();if((deps.scope!=='global'&&!target)||!nativeAvailable)return {confirmed:false,reason:'原生桌面 HUD 不可用。'};
      const nonce=deps.nonce();if(!/^[a-zA-Z0-9-]{16,80}$/.test(nonce))throw Error('Invalid desktop HUD nonce');
      let releaseClock:()=>void;
      try{releaseClock=deps.acquireClock?.()??(()=>{});}catch{return {confirmed:false,reason:'原生 HUD 时钟不可用。'};}
      const at=++epoch;creating=at;prefs={...normalizeMonitorOutput(value),output:'ymcc',enabled:true};
      return new Promise(resolve=>{
        const timeout=deps.setTimer(()=>{if(opening?.epoch===at)expireOpening();},12000);
        opening={nonce,id:null,earlyReady:null,epoch:at,timer:timeout,deadline:deps.now()+12000,current,releaseClock,resolve};
        void call<{id:number;nonce:string}>('desktopHud.create',scopedArgs(deps.scope==='global'?{scope:'global',nonce}:{...target,nonce},current),result=>reclaim(result,nonce),()=>{if(creating===at)creating=0;}).then(async result=>{
          if(!Number.isSafeInteger(result.id)||result.id<1||result.nonce!==nonce)throw Error('Invalid native HUD creation receipt');
          if(ended||epoch!==at||opening?.epoch!==at){reclaim(result,nonce);return;}
          // Keep the returned descriptor in this continuation while checking;
          // expiry must reclaim it even if the owner read throws or is late.
          if(current&&!await currentNow(current)){
            reclaim(result,nonce);if(opening?.epoch===at)expireOpening(expired);return;
          }
          if(ended||epoch!==at||opening?.epoch!==at){reclaim(result,nonce);return;}
          nextVisibilityCheck=deps.now()+visibilityInterval;visibilityFailures=0;
          owned={id:result.id,nonce,target,ready:false,painted:false,prepared:false,actualVisible:false,visibilityRevision:0,seq:0,sending:false,current,offSource:null};opening.id=result.id;
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
    inspect:()=>({nativeAvailable,recoveryBlocked,retirementRevision,visibilityRecoveryRevision,visibilityHealth:{expected:visibilityExpected,intervalMs:visibilityInterval,checking:visibilityChecking,failures:visibilityFailures,repairs:visibilityRepairs,maxRepairs:maxVisibilityRepairs},opening:!!opening,creating:!!creating,owned:owned?{id:owned.id,scope:deps.scope??'game',target:owned.target?{...owned.target}:null,ready:owned.ready,painted:owned.painted,prepared:owned.prepared,actualVisible:owned.actualVisible}:null,uncertain,timer:timer!==null,pendingCalls:calls.size}),
  };
}
