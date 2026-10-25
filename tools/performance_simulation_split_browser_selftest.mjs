import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createRequire} from 'node:module';
import {createPerformancePreviewServer} from './lib/continuousPerformancePreview.mjs';
const {chromium}=createRequire('C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/_continuous_ui.cjs')('playwright');
const out=process.argv[2]||JSON.parse(fs.readFileSync('G:/YeManCC-Work/Build/Tasks/ContinuousPerformance-20261017/SPLIT-CURRENT.json')).taskRoot;
const report={pass:false,layouts:[],cases:[],errors:[]};
const server=await createPerformancePreviewServer();let browser;
try {
 browser=await chromium.launch({channel:'msedge',headless:true});const page=await browser.newPage({viewport:{width:1280,height:720}});page.setDefaultTimeout(7000);page.on('pageerror',e=>report.errors.push(e.message));
 await page.goto('http://127.0.0.1:'+server.address().port+'/?simulation=1');await page.waitForSelector('.simulation-editor .limits-bubble');
 const frame=await(await page.locator('.performance-simulation-window iframe').elementHandle()).contentFrame();await frame.waitForFunction(()=>ymccEnergyDemo.viewState().sourceReady);
 const dcBefore=await page.evaluate(()=>JSON.stringify(__fixture.controls.dc));assert.equal(await page.evaluate(()=>__fixture.writes.length),0);
 for(const [width,height] of [[1280,720],[1280,800],[1160,720],[1160,800]]){
  await page.setViewportSize({width,height});await frame.evaluate(()=>ymccEnergyDemo.advance(12));
  const layout=await page.locator('.simulation-editor').evaluate(el=>{const box=el.getBoundingClientRect(),children=[...el.querySelectorAll('input,button')].filter(n=>n.getBoundingClientRect().width>0).map(n=>{const r=n.getBoundingClientRect();return{id:n.getAttribute('aria-label')||n.textContent.trim(),x:r.x,y:r.y,w:r.width,h:r.height}});return{overflow:el.scrollHeight-el.clientHeight,width:box.width,bottom:box.bottom,children}});
  assert(layout.overflow<=0,JSON.stringify(layout));assert(layout.children.every(r=>r.x>=0&&r.x+r.w<=width&&r.y>=0&&r.y+r.h<=height));
  const left=await frame.evaluate(()=>({scroll:[document.documentElement.scrollWidth-innerWidth,document.documentElement.scrollHeight-innerHeight],boxes:[...document.querySelectorAll('.preview-panel,.comparison,.charts')].map(el=>el.getBoundingClientRect().toJSON()),table:[...document.querySelectorAll('.comparison .strategy')].map(el=>({width:el.clientWidth,scroll:el.scrollWidth})),outside:[...document.querySelectorAll('.comparison b')].filter(el=>{const r=el.getBoundingClientRect(),p=el.parentElement.getBoundingClientRect();return r.width>p.width+1}).map(el=>el.id)}));
  assert.deepEqual(left.scroll,[0,0]);assert(left.boxes.every(r=>r.x>=0&&r.right<=width-432&&r.bottom<=height));assert(left.table.every(r=>r.scroll<=r.width));assert.deepEqual(left.outside,[]);
  const align=await page.locator('.simulation-editor .slider-dropdown-row').evaluateAll(rows=>rows.map(row=>{const a=row.querySelector('input').getBoundingClientRect(),b=row.querySelector('button').getBoundingClientRect();return Math.abs(a.y+a.height/2-b.y-b.height/2)}));assert(align.every(n=>n<1));
  report.layouts.push({width,height,editorOverflow:layout.overflow,alignment:align,left:left.boxes});await page.screenshot({path:path.join(out,`verified-${width}x${height}.png`)});
 }
 report.cases.push('720P/800P at1280px and native1160px: original35px controls; aligned sliders/dropdowns; complete table/charts; no page scrolling or clipping');
 await page.setViewportSize({width:1280,height:720});
 const bias=page.locator('.simulation-editor .bias-slider input');const readBias=()=>frame.evaluate(()=>ymccEnergyDemo.viewState().sourcePolicy.bias);
 await bias.focus();const before=await readBias();await page.evaluate(()=>__command('nav-right'));assert.equal(await readBias(),before);
 await bias.focus();await page.evaluate(()=>__command('confirm'));await page.evaluate(()=>__command('nav-right'));await frame.waitForFunction(value=>ymccEnergyDemo.viewState().sourcePolicy.bias===value,before+5);await page.evaluate(()=>__command('back'));assert.equal(await page.locator('.performance-simulation-window').count(),1);assert.equal(await bias.getAttribute('data-gp-editing'),null);
 const tdp=page.locator('.simulation-editor .slider-dropdown-row input').first();await tdp.fill('25');await tdp.dispatchEvent('change');await frame.waitForFunction(()=>YmccScheduleLink.snapshot().auto.tdp.ac===25);assert.equal(await page.evaluate(()=>__fixture.controls.ac.tdp),55);assert.equal(await page.evaluate(()=>__fixture.writes.length),0);
 const ceiling=page.locator('.simulation-editor .slider-dropdown-row .dd-trigger').first();await ceiling.focus();await page.evaluate(()=>__command('confirm'));await page.waitForSelector('.dd-menu');await page.evaluate(()=>__command('back'));await page.waitForSelector('.dd-menu',{state:'detached'});assert.equal(await page.locator('.performance-simulation-window').count(),1);
 // Every declared right-hand row must match actual horizontal placement and be reachable.
 for(const mode of ['auto','manual']){
  await page.locator('.simulation-editor .control-modes button').nth(mode==='auto'?0:1).click();
  await page.waitForSelector(mode==='auto'?'.simulation-editor .bias-slider':'.simulation-editor .manual-sliders');
  const rows=await page.locator('.simulation-editor').evaluate(el=>{
   const map=new Map();for(const n of el.querySelectorAll('[data-gp-row][data-gp-col]')){if(n.disabled||!n.getBoundingClientRect().width)continue;const row=n.dataset.gpRow;if(!map.has(row))map.set(row,[]);const r=n.getBoundingClientRect();map.get(row).push({row:+row,col:+n.dataset.gpCol,x:r.x,y:r.y,w:r.width,h:r.height});}
   return [...map.values()].map(row=>row.sort((a,b)=>a.col-b.col));
  });
  assert(rows.every(row=>row.every(n=>Math.abs(n.y+n.h/2-row[0].y-row[0].h/2)<1)),JSON.stringify(rows));
  for(let index=0;index<rows.length;index++){
   const row=rows[index],first=page.locator(`.simulation-editor [data-gp-row="${row[0].row}"][data-gp-col="${row[0].col}"]`);
   await first.focus();
   for(let col=1;col<row.length;col++){await page.evaluate(()=>__command('nav-right'));assert.equal(await page.evaluate(()=>+document.activeElement.dataset.gpCol),row[col].col);assert.equal(await page.evaluate(()=>+document.activeElement.dataset.gpRow),row[col].row);}
   await first.focus();
   if(index+1<rows.length){await page.evaluate(()=>__command('nav-down'));assert.equal(await page.evaluate(()=>+document.activeElement.dataset.gpRow),rows[index+1][0].row);}
   await first.focus();
   if(index>0){await page.evaluate(()=>__command('nav-up'));assert.equal(await page.evaluate(()=>+document.activeElement.dataset.gpRow),rows[index-1][0].row);}
  }
 }
 await page.locator('.simulation-editor .control-modes button').first().click();
 report.cases.push('All visible automatic/manual control rows match physical alignment; every horizontal neighbor and adjacent vertical row is reachable without editing values');
 report.cases.push('Production sliders arm on A, change by5%, B exits editing/menu first; edits update only the shared simulation draft');
 await frame.locator('[data-scene=e]').focus();await frame.evaluate(()=>ymccEnergyDemo.press('right'));await page.waitForFunction(()=>document.querySelector('.simulation-editor').contains(document.activeElement));
 await page.locator('.simulation-editor .control-modes button').first().focus();await page.evaluate(()=>__command('nav-left'));assert.equal(await page.evaluate(()=>document.activeElement.tagName),'IFRAME');
 await frame.locator('#manual-card').click();await page.waitForSelector('.simulation-editor .manual-sliders');const manualInputs=page.locator('.simulation-editor .manual-sliders input');await manualInputs.first().fill('3000');await manualInputs.first().dispatchEvent('change');await frame.waitForFunction(()=>ymccEnergyDemo.viewState().sourcePolicy.manual.frequencyMHz===3000);
 await page.locator('.simulation-editor .manual-actions-grid button').nth(1).click();await frame.waitForFunction(()=>ymccEnergyDemo.viewState().sourcePolicy.manual.turbo===false);
 await frame.evaluate(()=>ymccEnergyDemo.advance(10));assert.equal(await frame.locator('#manual-summary-low').textContent(),await frame.locator('#manual-low').textContent());assert.equal(await page.evaluate(()=>__fixture.writes.length),0);
 report.cases.push('Left/right focus crosses the split; selecting a strategy selects the same real mode editor; manual frequency/turbo and1%Low share the original draft/data');
 await frame.locator('#plan-open').click();await frame.waitForSelector('#scenario-plan[open]');await page.waitForFunction(()=>getComputedStyle(document.querySelector('.simulation-editor')).display==='none');await frame.evaluate(()=>ymccEnergyDemo.press('b'));await frame.waitForSelector('#scenario-plan[open]',{state:'detached'});await page.waitForSelector('.simulation-editor',{state:'visible'});
 await frame.locator('#session-save').click();await frame.waitForSelector('#session-save-dialog[open]');await page.waitForFunction(()=>getComputedStyle(document.querySelector('.simulation-editor')).display==='none');await frame.locator('#session-save-confirm').click();await frame.waitForFunction(()=>document.getElementById('session-save').textContent==='配置已保存');assert.equal(await page.evaluate(()=>__fixture.controls.ac.tdp),25);assert.equal(await page.evaluate(()=>__fixture.controls.ac.manual.frequencyMHz),3000);assert.equal(await page.evaluate(()=>__fixture.controls.ac.manual.turbo),false);assert.equal(await page.evaluate(()=>JSON.stringify(__fixture.controls.dc)),dcBefore);
 report.cases.push('Quick simulation and save dialogs cover the whole window; explicit Save alone writes the selected formal bank and preserves the other bank');
 await page.locator('.simulation-editor .control-modes button').first().click();await frame.waitForFunction(()=>document.getElementById('session-save').textContent==='保存当前设置');await page.evaluate(()=>__command('back'));await page.waitForSelector('.performance-simulation-window',{state:'detached'});
 assert.deepEqual(report.errors,[]);report.pass=true;
} catch(error) { report.failure=error.stack;throw error; } finally { await browser?.close();await new Promise(resolve=>server.close(resolve));fs.writeFileSync(path.join(out,'split-verification.json'),JSON.stringify(report,null,2)); }
console.log(JSON.stringify(report,null,2));
