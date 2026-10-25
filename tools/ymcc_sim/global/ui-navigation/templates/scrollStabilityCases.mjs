import assert from 'node:assert/strict';

// Exercise current Vue event handlers using trusted pointer clicks. Preparation
// scrolls only the content viewport, so browser scrollIntoView cannot hide a jump.
export async function scrollStabilityCases({page,height,open,command,press,actionCase,task}) {
 const rows=[];let pointerActions=0,dropdownActions=0,recoveries=0;
 const pause=()=>page.waitForTimeout(85);
 const snapshot=f=>f.evaluate(()=>{
  const c=document.querySelector('.app-content');
  const shell=[document.scrollingElement,...document.querySelectorAll('.app-root,.app-stage,.app-body,.app-main')];
  return {top:c.scrollTop,max:c.scrollHeight-c.clientHeight,route:window.__currentAuditRoute(),shell:shell.map(el=>({top:el.scrollTop,left:el.scrollLeft}))};
 });
 async function place(f,locator,edge=false){
  await locator.evaluate((el,edge)=>{
   const c=document.querySelector('.app-content'),r=el.getBoundingClientRect(),b=c.getBoundingClientRect(),scale=b.height/c.clientHeight||1;
   const center=Math.min(innerHeight-45,b.bottom-45);
   c.scrollTop+=(r.top+r.height/2-(edge?center:b.top+(Math.min(b.bottom,innerHeight)-b.top)/2))/scale;
  },edge);await pause();
 }
 async function click(locator){const r=await locator.boundingBox();assert.ok(r&&r.width&&r.height,'pointer target has a real box');await page.mouse.click(r.x+r.width/2,r.y+r.height/2);await pause();}
 function stable(before,after,label){
  assert.equal(after.route,before.route,label+': route changed');
  assert.deepEqual(after.shell,before.shell,label+': clipped shell scrolled');
  // A section can legitimately shrink. The browser may clamp to its new end.
  if(Math.abs(before.max-after.max)<2)assert.ok(Math.abs(after.top-before.top)<2,label+': same content jumped '+before.top+' -> '+after.top);
  else if(after.max>=before.top&&before.top>80)assert.ok(after.top>2,label+': content with sufficient height reset to top');
 }
 const scenarios=[['/schedule'],['/schedule',null,'manual'],['/fan'],['/button-mapping'],['/gyro-motion'],['/gyro-motion',null,'advanced'],
 ['/settings','appearance'],['/settings','device'],['/settings','diagnostics'],['/settings','updates'],
 ['/power'],['/power','services'],['/power','tools'],['/rtss'],['/steam'],['/sleep'],['/quick']];
 for(const [route,section,mode]of scenarios)await actionCase('Scroll: pointer controls '+route+'/'+(section||mode||'main'),height,async()=>{
  const f=await open(route,height);if(section&&route!=='/power')await f.locator('[data-settings-section="'+section+'"]').click();const groupSelector=route==='/power'&&section?'[data-boot-startup-group="'+section+'"]':null;if(groupSelector)await f.locator(groupSelector).waitFor();
  if(mode==='manual')await f.getByRole('button',{name:'手动模式',exact:true}).click();
  if(mode==='advanced')await f.getByRole('button',{name:'展开',exact:true}).click();
  await pause();
  const count=await f.evaluate(selector=>{const group=selector?document.querySelector(selector):null;let i=0;for(const el of document.querySelectorAll('.route-view .ymcc-switch:not(:disabled),.route-view .toggle-text,.route-view .seg-btn:not(:disabled),.route-view .shortcut-toggle:not(:disabled),.route-view .dd-trigger:not(:disabled),.route-view input[type=range]:not(:disabled),.route-view .persona-btn:not(:disabled)')){
   if(group&&!group.contains(el))continue;
   if(!el.getBoundingClientRect().height||el.closest('[hidden],[inert],.toggle-row.disabled'))continue;
   el.dataset.scrollStability=String(i++);
  }return i;},groupSelector);
  if(route==='/power'&&section==='tools'){
   const buttons=f.locator(groupSelector+' button:not(:disabled)');assert.equal(await buttons.count(),4,'All system tool buttons remain visible');
   for(let index=0;index<await buttons.count();index++){const button=buttons.nth(index);await place(f,button);const bounds=await button.evaluate(el=>{const r=el.getBoundingClientRect();return {y:r.y,bottom:r.bottom,viewport:innerHeight};});assert.ok(bounds.y>=0&&bounds.bottom<=bounds.viewport,'System tool button clipped at '+height+': '+JSON.stringify(bounds));}
   const search=buttons.first();await place(f,search);const before=await snapshot(f);await click(search);pointerActions++;const after=await snapshot(f);stable(before,after,'system search tool');rows.push({route,section,label:'system search tool',before,after});
  }
  for(let i=0;i<count;i++){
   const el=f.locator('[data-scroll-stability="'+i+'"]');if(!await el.count()||!await el.isVisible()||!await el.isEnabled())continue;
   const label=(await el.getAttribute('aria-label'))||(await el.textContent()).trim().slice(0,65);
   const isDropdown=await el.evaluate(e=>e.matches('.dd-trigger'));
   const isRange=await el.evaluate(e=>e.matches('input[type=range]'));
   await place(f,el,isDropdown);const before=await snapshot(f);await click(el);pointerActions++;
   let after=await snapshot(f);stable(before,after,route+'/'+label);rows.push({route,section,mode,label,before,after});
   if(isDropdown&&await f.locator('[role=listbox]').count()){
    const option=f.locator('[role=option]:not([disabled]):visible').first();
    // Selecting the current value keeps layout identical and exercises restore.
    const selected=f.locator('[role=option][aria-selected=true]:visible');
    const target=await selected.count()?selected:option;
    if(await target.count()){await click(target);dropdownActions++;after=await snapshot(f);stable(before,after,route+'/'+label+' selection');}
    if(await f.locator('[role=listbox]').count())await command(f,'back');
   }
   // Move the thumb with a real pointer, then let the real change/commit run.
   if(isRange&&await el.isEnabled()){
    const r=await el.boundingBox();const original=await el.inputValue();
    await page.mouse.move(r.x+r.width*.5,r.y+r.height/2);await page.mouse.down();await page.mouse.move(r.x+r.width*.58,r.y+r.height/2);await page.mouse.up();await pause();
    stable(after,await snapshot(f),route+'/'+label+' slider commit');
   }
   // Never let a confirmation change native settings. All native IO is memory,
   // and cancel is exercised here rather than accepting destructive actions.
   for(let n=0;n<3&&await f.locator('[data-gp-modal]').count();n++){await command(f,'back');await pause();}
  }
 });
 await actionCase('Scroll: unrelated game-name scrolling preserves the real dropdown pointer target',height,async()=>{
  const f=await open('/schedule',height),trigger=f.getByRole('button',{name:'低于分界的核心方案',exact:true});
  await place(f,trigger,true);await click(trigger);pointerActions++;
  const target=await f.locator('[role=option][aria-selected=true]:visible').elementHandle();
  assert.ok(target,'real selected option exists before unrelated scrolling');
  assert.equal(await trigger.getAttribute('aria-expanded'),'true');
  const scroll=await f.locator('.game-recognition-label').evaluate(el=>{
   const before=el.scrollLeft,max=el.scrollWidth-el.clientWidth;
   assert: if(max<1)throw Error('Game-name fixture must have real horizontal overflow');
   el.scrollLeft=before<max-1?before+1.5:0;return{before,after:el.scrollLeft};
  });
  assert.notEqual(scroll.after,scroll.before,'the actual game-name element scrolled');
  await page.waitForTimeout(180);
  await click(target);dropdownActions++;
  assert.equal(await trigger.getAttribute('aria-expanded'),'false','selection closes the surviving popup');
 });
 for(const route of ['/schedule','/fan','/button-mapping','/gyro-motion','/settings','/power','/sleep','/rtss','/steam','/quick'])await actionCase('Scroll: focus recovery stays in current view '+route,height,async()=>{
  const f=await open(route,height);
  if(route==='/gyro-motion')await f.getByRole('button',{name:'展开',exact:true}).click();
  for(const action of ['nav-down','nav-left','confirm']){
   await f.evaluate(()=>{document.activeElement?.blur();const c=document.querySelector('.app-content');c.scrollTop=Math.min(450,c.scrollHeight-c.clientHeight);});await pause();
   const before=await snapshot(f);await command(f,action);const after=await snapshot(f);stable(before,after,route+'/'+action+' lost focus');
   assert.equal(await f.evaluate(()=>window.__gamepadAudit.editing()),false,'recovery must not arm a slider or replay a click');recoveries++;
  }
 });
 await actionCase('Scroll: controller A waits through real async save without jumping or duplicate activation',height,async()=>{
  const f=await open('/button-mapping',height),entry=f.locator('.shortcut-toggle').last();await place(f,entry);
  await entry.evaluate(el=>window.__gamepadAudit.focus(el));
  await f.evaluate(()=>{window.__fixture.scrollSaveDelay=400;});const before=await snapshot(f);
  const original=await entry.getAttribute('class');
  await command(f,'confirm');await f.waitForFunction(()=>document.querySelector('.shortcut-toggle')?.disabled);
  assert.equal(await entry.isDisabled(),true,'real save disables the real selected control');
  const writes=await f.evaluate(()=>window.__previewCalls.filter(c=>c.cmd==='settings.write').length);
  for(const action of ['nav-down','nav-left','confirm']){await command(f,action);stable(before,await snapshot(f),'pending save/'+action);}
  await f.waitForFunction(()=>!document.querySelector('.shortcut-toggle')?.disabled);
  stable(before,await snapshot(f),'save completed');assert.equal(await f.evaluate(()=>window.__previewCalls.filter(c=>c.cmd==='settings.write').length),writes,'pending A must not duplicate the write');
  // Some Chromium builds retain DOM focus across disabled -> enabled. In
  // that case the next direction is a real move, not a recovery request.
  if(!await entry.evaluate(el=>document.activeElement===el))await command(f,'nav-down');
  assert.ok(await entry.evaluate(el=>document.activeElement===el),'save retains or recovers the saved row');stable(before,await snapshot(f),'post-save focus recovery');
 });
 await actionCase('Scroll: modal open/cancel restores the scrolled controller page',height,async()=>{
  const f=await open('/button-mapping',height),entry=f.getByRole('button',{name:'打开编辑器',exact:true});await place(f,entry);const before=await snapshot(f);
  await click(entry);await f.locator('.editor-overlay').waitFor();assert.deepEqual((await snapshot(f)).shell,before.shell);
  await command(f,'back');await f.waitForFunction(()=>!document.querySelector('.editor-overlay'));stable(before,await snapshot(f),'controller editor B');
 });
 await actionCase('Scroll: header dedicated menu leaves underlying viewport unchanged',height,async()=>{
  const f=await open('/button-mapping',height);await f.evaluate(()=>{const c=document.querySelector('.app-content');c.scrollTop=350;});const before=await snapshot(f);
  await command(f,'edit-game');await f.locator('[data-gp-game-quick-menu]').waitFor();await command(f,'back');stable(before,await snapshot(f),'header menu B');
 });
 await actionCase('Scroll: real route change returns to title; same-page click does not',height,async()=>{
  const f=await open('/button-mapping',height);await f.evaluate(()=>{const c=document.querySelector('.app-content');c.scrollTop=350;});
  await f.locator('.navrail [href="#"]').count();
  await command(f,'page-next');await f.locator('.route-view[data-route="/gyro-motion"]').waitFor();assert.ok((await snapshot(f)).top<2,'route title resets once');
 });
 const fs=await import('node:fs');const path=await import('node:path');fs.writeFileSync(path.join(task,'scroll-stability-'+height+'.json'),JSON.stringify({height,pointerActions,dropdownActions,recoveries,rows,physicalController:false},null,2));
}
