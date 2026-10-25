// Real Vue component, controls and production API wrappers; IPC/settings use explicit memory ports.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
const repo=process.cwd(),out=path.resolve(process.argv[2]);
assert(!out.startsWith(repo+path.sep)&&!fs.existsSync(out),'Use a fresh evidence directory outside source');
fs.mkdirSync(out,{recursive:true});
const sourceIndex=process.argv.indexOf('--component-source');
const component=sourceIndex>=0?path.resolve(process.argv[sourceIndex+1]):path.join(repo,'src/components/ControllerFeedbackCapabilitiesCard.vue');
const sourceBefore=fs.readFileSync(component),hash=b=>crypto.createHash('sha256').update(b).digest('hex');
let script=fs.readFileSync(path.join(repo,'tools/controller_lighting_operations_browser_selftest.mjs'),'utf8');
const replace=(before,after)=>{assert(script.includes(before),'Baseline fixture changed: '+before);script=script.replace(before,after)};
replace("const {build}=req('esbuild')","const {build,stop}=req('esbuild')");
replace("fs.readFileSync(a.path,'utf8')","fs.readFileSync(a.path.endsWith('ControllerFeedbackCapabilitiesCard.vue')?"+JSON.stringify(component)+":a.path,'utf8')");
replace("import{createApp,h}from'vue';import Card","import{createApp,h,KeepAlive,ref}from'vue';import Card");
replace('window.mount=(scenario)=>','window.unmount=()=>{app?.unmount();app=undefined};window.mount=(scenario)=>');
replace("app=createApp({render:()=>h(Card)});app.mount('#app')", "app=createApp({setup(){const shown=ref(true);window.showCard=()=>shown.value=true;window.hideCard=()=>shown.value=false;return()=>window.__useKeepAlive?h(KeepAlive,null,()=>shown.value?h(Card):null):h(Card)}});app.mount('#app')");
replace('vibrationAvailable:false,physicalPrimarySlot:-1','vibrationAvailable:!!window.__allowVibration,physicalPrimarySlot:-1');
replace('loadSettings(){return structuredClone(window.f.settings)}',
 'loadSettings(){const result=structuredClone(window.f.settings);if(window.__deferSettings)return await new Promise(resolve=>window.__pendingSettings.push(()=>resolve(result)));return result}');

const apiPath=path.join(repo,'src/bridge/api.ts'),apiBefore=fs.readFileSync(apiPath);
const ipcMemory=String.raw`export async function invoke(cmd,args={},options={}){
 (window.__ipcLedger??=[]).push({cmd,args:structuredClone(args),options:structuredClone(options)});
 if(cmd==='gamepad.feedbackCapabilities'){
  const result=structuredClone(window.f.caps);
  if(window.__deferCaps)return await new Promise(resolve=>window.__pendingCaps.push(()=>resolve(result)));
  return result;
 }
 if(cmd==='gamepad.setLed'){window.f.applies.push(structuredClone(args));return {ok:true}}
 if(cmd==='gamepad.vibrate'){(window.__vibrations??=[]).push(args.strength);return {ok:true}}
 if(cmd==='clipboard.writeText')return {};
 throw Error('Unexpected actual API command: '+cmd);
}`;
replace('const bundle=await build(',
 `mocks['./ipc']=mocks['@/bridge/ipc']=${JSON.stringify(ipcMemory)};\n`+
 `mocks['@/bridge/api']=${JSON.stringify('export {gamepadFeedback} from '+JSON.stringify(apiPath)+';')};\n`+
 'const bundle=await build(');

