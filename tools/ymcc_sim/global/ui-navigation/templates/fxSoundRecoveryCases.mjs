import assert from 'node:assert/strict';
export async function fxSoundRecoveryCases({f,height,press,state,actionCase,command}) {
 await actionCase('FxSound repair setting saves, survives reentry and works with gamepad A/B',height,async()=>{
  await press(f,f.locator('[data-settings-section="device"]'));
  assert.equal(await f.locator('.settings-section-panel:not([hidden]) [data-fxsound-repair-toggle]').count(),0,'FxSound repair is not in device settings');
  assert.equal((await f.locator('[data-settings-section="diagnostics"]').innerText()).trim(),'日志修复','diagnostics category renamed');
  await press(f,f.locator('[data-settings-section="diagnostics"]'));
  const toggle=f.getByRole('switch',{name:'FxSound 自动修复',exact:true});
  assert.equal(await toggle.count(),1,'FXSOUND_REPAIR_SETTING_MISSING');
  assert.equal(await toggle.getAttribute('aria-checked'),'true','new setting defaults on');
  await state(f,'settings/fxsound-repair',height);
  await press(f,toggle);
  await f.waitForFunction(()=>document.querySelector('[data-fxsound-repair-toggle] [role="switch"]')?.getAttribute('aria-checked')==='false');
  await command(f,'back');
  assert.ok(await f.locator('[data-settings-section="diagnostics"]').evaluate(el=>document.activeElement===el),'B returns from repair toggle to log/repair category');
  await press(f,f.locator('[data-settings-section="appearance"]'));
  await press(f,f.locator('[data-settings-section="diagnostics"]'));
  assert.equal(await toggle.getAttribute('aria-checked'),'false','saved OFF survives reentry');
  await press(f,toggle);
  await f.waitForFunction(()=>document.querySelector('[data-fxsound-repair-toggle] [role="switch"]')?.getAttribute('aria-checked')==='true');
 });
}
