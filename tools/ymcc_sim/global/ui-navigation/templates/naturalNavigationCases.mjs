import {performanceCombinedNavigation} from './performanceCombinedNavigation.mjs';
import assert from 'node:assert/strict';
export async function naturalNavigationCases({page,height,open,command,press,actionCase,state,task,recordState=()=>{}}){
 const evidence=[];
 async function edge(f,from,action,to,label){
  assert.equal(await from.evaluate(el=>window.__gamepadAudit.focusables().includes(el)),true,label+' starts from an enabled focus target');
  await from.evaluate(el=>window.__gamepadAudit.focus(el));await page.waitForTimeout(20);
  assert.equal(await from.evaluate(el=>document.activeElement===el),true,label+' origin actually received focus');
  await command(f,action);
  const good=await to.evaluate(el=>document.activeElement===el);const actual=await f.evaluate(()=>document.activeElement?.getAttribute('aria-label')||document.activeElement?.textContent?.trim().slice(0,100));
  assert.ok(good,label+' selected '+actual);evidence.push({label,action,actual});
 }
 async function locked(f,label){
  const before=await f.evaluate(()=>({route:window.__currentAuditRoute(),active:document.activeElement}));
  for(const action of ['page-prev','page-next','edit-game']){
   const original=await f.evaluateHandle(()=>document.activeElement);await command(f,action);
   assert.equal(await f.evaluate(()=>window.__currentAuditRoute()),before.route,label+' route locked');
   assert.equal(await f.evaluate(el=>document.activeElement===el,original),true,label+' focus retained on '+action);
   assert.equal(await f.locator('.game-recognition-popover').count(),0,label+' Y cannot open a background menu');
  }
 }
 for(const scope of ['global','dedicated'])await actionCase('Natural columns: four mode buttons to two performance fields / '+scope,height,async()=>{
  const f=await open('/schedule',height);if(scope==='dedicated'){await command(f,'edit-game');await press(f,f.locator('[data-gp-custom-entry]'));}
  const host=f.locator(scope==='dedicated'?'[data-gp-custom-body] .continuous-performance':'.route-view .continuous-performance');
  const manual=host.getByRole('button',{name:'手动模式',exact:true}),tdp=host.getByRole('slider',{name:'TDP',exact:true}),maximum=host.getByRole('button',{name:'最大 TDP',exact:true}),dc=host.getByRole('button',{name:'电池模式',exact:true});
  await edge(f,manual,'nav-down',tdp,scope+' manual down -> TDP in same visual column');
  await edge(f,tdp,'nav-up',manual,scope+' TDP up -> manual');
  await edge(f,maximum,'nav-up',dc,scope+' maximum up -> battery');
 });
 await actionCase('Natural columns: top menu buttons retain visual column across changing row counts',height,async()=>{
  const f=await open('/schedule',height);await command(f,'edit-game');const menu=f.locator('[data-gp-game-quick-menu]');
  const mouse=menu.locator('.quick-game-controls button').last(),remove=menu.locator('[data-gp-custom-remove]');
  await edge(f,mouse,'nav-down',remove,'top mouse down -> profile right action');await edge(f,remove,'nav-up',mouse,'profile right action up -> mouse');
  const speed=menu.getByRole('button',{name:'游戏变速倍率',exact:true}),close=menu.locator('.game-quick-footer button').last();
  await edge(f,speed,'nav-down',close,'speed down -> B footer');await edge(f,close,'nav-up',speed,'B footer up -> speed');
  // Unknown backend state must stay unavailable; never force a disabled origin to fake a navigation pass.
  await f.evaluate(()=>window.dispatchEvent(new CustomEvent('gp:mouse-mode',{detail:{on:false,provider:'ymcc',controlAllowed:false,stateKnown:false}})));
  await page.waitForTimeout(30);assert.equal(await mouse.isDisabled(),true,'unknown mouse readback keeps the real control disabled');
  assert.equal(await mouse.evaluate(el=>window.__gamepadAudit.focusables().includes(el)),false,'unknown mouse control is excluded from gamepad candidates');
  await edge(f,menu.locator('.quick-game-controls button').nth(1),'nav-down',menu.locator('[data-gp-custom-entry]'),'disabled mouse does not disturb the enabled left column');
 });
 await actionCase('Natural rows: touchpad sliders and feedback sections retain navigation and B return',height,async()=>{
  const f=await open('/button-mapping',height);await press(f,f.locator('[data-controller-section="touchpad"]'));
  const master=f.getByRole('switch',{name:'触控板总开关',exact:true});if(await master.getAttribute('aria-checked')!=='true')await press(f,master);
  await edge(f,f.getByRole('slider',{name:'触摸板缩放',exact:true}),'nav-right',f.getByRole('slider',{name:'触摸板显示透明度',exact:true}),'touchpad sliders stay on their visual row');
  await command(f,'back');assert.equal(await f.locator('[data-controller-section="touchpad"]').evaluate(el=>document.activeElement===el),true);
  await command(f,'nav-right');await command(f,'confirm');
  assert.equal(await f.locator('[data-controller-section="feedback"]').getAttribute('aria-pressed'),'true');
  await f.getByRole('slider',{name:'震动强度',exact:true}).evaluate(el=>window.__gamepadAudit.focus(el));
  await command(f,'back');assert.equal(await f.locator('[data-controller-section="feedback"]').evaluate(el=>document.activeElement===el),true);
 });
 await actionCase('Natural panes: shortcut editor vertical input stays in settings; side movement follows an actual row',height,async()=>{
  const f=await open('/button-mapping',height);await press(f,f.getByRole('button',{name:'打开编辑器',exact:true}));await f.locator('.editor-overlay').waitFor();
  const record=f.getByRole('button',{name:'重新录制输入',exact:true}),clear=f.getByRole('button',{name:'清空',exact:true});
  const trigger=f.locator('.action-trigger-bubble .seg-btn');
  await edge(f,record,'nav-down',trigger.nth(2),'record down -> center trigger row');await edge(f,trigger.first(),'nav-up',record,'trigger up -> record in same pane');
  await edge(f,clear,'nav-down',trigger.last(),'clear down -> right trigger');
  const card=f.locator('.rule-row').filter({hasText:'调节亮度'});await edge(f,card,'nav-right',card,'partial edge overlap does not make a false horizontal row');
  await edge(f,record,'nav-left',record,'record left edge does not jump diagonally into list');
  await locked(f,'shortcut editor');await state(f,'controller/editor-natural-panes',height,'.editor-overlay');
 });
 await actionCase('Natural panes: gyro header bridges left test and right settings without diagonal jumps',height,async()=>{
  const f=await open('/gyro-motion',height);await press(f,f.getByRole('button',{name:'模拟陀螺仪',exact:true}));const left=f.locator('.gyro-preview-entry'),right=f.locator('.gyro-simulation-heading button').last(),tabs=f.locator('.gyro-scene-tabs button');
  await edge(f,right,'nav-left',left,'right header left -> left preview header');await edge(f,left,'nav-down',tabs.first(),'left header down -> scene tabs');
  const first=f.locator('.gyro-simulation-editor button:not(:disabled)').first();await edge(f,right,'nav-down',first,'right header down -> settings');
  const pedals=f.locator('.gyro-game-pedal').first();await edge(f,tabs.first(),'nav-down',pedals,'scene down -> preview controls');await locked(f,'gyro simulation');
 });
 await actionCase('All slider buttons: A arms, RT edits, B cancels; unarmed LT/RT do nothing',height,async()=>{
  const f=await open('/schedule',height),tdp=f.getByRole('slider',{name:'TDP',exact:true});await tdp.evaluate(el=>window.__gamepadAudit.focus(el));const before=await tdp.inputValue();
  await command(f,'slider-decrease');assert.equal(await tdp.inputValue(),before);await command(f,'confirm');assert.equal(await f.evaluate(()=>window.__gamepadAudit.editing()),true);
  await command(f,'slider-decrease');assert.ok(Number(await tdp.inputValue())<Number(before));await command(f,'back');assert.equal(await f.evaluate(()=>window.__gamepadAudit.editing()),false);assert.equal(await tdp.inputValue(),before,'B cancels the uncommitted performance draft');
 });
 await actionCase('Actual performance simulator: live right editor, cross-document control, modal keys and isolated edits',height,async()=>{
  const f=await open('/schedule',height);await press(f,f.locator('[data-performance-simulation-entry]'));await f.locator('.simulation-editor').waitFor();const child=await(await f.locator('.performance-simulation-window iframe').elementHandle()).contentFrame();
  await child.waitForFunction(()=>window.YmccScheduleLink?.snapshot()&&window.YmccSimulationNavigation);
  const host=f.locator('.simulation-editor');
  const auto=host.getByRole('button',{name:'自动模式',exact:true});await auto.evaluate(el=>window.__gamepadAudit.focus(el));await command(f,'nav-left');
  assert.equal(await f.evaluate(()=>document.activeElement?.tagName),'IFRAME','left exits the native editor into the real preview document');
  await command(f,'nav-right');await f.waitForFunction(()=>document.activeElement?.closest('.simulation-editor'));assert.equal(await auto.evaluate(el=>document.activeElement===el),true,'right returns to the matching native row');
  const maximum=host.getByRole('button',{name:'最大 TDP',exact:true}),tdp=host.getByRole('slider',{name:'TDP',exact:true});
  await edge(f,maximum,'nav-left',tdp,'simulator dropdown left -> adjacent TDP');
  const saved=await f.evaluate(()=>window.__fixture.writes.length);await command(f,'confirm');const before=await tdp.inputValue();await command(f,'slider-decrease');assert.ok(Number(await tdp.inputValue())<Number(before));await command(f,'confirm');assert.equal(await f.evaluate(()=>window.__fixture.writes.length),saved,'temporary simulation edit does not write live controls');
  await locked(f,'performance simulator');await state(f,'performance/simulation/right',height,'.simulation-editor');
  await page.locator('.screen').screenshot({path:task+'/evidence/performance-real-'+height+'.png'});
  const autoGraph=await performanceCombinedNavigation({f,page,command,height,mode:'auto',task});recordState(autoGraph);assert.deepEqual(autoGraph.issues,[],'actual auto simulator combined navigation');
  await press(f,host.getByRole('button',{name:'手动模式',exact:true}));await host.getByRole('slider',{name:'CPU 频率上限',exact:true}).waitFor();
  const manualGraph=await performanceCombinedNavigation({f,page,command,height,mode:'manual',task});recordState(manualGraph);assert.deepEqual(manualGraph.issues,[],'actual manual simulator combined navigation');
  assert.equal(await f.evaluate(()=>window.__fixture.writes.length),saved,'simulation navigation/manual mode stays isolated from live controls');

  // Dispatch native A/B through the real parent/child owner. Cancel each
  // child dialog, restore its opener and verify no live settings were saved.
  async function childFocus(selector){
   await f.locator('.performance-simulation-window iframe').evaluate(el=>el.focus({preventScroll:true}));
   await child.locator(selector).evaluate(el=>el.focus({preventScroll:true}));
  }
  for(const [opener,dialog] of [['#session-save','#session-save-dialog'],['#session-reset','#session-reset-dialog'],['#plan-open','#scenario-plan']]){
   await childFocus(opener);await command(f,'confirm');await child.waitForFunction(selector=>document.querySelector(selector)?.open,dialog);
   await locked(f,'performance child '+dialog);await command(f,'back');await child.waitForFunction(selector=>!document.querySelector(selector)?.open,dialog);
   assert.equal(await child.locator(opener).evaluate(el=>document.activeElement===el),true,'B returns focus to '+opener);
   assert.equal(await f.locator('.performance-simulation-window').count(),1,'B cancels the child dialog without closing simulator');
  }
  assert.equal(await f.evaluate(()=>window.__fixture.writes.length),saved,'cancelled save/reset/quick simulation cannot write live controls');
  await childFocus('#simulation-return');await command(f,'back');await f.waitForFunction(()=>!document.querySelector('.performance-simulation-window'));

 });
 const fs=await import('node:fs');fs.writeFileSync(task+'/natural-navigation-'+height+'.json',JSON.stringify({height,evidence,physicalController:false},null,2));
}
