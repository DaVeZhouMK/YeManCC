import assert from 'node:assert/strict';import fs from 'node:fs';import path from 'node:path';import {createRequire} from 'node:module';import {createPerformancePreviewServer} from './lib/continuousPerformancePreview.mjs';
const {chromium}=createRequire('C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/_continuous_ui.cjs')('playwright');
const out=process.argv[2]||JSON.parse(fs.readFileSync('G:/YeManCC-Work/Build/Tasks/ContinuousPerformance-20261017/SPLIT-CURRENT.json')).taskRoot,report={pass:false,layouts:[],cases:[],errors:[]};
const server=await createPerformancePreviewServer();let browser;
try{
 browser=await chromium.launch({channel:'msedge',headless:true});const page=await browser.newPage({viewport:{width:1280,height:720}});page.on('pageerror',e=>report.errors.push(e.message));
 for(const dedicated of [false,true]){
  await page.goto('http://127.0.0.1:'+server.address().port+'/?'+(dedicated?'dedicated=1':''));await page.waitForSelector('.continuous-performance .limits-bubble');
  for(const height of [720,800]){
   await page.setViewportSize({width:1280,height});
   for(const mode of ['auto','manual']){
    await page.locator('.continuous-performance .control-modes button').nth(mode==='auto'?0:1).click();await page.waitForSelector(mode==='auto'?'.auto-bubble':'.manual-bubble');
    const rows=await page.locator('.continuous-performance').evaluate(el=>{const map=new Map();for(const node of el.querySelectorAll('[data-gp-row][data-gp-col]')){const r=node.getBoundingClientRect();if(node.disabled||!r.width)continue;const row=node.dataset.gpRow;if(!map.has(row))map.set(row,[]);map.get(row).push({row:+row,col:+node.dataset.gpCol,y:r.y,h:r.height});}return [...map.values()].map(row=>row.sort((a,b)=>a.col-b.col));});
    assert(rows.every(row=>row.every(v=>Math.abs(v.y+v.h/2-row[0].y-row[0].h/2)<1)),JSON.stringify(rows));
    for(let i=0;i<rows.length;i++){
     const row=rows[i],first=page.locator(`.continuous-performance [data-gp-row="${row[0].row}"][data-gp-col="${row[0].col}"]`);await first.focus();
     for(let c=1;c<row.length;c++){await page.evaluate(()=>__move(1,0));assert.equal(await page.evaluate(()=>+document.activeElement.dataset.gpCol),row[c].col);assert.equal(await page.evaluate(()=>+document.activeElement.dataset.gpRow),row[c].row);}
     await first.focus();if(i+1<rows.length){await page.evaluate(()=>__move(0,1));assert.equal(await page.evaluate(()=>+document.activeElement.dataset.gpRow),rows[i+1][0].row);}
     await first.focus();if(i>0){await page.evaluate(()=>__move(0,-1));assert.equal(await page.evaluate(()=>+document.activeElement.dataset.gpRow),rows[i-1][0].row);}
    }
    const last=rows.at(-1)[0];await page.locator(`.continuous-performance [data-gp-row="${last.row}"][data-gp-col="${last.col}"]`).focus();await page.evaluate(()=>__move(0,1));
    if(dedicated){assert.equal(await page.evaluate(()=>document.activeElement.dataset.gpGameRow),'custom-input-pad');await page.evaluate(()=>__move(0,-1));assert.equal(await page.evaluate(()=>+document.activeElement.dataset.gpRow),last.row);}
    else assert.equal(await page.evaluate(()=>+document.activeElement.dataset.gpRow),last.row);
    const aligns=await page.locator('.continuous-performance .slider-dropdown-row').evaluateAll(rows=>rows.map(row=>{const a=row.querySelector('input').getBoundingClientRect(),b=row.querySelector('button').getBoundingClientRect();return Math.abs(a.y+a.height/2-b.y-b.height/2)}));assert(aligns.every(x=>x<1));assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
    report.layouts.push({scope:dedicated?'dedicated':'global',height,mode,rows:rows.map(row=>row.map(v=>[v.row,v.col])),alignment:aligns});
   }
  }
  const original=await page.evaluate(d=>JSON.stringify(d?__fixture.controls:__fixture.dedicatedControls),dedicated);
  const slider=page.locator('.continuous-performance .manual-sliders input').last();await slider.fill('45');await slider.dispatchEvent('change');await page.waitForFunction(()=>__fixture.writes.at(-1)?.patch.manual?.aggressiveness===45);
  assert.equal(await page.evaluate(()=>__fixture.writes.at(-1).scope),dedicated?'dedicated':'global');assert.equal(await page.evaluate(d=>JSON.stringify(d?__fixture.controls:__fixture.dedicatedControls),dedicated),original);
 }

 // CCD uses the actual bridge/runtime/cards with only native IO replaced.
 const ccdServer=await createPerformancePreviewServer(0,{realNavigation:true,ccdRuntime:true,ccdSettings:true});
 report.ccd={gates:[],layouts:[],sharedNativeEntry:false};
 try{
  const ccdPage=await browser.newPage({viewport:{width:1280,height:720}});ccdPage.setDefaultTimeout(5000);ccdPage.on('pageerror',e=>report.errors.push(e.message));
  const base='http://127.0.0.1:'+ccdServer.address().port+'/?';
  for(const query of ['ccd=2','uniform=1&ccd=1','uniform=1&ccd=2&arch=unknown','uniform=1&ccd=2&cross=1','uniform=1&ccd=2&die=single','uniform=1&ccd=2&dedicated=1']){
   await ccdPage.goto(base+query);await ccdPage.locator('.limits-bubble').waitFor();
   // A hidden entry is checked after the actual capability read has settled.
   if(!query.includes('dedicated=1'))await ccdPage.waitForFunction(()=>window.__ccdPorts.probes===1);
   await ccdPage.waitForTimeout(50);assert.equal(await ccdPage.locator('.ccd-control').count(),0,query);
   assert.equal(await ccdPage.evaluate(()=>window.__ccdPorts.writes.length),0);
   report.ccd.gates.push({query,visible:false,zeroWrites:true});
  }
  for(const count of [2,3])for(const height of [720,800]){
   await ccdPage.setViewportSize({width:1280,height});await ccdPage.goto(base+'uniform=1&ccd='+count);await ccdPage.locator('.ccd-control').waitFor();
   assert.equal(await ccdPage.evaluate(()=>window.__ccdPorts.writes.length),0);
   for(const mode of ['auto','manual']){
    await ccdPage.locator('.control-modes button').nth(mode==='auto'?0:1).click();await ccdPage.locator('.ccd-control').waitFor();
    const row=await ccdPage.locator(mode==='auto'?'.auto-core-row':'.uv-manual-bottom-row').evaluate(el=>[...el.querySelectorAll('button[data-gp-col]')].map(b=>{const r=b.getBoundingClientRect();return {x:r.x,y:r.y,w:r.width,h:r.height,col:+b.dataset.gpCol,label:b.getAttribute('aria-label')}}));
    assert.equal(row.length,3);assert.deepEqual(row.map(r=>r.col),[0,1,2]);assert(row.every(r=>Math.abs(r.y+r.h/2-row[0].y-row[0].h/2)<1),JSON.stringify(row));assert(row.every(r=>r.w>70&&Math.abs(r.w-row[0].w)<2),JSON.stringify(row));assert.equal(row.at(-1).label,'降压');
    await ccdPage.locator('.ccd-control .dd-trigger').focus();await ccdPage.evaluate(()=>window.__move(1,0));assert.equal(await ccdPage.evaluate(()=>document.activeElement.getAttribute('aria-label')),'超线程');await ccdPage.evaluate(()=>window.__move(1,0));assert.equal(await ccdPage.evaluate(()=>document.activeElement.getAttribute('aria-label')),'降压');
    assert.equal(await ccdPage.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
    report.ccd.layouts.push({count,height,mode,row});
   }
   await ccdPage.screenshot({path:path.join(out,`ccd-${count}-${height}.png`)});
  }
  await ccdPage.goto(base+'uniform=1&ccd=2');await ccdPage.locator('.ccd-control').waitFor();
  const choose=async label=>{await ccdPage.locator('.ccd-control .dd-trigger').click();await ccdPage.getByRole('option',{name:new RegExp(label)}).click();await ccdPage.waitForFunction(()=>document.querySelectorAll('[role=option]').length===0);};
  await choose('CCD · 2');await ccdPage.waitForFunction(()=>window.__ccdPorts.mode===2);assert.deepEqual(await ccdPage.evaluate(()=>window.__ccdPorts.writes),[2]);
  await ccdPage.locator('#test-ccd-settings').click();await ccdPage.locator('[data-cpu-setting="ccd"]').waitFor();assert((await ccdPage.locator('[data-cpu-setting="ccd"] .dd-trigger').textContent()).includes('CCD · 2'));
  await ccdPage.locator('[data-cpu-setting="ccd"] .dd-trigger').click();await ccdPage.getByRole('option',{name:/CCD · 1/}).click();await ccdPage.waitForFunction(()=>document.querySelectorAll('[role=option]').length===0);assert.deepEqual(await ccdPage.evaluate(()=>window.__ccdPorts.writes),[2]);await ccdPage.locator('[data-cpu-apply="ccd"]').click();await ccdPage.waitForFunction(()=>window.__ccdPorts.mode===1);
  await ccdPage.locator('#test-performance').click();await ccdPage.waitForFunction(()=>document.querySelector('.ccd-control')?.textContent.includes('CCD · 1'));
  await choose('CCD · 全部');await ccdPage.waitForFunction(()=>window.__ccdPorts.mode===0);
  await ccdPage.evaluate(()=>window.__ccdPorts.failNext=true);await choose('CCD · 2');await ccdPage.waitForFunction(()=>document.querySelector('.save-note')?.textContent.includes('fixture CCD failure'));assert((await ccdPage.locator('.ccd-control').textContent()).includes('CCD · 全部'));
  await ccdPage.evaluate(()=>window.__ccdPorts.pendingNext=true);await choose('CCD · 2');await ccdPage.waitForFunction(()=>document.querySelector('.save-note')?.textContent.includes('尚未确认'));assert(!(await ccdPage.locator('.save-note').textContent()).includes('已回读'));
  await ccdPage.screenshot({path:path.join(out,'ccd-pending.png')});
  await ccdPage.evaluate(()=>window.__ccdPorts.hold=true);await choose('CCD · 1');await ccdPage.waitForFunction(()=>window.__ccdPorts.release!==null);assert(await ccdPage.locator('.ccd-control .dd-trigger').isDisabled());
  await ccdPage.locator('#test-ccd-settings').click();await ccdPage.locator('#test-performance').click();await ccdPage.evaluate(()=>{window.__ccdPorts.hold=false;window.__ccdPorts.release();});await ccdPage.waitForFunction(()=>document.querySelector('.ccd-control')?.textContent.includes('CCD · 1'));
  assert.deepEqual(await ccdPage.evaluate(()=>window.__ccdPorts.writes),[2,1,0,2,2,1]);
  report.ccd.sharedNativeEntry=true;report.ccd.settingsApplyPreserved=true;report.ccd.nativeFailurePreserved=true;report.ccd.pendingNotPromoted=true;report.ccd.routeChangeDuringWrite=true;
  await ccdPage.close();
 }finally{await new Promise(resolve=>ccdServer.close(resolve));}
 report.cases=['公用/专属自动手动的所有可用控件在720P/800P逐行对齐，同一行左右、相邻行上下全部可到达','专属末行向下到手柄/陀螺仪，上返回原末行；公用页面左侧空白不产生焦点跳转','公用提交只接公用bank、专属提交只接专属bank，另一套参数保持不变'];assert.deepEqual(report.errors,[]);report.pass=true;
}finally{await browser?.close();await new Promise(resolve=>server.close(resolve));fs.writeFileSync(path.join(out,'panel-navigation-verification.json'),JSON.stringify(report,null,2));}
console.log(JSON.stringify({pass:report.pass,layouts:report.layouts.length,cases:report.cases,errors:report.errors},null,2));
