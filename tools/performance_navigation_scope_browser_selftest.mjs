import assert from 'node:assert/strict';import fs from 'node:fs';import path from 'node:path';import {createRequire} from 'node:module';import {createPerformancePreviewServer} from './lib/continuousPerformancePreview.mjs';
const {chromium}=createRequire('C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/_continuous_ui.cjs')('playwright'),out=process.argv[2];fs.mkdirSync(out,{recursive:true});const report={pass:false,cases:[],routes:[],errors:[]},server=await createPerformancePreviewServer(0,{realNavigation:true});let browser;
try{
 browser=await chromium.launch({channel:'msedge',headless:true});const page=await browser.newPage({viewport:{width:1280,height:720}});page.on('pageerror',e=>report.errors.push(e.stack));
 const move=async(dx,dy)=>{await page.evaluate(({dx,dy})=>__move(dx,dy),{dx,dy});return page.evaluate(()=>({row:document.activeElement.dataset.gpRow,col:document.activeElement.dataset.gpCol,menu:document.activeElement.closest('[data-gp-game-row]')?.dataset.gpGameRow,inPanel:!!document.activeElement.closest('[data-gp-performance-panel]'),text:document.activeElement.getAttribute('aria-label')||document.activeElement.textContent}));};
 for(const dedicated of [false,true])for(const uniform of [false,true]){
  await page.goto('http://127.0.0.1:'+server.address().port+'/?dedicated='+(dedicated?'1':'0')+'&uniform='+(uniform?'1':'0'));await page.waitForSelector('.continuous-performance .limits-bubble');
  for(const height of [720,800]){
   await page.setViewportSize({width:1280,height});
   for(const mode of ['auto','manual']){
    await page.locator('.control-modes button').nth(mode==='auto'?0:1).click();await page.waitForSelector(mode==='auto'?'.auto-bubble':'.manual-bubble');
    if(dedicated){await page.locator('[data-gp-custom-entry]').focus();const target=await move(0,1);assert(target.inPanel&&target.row==='0','专用入口向下跳过新性能面板: '+JSON.stringify(target));await page.locator('.control-modes button').nth(0).focus();const up=await move(0,-1);assert.equal(up.menu,'custom-entry');}
    const rows=await page.locator('.continuous-performance').evaluate(panel=>{const map=new Map();for(const el of __auditFocusables().filter(el=>panel.contains(el))){if(el.dataset.gpRow===undefined)continue;const row=Number(el.dataset.gpRow);if(!map.has(row))map.set(row,[]);map.get(row).push({row,col:Number(el.dataset.gpCol),label:el.getAttribute('aria-label')||el.textContent});}return [...map].sort((a,b)=>a[0]-b[0]).map(([row,items])=>items.sort((a,b)=>a.col-b.col));});
    const select=item=>page.locator('.continuous-performance [data-gp-row="'+item.row+'"][data-gp-col="'+item.col+'"]').focus();
    for(let i=0;i<rows.length;i++)for(const item of rows[i]){
     const index=rows[i].indexOf(item);for(const [dx,expected]of [[-1,rows[i][index-1]],[1,rows[i][index+1]]]){await select(item);const target=await move(dx,0);assert.equal(Number(target.row),expected?.row??item.row);assert.equal(Number(target.col),expected?.col??item.col);}
     if(i>0){await select(item);const target=await move(0,-1);assert.equal(Number(target.row),rows[i-1][0].row);}if(i+1<rows.length){await select(item);const target=await move(0,1);assert.equal(Number(target.row),rows[i+1][0].row);}
    }
    const last=rows.at(-1),left=last[0],right=last.at(-1);if(dedicated){
     await select(left);const bottomLeft=await move(0,1);assert.equal(bottomLeft.menu,uniform?'custom-input-gyro':'custom-input-pad');await page.locator('[data-gp-game-row="custom-input-pad"]').focus();const back=await move(0,-1);assert.equal(Number(back.row),left.row);assert.equal(Number(back.col),left.col);
     await select(right);const bottom=await move(0,1);assert.equal(bottom.menu,'custom-input-gyro');assert.equal((await move(0,-1)).row,String(right.row));
     await page.locator('[data-gp-custom-actions] button').nth(1).focus();assert.equal((await move(0,-1)).menu,'custom-input-gyro');await page.locator('[data-gp-game-row="rules-entry"]').focus();const outer=await move(0,-1);assert.equal(outer.menu,'custom-actions');
    }
    if(!uniform&&height===720)await page.screenshot({path:path.join(out,'navigation-'+(dedicated?'dedicated':'global')+'-'+mode+'-1280x720.png'),fullPage:true});
    report.routes.push({dedicated,uniform,height,mode,rows:rows.map(row=>row.map(v=>[v.row,v.col]))});
   }
  }
 }
 for(const dedicated of [false,true]){
  await page.goto('http://127.0.0.1:'+server.address().port+'/?dedicated='+(dedicated?'1':'0'));await page.waitForSelector('.continuous-performance .limits-bubble');
  const scope=dedicated?'dedicated':'global',other=dedicated?'controls':'dedicatedControls',bank=dedicated?'dedicatedControls':'controls',otherBefore=await page.evaluate(key=>JSON.stringify(__fixture[key]),other),tdp=page.locator('.continuous-performance [data-gp-row="2"][data-gp-col="0"]');
  const stored=()=>page.evaluate(key=>__fixture[key].ac.tdp,bank);const initial=await stored();await tdp.focus();
  await page.evaluate(()=>__press('nav-right'));assert.equal(await page.evaluate(()=>document.activeElement.dataset.gpCol),'1');assert.equal(await stored(),initial);await page.evaluate(()=>__press('nav-left'));
  await page.evaluate(()=>{window.__backEscapes=0;window.addEventListener('ipc:gamepad-back',()=>__backEscapes++);__press('confirm');__press('nav-left');});assert.equal(await page.evaluate(()=>__readEdit()),true);assert.equal(Number(await tdp.inputValue()),initial-1);assert.equal(await stored(),initial);
  await page.evaluate(()=>__press('back'));await page.waitForFunction(({key,initial})=>Number(document.querySelector('.continuous-performance [data-gp-row="2"][data-gp-col="0"]').value)===initial,{key:bank,initial});assert.equal(await page.evaluate(()=>__readEdit()),false);assert.equal(await page.evaluate(()=>__backEscapes),0);assert.equal(await stored(),initial);
  await tdp.focus();await page.evaluate(()=>{__press('confirm');__press('nav-left');__press('nav-up');});await page.waitForFunction(value=>Number(document.querySelector('.continuous-performance [data-gp-row="2"][data-gp-col="0"]').value)===value,initial);assert.equal(await page.evaluate(()=>__readEdit()),false);assert.equal(await stored(),initial);
  await tdp.focus();await page.evaluate(()=>{__press('confirm');__press('nav-left');__press('confirm');});await page.waitForFunction(({key,value})=>__fixture[key].ac.tdp===value,{key:bank,value:initial-1});assert.equal(await page.evaluate(()=>__fixture.writes.at(-1).scope),scope);assert.equal(await page.evaluate(key=>JSON.stringify(__fixture[key]),other),otherBefore);
  await tdp.focus();await page.evaluate(()=>{__press('confirm');__press('nav-left');});await page.evaluate(async({scope,value})=>__externalTdp(scope,'ac',value),{scope,value:initial-3});await page.evaluate(()=>__press('back'));await page.waitForFunction(value=>Number(document.querySelector('.continuous-performance [data-gp-row="2"][data-gp-col="0"]').value)===value,initial-3);assert.equal(await stored(),initial-3);
  const writes=await page.evaluate(()=>__fixture.writes.length);await page.locator('.continuous-performance [data-gp-row="2"][data-gp-col="1"]').focus();await page.evaluate(()=>__press('confirm'));await page.waitForSelector('[role="listbox"]');await page.evaluate(()=>__press('nav-down'));await page.evaluate(()=>__press('back'));await page.waitForSelector('[role="listbox"]',{state:'detached'});assert.equal(await page.evaluate(()=>__fixture.writes.length),writes);
  await page.locator('.power-modes button').nth(1).focus();await page.evaluate(()=>__press('confirm'));await page.waitForSelector('.continuous-performance.dc');const acBefore=await page.evaluate(key=>JSON.stringify(__fixture[key].ac),bank);await tdp.focus();await page.evaluate(()=>{__press('confirm');__press('nav-left');__press('confirm');});await page.waitForFunction(key=>__fixture[key].dc.tdp===29,bank);assert.equal(await page.evaluate(key=>JSON.stringify(__fixture[key].ac),bank),acBefore);assert.equal(await page.locator('.integrated-supply').textContent(),'插电 · AC');
 }
 report.cases.push('Production native-action dispatcher: unarmed directions only navigate;A arms/commits, B cancels without escaping the page, latest externalTDP survives cancellation, dropdownB only closes, AC/DC and global/dedicated storage stay isolated');
 report.cases.push('Production moveFocus with real focusables/performance/menu resolvers: entry/panel/pad/gyro/action/rules boundaries and both directions across16 configurations');
 assert.deepEqual(report.errors,[]);report.pass=true;
}catch(error){report.failure=error.stack;throw error;}finally{await browser?.close();await new Promise(resolve=>server.close(resolve));fs.writeFileSync(path.join(out,'navigation-scope-verification.json'),JSON.stringify(report,null,2));}
console.log(JSON.stringify({pass:report.pass,configurations:report.routes.length,cases:report.cases,errors:report.errors},null,2));
