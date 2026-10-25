import assert from 'node:assert/strict';
import path from 'node:path';
export async function radialGestureCases({browser,url,height,actionCase,task}){
 const context=await browser.newContext({viewport:{width:1280,height},hasTouch:true}),page=await context.newPage();
 const fresh=async()=>{await page.evaluate(()=>{window.__radialUsage?.unmount();window.__mountRadialWheel();});};
 const pad=async input=>{await page.evaluate(input=>window.__radialUsage.api.gamepad(input),input);};
 const count=()=>page.evaluate(()=>window.__radialUsage.events.length);
 const direction=async(x,y,phase='down',heldMs=0)=>pad({action:'direction',x,y,phase,heldMs});
 try{
  await page.goto(new URL('/product.html?route=/radial&height='+height,url).href);await page.waitForFunction(()=>window.__previewReady&&window.__mountRadialWheel);await fresh();
  await actionCase('Direction release confirms once; source cancellation and stick centre never execute',height,async()=>{
   await direction(1,0);assert.equal(await count(),0);await direction(1,0,'release');assert.deepEqual(await page.evaluate(()=>window.__radialUsage.events),[{id:'ymcc',value:undefined}]);
   await direction(1,0,'release');assert.equal(await count(),1);
   await fresh();await pad({lx:1,ly:0});await pad({lx:0,ly:0});await pad({action:'accept'});assert.equal(await count(),0);assert.equal(await page.locator('.sector-selected').count(),0);
   await direction(1,0);await pad({action:'direction-cancel'});await direction(1,0,'release');assert.equal(await count(),0);
   await direction(1,0);await page.evaluate(()=>window.__radialUsage.api.pointer('down',248,248,8));await direction(1,0,'release');await page.evaluate(()=>window.__radialUsage.api.pointer('up',248,248,8));await direction(1,0,'release');assert.equal(await count(),0);
  });
  await actionCase('Same direction hold opens second level; inward direction returns without committing',height,async()=>{
   await fresh();await direction(1,1);assert.equal(await page.locator('.wheel-child').count(),0);
   await direction(1,1,'hold',599);assert.equal(await page.locator('.wheel-child').count(),0);
   await direction(1,1,'hold',600);assert.equal(await page.locator('.wheel-child').count(),3);assert.equal(await page.locator('[data-radial-wheel]').getAttribute('data-radial-level'),'child');assert.equal(await count(),0);
   await direction(1,1,'release');assert.equal(await count(),0);
   await direction(-1,-1);assert.equal(await page.locator('[data-radial-wheel]').getAttribute('data-radial-level'),'primary');await direction(-1,-1,'release');assert.equal(await count(),0);
   await direction(1,1);await direction(1,1,'hold',600);await direction(1,1,'release');
   await direction(0,1);await direction(0,1,'release');assert.deepEqual(await page.evaluate(()=>window.__radialUsage.events),[{id:'thisPc',value:undefined}]);
  });
  await actionCase('Analog hold extends submenu and partial inward movement restores first level',height,async()=>{
   await fresh();await pad({lx:1,ly:-1});assert.equal(await page.locator('.wheel-child').count(),0);
   await page.waitForTimeout(630);await pad({lx:1,ly:-1});assert.equal(await page.locator('.wheel-child').count(),3);
   await pad({lx:.38,ly:-.38});assert.equal(await page.locator('[data-radial-wheel]').getAttribute('data-radial-level'),'primary');assert.equal(await count(),0);
   await pad({lx:0,ly:0});await pad({action:'accept'});assert.equal(await count(),0);
  });
  await actionCase('Top numeric bubble is 4:3 with a glowing linear track and staged application',height,async()=>{
   await fresh();await direction(0,-1);await direction(0,-1,'release');
   const bubble=page.locator('[data-radial-value-bubble]');await bubble.waitFor();
   const r=await bubble.locator('.value-bubble-surface').boundingBox();assert.ok(Math.abs(r.width/r.height-4/3)<.01);
   const thumb=page.locator('[data-radial-dial-thumb]');assert.equal(await thumb.getAttribute('role'),'slider');assert.equal(await thumb.getAttribute('aria-valuenow'),'15');assert.notEqual(await thumb.evaluate(el=>getComputedStyle(el).filter),'none');
   const box=await page.locator('.wheel-geometry').boundingBox(),cdp=await context.newCDPSession(page);
   for(const [type,x] of [['touchStart',248],['touchMove',304],['touchEnd',304]])await cdp.send('Input.dispatchTouchEvent',{type,touchPoints:type==='touchEnd'?[]:[{x:box.x+x*box.width/496,y:box.y+126*box.height/496,id:1,radiusX:6,radiusY:6,force:1}]});
   const value=Number(await thumb.getAttribute('aria-valuenow'));assert.ok(value>15&&value<30);assert.equal(await count(),0);
   await direction(1,0);await direction(1,0,'release');assert.equal(Number(await thumb.getAttribute('aria-valuenow')),value+1);assert.equal(await count(),0);
   await page.locator('[data-radial-wheel]').screenshot({path:path.join(task,'evidence','radial-top-slider-'+height+'.png')});
   await pad({action:'back'});assert.equal(await count(),0);assert.equal(await bubble.count(),0);
   await direction(0,-1);await direction(0,-1,'release');await pad({action:'value-up'});await pad({action:'accept'});assert.deepEqual(await page.evaluate(()=>window.__radialUsage.events),[{id:'tdp',value:16}]);
  });
 }finally{await page.evaluate(()=>window.__radialUsage?.unmount()).catch(()=>{});await context.close();}
}
