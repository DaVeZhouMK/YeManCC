import assert from 'node:assert/strict';
import path from 'node:path';
// Real product components and engine; only native IO is an explicit memory boundary.
export async function steamDesktopMouseCases({height,open,command,press,actionCase,state,page,task}){
 let f=await open('/touch-mouse',height,{steamMouseSeed:'standard'});
 await f.getByRole('button',{name:'JoyXoff',exact:true}).waitFor();
 await actionCase('Touch/mouse page exposes Microsoft and JoyXoff with SD visibly locked',height,async()=>{
  assert.deepEqual(await f.locator('.mouse-backend-buttons button').allTextContents(),['微软鼠标','JoyXoff','SteamDeck']);
  assert.equal(await f.getByRole('button',{name:'SteamDeck',exact:true}).isDisabled(),true);assert.equal(await f.locator('.steamdeck-mouse-bubble').count(),0);
  const calls=await f.evaluate(()=>window.__previewCalls.filter(c=>(c.cmd==='mouseMode.setBackend'&&c.args.backend==='steam')||((c.cmd==='steam.settings.get'||c.cmd==='steam.settings.set')&&(c.args.scope==='mouse'||'mousePercent' in c.args))));assert.equal(calls.length,0);
  await press(f,f.getByRole('button',{name:'微软鼠标',exact:true}));await press(f,f.getByRole('button',{name:'JoyXoff',exact:true}));
  assert.equal(await f.getByRole('button',{name:'JoyXoff',exact:true}).evaluate(el=>el.classList.contains('active')),true);
 });
 await state(f,'controller/standard-mouse',height);
 await actionCase('Ordinary backend timeout releases controls and B dismisses the warning',height,async()=>{
  await f.evaluate(()=>window.__fixture.mouseBackendThrow=true);await press(f,f.getByRole('button',{name:'微软鼠标',exact:true}));
  await f.waitForFunction(()=>document.querySelector('.mouse-backend-popover')?.textContent.includes('响应超时'));
  assert.equal(await f.getByRole('button',{name:'微软鼠标',exact:true}).isEnabled(),true);
  await command(f,'back');await f.waitForFunction(()=>!document.querySelector('.mouse-backend-popover'));
  await f.evaluate(()=>window.__fixture.mouseBackendThrow=false);await press(f,f.getByRole('button',{name:'微软鼠标',exact:true}));
  assert.equal(await f.getByRole('button',{name:'微软鼠标',exact:true}).evaluate(el=>el.classList.contains('active')),true);
 });
 await actionCase('ROG Microsoft mouse uses the existing warning without installing or changing backend',height,async()=>{
  await press(f,f.getByRole('button',{name:'JoyXoff',exact:true}));
  const before=await f.evaluate(()=>window.__fixture.mouseWrites??0);
  await f.evaluate(()=>window.__fixture.mouseRogModel=true);await press(f,f.getByRole('button',{name:'微软鼠标',exact:true}));
  const warning=f.getByRole('alertdialog',{name:'微软鼠标不可用',exact:true});await warning.waitFor();
  assert.match(await warning.innerText(),/ROG 家族/);assert.equal(await warning.getByRole('button',{name:/确认修复/}).count(),0);
  assert.equal(await f.evaluate(()=>window.__fixture.mouseWrites??0),before);
  assert.equal(await f.getByRole('button',{name:'JoyXoff',exact:true}).evaluate(el=>el.classList.contains('active')),true);
  assert.equal(await f.evaluate(()=>window.__previewCalls.filter(c=>c.cmd==='gameinput.redist.install').length),0);
  await command(f,'back');await f.waitForFunction(()=>!document.querySelector('.gameinput-repair-confirm'));
  assert.equal(await f.getByRole('button',{name:'微软鼠标',exact:true}).evaluate(el=>document.activeElement===el),true);
  await f.evaluate(()=>{window.__fixture.mouseRogModel=false;window.__fixture.mouseComponentMissing=true;});
  await press(f,f.getByRole('button',{name:'微软鼠标',exact:true}));
  await f.getByRole('alertdialog',{name:'修复微软鼠标组件',exact:true}).waitFor();
  assert.equal(await f.getByRole('button',{name:'确认修复',exact:true}).isVisible(),true);
  await command(f,'back');await f.waitForFunction(()=>!document.querySelector('.gameinput-repair-confirm'));
  await f.evaluate(()=>window.__fixture.mouseComponentMissing=false);
 });
 await actionCase('ON selection remains ON while YMCC pauses the backend and shares list explanations',height,async()=>{
  await press(f,f.locator('.mouse-policy-modes').getByRole('button',{name:'开启',exact:true}));
  assert.equal(await f.locator('.mouse-policy-modes').getByRole('button',{name:'开启',exact:true}).getAttribute('aria-pressed'),'true');
  assert.equal(await f.locator('.nav-touch-mouse .mouse-status-icon').getAttribute('data-mouse-icon-state'),'on');
  assert.match(await f.locator('.mouse-settings').innerText(),/开启挡暂停 · YMCC/);
  assert.match(await f.locator('.mouse-settings').innerText(),/自动、开启均遵循鼠标名单/);
 });
 f=await open('/touch-mouse',height,{steamMouseSeed:'legacy-steam'});
 await actionCase('Old Steam backend receipt does not remount imports or ordinary sensitivity',height,async()=>{
  await f.getByRole('button',{name:'JoyXoff',exact:true}).waitFor();assert.equal(await f.getByRole('button',{name:'JoyXoff',exact:true}).evaluate(el=>el.classList.contains('active')),true);
  assert.equal(await f.locator('.steamdeck-mouse-bubble').count(),0);assert.equal(await f.getByRole('button',{name:'SteamDeck',exact:true}).isDisabled(),true);
  assert.equal(await f.evaluate(()=>window.__previewCalls.filter(c=>c.cmd==='mouseMode.setBackend').length),0);
 });
 f=await open('/touch-mouse',height);
 await f.getByRole('button',{name:'SteamDeck',exact:true}).waitFor();
 await actionCase('SteamDeck retains managed status and sensitivity with ordinary controls disabled',height,async()=>{
  const steam=f.getByRole('button',{name:'SteamDeck',exact:true});assert.equal(await steam.isDisabled(),true);assert.equal(await steam.getAttribute('aria-pressed'),'true');
  assert.equal(await f.getByRole('button',{name:'微软鼠标',exact:true}).isDisabled(),true);assert.equal(await f.getByRole('button',{name:'JoyXoff',exact:true}).isDisabled(),true);
  const slider=f.getByRole('slider',{name:'SteamDeck鼠标灵敏度',exact:true});await slider.waitFor();assert.equal(await slider.inputValue(),'100');
  await press(f,slider);await command(f,'nav-right');await command(f,'confirm');await f.waitForFunction(()=>window.__fixture.mouseSensitivityWrites>0);assert.equal(await slider.inputValue(),'105');
  assert.equal(await f.evaluate(()=>window.__previewCalls.filter(c=>c.cmd==='mouseMode.setBackend'&&c.args.backend==='steam').length),0);
 });
 await state(f,'controller/steam-mouse',height);await page.screenshot({path:path.join(task,'steamdeck-managed-mouse-'+height+'.png')});
 f=await open('/schedule',height,{steamMouseSeed:'standard'});
 await f.evaluate(()=>{window.__fixture.mouseModeState={ok:true,backend:'joyxoff',provider:'steam',mode:'auto',scene:'steam',on:true,stateKnown:true,controlAllowed:true};window.__gamepadAudit.command('edit-game');});
 const menu=f.locator('[data-gp-game-quick-menu]');await menu.waitFor();
 await actionCase('Stale controllable Steam receipt cannot revive retired game-menu toggle',height,async()=>{
  assert.equal(await menu.getByRole('button',{name:/Steam鼠标|模拟鼠标/}).count(),0);
  const desktop=menu.getByRole('button',{name:'返回桌面',exact:true});await desktop.waitFor();assert.equal(await desktop.isEnabled(),true);
  assert.equal(await desktop.getAttribute('data-gp-col'),'1');
  assert.equal(await f.evaluate(()=>window.__previewCalls.filter(c=>['mouseMode.toggle','mouseMode.set','mouseMode.setPolicy'].includes(c.cmd)).length),0);
 });
}
