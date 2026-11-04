import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
export async function gyroNavigationCases({f,height,command,press,actionCase,task}) {
 const playing=()=>f.evaluate(()=>window.__gyroGameAudit.playing);
 await f.evaluate(()=>{window.__startDebugEvents=0;window.addEventListener('ipc:gamepad-start',()=>window.__startDebugEvents++);});
 await actionCase('Gyro A edits controls; Start enters from right and B returns without slider edit leak',height,async()=>{
  for(const selector of ['[data-gyro-enter-play]','.gyro-game-pedal']){await press(f,f.locator(selector).first());assert.equal(await playing(),false);}
  const slider=f.locator('.gyro-simulation-editor input[type="range"]:not(:disabled)').first();
  await press(f,slider);assert.equal(await f.evaluate(()=>window.__gamepadAudit.editing()),true);
  await command(f,'debug');assert.equal(await playing(),true);assert.equal(await f.evaluate(()=>window.__gamepadAudit.editing()),false);
  assert.ok(await f.locator('canvas').evaluate(el=>document.activeElement===el));
  const value=await slider.inputValue();for(const action of ['nav-left','nav-right','nav-up','nav-down','confirm','slider-increase','page-next'])await command(f,action);
  assert.equal(await slider.inputValue(),value);assert.ok(await f.locator('canvas').evaluate(el=>document.activeElement===el));
  await command(f,'back');assert.equal(await playing(),false);assert.equal(await f.locator('.gyro-simulation-window').count(),1);
 });
 await actionCase('Gyro right settings can reach and return from both left scene tabs by directions',height,async()=>{
  const results=await f.evaluate(()=>{
   const a=window.__gamepadAudit,els=a.focusables(),start=els.findIndex(el=>el.closest('.gyro-simulation-editor'));
   const graph=els.map(el=>['nav-left','nav-right','nav-up','nav-down'].map(action=>{el.focus({preventScroll:true});a.command(action);return els.indexOf(document.activeElement);}));
   function walk(from,to){const seen=new Map([[from,[]]]),q=[from];while(q.length){const n=q.shift();if(n===to)return seen.get(n);for(let d=0;d<4;d++){const j=graph[n][d];if(j>=0&&!seen.has(j)){seen.set(j,[...seen.get(n),['nav-left','nav-right','nav-up','nav-down'][d]]);q.push(j);}}}return null;}
   return [...document.querySelectorAll('.gyro-scene-tabs button')].slice(0,2).map(el=>{
    const target=els.indexOf(el),forward=walk(start,target),back=walk(target,start);a.focus(els[start]);for(const action of forward||[])a.command(action);const reached=document.activeElement===el;a.command('confirm');for(const action of back||[])a.command(action);
    return {forward,back,reached,returned:document.activeElement===els[start]};
   });
  });for(const r of results)assert.ok(r.forward&&r.back&&r.reached&&r.returned,JSON.stringify(r));
 });
 await actionCase('Gyro dropdown and nested confirmation retain Start ownership',height,async()=>{
  const dropdown=f.locator('.gyro-simulation-window .dd-trigger').first();await press(f,dropdown);await f.locator('[role="listbox"]').waitFor();
  await command(f,'debug');assert.equal(await playing(),false,'Start while dropdown open');assert.equal(await f.locator('[role="listbox"]').count(),1);await command(f,'back');
  await press(f,f.locator('[data-gyro-profile-exit]'));await f.locator('.profile-remove-dialog').waitFor();await command(f,'debug');assert.equal(await playing(),false,'Start while confirmation open');assert.equal(await f.locator('.profile-remove-dialog').count(),1);await command(f,'back');
  assert.equal(await f.evaluate(()=>window.__startDebugEvents),0);
 });
 await actionCase('Gyro browser Start requires release and never replays a shortcut modifier',height,async()=>{
  await f.evaluate(()=>window.__gamepadAudit.browserStart(true,false));assert.equal(await playing(),false);
  await f.evaluate(()=>window.__gamepadAudit.browserStart(false,true,true));assert.equal(await playing(),false);
  await f.evaluate(()=>window.__gamepadAudit.browserStart(false,true,false));await f.waitForFunction(()=>window.__gyroGameAudit.playing);assert.equal(await f.evaluate(()=>window.__startDebugEvents),0);
  await command(f,'back');assert.equal(await playing(),false);
 });
 await actionCase('Motorcycle steering: admitted gyro and stick directions match the rider and visible handlebar',height,async()=>{
  await press(f,f.locator('.gyro-scene-tabs button').nth(1));const wheel=f.locator('.motorcycle-handlebar');await wheel.waitFor();
  assert.equal(await wheel.getAttribute('tabindex'),null,'decorative wheel is not an input target');
  assert.equal(await wheel.evaluate(el=>window.__gamepadAudit.focusables().includes(el)),false);
  const writes=await f.evaluate(()=>window.__fixture.writes.length),results=[];
  await command(f,'debug');assert.equal(await playing(),true);
  for(const source of ['gyro','stick'])for(const sign of [-1,1]){
   await f.evaluate(({source,sign})=>{
    window.__motorcyclePreview.patch({x:0,distance:500,speed:120,lateralSpeed:0,lean:0,steer:0,opponents:[]});
    const buttons=Array(16).fill(false);buttons[7]=true;
    window.__emitNativeEvent('gamepad.state',{connected:true,buttons,axes:[source==='stick'?sign*.7:0,0,0,0]});
    window.__motorcycleSequence=(window.__motorcycleSequence||10000)+1;
    window.dispatchEvent(new CustomEvent('input:motion-telemetry',{detail:{sequence:window.__motorcycleSequence,timestampUtc:new Date().toISOString(),sensorPresent:true,gyro:{x:0,y:0,z:0},motionAdmission:'hc-motion-admitted',motionContribution:{x:source==='gyro'?sign*.65:0,y:0}}}));
   },{source,sign});
   await f.waitForFunction(sign=>{const wheel=document.querySelector('.motorcycle-handlebar'),race=window.__gyroGameAudit.motorcycle;return Number(wheel?.getAttribute('data-steering'))*sign>.35&&race.x*sign>.005;},sign);
   const result=await f.evaluate(()=>({steer:window.__gyroGameAudit.motorcycle.steer,heading:window.__gyroGameAudit.motorcycle.x,angle:Number(document.querySelector('.motorcycle-handlebar').getAttribute('data-angle')),label:document.querySelector('.motorcycle-handlebar').getAttribute('aria-label')}));
   assert.ok(result.steer*sign>0&&result.heading*sign>0&&result.angle*sign>0,JSON.stringify(result));assert.ok(Math.abs(result.angle-result.steer*35)<2,'handlebar follows actual smoothed motorcycle steering');assert.match(result.label,sign<0?/向左倾斜/:/向右倾斜/);results.push({source,sign,...result});
  }
  const box=await wheel.boundingBox(),view=await f.locator('.gyro-scene-viewport').boundingBox(),pedals=await f.locator('.gyro-game-control-row').boundingBox();
  assert.ok(box.x>=view.x&&box.y>=view.y&&box.x+box.width<=view.x+view.width&&box.y+box.height<pedals.y,'wheel stays inside scene and above pedal buttons');
  if(task){fs.writeFileSync(path.join(task,'motorcycle-steering-'+height+'.json'),JSON.stringify({height,results,box,physicalController:false},null,2));await f.page().locator('.screen').screenshot({path:path.join(task,'evidence','motorcycle-steering-'+height+'.png')});}
  await f.evaluate(()=>{window.__emitNativeEvent('gamepad.state',{connected:true,buttons:Array(16).fill(false),axes:[0,0,0,0]});window.dispatchEvent(new CustomEvent('input:motion-telemetry',{detail:{sequence:++window.__motorcycleSequence,timestampUtc:new Date().toISOString(),sensorPresent:true,gyro:{x:0,y:0,z:0},motionAdmission:'safe-zero',motionContribution:{x:0,y:0}}}));});
  await command(f,'back');assert.equal(await playing(),false);assert.equal(await f.evaluate(()=>window.__fixture.writes.length),writes,'steering preview never saves or changes a real input configuration');
 });
 await actionCase('Motorcycle gameplay: X strikes a nearby AI rider, RT accelerates and LT brakes',height,async()=>{
  await press(f,f.locator('.gyro-scene-tabs button').nth(1));await command(f,'debug');
  await f.evaluate(()=>{window.__motorcyclePreview.patch({x:0,distance:100,speed:100,steer:0,lean:0,lateralSpeed:0,attackClock:0,hits:0,health:100,opponents:[{id:0,x:1,distance:102,speed:100,color:'#f8b14f',health:3,stagger:0,attackAt:1000,passed:false}]});const buttons=Array(16).fill(false);buttons[2]=true;window.__emitNativeEvent('gamepad.state',{connected:true,buttons,axes:[0,0,0,0]});});
  await f.waitForFunction(()=>window.__gyroGameAudit.motorcycle.hits>0);
  assert.equal(await playing(),true,'X attacks without resetting or leaving the motorcycle test');
  const speed=await f.evaluate(()=>window.__gyroGameAudit.motorcycle.speed);
  await f.evaluate(()=>{const buttons=Array(16).fill(false);buttons[7]=true;window.__emitNativeEvent('gamepad.state',{connected:true,buttons,axes:[0,0,0,0]});});await f.waitForFunction(speed=>window.__gyroGameAudit.motorcycle.speed>speed+3,speed);
  const accelerated=await f.evaluate(()=>window.__gyroGameAudit.motorcycle.speed);
  await f.evaluate(()=>{const buttons=Array(16).fill(false);buttons[6]=true;window.__emitNativeEvent('gamepad.state',{connected:true,buttons,axes:[0,0,0,0]});});await f.waitForFunction(speed=>window.__gyroGameAudit.motorcycle.speed<speed-3,accelerated);
  await f.evaluate(()=>window.__emitNativeEvent('gamepad.state',{connected:true,buttons:Array(16).fill(false),axes:[0,0,0,0]}));await command(f,'back');assert.equal(await playing(),false);
 });
 await actionCase('Gyro two-scene UI stays complete after test exit',height,async()=>{
  assert.deepEqual(await f.locator('.gyro-scene-tabs button').allTextContents(),['FPS 射击','暴力摩托','演示动作']);
  await press(f,f.locator('.gyro-scene-tabs button').first());assert.equal(await f.evaluate(()=>window.__gyroGameAudit.scene),'fps');
  assert.match(await f.locator('[data-gyro-enter-play]').textContent(),/Start/);
 });
}
/** Regression: native window ownership can precede WebView2 document focus.
 * Exercise the real Vue test window and real production command consumer.
 * Memory IPC supplies only independent sensor/pad/window/power observations.
 */
