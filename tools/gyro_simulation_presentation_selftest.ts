import assert from 'node:assert/strict';
import {createRange,targetPose,type Target} from '../src/gyroSimulation/model';
import {weaponPoint} from '../src/gyroSimulation/renderer';
let checks=0;function test(name:string,fn:()=>void){fn();checks++;console.log('PASS '+name);}
const ground:Target={x:0,y:1.52,z:24,downUntil:11.7,moving:false};
test('Ground target falls around its bottom pivot, holds down and rises smoothly',()=>{
 const start=targetPose(ground,10),middle=targetPose(ground,10.12),down=targetPose(ground,10.4),rising=targetPose(ground,11.45),up=targetPose(ground,11.7);
 assert.equal(start.fold,0);assert.ok(middle.fold>0&&middle.fold<Math.PI/2);assert.equal(down.fold,Math.PI/2);assert.ok(rising.fold>0&&rising.fold<Math.PI/2);assert.equal(up.fold,0);assert.equal(up.exposure,1);
});
test('Ceiling target retracts above carriage mask instead of flattening in place',()=>{
 const rail={...ground,y:3.65,rail:true},mid=targetPose(rail,10.12),down=targetPose(rail,10.4);
 assert.equal(mid.fold,0);assert.ok(mid.lift>0&&mid.lift<down.lift);assert.ok(rail.y+down.lift-.64>4.7);assert.equal(targetPose(rail,11.7).lift,0);
});
test('Target poses have continuous fall/rise endpoints',()=>{
 for(const time of [10,10.24,11.15,11.7]){const a=targetPose(ground,time-.0001),b=targetPose(ground,time+.0001);assert.ok(Math.abs(a.fold-b.fold)<.002);}
 assert.equal(targetPose({...ground,downUntil:0},0).fold,0);
});
test('Muzzle extends into forward depth and converges toward the reticle',()=>{
 const s=createRange(),near=weaponPoint(s,644,550,{x:0,y:0,z:.31}),muzzle=weaponPoint(s,644,550,{x:0,y:0,z:1.41});
 assert.ok(muzzle.depth>near.depth*4);assert.ok(Math.abs(muzzle.x-322)<Math.abs(near.x-322));assert.ok(Math.abs(muzzle.y-550*.47)<Math.abs(near.y-550*.47));assert.ok(muzzle.y>550*.47);
});
test('Raised optic aligns with screen aim, rather than a top-down rifle sprite',()=>{
 const s=createRange();s.ads=1;const lens=weaponPoint(s,644,550,{x:0,y:.09,z:.35});assert.ok(Math.abs(lens.x-322)<.1);assert.ok(Math.abs(lens.y-550*.47)<.1);
});
console.log(`Gyro presentation selftest: ${checks}/${checks} passed`);
