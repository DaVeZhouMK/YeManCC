import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';

// Settings, Steam and Sleep are real mainline components. Only IO uses memory.
export async function steamDiagnosticsCases({height,open,command,press,actionCase,state,page,task}) {
  let f=await open('/steam',height,{nativeWidth:'1'});
  await f.waitForFunction(()=>document.querySelector('.sc-text')?.textContent==='大屏Steam运行中');
  await actionCase('Big Picture running status displays only the current client on A',height,async()=>{
    await press(f,f.locator('.steam-state-card'));
    await f.waitForFunction(()=>window.__previewCalls.some(c=>c.cmd==='steam.ui.showBigPicture'));
    const commands=await f.evaluate(()=>window.__previewCalls.filter(c=>/steam\.ui(?:Mode|\.launch|\.show)/.test(c.cmd)));
    assert.deepEqual(commands,[{cmd:'steam.ui.showBigPicture',args:{pid:100,birth:'456',userInitiated:true}}]);
  });
  await actionCase('Desktop running status only rechecks and never switches or launches',height,async()=>{
    await f.waitForFunction(()=>!document.querySelector('.steam-state-card')?.disabled);
    await f.evaluate(()=>window.__fixture.steamUiState={known:true,running:true,mode:'desktop',pid:100,birth:'456',reason:''});
    await press(f,f.locator('.steam-state-card'));
    await f.waitForFunction(()=>document.querySelector('.sc-text')?.textContent==='桌面Steam运行中'&&!document.querySelector('.steam-state-card')?.disabled);
    await press(f,f.locator('.steam-state-card'));
    assert.equal(await f.evaluate(()=>window.__previewCalls.filter(c=>c.cmd==='steam.ui.showBigPicture').length),1);
    assert.equal(await f.evaluate(()=>window.__previewCalls.filter(c=>/steam\.uiMode\.set|steam\.ui\.launch/.test(c.cmd)).length),0);
  });
  await actionCase('Steam has no duplicate recovery UI or contact reads',height,async()=>{
    await press(f,f.locator('[data-settings-section="integration"]'));
    assert.equal(await f.locator('.steam-contact-card,.steam-recovery-diagnostics,.sleep-recovery-auto').count(),0);
    assert.equal(await f.evaluate(()=>window.__previewCalls.filter(c=>c.cmd==='steamGameLink.get').length),0);
  });
  await state(f,'steam/integration-after-diagnostics-move',height);
  f=await open('/steam',height,{nativeWidth:'1'});
  await press(f,f.locator('[data-settings-section="integration"]'));
  await actionCase('Decky manual repair is reachable on A and never runs from a status event',height,async()=>{
    await f.evaluate(()=>window.__emitNativeEvent('deckySidebar.updated',{enabled:true,pid:0,revision:21,phase:'waiting-manual-steam',reason:'decky-context-already-loaded',steamKnown:true,steamRunning:true}));
    const button=f.locator('[data-gp-id="steam-decky-repair"]');await button.waitFor();
    assert.equal(await f.evaluate(()=>window.__previewCalls.filter(c=>c.cmd==='deckySidebar.repairConnection').length),0);
    await press(f,button);await f.waitForFunction(()=>window.__previewCalls.some(c=>c.cmd==='deckySidebar.repairConnection'));
    assert.deepEqual(await f.evaluate(()=>window.__previewCalls.filter(c=>c.cmd==='deckySidebar.repairConnection')),[{cmd:'deckySidebar.repairConnection',args:{userInitiated:true,pid:100,birth:'456',revision:21}}]);
    await f.evaluate(()=>window.__emitNativeEvent('deckySidebar.updated',{enabled:true,pid:123,revision:22,phase:'loader-started',reason:'mirror-handshake-pending',steamKnown:true,steamRunning:true}));
    await button.waitFor({state:'visible'});
  });
  f=await open('/steam',height,{nativeWidth:'1'});
  await press(f,f.locator('[data-settings-section="integration"]'));
  await actionCase('Retained bridge offers explicit backend repair without automatic dispatch',height,async()=>{
    const before=await f.evaluate(()=>window.__previewCalls.filter(c=>c.cmd==='deckySidebar.repairConnection').length);
    await f.evaluate(()=>window.__emitNativeEvent('deckySidebar.updated',{enabled:true,pid:0,revision:23,phase:'frontend-attached',reason:'retained-frontend-connected',steamKnown:true,steamRunning:true}));
    const button=f.locator('[data-gp-id="steam-decky-repair"]');await button.waitFor();
    assert.match(await f.locator('.decky-connection').innerText(),/正在接续服务[\s\S]*插件服务尚未就绪/);
    assert.equal(await f.locator('.decky-connection').getAttribute('data-tone'),'pending','transport alone never reports fully ready');
    await page.locator('.screen').screenshot({path:path.join(task,'evidence','decky-retained-backend-repair-'+height+'.png')});
    assert.equal(await f.evaluate(()=>window.__previewCalls.filter(c=>c.cmd==='deckySidebar.repairConnection').length),before);
    await press(f,button);await f.waitForFunction(n=>window.__previewCalls.filter(c=>c.cmd==='deckySidebar.repairConnection').length===n+1,before);
    assert.deepEqual(await f.evaluate(()=>window.__previewCalls.filter(c=>c.cmd==='deckySidebar.repairConnection').at(-1).args),{userInitiated:true,pid:100,birth:'456',revision:23});
    await f.evaluate(()=>window.__emitNativeEvent('deckySidebar.updated',{enabled:false,pid:0,revision:24,phase:'disabled',reason:'user-disabled',steamKnown:true,steamRunning:true}));
    await button.waitFor({state:'detached'});
  });
  await actionCase('Native automatic repair is visible without frontend dispatch or error banner',height,async()=>{
    await f.evaluate(()=>window.__emitNativeEvent('deckySidebar.updated',{enabled:true,pid:0,revision:25,phase:'frontend-attached',reason:'retained-frontend-connected',steamKnown:true,steamRunning:true,recovery:{phase:'recovering',attempted:true}}));
    await f.locator('.decky-connection').filter({hasText:'正在恢复连接'}).waitFor();
    assert.equal(await f.evaluate(()=>window.__previewCalls.filter(c=>c.cmd==='deckySidebar.repairConnection').length),1);
    assert.equal(await f.locator('.err-bar').count(),0);
    await page.locator('.screen').screenshot({path:path.join(task,'evidence','decky-auto-repair-'+height+'.png')});
    await f.evaluate(()=>window.__emitNativeEvent('deckySidebar.updated',{enabled:true,pid:123,revision:26,phase:'loader-started',reason:'mirror-handshake-pending',steamKnown:true,steamRunning:true}));
    await f.locator('[data-gp-id="steam-decky-repair"]').waitFor({state:'visible'});
  });
  await actionCase('Owned Loader retains manual repair and tracks authenticated connection and power hold',height,async()=>{
    await f.evaluate(()=>window.__emitNativeEvent('deckySidebar.updated',{enabled:true,pid:123,revision:27,connectionRevision:1,phase:'loader-started',reason:'mirror-handshake-pending',steamKnown:true,steamRunning:true,mirrorConnected:true,powerReady:true}));
    const button=f.locator('[data-gp-id="steam-decky-repair"]');await button.waitFor();assert.equal(await button.isEnabled(),true);
    assert.match(await f.locator('.decky-connection').innerText(),/已连接 YMCC/);assert.equal(await f.locator('.decky-connection').getAttribute('data-tone'),'connected');
    const count=await f.evaluate(()=>window.__previewCalls.filter(c=>c.cmd==='deckySidebar.repairConnection').length);
    await press(f,button);await f.waitForFunction(n=>window.__previewCalls.filter(c=>c.cmd==='deckySidebar.repairConnection').length===n+1,count);
    assert.deepEqual(await f.evaluate(()=>window.__previewCalls.filter(c=>c.cmd==='deckySidebar.repairConnection').at(-1).args),{userInitiated:true,pid:100,birth:'456',revision:27});
    await f.evaluate(()=>window.__emitNativeEvent('deckySidebar.updated',{enabled:true,pid:123,revision:28,connectionRevision:2,phase:'loader-started',reason:'mirror-handshake-pending',steamKnown:true,steamRunning:true,mirrorConnected:true,powerReady:false}));
    assert.match(await f.locator('.decky-connection').innerText(),/等待唤醒完成/);assert.equal(await button.isDisabled(),true);
    await f.evaluate(()=>window.__emitNativeEvent('deckySidebar.updated',{enabled:true,pid:123,revision:29,connectionRevision:3,phase:'loader-started',reason:'mirror-handshake-pending',steamKnown:true,steamRunning:true,mirrorConnected:false,powerReady:true}));
    assert.equal(await button.isEnabled(),true);assert.match(await f.locator('.decky-connection').innerText(),/等待面板连接/);assert.equal(await f.locator('.decky-connection').getAttribute('data-tone'),'pending');
    await f.evaluate(()=>window.__emitNativeEvent('deckySidebar.updated',{enabled:true,pid:123,revision:29,connectionRevision:2,phase:'loader-started',reason:'mirror-handshake-pending',steamKnown:true,steamRunning:true,mirrorConnected:true,powerReady:true}));
    assert.equal(await f.locator('.decky-connection').getAttribute('data-tone'),'pending','older transport acknowledgement cannot overwrite a newer disconnect');
    await page.locator('.screen').screenshot({path:path.join(task,'evidence','decky-owned-backend-repair-'+height+'.png')});
  });
  await state(f,'steam/decky-manual-repair-after-ready',height);
  f=await open('/sleep',height,{nativeWidth:'1'});
  await actionCase('Sleep preserves pause setting without duplicate recovery entry',height,async()=>{
    assert.equal(await f.locator('.sleep-recovery-auto,.steam-recovery-diagnostics').count(),0);
    assert.equal(await f.getByRole('switch',{name:'睡眠暂停游戏',exact:true}).count(),1);
  });
  await state(f,'sleep/protection-after-diagnostics-move',height);
  f=await open('/settings',height,{nativeWidth:'1'});
  // 2026-10-22 product decision (STEAM-UPGRADE): recovery/contact UI was
  // removed from Settings; its background API remains. Test that actual contract.
  const retired='.steam-recovery-diagnostics,.sleep-recovery-auto,.recovery-link-states';
  await actionCase('Settings diagnostics keeps logs and excludes retired Steam recovery controls',height,async()=>{
    assert.equal(await f.locator(retired).count(),0);
    await press(f,f.locator('[data-settings-section="diagnostics"]'));
    await f.getByRole('region',{name:'诊断日志'}).waitFor().catch(async()=>{await f.locator('.fan-log-card').waitFor();});
    assert.equal(await f.locator('.fan-log-card').count(),1);
    assert.equal(await f.locator(retired).count(),0);
    assert.equal(await f.getByRole('switch',{name:'睡眠唤醒 Steam 自动修复',exact:true}).count(),0);
    assert.equal(await f.evaluate(()=>window.__previewCalls.filter(c=>/steamGameLink\.get|sleepRecovery\.(inspect|repair)|steam\.launch|launchSteam/.test(c.cmd)).length),0);
    assert.equal(await f.evaluate(()=>window.__sleepWrites?.filter(w=>w.name==='automaticRecovery').length||0),0);
  });
  await state(f,'settings/steam-diagnostics-retired',height);
  await page.locator('.screen').screenshot({path:path.join(task,'evidence','steam-diagnostics-'+height+'.png')});
  await actionCase('Reentering diagnostics and resumed events do not revive removed recovery UI',height,async()=>{
    await f.evaluate(()=>{window.__fixture.steamGameLink={fresh:false,linked:true,contactConnected:true};window.dispatchEvent(new Event('ipc:power.resumed'));});
    await press(f,f.locator('[data-settings-section="appearance"]'));
    await press(f,f.locator('[data-settings-section="diagnostics"]'));
    assert.equal(await f.locator(retired).count(),0);
    assert.equal(await f.evaluate(()=>window.__previewCalls.filter(c=>/steamGameLink\.get|sleepRecovery\.(inspect|repair)/.test(c.cmd)).length),0);
    assert.equal(await f.evaluate(()=>window.__sleepWrites?.filter(w=>w.name==='automaticRecovery').length||0),0);
  });
  await state(f,'settings/steam-diagnostics-reentered',height);
  await actionCase('B returns to section tabs and LB/RB retain route switching',height,async()=>{
    await f.locator('.fan-log-action').first().evaluate(el=>window.__gamepadAudit.focus(el));
    await command(f,'back');
    assert.equal(await f.locator('[data-settings-section="diagnostics"]').evaluate(el=>el===document.activeElement),true);
    await command(f,'page-next');
    await f.waitForFunction(()=>window.__currentAuditRoute()!=='/settings');
    await command(f,'page-prev');
    await f.waitForFunction(()=>window.__currentAuditRoute()==='/settings');
    assert.equal(await f.evaluate(()=>window.__gamepadAudit.focusables().some(el=>el.closest('.navrail'))),false);
  });
}