export async function gyroFullscreenCases({height,open,command,press,actionCase,page,task}){
 let f;
 const evidence=[];
 async function installLoopProbe(){
  await f.evaluate(()=>{
   if(window.__simulationLoopProbe)return;
   const pending=new Map(),request=window.requestAnimationFrame.bind(window),cancel=window.cancelAnimationFrame.bind(window);
   const transform=CanvasRenderingContext2D.prototype.setTransform;
   const probe=window.__simulationLoopProbe={paints:0,pending,old:null};
   CanvasRenderingContext2D.prototype.setTransform=function(...args){if(this.canvas.closest('.gyro-simulation-window'))probe.paints++;return transform.apply(this,args);};
   window.requestAnimationFrame=callback=>{let id;id=request(now=>{pending.delete(id);callback(now);});pending.set(id,callback);return id;};
   window.cancelAnimationFrame=id=>{pending.delete(id);return cancel(id);};
  });
  await f.waitForFunction(()=>window.__simulationLoopProbe.pending.has(window.__gyroGameAudit.frameId));
 }
 async function hiddenLoop(stop,resume){
  await installLoopProbe();
  const before=await f.evaluate(()=>{const p=window.__simulationLoopProbe,a=window.__gyroGameAudit;p.old=p.pending.get(a.frameId);return {paints:p.paints,documentVisible:document.visibilityState==='visible',scene:a.scene};});
  await f.evaluate(stop=>window.dispatchEvent(new Event(stop)),stop);
  const initial=await f.evaluate(()=>{const p=window.__simulationLoopProbe,a=window.__gyroGameAudit;return {paints:p.paints,time:a.scene==='fps'?a.range.time:a.motorcycle.time};});
  await page.waitForTimeout(350);
  const hidden=await f.evaluate(()=>{const p=window.__simulationLoopProbe,a=window.__gyroGameAudit;return {paints:p.paints,time:a.scene==='fps'?a.range.time:a.motorcycle.time,pending:a.framePending,playing:a.playing,documentVisible:document.visibilityState==='visible'};});
  await f.evaluate(resume=>window.dispatchEvent(new Event(resume)),resume);
  await f.waitForFunction(paints=>window.__simulationLoopProbe.paints>paints,hidden.paints);
  const stale=await f.evaluate(()=>{const p=window.__simulationLoopProbe,a=window.__gyroGameAudit,id=a.frameId,paints=p.paints;p.old?.(performance.now());const after=window.__gyroGameAudit;return {sameFrame:after.frameId===id,extraPaints:p.paints-paints,playing:after.playing};});
  const row={scene:before.scene,stop,resume,documentVisible:before.documentVisible&&hidden.documentVisible,observationMs:350,hiddenPaints:hidden.paints-initial.paints,modelAdvance:hidden.time-initial.time,pendingWhileHidden:hidden.pending,playingWhileHidden:hidden.playing,stale};evidence.push(row);
  assert.equal(row.documentVisible,true,'native-hidden test must leave document visible');
  assert.equal(row.hiddenPaints,0,'hidden simulator must do zero Canvas paints');
  assert.equal(row.modelAdvance,0,'hidden simulator model clock must remain stopped');
  assert.equal(row.pendingWhileHidden,false,'hidden simulator must own no animation frame');
  assert.equal(row.playingWhileHidden,false);assert.equal(stale.playing,false,'show does not resume game input');
  assert.equal(stale.sameFrame,true,'canceled old frame must not clear or replace a successor frame');assert.equal(stale.extraPaints,0,'canceled old frame must not draw after show');
 }

 async function enter(){
  await f.evaluate(()=>window.__emitNativeEvent('gamepad.state',{connected:true,buttons:Array(16).fill(false),axes:[0,0,0,0]}));
  await command(f,'debug');await f.waitForFunction(()=>window.__gyroGameAudit.playing);
 }
 async function supply(x){
  await f.evaluate(x=>{
   window.__motorcyclePreview.patch({x:0,distance:500,speed:120,lateralSpeed:0,lean:0,steer:0,opponents:[]});
   const buttons=Array(16).fill(false);buttons[7]=true;window.__emitNativeEvent('gamepad.state',{connected:true,buttons,axes:[0,0,0,0]});
   window.__fullScreenSample=(window.__fullScreenSample||4000)+1;
   window.dispatchEvent(new CustomEvent('input:motion-telemetry',{detail:{sequence:window.__fullScreenSample,timestampUtc:new Date().toISOString(),sensorPresent:true,gyro:{x:0,y:0,z:0},motionAdmission:'hc-motion-admitted',motionContribution:{x,y:0}}}));
  },x);
 }
 await actionCase('Gyro native full-screen owner accepts motion before DOM focus settles',height,async()=>{
  f=await open('/gyro-motion',height,{nativeWidth:'1'});await press(f,f.getByRole('button',{name:'模拟陀螺仪',exact:true}));await f.locator('.gyro-simulation-window').waitFor();
  await press(f,f.locator('.gyro-scene-tabs button').nth(1));
  await f.evaluate(()=>Object.defineProperty(document,'hasFocus',{configurable:true,value:()=>false}));await enter();
  for(const x of [.7,-.7]){
   await supply(x);await f.waitForFunction(x=>{const r=window.__gyroGameAudit.motorcycle;return r.steer*x>.35&&r.x*x>.005;},x,{timeout:3000});
   const r=await f.evaluate(()=>({focused:document.hasFocus(),playing:window.__gyroGameAudit.playing,steer:window.__gyroGameAudit.motorcycle.steer,heading:window.__gyroGameAudit.motorcycle.x,signal:document.querySelector('.gyro-simulation-signal').textContent}));
   assert.equal(r.focused,false);assert.equal(r.playing,true);assert.match(r.signal,/使用实际陀螺仪输出/);evidence.push({x,...r});
  }
 });
 await actionCase('Gyro native DOM blur keeps the admitted owner; real native blur ends it',height,async()=>{
  assert.ok(f);await f.evaluate(()=>window.dispatchEvent(new Event('blur')));assert.equal(await f.evaluate(()=>window.__gyroGameAudit.playing),true);
  await supply(.6);await f.waitForFunction(()=>window.__gyroGameAudit.motorcycle.steer>.35);
  await f.evaluate(()=>window.dispatchEvent(new Event('ipc:window.blur')));assert.equal(await f.evaluate(()=>window.__gyroGameAudit.playing),false);
  await f.evaluate(()=>window.dispatchEvent(new Event('ipc:window.focus')));assert.equal(await f.evaluate(()=>window.__gyroGameAudit.playing),false,'native focus cannot auto restart a test');
 });
 for(const [stop,resume] of [['ipc:window.hidden','ipc:window.shown'],['ipc:window.minimized','ipc:window.restored'],['ipc:power.suspending','ipc:power.resumed']])await actionCase('Gyro native '+stop+' stops input and '+resume+' never auto plays',height,async()=>{
  await enter();await supply(.6);await f.waitForFunction(()=>window.__gyroGameAudit.motorcycle.steer>.3);
  await hiddenLoop(stop,resume);assert.equal(await f.evaluate(()=>window.__gyroGameAudit.playing),false);
  assert.doesNotMatch(await f.locator('.gyro-simulation-signal').textContent(),/使用实际陀螺仪输出/);
 });
 await actionCase('Gyro safe-zero observation cannot become active motion in the full-screen simulator',height,async()=>{
  await enter();await supply(0);
  await f.evaluate(()=>window.dispatchEvent(new CustomEvent('input:motion-telemetry',{detail:{sequence:++window.__fullScreenSample,timestampUtc:new Date().toISOString(),sensorPresent:true,gyro:{x:0,y:0,z:40},motionAdmission:'safe-zero',motionContribution:{x:.9,y:0}}})));
  // Model reset happens synchronously; require the queued telemetry's actual presentation too.
  await f.waitForFunction(()=>window.__gyroGameAudit.playing === true &&
    (document.querySelector('.gyro-simulation-signal')?.textContent || '').includes('等待触发或有效输出') &&
    Math.abs(window.__gyroGameAudit.motorcycle.steer)<.01,null,{timeout:2000});
  assert.doesNotMatch(await f.locator('.gyro-simulation-signal').textContent(),/使用实际陀螺仪输出/);
  await command(f,'back');await command(f,'back');assert.equal(await f.locator('.gyro-simulation-window').count(),0);
 });
 await actionCase('Gyro native FPS receives aiming and firing while DOM focus is unsettled',height,async()=>{
  f=await open('/gyro-motion',height,{nativeWidth:'1'});await press(f,f.getByRole('button',{name:'模拟陀螺仪',exact:true}));await f.locator('.gyro-simulation-window').waitFor();
  await f.evaluate(()=>Object.defineProperty(document,'hasFocus',{configurable:true,value:()=>false}));await enter();
  await f.evaluate(()=>{
   const buttons=Array(16).fill(false);buttons[7]=true;window.__emitNativeEvent('gamepad.state',{connected:true,buttons,axes:[0,0,0,0]});
   window.dispatchEvent(new CustomEvent('input:motion-telemetry',{detail:{sequence:5000,timestampUtc:new Date().toISOString(),sensorPresent:true,gyro:{x:0,y:0,z:0},motionAdmission:'hc-motion-admitted',motionContribution:{x:.6,y:0}}}));
  });
  await f.waitForFunction(()=>window.__gyroGameAudit.range.shots>0&&window.__gyroGameAudit.range.yaw>.02,null,{timeout:3000});
  const range=await f.evaluate(()=>({yaw:window.__gyroGameAudit.range.yaw,shots:window.__gyroGameAudit.range.shots}));evidence.push({scene:'fps',...range});
  await hiddenLoop('ipc:window.hidden','ipc:window.shown');
  await f.evaluate(()=>{const p=window.__simulationLoopProbe;p.old=p.pending.get(window.__gyroGameAudit.frameId);});
  await command(f,'back');assert.equal(await f.locator('.gyro-simulation-window').count(),0);
  const retired=await f.evaluate(()=>{const p=window.__simulationLoopProbe,paints=p.paints;p.old?.(performance.now());return {paints:p.paints-paints,pending:window.__gyroGameAudit.framePending};});
  assert.equal(retired.paints,0,'unmounted simulation cannot draw from a queued old callback');assert.equal(retired.pending,false,'unmounted simulator must retire its animation ticket');
 });
 await actionCase('Gyro simulation mounted during native-hidden owns no frame until shown',height,async()=>{
  f=await open('/gyro-motion',height,{nativeWidth:'1'});
  await f.evaluate(()=>{window.dispatchEvent(new Event('ipc:window.hidden'));document.querySelector('.gyro-simulation-open').click();});
  await f.locator('.gyro-simulation-window').waitFor({state:'attached'});await page.waitForTimeout(120);
  const hidden=await f.evaluate(()=>({pending:window.__gyroGameAudit.framePending,time:window.__gyroGameAudit.range.time,playing:window.__gyroGameAudit.playing}));
  assert.equal(hidden.pending,false);assert.equal(hidden.time,0);assert.equal(hidden.playing,false);
  await f.evaluate(()=>window.dispatchEvent(new Event('ipc:window.shown')));
  await f.waitForFunction(()=>window.__gyroGameAudit.range.time>0&&window.__gyroGameAudit.framePending);
  assert.equal(await f.evaluate(()=>window.__gyroGameAudit.playing),false);
  await command(f,'back');assert.equal(await f.locator('.gyro-simulation-window').count(),0);
 });
 await actionCase('Gyro document-hidden pauses the same model without allocating repeated frames',height,async()=>{
  f=await open('/gyro-motion',height,{nativeWidth:'1'});await press(f,f.getByRole('button',{name:'模拟陀螺仪',exact:true}));await f.locator('.gyro-simulation-window').waitFor();
  await installLoopProbe();
  await f.evaluate(()=>{window.__simulationDocVisible='hidden';Object.defineProperty(document,'visibilityState',{configurable:true,get:()=>window.__simulationDocVisible});Object.defineProperty(document,'hidden',{configurable:true,get:()=>window.__simulationDocVisible==='hidden'});document.dispatchEvent(new Event('visibilitychange'));});
  const start=await f.evaluate(()=>({paints:window.__simulationLoopProbe.paints,time:window.__gyroGameAudit.range.time}));await page.waitForTimeout(150);
  const hidden=await f.evaluate(()=>({paints:window.__simulationLoopProbe.paints,time:window.__gyroGameAudit.range.time,pending:window.__gyroGameAudit.framePending}));
  assert.equal(hidden.paints,start.paints);assert.equal(hidden.time,start.time);assert.equal(hidden.pending,false);
  await f.evaluate(()=>{window.__simulationDocVisible='visible';document.dispatchEvent(new Event('visibilitychange'));});
  await f.waitForFunction(time=>window.__gyroGameAudit.range.time>time,start.time);assert.equal(await f.evaluate(()=>window.__gyroGameAudit.playing),false);
  await command(f,'back');assert.equal(await f.locator('.gyro-simulation-window').count(),0);
 });
 await actionCase('Gyro H5 still requires browser focus and stops on browser blur',height,async()=>{
  f=await open('/gyro-motion',height);await press(f,f.getByRole('button',{name:'模拟陀螺仪',exact:true}));await f.locator('.gyro-simulation-window').waitFor();
  await press(f,f.locator('.gyro-scene-tabs button').nth(1));await f.evaluate(()=>Object.defineProperty(document,'hasFocus',{configurable:true,value:()=>false}));await enter();await supply(.7);await page.waitForTimeout(120);
  assert.match(await f.locator('.gyro-simulation-signal').textContent(),/等待测试窗口接管输入/);
  await f.evaluate(()=>window.dispatchEvent(new Event('blur')));assert.equal(await f.evaluate(()=>window.__gyroGameAudit.playing),false);
 });
 if(task)fs.writeFileSync(path.join(task,'gyro-fullscreen-'+height+'.json'),JSON.stringify({height,evidence,source:'Actual mainline Vue/engine; independently controlled memory sensor and native focus boundaries',physicalController:false},null,2));
}
// End-to-end renderer regression for the exported G3E Ready/native versus
// powerReady=false/frontend split. Native events use the real admission queue.
export async function gyroPowerRecoveryCases({height,open,press,page,actionCase,task}){
 const f=await open('/gyro-motion',height,{nativeWidth:'1'}),receipts=[];
 await press(f,f.getByRole('button',{name:'模拟陀螺仪',exact:true}));await f.locator('.gyro-simulation-window').waitFor();await press(f,f.locator('.gyro-scene-tabs button').nth(1));
 await f.evaluate(()=>{window.__stopPowerGamepad=window.__gamepadAudit.startNative();Object.defineProperty(document,'hasFocus',{configurable:true,value:()=>false});});
 const event=async(name,generation)=>{await f.evaluate(({name,generation})=>window.dispatchEvent(new CustomEvent(name,{detail:generation===undefined?{}:{generation}})),{name,generation});await page.waitForTimeout(60);};
 const state=()=>f.evaluate(()=>window.__uiPowerState());
 async function nativeStart(){await f.evaluate(()=>{const p={connected:true,buttons:Array(16).fill(false),axes:[0,0,0,0]};window.__emitNativeEvent('gamepad.state',p);window.dispatchEvent(new CustomEvent('ipc:gamepad.state',{detail:p}));window.__gamepadAudit.nativeCommand('debug');});}
 await actionCase('G3E missed power commit: native Ready restores gyro and actual queued gamepad commands',height,async()=>{
  await event('ipc:power.suspending',2);assert.equal((await state()).powerReady,false);
  // This is the observed device state. Deliberately omit power.resumed.
  await f.evaluate(()=>Object.assign(window.__uiPowerFixture.state,{phase:'ready',generation:2,resumeReady:true}));await event('ipc:window.shown');
  await f.waitForFunction(()=>window.__uiPowerState().visible&&window.__uiPowerState().powerReady,null,{timeout:3000});
  await nativeStart();await f.waitForFunction(()=>window.__gyroGameAudit.playing,null,{timeout:3000});
  await f.evaluate(()=>{window.__motorcyclePreview.patch({x:0,distance:500,speed:120,lateralSpeed:0,lean:0,steer:0,opponents:[]});window.dispatchEvent(new CustomEvent('input:motion-telemetry',{detail:{sequence:8000,timestampUtc:new Date().toISOString(),sensorPresent:true,gyro:{x:0,y:0,z:0},motionAdmission:'hc-motion-admitted',motionContribution:{x:.65,y:0}}}));});
  await f.waitForFunction(()=>window.__gyroGameAudit.motorcycle.steer>.35&&window.__gyroGameAudit.motorcycle.x>.005);
  receipts.push({case:'missed-commit',state:await state(),wheel:await f.locator('.motorcycle-handlebar').getAttribute('aria-label')});
  await event('ipc:power.resuming',2);assert.equal((await state()).visible,true,'late same-generation pending cannot latch the frontend closed');
 });
 await actionCase('G3E next real sleep stays closed; showing the window cannot wake or restart it',height,async()=>{
  await f.evaluate(()=>Object.assign(window.__uiPowerFixture.state,{phase:'suspended',generation:3,resumeReady:false}));await event('ipc:power.suspending',3);await event('ipc:window.shown');
  assert.equal((await state()).powerReady,false);assert.equal(await f.evaluate(()=>window.__gyroGameAudit.playing),false);
  await nativeStart();await page.waitForTimeout(120);assert.equal(await f.evaluate(()=>window.__gyroGameAudit.playing),false,'real native queued command is rejected while native remains suspended');
  await event('ipc:power.resumed',2);assert.equal((await state()).powerReady,false,'old commit cannot release new sleep');
 });
 await actionCase('G3E late native snapshot cannot release a newer sleep and focus can reconcile its own Ready',height,async()=>{
  await f.evaluate(()=>{Object.assign(window.__uiPowerFixture.state,{phase:'ready',generation:3,resumeReady:true});window.__uiPowerFixture.holdNext=true;});
  await event('ipc:window.shown');await f.waitForFunction(()=>window.__uiPowerFixture.pending.length===1);
  await f.evaluate(()=>Object.assign(window.__uiPowerFixture.state,{phase:'suspended',generation:4,resumeReady:false}));await event('ipc:power.suspending',4);
  await f.evaluate(()=>window.__uiPowerFixture.release());await page.waitForTimeout(80);assert.equal((await state()).powerReady,false);
  await f.evaluate(()=>Object.assign(window.__uiPowerFixture.state,{phase:'ready',generation:4,resumeReady:true}));await event('ipc:window.focus');await f.waitForFunction(()=>window.__uiPowerState().visible);
  assert.equal(await f.evaluate(()=>window.__gyroGameAudit.playing),false,'reconciliation restores presentation only, not the test session');
 });
 await actionCase('G3E presentation recovery does not invoke a power commit, input restart or hardware action',height,async()=>{
  const calls=await f.evaluate(()=>window.__previewCalls.map(c=>c.cmd));
  assert.equal(calls.filter(c=>c==='power.resumeComplete'||c==='gamepad.restart'||c==='os.sleep'||c==='virtualGamepad.rearm').length,0);
  assert.ok(await f.evaluate(()=>window.__uiPowerFixture.calls>0));
  receipts.push({case:'no-hardware-recovery',state:await state(),nativePowerReads:await f.evaluate(()=>window.__uiPowerFixture.calls)});
  await f.evaluate(()=>window.__stopPowerGamepad?.());
 });
 if(task)fs.writeFileSync(path.join(task,'gyro-power-recovery-'+height+'.json'),JSON.stringify({height,receipts,source:'Actual renderer lifecycle, production queued gamepad admission and real gyro simulation; native power IO is memory only',physicalController:false},null,2));
}
// The gyro page must keep accepting a new preset while a prior write is pending.
// Version conflicts are injected only at the memory native settings port.
export async function gyroPresetSwitchCases({height,open,press,actionCase,page}){
 const f=await open('/gyro-motion',height,{nativeWidth:'1',controllerScope:'global'});
 const presets=f.locator('.gyro-preset-field .seg-btn');
 await presets.filter({hasText:'赛车'}).waitFor();
 await actionCase('Gyro preset: native revision conflict can switch racing back to FPS',height,async()=>{
  await f.evaluate(()=>{window.__gyroPresetFixture.armed=true;window.__gyroPresetFixture.conflictNext=true;});
  await press(f,presets.filter({hasText:'赛车'}));
  await f.waitForFunction(()=>window.__gyroPresetFixture.last?.input?.gyroMotion?.preset==='racing',null,{timeout:5000});
  await f.waitForFunction(()=>!document.querySelector('.live-status')?.textContent?.includes('正在记录'));
  await press(f,presets.filter({hasText:'FPS'}));
  await f.waitForFunction(()=>window.__gyroPresetFixture.last?.input?.gyroMotion?.preset==='fps',null,{timeout:5000});
  const facts=await f.evaluate(()=>window.__gyroPresetFixture.writes);assert.ok(facts.some(r=>r.kind==='conflict'));assert.equal(facts.at(-1).preset,'fps');
 });
 await actionCase('Gyro preset: choices during a held save keep the latest request and remain reachable',height,async()=>{
  await f.waitForFunction(()=>!document.querySelector('.live-status')?.textContent?.includes('正在记录'));
  await f.evaluate(()=>window.__gyroPresetFixture.holdNext=true);
  await press(f,presets.filter({hasText:'赛车'}));await f.waitForFunction(()=>window.__gyroPresetFixture.pending);
  assert.equal(await presets.filter({hasText:'FPS'}).isEnabled(),true);
  await press(f,presets.filter({hasText:'FPS'}));
  await press(f,presets.filter({hasText:'自定义'}));
  await press(f,presets.filter({hasText:'FPS'}));
  await f.evaluate(()=>window.__gyroPresetFixture.release());
  await f.waitForFunction(()=>window.__gyroPresetFixture.last?.input?.gyroMotion?.preset==='fps'&&!window.__gyroPresetFixture.pending,null,{timeout:5000});
  assert.match(await presets.filter({hasText:'FPS'}).getAttribute('class'),/active/);
  await f.waitForFunction(()=>!document.querySelector('.live-status')?.textContent?.includes('正在记录'));
  const facts=await f.evaluate(()=>window.__gyroPresetFixture.writes);assert.equal(facts.at(-1).preset,'fps');
  await presets.filter({hasText:'FPS'}).evaluate(el=>window.__gamepadAudit.focus(el));await f.evaluate(()=>window.__gamepadAudit.command('nav-right'));assert.equal(await presets.filter({hasText:'赛车'}).evaluate(el=>document.activeElement===el),true);
 });
}

