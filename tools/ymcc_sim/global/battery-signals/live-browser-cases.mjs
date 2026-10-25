import assert from 'node:assert/strict';
export async function runLiveBatteryCases({browser,url,bridge}){
 const cases=[],errors=[];let page;
 const check=async(name,fn)=>{await fn();cases.push({name,status:'PASS',passed:true});};
 const setup=async(route)=>{
  await page?.close();await bridge.request('fixture.reset',{route});
  page=await browser.newPage({viewport:{width:1000,height:720}});page.on('pageerror',error=>errors.push(String(error)));await page.goto(url+'?actual-controller=1');
  await page.getByRole('button',{name:'充电上限档位',exact:true}).waitFor();
  await page.waitForFunction(()=>!document.querySelector('[data-testid="battery-charge-settings"] .dd-trigger')?.disabled);
 };
 const choose=async label=>{await page.getByRole('button',{name:'充电上限档位',exact:true}).click();await page.getByRole('option',{name:label,exact:true}).click();};
 const text=async value=>page.waitForFunction(value=>document.querySelector('.charge-status')?.textContent?.includes(value),value);
 const apply=async()=>{await page.getByRole('button',{name:'应用',exact:true}).click();await page.waitForFunction(()=>!document.querySelector('[data-testid="battery-charge-settings"] .charge-controls > button')?.disabled);};
 try{
  await setup('lenovo-go-conservation');await text('当前：100%');
  await check('live component selection does not write; explicit Apply reaches real Controller',async()=>{
   await choose('80%');assert.equal((await bridge.request('fixture.stats')).writes,0);await apply();await text('当前：80%');assert.equal((await bridge.request('fixture.stats')).writes,1);
  });
  await check('real stale fence refuses the write through the production bridge',async()=>{
   await choose('100%');await bridge.request('fixture.set',{raw:0});const writes=(await bridge.request('fixture.stats')).writes;
   await apply();assert.equal((await bridge.request('fixture.stats')).writes,writes);assert(bridge.trace.some(row=>row.command==='battery.charge.set'&&row.reply?.ok===false&&row.reply?.reason?.includes('充电状态已更新')));
  });
  await check('real no-battery gate retains the unsupported card without provider writes',async()=>{
   const writes=(await bridge.request('fixture.stats')).writes;await bridge.request('fixture.set',{present:false});await page.evaluate(()=>window.dispatchEvent(new Event('focus')));
   await text('不支持（未检测到内置电池）');assert.equal(await page.getByTestId('battery-charge-settings').count(),1);assert.equal((await bridge.request('fixture.stats')).writes,writes);
  });
  await setup('lenovo-go-conservation');await text('当前：100%');
  await check('lost acknowledgement remains unconfirmed until explicit recovery; no automatic replay',async()=>{
   await choose('80%');await bridge.request('fixture.set',{throwAfterWrite:true});await apply();await text('当前设置未确认');
   assert.equal((await bridge.request('fixture.stats')).writes,1);await bridge.request('fixture.set',{throwAfterWrite:false});await page.evaluate(()=>window.dispatchEvent(new Event('focus')));await text('当前设置未确认');assert.equal((await bridge.request('fixture.stats')).writes,1);
   await apply();await text('当前：80%');assert.equal((await bridge.request('fixture.stats')).writes,1);assert(bridge.trace.some(row=>row.command==='battery.charge.refresh'));
  });
  await setup('asus-ally-acpi');
  await check('real ASUS Controller ACK never becomes invented threshold readback',async()=>{
   await choose('80%');await apply();await text('当前设置：80%（已接受）');const reply=bridge.trace.filter(row=>row.command==='battery.charge.set').at(-1).reply;assert.equal(reply.currentLimit,null);assert.equal(reply.verification,'acknowledged');
  });
  await setup('ayaneo-cec-bypass');await bridge.request('fixture.set',{percent:90});
  await check('real software ownership survives logical suspend/resume and explicit release',async()=>{
   await choose('80%');await apply();await text('当前目标：80%');assert.equal((await bridge.request('fixture.stats')).raw,0x55);
   await bridge.request('fixture.suspend');assert.equal((await bridge.request('fixture.stats')).raw,0xaa);await bridge.request('fixture.resume');assert.equal((await bridge.request('fixture.stats')).raw,0x55);const writes=(await bridge.request('fixture.stats')).writes;await bridge.request('fixture.restore');assert.equal((await bridge.request('fixture.stats')).writes,writes);
   await choose('100%');await apply();await text('当前：100%');assert.equal((await bridge.request('fixture.stats')).raw,0xaa);assert(bridge.trace.some(row=>row.command==='battery.charge.release'&&row.reply?.managed===false));
  });
  assert.deepEqual(errors,[]);return {status:'PASS',cases,errors,scope:'real component -> production bridge -> test stdio transport -> real Controller; injected provider/power/storage; no native pipe or hardware'};
 }finally{await page?.close();}
}