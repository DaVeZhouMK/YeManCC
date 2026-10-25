import {radialGestureCases} from './radialGestureCases.mjs';
import {radialUtilityCases} from './radialUtilityCases.mjs';
import {radialInputCases} from './radialInputCases.mjs';
import {radialFanCases} from './radialFanCases.mjs';
import {radialTouchCases} from './radialTouchCases.mjs';
import assert from 'node:assert/strict';
import path from 'node:path';
export async function radialCases({height,open,command,press,actionCase,state,page,task}){
 let f=await open('/settings',height,{nativeWidth:'1'});
 await press(f,f.locator('[data-settings-section="diagnostics"]'));
 const testingSwitch=f.locator('[data-radial-testing-toggle] button');
 await actionCase('Testing wheel defaults hidden with four equal settings tabs',height,async()=>{
  assert.equal(await f.locator('.app-body > .navrail .nav-item').filter({hasText:'触摸轮盘'}).count(),0);
  assert.equal(await testingSwitch.getAttribute('aria-checked'),'false');
  const tabs=await f.locator('.settings-section-tabs button').evaluateAll(els=>els.map(el=>({text:el.textContent.trim(),width:el.getBoundingClientRect().width})));
  assert.deepEqual(tabs.map(t=>t.text),['外观','设备','日志修复','更新']);assert.ok(tabs.every(t=>Math.abs(t.width-tabs[0].width)<1));
  assert.equal(await f.evaluate(()=>window.__previewCalls.filter(c=>['radial.configure','radial.request','radial.create'].includes(c.cmd)).length),0);
  await state(f,'settings/testing-off',height);
 });
 await actionCase('Gamepad A explicitly opts into testing and exposes wheel navigation',height,async()=>{
  await press(f,testingSwitch);await f.locator('.app-body > .navrail .nav-item').filter({hasText:'触摸轮盘'}).waitFor();
  assert.equal(await testingSwitch.getAttribute('aria-checked'),'true');await state(f,'settings/testing-on',height);
  await command(f,'page-next');await f.locator('.route-view[data-route="/quick"]').waitFor();
  await command(f,'page-prev');await f.locator('.route-view[data-route="/settings"]').waitFor();
  await command(f,'page-prev');await f.locator('.radial-settings-page').waitFor();
  assert.deepEqual(await f.locator('.radial-settings-page button[aria-pressed]').evaluateAll(els=>els.map(el=>el.getAttribute('aria-pressed'))),['false','false','false','false']);
 });
 await actionCase('Pointer disables testing, blocks direct access and preserves wheel settings',height,async()=>{
  await f.evaluate(()=>window.__navigate('/settings'));await f.locator('.route-view[data-route="/settings"]').waitFor();await f.locator('[data-settings-section="diagnostics"]').click();await f.locator('.settings-section-panel[aria-label="日志修复"]:not([hidden])').waitFor();
  await testingSwitch.click();await f.locator('.app-body > .navrail .nav-item').filter({hasText:'触摸轮盘'}).waitFor({state:'detached'});
  await f.evaluate(()=>window.__navigate('/radial'));await f.locator('.route-view[data-route="/schedule"]').waitFor();
  await f.evaluate(()=>window.__navigate('/settings'));await f.locator('.route-view[data-route="/settings"]').waitFor();await f.locator('[data-settings-section="diagnostics"]').click();await f.locator('.settings-section-panel[aria-label="日志修复"]:not([hidden])').waitFor();
  assert.equal(await testingSwitch.getAttribute('aria-checked'),'false');await state(f,'settings/testing-disabled',height);
 });
 f=await open('/radial',height);
 await actionCase('All four wheel enable channels initially display off',height,async()=>{const values=await f.locator('.radial-settings-page button[aria-pressed]').evaluateAll(buttons=>buttons.map(button=>button.getAttribute('aria-pressed')));assert.deepEqual(values,['false','false','false','false']);});
 await press(f,f.locator('[data-radial-edit]'));await f.locator('.radial-editor-window').waitFor();
 await state(f,'radial/editor',height,'.radial-editor-window');
 await f.locator('.radial-editor-window').screenshot({path:path.join(task,'evidence','radial-editor-'+height+'.png')});
 const editorPrimaryIcons=await f.locator('.radial-editor-window [data-radial-sector]').evaluateAll(nodes=>nodes.map(node=>({id:node.getAttribute('data-radial-sector'),markup:node.querySelector('.sector-icon').innerHTML})));
 await actionCase('Radial editor proportions and hollow centre',height,async()=>{
  const result=await f.evaluate(()=>{const root=document.querySelector('.radial-editor-window'),wheel=root.querySelector('[data-radial-wheel]'),r=wheel.getBoundingClientRect();return {width:root.getBoundingClientRect().width,wheel:r.width,close:root.querySelector('[data-editor-action="close"]').getBoundingClientRect().height,paths:root.querySelectorAll('.wheel-sector').length,centralButtons:[...wheel.querySelectorAll('button')].filter(el=>{const b=el.getBoundingClientRect();return Math.hypot((b.left+b.right)/2-r.left-r.width/2,(b.top+b.bottom)/2-r.top-r.width/2)<r.width*66/496;}).length};});
  assert.ok(result.width>=1000&&result.width<=1280);assert.equal(result.close,40);assert.equal(result.centralButtons,0);assert.equal(result.paths,8);assert.ok(result.wheel>=260&&result.wheel<=496);
 });
 await actionCase('Local wheel icon viewports stay bounded and never paint over the hollow centre',height,async()=>{
  const icons=await f.locator('.wheel-geometry .sector-icon').evaluateAll(nodes=>nodes.map(el=>{const wheel=el.closest('.wheel-geometry').getBoundingClientRect(),r=el.getBoundingClientRect(),scale=wheel.width/496,x=(r.x-wheel.x)/scale,y=(r.y-wheel.y)/scale,w=r.width/scale,h=r.height/scale,nearX=Math.max(x,Math.min(248,x+w)),nearY=Math.max(y,Math.min(248,y+h));return {width:w,height:h,viewport:[el.getAttribute('width'),el.getAttribute('height')],distance:Math.hypot(nearX-248,nearY-248)};}));
  assert.equal(icons.length,8);for(const icon of icons){assert.deepEqual(icon.viewport,['24','24']);assert.ok(icon.width>0&&icon.width<=26&&icon.height>0&&icon.height<=26,JSON.stringify(icon));assert.ok(icon.distance>=66,JSON.stringify(icon));}
 });
 await press(f,f.locator('[data-radial-select="windows"]'));await state(f,'radial/editor-children',height,'.radial-editor-window');
 await actionCase('Radial children extend parent, remain reachable and preserve primary ring',height,async()=>{assert.equal(await f.locator('.wheel-child').count(),3);assert.equal(await f.locator('.wheel-sector').count(),11);await press(f,f.locator('[data-radial-select="thisPc"]'));await state(f,'radial/editor-child-property',height,'.radial-editor-window');assert.equal(await f.getByRole('textbox',{name:'显示名称',exact:true}).inputValue(),'我的电脑');});
 await actionCase('Radial Dropdown A, B returns to trigger',height,async()=>{const trigger=f.locator('.radial-editor-properties .dd-trigger');await press(f,trigger);await command(f,'nav-down');await command(f,'back');await f.waitForFunction(()=>!document.querySelector('[role="listbox"]'));assert.equal(await f.locator('[role="listbox"]').count(),0);assert.ok(await trigger.evaluate(el=>document.activeElement===el));});
 await command(f,'back');await f.locator('.radial-editor-window').waitFor({state:'detached'});
 await actionCase('Radial page range enters with A, LT/RT changes, B cancels',height,async()=>{const slider=f.locator('input[type="range"]');const before=await slider.inputValue();await press(f,slider);await command(f,'slider-increase');assert.notEqual(await slider.inputValue(),before);await command(f,'back');assert.equal(await slider.inputValue(),before);});
 f=await open('/radial',height,{shortcutSeed:'legacy-custom',radialRecorder:'1'});
 await press(f,f.getByRole('button',{name:/^录制呼出按键/}));await f.locator('.editor-overlay').waitFor();await state(f,'radial/recorder',height,'.editor-overlay');
 await actionCase('Opt-in wheel action records single OEM rear key through input CAS',height,async()=>{
  await press(f,f.getByRole('button',{name:'录制输入',exact:true}));
  await f.evaluate(()=>{window.__emitNativeEvent('shortcut.recording.gamepad',{inputs:[{source:'oem',code:'back1'}],released:false});window.__emitNativeEvent('shortcut.recording.gamepad',{inputs:[{source:'oem',code:'back1'}],released:true});});
  await f.getByRole('button',{name:'下一步：选择动作',exact:true}).waitFor();await press(f,f.getByRole('button',{name:'下一步：选择动作',exact:true}));await press(f,f.locator('.output-summary'));assert.equal(await f.getByRole('button',{name:'呼出触摸轮盘',exact:true}).count(),1);await press(f,f.getByRole('button',{name:'呼出触摸轮盘',exact:true}));await press(f,f.getByRole('button',{name:'完成新建',exact:true}));await press(f,f.getByRole('button',{name:'应用修改',exact:true}));
  await f.waitForFunction(()=>window.__shortcutFixture.read().input.buttonMapping.rules.shortcutRules.some(r=>r.actionId==='radial.summon'&&r.inputs.some(input=>input.source==='oem'&&input.code==='back1')));
  const rules=await f.evaluate(()=>window.__shortcutFixture.read().input.buttonMapping.rules.shortcutRules);const summon=rules.find(r=>r.actionId==='radial.summon'&&r.inputs.some(input=>input.source==='oem'&&input.code==='back1'));assert.deepEqual(summon.inputs,[{source:'oem',code:'back1'}]);assert.ok(rules.some(r=>r.id==='custom-start-f7'));
 });
 await command(f,'back');await f.locator('.editor-overlay').waitFor({state:'detached'});
 f=await open('/radial',height,{nativeWidth:'1'});
 await actionCase('Radial editor width lease returns after nested recorder closes',height,async()=>{
  await press(f,f.locator('[data-radial-edit]'));
  await f.waitForFunction(()=>window.__editorWidthFixture.owner.startsWith('radial-editor-'));
  for(let cycle=0;cycle<2;cycle++){
   await press(f,f.locator('[data-radial-record]'));
   await f.locator('.editor-overlay').waitFor();
   await f.waitForFunction(()=>window.__editorWidthFixture.owner.startsWith('controller-editor-'));
   await command(f,'back');await f.locator('.editor-overlay').waitFor({state:'detached'});
   await f.waitForFunction(()=>window.__editorWidthFixture.owner.startsWith('radial-editor-'));
   assert.ok(await f.locator('[data-radial-record]').evaluate(el=>document.activeElement===el));
  }
  await command(f,'back');await f.locator('.radial-editor-window').waitFor({state:'detached'});
  await f.waitForFunction(()=>window.__editorWidthFixture.owner==='');
  const events=await f.evaluate(()=>window.__editorWidthFixture.events);
  assert.ok(events.length>=10);assert.ok(events.every(e=>e.ok),JSON.stringify(events));
 });
 for(const widthHold of ['parent','recorder']){
  f=await open('/radial',height,{nativeWidth:'1',widthHold});
  await actionCase('Radial slow native width receipt / '+widthHold,height,async()=>{
   await press(f,f.locator('[data-radial-edit]'));await f.waitForFunction(()=>window.__editorWidthFixture.owner.startsWith('radial-editor-'));
   if(widthHold==='parent')await f.waitForFunction(()=>window.__editorWidthFixture.waiting);
   await press(f,f.locator('[data-radial-record]'));
   if(widthHold==='recorder'){await f.locator('.editor-overlay').waitFor();await f.waitForFunction(()=>window.__editorWidthFixture.waiting);await command(f,'back');}
   await page.waitForTimeout(60);
   const pending=await f.evaluate(()=>({events:window.__editorWidthFixture.events.slice(),child:!!document.querySelector('.editor-overlay')}));
   await f.evaluate(()=>window.__editorWidthFixture.releaseReply());
   assert.equal(pending.child,widthHold==='recorder');assert.ok(pending.events.every(e=>e.ok),JSON.stringify(pending));
   if(widthHold==='parent'){await f.waitForFunction(()=>window.__editorWidthFixture.owner.startsWith('controller-editor-'));await command(f,'back');}
   await f.locator('.editor-overlay').waitFor({state:'detached'});await f.waitForFunction(()=>window.__editorWidthFixture.owner.startsWith('radial-editor-'));
   assert.ok(await f.locator('[data-radial-record]').evaluate(el=>document.activeElement===el));
   await command(f,'back');await f.locator('.radial-editor-window').waitFor({state:'detached'});await f.waitForFunction(()=>window.__editorWidthFixture.owner==='');
   assert.ok((await f.evaluate(()=>window.__editorWidthFixture.events)).every(e=>e.ok));
  });
 }
 await actionCase('Radial trusted touch reaches editor, children and TDP dial',height,async()=>{await radialTouchCases({browser:page.context().browser(),url:new URL(f.url()).origin,height,task});});
 f=await open('/radial',height,{nativeWidth:'1'});
 await actionCase('Radial reactive menu and settings persist, reopen and discard without losing saved changes',height,async()=>{
  await press(f,f.locator('[data-radial-edit]'));
  await f.getByRole('textbox',{name:'显示名称',exact:true}).fill('主菜单已保存');
  await press(f,f.locator('.radial-editor-window [data-editor-action="apply"]'));
  await f.waitForFunction(async()=>((await window.__virtualStartConsumer.loadSettings()).radialMenu?.pages?.[0]?.items?.[0]?.label)==='主菜单已保存');
  await command(f,'back');await f.locator('.radial-editor-window').waitFor({state:'detached'});
  await press(f,f.locator('[data-radial-edit]'));
  assert.equal(await f.getByRole('textbox',{name:'显示名称',exact:true}).inputValue(),'主菜单已保存');
  await f.getByRole('textbox',{name:'显示名称',exact:true}).fill('未保存的草稿');
  await command(f,'back');await f.locator('.radial-editor-window').waitFor({state:'detached'});
  await press(f,f.locator('[data-radial-edit]'));assert.equal(await f.getByRole('textbox',{name:'显示名称',exact:true}).inputValue(),'主菜单已保存');
  await command(f,'back');await f.locator('.radial-editor-window').waitFor({state:'detached'});
  const feedback=f.locator('.setting-row').filter({hasText:'触觉反馈'}).getByRole('button');
  assert.equal(await feedback.getAttribute('aria-pressed'),'false');
  await press(f,feedback);await f.waitForFunction(async()=>((await window.__virtualStartConsumer.loadSettings()).radialMenu?.feedback)===true);
  await press(f,f.locator('[data-radial-edit]'));await command(f,'back');await f.locator('.radial-editor-window').waitFor({state:'detached'});assert.equal(await feedback.getAttribute('aria-pressed'),'true');
  await press(f,feedback);await f.waitForFunction(async()=>((await window.__virtualStartConsumer.loadSettings()).radialMenu?.feedback)===false);assert.equal(await feedback.getAttribute('aria-pressed'),'false');
 });
 f=await open('/radial',height,{nativeWidth:'1'});
 await actionCase('Radial full-screen touch source status is not confused with device acceptance',height,async()=>{
  await f.evaluate(()=>{window.__fixture.radialSourceStatus={fullScreenInstalled:false,rawObservedDevices:0};});
  await press(f,f.locator('.radial-settings-card').last().getByRole('button'));
  await press(f,f.locator('[data-radial-edit]'));await command(f,'back');await f.locator('.radial-editor-window').waitFor({state:'detached'});
  await f.getByRole('status').filter({hasText:'全屏长按尚未就绪'}).waitFor();
  await f.evaluate(()=>{window.__fixture.radialSourceStatus={fullScreenInstalled:true,rawObservedDevices:0};});
  await press(f,f.locator('[data-radial-edit]'));await command(f,'back');await f.locator('.radial-editor-window').waitFor({state:'detached'});
  await f.getByRole('status').filter({hasText:'等待触摸验证'}).waitFor();
  await state(f,'radial/fullscreen-waiting',height);
  await f.evaluate(()=>{window.__fixture.radialSourceStatus={fullScreenInstalled:true,rawObservedDevices:1};});
  await press(f,f.locator('[data-radial-edit]'));await command(f,'back');await f.locator('.radial-editor-window').waitFor({state:'detached'});
  await f.getByRole('status').filter({hasText:'已检测到触摸屏输入'}).waitFor();
 });
 await f.evaluate(()=>window.__mountRadialWheel());
 await actionCase('Radial actual usage: hollow, children, dial draft/cancel and controller',height,async()=>{
  const result=await f.evaluate(()=>{
   const w=window.__radialUsage,api=w.api;const chosen=()=>document.querySelector('[data-radial-wheel] .sector-selected .sector-label')?.textContent;
   api.gamepad({lx:0,ly:1});api.gamepad({action:'accept'});api.gamepad({action:'value-up'});const before=w.events.length;api.gamepad({action:'back'});const cancelled=w.events.length===before;
   api.gamepad({lx:0,ly:1});api.gamepad({action:'accept'});api.gamepad({action:'value-up'});api.gamepad({action:'accept'});
   const tdp=w.events.find(e=>e.id==='tdp');
   api.gamepad({action:'back'});api.pointer('down',325.8,325.8,1);api.pointer('move',437,287,1);api.pointer('up',437,287,1);
   api.pointer('down',248,248,2);api.pointer('up',248,248,2);const afterCentre=w.events.length;
   api.gamepad({lx:1,ly:0});api.gamepad({action:'accept'});
   return {cancelled,tdp,actions:w.events,afterCentre};
  });
  assert.ok(result.cancelled);assert.deepEqual(result.tdp,{id:'tdp',value:16});assert.ok(result.actions.some(e=>e.id==='thisPc'));assert.ok(result.actions.some(e=>e.id==='ymcc'));
 });
 await f.locator('[data-radial-wheel]').screenshot({path:path.join(task,'evidence','radial-usage-'+height+'.png')});
 await f.evaluate(()=>window.__radialUsage.unmount());
 await radialGestureCases({height,browser:page.context().browser(),url:new URL(f.url()).origin,actionCase,task});
 await radialUtilityCases({height,browser:page.context().browser(),url:new URL(f.url()).origin,actionCase,task,editorPrimaryIcons});
 await radialInputCases({height,browser:page.context().browser(),url:new URL(f.url()).origin,actionCase,task});
 await radialFanCases({height,browser:page.context().browser(),url:new URL(f.url()).origin,actionCase,task});
 await radialPerformanceCases({height,browser:page.context().browser(),url:new URL(f.url()).origin,actionCase,task});
}


