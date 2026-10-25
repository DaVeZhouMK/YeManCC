// One native display clock (1Hz hardware / 5Hz active curve), independently leased by samples AND control operations.
// Acquiring this clock never acquires sensors, reads hardware, or enumerates games.
export interface HudClockDeps {
  nonce():string;now():number;
  invoke<T>(command:string,args:Record<string,unknown>):Promise<T>;
  setTimer(fn:()=>void,ms:number):unknown;clearTimer(id:unknown):void;
  pulse(snapshot:unknown):void;failed(reason:string):void;
}
export function createHudClock(deps:HudClockDeps) {
  type Lease={nonce:string;users:number;frameTime:boolean;enabled:boolean;failed:boolean;startDeadline:number;startTimer:unknown;
    ack:{deadline:number;timer:unknown}|null};
  let current:Lease|null=null,ended=false,frameTime=false,lastAckAt=-Infinity;
  function disable(nonce:string) {void deps.invoke('monitor.hudPulse.set',{enabled:false,nonce}).catch(()=>{});}
  function clear(own:Lease) {
    if(own.startTimer!==null)deps.clearTimer(own.startTimer);own.startTimer=null;
    if(own.ack)deps.clearTimer(own.ack.timer);own.ack=null;
  }
  function fail(own:Lease,reason:string) {
    if(current!==own||own.failed)return;
    own.failed=true;clear(own);disable(own.nonce);
    try{deps.failed(reason);}catch{}
  }
  function configure(own:Lease) {
    if(current!==own || own.failed || ended || !own.enabled || own.frameTime===frameTime)return;
    own.frameTime=frameTime;
    void deps.invoke<boolean>('monitor.hudPulse.set',{enabled:true,nonce:own.nonce,frameTime,update:true}).then(ok=>{
      if(current!==own || own.failed){if(ok)disable(own.nonce);return;}
      if(!ok)fail(own,'原生 HUD 时钟调节失败。');
    }).catch(()=>{if(current===own)fail(own,'原生 HUD 时钟调节未确认。');});
  }
  function enabled(own:Lease) {
    own.enabled=true;if(own.startTimer!==null)deps.clearTimer(own.startTimer);own.startTimer=null;configure(own);
  }
  return {
    setFrameTime(enabled:boolean) {
      if(frameTime===enabled)return;frameTime=enabled;
      if(current)configure(current);
    },
    acquire() {
      if(ended||current?.failed)throw Error('原生 HUD 时钟不可用。');
      if(!current) {
        const nonce=deps.nonce();if(!/^[a-zA-Z0-9-]{16,80}$/.test(nonce))throw Error('Invalid native HUD clock nonce');
        const own:Lease={nonce,users:0,frameTime,enabled:false,failed:false,startDeadline:deps.now()+3000,startTimer:null,ack:null};
        current=own;
        own.startTimer=deps.setTimer(()=>fail(own,'原生 HUD 时钟启用超时。'),3000);
        void deps.invoke<boolean>('monitor.hudPulse.set',{enabled:true,nonce,frameTime}).then(ok=>{
          if(current!==own){if(ok)disable(nonce);return;}
          if(own.failed){if(ok)disable(nonce);return;}
          if(ok)enabled(own);else fail(own,'原生 HUD 时钟启用失败。');
        }).catch(()=>fail(own,'原生 HUD 时钟启用未确认。'));
      }
      const own=current;++own.users;
      let released=false;return()=>{
        if(released)return;released=true;
        if(--own.users===0&&current===own){current=null;clear(own);disable(own.nonce);}
      };
    },
    accept(event:{nonce:string;snapshot:unknown}) {
      const own=current;if(!own||own.failed||ended||event.nonce!==own.nonce)return false;
      if(!own.enabled){
        if(deps.now()>=own.startDeadline){fail(own,'原生 HUD 时钟启用超时。');return false;}
        // A nonce-bound native tick is proof the timer is running, even if its
        // enable receipt is late. It cannot revive a failed/released generation.
        enabled(own);
      }
      if(own.ack&&deps.now()>=own.ack.deadline){fail(own,'原生 HUD 时钟续期回执超时。');return false;}
      deps.pulse(event.snapshot);
      if(current!==own||own.failed||own.ack||deps.now()-lastAckAt<1000)return true;
      lastAckAt=deps.now();
      const ack={deadline:deps.now()+3000,timer:null as unknown};own.ack=ack;
      ack.timer=deps.setTimer(()=>{if(own.ack===ack)fail(own,'原生 HUD 时钟续期回执超时。');},3000);
      void deps.invoke<boolean>('monitor.hudPulse.ack',{nonce:own.nonce}).then(ok=>{
        if(current!==own||own.ack!==ack||own.failed)return;
        deps.clearTimer(ack.timer);own.ack=null;
        if(!ok)fail(own,'原生 HUD 时钟续期失败。');
      }).catch(()=>{if(own.ack===ack)fail(own,'原生 HUD 时钟续期未确认。');});
      return true;
    },
    dispose(){if(ended)return;ended=true;const own=current;current=null;if(own){clear(own);disable(own.nonce);}},
    inspect:()=>({users:current?.users??0,nonce:current?.nonce??null,enabled:current?.enabled??false,failed:current?.failed??false}),
  };
}