const tests=String.raw`
 const lifecycle=[];
 const record=(id,height,actual,expected)=>lifecycle.push({id:id+'-'+height,viewport:{width:1280,height},actual,expected,status:JSON.stringify(actual)===JSON.stringify(expected)?'PASS':'FAIL',hardwareIo:false});
 async function resetCase(options={}){
  await page.evaluate(options=>{
   window.unmount();window.__useKeepAlive=!!options.keepAlive;
   window.__deferCaps=!!options.deferCaps;window.__deferSettings=!!options.deferSettings;
   window.__pendingCaps=[];window.__pendingSettings=[];window.mount('mini');
  },options);
 }
 for(const height of [720,800]){
  await page.setViewportSize({width:1280,height});
  for(const keepAlive of [false,true]){
   await resetCase({keepAlive});
   await page.waitForFunction(()=>window.f.applies.length===1);
   const before=await page.evaluate(()=>window.f.applies.length);
   await page.getByRole('slider',{name:'亮度',exact:true}).evaluate(el=>{el.value='61';el.dispatchEvent(new Event('input',{bubbles:true}))});
   await page.waitForFunction(()=>window.f.saved.length>0);
   await page.evaluate(keepAlive=>keepAlive?window.hideCard():window.unmount(),keepAlive);
   await page.waitForTimeout(400);
   record(keepAlive?'deactivate-cancels-queued-write':'unmount-cancels-queued-write',height,{additionalWrites:await page.evaluate(()=>window.f.applies.length)-before},{additionalWrites:0});
   if(keepAlive){
    await page.evaluate(()=>window.showCard());
    await page.getByRole('slider',{name:'亮度',exact:true}).waitFor();
    await page.getByRole('slider',{name:'亮度',exact:true}).evaluate(el=>{el.value='72';el.dispatchEvent(new Event('input',{bubbles:true}))});
    await page.waitForTimeout(400);
    record('reactivation-allows-new-explicit-write',height,await page.evaluate(()=>({lastBrightness:window.f.applies.at(-1)?.brightness})),{lastBrightness:72});
   }
  }
  for(const keepAlive of [false,true]){
   await resetCase({keepAlive,deferCaps:true});
   await page.waitForFunction(()=>window.__pendingCaps.length>0);
   await page.evaluate(keepAlive=>{if(keepAlive)window.hideCard();else window.unmount()},keepAlive);
   await page.waitForTimeout(20);
   await page.evaluate(()=>{window.__deferCaps=false;window.__pendingCaps.splice(0).forEach(resolve=>resolve())});
   await page.waitForTimeout(100);
   record(keepAlive?'late-capabilities-after-deactivation':'late-capabilities-after-unmount',height,{writes:await page.evaluate(()=>window.f.applies.length)},{writes:0});
  }
  for(const keepAlive of [false,true]){
   await resetCase({keepAlive,deferSettings:true});
   await page.waitForFunction(()=>window.__pendingSettings.length>0);
   await page.evaluate(keepAlive=>{if(keepAlive)window.hideCard();else window.unmount()},keepAlive);
   await page.waitForTimeout(20);
   await page.evaluate(()=>{window.__deferSettings=false;window.__pendingSettings.splice(0).forEach(resolve=>resolve())});
   await page.waitForTimeout(100);
   record(keepAlive?'late-settings-after-deactivation':'late-settings-after-unmount',height,{writes:await page.evaluate(()=>window.f.applies.length)},{writes:0});
  }
  for(const keepAlive of [false,true]){
   await page.evaluate(()=>{window.__allowVibration=true;window.__vibrations=[]});
   await resetCase({keepAlive});
   await page.waitForFunction(()=>window.f.applies.length===1);
   await page.getByRole('button',{name:'立即震动',exact:true}).evaluate((el,keepAlive)=>{
    el.click();
    if(keepAlive)window.hideCard();else window.unmount();
   },keepAlive);
   await page.waitForFunction(()=>window.__vibrations.length>=1);
   await page.waitForTimeout(220);
   record(keepAlive?'deactivate-revokes-second-rumble-pulse':'unmount-revokes-second-rumble-pulse',height,{pulses:await page.evaluate(()=>window.__vibrations.length)},{pulses:1});
   await page.evaluate(()=>{window.__allowVibration=false});
  }
  await resetCase({keepAlive:true,deferCaps:true});
  await page.waitForFunction(()=>window.__pendingCaps.length>0);
  await page.evaluate(()=>window.hideCard());
  await page.waitForTimeout(20);
  await page.evaluate(()=>{window.__deferCaps=false;window.f.caps.ledAvailable=false;window.showCard()});
  await page.waitForTimeout(60);
  await page.evaluate(()=>window.__pendingCaps.splice(0).forEach(resolve=>resolve()));
  await page.waitForTimeout(100);
  record('old-capability-response-cannot-reenable-new-session',height,{writes:await page.evaluate(()=>window.f.applies.length),enabled:await page.getByRole('switch',{name:'启用控制',exact:true}).isEnabled()},{writes:0,enabled:false});
  await resetCase();
  await page.waitForFunction(()=>window.f.applies.length===1);
  await page.getByRole('slider',{name:'亮度',exact:true}).evaluate(el=>{el.value='83';el.dispatchEvent(new Event('input',{bubbles:true}))});
  await page.waitForFunction(()=>window.f.applies.length===2);
  record('active-settings-still-consumed',height,await page.evaluate(()=>({brightness:window.f.applies[1].brightness,stored:window.f.settings.gamepad.feedback.lighting.brightness})),{brightness:83,stored:83});
 }
 await page.evaluate(()=>window.unmount());
 const ledger=await page.evaluate(()=>window.__ipcLedger);
 for(const [cmd,timeout] of [['gamepad.feedbackCapabilities',2000],['gamepad.setLed',35000],['gamepad.vibrate',3000]]){
  const rows=ledger.filter(row=>row.cmd===cmd);
  assert(rows.length>0&&rows.every(row=>row.options.timeoutMs===timeout),'Actual API command and timeout contract: '+cmd);
 }
 fs.writeFileSync(path.join(out,'ipc-ledger.json'),JSON.stringify({actualProductionApi:true,calls:ledger,nativeIo:false},null,2));
 fs.writeFileSync(path.join(out,'lifecycle-cases.json'),JSON.stringify({status:lifecycle.every(c=>c.status==='PASS')?'PASS':'FAIL',caseCount:lifecycle.length,failedCount:lifecycle.filter(c=>c.status==='FAIL').length,cases:lifecycle,hardwareIo:false,consumer:'actual Vue component lifecycle -> settings/debounce -> gamepadFeedback.setLed memory boundary'},null,2));
 assert.equal(lifecycle.filter(c=>c.status==='FAIL').length,0,'leaving lighting controls must revoke queued and pending initialization writes');
`;
replace(' assert.deepEqual(errors,[]);',tests+' assert.deepEqual(errors,[]);');
replace('}finally{await browser?.close();await new Promise(r=>server.close(r));}',
 '}finally{await browser?.close();await new Promise(r=>server.close(r));stop();}');
