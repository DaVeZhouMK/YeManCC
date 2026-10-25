/** Local mini-games. Only consumes presentation input; never writes controller/output settings. */
export type SimulationScene = 'fps' | 'motorcycle';
export interface SimulationInput { x: number; y: number; moveX?:number; moveY?:number; throttle: number; brake: number; active: boolean; demo: boolean; attack?: boolean }
export interface Target { x: number; y: number; z: number; downUntil: number; moving: boolean;rail?:boolean;kind?:'plate'|'silhouette' }
export interface RangeState {
 time: number; playerX:number; playerZ:number; yaw: number; pitch: number; ads: number; recoil: number; kick: number; kickYaw:number; recoilYaw:number; seed:number; lastShotYaw:number; lastShotPitch:number; flash: number;
 shotClock: number; shots: number; hits: number; ammo: number; reload: number; hitFlash: number;
 targets: Target[]; impacts: {x:number;y:number;z:number;life:number}[];
}
export const clamp = (value: number, min: number, max: number) => Math.max(min, Math.min(max, Number.isFinite(value) ? value : 0));
export function triggerSnapshot(raw: unknown): {connected:boolean;lt:number;rt:number;axes:number[];restart:boolean} {
 const state=raw as {connected?:unknown;buttons?:unknown;axes?:unknown}|null;
 if(!state?.connected||!Array.isArray(state.buttons))return {connected:false,lt:0,rt:0,axes:[0,0,0,0],restart:false};
 // gamepad.state uses the existing standard button indices, 6=LT and 7=RT.
 const rawAxes=Array.isArray(state.axes)?state.axes:[];
 const axes=[0,1,2,3].map(i=>clamp(Number(rawAxes[i]),-1,1));
 return {connected:true,lt:state.buttons[6]===true?1:0,rt:state.buttons[7]===true?1:0,axes,restart:state.buttons[2]===true};
}
export function createRange(): RangeState {
 return {time:0,playerX:0,playerZ:0,yaw:0,pitch:0,ads:0,recoil:0,kick:0,kickYaw:0,recoilYaw:0,seed:0x4d344131,lastShotYaw:0,lastShotPitch:0,flash:0,shotClock:0,shots:0,hits:0,ammo:30,reload:0,hitFlash:0,
  targets:[{x:0,y:1.52,z:24,downUntil:0,moving:false},{x:-4.6,y:1.5,z:32,downUntil:0,moving:false},
   {x:4.5,y:1.5,z:30,downUntil:0,moving:true},{x:-7,y:1.65,z:48,downUntil:0,moving:true,kind:'plate'},{x:7.7,y:1.65,z:54,downUntil:0,moving:false,kind:'plate'},{x:2.4,y:1.1,z:16,downUntil:0,moving:false},{x:-2.8,y:1.4,z:41,downUntil:0,moving:true},{x:-2.2,y:3.65,z:22,downUntil:0,moving:true,rail:true},{x:3.8,y:3.7,z:38,downUntil:0,moving:true,rail:true,kind:'plate'}],impacts:[]};
}
export function targetX(target:Target,time:number) { return target.x+(target.moving?Math.sin(time*.65+target.z)*1.6:0); }
/** Visible hit cycle: fall/retract, stay hidden, then rise before becoming shootable. */
export function targetPose(target:Target,time:number):{fold:number;lift:number;exposure:number} {
 if(target.downUntil<=time)return {fold:0,lift:0,exposure:1};
 const elapsed=Math.max(0,time-(target.downUntil-1.7));
 const smooth=(v:number)=>{v=clamp(v,0,1);return v*v*(3-2*v);};
 const hidden=elapsed<.24?smooth(elapsed/.24):elapsed<1.15?1:1-smooth((elapsed-1.15)/.55);
 return {fold:target.rail?0:hidden*Math.PI/2,lift:target.rail?hidden*(5.45-target.y):0,exposure:1-hidden};
}
export function updateRange(state:RangeState,dt:number,input:SimulationInput):void {
 dt=clamp(dt,0,.05);state.time+=dt;
 const live=input.active||input.demo;
 const x=input.demo?Math.sin(state.time*.58)*.12:input.active?clamp(input.x,-1,1):0;
 const y=input.demo?Math.cos(state.time*.45)*.025:input.active?clamp(input.y,-1,1):0;
 state.yaw=Math.atan2(Math.sin(state.yaw+x*dt*.95),Math.cos(state.yaw+x*dt*.95));state.pitch=clamp(state.pitch+y*dt*.65,-.3,.3);
 if(input.active){
  const mx=clamp(input.moveX||0,-1,1),my=clamp(input.moveY||0,-1,1),length=Math.max(1,Math.hypot(mx,my));
  const moveSpeed=3.8*dt/length;
  state.playerX=clamp(state.playerX+(mx*Math.cos(state.yaw)+my*Math.sin(state.yaw))*moveSpeed,-9,9);
  state.playerZ=clamp(state.playerZ+(-mx*Math.sin(state.yaw)+my*Math.cos(state.yaw))*moveSpeed,-1,55);
 }
 const aiming=live&&state.reload<=0&&(input.demo?Math.sin(state.time*.4)>0:input.brake>.2);
 state.ads+=(Number(aiming)-state.ads)*Math.min(1,dt*13);
 state.recoil*=Math.exp(-dt*9);state.kick*=Math.exp(-dt*7);state.kickYaw*=Math.exp(-dt*6);state.recoilYaw*=Math.exp(-dt*11);state.flash=Math.max(0,state.flash-dt);state.hitFlash=Math.max(0,state.hitFlash-dt);
 state.impacts=state.impacts.filter(p=>(p.life-=dt)>0);
 state.shotClock-=dt;
 if(state.reload>0){state.shotClock=0;state.reload=Math.max(0,state.reload-dt);if(state.reload===0)state.ammo=30;return;}
 const firing=live&&(input.demo?state.time%4<1.5:input.throttle>.2);
 if(!firing){state.shotClock=Math.max(0,state.shotClock);return;}
 if(state.shotClock>0)return;
 if(state.ammo===0){state.reload=1.35;state.shotClock=0;return;}
 const aimKick=state.kick,aimYaw=state.kickYaw;
 // Seeded shot cone: dispersion changes the collision ray, not just a screen shake.
 const random=()=>{state.seed=(Math.imul(state.seed,1664525)+1013904223)>>>0;return state.seed/4294967296;};
 const angle=random()*Math.PI*2,radius=Math.sqrt(random());
 const moving=Math.hypot(input.moveX||0,input.moveY||0)>.2;
 const spread=(.0042*(1-state.ads)+.0008*state.ads)+(moving?.0018:0);
 state.shotClock+=60/800;state.ammo--;state.shots++;state.flash=.045;state.recoil=clamp(state.recoil+.011,0,.06);state.kick=clamp(state.kick+.013,0,.12); // Camera/gun kick accumulate; gyro down counters them.
 const horizontal=(random()-.5)*.012+Math.sin(state.shots*.8)*.003;
 state.kickYaw=clamp(state.kickYaw+horizontal,-.045,.045);state.recoilYaw=clamp(state.recoilYaw+horizontal*1.8,-.06,.06);
 state.lastShotYaw=state.yaw+aimYaw+Math.cos(angle)*radius*spread;
 state.lastShotPitch=-state.pitch+aimKick+Math.sin(angle)*radius*spread;
 const rayX=Math.sin(state.lastShotYaw),rayZ=Math.cos(state.lastShotYaw),rayY=Math.tan(state.lastShotPitch);
 let hit:Target|undefined;
 for(const target of [...state.targets].sort((a,b)=>a.z-b.z)){
  if(target.downUntil>state.time)continue;
  const distance=(target.z-state.playerZ)/rayZ;
  if(Math.abs(rayZ)<.005||distance<.5)continue;
  const xAtZ=state.playerX+rayX*distance,yAtZ=1.62+rayY*distance;
  const inTarget=target.kind==='plate'?Math.hypot(xAtZ-targetX(target,state.time),yAtZ-target.y)<.4:
   Math.abs(xAtZ-targetX(target,state.time))<.48&&yAtZ>target.y-.7&&yAtZ<target.y+.42;
  if(inTarget){hit=target;break;}
 }
 if(hit){hit.downUntil=state.time+1.7;state.hits++;state.hitFlash=.2;state.impacts.push({x:state.playerX+rayX*(hit.z-state.playerZ)/rayZ,y:1.62+rayY*(hit.z-state.playerZ)/rayZ,z:hit.z,life:.3});}
 else state.impacts.push({x:state.playerX+rayX*64,y:clamp(1.62+rayY*64,.05,8),z:state.playerZ+rayZ*64,life:1.5});
}
export {createMotorcycle,updateMotorcycle,type MotorcycleState} from './motorcycle';