export async function radialPerformanceCases({height,browser,url,actionCase,task}){
 const context=await browser.newContext({viewport:{width:1280,height},hasTouch:true}),page=await context.newPage();
 const run=async name=>{await page.goto(new URL('/product.html?route=/radial&height='+height,url).href);await page.waitForFunction(()=>window.__previewReady&&window.__mountRadialPerformanceWheel);await page.evaluate(()=>window.__mountRadialPerformanceWheel());};
 const read=()=>page.evaluate(()=>window.__radialPerformance.read());
 const settled=()=>page.waitForFunction(()=>!window.__radialPerformance.read().pending);
 const parent=async()=>{await page.evaluate(()=>{const api=window.__radialPerformance.api;api.gamepad({lx:-Math.SQRT1_2,ly:Math.SQRT1_2});api.gamepad({action:'accept'});});await page.locator('[data-radial-control="mode"]').waitFor();};
 const controller=async key=>{await page.evaluate(async key=>{
  const w=window.__radialPerformance,target=w.read().frame.controls.findIndex(control=>control.key===key);
  if(target<0)throw Error('Missing performance control '+key);
  for(let index=0;index<12;index++){
   const selected=document.querySelector('.sector-selected[data-radial-control]')?.getAttribute('data-radial-control');
   if(selected===key){w.api.gamepad({action:'accept'});return;}
   const at=w.read().frame.controls.findIndex(control=>control.key===selected);w.api.gamepad({action:at>target?'nav-left':'nav-right'});await new Promise(resolve=>setTimeout(resolve,0));
  }throw Error('Controller could not reach '+key);
 },key);};
 const point=async selector=>page.locator(selector+' .sector-label').evaluate(el=>{const root=el.closest('.wheel-geometry'),r=root.getBoundingClientRect();return{x:r.x+Number(el.getAttribute('x'))*r.width/496,y:r.y+(Number(el.getAttribute('y'))-16)*r.height/496};});
 const tap=async selector=>{const p=await point(selector);await page.touchscreen.tap(p.x,p.y);};
 try{
  await actionCase('Performance controller reaches automatic sliders, preserves main ring and cancels drafts',height,async()=>{
   await run();await parent();assert.equal(await page.locator('.wheel-sector').count(),12);
   assert.deepEqual((await read()).frame.controls.map(control=>control.key),['tdp','mode','bias','saving']);
   await controller('bias');await page.locator('[data-radial-draft]').waitFor();
   const before=Number(await page.locator('[data-radial-draft]').getAttribute('data-radial-draft-value'));
   await page.evaluate(()=>window.__radialPerformance.api.gamepad({action:'value-up'}));
   assert.equal(Number(await page.locator('[data-radial-draft]').getAttribute('data-radial-draft-value')),before+5);
   assert.equal(await page.evaluate(()=>window.__radialPerformance.writes.length),0);
   await page.evaluate(()=>window.__radialPerformance.api.gamepad({action:'back'}));assert.equal(await page.locator('[data-radial-draft]').count(),0);assert.equal(await page.locator('.wheel-sector').count(),12);
   await controller('saving');assert.equal(await page.locator('[data-radial-draft]').textContent(),'中幅');await page.evaluate(()=>window.__radialPerformance.api.gamepad({action:'value-up'}));assert.equal(await page.locator('[data-radial-draft]').textContent(),'大幅');await page.evaluate(()=>window.__radialPerformance.api.gamepad({action:'back'}));
   assert.equal(await page.evaluate(()=>window.__radialPerformance.writes.length),0);
  });
  await actionCase('Performance controller changes mode, reaches manual frequency and respects disabled controls',height,async()=>{
   await run();await parent();await controller('mode');await page.evaluate(()=>{const api=window.__radialPerformance.api;api.gamepad({action:'value-up'});api.gamepad({action:'accept'});});await settled();
   assert.equal((await read()).frame.mode,'manual');assert.equal(await page.locator('.wheel-sector').count(),14);
   await controller('frequencyMHz');const before=Number(await page.locator('[data-radial-draft]').getAttribute('data-radial-draft-value'));await page.evaluate(()=>{const api=window.__radialPerformance.api;api.gamepad({action:'value-down'});api.gamepad({action:'accept'});});await settled();
   assert.equal((await read()).snapshot.bank.manual.frequencyMHz,before-100);assert.equal(await page.evaluate(()=>window.__radialPerformance.writes.length),2);assert.match((await read()).status,/实际效果待设备确认/);
   await controller('aggressiveness');await page.evaluate(()=>{const api=window.__radialPerformance.api;api.gamepad({action:'value-up'});api.gamepad({action:'accept'});});await settled();assert.equal((await read()).snapshot.bank.manual.aggressiveness,61);
   await controller('unlimited');await settled();assert.equal((await read()).frame.controls.find(c=>c.key==='frequencyMHz').enabled,false);
   await page.evaluate(()=>{const api=window.__radialPerformance.api;api.gamepad({action:'nav-right'});api.gamepad({action:'nav-right'});api.gamepad({action:'nav-right'});});
   assert.equal(await page.locator('.sector-selected').getAttribute('data-radial-control'),'aggressiveness');
   const writes=await page.evaluate(()=>window.__radialPerformance.writes.length);await tap('[data-radial-control="frequencyMHz"]');assert.equal(await page.evaluate(()=>window.__radialPerformance.writes.length),writes);assert.equal(await page.locator('[data-radial-draft]').count(),0);
   await page.locator('[data-radial-wheel]').screenshot({path:path.join(task,'evidence','radial-performance-manual-'+height+'.png')});
  });
  await actionCase('Performance trusted touch adjusts the top bubble slider and submits only on Apply',height,async()=>{
   await run();await tap('[data-radial-sector="pages"]');await tap('[data-radial-control="tdp"]');await page.locator('[data-radial-draft]').waitFor();
   const cdp=await context.newCDPSession(page),box=await page.locator('.wheel-geometry').boundingBox();const arc=(delta)=>({x:box.x+(248+100*delta)*box.width/496,y:box.y+126*box.height/496});
   for(const [type,delta]of [['touchStart',-.4],['touchMove',.4],['touchEnd',.4]]){const p=arc(delta);await cdp.send('Input.dispatchTouchEvent',{type,touchPoints:type==='touchEnd'?[]:[{x:p.x,y:p.y,id:1,radiusX:6,radiusY:6,force:1}]});}
   const draft=Number(await page.locator('[data-radial-draft]').getAttribute('data-radial-draft-value'));assert.ok(draft>15&&draft<=25);assert.equal(await page.evaluate(()=>window.__radialPerformance.writes.length),0);
   await page.locator('[data-radial-footer="1"]').tap();await settled();assert.equal((await read()).snapshot.bank.tdp,draft);assert.match((await read()).status,/已确认写入/);assert.equal(await page.evaluate(()=>window.__radialPerformance.writes.length),1);
   await tap('[data-radial-control="bias"]');await page.locator('[data-radial-footer="2"]').tap();await page.locator('[data-radial-footer="0"]').tap();assert.equal(await page.evaluate(()=>window.__radialPerformance.writes.length),1);
   await page.touchscreen.tap(box.x+248*box.width/496,box.y+248*box.height/496);assert.equal(await page.evaluate(()=>window.__radialPerformance.writes.length),1);assert.equal(await page.locator('.wheel-child').count(),0);
  });
  await actionCase('Performance drafts are discarded on owner-frame change and cannot be revived by a late Apply',height,async()=>{
   await run();await parent();await controller('bias');await page.evaluate(()=>{const w=window.__radialPerformance;w.api.gamepad({action:'value-up'});w.changeContext();});
   await page.waitForFunction(()=>!document.querySelector('[data-radial-draft]'));await page.evaluate(()=>window.__radialPerformance.api.footer(1));await settled();assert.equal(await page.evaluate(()=>window.__radialPerformance.writes.length),0);
   const rect=await page.locator('.wheel-geometry').boundingBox();await page.touchscreen.tap(rect.x+rect.width/2,rect.y+rect.width/2);await page.evaluate(()=>window.__radialPerformance.api.gamepad({action:'nav-right'}));assert.ok(await page.locator('.sector-selected').count()>0);
  });
 }finally{await page.evaluate(()=>window.__radialPerformance?.unmount()).catch(()=>{});await context.close();}
}