const probe=path.join(out,'browser-probe.mjs');fs.writeFileSync(probe,script);
const child=spawnSync(process.execPath,[probe,out],{cwd:repo,encoding:'utf8',timeout:90000,windowsHide:true,maxBuffer:2<<20});
fs.writeFileSync(path.join(out,'stdout.log'),child.stdout??'');fs.writeFileSync(path.join(out,'stderr.log'),child.stderr??'');
const stable=hash(fs.readFileSync(component))===hash(sourceBefore)&&hash(fs.readFileSync(apiPath))===hash(apiBefore);
fs.writeFileSync(path.join(out,'summary.json'),JSON.stringify({allPassed:child.status===0&&stable,exit:child.status,signal:child.signal,error:child.error?.message,source:component,sourceSha256:hash(sourceBefore),apiSource:apiPath,apiSourceSha256:hash(apiBefore),sourceStable:stable,fixtureBaselineSha256:hash(fs.readFileSync(path.join(repo,'tools/controller_lighting_operations_browser_selftest.mjs'))),hardwareIo:false,installedApplicationStarted:false,scope:'Real Vue/controls and production API wrappers at 1280x720 and 1280x800; only IPC/settings are in memory; no native/physical device acceptance'},null,2));
console.log('LIGHTING_LIFECYCLE',child.status===0&&stable?'PASS':'FAIL','evidence='+out);
if(child.status!==0||!stable)process.exitCode=1;
