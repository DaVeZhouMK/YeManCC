// Reuse the existing real FanView/NavRail/fanFeature + inert Host browser fixture.
// Add unavailable/recovery observations only; no native or hardware access.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {spawnSync} from 'node:child_process';
const root=process.cwd(),out=path.resolve(process.env.YMCC_SHARED_TEMP_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/shared-temperature-consumers-20261017'),dir=out+'/browser';fs.mkdirSync(dir,{recursive:true});
let script=fs.readFileSync('tools/fan_navigation_wake_browser_selftest.mjs','utf8');
const setter='window.__monitorTemperature=value=>{topMonitorData.value={tempC:value}};';assert.equal(script.split(setter).length,2);script=script.replace(setter,setter+'window.__monitorSample=value=>{topMonitorData.value=value};');
const marker="  await test(family+': unchanged receipt deduplicates display and adds no timer'";
const extra=String.raw`
  await test(family+': missing/failed temperature renders unavailable without a Host query or control stop',async()=>{
   await page.evaluate(()=>window.__go('/fan'));await page.waitForFunction(()=>document.querySelector('.fan-page'));await page.evaluate(async()=>{for(let i=0;i<100;i++)await Promise.resolve()});
   const before=await page.evaluate(()=>window.__chain.trace.length);
   for(const sample of [{tempC:0,hwDown:true,cpuTempValid:true},{tempC:95,hwDown:false,cpuTempValid:false},{tempC:null},{tempC:'95'}]){
    await page.evaluate(v=>window.__monitorSample(v),sample);
    await page.waitForFunction(()=>document.querySelector('.temperature-state')?.textContent.includes('无数据'));
    assert.equal(await page.evaluate(()=>window.__navigation.fanNavigationDuty.value),0);
    assert.equal(await page.evaluate(()=>window.__navigation.fanControlActive.value),true);
    assert.equal(await spins(),true,'control receipt remains active when only display temperature is missing');
    assert.equal(await page.getByText('期望转速 无数据',{exact:true}).count(),1);
   }
   assert.equal(await page.evaluate(()=>window.__chain.trace.length),before,'temperature loss must add no Host/HTTP/control operation');
   if(family==='ClawCG3EM')await page.screenshot({path:path.join(out,'unavailable-temperature.png')});
  });
  await test(family+': real temperature recovery restores all display values without a Host query',async()=>{
   const before=await page.evaluate(()=>window.__chain.trace.length);await page.evaluate(()=>window.__monitorTemperature(80));
   await page.waitForFunction(()=>window.__navigation.fanNavigationDuty.value===60);
   assert.equal(await page.locator('.temperature-state').innerText(),'温度 80°C');
   assert.equal(await page.getByText('期望转速 60%',{exact:true}).count(),1);
   assert.equal(await page.evaluate(()=>window.__chain.trace.length),before);
   await page.evaluate(()=>window.__go('/tdp'));await page.waitForFunction(()=>document.querySelector('#not-fan-page'));await spinReady();
  });
`;
assert.equal(script.split(marker).length,2);script=script.replace(marker,extra+marker);script=script.replace('const test=async(name,fn)=>{await fn();cases.push(name)};', 'const test=async(name,fn)=>{if(!/cold navigation|background start|committed curve|missing|real temperature recovery|unchanged receipt|inactive UI/.test(name))return;console.log("START "+name);await fn();cases.push(name);console.log("PASS "+name)};');script=script.replace("productionUi:['NavRail.vue','FanView.vue','fanFeature.ts','fanHost.ts'],","productionUi:['NavRail.vue','FanView.vue','fanFeature.ts','fanHost.ts'],testScope:'Temperature loss/recovery and no extra query only; unrelated wake/OFF cases omitted',");script=script.replace('page=await browser.newPage();','page=await browser.newPage();page.setDefaultTimeout(5000);');script=script.replace("page.on('pageerror',e=>errors.push(e.message));","page.on('pageerror',e=>{errors.push(e.message);console.error('PAGEERROR '+e.message)});");const file=out+'/browser-current.mjs';fs.writeFileSync(file,script);
const run=spawnSync(process.execPath,[file],{windowsHide:true,encoding:'utf8',timeout:45000,maxBuffer:3e6,env:{...process.env,YMCC_FAN_NAV_EVIDENCE_DIR:dir}});fs.writeFileSync(out+'/BROWSER-REGRESSION.log',(run.stdout||'')+(run.stderr||''));assert.equal(run.status,0,(run.error?.message||'')+run.stdout+run.stderr);const report=JSON.parse(fs.readFileSync(dir+'/result.json','utf8'));assert.equal(report.ok,true);assert.equal(report.passed,21);assert.deepEqual(report.browserErrors,[]);console.log(JSON.stringify({ok:true,passed:report.passed,scope:report.testScope}));
