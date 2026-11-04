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

// Reuse the production navigation slice and its real geometry/focus helpers.
const navigationInputs=['src/gamepad/engine.ts','src/gamepad/focus.ts','src/gamepad/spatial.ts','src/gamepad/performanceNavigation.ts'];
const navigationBefore=Object.fromEntries(navigationInputs.map(p=>[p,hash(fs.readFileSync(path.join(repo,p)))]));
const engine=fs.readFileSync(path.join(repo,navigationInputs[0]),'utf8');
const focusStart=engine.indexOf('function focusables('),focusEnd=engine.indexOf('function isRangeInput(',focusStart);
assert(focusStart>=0&&focusEnd>focusStart,'Production focus slice boundary');
const engineFunction=signature=>{const start=engine.indexOf(signature),end=engine.indexOf('\n}',start);assert(start>=0&&end>start);return engine.slice(start,end+2)};
const navModule=`import{focusGamepadElement,setGamepadFocused,recoverGamepadFocus}from ${JSON.stringify(path.join(repo,'src/gamepad/focus.ts'))};import{spatialNavigationTarget,createSpatialMeasurements,type SpatialMeasurements}from ${JSON.stringify(path.join(repo,'src/gamepad/spatial.ts'))};import{performancePanelTarget}from ${JSON.stringify(path.join(repo,'src/gamepad/performanceNavigation.ts'))};const FOCUSABLE='button:not([disabled]):not([tabindex="-1"]):not([data-gp-ignore]), input:not([disabled]):not([tabindex="-1"]):not([data-gp-ignore]), select:not([disabled]):not([tabindex="-1"]):not([data-gp-ignore]), [tabindex]:not([tabindex="-1"]):not([data-gp-ignore])';let sliderEditMode=false;export const isMouseModeSuppressed=()=>false;${engineFunction('function offerPageCommand(')}\n${engineFunction('function activeFanNodeEditor(')}\n${engine.slice(focusStart,focusEnd)}\nexport{moveFocus as testMoveFocus,focusables as testFocusables,focusGamepadElement as testFocus};`;
replace('const bundle=await build(',`mocks['@/gamepad/focus']=${JSON.stringify('export * from '+JSON.stringify(path.join(repo,'src/gamepad/focus.ts'))+';')};mocks['@/gamepad/engine']=${JSON.stringify(navModule)};\nconst bundle=await build(`);
replace("import{createApp,h,KeepAlive,ref}from'vue';import Card", "import{createApp,h,KeepAlive,ref}from'vue';import*as Nav from'@/gamepad/engine';window.__lightingNavigation=Nav;import Card");
// Consume the existing unified rows; this is not a second model capability table.
const catalogPath=path.join(repo,'capabilities/hc-device-catalog.json'),catalogBefore=fs.readFileSync(catalogPath);
const lightingCatalog=JSON.parse(catalogBefore.toString('utf8').replace(/^\uFEFF/,''));
const lightingRows=[...lightingCatalog.domains.lighting.rows,...lightingCatalog.domains.lighting.extensionRows];
replace('app=createApp({setup()',`if(window.__lightingRow){const r=window.__lightingRow;window.f.caps={...window.f.caps,ledAvailable:true,brightnessAvailable:r.brightness,supportsSecondColor:r.secondColor,lightingFixedSpeed:r.fixedSpeed===true,presetSupportsSecondColor:r.presetSecondColor===true,ledEffects:r.declaredEffects,ledPresets:r.presetAvailable?(r.presets??Array.from({length:11},(_,i)=>({id:'LEDPreset_OneXPlayerX1_Preset'+String(i+1).padStart(2,'0'),label:'预设 '+(i+1)}))):[],presetDeclared:r.presetDeclared,presetImplemented:r.presetAvailable};Object.assign(window.f.settings.gamepad.feedback.lighting,{mode:window.__lightingMode,color:'#2ea6ff',color2:'#9d7cff'})}app=createApp({setup()`);
const colorTests=String.raw`
 const colorControls=[];
 const saveColorControls=()=>fs.writeFileSync(path.join(out,'catalog-color-controls.json'),JSON.stringify({status:colorControls.every(c=>c.status==='PASS')?'PASS':'FAIL',cases:colorControls,hardwareIo:false,scope:'Unified catalog rows -> real Vue controls -> production API memory boundary'},null,2));
 for(const height of [720,800]){
  await page.setViewportSize({width:1280,height});
  for(const row of LIGHTING_ROWS)for(const mode of row.declaredEffects.filter(m=>m!=='preset'||row.presetAvailable)){
   await page.evaluate(({row,mode})=>{window.unmount();window.__useKeepAlive=false;window.__deferCaps=false;window.__deferSettings=false;window.__lightingRow=row;window.__lightingMode=mode;window.mount('mini')},{row,mode});
   await page.waitForFunction(()=>window.f.applies.length===1);
   await page.waitForFunction(()=>document.querySelectorAll('[role="listbox"]').length===0);
   const split=mode==='ambilight',expectedCount=mode==='preset'?(row.presetSecondColor?1:0):(row.secondColor||split?2:1);
   const actual=await page.evaluate(()=>({colors:document.querySelectorAll('.color-field').length,brightness:document.querySelectorAll('input[aria-label="亮度"]').length,speedDisabled:document.querySelector('input[aria-label="速度"]').disabled,mode:window.f.applies[0].mode}));
   const expected={colors:expectedCount,brightness:row.brightness?1:0,speedDisabled:row.fixedSpeed===true||['solid','ambilight','preset'].includes(mode),mode};
   colorControls.push({id:row.name+'-'+mode+'-'+height,actual,expected,status:JSON.stringify(actual)===JSON.stringify(expected)?'PASS':'FAIL',hardwareIo:false});
   saveColorControls();
   assert.deepEqual(actual,expected,'Catalog effect must expose every consumed color: '+row.name+':'+mode);
   if(mode==='preset'){
    await page.locator('.preset-field button').first().click();assert.equal(await page.getByRole('option').count(),(row.presets??Array(11)).length);await page.keyboard.press('Escape');await page.waitForFunction(()=>document.querySelectorAll('[role="listbox"]').length===0);
    if(row.presetSecondColor){
     assert.equal(await page.locator('.color-field .field-label').textContent(),'标志与功能键颜色');
     const presetId=await page.evaluate(()=>window.f.applies[0].presetId);
     await page.locator('.color-field button').first().click();await page.getByRole('option',{name:'翠绿',exact:true}).click();await page.waitForFunction(()=>window.f.applies.length===2);
     const next=await page.evaluate(()=>window.f.applies.at(-1));assert.equal(next.presetId,presetId);assert.equal(next.color2,'#2f9e44');
     colorControls.push({id:row.name+'-preset-secondary-'+height,status:'PASS',presetId:next.presetId,secondColor:next.color2,hardwareIo:false});saveColorControls();
    }
   }
   if(split){
    assert.equal(await page.locator('.color-field .field-label').allTextContents().then(v=>v.join('/')),'左侧颜色/右侧颜色');
    assert.match(await page.locator('.lighting-effect-hint').textContent(),/固定配色/);
    const before=await page.evaluate(()=>structuredClone(window.f.applies.at(-1)));
    await page.locator('.color-field').nth(1).locator('button').first().click();
    await page.getByRole('option',{name:'翠绿',exact:true}).click();
    await page.waitForFunction(()=>window.f.applies.length===2);
    const after=await page.evaluate(()=>({call:window.f.applies.at(-1),saved:window.f.saved.at(-1).value.feedback.lighting}));
    assert.equal(after.call.color,before.color,'Right edit must preserve the left color');assert.equal(after.call.color2,'#2f9e44');assert.equal(after.saved.color2,after.call.color2);
    await page.locator('.color-field').nth(0).locator('button').first().click();await page.getByRole('option',{name:'珊瑚',exact:true}).click();
    await page.waitForFunction(()=>window.f.applies.length===3);
    const leftEdited=await page.evaluate(()=>window.f.applies.at(-1));assert.equal(leftEdited.color,'#ff7a59');assert.equal(leftEdited.color2,'#2f9e44');
    const navigation=await page.evaluate(()=>{
     const nav=window.__lightingNavigation,nodes=nav.testFocusables(),left=document.querySelectorAll('.color-field button')[0],right=document.querySelectorAll('.color-field button')[1];
     const edges=nodes.map(()=>[]);for(let i=0;i<nodes.length;i++)for(const [dx,dy]of [[-1,0],[1,0],[0,-1],[0,1]]){nav.testFocus(nodes[i]);nav.testMoveFocus(dx,dy);edges[i].push(nodes.indexOf(document.activeElement))}
     const reach=start=>{const seen=new Set([start]),todo=[start];while(todo.length)for(const next of edges[todo.shift()])if(next>=0&&!seen.has(next)){seen.add(next);todo.push(next)}return seen};
     const li=nodes.indexOf(left),ri=nodes.indexOf(right);nav.testFocus(right);
     return{leftPresent:li>=0,rightPresent:ri>=0,rightReachable:reach(li).has(ri),leftReturnable:reach(ri).has(li),disabledExcluded:nodes.every(n=>!n.disabled&&!n.closest('[inert]'))};
    });assert(Object.values(navigation).every(Boolean),'Production gamepad navigation must reach and return from both colors');
    await page.locator('.color-field').nth(1).locator('button').first().dispatchEvent('gp:dropdown-open',{bubbles:true,cancelable:true});await page.getByRole('listbox').waitFor();
    const backConsumed=await page.evaluate(()=>!window.dispatchEvent(new Event('ipc:gamepad-back',{cancelable:true})));assert(backConsumed,'B must be consumed by the open color menu');await page.getByRole('listbox').waitFor({state:'detached'});
    assert(await page.locator('.color-field').nth(1).locator('button').first().evaluate(el=>document.activeElement===el),'B returns focus to the right color');
    assert.equal(await page.evaluate(()=>window.f.applies.length),3,'Opening/cancelling a color menu must not write lighting');
    colorControls.push({id:row.name+'-production-navigation-'+height,status:'PASS',...navigation,hardwareIo:false});saveColorControls();
    const bounds=await page.locator('.light-bubble').boundingBox();assert(bounds&&bounds.x>=0&&bounds.x+bounds.width<=1280,'Both color controls remain inside the viewport');
    if(row.name==='ClawA1M')await page.screenshot({path:path.join(out,'claw-a1m-two-colors-'+height+'.png'),fullPage:true});
    colorControls.push({id:row.name+'-independent-colors-'+height,status:'PASS',left:leftEdited.color,right:leftEdited.color2,hardwareIo:false});saveColorControls();
   }
  }
 }
 await page.evaluate(()=>{window.__lightingRow=undefined;window.unmount()});
`;
const tests=colorTests.replace('LIGHTING_ROWS',JSON.stringify(lightingRows.map(({name,deviceClass,brightness,secondColor,declaredEffects,presetDeclared,presetAvailable,presets,fixedSpeed,presetSecondColor})=>({name,deviceClass,brightness,secondColor,declaredEffects,presetDeclared,presetAvailable,presets,fixedSpeed,presetSecondColor}))))+String.raw`
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
const stable=hash(fs.readFileSync(component))===hash(sourceBefore)&&hash(fs.readFileSync(apiPath))===hash(apiBefore)&&hash(fs.readFileSync(catalogPath))===hash(catalogBefore)&&navigationInputs.every(p=>hash(fs.readFileSync(path.join(repo,p)))===navigationBefore[p]);
fs.writeFileSync(path.join(out,'summary.json'),JSON.stringify({allPassed:child.status===0&&stable,exit:child.status,signal:child.signal,error:child.error?.message,source:component,sourceSha256:hash(sourceBefore),apiSource:apiPath,apiSourceSha256:hash(apiBefore),sourceStable:stable,fixtureBaselineSha256:hash(fs.readFileSync(path.join(repo,'tools/controller_lighting_operations_browser_selftest.mjs'))),hardwareIo:false,installedApplicationStarted:false,scope:'Real Vue/controls and production API wrappers at 1280x720 and 1280x800; only IPC/settings are in memory; no native/physical device acceptance'},null,2));
console.log('LIGHTING_LIFECYCLE',child.status===0&&stable?'PASS':'FAIL','evidence='+out);
if(child.status!==0||!stable)process.exitCode=1;