// Reuse the original gyro page and simulator acceptance in the bounded display import.
export async function gyroPageNavigationCases({height,open,command,press,actionCase,state,shot,page,task}){
 let f;
  f=await open('/gyro-motion',height);
  await actionCase('Gyro unrecognized state retains equal actions and usable settings',height,async()=>{await f.evaluate(async()=>{window.__fixture.profileInput.gyroOverride='off';await window.__profileChanged();});await f.locator('.gyro-power-btn.unknown').waitFor();assert.ok(await f.locator('.gyro-power-btn.unknown').isDisabled());await state(f,'gyro/unrecognized',height);const layout=await f.locator('.gyro-power-row>.gyro-power-btn').evaluateAll(els=>els.map(el=>el.getBoundingClientRect().toJSON()));assert.ok(Math.abs(layout[0].y-layout[1].y)<1&&Math.abs(layout[0].width-layout[1].width)<1);});
  f=await open('/gyro-motion',height);await press(f,f.getByRole('button',{name:'展开',exact:true}));await state(f,'gyro/advanced',height);await press(f,f.getByRole('button',{name:'赛车',exact:true}));await state(f,'gyro/racing',height);await press(f,f.locator('.seg-btn').filter({hasText:/^Steam$/}));await state(f,'gyro/steam',height);await press(f,f.getByRole('button',{name:'自定义',exact:true}));await state(f,'gyro/custom',height);
  f=await open('/gyro-motion',height);await press(f,f.getByRole('button',{name:'模拟陀螺仪',exact:true}));await f.locator('.gyro-simulation-window').waitFor();await page.waitForTimeout(130);await state(f,'gyro/simulation',height,'.gyro-simulation-window');
  await gyroNavigationCases({f,height,command,press,actionCase,task});
  await actionCase('Gyro simulation dropdown B stays inside; LB/RB page lock',height,async()=>{const trigger=f.locator('.gyro-simulation-window .dd-trigger').first();await press(f,trigger);await f.locator('[role="listbox"]').waitFor();await command(f,'nav-down');await command(f,'back');await f.waitForFunction(()=>!document.querySelector('[role="listbox"]'));assert.equal(await f.locator('.gyro-simulation-window').count(),1);await command(f,'page-next');assert.equal(await f.locator('.gyro-simulation-window').count(),1);assert.ok(await f.locator('.route-view[data-route="/gyro-motion"]').count());});
  await press(f,f.getByRole('button',{name:'展开',exact:true}));await state(f,'gyro/simulation-advanced',height,'.gyro-simulation-window');await shot('gyro-simulation-'+height);
  await actionCase('Gyro nested deletion B stays in simulation then B exits',height,async()=>{await press(f,f.locator('[data-gyro-profile-exit]'));await f.locator('.profile-remove-dialog').waitFor();await state(f,'gyro/simulation-delete',height,'.profile-remove-dialog');await command(f,'back');assert.equal(await f.locator('.profile-remove-dialog').count(),0);assert.equal(await f.locator('.gyro-simulation-window').count(),1);await command(f,'back');assert.equal(await f.locator('.gyro-simulation-window').count(),0);assert.equal(await f.locator('.gyro-integrated-page').count(),1);});
  await actionCase('Native Start outside simulator does not open global debug',height,async()=>{
   const before=await f.evaluate(()=>window.__startDebugEvents);assert.equal(before,0);await command(f,'debug');assert.equal(await f.evaluate(()=>window.__startDebugEvents),before);
  });
  await actionCase('Browser Start outside simulator does not open global debug',height,async()=>{
   const before=await f.evaluate(()=>window.__startDebugEvents);assert.equal(before,0);await f.evaluate(()=>window.__gamepadAudit.browserStart(false,true,false));assert.equal(await f.evaluate(()=>window.__startDebugEvents),before);
  });
}

