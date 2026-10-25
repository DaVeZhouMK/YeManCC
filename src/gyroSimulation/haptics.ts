import {invoke,isNativeRuntime}from '@/bridge/ipc';
export type GameHapticEvent='shot'|'hit'|'reload'|'collision'|'landing'|'skid';
export interface HapticPulse{event:GameHapticEvent;strength:number;durationMs:number;priority:number}
export const GAME_HAPTIC_PATTERNS:Record<GameHapticEvent,Omit<HapticPulse,'event'>>={shot:{strength:.34,durationMs:35,priority:1},hit:{strength:.52,durationMs:60,priority:2},reload:{strength:.27,durationMs:80,priority:1},collision:{strength:.85,durationMs:150,priority:3},landing:{strength:.72,durationMs:120,priority:3},skid:{strength:.18,durationMs:45,priority:0}};
interface Actuator{playEffect?:(type:string,args:{duration:number;startDelay:number;strongMagnitude:number;weakMagnitude:number})=>Promise<unknown>;reset?:()=>Promise<unknown>}
export interface HapticPad{connected:boolean;vibrationActuator?:Actuator;hapticActuators?:Actuator[];index?:number;id?:string}
export interface HapticDependencies{native:boolean;invoke:(command:string,args?:Record<string,unknown>)=>Promise<unknown>;pad:()=>HapticPad|null;now:()=>number}
export interface HapticReceipt{event:GameHapticEvent;backend:'native'|'browser';strength:number;durationMs:number;accepted:boolean;time:number}
/** One event dispatcher for both games. Finite native pulses use the existing managed-device gate. */
export class GameHaptics{
 private active=false;private disposed=false;private generation=0;private lastAt=-Infinity;private lastPriority=-1;private busy=false;private sent=false;private probe:Promise<void>|null=null;private nativeReady=false;private retryAt=0;private actuator:Actuator|null=null;
 status:'idle'|'ready'|'unavailable'='idle';receipts:HapticReceipt[]=[];
 constructor(private dependencies:HapticDependencies){}
 async start(){if(this.disposed)return;this.active=true;const generation=++this.generation;
  if(this.dependencies.native){if(!this.probe)this.probe=this.dependencies.invoke('gamepad.feedbackCapabilities',{}).then(value=>{this.nativeReady=!!(value as{vibrationAvailable?:boolean})?.vibrationAvailable;}).catch(()=>{this.nativeReady=false;}).finally(()=>{this.probe=null;});await this.probe;if(this.active&&generation===this.generation)this.status=this.nativeReady?'ready':'unavailable';}
  else{this.actuator=this.currentActuator();this.status=this.actuator?'ready':'unavailable';}
 }
 private currentActuator(){const p=this.dependencies.pad();if(!p?.connected)return null;return p.vibrationActuator?.playEffect?p.vibrationActuator:p.hapticActuators?.find(a=>a.playEffect)||null;}
 pulse(event:GameHapticEvent,scale=1){if(!this.active||this.disposed)return;const pattern=GAME_HAPTIC_PATTERNS[event],now=this.dependencies.now();
  if(now<this.retryAt||this.busy||now-this.lastAt<(pattern.priority>=3?35:pattern.priority===0?180:65))return;
  const strength=Math.max(0,Math.min(1,pattern.strength*scale));if(!strength)return;
  const actuator=this.dependencies.native?null:this.currentActuator();if(this.dependencies.native?!this.nativeReady:!actuator){this.status='unavailable';return;}
  this.lastAt=now;this.lastPriority=pattern.priority;const generation=this.generation;this.sent=true;this.busy=true;
  const receipt:HapticReceipt={event,backend:this.dependencies.native?'native':'browser',strength,durationMs:pattern.durationMs,accepted:false,time:now};this.receipts.push(receipt);if(this.receipts.length>160)this.receipts.shift();
  let call:Promise<unknown>|unknown;try{call=this.dependencies.native?this.dependencies.invoke('gamepad.vibrate',{strength:Math.round(strength*100),durationMs:pattern.durationMs}):actuator!.playEffect!('dual-rumble',{duration:pattern.durationMs,startDelay:0,strongMagnitude:strength,weakMagnitude:strength*.7});}catch{this.busy=false;this.status='unavailable';this.retryAt=now+3000;return;}
  if(actuator)this.actuator=actuator;
  Promise.resolve(call).then(async result=>{receipt.accepted=this.dependencies.native?!!(result as{ok?:boolean})?.ok:result==='complete'||result==='preempted'||result===undefined;
   if(generation!==this.generation||!this.active||this.disposed){if(this.dependencies.native)await this.dependencies.invoke('gamepad.vibrate',{strength:0,durationMs:10});else await actuator?.reset?.();}
   else{this.status=receipt.accepted?'ready':'unavailable';if(!receipt.accepted)this.retryAt=this.dependencies.now()+3000;}
  }).catch(()=>{if(generation===this.generation){this.status='unavailable';this.retryAt=this.dependencies.now()+3000;}}).finally(()=>{this.busy=false;});
 }
 stop(){const hadPulse=this.sent;this.active=false;this.generation++;this.sent=false;this.lastAt=-Infinity;this.lastPriority=-1;
  if(this.dependencies.native){if(hadPulse)void this.dependencies.invoke('gamepad.vibrate',{strength:0,durationMs:10}).catch(()=>{});}
  else{void this.actuator?.reset?.().catch(()=>{});this.actuator=null;}
 }
 dispose(){this.stop();this.disposed=true;}
}
export function createGameHaptics(pad:()=>HapticPad|null){return new GameHaptics({native:isNativeRuntime,invoke,pad,now:()=>performance.now()});}
