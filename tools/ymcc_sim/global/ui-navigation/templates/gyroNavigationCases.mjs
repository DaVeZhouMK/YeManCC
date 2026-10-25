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
  await f.evaluate(stop=>window.dispatchEvent(new Event(stop)),stop);assert.equal(await f.evaluate(()=>window.__gyroGameAudit.playing),false);
  await f.evaluate(resume=>window.dispatchEvent(new Event(resume)),resume);assert.equal(await f.evaluate(()=>window.__gyroGameAudit.playing),false);
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
  await command(f,'back');assert.equal(await f.evaluate(()=>window.__gyroGameAudit.playing),false);await command(f,'back');
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
