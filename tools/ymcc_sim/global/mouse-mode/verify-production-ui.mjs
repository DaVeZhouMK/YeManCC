import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {createRequire} from 'node:module';
const task=import.meta.dirname,require=createRequire(process.env.YMCC_SIM_BROWSER_RUNTIME_ENTRY||'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/_mouse.cjs'),{chromium}=require('playwright'),{url}=JSON.parse(fs.readFileSync(path.join(task,'server.json'),'utf8'));
const browser=await chromium.launch({channel:'msedge',headless:true});const cases=[],errors=[];let sequence=1;
function need(v,name,height){assert.ok(v,name);cases.push({name,height});}
const base={ok:true,mode:'auto',scene:'desktop',on:true,stateKnown:true,provider:'ymcc',controlAllowed:true,mappingVerified:false,verification:'process-liveness',pending:false,policyReason:'desktop_foreground'};
try{
 for(const height of [720,800]){
  const page=await browser.newPage({viewport:{width:1280,height}});page.on('pageerror',e=>errors.push(e.message));await page.goto(url+'product?height='+height+'&route=/schedule');await page.waitForFunction(()=>window.__previewReady&&window.__mouseProduction&&window.__gamepadAudit);
  await page.evaluate(v=>window.__mouseProduction.accept(v),{...base,sequence:sequence++});
  const sidebar=page.locator('.nav-item').filter({hasText:'控制器'});need(await sidebar.count()===1,'automatic-desktop-keeps-controller-sidebar',height);
  await page.evaluate(()=>window.__gamepadAudit.command('edit-game'));const menu=page.locator('[data-gp-game-quick-menu]');await menu.waitFor();const mouse=menu.getByRole('button',{name:/模拟鼠标/});await mouse.waitFor();await page.waitForFunction(()=>!document.querySelector('[title="切换 YMCC 模拟鼠标"]')?.disabled);
  await mouse.evaluate(el=>{window.__gamepadAudit.focus(el);window.__gamepadAudit.command('confirm');});await page.waitForFunction(()=>window.__fixture.mouseModeState?.mode==='auto');need((await mouse.textContent()).includes('自动 · 已暂停'),'controller-A-selects-auto-and-shows-ymcc-pause',height);
  await mouse.evaluate(el=>{window.__gamepadAudit.focus(el);window.__gamepadAudit.command('confirm');});await page.waitForFunction(()=>window.__fixture.mouseModeState?.mode==='on');need((await mouse.textContent()).includes('映射待确认'),'manual-on-does-not-claim-mapping-proof',height);
  need(await page.locator('.nav-item').filter({hasText:'模拟鼠标'}).count()===1,'manual-only-sidebar-becomes-mouse',height);
  need(await mouse.locator('.app-icon-mouse-classic').count()===1,'accepted-A-icon-used-in-real-menu',height);
  need(await mouse.locator('.app-icon-mouse-classic').evaluate(el=>getComputedStyle(el).maskImage.startsWith('url("data:image/png')),'accepted-icon-loaded-without-external-path',height);
  await mouse.click();await page.waitForFunction(()=>window.__fixture.mouseModeState?.mode==='off');need((await mouse.textContent()).includes('关闭'),'third-detent-disables',height);
  need(await page.locator('.nav-item').filter({hasText:'控制器'}).count()===1,'off-restores-controller-sidebar',height);
  await page.evaluate(()=>{window.__fixture.mousePersona='steamdeck';window.dispatchEvent(new CustomEvent('gp:pad-persona',{detail:{persona:'steamdeck'}}));});
  const steam=menu.getByRole('button',{name:/Steam鼠标/});await page.waitForFunction(()=>document.querySelector('[title="由 Steam 管理，仅显示状态"]')?.disabled);need(await steam.isDisabled(),'sd-control-readonly',height);need(!(await steam.evaluate(el=>window.__gamepadAudit.focusables().includes(el))),'sd-disabled-excluded-from-direction-navigation',height);
  await page.evaluate(()=>window.__gamepadAudit.command('back'));need(await menu.count()===0,'controller-B-leaves-menu',height);
  // The new real monitoring setting is reachable and activates with the production engine.
  await page.evaluate(()=>window.__navigate('/rtss'));const overview=page.locator('.monitor-overview');await overview.waitFor();
  const setting=overview.getByRole('switch',{name:'鼠标状态'});await setting.waitFor();await page.waitForFunction(()=>!document.querySelector('.monitor-overview-controls button:last-child')?.disabled);
  need(await setting.isEnabled(),'monitor-has-enabled-mouse-status-setting',height);
  const before=await setting.getAttribute('aria-checked');
  await setting.evaluate(el=>{window.__gamepadAudit.focus(el);window.__gamepadAudit.command('confirm');});
  await page.waitForTimeout(100);need(await setting.getAttribute('aria-checked')!==before,'production-controller-A-changes-mouse-status-setting',height);
  await setting.evaluate(el=>{window.__gamepadAudit.command('nav-left');});
  need(await page.evaluate(()=>document.activeElement?.closest('.toggle-row')?.textContent?.includes('弹出提示')),'mouse-setting-has-correct-left-neighbour',height);
  await page.evaluate(()=>window.__gamepadAudit.command('nav-right'));
  need(await setting.evaluate(el=>el===document.activeElement),'mouse-setting-can-return-with-right',height);
  // Real renderer gets authoritative work area and B defaults; initial state is quiet.
  await page.evaluate(v=>{window.__mouseProduction.accept(v);window.__mouseProduction.hud();},{...base,sequence:sequence++});await page.waitForTimeout(100);
  const water=page.locator('[data-mouse-watermark]');need(await water.count()===0,'initial-monitor-mount-is-quiet',height);
  await page.evaluate(v=>window.__mouseProduction.accept(v),{...base,on:false,scene:'external',sequence:sequence++});
  await water.waitFor();need(await water.locator('strong').innerText()==='手柄模式','foreground-game-title-is-controller-mode',height);
  await page.screenshot({path:path.join(task,'evidence','monitor-B-controller-'+height+'.png')});
  await page.evaluate(v=>window.__mouseProduction.accept(v),{...base,sequence:sequence++});
  await water.waitFor();need(await water.locator('strong').innerText()==='鼠标模式','desktop-title-is-mouse-mode',height);need((await water.innerText()).trim()==='鼠标模式'&&await water.locator('small').count()===0,'hint-has-only-mode-title-no-secondary-text',height);
  const rect=await water.boundingBox();need(rect.x>900&&rect.y>height-110&&rect.x+rect.width<=1280&&rect.y+rect.height<=height-42,'hint-bottom-right-inside-work-area',height);
  need(await water.evaluate(el=>getComputedStyle(el).pointerEvents==='none'),'hint-input-transparent',height);
  need(await water.locator('.app-icon-mouse-classic').count()===1,'hint-reuses-selected-A-mouse-icon',height);
  await page.screenshot({path:path.join(task,'evidence','monitor-B-mouse-'+height+'.png')});
  await page.waitForTimeout(1500);await page.evaluate(v=>window.__mouseProduction.accept(v),{...base,sequence:sequence++});await page.waitForTimeout(1150);
  need(await water.count()===0,'duplicate-frame-does-not-extend-hint',height);
  await page.evaluate(v=>window.__mouseProduction.accept(v),{...base,sequence:sequence++});await page.waitForTimeout(50);need(await water.count()===0,'periodic-frame-does-not-resurrect-expired-hint',height);
  await page.evaluate(v=>window.__mouseProduction.accept(v),{...base,scene:'ymcc',on:false,verification:'backend-state',sequence:sequence++});await page.waitForTimeout(25);need(await water.count()===0,'ymcc-entry-is-silent',height);
  await page.evaluate(v=>window.__mouseProduction.accept(v),{...base,pending:true,sequence:sequence++});await page.waitForTimeout(25);need(await water.count()===0,'ymcc-return-pending-is-silent',height);
  await page.evaluate(v=>window.__mouseProduction.accept(v),{...base,sequence:sequence++});await page.waitForTimeout(25);need(await water.count()===0,'ymcc-return-desktop-is-silent',height);
  await page.evaluate(v=>window.__mouseProduction.observeOnly(v),{...base,on:false,scene:'ymcc',sequence:sequence++});
  await page.evaluate(v=>window.__mouseProduction.accept(v),{...base,on:false,scene:'external',sequence:sequence++});await page.waitForTimeout(25);need(await water.count()===0,'quick-ymcc-visit-with-skipped-monitor-frame-is-silent',height);
  await page.evaluate(v=>window.__mouseProduction.accept(v),{...base,sequence:sequence++});await water.waitFor();need(await water.locator('strong').innerText()==='鼠标模式','next-real-transition-after-ymcc-return-shows',height);
  await page.evaluate(v=>window.__mouseProduction.accept(v),{...base,on:false,scene:'external',sequence:sequence++});await water.waitFor();
  await page.evaluate(()=>window.__mouseProduction.enabled(false));await page.waitForTimeout(25);need(await water.count()===0,'top-level-monitor-setting-hides-immediately',height);
  await page.evaluate(v=>window.__mouseProduction.accept(v),{...base,sequence:sequence++});await page.waitForTimeout(25);need(await water.count()===0,'disabled-monitor-setting-stays-quiet-on-desktop',height);
  await page.evaluate(()=>window.__mouseProduction.enabled(true));await page.waitForTimeout(25);need(await water.count()===0,'reenabling-setting-does-not-replay-old-mode',height);
  await page.evaluate(v=>window.__mouseProduction.accept(v),{...base,scene:'external',on:false,sequence:sequence++});await water.waitFor();need(await water.count()===1,'next-real-transition-after-reenable-renders',height);
  await page.evaluate(v=>window.__mouseProduction.accept(v),{...base,mode:'on',sequence:sequence++});await page.waitForTimeout(25);need(await water.count()===0,'manual-on-hides-automatic-hint',height);
  await page.evaluate(v=>window.__mouseProduction.accept(v),{...base,mode:'off',on:false,sequence:sequence++});await page.waitForTimeout(25);need(await water.count()===0,'manual-off-has-no-hint',height);
  await page.evaluate(v=>window.__mouseProduction.accept(v),{...base,provider:'steam',controlAllowed:false,stateKnown:false,on:false,sequence:sequence++});await page.waitForTimeout(25);need(await water.count()===0,'sd-steam-state-has-no-hint',height);
  for(const state of ['applied','failed'])await page.evaluate(state=>window.__mouseProduction.notice({schemaVersion:1,id:'accept-'+state,actionId:'mouse.toggle',origin:'controller-shortcut',state,title:'模拟鼠标',message:'旧鼠标通知',occurredAtUnixMs:Date.now(),expiresAtUnixMs:Date.now()+2500,powerGeneration:1,presentation:{fullscreen:true,leftMonitor:true}}),state);
  need(await page.locator('.shortcut-notice').count()===0,'legacy-large-mouse-notice-not-rendered-even-on-failure',height);
  await page.evaluate(()=>window.__mouseProduction.notice({schemaVersion:1,id:'brightness',actionId:'display.brightnessAdjust',origin:'controller-shortcut',state:'applied',title:'亮度',message:'亮度已调整',value:{text:'50%'},occurredAtUnixMs:Date.now(),expiresAtUnixMs:Date.now()+2500,powerGeneration:1,presentation:{fullscreen:true,leftMonitor:true}}));
  need(await page.locator('.shortcut-notice').count()===1,'other-large-notifications-still-render',height);
  await page.close();
 }
 need(errors.length===0,'no-browser-errors',0);const report={status:'PASS',cases,errors,scope:'actual GameQuickActions/NavRail/MonitorHudRenderer/MonitorShortcutNotice and production gamepad engine; memory native boundary; not OS classifier or physical-controller acceptance'};fs.writeFileSync(path.join(task,'mouse-production-ui-result.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({status:'PASS',cases:cases.length}));
}finally{await browser.close();}
