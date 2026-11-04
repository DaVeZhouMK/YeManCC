import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';

// Real Y menu + trainer component + production navigation; native actions stay
// behind the memory IPC boundary. No trainer/manager/CE action is launched.
export async function trainerMenuCases({height,open,command,press,actionCase,state,page,task,scope="all"}) {
  assert.ok(["all","lossless"].includes(scope),"Unknown trainer case scope");
  const geometry = f => f.evaluate(() => {
    const menu=document.querySelector('[data-trainer-menu]'),list=menu.querySelector('.trainer-menu-items'),anchor=document.querySelector('[data-gp-game-control="trainer-menu"]');
    const box=el=>{const r=el.getBoundingClientRect();return {left:r.left,top:r.top,right:r.right,bottom:r.bottom,width:r.width,height:r.height};};
    const safeBottom=innerHeight-Number.parseFloat(getComputedStyle(document.documentElement).getPropertyValue('--gamepad-viewport-bottom-gap')||'0');
    return {menu:box(menu),list:box(list),anchor:box(anchor),heading:box(menu.querySelector('.trainer-menu-heading')),close:box(menu.querySelector('.trainer-menu-cancel')),scroll:list.scrollHeight-list.clientHeight,outerScroll:menu.scrollHeight-menu.clientHeight,safeBottom,viewport:[innerWidth,innerHeight]};
  });
  function contained(g){assert.ok(g.menu.top>=23 && g.menu.bottom<=g.safeBottom+1,JSON.stringify(g));assert.ok(g.menu.left>=7 && g.menu.right<=g.viewport[0]-7,JSON.stringify(g));assert.ok(g.close.top>=g.menu.top && g.close.bottom<=g.menu.bottom-1,JSON.stringify(g));assert.ok(g.heading.top>=g.menu.top && g.heading.bottom<=g.menu.bottom,JSON.stringify(g));assert.ok(g.outerScroll<=1,JSON.stringify(g));}
  {
    const f=await open('/settings',height,{trainerMenu:'lossless-xbox'});
    await command(f,'edit-game');await f.locator('[data-gp-game-quick-menu]').waitFor();
    const button=f.locator('[data-gp-game-control="lossless-scaling"]');
    await f.waitForFunction(()=>!document.querySelector('[data-gp-game-control="lossless-scaling"]')?.disabled);
    await actionCase('Lossless one-click toggle preserves parameters and unbinds only the current EXE; lamp is process-only',height,async()=>{
      assert.equal(await button.getAttribute('role'),null);assert.equal(await button.getAttribute('aria-checked'),null);
      assert.match(await button.innerText(),/一键开启小黄鸭/);assert.match(await button.innerText(),/小黄鸭未运行/);
      const remove=f.locator('[data-gp-game-control="lossless-remove"]');assert.equal(await remove.isDisabled(),true);
      await state(f,'top-game-menu/lossless-button',height,'[data-gp-game-quick-menu]');
      await press(f,button);
      await f.waitForFunction(()=>!document.querySelector('[data-gp-game-control="lossless-scaling"]')?.disabled&&document.querySelector('.quick-lossless')?.innerText.includes('小黄鸭已运行'));
      const afterOn=await f.evaluate(()=>({xml:window.__losslessMemory.read(),launches:window.__losslessMemory.launches,focus:window.__losslessMemory.focus}));
      assert.match(afterOn.xml,/Cyberpunk2077/);assert.equal(afterOn.launches,1);assert.equal(afterOn.focus.length,0);
      assert.match(await button.innerText(),/一键关闭小黄鸭/);assert.equal(await remove.isDisabled(),false);
      await button.evaluate(el=>window.__gamepadAudit.focus(el));await command(f,'nav-right');assert.equal(await remove.evaluate(el=>document.activeElement===el),true);
      await command(f,'nav-left');assert.equal(await button.evaluate(el=>document.activeElement===el),true);
      await command(f,'nav-down');assert.equal(await f.locator('[data-gp-game-control="trainer-menu"]').evaluate(el=>document.activeElement===el),true);
      await command(f,'nav-up');assert.equal(await button.evaluate(el=>document.activeElement===el),true);
      await state(f,'top-game-menu/lossless-button',height,'[data-gp-game-quick-menu]');
      await f.locator('.app-root').screenshot({path:path.join(task,'evidence','lossless-on-xbox-'+height+'.png')});
      await press(f,button);
      await f.waitForFunction(()=>!document.querySelector('[data-gp-game-control="lossless-scaling"]')?.disabled);
      const again=await f.evaluate(()=>({xml:window.__losslessMemory.read(),running:window.__losslessMemory.running,launches:window.__losslessMemory.launches,closes:window.__losslessMemory.closes,focus:window.__losslessMemory.focus}));
      assert.equal(again.running,false);assert.equal(again.launches,1);assert.equal(again.closes,1);
      const disabledXml=afterOn.xml.replace(/(<Path>C:\/Games\/Cyberpunk2077\.exe<\/Path>\s*<AutoScale>)true(<\/AutoScale>)/,'$1false$2');
      assert.notEqual(disabledXml,afterOn.xml,'The exact current EXE profile must be found independently of its display title');assert.equal(again.xml,disabledXml);
      assert.match(again.xml,/Other.exe/);assert.match(again.xml,/<Unknown>retained<\/Unknown>/);assert.equal(again.focus.length,0);assert.match(await button.innerText(),/一键开启小黄鸭/);
      await press(f,button);await f.waitForFunction(()=>!document.querySelector('[data-gp-game-control="lossless-scaling"]')?.disabled);
      assert.match(await button.innerText(),/一键关闭小黄鸭/);assert.equal(await f.evaluate(()=>window.__losslessMemory.launches),2);
      await press(f,remove);await f.waitForFunction(()=>document.querySelector('[data-gp-game-control="lossless-remove"]')?.disabled===true);
      const removed=await f.evaluate(()=>({xml:window.__losslessMemory.read(),running:window.__losslessMemory.running,closes:window.__losslessMemory.closes,focus:window.__losslessMemory.focus}));
      assert.equal(removed.running,false);assert.equal(removed.closes,2);assert(!removed.xml.includes('Cyberpunk2077'));assert.match(removed.xml,/Other.exe/);assert.match(removed.xml,/<Unknown>retained<\/Unknown>/);assert.equal(removed.focus.length,0);
      assert.match(await button.innerText(),/一键开启小黄鸭/);await f.locator('.app-root').screenshot({path:path.join(task,'evidence','lossless-unbound-xbox-'+height+'.png')});
      await press(f,button);await f.waitForFunction(()=>!document.querySelector('[data-gp-game-control="lossless-scaling"]')?.disabled);
      assert.equal(await f.evaluate(()=>window.__losslessMemory.launches),3);assert.match(await f.evaluate(()=>window.__losslessMemory.read()),/Cyberpunk2077/);
      await f.evaluate(()=>{window.__losslessMemory.running=false;});
      await command(f,'back');await command(f,'edit-game');
      await f.waitForFunction(()=>document.querySelector('.quick-lossless')?.innerText.includes('小黄鸭未运行'));
      assert.equal(await f.evaluate(()=>window.__losslessMemory.launches),3);
      await command(f,'back');assert.equal(await f.locator('[data-gp-game-quick-menu]').count(),0);
    });
  }
  if(scope==='lossless')return; // Optional exact original subgroup; default trainer coverage remains intact.
  for(const seed of ['no-scan','delayed-scan']){
    const f=await open('/settings',height,{trainerMenu:seed});
    await command(f,'edit-game');await f.locator('[data-gp-game-quick-menu]').waitFor();
    const entry=f.locator('[data-gp-game-control="trainer-menu"]');
    await press(f,entry);const menu=f.locator('[data-trainer-menu]');await menu.waitFor();
    const initial=await geometry(f);
    if(seed==='delayed-scan')await menu.locator('[data-trainer-value-scan]').waitFor();
    else assert.equal(await menu.locator('[data-trainer-value-scan]').count(),0);
    await page.waitForTimeout(60);
    await state(f,'top-game-menu/trainer/'+seed,height,'[data-trainer-menu]');
    await actionCase('Trainer '+seed+' anchors to entry and uses full height before scrolling',height,async()=>{
      const g=await geometry(f);contained(g);assert.ok(Math.abs(g.menu.left-Math.max(8,Math.min(g.anchor.left,g.viewport[0]-8-g.menu.width)))<2,JSON.stringify(g));
      assert.equal(g.scroll,0,'Normal viewport must show the complete trainer list without a scrollbar: '+JSON.stringify(g));
      fs.writeFileSync(path.join(task,'evidence','trainer-geometry-'+seed+'-'+height+'.json'),JSON.stringify(g,null,2));
      await menu.locator('.trainer-menu-items').hover();await page.mouse.wheel(0,240);await page.waitForTimeout(30);assert.equal(await menu.locator('.trainer-menu-items').evaluate(el=>el.scrollTop),0);
      if(seed==='delayed-scan')assert.ok(g.menu.height>initial.menu.height+10,'New scan item must grow a previously placed menu');
      await f.locator('.app-root').screenshot({path:path.join(task,'evidence','trainer-'+seed+'-'+height+'.png')});
      // The trainer popover is teleported to body, outside .app-root.
      await page.screenshot({path:path.join(task,'evidence','trainer-screen-'+seed+'-'+height+'.png')});
    });
    await actionCase('Trainer '+seed+' owns LB/RB and B closes only its submenu',height,async()=>{
      const route=await f.evaluate(()=>window.__currentAuditRoute());
      for(const action of ['page-next','page-prev']){await command(f,action);assert.equal(await f.evaluate(()=>window.__currentAuditRoute()),route);assert.equal(await menu.count(),1);}
      await command(f,'back');await menu.waitFor({state:'detached'});assert.equal(await f.locator('[data-gp-game-quick-menu]').count(),1);assert.ok(await entry.evaluate(el=>document.activeElement===el));
    });
    if(seed==='delayed-scan'){
      await press(f,entry);await menu.waitFor();await menu.locator('[data-trainer-value-scan]').waitFor();
      await actionCase('Trainer small viewport keeps heading/close fixed and scrolls each focused action into view',height,async()=>{
        // The same opened component repositions when the viewport gets short.
        const viewport=f.page().viewportSize(),frame=f.parentFrame()?await f.frameElement():null;
        const previousFrameStyle=frame?await frame.evaluate(el=>({height:el.style.height,width:el.style.width})):null;
        if(frame)await frame.evaluate(el=>{el.style.height='320px';});else await f.page().setViewportSize({width:height===720?535:595,height:320});await page.waitForTimeout(120);
        const compact=await geometry(f);contained(compact);assert.ok(compact.scroll>0,JSON.stringify(compact));
        const controls=menu.locator('button:not(:disabled)');
        await controls.first().evaluate(el=>window.__gamepadAudit.focus(el));
        for(let i=0;i<await controls.count();i++){
          assert.ok(await controls.nth(i).evaluate(el=>document.activeElement===el),'Down skipped trainer item '+i);
          const visible=await controls.nth(i).evaluate(el=>{const r=el.getBoundingClientRect(),owner=el.closest('.trainer-menu-items')||el.closest('[data-trainer-menu]'),v=owner.getBoundingClientRect();return r.top>=v.top-2&&r.bottom<=v.bottom+2;});assert.ok(visible,'Clipped trainer focus '+i);
          if(i+1<await controls.count())await command(f,'nav-down');
        }
        const after=await geometry(f);assert.ok(Math.abs(after.heading.top-compact.heading.top)<1&&Math.abs(after.close.top-compact.close.top)<1,'List scroll must not move header or close');
        if(frame)await frame.evaluate((el,style)=>{el.style.height=style.height;el.style.width=style.width;},previousFrameStyle);else await f.page().setViewportSize(viewport);await page.waitForTimeout(100);
      });
      await command(f,'back');await menu.waitFor({state:'detached'});
      await press(f,entry);await menu.waitFor();
      await actionCase('Trainer native A on close restores original entry focus',height,async()=>{await press(f,menu.locator('.trainer-menu-cancel'));await menu.waitFor({state:'detached'});assert.ok(await entry.evaluate(el=>document.activeElement===el));assert.equal(await f.locator('[data-gp-game-quick-menu]').count(),1);});
    }
    await command(f,'back');await f.locator('[data-gp-game-quick-menu]').waitFor({state:'detached'});
  }
}
