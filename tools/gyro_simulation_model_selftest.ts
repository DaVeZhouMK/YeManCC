import assert from 'node:assert/strict';
import {createRange,updateRange,targetX,targetPose,triggerSnapshot,type SimulationInput} from '../src/gyroSimulation/model';
const idle:SimulationInput={active:false,demo:false,x:0,y:0,throttle:0,brake:0};
const input=(patch:Partial<SimulationInput>={})=>({...idle,active:true,...patch});
let count=0;
function test(name:string,fn:()=>void){fn();count++;console.log('PASS '+name);}
function rangeRun(seconds:number,patch:Partial<SimulationInput>={},state=createRange(),hz=120){for(let i=0;i<seconds*hz;i++)updateRange(state,1/hz,input(patch));return state;}
test('Existing native LT/RT and physical stick fields, disconnected is neutral',()=>{
 const raw={connected:true,buttons:[false,false,true,false,false,false,true,true],axes:[2,.4,-.2,NaN]};const s=triggerSnapshot(raw);
 assert.deepEqual(s,{connected:true,lt:1,rt:1,restart:true,axes:[1,.4,-.2,0]});assert.equal(triggerSnapshot({...raw,connected:false}).rt,0);assert.equal(triggerSnapshot(null).connected,false);
});
test('Inactive game never shoots or moves player',()=>{const s=createRange();for(let i=0;i<100;i++)updateRange(s,.01,{...idle,throttle:1,moveY:1,x:1});assert.equal(s.shots,0);assert.equal(s.playerZ,0);assert.equal(s.yaw,0);});
test('Held RT fires repeatedly, consumes ammo and scores real target intersections',()=>{const s=rangeRun(1,{throttle:1});assert.ok(s.shots>=13&&s.shots<=14);assert.equal(s.ammo,30-s.shots);assert.ok(s.hits>=1);assert.ok(s.targets.some(t=>t.downUntil>s.time));});
test('800 RPM cadence stays consistent at 30, 60 and 120 display Hz',()=>{const counts=[30,60,120].map(hz=>rangeRun(1,{throttle:1},createRange(),hz).shots);assert.ok(counts.every(n=>n>=13&&n<=14));assert.ok(Math.max(...counts)-Math.min(...counts)<=1);});
test('Overhead moving plate can be aimed at and scored',()=>{const s=createRange(),t=s.targets.find(t=>t.rail&&t.kind==='plate')!;assert.ok(t);s.targets=[t];s.yaw=Math.atan2(targetX(t,0),t.z);s.pitch=-Math.atan2(t.y-1.62,Math.hypot(t.z,targetX(t,0)));updateRange(s,.001,input({throttle:1}));assert.equal(s.hits,1);});
test('Misses do not score and moving targets change position over time',()=>{const s=createRange();s.yaw=1.1;rangeRun(.5,{throttle:1},s);assert.equal(s.hits,0);assert.ok(s.targets.some(t=>t.moving));});
test('LT zooms without firing; aim releases smoothly',()=>{const s=rangeRun(.4,{brake:1});assert.ok(s.ads>.95);assert.equal(s.shots,0);rangeRun(.6,{},s);assert.ok(s.ads<.01);});
test('RT has camera/gun recoil; it recovers when released',()=>{const s=rangeRun(.5,{throttle:1});assert.ok(s.kick>.01&&s.recoil>0);rangeRun(1,{},s);assert.ok(s.kick<.001&&s.recoil<.001);});
test('Downward input counters camera recoil',()=>{const a=rangeRun(.5,{throttle:1}),b=rangeRun(.5,{throttle:1,y:.12});assert.ok((-b.pitch+b.kick)<(-a.pitch+a.kick));});
test('Empty magazine auto reloads, then held RT resumes',()=>{const s=rangeRun(6,{throttle:1});assert.ok(s.shots>30);assert.ok(s.ammo>=0&&s.ammo<=30);});
test('Target rises again after its cooldown',()=>{const s=rangeRun(.01,{throttle:1});const t=s.targets.find(t=>t.downUntil>s.time)!;assert.ok(t);rangeRun(2,{},s);assert.ok(t.downUntil<s.time);});
test('Left stick moves relative to the view; diagonal speed is bounded',()=>{const a=rangeRun(1,{moveY:1}),b=rangeRun(1,{moveX:1,moveY:1});assert.ok(a.playerZ>3.7);assert.ok(Math.abs(Math.hypot(b.playerX,b.playerZ)-3.8)<.02);const s=createRange();s.yaw=Math.PI/2;rangeRun(1,{moveY:1},s);assert.ok(s.playerX>3.7);});
test('Right stick view can turn freely without moving the player',()=>{const s=rangeRun(8,{x:1});assert.ok(Math.abs(s.yaw)>1);assert.equal(s.playerZ,0);assert.equal(s.playerX,0);});
test('Horizontal recoil moves both directions, stays bounded and recovers',()=>{
 const s=createRange(),values=[];for(let i=0;i<240;i++){updateRange(s,1/120,input({throttle:1}));values.push(s.kickYaw);}assert.ok(values.some(x=>x>.001));assert.ok(values.some(x=>x<-.001));assert.ok(values.every(x=>Math.abs(x)<=.045));rangeRun(1.5,{},s);assert.ok(Math.abs(s.kickYaw)<.0001&&Math.abs(s.recoilYaw)<.0001);
});
test('Dispersion affects the shot ray, and ADS narrows the actual cone',()=>{
 const hip=createRange(),ads=createRange();ads.ads=1;updateRange(hip,.001,input({throttle:1}));updateRange(ads,.001,input({throttle:1,brake:1}));
 const a=Math.hypot(hip.lastShotYaw,hip.lastShotPitch),b=Math.hypot(ads.lastShotYaw,ads.lastShotPitch);assert.ok(a>b*4);assert.ok(a>0);assert.equal(hip.seed,ads.seed);
});
test('Shot results are deterministic and hit marks record the ray intersection',()=>{
 const a=rangeRun(.5,{throttle:1}),b=rangeRun(.5,{throttle:1});assert.equal(a.lastShotYaw,b.lastShotYaw);assert.equal(a.kickYaw,b.kickYaw);const s=rangeRun(.001,{throttle:1});const hit=s.impacts[0];assert.ok(s.hits===1);assert.ok(Math.abs(hit.x-Math.sin(s.lastShotYaw)*hit.z/Math.cos(s.lastShotYaw))<.00001);
});
console.log(`Gyro simulation model: ${count}/${count} passed`);
