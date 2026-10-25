import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
// All input below goes to owned headless browser documents, never Windows input.
export async function radialTouchCases({browser,url,height,task}){
 const context=await browser.newContext({viewport:{width:1280,height},hasTouch:true}),page=await context.newPage();
 const report={pass:false,height,hardwareWrites:0,physicalAcceptance:false,editorTargets:[],trustedTouchEvents:[],actions:[]};
 try{
  const target=new URL('/product.html',url);target.search=new URLSearchParams({route:'/radial',height:String(height),nativeWidth:'1',theme:'blue-black'}).toString();await page.goto(target.href);
  await page.waitForFunction(()=>window.__previewReady&&window.__gamepadAudit);
  await page.evaluate(()=>{window.__touchTrace=[];window.__touchClicks=[];document.addEventListener('click',e=>window.__touchClicks.push({target:e.target.tagName,label:e.target.textContent?.trim().slice(0,40),trusted:e.isTrusted}),true);for(const type of ['pointerdown','pointermove','pointerup','pointercancel'])document.addEventListener(type,event=>{if(event.pointerType==='touch')window.__touchTrace.push({type,pointerType:event.pointerType,trusted:event.isTrusted,target:event.target.tagName,label:event.target.textContent?.trim().slice(0,35)});},true);});
  await page.evaluate(()=>window.__navigate('/settings'));await page.locator('[data-settings-section="diagnostics"]').tap();
  const testing=page.locator('[data-radial-testing-toggle] button');
  assert.equal(await testing.getAttribute('aria-checked'),'true');
  await testing.tap();await page.locator('.app-body > .navrail .nav-item').filter({hasText:'触摸轮盘'}).waitFor({state:'detached'});
  await page.evaluate(()=>window.__navigate('/radial'));await page.locator('.route-view[data-route="/schedule"]').waitFor();
  await page.evaluate(()=>window.__navigate('/settings'));await page.locator('[data-settings-section="diagnostics"]').tap();
  await testing.tap();await page.locator('.app-body > .navrail .nav-item').filter({hasText:'触摸轮盘'}).waitFor();
  await page.locator('.app-body > .navrail .nav-item').filter({hasText:'触摸轮盘'}).tap();await page.locator('.radial-settings-page').waitFor();
  assert.deepEqual(await page.locator('.radial-settings-page button[aria-pressed]').evaluateAll(els=>els.map(el=>el.getAttribute('aria-pressed'))),['false','false','false','false']);
  report.actions.push('trusted touch toggles diagnostic testing off/on; hidden route refused; summon channels remain off');
  await page.locator('[data-radial-edit]').tap();await page.locator('.radial-editor-window').waitFor();
  await page.locator('[data-radial-select="windows"]').tap();assert.equal(await page.locator('.wheel-child').count(),3);
  await page.locator('[data-radial-item="thisPc"]').tap();assert.equal(await page.getByRole('textbox',{name:'显示名称',exact:true}).inputValue(),'我的电脑');report.actions.push('touch selects extended child and its actual property panel');
  const controls=page.locator('.radial-editor-window button:enabled,.radial-editor-window input:enabled');
  for(let i=0;i<await controls.count();i++){
   const control=controls.nth(i);if(!await control.isVisible())continue;await control.scrollIntoViewIfNeeded();
   const hit=await control.evaluate(el=>{const r=el.getBoundingClientRect(),x=(r.left+r.right)/2,y=(r.top+r.bottom)/2,target=document.elementFromPoint(x,y);return {label:el.getAttribute('aria-label')||el.textContent.trim(),width:r.width,height:r.height,inView:x>=0&&x<innerWidth&&y>=0&&y<innerHeight,hit:target===el||el.contains(target)};});
   assert.ok(hit.inView&&hit.hit,JSON.stringify(hit));assert.ok(hit.width>=32&&hit.height>=32,JSON.stringify(hit));report.editorTargets.push(hit);
  }
  await page.locator('[data-radial-record]').tap();await page.locator('.editor-overlay').waitFor();
  await page.locator('.editor-overlay [data-editor-action="close"]').tap();await page.locator('.editor-overlay').waitFor({state:'detached'});
  await page.waitForFunction(()=>window.__editorWidthFixture.owner.startsWith('radial-editor-'));assert.ok((await page.evaluate(()=>window.__editorWidthFixture.events)).every(e=>e.ok));report.actions.push('touch opens and closes existing recorder, restoring enlarged editor');
  await page.locator('.radial-editor-window [data-editor-action="close"]').tap();await page.locator('.radial-editor-window').waitFor({state:'detached'});
  const cdp=await context.newCDPSession(page);
  const fresh=async()=>{await page.evaluate(()=>{window.__radialUsage?.unmount();window.__mountRadialWheel();});return page.locator('.wheel-geometry').boundingBox();};
  const point=(box,x,y)=>({x:box.x+x*box.width/496,y:box.y+y*box.height/496});
  const tap=async(box,x,y)=>{const p=point(box,x,y);await page.touchscreen.tap(p.x,p.y);};
  const contact=async(type,box,x,y)=>{const p=point(box,x,y);await cdp.send('Input.dispatchTouchEvent',{type,touchPoints:type==='touchEnd'||type==='touchCancel'?[]:[{x:p.x,y:p.y,id:1,radiusX:6,radiusY:6,force:1}]});await page.waitForTimeout(16);};
  let box=await fresh();await tap(box,358,248);assert.ok(await page.evaluate(()=>window.__radialUsage.events.some(e=>e.id==='ymcc')));report.actions.push('trusted touch executes primary YMCC button');
  box=await fresh();await contact('touchStart',box,325.8,325.8);await page.locator('.wheel-child').first().waitFor();
  for(let i=1;i<=8;i++){await contact('touchMove',box,325.8+(437-325.8)*i/8,325.8+(287-325.8)*i/8);}
  await contact('touchEnd',box,437,287);assert.ok(await page.evaluate(()=>window.__radialUsage.events.some(e=>e.id==='thisPc')));assert.equal(await page.locator('.wheel-sector').count(),11);report.actions.push('continuous trusted touch extends parent, crosses gap and executes child with main ring retained');
  box=await fresh();await tap(box,325.8,325.8);await page.locator('.wheel-child').first().waitFor();
  const back=page.locator('[data-radial-footer="0"]');assert.equal(await back.getAttribute('aria-label'),'返回上级');
  await page.locator('[data-radial-wheel]').screenshot({path:path.join(task,'evidence','radial-touch-submenu-'+height+'.png')});
  await contact('touchStart',box,122,528);await contact('touchCancel',box,122,528);assert.equal(await page.locator('.wheel-child').count(),3);assert.equal(await page.evaluate(()=>window.__radialUsage.events.length),0);
  await back.tap();assert.equal(await page.locator('.wheel-child').count(),0);assert.equal(await page.locator('[data-radial-wheel]').getAttribute('data-radial-level'),'primary');assert.equal(await page.evaluate(()=>window.__radialUsage.events.length),0);
  assert.equal(await back.getAttribute('aria-label'),'返回监控');await back.tap();assert.deepEqual(await page.evaluate(()=>window.__radialUsage.events),[{close:true}]);report.actions.push('trusted touch Back returns from submenu to primary first; another Back closes to monitor');
  box=await fresh();await tap(box,325.8,325.8);await page.locator('.wheel-child').first().waitFor();
  await contact('touchStart',box,437,287);await contact('touchMove',box,390,275);await contact('touchMove',box,350,270);await contact('touchEnd',box,350,270);
  assert.equal(await page.locator('.wheel-child').count(),0);assert.equal(await page.locator('[data-radial-wheel]').getAttribute('data-radial-level'),'primary');assert.equal(await page.evaluate(()=>window.__radialUsage.events.length),0);report.actions.push('trusted touch slides inward from child to return to primary without executing or closing');
  box=await fresh();await contact('touchStart',box,358,248);await contact('touchCancel',box,358,248);await tap(box,248,248);assert.equal(await page.evaluate(()=>window.__radialUsage.events.length),0);report.actions.push('touch cancellation and hollow centre never execute an action');
  box=await fresh();await tap(box,248,138);await page.locator('[data-radial-dial-thumb]').waitFor();await contact('touchStart',box,248,126);await contact('touchMove',box,304,126);await contact('touchEnd',box,304,126);
  assert.equal(await page.evaluate(()=>window.__radialUsage.events.length),0);const draft=Number((await page.locator('.dial-value').textContent()).replace(/\D/g,''));assert.ok(draft>15&&draft<=30);
  await contact('touchStart',box,248,528);await contact('touchMove',box,294,528);await contact('touchEnd',box,294,528);assert.equal(await page.evaluate(()=>window.__radialUsage.events.length),0);
  await contact('touchStart',box,248,528);await contact('touchCancel',box,248,528);assert.equal(await page.evaluate(()=>window.__radialUsage.events.length),0);
  await page.locator('.wheel-actions button').nth(2).tap();assert.equal(Number((await page.locator('.dial-value').textContent()).replace(/\D/g,'')),draft+1);
  await page.locator('.wheel-actions button').nth(1).tap();await page.waitForTimeout(350);assert.ok(await page.evaluate(()=>window.__radialUsage.events.some(e=>e.id==='tdp'&&e.value>15)));assert.equal(await page.evaluate(()=>window.__radialUsage.events.filter(e=>e.id==='tdp').length),1);report.actions.push('touch footer drag/cancel never confirm; one plus tap adjusts once and one Apply tap commits once');
  box=await fresh();await tap(box,248,138);await page.locator('[data-radial-dial-thumb]').waitFor();await page.locator('.wheel-actions button').nth(0).tap();await page.locator('[data-radial-dial-thumb]').waitFor({state:'detached'});assert.equal(await page.evaluate(()=>window.__radialUsage.events.length),0);report.actions.push('touch footer cancel leaves no submitted TDP action');
  report.trustedTouchEvents=await page.evaluate(()=>window.__touchTrace);assert.ok(report.trustedTouchEvents.length>=20);assert.ok(report.trustedTouchEvents.every(e=>e.trusted&&e.pointerType==='touch'));
  await page.screenshot({path:path.join(task,'evidence',`radial-touch-${height}.png`)});await page.evaluate(()=>window.__radialUsage.unmount());
  await page.evaluate(()=>{window.__mountRadialOutput();window.__radialTransition.frame();});
  await page.locator('.radial-output.open').waitFor();
  const frames=await page.locator('.radial-output .radial-wheel').evaluate(el=>{
   const animation=el.getAnimations().find(a=>a.animationName==='wheel-open');if(!animation)throw Error('Missing actual enter animation');
   animation.pause();const samples=[];for(const time of [0,120,240]){animation.currentTime=time;const css=getComputedStyle(el),matrix=new DOMMatrix(css.transform);samples.push({time,opacity:Number(css.opacity),scale:matrix.a,x:matrix.e,y:matrix.f});}animation.finish();return samples;
  });
  assert.ok(Math.abs(frames[0].scale-.92)<.001);assert.ok(frames.every(frame=>Math.abs(frame.x)<.001&&Math.abs(frame.y)<.001),'Wheel must unfold in place without travelling from monitor coordinates');
  assert.ok(frames[1].scale>frames[0].scale&&frames[1].scale<frames[2].scale);assert.equal(frames[0].opacity,0);assert.equal(frames[2].opacity,1);assert.ok(Math.abs(frames[2].scale-1)<.001);
  await page.locator('.radial-output .wheel-actions button').first().tap();
  await page.locator('.radial-output.closing').waitFor();await page.evaluate(()=>window.__radialTransition.frame());
  assert.ok(await page.locator('.radial-output').evaluate(el=>el.classList.contains('closing')||el.classList.contains('closed')));
  await page.locator('.radial-output.closed').waitFor({state:'attached'});assert.equal(await page.locator('.radial-output').isVisible(),false);report.transitionFrames=frames;report.actions.push('actual output unfolds in place without lateral travel; stale frames cannot reopen exit; exit completes');
  await page.evaluate(()=>window.__radialTransition.unmount());
  await page.evaluate(()=>{window.__mountRadialOutput();window.__radialTransition.frame();});await page.locator('.radial-output.open').waitFor();
  const interruptedBefore=await page.locator('.radial-output .radial-wheel').evaluate(el=>{const animation=el.getAnimations().find(a=>a.animationName==='wheel-open');animation.pause();animation.currentTime=70;const css=getComputedStyle(el);return {scale:new DOMMatrix(css.transform).a,opacity:Number(css.opacity)};});
  await page.locator('.radial-output .wheel-actions button').first().tap();
  const interruptedAfter=await page.locator('.radial-output .radial-wheel').evaluate(el=>{const animation=el.getAnimations().find(a=>a.animationName==='wheel-close');if(!animation)throw Error('Missing interrupted exit');animation.pause();animation.currentTime=0;const css=getComputedStyle(el),value={scale:new DOMMatrix(css.transform).a,opacity:Number(css.opacity)};animation.finish();return value;});
  assert.ok(Math.abs(interruptedBefore.scale-interruptedAfter.scale)<.002);assert.ok(Math.abs(interruptedBefore.opacity-interruptedAfter.opacity)<.002);
  await page.locator('.radial-output.closed').waitFor({state:'attached'});report.interruptedExit={before:interruptedBefore,after:interruptedAfter};report.actions.push('interrupted entry exits from the currently rendered size and opacity without snapping');await page.evaluate(()=>window.__radialTransition.unmount());
  await page.emulateMedia({reducedMotion:'reduce'});await page.evaluate(()=>window.__mountRadialOutput());
  await page.locator('.radial-output.open').waitFor();await page.locator('.radial-output .wheel-actions button').first().tap();await page.locator('.radial-output.closed').waitFor({state:'attached'});assert.equal(await page.locator('.radial-output').isVisible(),false);
  await page.evaluate(()=>window.__radialTransition.unmount());await page.emulateMedia({reducedMotion:'no-preference'});report.actions.push('actual output supports reduced motion and still completes exit');
  assert.equal((await page.evaluate(()=>window.__previewErrors)).length,0);report.pass=true;
 }catch(error){report.error=error.message;report.debug=await page.evaluate(()=>({trace:window.__touchTrace,clicks:window.__touchClicks,protocolClicked:window.__protocolClicked,actions:window.__radialUsage?.events,buttons:[...document.querySelectorAll('.wheel-actions button')].map(el=>({text:el.textContent,disabled:el.disabled,rect:el.getBoundingClientRect().toJSON()})),errors:window.__previewErrors})).catch(()=>null);await page.screenshot({path:path.join(task,'evidence',`radial-touch-failure-${height}.png`)});throw error;}finally{fs.writeFileSync(path.join(task,`radial-touch-${height}.json`),JSON.stringify(report,null,2));await context.close();}
 return report;
}
