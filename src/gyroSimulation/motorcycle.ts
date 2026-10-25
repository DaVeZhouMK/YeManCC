import type {SimulationInput} from './model';
export interface Rider {id:number;x:number;distance:number;speed:number;color:string;health:number;stagger:number;attackAt:number;passed:boolean}
export interface MotorcycleState {time:number;distance:number;x:number;speed:number;steer:number;lean:number;lateralSpeed:number;health:number;score:number;knockouts:number;hits:number;collisions:number;attackClock:number;attackPose:number;attackSide:number;impact:number;hitFlash:number;stun:number;opponents:Rider[];seed:number}
export const MOTORCYCLE_ROAD_HALF_WIDTH=5.2;
export const MOTORCYCLE_TOP_SPEED=238;
const clamp=(v:number,a:number,b:number)=>Math.max(a,Math.min(b,v));
const random=(s:MotorcycleState)=>{s.seed=(Math.imul(s.seed,1664525)+1013904223)>>>0;return s.seed/4294967296;};
export function createMotorcycle():MotorcycleState {
 return {time:0,distance:0,x:0,speed:92,steer:0,lean:0,lateralSpeed:0,health:100,score:0,knockouts:0,hits:0,collisions:0,attackClock:0,attackPose:0,attackSide:1,impact:0,hitFlash:0,stun:0,seed:20261023,
 opponents:Array.from({length:9},(_,i)=>({id:i,x:(i%3-1)*2.3,distance:9+i*29,speed:101+i%4*9,color:['#f6a338','#68b9df','#cf6d96','#b4d66b'][i%4],health:3,stagger:0,attackAt:0,passed:false}))};
}
/** Fixed small physics steps keep steering, contacts and attacks stable at 30/60/120Hz. */
export function updateMotorcycle(s:MotorcycleState,dt:number,input:SimulationInput):void {
 if(!Number.isFinite(dt)||dt<=0)return;dt=clamp(dt,0,.05);
 for(let remaining=dt;remaining>1e-7;){const step=Math.min(remaining,1/120);remaining-=step;tick(s,step,input);}
}
function tick(s:MotorcycleState,dt:number,input:SimulationInput){
 s.time+=dt;s.attackClock=Math.max(0,s.attackClock-dt);s.attackPose=Math.max(0,s.attackPose-dt*2.8);s.hitFlash=Math.max(0,s.hitFlash-dt);s.stun=Math.max(0,s.stun-dt);s.impact*=Math.exp(-dt*7);
 const live=input.active||input.demo;
 const steering=input.active?clamp(input.x,-1,1):Math.sin(s.time*.34)*.23;
 s.steer+=(steering-s.steer)*(1-Math.exp(-dt*12));s.lean+=(s.steer*.62-s.lean)*(1-Math.exp(-dt*8));
 const throttle=input.active?clamp(input.throttle,0,1):.45,brake=input.active?clamp(input.brake,0,1):0;
 const offroad=Math.abs(s.x)>MOTORCYCLE_ROAD_HALF_WIDTH-.35;
 const acceleration=throttle*78-7-s.speed*s.speed*.00046-brake*139-(offroad?28:0)-(s.stun>0?38:0);
 s.speed=clamp(s.speed+acceleration*dt,0,MOTORCYCLE_TOP_SPEED);
 s.lateralSpeed+=(s.steer*(2.2+s.speed*.018)-s.lateralSpeed)*(1-Math.exp(-dt*8));
 s.x=clamp(s.x+s.lateralSpeed*dt,-7.1,7.1);s.distance+=s.speed/3.6*dt;
 if(live&&input.attack&&s.attackClock===0){
  s.attackClock=.44;s.attackPose=1;
  const rivals=s.opponents.filter(r=>r.health>0&&Math.abs(r.distance-s.distance)<6.5&&Math.abs(r.x-s.x)<2.2).sort((a,b)=>Math.abs(a.x-s.x)-Math.abs(b.x-s.x));
  const target=rivals[0];s.attackSide=target?Math.sign(target.x-s.x)||1:Math.sign(s.steer)||1;
  if(target){target.health--;target.stagger=.65;target.x=clamp(target.x+s.attackSide*.9,-6.5,6.5);s.hits++;s.score+=75;s.impact=.65;if(target.health===0){s.knockouts++;s.score+=250;target.stagger=2.5;}}
 }
 for(const r of s.opponents){
  r.stagger=Math.max(0,r.stagger-dt);
  if(r.health>0){
   const desired=120+Math.sin(s.time*.12+r.id)*13+(r.distance<s.distance-25?37:r.distance>s.distance+180?-24:0);
   r.speed+=(desired-r.speed)*(1-Math.exp(-dt*.45));
   r.distance+=r.speed/3.6*dt*(r.stagger>0?.78:1);
   const lane=(r.id%3-1)*2.45+Math.sin(s.time*.36+r.id)*.3;
   r.x+=(lane-r.x)*dt*(r.stagger>0?.25:1.05);
   const gap=r.distance-s.distance,side=r.x-s.x;
   if(Math.abs(gap)<2.6&&Math.abs(side)<.7&&s.stun===0){
    const direction=Math.sign(side)||((r.id%2)?1:-1);s.x=clamp(s.x-direction*.4,-7.1,7.1);r.x=clamp(r.x+direction*.4,-6.5,6.5);s.lateralSpeed-=direction*1.2;s.speed*=.86;s.stun=.42;s.impact=.9;s.collisions++;if(live){s.health=Math.max(0,s.health-7);s.hitFlash=.22;}
   }
   if(live&&Math.abs(gap)<4&&Math.abs(side)<1.9&&s.time>r.attackAt){r.attackAt=s.time+2.6+r.id*.13;if(s.attackPose===0){s.health=Math.max(0,s.health-3);s.hitFlash=.15;}}
   if(gap< -5&&!r.passed){r.passed=true;if(live)s.score+=100;}
  }else r.speed*=Math.exp(-dt*2);
  if(r.distance<s.distance-70||r.health===0&&r.stagger===0){r.distance=s.distance+190+random(s)*140;r.x=(r.id%3-1)*2.4;r.health=3;r.passed=false;r.speed=125;}
 }
 if(s.health===0){s.speed=Math.max(0,s.speed-95*dt);s.steer=0;s.lean*=Math.exp(-dt*5);}
}