async function gyroControlSignal(f,admission='hc-motion-admitted'){
 await f.evaluate(admission=>window.dispatchEvent(new CustomEvent('input:motion-telemetry',{detail:{
  sequence:window.__gyroControlSequence=(window.__gyroControlSequence||0)+1,timestampUtc:new Date().toISOString(),sensorPresent:true,
  gyro:{x:-13.4,y:3.9,z:-5.8},accel:{x:0,y:0,z:1},motionAdmission:admission,motionContribution:{x:.43,y:.27}
 }})),admission);
 await f.waitForFunction(admitted=>{
  const p=document.querySelector('.gyro-input-output-preview'),label=p?.querySelector('.stick-visual')?.getAttribute('aria-label');
  return admitted?p?.classList.contains('receiving')&&label?.includes('+43%'):!p?.classList.contains('receiving')&&label?.includes('—');
 },admission==='hc-motion-admitted',{timeout:5000});
}
export async function gyroControlCases({height,open,command,press,actionCase,state,shot}){
 for(const scope of ['global','dedicated'])await actionCase('Gyro control '+scope+': live data, layout and passive preview',height,async()=>{
  const f=await open('/gyro-motion',height,{controllerScope:scope});
  await state(f,'gyro/control/'+scope,height);
  await gyroControlSignal(f);
  assert.equal(await f.locator('.gyro-workflow-card>.card-title').innerText(),'陀螺仪控制');
  assert.equal(await f.locator('.gyro-state-line').count(),1);
  assert.equal(await f.locator('.gyro-stick-preview .gyro-state-line').count(),1);
  assert.equal(await f.locator('.gyro-stick-preview .gyro-space-caption').count(),1);
  assert.deepEqual(await f.locator('.gyro-info-row small').allTextContents(),['触发','输入','输出']);
  assert.deepEqual(await f.locator('.gyro-xyz-axis b').allTextContents(),['-13.4','3.9','-5.8']);
  assert.equal(await f.locator('.gyro-stick-heading,.gyro-stick-signal,.gyro-stick-coordinates').count(),0);
  assert.ok(!/%|有效输出|摇杆预览/.test(await f.locator('.gyro-input-output-preview').innerText()));
  const layout=await f.locator('.gyro-input-output-preview').evaluate(el=>{
   const box=e=>e.getBoundingClientRect().toJSON(),left=el.querySelector('.gyro-stick-preview'),right=el.querySelector('.gyro-flow-summary');
   return {left:box(left),right:box(right),rows:[...right.children].map(box),focusable:window.__gamepadAudit.focusables().filter(e=>el.contains(e)).length,overflow:el.scrollWidth-el.clientWidth};
  });
  assert.equal(layout.focusable,0);assert.ok(layout.overflow<=1);
  assert.ok(Math.abs(layout.left.y-layout.right.y)<1&&Math.abs(layout.left.height-layout.right.height)<1);
  assert.ok(Math.max(...layout.rows.map(r=>r.height))-Math.min(...layout.rows.map(r=>r.height))<1);
  await f.locator('.gyro-workflow-card').scrollIntoViewIfNeeded();await shot('gyro-control-'+scope+'-'+height);
  await gyroControlSignal(f,'safe-zero');assert.match(await f.locator('.gyro-state-line').innerText(),/传感器已连接/);
  await gyroControlSignal(f);
  await press(f,f.getByRole('button',{name:'赛车',exact:true}));
  await f.waitForFunction(()=>document.querySelector('.gyro-flow-summary')?.textContent.includes('左摇杆'));
  assert.match(await f.locator('.gyro-flow-summary').innerText(),/左摇杆 · 横向/);
  await press(f,f.locator('.seg-btn').filter({hasText:/^Steam$/}));
  await f.waitForFunction(()=>document.querySelector('.gyro-flow-summary')?.textContent.includes('Steam 体感'));
  assert.equal(await f.locator('.gyro-input-output-preview.receiving').count(),0);
  await press(f,f.getByRole('button',{name:'FPS射击',exact:true}));await gyroControlSignal(f);
  await press(f,f.locator('.gyro-power-btn:not(.link)'));await f.waitForFunction(()=>!document.querySelector('.gyro-input-output-preview.receiving'));
  assert.match(await f.locator('.stick-visual').getAttribute('aria-label'),/—/);
 });
 await actionCase('Gyro control sliders retain A, LT/RT edit and B exit',height,async()=>{
  const f=await open('/gyro-motion',height,{controllerScope:'global'});
  await press(f,f.getByRole('button',{name:'展开',exact:true}));
  const slider=f.locator('input[type=range]:not(:disabled)').first();
  await slider.evaluate(el=>window.__gamepadAudit.focus(el));const before=Number(await slider.inputValue());
  await command(f,'slider-increase');assert.equal(Number(await slider.inputValue()),before);
  await command(f,'confirm');assert.equal(await f.evaluate(()=>window.__gamepadAudit.editing()),true);
  await command(f,'slider-increase');const after=Number(await slider.inputValue());assert.ok(after>before);
  await command(f,'slider-decrease');assert.ok(Number(await slider.inputValue())<after);
  await command(f,'back');assert.equal(await f.evaluate(()=>window.__gamepadAudit.editing()),false);
  await command(f,'nav-up');assert.equal(await slider.evaluate(el=>document.activeElement===el),false);
  await state(f,'gyro/control/slider-return',height);
 });
 await actionCase('Gyro control LB/RB keeps existing page navigation',height,async()=>{
  const f=await open('/gyro-motion',height,{controllerScope:'global'});
  await command(f,'page-next');await f.locator('.route-view[data-route="/steam"]').waitFor();
  await command(f,'page-prev');await f.locator('.gyro-integrated-page').waitFor();
  assert.equal(await f.evaluate(()=>window.__gamepadAudit.editing()),false);
  await state(f,'gyro/control/page-return',height);
 });
 await actionCase('Gyro control simulator mirrors telemetry and square output',height,async()=>{
  const f=await open('/gyro-motion',height);
  await press(f,f.getByRole('button',{name:'模拟陀螺仪',exact:true}));await f.locator('.gyro-simulation-window').waitFor();
  assert.equal(await f.locator('.gyro-state-line').count(),1);assert.equal(await f.locator('.gyro-simulation-window .gyro-input-output-preview').count(),1);
  await gyroControlSignal(f);await press(f,f.getByRole('button',{name:'展开',exact:true}));
  await press(f,f.locator('.dd-trigger[data-gp-row="27"]'));await f.locator('[role=listbox]').waitFor();
  await press(f,f.getByRole('option',{name:/方形/}));
  await f.waitForFunction(()=>document.querySelector('.stick-visual rect[x="37"]'));
  await gyroControlSignal(f);await state(f,'gyro/control/simulation-square',height,'.gyro-simulation-window');
  await gyroControlSignal(f,'safe-zero');await command(f,'back');assert.equal(await f.locator('.gyro-simulation-window').count(),0);
 });
}

