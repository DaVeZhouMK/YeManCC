import assert from 'node:assert/strict';
export async function controllerPageCases({height,open,command,press,actionCase,state,page,task}) {
  for(const scope of ['global','dedicated']) {
    let f=await open('/button-mapping',height,{nativeWidth:'1',controllerScope:scope});
    for(const panel of ['controller','feedback']) {
      await press(f,f.locator('[data-controller-section="'+panel+'"]'));
      if(panel==='feedback')await f.getByRole('slider',{name:'震动强度',exact:true}).waitFor();
      await state(f,'controller/standard/'+scope+'/'+panel,height);
      if(panel==='controller')await actionCase('Controller isolation shares header row and natural navigation / '+scope,height,async()=>{
        const isolation=f.getByRole('switch',{name:'呼出隔离',exact:true});
        const priority=f.getByRole('switch',{name:'外置优先',exact:true});
        assert.equal(await isolation.count(),1);
        assert.equal(await f.locator('.controller-page-title').innerText(),'SteamDeck');
        assert.equal(await f.locator('.controller-heading-actions').getByRole('switch').count(),2);
        const geometry=await f.evaluate(()=>{
          const isolation=document.querySelector('.controller-heading-actions [aria-label="呼出隔离"]');
          const priority=document.querySelector('.controller-heading-actions [aria-label="外置优先"]');
          const header=document.querySelector('.controller-unified-header').getBoundingClientRect();
          const first=isolation.getBoundingClientRect(),second=priority.getBoundingClientRect();
          return {first:first.x,second:second.x,vertical:Math.abs(first.y-second.y),right:second.right,headerRight:header.right};
        });
        assert.ok(geometry.first<geometry.second&&geometry.vertical<1&&geometry.right<=geometry.headerRight+1,JSON.stringify(geometry));
        assert.equal(await f.locator('.virtual-card [aria-label="呼出隔离"]').count(),0);
        await isolation.evaluate(el=>window.__gamepadAudit.focus(el));
        await command(f,'nav-right');assert.ok(await priority.evaluate(el=>document.activeElement===el));
        await command(f,'nav-right');assert.ok(await priority.evaluate(el=>document.activeElement===el));
        await command(f,'nav-left');assert.ok(await isolation.evaluate(el=>document.activeElement===el));
        await command(f,'nav-left');assert.ok(await isolation.evaluate(el=>document.activeElement===el));
        await command(f,'nav-down');
        assert.ok(await f.evaluate(scope=>!!document.activeElement.closest(scope==='dedicated'?'.controller-profile-banner':'.controller-page-tabs'),scope));
        await command(f,'nav-up');assert.ok(await f.evaluate(()=>!!document.activeElement.closest('.controller-heading-actions')));
        const original=await isolation.getAttribute('aria-checked');
        await press(f,isolation);
        await f.waitForFunction(original=>document.querySelector('.controller-heading-actions [aria-label="呼出隔离"]')?.getAttribute('aria-checked')!==original,original);
        await isolation.waitFor({state:'visible'});
        await f.waitForFunction(()=>!document.querySelector('.controller-heading-actions [aria-label="呼出隔离"]').disabled);
        await press(f,isolation);
        await f.waitForFunction(original=>document.querySelector('.controller-heading-actions [aria-label="呼出隔离"]')?.getAttribute('aria-checked')===original,original);
        assert.equal(await f.locator('#frontend-isolation-status').count(),0);
        await page.locator('.screen').screenshot({path:task+'/evidence/controller-isolation-header-'+scope+'-'+height+'.png'});
      });
      await actionCase('Controller standard page state / '+scope+'/'+panel,height,async()=>{
        assert.equal(await f.locator('.controller-profile-banner').isVisible(),scope==='dedicated');
        assert.deepEqual(await f.locator('.controller-page-tabs button').evaluateAll(buttons=>buttons.map(button=>button.dataset.controllerSection)),['controller','calibration','feedback']);
        if(panel!=='controller')assert.equal(await f.locator('.persona-btn:visible').count(),0);

      });
      if(panel==='feedback')await actionCase('Enabled lighting controls are reachable / '+scope,height,async()=>{
        const control=f.getByRole('switch',{name:'启用控制',exact:true});
        if(await control.getAttribute('aria-checked')!=='true')await press(f,control);
        const mode=f.getByRole('button',{name:'常亮',exact:true});await press(f,mode);await f.locator('[role="listbox"]').waitFor();
        await command(f,'nav-down');await command(f,'confirm');await f.waitForFunction(()=>!document.querySelector('[role="listbox"]'));
        await state(f,'controller/standard/'+scope+'/feedback-enabled',height);
      });
    }
    await actionCase('Controller B returns to current tab / '+scope,height,async()=>{
      await f.getByRole('slider',{name:'震动强度',exact:true}).evaluate(el=>window.__gamepadAudit.focus(el));
      await command(f,'back');
      assert.equal(await f.locator('[data-controller-section="feedback"]').evaluate(el=>document.activeElement===el),true);
    });
    if(scope==='dedicated')await actionCase('Dedicated exit dialog B cancels back to the same page',height,async()=>{
      await press(f,f.getByRole('button',{name:'退出专属',exact:true}));await f.locator('.profile-remove-dialog').waitFor();
      await state(f,'controller/standard/delete-confirm',height,'.profile-remove-dialog');
      await command(f,'back');assert.equal(await f.locator('.profile-remove-dialog').count(),0);assert.equal(await f.locator('[data-controller-section="feedback"]').getAttribute('aria-pressed'),'true');
      await press(f,f.getByRole('button',{name:'退出专属',exact:true}));await f.locator('.profile-remove-dialog').waitFor();
      await press(f,f.getByRole('button',{name:'确认解除',exact:true}));await f.locator('.profile-remove-dialog').waitFor({state:'detached'});
      await f.waitForFunction(()=>!document.querySelector('.controller-profile-banner'));
      assert.equal(await f.locator('[data-controller-section="feedback"]').evaluate(el=>document.activeElement===el),true,'Deleted banner returns to the visible current tab, never hidden persona buttons');
    });
  }
  let f=await open('/touch-mouse',height,{nativeWidth:'1',controllerScope:'global'});
  await actionCase('Independent touch/mouse page defaults to mouse with vertical lists',height,async()=>{
    assert.equal(await f.locator('[data-touch-mouse-section="mouse"]').getAttribute('aria-pressed'),'true');
    assert.deepEqual(await f.locator('.touch-mouse-tabs button').allTextContents(),['鼠标','触控']);
    const lists=await f.locator('[data-mouse-rule-list]').evaluateAll(els=>els.map(el=>{const r=el.getBoundingClientRect();return {left:r.left,top:r.top,bottom:r.bottom};}));
    assert.equal(lists.length,2);assert.ok(Math.abs(lists[0].left-lists[1].left)<1&&lists[0].bottom<lists[1].top);
    assert.equal(await f.locator('.mouse-policy-modes button:enabled').count(),0);
  });
  await press(f,f.locator('[data-touch-mouse-section="touch"]'));
  const master=()=>f.getByRole('switch',{name:'触控板总开关',exact:true});
  await actionCase('Master switch persists only its own field and leaves mappings intact',height,async()=>{
    const before=await f.evaluate(()=>structuredClone(window.__screenTouchpadMemory.profiles.steamdeck));
    await press(f,master());await f.waitForFunction(()=>window.__screenTouchpadMemory.profiles.steamdeck.masterEnabled===false);
    const after=await f.evaluate(()=>structuredClone(window.__screenTouchpadMemory.profiles.steamdeck));assert.deepEqual(after,{...before,masterEnabled:false});
    assert.equal(await f.locator('.screen-touchpads-bubble button:not(:disabled),.screen-touchpads-bubble input:not(:disabled)').count(),0);
    const writes=await f.evaluate(()=>window.__screenTouchpadMemory.writes);assert.deepEqual(writes.at(-1),{persona:'steamdeck',config:{masterEnabled:false}});
    await state(f,'controller/standard/master-off',height);
  });
  await actionCase('PS5 and SteamDeck remember independent master choices after persona switches',height,async()=>{
    await f.evaluate(()=>window.__navigate('/button-mapping'));await press(f,f.getByRole('button',{name:'PS5',exact:true}));
    await f.locator('.persona-btn.active').filter({hasText:'PS5'}).waitFor();await f.evaluate(()=>window.__navigate('/touch-mouse'));await press(f,f.locator('[data-touch-mouse-section="touch"]'));
    await f.waitForFunction(()=>document.querySelector('[aria-label="屏幕触摸板布局"]')?.textContent.includes('单触摸板'));
    assert.equal(await master().getAttribute('aria-checked'),'true');assert.match(await f.locator('.touch-mouse-persona').innerText(),/PS5/);
    await state(f,'controller/standard/ps5-single',height);
    await f.evaluate(()=>window.__navigate('/button-mapping'));await press(f,f.getByRole('button',{name:'SteamDeck',exact:true}));
    await f.locator('.persona-btn.active').filter({hasText:'SteamDeck'}).waitFor();await f.evaluate(()=>window.__navigate('/touch-mouse'));await press(f,f.locator('[data-touch-mouse-section="touch"]'));
    assert.equal(await master().getAttribute('aria-checked'),'false');await press(f,master());
    await f.waitForFunction(()=>window.__screenTouchpadMemory.profiles.steamdeck.masterEnabled===true);
    assert.equal(await f.getByRole('button',{name:'屏幕触摸板布局',exact:true}).innerText(),'双触摸板');
  });
  await actionCase('Touchpad dropdown A opens; B closes without changing its saved layout',height,async()=>{
    const layout=f.getByRole('button',{name:'屏幕触摸板布局',exact:true});await press(f,layout);await f.locator('[role="listbox"]').waitFor();
    await command(f,'nav-down');await command(f,'back');await f.waitForFunction(()=>!document.querySelector('[role="listbox"]'));assert.equal(await f.locator('[role="listbox"]').count(),0);
    assert.equal(await f.evaluate(()=>window.__screenTouchpadMemory.profiles.steamdeck.layout),'dual');
    assert.equal(await layout.evaluate(el=>el===document.activeElement),true);
  });
  await actionCase('Touchpad slider LT/RT only changes after A; B cancels the edit without leaving its page',height,async()=>{
    const slider=f.getByRole('slider',{name:'触摸板缩放',exact:true});await slider.evaluate(el=>window.__gamepadAudit.focus(el));const before=await slider.inputValue();
    await command(f,'slider-increase');assert.equal(await slider.inputValue(),before);
    await command(f,'confirm');await command(f,'slider-increase');assert.equal(Number(await slider.inputValue()),Number(before)+5);
    await command(f,'back');assert.equal(await f.evaluate(()=>window.__gamepadAudit.editing()),false);
    assert.equal(await f.locator('[data-touch-mouse-section="touch"]').getAttribute('aria-pressed'),'true');
  });
  await actionCase('Failed master save restores authoritative stored state and reports failure',height,async()=>{
    await f.evaluate(()=>window.__screenTouchpadMemory.failNext=true);const before=await master().getAttribute('aria-checked');
    await press(f,master());await f.waitForFunction(()=>document.querySelector('.screen-touchpad-status')?.textContent.includes('保存失败'));
    assert.equal(await master().getAttribute('aria-checked'),before);
  });
  await actionCase('Pending save disables master and completes only its original persona on tab return',height,async()=>{
    await f.evaluate(()=>window.__screenTouchpadMemory.holdNext=true);await press(f,master());
    assert.equal(await master().isDisabled(),true);await f.evaluate(()=>window.__navigate('/button-mapping'));
    await press(f,f.getByRole('button',{name:'PS5',exact:true}));await f.locator('.persona-btn.active').filter({hasText:'PS5'}).waitFor();
    await f.evaluate(()=>window.__screenTouchpadMemory.release());await f.evaluate(()=>window.__navigate('/touch-mouse'));await press(f,f.locator('[data-touch-mouse-section="touch"]'));
    assert.equal(await master().getAttribute('aria-checked'),'true');
    assert.equal(await f.evaluate(()=>window.__screenTouchpadMemory.profiles.steamdeck.masterEnabled),false);
  });
  await actionCase('Controller LB/RB still switches navigation pages; hidden tabs never leak into other pages',height,async()=>{
    await command(f,'page-next');await f.locator('.route-view[data-route="/gyro-motion"]').waitFor();
    await command(f,'page-prev');await f.locator('.route-view[data-route="/touch-mouse"]').waitFor();
    await command(f,'page-prev');await f.locator('.route-view[data-route="/button-mapping"]').waitFor();
    assert.equal(await f.evaluate(()=>window.__gamepadAudit.focusables().some(el=>el.closest('.navrail'))),false);
  });
  await actionCase('Controller shortcut editor keeps modal ownership and returns with B',height,async()=>{
    await f.evaluate(()=>window.__navigate('/button-mapping'));await press(f,f.getByRole('button',{name:'打开编辑器',exact:true}));await f.locator('.editor-overlay').waitFor();
    await state(f,'controller/standard/shortcut-editor',height,'.editor-overlay');await command(f,'page-next');assert.equal(await f.locator('.editor-overlay').count(),1);
    await command(f,'back');assert.equal(await f.locator('.editor-overlay').count(),0);assert.equal(await f.locator('[data-controller-section="controller"]').getAttribute('aria-pressed'),'true');
  });
}
