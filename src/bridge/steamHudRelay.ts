// Passive display relay. Only original YMCC owners collect data and write controls.
import type { MirrorPeer, MirrorRequest } from './deckyMirrorRelay';
import { HUD_PROTOCOL, HUD_LEASE_MS, validHudStatus, type HudClientStatus, type HudFrame, type HudReady } from './monitorHudProtocol';
import { hudFromUniformSnapshot, normalizeMonitorOutput, type HudData, type MonitorOutputPreferences } from './monitorHudModel';
import type { OutputAck } from './monitorOutputCoordinator';
import { hudWirePreferences, hudWireData } from './monitorHudWire';
export interface SteamHudRelayDeps {
  now():number;session():string;
  reply(connection:string,message:HudFrame|HudReady):Promise<boolean>;
  sample():HudData;acquire():()=>void;
  setTimer(fn:()=>void,ms:number):unknown;clearTimer(id:unknown):void;
  statusChanged?():void;
  externalClock?:boolean;
  acquireClock?():()=>void;
}
export function createSteamHudRelay(deps:SteamHudRelayDeps) {
  let peer:MirrorPeer|null=null,status:HudClientStatus|null=null,session='',seq=0,operation=0,operationGame='';
  let hiddenKnown=false,absent=false;
  let wanted=false,confirmed=false,uncertain=false,disposed=false,prefs=normalizeMonitorOutput();
  let lastExternalPublish=-Infinity;
  let tick:unknown=null,releaseSample:(()=>void)|null=null;
  let pending:{operation:number;session:string;show:boolean;gameKey:string;timer:unknown;deadline:number;releaseClock:()=>void;resolve:(ack:OutputAck)=>void}|null=null;
  const key=(s:HudClientStatus|null)=>s?.game?JSON.stringify(s.game):'';
  const notify=()=>{try{deps.statusChanged?.();}catch{}};
  const hello=()=>{if(peer&&!disposed)void deps.reply(peer.connection,{type:'hud-ready',protocol:HUD_PROTOCOL,runId:peer.runId}).catch(()=>false);};
  const available=()=>!absent&&!!peer&&status?.capable===true&&!!status.game&&!disposed;
  function cancelTick(){if(tick!==null)deps.clearTimer(tick);tick=null;}
  function stopSample(){cancelTick();releaseSample?.();releaseSample=null;}
  function settle(ack:OutputAck){if(!pending)return;const own=pending;pending=null;deps.clearTimer(own.timer);own.releaseClock();own.resolve(ack);}
  function expire(reason='Steam HUD 原生窗口回执超时；没有开启另一输出。') {
    if(!pending)return;
    uncertain=uncertain||pending.show||confirmed;confirmed=false;
    settle({confirmed:false,reason});wanted=false;stopSample();
    // Revocation is best effort; never promote it to a confirmed hide.
    void publish().catch(()=>false);notify();
  }
  async function publish() {
    const own=peer;if(!own||disposed)return false;
    const data=wanted?deps.sample():hudFromUniformSnapshot(null,deps.now(),null,false);
    const frame:HudFrame={type:'hud',protocol:HUD_PROTOCOL,runId:own.runId,session,seq:++seq,operation,show:wanted,
      expiresAt:deps.now()+HUD_LEASE_MS,game:status?.game??null,preferences:hudWirePreferences(prefs),data:hudWireData(data,prefs)};
    if(frame.data.source!=='ymcc-uniform')throw Error('Fixture is forbidden in product HUD relay');
    lastExternalPublish=deps.now();return deps.reply(own.connection,frame);
  }
  function pulse(){
    cancelTick();if(!peer||disposed||!wanted&&!pending)return;
    void publish().catch(()=>false);
    if(!deps.externalClock)tick=deps.setTimer(pulse,1000); // one lease/transport tick, NEVER a hardware read
  }
  const api={
    connected(value:MirrorPeer){
      if(disposed||!/^\d{1,20}$/.test(value.connection)||!/^[a-f0-9]{32}$/.test(value.runId))return;
      if(value.connected===false){if(peer?.connection!==value.connection||peer.runId!==value.runId)return;
        peer=null;status=null;hiddenKnown=false;uncertain=uncertain||confirmed||!!pending?.show;confirmed=false;wanted=false;stopSample();settle({confirmed:false,reason:'Steam HUD 连接断开；未宣称旧窗口已隐藏。'});notify();return;}
      if(peer?.connection===value.connection&&peer.runId===value.runId)return;
      if(peer){uncertain=uncertain||confirmed;stopSample();settle({confirmed:false,reason:'Steam HUD 会话已变更。'});}
      peer={...value};absent=false;hiddenKnown=false;status=null;confirmed=false;wanted=false;session='';seq=0;hello();notify();
    },
    request(event:MirrorRequest){
      if(disposed||!peer||event.connection!==peer.connection||event.runId!==peer.runId||event.request.runId!==peer.runId)return;
      if(event.request.command!=='hud.status'&&event.request.command!=='hud.ack'||!validHudStatus(event.request.status))return;
      const next=event.request.status;
      if(event.request.command==='hud.status'){
        const changed=key(status)!==key(next);status=next;
        if(next.hiddenConfirmed){hiddenKnown=true;confirmed=false;uncertain=false;}else if(next.shown||!next.hiddenConfirmed)uncertain=true;
        if(changed&&wanted){wanted=false;stopSample();settle({confirmed:false,reason:'Steam 当前游戏身份改变；旧输出已撤销。'});}
        notify();return;
      }
      if(!pending){
        if(next.session!==session||next.operation!==operation||event.request.ok!==true||next.shown!==wanted||wanted&&key(next)!==operationGame||!wanted&&!next.hiddenConfirmed)return;
        status=next;hiddenKnown=!wanted;confirmed=wanted;uncertain=false;notify();return;
      }
      if(next.operation!==pending.operation||next.session!==pending.session)return;
      status=next;
      const good=event.request.ok===true&&next.shown===pending.show&&(pending.show?key(next)===pending.gameKey:next.hiddenConfirmed);
      hiddenKnown=good&&!next.shown;confirmed=good&&next.shown;uncertain=!good&&!next.hiddenConfirmed;
      settle({confirmed:good,reason:good?undefined:next.reason||'Steam 原生 HUD 窗口未确认。'});
      if(!good||!wanted){wanted=false;stopSample();}notify();
    },
    advance(){
      if(pending&&deps.now()>=pending.deadline)expire();
      if(deps.externalClock&&deps.now()-lastExternalPublish>=(prefs.ymcc.fields.frametimeGraph?200:1000))pulse();
    },
    clockUnavailable(reason:string){
      if(pending){expire(reason);return;}
      if(!wanted)return;
      uncertain=uncertain||confirmed;confirmed=false;wanted=false;stopSample();
      void publish().catch(()=>false);notify();
    },
    noteSteamPresence(value:boolean|null){
      const wasAbsent=absent;absent=value===false;
      if(absent){hiddenKnown=true;confirmed=false;uncertain=false;wanted=false;stopSample();status=null;settle({confirmed:pending?.show!==true,reason:pending?.show?'Steam 已退出，未完成显示。':undefined});}
      else if(wasAbsent){hiddenKnown=false;}
      notify();
    },
    hello,available,visible:()=>confirmed||uncertain||!hiddenKnown,
    state:():'shown'|'hidden'|'unknown'=>confirmed?'shown':uncertain||!hiddenKnown?'unknown':'hidden',
    setVisible(show:boolean,preferences:MonitorOutputPreferences):Promise<OutputAck>{
      if(!disposed&&!show&&absent){wanted=false;stopSample();confirmed=false;uncertain=false;hiddenKnown=true;return Promise.resolve({confirmed:true});}
      if(disposed||pending)return Promise.resolve({confirmed:false,reason:'Steam HUD 正在切换或已释放。'});
      if(!peer)return Promise.resolve({confirmed:!show&&(absent||hiddenKnown&&!confirmed&&!uncertain),reason:show?'Steam 插件尚未连接。':uncertain?'旧 Steam HUD 隐藏状态不确定。':undefined});
      if(show&&!available())return Promise.resolve({confirmed:false,reason:'当前游戏没有就绪的 Steam 叠加层承载。'});
      if(!session)session=deps.session();
      if(!/^[a-zA-Z0-9-]{16,80}$/.test(session))throw Error('Invalid product HUD session');
      let releaseClock:()=>void;
      try{releaseClock=deps.acquireClock?.()??(()=>{});}catch{return Promise.resolve({confirmed:false,reason:'原生 HUD 时钟不可用。'});}
      prefs=normalizeMonitorOutput(preferences);wanted=show;
      try{if(show){prefs={...prefs,output:'ymcc',enabled:true};if(!releaseSample)releaseSample=deps.acquire();}}
      catch{releaseClock();wanted=false;stopSample();return Promise.resolve({confirmed:false,reason:'统一监控数据源不可用。'});}
      const ownOperation=++operation;operationGame=key(status);
      return new Promise(resolve=>{
        const deadline=deps.now()+12000;
        const timer=deps.setTimer(()=>{if(pending?.operation===ownOperation)expire();},12000);
        pending={operation:ownOperation,session,show,gameKey:key(status),timer,deadline,releaseClock,resolve};pulse();
      });
    },
    updateAppearance(value:MonitorOutputPreferences){
      if(!wanted||disposed)return;
      // Selection/master are drafts until the exclusive coordinator applies.
      prefs={...prefs,ymcc:normalizeMonitorOutput(value).ymcc};
    },
    dispose(){if(disposed)return;wanted=false;void publish().catch(()=>false);disposed=true;stopSample();settle({confirmed:false,reason:'YMCC HUD 所有者已关闭。'});peer=null;status=null;},
    inspect(){return {connected:!!peer,available:available(),wanted,confirmed,uncertain,hiddenKnown,absent,operation,session,pending:!!pending,tick:tick!==null,sampleOwned:!!releaseSample,status:status?structuredClone(status):null};},
  };
  return api;
}
