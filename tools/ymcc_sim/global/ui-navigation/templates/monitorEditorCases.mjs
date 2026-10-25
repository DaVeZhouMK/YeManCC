import assert from 'node:assert/strict';
import path from 'node:path';
export async function monitorEditorCases({f,height,command,press,actionCase,state,page,task}){
 const overview=f.locator('.monitor-overview');
 await state(f,'monitor/overview-fixed',height,'.monitor-overview');
 await actionCase('Monitor overview has one title, editor below settings and fixed complete preview',height,async()=>{
  assert.equal(await overview.locator('.settings-section-tabs').count(),0);
  assert.equal(await overview.locator('.monitor-overview-heading').count(),1);
  const geometry=await overview.evaluate(root=>{
   const content=root.closest('.app-content'),panel=root.querySelector('.monitor-overview-output').getBoundingClientRect(),entry=root.querySelector('[data-monitor-edit]').getBoundingClientRect(),preview=root.querySelector('.monitor-overview-preview').getBoundingClientRect(),canvas=root.querySelector('.monitor-overview-preview-canvas').getBoundingClientRect(),hud=root.querySelector('.ymcc-hud').getBoundingClientRect();
   return {scroll:content.scrollHeight-content.clientHeight,panelBottom:panel.bottom,entryTop:entry.top,entryBottom:entry.bottom,previewTop:preview.top,previewBottom:preview.bottom,contentBottom:content.getBoundingClientRect().bottom,canvas:{left:canvas.left,right:canvas.right,top:canvas.top,bottom:canvas.bottom},hud:{left:hud.left,right:hud.right,top:hud.top,bottom:hud.bottom,width:hud.width,height:hud.height}};
  });
  assert.ok(geometry.scroll<=1,JSON.stringify(geometry));assert.ok(geometry.entryTop>=geometry.panelBottom && geometry.previewTop>=geometry.entryBottom,JSON.stringify(geometry));assert.ok(geometry.previewBottom<=geometry.contentBottom,JSON.stringify(geometry));assert.ok(geometry.hud.width>20 && geometry.hud.height>10,JSON.stringify(geometry));
  for(const side of ['left','top'])assert.ok(geometry.hud[side]>=geometry.canvas[side]-1,JSON.stringify(geometry));
  for(const side of ['right','bottom'])assert.ok(geometry.hud[side]<=geometry.canvas[side]+1,JSON.stringify(geometry));
  await overview.locator('.monitor-overview-preview').hover();await page.mouse.wheel(0,800);await page.waitForTimeout(40);
  assert.equal(await f.locator('.app-content').evaluate(el=>el.scrollTop),0);
 });
 await actionCase('Monitor overview output buttons first, switches middle, visibility last',height,async()=>{
  await overview.evaluate(root=>{
   const visibility=root.querySelector('.monitor-overview-visibility'),switches=root.querySelector('.monitor-overview-controls'),modes=root.querySelector('.monitor-overview-output-modes'),audit=window.__gamepadAudit;
   const v=visibility.getBoundingClientRect(),s=switches.getBoundingClientRect(),m=modes.getBoundingClientRect();
   if(m.bottom>s.top+1||s.bottom>v.top+1)throw Error('Visibility/output rows were not exchanged');
   const toggles=[...switches.querySelectorAll('button')];audit.focus(visibility.querySelector('.dd-trigger'));audit.command('nav-up');if(!toggles.includes(document.activeElement))throw Error('Visibility Up skipped switches');
   for(const button of modes.querySelectorAll('button')){audit.focus(button);audit.command('nav-down');if(!toggles.includes(document.activeElement))throw Error('Output Down skipped switches');}
  });
 });
 await actionCase('Monitor overview editor and A/B selectors share one row with natural neighbours',height,async()=>{
  assert.equal(await overview.locator('.monitor-overview-preview-heading button').count(),0);
  assert.equal(await overview.locator('[data-monitor-edit] button').count(),0);
  assert.equal(await overview.locator('[data-monitor-edit] .monitor-overview-template-switch').count(),0);
  await overview.evaluate(root=>{
   const controls=[...root.querySelectorAll('.monitor-overview-editor-bar button')],lock=root.querySelector('.monitor-overview-lock'),visibility=root.querySelector('.monitor-overview-visibility .dd-trigger'),audit=window.__gamepadAudit;
   if(controls.length!==3)throw Error('Expected independent edit/A/B controls');
   const rows=controls.map(el=>el.getBoundingClientRect());
   if(rows.some(row=>Math.abs((row.top+row.bottom)/2-(rows[0].top+rows[0].bottom)/2)>2))throw Error('Edit/A/B are not visually aligned');
   for(let i=0;i<controls.length;i++){
    for(const [direction,target] of [['nav-left',controls[Math.max(0,i-1)]],['nav-right',controls[Math.min(controls.length-1,i+1)]],['nav-down',lock]]){audit.focus(controls[i]);audit.command(direction);if(document.activeElement!==target)throw Error('Unexpected row neighbour '+i+' '+direction);}
    audit.focus(controls[i]);audit.command('nav-up');if(document.activeElement!==visibility)throw Error('Editor/template Up skipped visibility row');
   }
  });
 });
 await actionCase('Monitor overview separate template buttons explicitly identify the current bank',height,async()=>{
  for(const bank of ['B','A']){
   const selected=overview.locator('[data-monitor-overview-template="'+bank+'"]'),other=overview.locator('[data-monitor-overview-template="'+(bank==='A'?'B':'A')+'"]');
   await press(f,selected);await f.waitForFunction(bank=>window.__monitorPreview.preferences.ymccTemplate===bank,bank);
   assert.equal((await selected.textContent()).trim(),'当前模板'+bank);assert.equal(await selected.getAttribute('aria-pressed'),'true');assert.equal((await other.textContent()).trim(),'模板'+(bank==='A'?'B':'A'));assert.equal(await other.getAttribute('aria-pressed'),'false');
   assert.ok((await overview.locator('[data-monitor-edit]').textContent()).includes('外观与显示数据'));
  }
 });
 await actionCase('Monitor overview lock hint and icon follow saved current-template state',height,async()=>{
  const control=overview.locator('.monitor-overview-lock');assert.ok(await control.locator('svg').count());
  for(const locked of [true,false]){await press(f,control);await f.waitForFunction(locked=>window.__monitorPreview.preferences.ymcc.dragLocked===locked,locked);assert.equal(await control.getAttribute('aria-pressed'),String(locked));assert.ok((await control.textContent()).includes(locked?'拖动已锁定':'可拖动 · 点击锁定'));}
 });
 await f.locator('.monitor-overview .ymcc-hud').waitFor();
 await f.locator('.app-root').screenshot({path:path.join(task,'evidence','monitor-overview-'+height+'.png')});
 const modal=f.locator('.monitor-editor-window');
 await press(f,f.locator('[data-monitor-edit]'));await modal.waitFor();
 await state(f,'monitor/editor',height,'.monitor-editor-window');
 await actionCase('Monitor editor is unscaled modal without sidebar or RTSS',height,async()=>{assert.equal(await modal.locator('.navrail,.topmon-bar').count(),0);assert.equal(await modal.getByRole('button',{name:'RTSS监控',exact:true}).count(),0);assert.equal(await modal.getByRole('button',{name:'A模板',exact:true}).count(),1);assert.equal(await modal.getByRole('button',{name:'B模板',exact:true}).count(),1);});
 for(const bank of ['A','B']){
  await press(f,modal.getByRole('button',{name:bank+'模板',exact:true}));
  await f.waitForFunction(bank=>document.querySelector('[data-monitor-template=\"'+bank+'\"]').getAttribute('aria-pressed')==='true',bank);
  await press(f,modal.locator('[data-settings-section="fields"]'));
  const tabs=modal.locator('.monitor-field-tab');
  await actionCase('Monitor '+bank+' category row follows left/right neighbours and Down reaches preset',height,async()=>{
   await f.evaluate(()=>{const a=window.__gamepadAudit,tabs=[...document.querySelectorAll('.monitor-field-tab')],preset=document.querySelector('.monitor-data-preset-slider input[type=range]');for(let i=0;i<tabs.length;i++){for(const [direction,index] of [['nav-left',i-1],['nav-right',i+1]]){if(index<0)continue;a.focus(tabs[i]);a.command(direction);if(document.activeElement!==tabs[Math.min(tabs.length-1,index)])throw Error('Category row skipped neighbour at '+i+' '+direction);}a.focus(tabs[i]);a.command('nav-down');if(document.activeElement!==preset)throw Error('Category Down did not reach preset at '+i);}});
  });
  for(let i=0;i<await tabs.count();i++){await press(f,tabs.nth(i));const label=(await tabs.nth(i).textContent()).trim();await state(f,'monitor/'+bank+'/fields/'+label,height,'.monitor-editor-window');}
  await actionCase('Monitor '+bank+' reset and hide controls identify active bank',height,async()=>{assert.equal(await modal.getByRole('button',{name:'重置模板'+bank,exact:true}).count(),1);assert.ok((await modal.locator('[data-settings-section="fields"]').textContent()).includes(bank+'模板'));});
  await actionCase('Monitor '+bank+' reset restores the full current template through controller A and preserves the other bank',height,async()=>{
   const before=await f.evaluate(async()=>{
    const p=structuredClone(window.__monitorPreview.preferences),selected=p.ymccTemplate,other=selected==='A'?'B':'A';
    const snapshot={other,bank:p.ymccTemplates[other],enabled:p.enabled,popupEnabled:p.popupEnabled,dragLocked:p.ymcc.dragLocked};
    await window.__monitorSetPreferences({...p,ymccTemplateRevision:(p.ymccTemplateRevision||0)+1,ymcc:{...p.ymcc,scale:.5,textScale:1.8,font:'mono',fpsFont:'arial',underlay:'none',underlayPreset:'none',valueScales:{cpuPower:2},dataPreset:'detailed',fields:{...p.ymcc.fields,fps:false,cpuPower:false,cpuTemp:false},dragX:.3,dragY:.4}});
    return snapshot;
   });
   await press(f,modal.getByRole('button',{name:'重置模板'+bank,exact:true}));
   await f.waitForFunction(bank=>{const p=window.__monitorPreview.preferences;return p.ymccTemplate===bank&&p.ymcc.scale===(bank==='B'?.5:.7)&&p.ymcc.textScale===1.2&&p.ymcc.font==='system'&&p.ymcc.dataPreset===(bank==='A'?'simple':'minimal')},bank);
   const p=await f.evaluate(()=>window.__monitorPreview.preferences);assert.equal(p.ymcc.layout,'capsule');assert.equal(p.ymcc.fpsFont,'inherit');assert.equal(p.ymcc.underlayPreset,'ac8-pink');assert.deepEqual(p.ymcc.valueScales,{});assert.equal(p.ymcc.dragX,undefined);assert.equal(p.ymcc.dragY,undefined);assert.equal(p.ymcc.fields.fps,true);assert.equal(p.ymcc.fields.cpuPower,true);assert.equal(p.ymcc.fields.cpuTemp,true);assert.equal(p.enabled,before.enabled);assert.equal(p.popupEnabled,before.popupEnabled);assert.equal(p.ymcc.dragLocked,before.dragLocked);assert.deepEqual(p.ymccTemplates[before.other],before.bank);
   await press(f,modal.getByRole('button',{name:before.other+'模板',exact:true}));await f.waitForFunction(bank=>window.__monitorPreview.preferences.ymccTemplate===bank,before.other);
   await press(f,modal.getByRole('button',{name:bank+'模板',exact:true}));await f.waitForFunction(bank=>window.__monitorPreview.preferences.ymccTemplate===bank,bank);
   assert.equal(await f.evaluate(()=>window.__monitorPreview.preferences.ymcc.scale),bank==='B'?.5:.7);assert.equal(await f.evaluate(()=>window.__monitorPreview.preferences.ymcc.fields.fps),true);
  });
  await press(f,modal.locator('[data-settings-section="appearance"]'));
  await state(f,bank==='A'?'monitor/appearance-expanded':'monitor/B/appearance',height,'.monitor-editor-window');
  await actionCase('Monitor '+bank+' global text maximum is 200 percent',height,async()=>{const slider=modal.getByRole('slider',{name:'监控文字倍率',exact:true});assert.equal(await slider.getAttribute('max'),'200');await slider.evaluate(el=>{el.value='200';el.dispatchEvent(new Event('input',{bubbles:true}));el.dispatchEvent(new Event('change',{bubbles:true}));});await f.waitForFunction(()=>window.__monitorPreview.preferences.ymcc.textScale===2);assert.equal(await modal.locator('.monitor-refinements summary').count(),0);assert.ok(await modal.getByRole('slider',{name:'圆角',exact:true}).isVisible());});
  await actionCase('Monitor '+bank+' header Down enters picture editor and Up returns to selected bank',height,async()=>{
   await press(f,modal.getByRole('button',{name:bank+'模板',exact:true}));
   await command(f,'nav-down');await f.waitForFunction(()=>document.activeElement?.matches('.monitor-image-underlay--top .dd-trigger'));
   await command(f,'nav-up');assert.equal(await modal.getByRole('button',{name:bank+'模板',exact:true}).evaluate(el=>document.activeElement===el),true);
   await command(f,'nav-down');await f.waitForFunction(()=>document.activeElement?.matches('.monitor-image-underlay--top .dd-trigger'));
   await command(f,'confirm');await f.locator('[role=listbox]').waitFor();await command(f,'back');await f.waitForFunction(()=>!document.querySelector('[role=listbox]'));
  });
  await actionCase('Monitor '+bank+' all data controls fit one page',height,async()=>{
   await f.evaluate(async()=>{const p=window.__monitorPreview.preferences,fields=Object.fromEntries(Object.keys(p.ymcc.fields).map(id=>[id,true]));await window.__monitorSetPreferences({...p,ymccTemplateRevision:(p.ymccTemplateRevision||0)+1,ymcc:{...p.ymcc,dataPreset:'detailed',fields,dataPresetFields:{...p.ymcc.dataPresetFields,detailed:fields}}});});
   await press(f,modal.locator('[data-settings-section="fields"]'));
   const tabs=modal.locator('.monitor-field-tab');for(let i=0;i<await tabs.count();i++){
    await press(f,tabs.nth(i));await state(f,'monitor/'+bank+'/all-fields/'+i,height,'.monitor-editor-window');
    const geometry=await modal.locator('.settings-section-panel:not([hidden])').evaluate(el=>{const tabs=[...el.querySelectorAll('.monitor-field-tab')].map(e=>e.getBoundingClientRect()),preset=el.querySelector('.monitor-fields-head').getBoundingClientRect();return {scroll:el.scrollHeight-el.clientHeight,horizontal:el.scrollWidth-el.clientWidth,categoryRow:tabs.every(r=>Math.abs(r.top-tabs[0].top)<1),abovePreset:tabs.every(r=>r.bottom<=preset.top),equalWidths:tabs.every(r=>Math.abs(r.width-tabs[0].width)<1)};});
    assert.ok(geometry.scroll<=1&&geometry.horizontal<=1&&geometry.categoryRow&&geometry.abovePreset&&geometry.equalWidths,JSON.stringify(geometry));
   }
  });
 }
 await press(f,modal.getByRole('button',{name:'A模板',exact:true}));
 await press(f,modal.locator('[data-settings-section="appearance"]'));

 await actionCase('Monitor dropdown B keeps editor open and LB/RB stay owned',height,async()=>{await press(f,modal.locator('.dd-trigger').first());await f.locator('[role=listbox]').waitFor();await command(f,'nav-down');await command(f,'back');await f.waitForFunction(()=>!document.querySelector('[role=listbox]'));assert.equal(await f.locator('[role=listbox]').count(),0);assert.equal(await modal.count(),1);await command(f,'page-next');assert.equal(await modal.count(),1);assert.equal(await f.locator('.route-view[data-route="/rtss"]').count(),1);});
 await actionCase('Monitor B returns from controls to section, then closes and restores entry focus',height,async()=>{await press(f,modal.getByRole('slider',{name:'圆角',exact:true}));await command(f,'back');assert.equal(await modal.count(),1);await command(f,'back');assert.equal(await modal.count(),1);await command(f,'back');await modal.waitFor({state:'detached'});await f.waitForFunction(()=>document.activeElement?.matches('[data-monitor-edit]'));assert.ok(await f.locator('[data-monitor-edit]').evaluate(el=>document.activeElement===el));assert.equal(await f.evaluate(()=>window.__monitorPreview.preferences.ymccTemplate),'A');assert.equal(await f.evaluate(()=>window.__monitorPreview.preferences.ymccTemplates.B.textScale),2);});
 await press(f,f.getByRole('button',{name:'RTSS监控',exact:true}));await state(f,'monitor/rtss-output',height);
 await press(f,f.getByRole('button',{name:'自定义监控',exact:true}));
}
