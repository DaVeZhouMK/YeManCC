// Execute the real SettingsView -> BatteryChargeSettings -> batteryCharge bridge.
// The only injected part is the explicit charging transport in memoryIpc.ts.
import assert from 'node:assert/strict';
export async function batterySettingsCases({f,height,press,state,actionCase}) {
 await actionCase('Settings charging entry consumes get/set without implicit writes',height,async()=>{
  await press(f,f.locator('[data-settings-section="appearance"]'));
  const before=await f.evaluate(()=>window.__batteryChargeFixture.calls.filter(c=>c.cmd==='battery.charge.set').length);
  await press(f,f.locator('[data-settings-section="device"]'));
  // This assertion deliberately fails if the parent drops its actual consumer again.
  assert.equal(await f.locator('[data-testid="battery-charge-settings"]').count(),1,'SETTINGS_CHARGE_ENTRY_MISSING');
  const card=f.locator('[data-testid="battery-charge-settings"]');await card.waitFor();
  await f.waitForFunction(()=>window.__batteryChargeFixture.calls.some(c=>c.cmd==='battery.charge.get'));
  assert.equal(await f.evaluate(()=>window.__batteryChargeFixture.calls.filter(c=>c.cmd==='battery.charge.set').length),before,'opening settings must not write');
  await state(f,'settings/battery-charge',height);
  await press(f,card.getByRole('button',{name:'充电上限档位',exact:true}));
  await press(f,f.getByRole('option',{name:'80%',exact:true}));
  assert.equal(await f.evaluate(()=>window.__batteryChargeFixture.calls.filter(c=>c.cmd==='battery.charge.set').length),before,'selecting a target must not write');
  await press(f,card.getByRole('button',{name:'应用',exact:true}));
  await f.waitForFunction(()=>window.__batteryChargeFixture.state.currentLimit===80);
  const result=await f.evaluate(()=>({state:window.__batteryChargeFixture.state,sets:window.__batteryChargeFixture.calls.filter(c=>c.cmd==='battery.charge.set')}));
  assert.equal(result.sets.length,before+1);assert.equal(result.sets.at(-1).args.target,80);
  assert.match(result.sets.at(-1).args.fence,/^[a-f0-9]{64}$/);assert.match(await card.getByRole('status').innerText(),/80/);
  await press(f,f.locator('[data-settings-section="appearance"]'));
  assert.equal(await card.count(),0,'inactive tab unmounts polling consumer');
  const reads=await f.evaluate(()=>window.__batteryChargeFixture.calls.length);await f.waitForTimeout(50);
  assert.equal(await f.evaluate(()=>window.__batteryChargeFixture.calls.length),reads,'inactive tab must not poll');
  await press(f,f.locator('[data-settings-section="device"]'));await card.waitFor();
  assert.match(await card.getByRole('status').innerText(),/80/,'reentry queries the saved device value');
  assert.equal(await f.evaluate(()=>window.__batteryChargeFixture.calls.filter(c=>c.cmd==='battery.charge.set').length),before+1,'reentry must not replay a write');
 });
}
