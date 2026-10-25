import fs from 'node:fs';import path from 'node:path';import {pathToFileURL} from 'node:url';
const root=process.cwd(),out=process.env.YMCC_APPEARANCE_TEST_OUT;fs.mkdirSync(out,{recursive:true});
const base=fs.readFileSync('tools/steam_appearance_browser_selftest.mjs','utf8'),end=base.indexOf('let browser,activePage;');
let setup=base.slice(0,end).replace('documentVisible:true,nativeWindowVisible:true,powerReady:true','documentVisible:window.__fixture.uiVisible!==false,nativeWindowVisible:true,powerReady:true');
const tail=String.raw`
let browser;const cases=[],errors=[];
try{
 browser=await chromium.launch({headless:true,executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
 const page=await browser.newPage();page.on('pageerror',e=>errors.push(e.message));await page.goto('http://127.0.0.1:'+server.address().port);await page.waitForFunction(()=>window.__draft.getState().snapshot);await page.waitForTimeout(400);
 const reads=()=>page.evaluate(()=>window.__fixture.calls.filter(c=>c.action==='Status').length);
 const events=()=>page.evaluate(()=>{for(let i=0;i<1000;i++)window.dispatchEvent(new Event('ipc:steamAppearance.backendChanged'))});
 const initial=await reads();await events();await page.waitForTimeout(2000);assert.equal(await reads(),initial);await page.waitForTimeout(3300);assert.equal(await reads(),initial+1);cases.push('1000 backend events produce one Status read after the 5s gap');
 const quiet=await reads();await page.waitForTimeout(6000);assert.equal(await reads(),quiet);cases.push('no events means no appearance Status poll or recurring timer');
 await page.evaluate(()=>window.__fixture.failRead=true);
 for(let i=0;i<3;i++){await events();await page.waitForTimeout(5400)}
 const failed=await reads();assert.equal(failed,quiet+3);await events();await page.waitForTimeout(5500);assert.equal(await reads(),failed);cases.push('three failures exhaust automatic reads even under continued event storms');
 await page.evaluate(()=>{window.__fixture.failRead=false;window.dispatchEvent(new Event('global:refresh'))});await page.waitForFunction(()=>window.__draft.getState().snapshot);assert.equal(await reads(),failed+1);cases.push('one explicit refresh renews the stopped read budget');
 await events();await page.evaluate(()=>{window.__fixture.uiVisible=false;document.dispatchEvent(new Event('visibilitychange'))});await page.waitForTimeout(5400);assert.equal(await reads(),failed+1);cases.push('hiding cancels the queued read and backend events cannot search while hidden');
 await page.evaluate(()=>{window.__fixture.uiVisible=true;document.dispatchEvent(new Event('visibilitychange'))});await page.waitForTimeout(700);assert.equal(await reads(),failed+2);cases.push('restoring visibility schedules one bounded read, not a Steam launch or CSS restart');
 assert.equal(errors.length,0);const result={ok:true,passed:cases.length,cases,pageErrors:errors,realSteamOperations:0,realCssOperations:0};fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2));console.log(JSON.stringify(result,null,2));
}finally{await browser?.close();await new Promise(r=>server.close(r))}
`;
const runner=path.join(out,'read-budget-runner.mjs');fs.writeFileSync(runner,setup+tail);await import(pathToFileURL(runner).href);
