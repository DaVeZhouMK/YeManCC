import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createRequire} from 'node:module';
import {createPerformancePreviewServer} from './lib/continuousPerformancePreview.mjs';
const {chromium}=createRequire('C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/_continuous_ui.cjs')('playwright');
const out=process.argv[2]||'G:/YeManCC-Work/Build/Validation/live-ts';fs.mkdirSync(out,{recursive:true});
const report={pass:false,cases:[],layouts:[],errors:[]},server=await createPerformancePreviewServer();let browser;
try{
 browser=await chromium.launch({channel:'msedge',headless:true});const page=await browser.newPage({viewport:{width:1280,height:720}});page.on('pageerror',e=>report.errors.push(e.stack));
 await page.addInitScript(()=>localStorage.setItem('ymcc-gpu-capability-curves-v1',JSON.stringify({schema:1,kind:'ymcc-gpu-capability-overrides',curves:{z2e:[[3,999],[45,99999]]}})));
 await page.goto('http://127.0.0.1:'+server.address().port+'/?simulation=1');
 const frame=await(await page.locator('.performance-simulation-window iframe').elementHandle()).contentFrame();
 await frame.waitForFunction(()=>document.documentElement.dataset.editorLayout==='measured'&&ymccEnergyDemo.viewState().sourceReady);await frame.evaluate(()=>{ymccEnergyDemo.pause();ymccEnergyDemo.advance(4);});
 assert.equal(await frame.evaluate(()=>YmccBenchmarkData.stockScores('z2e',30).gpu),3576);assert.equal(await frame.evaluate(()=>YmccBenchmarkData.GPU_CAPABILITIES.setCurve),undefined);
 assert.equal(await frame.locator('#gpu-capability-editor').count(),0);assert.equal(await page.locator('.curve-toggle').count(),0);assert.equal(await frame.locator('.gpu-ts-chart input,.gpu-ts-chart button').count(),0);
 report.cases.push('Read-only capability anchors remain intact; the live score chart has no editor, saved curve import or capability override');
 for(const [width,height]of [[1280,720],[1280,800],[1160,720],[1160,800],[1536,1024],[1920,1080]]){
  await page.setViewportSize({width,height});await page.waitForTimeout(50);
  const charts=await frame.locator('.charts .chart').evaluateAll(nodes=>nodes.map(el=>({box:el.getBoundingClientRect().toJSON(),title:el.querySelector('.chart-title').textContent})));
  assert.equal(charts.length,3);assert.match(charts[1].title,/能效/);assert.match(charts[2].title,/TS 功耗曲线/);assert(charts.every(row=>Math.abs(row.box.y-charts[0].box.y)<1));assert(charts[2].box.left>=charts[1].box.right);
  const right=await page.locator('.simulation-editor').evaluate(el=>({box:el.getBoundingClientRect().toJSON(),overflow:el.scrollHeight-el.clientHeight}));
  const left=await frame.locator('.visual-column').evaluate(el=>el.getBoundingClientRect().toJSON());
  assert(Math.abs(right.box.width-432*(height-92)/628)<.2);assert.equal(right.box.height,height-92);assert.equal(right.box.bottom,height-34);assert.equal(right.box.top,58);assert.equal(right.overflow,0);assert(Math.abs(left.top-right.box.top)<1.5);assert(Math.abs(left.bottom-right.box.bottom)<1.5);
  const labels=await frame.locator('#gpu-ts-chart').evaluate(el=>{const box=el.getBoundingClientRect();return [...el.querySelectorAll('.ts-watt-label')].map(label=>{const b=label.getBoundingClientRect();return {left:b.left-box.left,right:b.right-box.left,bottom:b.bottom-box.top,width:box.width,height:box.height};}).sort((a,b)=>a.left-b.left)});
  for(const[index,label]of labels.entries()){assert(label.left>=1&&label.right<=label.width-1&&label.bottom<=label.height,JSON.stringify({width,height,label}));if(index)assert(label.left-labels[index-1].right>=11.9);}
  await page.screenshot({path:path.join(out,`watt-ts-${width}x${height}.png`)});report.layouts.push({width,height,right:right.box,left});
 }
 report.cases.push('The right editor proportionally fills the actual available height across720P/800P and tall native windows; its width is computed first, then the left side independently scales into the remainder with no bottom void');
 const verifySeries=async()=>{
  const result=await frame.evaluate(()=>({curve:YmccTsPowerCurve.sample(ymccEnergyDemo.snapshot(),document.querySelector('.gpu-ts-chart').dataset.comparisonMode),mode:document.querySelector('.gpu-ts-chart').dataset.comparisonMode,axis:document.querySelector('.gpu-ts-chart').dataset.axis,lines:[...document.querySelectorAll('.ts-power-line')].map(el=>({key:el.dataset.series,watts:JSON.parse(el.dataset.watts),scores:JSON.parse(el.dataset.scores),d:el.getAttribute('d')}))}));
  assert.equal(result.axis,'watts');assert.equal(result.lines.length,4);assert.equal(new Set(result.lines.map(row=>row.key)).size,4);
  for(const line of result.lines){const expected=result.curve.series.find(row=>row.key===line.key);assert.deepEqual(line.watts,expected.points.map(point=>point.watts));assert.deepEqual(line.scores,expected.points.map(point=>point.score));assert.equal(line.watts[0],result.curve.minWatts);assert.equal(line.watts.at(-1),result.curve.maxWatts);assert(line.d&&!line.d.includes('NaN'));}
  assert.equal(await frame.locator('.ts-time-label,.ts-live-line').count(),0);return result;
 };
 await frame.evaluate(async()=>{ymccEnergyDemo.selectPreview('manual');await YmccScheduleLink.flush();});const initial=await verifySeries();
 await frame.evaluate(()=>ymccEnergyDemo.advance(5));const advanced=await verifySeries();assert.deepEqual(advanced.lines,initial.lines);
 await page.locator('.simulation-editor .control-modes button').nth(0).click();await frame.waitForFunction(()=>document.querySelector('.gpu-ts-chart').dataset.comparisonMode==='auto');assert.equal((await verifySeries()).mode,'auto');
 await frame.evaluate(()=>ymccEnergyDemo.selectPreview('stock'));assert.equal((await verifySeries()).mode,'auto');
 report.cases.push('Exactly four watt curves reuse the existing supply model: baseline CPU/GPU and selected optimization CPU/GPU; elapsed time alone does not move the graph, and auto/manual selection replaces the optimization pair');
 // Popup scale must follow this local full-height editor rather than the unrelated app-stage scale.
 await page.evaluate(()=>document.documentElement.style.setProperty('--ui-scale','2.7'));
 const machine=page.locator('.simulation-editor .limits-bubble .dd-trigger').first();await machine.click();
 const popup=await page.locator('.dd-menu').evaluate(el=>({font:parseFloat(getComputedStyle(el).getPropertyValue('--dd-popup-font-size')),box:el.getBoundingClientRect().toJSON()}));
 assert(Math.abs(popup.font-14*(1080-92)/628)<.1);assert(popup.box.bottom<=1080&&popup.box.right<=1920);await page.keyboard.press('Escape');await page.evaluate(()=>document.documentElement.style.removeProperty('--ui-scale'));
 report.cases.push('Teleported dropdowns follow the local editor scale and remain in bounds even when the host app publishes a different global scale');
 for(const[profile,watts,warning]of [['deck',16,false],['deck',17,true],['m780',45,false],['m780',46,true],['g3e',50,false],['m395',55,false]]){
  await frame.evaluate(({profile,watts})=>ymccEnergyDemo.configure({profile,budget:watts}),{profile,watts});
  await frame.waitForFunction(({profile,watts})=>ymccEnergyDemo.snapshot().config.profile===profile&&ymccEnergyDemo.snapshot().config.budget===watts,{profile,watts});
  await page.waitForFunction(warning=>Boolean(document.querySelector('.simulation-tdp-advisory')?.textContent.trim())===warning,warning);
  if(profile==='g3e'){assert.equal(await frame.locator('.ts-unverified-region').count(),1);assert.match(await frame.locator('#gpu-ts-readout').textContent(),/35W 待校准/);assert.equal(await frame.locator('.gpu-ts-chart').getAttribute('data-unverified-above-watts'),'35');}else assert.equal(await frame.locator('.ts-unverified-region').count(),0);
  assert.equal(await page.evaluate(()=>__fixture.writes.length),0);assert.equal(await page.locator('.simulation-editor').evaluate(el=>el.getBoundingClientRect().bottom),1080-34);
 }
 report.cases.push('Shared TDP and advisory thresholds are retained; the reserved advisory row stays at the viewport bottom and no formal writes occur');
 await page.setViewportSize({width:1280,height:720});await frame.evaluate(async()=>{ymccEnergyDemo.configure({profile:'m780',budget:20,saving:'none',manualMHz:2000,manualAggressiveness:60});await YmccScheduleLink.flush();ymccEnergyDemo.selectPreview('manual');await YmccScheduleLink.flush();ymccEnergyDemo.pause();ymccEnergyDemo.advance(8);});await page.waitForFunction(()=>!document.querySelector('.simulation-tdp-advisory')?.textContent.trim());await page.screenshot({path:path.join(out,'watt-ts-final-1280x720.png')});
 const nativePage=await browser.newPage({viewport:{width:1536,height:1024},deviceScaleFactor:1.25});await nativePage.goto('http://127.0.0.1:'+server.address().port+'/?simulation=1');const nativeFrame=await(await nativePage.locator('.performance-simulation-window iframe').elementHandle()).contentFrame();await nativeFrame.waitForFunction(()=>document.documentElement.dataset.editorLayout==='measured'&&ymccEnergyDemo.viewState().sourceReady);await nativeFrame.evaluate(()=>{ymccEnergyDemo.pause();ymccEnergyDemo.advance(4);});assert.equal(await nativePage.locator('.simulation-editor').evaluate(el=>el.getBoundingClientRect().bottom),1024-34);await nativePage.screenshot({path:path.join(out,'watt-ts-native-125pct-1920x1280.png')});await nativePage.close();
 report.cases.push('A125% display-scale reproduction of the native screenshot fills to the actual bottom, with the left side fitted after the right geometry');
 assert.deepEqual(report.errors,[]);report.pass=true;
}catch(error){report.failure=error.stack;throw error;}finally{await browser?.close();await new Promise(resolve=>server.close(resolve));fs.writeFileSync(path.join(out,'gpu-chart-browser-verification.json'),JSON.stringify(report,null,2));}
console.log(JSON.stringify({pass:report.pass,cases:report.cases,layouts:report.layouts.length,errors:report.errors},null,2));