export const gyroControlSelection={
  "actions": [
    "Gyro control global: live data, layout and passive preview",
    "Gyro control dedicated: live data, layout and passive preview",
    "Gyro control sliders retain A, LT/RT edit and B exit",
    "Gyro control LB/RB keeps existing page navigation",
    "Gyro control simulator mirrors telemetry and square output",
    "Gyro A edits controls; Start enters from right and B returns without slider edit leak",
    "Gyro right settings can reach and return from both left scene tabs by directions",
    "Gyro dropdown and nested confirmation retain Start ownership",
    "Gyro browser Start requires release and never replays a shortcut modifier",
    "Motorcycle steering: admitted gyro and stick directions match the rider and visible handlebar",
    "Motorcycle gameplay: X strikes a nearby AI rider, RT accelerates and LT brakes",
    "Gyro two-scene UI stays complete after test exit",
    "Gyro unrecognized state retains equal actions and usable settings",
    "Gyro simulation dropdown B stays inside; LB/RB page lock",
    "Gyro nested deletion B stays in simulation then B exits",
    "Native Start outside simulator does not open global debug",
    "Browser Start outside simulator does not open global debug"
  ],
  "states": [
    "gyro/control/global",
    "gyro/control/dedicated",
    "gyro/control/slider-return",
    "gyro/control/page-return",
    "gyro/control/simulation-square",
    "gyro/unrecognized",
    "gyro/advanced",
    "gyro/racing",
    "gyro/steam",
    "gyro/custom",
    "gyro/simulation",
    "gyro/simulation-advanced",
    "gyro/simulation-delete"
  ]
};
