import {controllerPageCases} from './controllerPageCases.mjs';
import {fxSoundRecoveryCases} from './fxSoundRecoveryCases.mjs';
import {backgroundPreviewCases} from './backgroundPreviewCases.mjs';
import {batterySettingsCases} from './batterySettingsCases.mjs';
import {steamDesktopMouseCases} from './steamDesktopMouseCases.mjs';
import {steamDiagnosticsCases} from './steamDiagnosticsCases.mjs';
import {trainerMenuCases} from './trainerMenuCases.mjs';
import {fanStatusCases} from './fanStatusCases.mjs';
import {radialCases} from './radialCases.mjs';
import {monitorEditorCases} from './monitorEditorCases.mjs';
import {naturalNavigationCases} from './naturalNavigationCases.mjs';
import {scrollStabilityCases} from './scrollStabilityCases.mjs';
import {gyroNavigationCases,gyroFullscreenCases,gyroPowerRecoveryCases,gyroPresetSwitchCases} from './gyroNavigationCases.mjs';
import {virtualStartProtectionCases} from './virtualStartProtectionCases.mjs';
import os from 'node:os';import {createHash} from 'node:crypto';import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {createRequire} from 'node:module';
const uiGuarded=Boolean(process.env.YMCC_UI_TOOL_HELPER_URL||process.env.YMCC_SIM_GLOBAL_RUN==='1');
if(uiGuarded&&(!process.env.YMCC_UI_TOOL_HELPER_URL||!process.env.YMCC_UI_TOOL_MANIFEST||!process.env.YMCC_UI_SOURCE_ROOT))throw Error('UI_TOOL_STAGE_CONTEXT_REQUIRED');
const uiGuard=uiGuarded?await import(process.env.YMCC_UI_TOOL_HELPER_URL):null;
const uiTools=uiGuard?.loadUiToolDeclaration({repoRoot:process.env.YMCC_UI_SOURCE_ROOT,manifestPath:process.env.YMCC_UI_TOOL_MANIFEST});
const req=createRequire(uiTools?.manifest.playwrightCreateRequireAnchor||process.env.YMCC_SIM_BROWSER_RUNTIME_ENTRY||path.join(os.homedir(),'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/_ymcc_ui.cjs')),{chromium}=req('playwright');
if(uiTools)uiGuard.verifyUiNodeAndCjs(uiTools,req.cache);

const task=import.meta.dirname;
const uiScope=process.env.YMCC_UI_SCOPE||'all';
assert.ok(['all','sleep'].includes(uiScope),'Unsupported UI scope');
const proofInputs=['backgroundPreviewCases.mjs','site/settings-preview/SteamDeck.mp4','controllerPageCases.mjs','fxSoundRecoveryCases.mjs','batterySettingsCases.mjs','steamDesktopMouseCases.mjs','steamDiagnosticsCases.mjs','trainerMenuCases.mjs','radialUtilitiesFixture.ts','radialUtilityCases.mjs','radialInputFixture.ts','radialInputCases.mjs','radialFanCases.mjs','fanStatusCases.mjs','fanStatusAudit-hook.inc.ts','radialTouchCases.mjs','radialGestureCases.mjs','radialCases.mjs','monitorEditorCases.mjs','virtualStartProtectionCases.mjs','performanceCombinedNavigation.mjs','naturalNavigationCases.mjs','scrollStabilityCases.mjs','gyroNavigationCases.mjs','build-result.json','site/index.html','site/product.html','site/product.js','site/product.css','verify.mjs','engineAudit.ts','entry.ts','memoryIpc.ts','scheduleFixture.ts','policyFixture.ts','monitorFixture.ts','sleepPageFixture.ts','sleepRecoveryFixture.ts','Shell.vue','app-shell.css'];
proofInputs.push(...fs.readdirSync(path.join(task,'site/performance-simulation'),{recursive:true,withFileTypes:true}).filter(e=>e.isFile()).map(e=>path.relative(task,path.join(e.parentPath||e.path,e.name))));
proofInputs.push(...fs.readdirSync(path.join(task,'site/gyro-simulation'),{recursive:true,withFileTypes:true}).filter(e=>e.isFile()).map(e=>path.relative(task,path.join(e.parentPath||e.path,e.name))));
const hash=file=>createHash('sha256').update(fs.readFileSync(path.join(task,file))).digest('hex');
const testedInputs=Object.fromEntries(proofInputs.map(file=>[file,hash(file)]));
const {url}=JSON.parse(fs.readFileSync(path.join(task,'server.json')));
const uiBrowserServer=await chromium.launchServer(uiTools?{executablePath:uiTools.executables.edge,headless:true}:{channel:'msedge',headless:true});
const uiBrowserProcess=uiBrowserServer.process();const uiBrowserTerminal=new Promise(resolve=>uiBrowserProcess.once('close',(exitCode,signal)=>resolve({exitCode,signal})));
const browser=await chromium.connect(uiBrowserServer.wsEndpoint()),states=[],actions=[],failures=[],pageErrors=[],shortcutRetirement=[];

// Optional Node monotonic timing stays outside browser virtual clocks.
// It observes the existing UI suite; assertions, cases and deadlines are unchanged.
const uiTimingEnabled=process.env.YMCC_UI_TIMING==='1',uiTimingEpoch=uiTimingEnabled?process.hrtime.bigint():0n;
function uiTiming(phase,name,height,started){if(uiTimingEnabled){const now=process.hrtime.bigint();console.log('UI_TIMING '+JSON.stringify({phase,name,height,wallMs:Number(now-uiTimingEpoch)/1e6,...(started===undefined?{}:{durationMs:Number(now-started)/1e6})}));}}
// Run the two required viewports in independent browser contexts.
// A bounded sleep run reuses the same real page, engine and assertions.
async function runViewport(height){
 const context=await browser.newContext({viewport:{width:1400,height:1130}}),page=await context.newPage();
 const viewportTimingStart=uiTimingEnabled?process.hrtime.bigint():0n;uiTiming('viewport-begin','full-suite',height);
 try{
page.on('pageerror',error=>pageErrors.push(String(error)));
async function open(route,height,extra={}){const timingStart=uiTimingEnabled?process.hrtime.bigint():0n;uiTiming('open-begin',route,height);await page.goto(url+'?'+new URLSearchParams({route,height,theme:'blue-black',...extra}));const f=await(await page.$('#product')).contentFrame();if(extra.controllerScope||extra.shortcutSeed||extra.virtualStartSeed||extra.keepAlive||extra.nativeWidth||extra.trainerMenu||extra.steamMouseSeed){const seededUrl=new URL(f.url());for(const key of ['controllerScope','steamMouseSeed','trainerMenu','shortcutSeed','virtualStartSeed','keepAlive','radialRecorder','nativeWidth','widthHold'])if(extra[key])seededUrl.searchParams.set(key,extra[key]);await f.goto(seededUrl.href);}try{await f.waitForFunction(()=>window.__previewReady&&window.__gamepadAudit,null,{timeout:10000});}catch(error){fs.writeFileSync(path.join(task,'load-failure.json'),JSON.stringify({route,height,url:f.url(),state:await f.evaluate(()=>({ready:window.__previewReady,errors:window.__previewErrors,calls:window.__previewCalls?.slice(-12),html:document.body.innerHTML.slice(0,1000)}))},null,2));throw error;}await page.waitForTimeout(140);uiTiming('open-end',route,height,timingStart);return f;}
async function command(f,action){await f.evaluate(action=>window.__gamepadAudit.command(action),action);await page.waitForTimeout(50);}
async function press(f,locator){await locator.evaluate(el=>{if(!window.__gamepadAudit.focusables().includes(el))throw Error('Setup target outside current gamepad ownership: '+el.textContent);window.__gamepadAudit.focus(el);});await command(f,'confirm');}
async function state(f,name,height,root='.route-view'){
 const timingStart=uiTimingEnabled?process.hrtime.bigint():0n;uiTiming('state-begin',name,height);
 const result=await f.evaluate(({root})=>{
  const a=window.__gamepadAudit;a.reset();const all=a.focusables(),host=document.querySelector(root);if(!host)throw Error('Missing host '+root);const els=all.filter(el=>host.contains(el));
  const label=el=>el?.getAttribute('aria-label')||el?.innerText?.replace(/\s+/g,' ').trim().slice(0,80)||el?.getAttribute('type')||el?.tagName;const nodes=els.map(el=>({label:label(el),edges:[]})),issues=[];const dirs=['nav-left','nav-right','nav-up','nav-down'];
  for(let i=0;i<els.length;i++)for(let d=0;d<4;d++){
   const el=els[i];el.focus({preventScroll:true});a.command(dirs[d]);const target=document.activeElement,to=els.indexOf(target);nodes[i].edges.push(to);
   if(target?.closest('.navrail,[data-gp-ignore],[hidden],[inert],[aria-hidden="true"]'))issues.push({kind:'excluded-target',from:label(el),to:label(target),direction:dirs[d]});
   if(to<0||to===i)continue;const b=a.rect(el),r=a.rect(target),fanGraph=el.matches('.chart-node-hit')&&target.matches('.chart-node-hit');
   if(d<2&&!fanGraph&&Math.min(b.bottom,r.bottom)-Math.max(b.top,r.top)<.5)issues.push({kind:'horizontal-row',from:label(el),to:label(target),direction:dirs[d]});
   if(d>=2&&((r.top+r.height/2)-(b.top+b.height/2))*(d===2?-1:1)<1)issues.push({kind:'vertical-order',from:label(el),to:label(target),direction:dirs[d]});
   if(d>=2&&el.closest('[data-gp-spatial="nearest"]')){
    const sign=d===2?-1:1,scope=el.closest('[data-gp-scope]'),pane=el.closest('[data-gp-pane]');let items=els.filter(e=>e!==el&&e.closest('[data-gp-scope]')===scope).map(e=>{const r=a.rect(e);return {e,gap:sign>0?r.top-b.bottom:b.top-r.bottom,forward:((r.top+r.height/2)-(b.top+b.height/2))*sign};}).filter(c=>c.forward>1&&c.gap>=-.5);
    if(pane){const local=items.filter(c=>c.e.closest('[data-gp-pane]')?.getAttribute('data-gp-pane')===pane.getAttribute('data-gp-pane'));items=local.length?local:items.filter(c=>!c.e.closest('[data-gp-pane]'));}
    const delta=((r.top+r.height/2)-(b.top+b.height/2))*sign,min=Math.min(...items.map(c=>c.forward));if(delta>min+Math.max(6,Math.min(12,b.height/2))+1)issues.push({kind:'skipped-visual-row',from:label(el),to:label(target),direction:dirs[d],delta,min});
   }
  }
  const seen=new Map([[0,[]]]),q=[0];while(q.length){const i=q.shift();for(let d=0;d<4;d++){const j=nodes[i]?.edges[d];if(j>=0&&!seen.has(j)){seen.set(j,[...seen.get(i),dirs[d]]);q.push(j);}}}
  const reverse=new Set([0]),rq=[0];while(rq.length){const j=rq.shift();for(let i=0;i<nodes.length;i++)if(nodes[i].edges.includes(j)&&!reverse.has(i)){reverse.add(i);rq.push(i);}}
  for(let i=0;i<els.length;i++){
   if(!seen.has(i)){issues.push({kind:'unreachable',to:nodes[i].label});continue;}if(!reverse.has(i))issues.push({kind:'no-return',from:nodes[i].label});
   a.focus(els[0]);for(const action of seen.get(i))a.command(action);if(document.activeElement!==els[i])issues.push({kind:'walk-mismatch',to:nodes[i].label,actual:label(document.activeElement),path:seen.get(i)});
   const b=els[i].getBoundingClientRect();let left=0,top=0,right=innerWidth,bottom=innerHeight;for(let p=els[i].parentElement;p;p=p.parentElement){const c=getComputedStyle(p),r=p.getBoundingClientRect();if(/auto|scroll|hidden|clip/.test(c.overflowY)){top=Math.max(top,r.top);bottom=Math.min(bottom,r.bottom);}if(/auto|scroll|hidden|clip/.test(c.overflowX)){left=Math.max(left,r.left);right=Math.min(right,r.right);}}
   if(b.left<left-2||b.right>right+2||b.top<top-2||b.bottom>bottom+2)issues.push({kind:'clipped-focus',to:nodes[i].label,box:{left:b.left,right:b.right,top:b.top,bottom:b.bottom},clip:{left,right,top,bottom}});
  }
  return {count:els.length,directions:els.length*4,issues,nodes,errors:window.__previewErrors};
 },{root});uiTiming('state-end',name,height,timingStart);states.push({name,height,...result});console.log(name,height,result.count,'stops',result.issues.length,'issues');if(result.issues.length)failures.push({name,height,issues:result.issues});return result;
}
async function shot(name){await page.locator('.screen').screenshot({path:path.join(task,'evidence',name+'.png')});}
async function actionCase(name,height,fn){const timingStart=uiTimingEnabled?process.hrtime.bigint():0n;uiTiming('action-begin',name,height);try{await fn();actions.push({name,height,pass:true});}catch(error){failures.push({name,height,error:String(error)});console.log('ACTION FAILED',name,String(error));}finally{uiTiming('action-end',name,height,timingStart);}}
const remainingDefaultKeys=['enabled','bDoubleMinimize','tdpShortcut','fpsShortcut','killGame','openKeyboard','returnDesktop','mouseToggle'];
const memoryShortcutRules=f=>f.evaluate(()=>window.__shortcutFixture.read().input.buttonMapping.rules.shortcutRules);
function assertSavedShortcuts(rules,custom){
 assert.deepEqual(rules.filter(r=>r.origin==='native-default').map(r=>r.defaultKey).sort(),[...remainingDefaultKeys].sort(),'all remaining defaults survive; retired native-default is absent');
 const f7=rules.filter(r=>r.actionId.startsWith('keyboard.')&&r.params.key==='F7');
 assert.equal(f7.length,custom?1:0,'F7 survives only as the explicitly supplied custom rule');
 if(custom){assert.equal(f7[0].id,'custom-start-f7');assert.equal(f7[0].origin,'custom');assert.equal(f7[0].defaultKey,'startDoubleF7');assert.equal(f7[0].enabled,true);assert.equal(f7[0].trigger,'double');assert.deepEqual(f7[0].inputs,[{source:'controller',code:'start'}]);}
}
async function shortcutCardRows(f){
 const rows=[];
 for(let p=0;p<8;p++){
  rows.push(...await f.locator('.shortcut-toggle').evaluateAll(els=>els.map(el=>({title:el.querySelector('b').textContent,meta:el.querySelector('small').textContent}))));
  const next=f.locator('.shortcut-pager').getByRole('button',{name:'下一页 ›',exact:true});
  if(!await next.count()||await next.isDisabled())break;
  await press(f,next);
 }
 const previous=f.locator('.shortcut-pager').getByRole('button',{name:'‹ 上一页',exact:true});
 while(await previous.count()&&await previous.isEnabled())await press(f,previous);
 return rows;
}
async function checkShortcutRetirement(height){
 for(const seed of ['empty','legacy-flag-only','legacy-default','legacy-custom'])await actionCase('Controller Start/F7 retirement / '+seed,height,async()=>{
  const custom=seed==='legacy-custom',f=await open('/button-mapping',height,{shortcutSeed:seed});
  await f.waitForFunction(()=>document.querySelectorAll('.shortcut-toggle').length>=8);
  const before=await memoryShortcutRules(f),card=await shortcutCardRows(f);
  assert.equal(card.length,custom?9:8,'retired default is not a card, even with legacy enabled=true');
  assert.equal(card.filter(r=>r.meta.startsWith('默认 ·')).length,8,'other default cards remain');
  assert.equal(card.filter(r=>r.title==='数字键盘').length,custom?1:0,'only the custom F7 card remains');
  await state(f,'controller/start-f7/'+seed+'/card',height);
  await f.locator('.shortcut-toggle').first().evaluate(el=>window.__gamepadAudit.focus(el));await shot('start-f7-'+seed+'-card-'+height);
  if(custom){await press(f,f.locator('.shortcut-pager').getByRole('button',{name:'下一页 ›',exact:true}));await state(f,'controller/start-f7/custom/card-page2',height);await shot('start-f7-custom-card-page2-'+height);}
  await press(f,f.getByRole('button',{name:'打开编辑器',exact:true}));await f.locator('.editor-overlay').waitFor();
  await f.waitForFunction(()=>document.querySelectorAll('.rule-row').length>=8);
  const editor=await f.locator('.rule-row').allTextContents();
  assert.equal(editor.length,custom?9:8);assert.equal(editor.filter(text=>text.includes('键盘 · F7')).length,custom?1:0,'editor excludes retired F7 while reading custom F7');
  // Loading reconciles the view; saving later removes the retired record from storage.
  assert.deepEqual(await memoryShortcutRules(f),before,'loading the UI does not need to rewrite shortcut storage');
  await state(f,'controller/start-f7/'+seed+'/editor',height,'.editor-overlay');await shot('start-f7-'+seed+'-editor-'+height);
  if(custom){
   await press(f,f.locator('.rule-row').filter({hasText:'键盘 · F7'}));
   await press(f,f.locator('.action-manage-cancel'));
   await press(f,f.locator('.output-summary'));await f.locator('.keyboard-output-dialog').waitFor();
   const key=f.locator('.keyboard-hit-layer button[data-key-code="F7"]');assert.equal(await key.isEnabled(),true,'custom output still offers F7');
   await command(f,'back');assert.equal(await f.locator('.keyboard-output-dialog').count(),0);
   // Re-selecting an output intentionally resets its trigger. Toggle twice instead,
   // so this preservation scenario saves without changing the custom double trigger.
   await press(f,f.locator('.rule-row').filter({hasText:'键盘 · F7'}));
   await press(f,f.locator('.action-controls [role="switch"]'));await press(f,f.locator('.action-controls [role="switch"]'));
   await press(f,f.locator('.action-manage-cancel'));
  }else{
   await press(f,f.locator('[data-editor-action="restore"]'));
   assert.equal(await f.locator('.rule-row').filter({hasText:'键盘 · F7'}).count(),0,'restore defaults does not resurrect F7');
  }
  await press(f,f.locator('[data-editor-action="apply"]'));
  await f.waitForFunction(()=>document.querySelector('[data-editor-action="apply"]')?.disabled);
  const afterEditor=await memoryShortcutRules(f);assertSavedShortcuts(afterEditor,custom);
  await command(f,'back');assert.equal(await f.locator('.editor-overlay').count(),0);
  // Exercise the card's actual CAS/persist consumer as well as the editor's apply consumer.
  await shortcutCardRows(f);
  await press(f,f.locator('.shortcut-toggle').first());
  await f.waitForFunction(()=>!document.querySelector('.shortcut-toggle')?.disabled);
  const afterCard=await memoryShortcutRules(f);assertSavedShortcuts(afterCard,custom);
  await f.evaluate(()=>window.__navigate('/fan'));await f.locator('.fan-page').waitFor();
  await f.evaluate(()=>window.__navigate('/button-mapping'));await f.locator('.shortcut-toggle').first().waitFor();
  const reenteredCard=await shortcutCardRows(f);assert.equal(reenteredCard.length,custom?9:8);
  await press(f,f.getByRole('button',{name:'打开编辑器',exact:true}));await f.locator('.rule-row').first().waitFor();
  assert.equal(await f.locator('.rule-row').count(),custom?9:8);assert.equal(await f.locator('.rule-row').filter({hasText:'键盘 · F7'}).count(),custom?1:0);
  assert.deepEqual(await memoryShortcutRules(f),afterCard,'reenter remains stable without resurrecting retired defaults');
  await state(f,'controller/start-f7/'+seed+'/reentered',height,'.editor-overlay');
  const calls=await f.evaluate(()=>window.__previewCalls.filter(c=>['settings.write','summon.set'].includes(c.cmd)));
  assert.ok(calls.some(c=>c.cmd==='settings.write'),'real UI persisted through memory settings boundary');
  assert.ok(calls.filter(c=>c.cmd==='summon.set').every(c=>!Object.hasOwn(c.args,'startDoubleF7')),'retired switch is not re-enabled on save');
  shortcutRetirement.push({height,seed,card,editor,before,afterEditor,afterCard,reenteredCard,calls});
  await command(f,'back');
 });
}


  let f;
  if(uiScope==='all'){
  await controllerPageCases({height,open,command,press,actionCase,state,page,task});
  await naturalNavigationCases({page,height,open,command,press,actionCase,state,task,recordState:result=>{states.push(result);if(result.issues.length)failures.push({name:result.name,height,issues:result.issues});}});
  await scrollStabilityCases({page,height,open,command,press,actionCase,task});
  await gyroPresetSwitchCases({page,height,open,command,press,actionCase,task});
  await gyroFullscreenCases({page,height,open,command,press,actionCase,task});
  await gyroPowerRecoveryCases({page,height,open,command,press,actionCase,task});
  for(const route of ['/fan','/schedule','/button-mapping','/gyro-motion','/settings','/power','/rtss','/steam','/sleep','/quick','/radial']){const f=await open(route,height);await state(f,route,height);}
  await actionCase('Controller inventory advisory is neutral and navigable',height,async()=>{
   const f=await open('/button-mapping',height);
   await f.evaluate(()=>window.__emitNativeEvent('gamepad.state',{driverSetup:8,backendFaultHold:false}));
   const note=f.locator('.persona-block .environment-note');await note.waitFor();
   assert.equal(await note.getAttribute('role'),'status');
   assert.match(await note.innerText(),/驱动清单格式暂未识别，自动维护未执行/);
   assert.match(await note.innerText(),/实际创建和输入结果/);
   assert.equal(await f.locator('.persona-block .fault-line').count(),0);
   const tone=await note.evaluate(el=>{const strong=el.querySelector('strong'),probe=document.createElement('span');probe.style.color='var(--text)';el.append(probe);const result={actual:getComputedStyle(strong).color,expected:getComputedStyle(probe).color,focusable:window.__gamepadAudit.focusables().some(e=>el.contains(e))};probe.remove();return result;});
   assert.equal(tone.actual,tone.expected);assert.equal(tone.focusable,false);
   for(const button of await f.locator('.persona-actions .persona-btn').all())assert.equal(await button.isEnabled(),true);
   await state(f,'controller/inventory-unverified',height);
   await shot('inventory-unverified-'+height);
  });
  await actionCase('Controller inventory advisory preserves actual backend failure',height,async()=>{
   const f=await open('/button-mapping',height);
   await f.evaluate(()=>window.__emitNativeEvent('gamepad.state',{driverSetup:8,backendFaultHold:true,virtualOutput:{state:'failed',reason:'target-create-failed',requestedPersona:'steamdeck',boundPersona:'steamdeck',hostAlive:true,prepared:false,frameAccepted:false,sourceLive:false}}));
   await f.locator('.environment-note').waitFor();
   const fault=f.locator('.persona-block .fault-line');await fault.waitFor();
   assert.match(await fault.innerText(),/虚拟输出已停写/);
   assert.match(await fault.innerText(),/后端创建或输入输出失败/);
   await shot('inventory-unverified-backend-failure-'+height);
  });
  await actionCase('Controller environment advisories stay separate from backend faults',height,async()=>{
   const f=await open('/button-mapping',height);
   // Only current report-confirmed notices identify a HidHide installer failure.
   // Preserve confirmed warnings; generic/unknown receipts remain advisories.
   const statuses=[[1,undefined,'正在准备手柄环境',true],[3,undefined,'后台环境检查未能确认结果',true],[4,undefined,'需要重启',false],[5,undefined,'耗时较长',true],[6,5,'缺少 HidHide 安装器',false],[7,6,'HidHide 安装器校验失败',false],[6,undefined,'手柄环境准备未确认',true],[7,undefined,'手柄环境准备未确认',true],[6,0,'手柄环境准备未确认',true],[7,0,'手柄环境准备未确认',true],[6,8,'手柄环境准备未确认',true],[7,8,'手柄环境准备未确认',true],[8,undefined,'驱动清单格式暂未识别',true],[9,0,'手柄驱动维护已暂缓',true],[9,8,'手柄环境状态未确认',true],[999,undefined,'手柄环境状态未确认',true]];
   for(const [driverSetup,hidHideSetupNotice,text,advisory] of statuses)for(const backendFaultHold of [true,false]){
    await f.evaluate(({driverSetup,hidHideSetupNotice,backendFaultHold})=>window.__emitNativeEvent('gamepad.state',{driverSetup,hidHideSetupNotice,backendFaultHold,virtualOutput:{state:'active',requestedPersona:'steamdeck',boundPersona:'steamdeck',hostAlive:true,prepared:true,frameAccepted:true,sourceLive:true}}),{driverSetup,hidHideSetupNotice,backendFaultHold});
    await f.waitForFunction(({text,backendFaultHold})=>{const block=document.querySelector('.persona-block');return block?.textContent.includes(text)&&block.textContent.includes('虚拟输出已停写')===backendFaultHold;},{text,backendFaultHold});
    const notes=f.locator('.persona-block .environment-note'),faults=f.locator('.persona-block .fault-line');
    assert.equal(await notes.count(),advisory?1:0);
    assert.equal(await faults.count(),(advisory?0:1)+(backendFaultHold?1:0));
    if(advisory){
     const note=notes.first();assert.equal(await note.getAttribute('role'),'status');
     assert.match(await note.innerText(),new RegExp(text));
     if(driverSetup===9&&hidHideSetupNotice===0){
      const description=await note.innerText();
      assert.match(description,/驱动包存在不代表当前设备已绑定或输入正常/);
      assert.match(description,/请以实际创建和输入结果为准/);
      assert.doesNotMatch(description,/虚拟输出已停写|后端创建或输入输出失败/);
      assert.equal(await f.locator('.persona-actions .persona-btn').count(),4);
     }
     assert.equal(await note.evaluate(el=>window.__gamepadAudit.focusables().some(f=>f===el||el.contains(f))),false);
     assert.equal(await note.evaluate(el=>el.matches('.fault-line')),false);
     if(backendFaultHold){const colors=await f.evaluate(()=>({note:getComputedStyle(document.querySelector('.environment-note strong')).color,fault:getComputedStyle(document.querySelector('.persona-block .fault-line strong')).color}));assert.notEqual(colors.note,colors.fault);}
    }else assert.match(await faults.first().innerText(),new RegExp(text));
    if(backendFaultHold){assert.match(await faults.last().innerText(),/虚拟输出已停写/);assert.match(await faults.last().innerText(),/后端创建或输入输出失败/);}
    for(const button of await f.locator('.persona-actions .persona-btn').all())assert.equal(await button.isEnabled(),true);
   }
  });
  await radialCases({height,open,command,press,actionCase,state,page,task});
  await trainerMenuCases({height,open,command,press,actionCase,state,page,task});
  await steamDiagnosticsCases({height,open,command,press,actionCase,state,page,task});
  await steamDesktopMouseCases({height,open,command,press,actionCase,state,page,task});
  f=await open('/fan',height);
  await actionCase('Fan header one row / state not focusable',height,async()=>{const r=await f.evaluate(()=>{const h=document.querySelector('.console-heading h1').getBoundingClientRect(),s=document.querySelector('.fan-status-banner').getBoundingClientRect();return {same:Math.abs(h.top+h.height/2-s.top-s.height/2)<5,nested:!!document.querySelector('.console-heading .fan-status-banner'),focus:window.__gamepadAudit.focusables().some(el=>el.closest('.fan-status-banner'))};});assert.ok(r.same&&r.nested&&!r.focus);});
  await f.evaluate(()=>document.querySelector('.app-content').scrollTo({top:0}));await shot('fan-'+height);
  await actionCase('Fan dropdown native A/up/down/B',height,async()=>{const drop=f.locator('.controls-card .dd-trigger').first();await press(f,drop);assert.equal(await f.locator('[role="listbox"]').count(),1);await command(f,'nav-down');await command(f,'back');await f.waitForFunction(()=>!document.querySelector('[role="listbox"]'));assert.equal(await f.locator('[role="listbox"]').count(),0);assert.ok(await drop.evaluate(el=>document.activeElement===el));});
  await actionCase('Fan curve A edit, direction edits, B exits',height,async()=>{const node=f.locator('.chart-node-hit').nth(1);await press(f,node);assert.equal(await node.getAttribute('data-gp-inline-edit-active'),'true');const label=await node.getAttribute('aria-label');await command(f,'nav-up');assert.notEqual(await node.getAttribute('aria-label'),label);assert.ok(await node.evaluate(el=>document.activeElement===el));await command(f,'back');assert.notEqual(await node.getAttribute('data-gp-inline-edit-active'),'true');});
  await fanStatusCases({height,open,actionCase,page,shot,task});
  f=await open('/schedule',height);await press(f,f.getByRole('button',{name:'手动模式',exact:true}));await state(f,'performance/manual/dedicated',height);
  await actionCase('Performance slider A edit LT/RT B rollback',height,async()=>{const slider=f.locator('input[type="range"]').first();const before=await slider.inputValue();await press(f,slider);assert.ok(await f.evaluate(()=>window.__gamepadAudit.editing()));await command(f,'slider-decrease');assert.notEqual(await slider.inputValue(),before);await command(f,'back');assert.equal(await slider.inputValue(),before);assert.equal(await f.evaluate(()=>window.__gamepadAudit.editing()),false);});
  await press(f,f.getByRole('button',{name:'电池模式',exact:true}));await state(f,'performance/manual/battery',height);
  await actionCase('Performance dedicated deletion dialog B restore',height,async()=>{const trigger=f.locator('[data-performance-remove]');await press(f,trigger);await f.locator('.profile-remove-dialog').waitFor();await state(f,'performance/delete-confirm',height,'.profile-remove-dialog');await command(f,'back');assert.equal(await f.locator('.profile-remove-dialog').count(),0);assert.ok(await trigger.evaluate(el=>document.activeElement===el));});
  await f.evaluate(async()=>{window.__fixture.profileDeleted=true;window.__setPolicyGame(null);await window.__profileChanged();});await page.waitForTimeout(100);await press(f,f.getByRole('button',{name:'手动模式',exact:true}));assert.equal(await f.locator('[data-performance-remove]').count(),0);assert.equal(await f.locator('.manual-bubble').count(),1);await state(f,'performance/manual/global',height);await press(f,f.getByRole('button',{name:'自动模式',exact:true}));await state(f,'performance/auto/global',height);
  await actionCase('Native LB/RB route commands, rail excluded',height,async()=>{await command(f,'page-next');await f.locator('.fan-page').waitFor();await command(f,'page-prev');await f.locator('.continuous-performance').waitFor();assert.equal(await f.evaluate(()=>window.__gamepadAudit.focusables().some(el=>el.closest('.navrail'))),false);});
  f=await open('/schedule',height);await command(f,'edit-game');await f.locator('[data-gp-game-quick-menu]').waitFor();await state(f,'top-game-menu/collapsed',height,'[data-gp-game-quick-menu]');
  await press(f,f.locator('[data-gp-custom-entry]'));await page.waitForTimeout(130);await state(f,'top-game-menu/dedicated/auto',height,'[data-gp-game-quick-menu]');await press(f,f.locator('[data-gp-game-quick-menu]').getByRole('button',{name:'手动模式',exact:true}));assert.equal(await f.locator('[data-gp-game-quick-menu] .manual-bubble').count(),1);await state(f,'top-game-menu/dedicated/manual',height,'[data-gp-game-quick-menu]');await shot('dedicated-'+height);await command(f,'back');
  f=await open('/button-mapping',height,{shortcuts:'paged'});await press(f,f.getByRole('button',{name:'下一页 ›',exact:true}));await state(f,'controller/shortcuts-page2',height);await press(f,f.getByRole('button',{name:'打开编辑器',exact:true}));await f.locator('.editor-overlay').waitFor();await state(f,'controller/editor',height,'.editor-overlay');await shot('controller-editor-'+height);
  await actionCase('Controller editor B returns to original page',height,async()=>{await command(f,'back');assert.equal(await f.locator('.editor-overlay').count(),0);assert.ok(await f.locator('.controller-page').isVisible());});
  await checkShortcutRetirement(height);
  await virtualStartProtectionCases({page,height,open,command,press,actionCase,state});
  }
  f=await open('/sleep',height);
  await actionCase('Sleep abnormal recovery retains switch, concise disclosure, and removes long timing note',height,async()=>{
    await press(f,f.locator('[data-settings-section="protection"]'));
    const powerChange=f.getByRole('switch',{name:'意外唤醒重睡',exact:true});
    assert.equal(await powerChange.count(),1);
    assert.match(await powerChange.locator('xpath=ancestor::*[contains(@class,"toggle-row")]').locator('.toggle-desc').innerText(),/电源变动/);
    await press(f,powerChange);assert.equal(await f.evaluate(()=>window.__sleepFixture.retryOnNonUserWake),false);
    await press(f,powerChange);assert.equal(await f.evaluate(()=>window.__sleepFixture.retryOnNonUserWake),true);
    const toggle=f.getByRole('switch',{name:'入睡失败重试',exact:true});
    assert.equal(await toggle.count(),1);
    assert.equal(await f.getByRole('note').filter({hasText:'30秒'}).count(),0);
    const description=await toggle.locator('xpath=ancestor::*[contains(@class,"toggle-row")]').locator('.toggle-desc').innerText();
    assert.equal(description,'明确入睡失败时重试，电源键唤醒优先');
    for(const value of ['休眠','关机','持续异常'])assert.equal(description.includes(value),false);
    await press(f,toggle);assert.equal(await f.evaluate(()=>window.__sleepFixture.retryOnEntryFailure),false);
    await press(f,toggle);assert.equal(await f.evaluate(()=>window.__sleepFixture.retryOnEntryFailure),true);
  });
  await actionCase('Sleep static wake has no immediate scan, refreshes native completion, and preserves power repair',height,async()=>{
    const summary=f.getByRole('status',{name:'静态唤醒屏蔽状态',exact:true});
    const repair=f.getByRole('switch',{name:'睡眠电源修复',exact:true});
    const scan=f.getByRole('switch',{name:'自动扫描并屏蔽',exact:true});
    assert.match(await summary.innerText(),/已开启自动扫描.*等待单次扫描/);
    assert.equal(await scan.getAttribute('aria-checked'),'true');
    assert.equal(await f.getByRole('button',{name:'扫描并屏蔽',exact:true}).count(),0);
    assert.equal(await f.getByRole('button',{name:'重试恢复',exact:true}).count(),0);
    assert.deepEqual(await f.evaluate(()=>window.__staticWakeFixture.calls.filter(c=>c!=='get')),[]);
    assert.deepEqual(await f.evaluate(()=>window.__staticWakeFixture.preferenceWrites),[]);
    assert.equal(await repair.getAttribute('aria-checked'),'true');
    assert.equal(await scan.locator('xpath=ancestor::*[contains(@class,"toggle-row")]').locator('.toggle-desc').innerText(),'开启后及开机、唤醒1分钟后单次扫描');
    assert.equal(await repair.locator('xpath=ancestor::*[contains(@class,"toggle-row")]').locator('.toggle-desc').innerText(),'启动及电源变动时修正睡眠设置');
    const getsBeforeAuto=await f.evaluate(()=>window.__staticWakeFixture.calls.filter(c=>c==='get').length);
    await f.evaluate(()=>window.__staticWakeFixture.completeAuto({enabled:true,phase:'applied',lastScanEpoch:1,devices:4,software:5,timers:1,protectedDevices:2}));
    await summary.filter({hasText:'设备 4'}).waitFor();
    assert.equal(await f.evaluate(()=>window.__staticWakeFixture.calls.filter(c=>c==='get').length),getsBeforeAuto+1);
    assert.deepEqual(await f.evaluate(()=>window.__staticWakeFixture.calls.filter(c=>c!=='get')),[]);
    // Power repair has its own persisted preference and never restores sources.
    await f.evaluate(()=>{window.__staticWakeFixture.failPlanSave=true});await press(f,repair);
    assert.equal(await repair.getAttribute('aria-checked'),'true');
    assert.equal(await f.evaluate(()=>window.__sleepFixture.plan),true);
    await f.evaluate(()=>{window.__staticWakeFixture.failPlanSave=false});
    await press(f,repair);assert.equal(await repair.getAttribute('aria-checked'),'false');
    assert.equal(await scan.getAttribute('aria-checked'),'true');
    assert.equal(await f.evaluate(()=>window.__sleepFixture.staticWakeAutoScanEnabled),true);
    assert.deepEqual(await f.evaluate(()=>window.__staticWakeFixture.calls.filter(c=>c!=='get')),[]);
    await press(f,repair);
    await f.evaluate(()=>{window.__staticWakeFixture.failAutoScanSave=true});await press(f,scan);
    assert.equal(await scan.getAttribute('aria-checked'),'true');
    assert.equal(await f.evaluate(()=>window.__sleepFixture.staticWakeAutoScanEnabled),true);
    assert.equal(await f.evaluate(()=>window.__staticWakeFixture.calls.filter(c=>c==='restore').length),0);
    await f.evaluate(()=>{window.__staticWakeFixture.failAutoScanSave=false});
    const getsBeforeOff=await f.evaluate(()=>window.__staticWakeFixture.calls.filter(c=>c==='get').length);
    await f.evaluate(()=>{const s=window.__staticWakeFixture;s.holdAutoScanSave=true;s.hold=true});await press(f,scan);
    await f.waitForFunction(()=>window.__staticWakeFixture.releaseAutoScanSave!==null);
    assert.equal(await repair.isDisabled(),true);
    assert.equal(await scan.isDisabled(),true);
    await scan.evaluate(el=>{el.click();el.click()});
    assert.deepEqual(await f.evaluate(()=>window.__staticWakeFixture.preferenceWrites),[false,false]);
    assert.equal(await f.evaluate(()=>window.__staticWakeFixture.calls.filter(c=>c==='restore').length),0);
    // Start an old ON read during the save; release it while restore is held.
    await f.evaluate(()=>{const s=window.__staticWakeFixture;s.holdPreferenceRead=true;void s.refresh()});
    await f.waitForFunction(()=>window.__staticWakeFixture.preferenceReadReleases.length===1);
    await f.evaluate(()=>{const s=window.__staticWakeFixture;s.holdAutoScanSave=false;s.releaseAutoScanSave()});
    await f.waitForFunction(()=>window.__staticWakeFixture.release!==null);
    assert.equal(await f.evaluate(()=>window.__sleepFixture.staticWakeAutoScanEnabled),false);
    assert.equal(await scan.getAttribute('aria-checked'),'false');
    assert.equal(await scan.isDisabled(),true);
    assert.equal(await repair.isDisabled(),true);
    await f.evaluate(()=>{const s=window.__staticWakeFixture;s.holdPreferenceRead=false;s.preferenceReadReleases.splice(0).forEach(release=>release())});
    await page.waitForTimeout(50);
    assert.equal(await scan.getAttribute('aria-checked'),'false','SAVE_ACK_MUST_RETIRE_OLD_ON_READ_BEFORE_RESTORE_ACK');
    await scan.evaluate(el=>{el.click();el.click()});
    assert.equal(await f.evaluate(()=>window.__staticWakeFixture.calls.filter(c=>c==='restore').length),1);
    assert.deepEqual(await f.evaluate(()=>window.__sleepWrites.filter(w=>w.name==='staticWakeAutoScanEnabled'||w.name==='staticWake')),[{name:'staticWakeAutoScanEnabled',value:false},{name:'staticWake',value:'restore'}]);
    await f.evaluate(()=>{const s=window.__staticWakeFixture;s.hold=false;s.release()});
    await summary.filter({hasText:'已恢复'}).waitFor();
    assert.equal(await f.getByRole('button',{name:'重试恢复',exact:true}).count(),0);
    assert.equal(await f.evaluate(()=>window.__staticWakeFixture.calls.filter(c=>c==='get').length),getsBeforeOff+2);
    await press(f,scan);assert.equal(await scan.getAttribute('aria-checked'),'true');
    assert.deepEqual(await f.evaluate(()=>window.__staticWakeFixture.calls.filter(c=>c!=='get')),['restore']);
    assert.equal(await f.evaluate(()=>window.__sleepFixture.plan),true);
    await f.evaluate(()=>window.__staticWakeFixture.completeAuto({enabled:true,phase:'applied',lastScanEpoch:2,devices:2,software:3,timers:1,protectedDevices:2}));
    await summary.filter({hasText:'设备 2'}).waitFor();assert.match(await summary.innerText(),/软件 3.*定时器 1.*保护 2/);
    await state(f,'sleep/static-auto-scan',height);
    await repair.evaluate(el=>window.__gamepadAudit.focus(el));await command(f,'nav-down');
    assert.equal(await scan.evaluate(el=>el===document.activeElement),true);
    await scan.evaluate(el=>window.__gamepadAudit.focus(el));await command(f,'nav-down');
    assert.equal(await f.getByRole('switch',{name:'入睡失败重试',exact:true}).evaluate(el=>el===document.activeElement),true);
    await command(f,'nav-up');assert.equal(await scan.evaluate(el=>el===document.activeElement),true);
    await f.evaluate(()=>window.__staticWakeFixture.completeAuto({phase:'partial',failed:1,lastError:'memory item refused'}));
    await summary.filter({hasText:'失败 1'}).waitFor();assert.match(await summary.innerText(),/部分完成.*设备 2/);
    assert.equal(await f.getByRole('button',{name:'重试恢复',exact:true}).count(),0);
    await f.evaluate(()=>{window.__staticWakeFixture.failure=true});await press(f,scan);
    await summary.filter({hasText:'待恢复 1'}).waitFor();
    assert.equal(await scan.getAttribute('aria-checked'),'false');
    assert.equal(await repair.getAttribute('aria-checked'),'true');
    await state(f,'sleep/static-restore-pending',height);
    await f.evaluate(()=>{window.__staticWakeFixture.failure=false});
    await press(f,f.getByRole('button',{name:'重试恢复',exact:true}));await summary.filter({hasText:'已恢复'}).waitFor();
    assert.equal(await f.evaluate(()=>window.__staticWakeFixture.status.restorePending),0);
    assert.equal(await f.getByRole('button',{name:'重试恢复',exact:true}).count(),0);
    const copy=await f.locator('.sleep-static-wake .toggle-label,.sleep-static-wake .toggle-desc,.sleep-static-wake p').evaluateAll(els=>els.map(el=>({whiteSpace:getComputedStyle(el).whiteSpace,wraps:el.getBoundingClientRect().height>24})));
    assert.equal(copy.length,3);
    assert.ok(copy.every(el=>el.whiteSpace==='nowrap'&&!el.wraps));
    assert.equal(await f.locator('.sleep-console').evaluate(el=>el.scrollWidth>el.clientWidth+1),false);
    assert.equal(await f.evaluate(()=>window.__staticWakeFixture.calls.filter(c=>c==='scan').length),0);
    await press(f,scan); // Leave both independent preferences enabled.
    await shot('sleep-auto-scan-'+height);
  });
  await actionCase('Sleep static wake reverse concurrent completion and OFF callbacks',height,async()=>{
    f=await open('/sleep',height);
    const summary=f.getByRole('status',{name:'静态唤醒屏蔽状态',exact:true});
    const repair=f.getByRole('switch',{name:'睡眠电源修复',exact:true});
    const scan=f.getByRole('switch',{name:'自动扫描并屏蔽',exact:true});
    await f.evaluate(()=>{const s=window.__staticWakeFixture;s.completeAuto({enabled:true,phase:'applied',lastScanEpoch:1,devices:2,software:3});s.holdReply=true});
    await press(f,scan);
    await f.waitForFunction(()=>window.__staticWakeFixture.releaseReply!==null);
    await f.evaluate(()=>window.__staticWakeFixture.completeAuto({enabled:false,phase:'restore-pending',restorePending:1,devices:7,software:8}));
    await f.evaluate(()=>{const s=window.__staticWakeFixture;s.holdReply=false;s.releaseReply()});
    await summary.filter({hasText:'设备 7'}).waitFor();
    assert.match(await summary.innerText(),/设备 7.*软件 8/,'LATEST_STATE_MUST_SURVIVE_DELAYED_RESTORE_ACK');
    await press(f,f.getByRole('button',{name:'重试恢复',exact:true}));await summary.filter({hasText:'已恢复'}).waitFor();
    const scans=await f.evaluate(()=>window.__staticWakeFixture.calls.filter(c=>c==='scan').length);
    await f.evaluate(()=>{window.__staticWakeFixture.failAutoScanSave=true});await press(f,scan);
    assert.equal(await scan.getAttribute('aria-checked'),'false');
    assert.equal(await f.evaluate(()=>window.__sleepFixture.staticWakeAutoScanEnabled),false);
    assert.equal(await f.evaluate(()=>window.__sleepFixture.plan),true);
    const restores=await f.evaluate(()=>window.__staticWakeFixture.calls.filter(c=>c==='restore').length);
    await f.evaluate(()=>{window.__staticWakeFixture.failAutoScanSave=false});await press(f,scan);
    await f.evaluate(()=>{const s=window.__staticWakeFixture;s.holdRead=true;s.completeAuto({enabled:true,phase:'applied',devices:9})});
    await f.waitForFunction(()=>window.__staticWakeFixture.readReleases.length>0);
    await press(f,scan);
    await f.evaluate(()=>{const s=window.__staticWakeFixture;s.holdRead=false;s.readReleases.splice(0).forEach(release=>release())});
    await summary.filter({hasText:'已恢复'}).waitFor();
    assert.equal(await scan.getAttribute('aria-checked'),'false');
    assert.equal(await f.evaluate(()=>window.__sleepFixture.staticWakeAutoScanEnabled),false);
    assert.equal(await repair.getAttribute('aria-checked'),'true');
    assert.equal(await f.evaluate(()=>window.__sleepFixture.plan),true);
    assert.equal(await f.evaluate(()=>window.__staticWakeFixture.calls.filter(c=>c==='restore').length),restores+1);
    await f.evaluate(()=>window.dispatchEvent(new CustomEvent('ipc:sleep.staticWake.updated',{detail:{enabled:true,phase:'applied',devices:99}})));
    await summary.filter({hasText:'已恢复'}).waitFor();
    assert.equal(await scan.getAttribute('aria-checked'),'false');
    assert.equal(await f.evaluate(()=>window.__staticWakeFixture.calls.filter(c=>c==='scan').length),scans);
    assert.equal(await f.evaluate(()=>window.__staticWakeFixture.calls.filter(c=>c==='restore').length),restores+1);
    await press(f,scan);
  });
  for(const mode of ['timeoutAction','throwAction'])await actionCase('Sleep OFF uncertain '+mode+' stays pending until its own completion',height,async()=>{
    f=await open('/sleep',height);
    const scan=f.getByRole('switch',{name:'自动扫描并屏蔽',exact:true}),repair=f.getByRole('switch',{name:'睡眠电源修复',exact:true});
    const summary=f.getByRole('status',{name:'静态唤醒屏蔽状态',exact:true});
    await f.evaluate(mode=>{const s=window.__staticWakeFixture;s[mode]=true;s.completeAuto({enabled:true,phase:'applied',lastScanEpoch:1,devices:2})},mode);
    await press(f,scan);await summary.filter({hasText:'恢复结果未确认'}).waitFor();
    const request=await f.evaluate(()=>window.__staticWakeFixture.requests.at(-1));
    assert.equal(request.action,'restore');assert.ok(request.requestId);
    for(const operation of [undefined,{...request,requestId:'old-request',state:'completed',ok:true},{...request,action:'scan',state:'completed',ok:true},{...request,state:'pending',ok:false}]){
      await f.evaluate(operation=>window.__staticWakeFixture.completeAuto({enabled:false,phase:'idle',lastScanEpoch:1,operation}),operation);
      await page.waitForTimeout(50);
      assert.equal(await scan.isDisabled(),true);assert.equal(await repair.isDisabled(),true);
      const retry=f.getByRole('button',{name:'重试恢复',exact:true});assert.equal(await retry.isDisabled(),true);
      await retry.evaluate(el=>{el.click();el.click()});await scan.evaluate(el=>{el.click();el.click()});
      assert.deepEqual(await f.evaluate(()=>window.__staticWakeFixture.preferenceWrites),[false]);
      assert.deepEqual(await f.evaluate(()=>window.__staticWakeFixture.calls.filter(c=>c!=='get')),['restore']);
    }
    await f.evaluate(()=>{const s=window.__staticWakeFixture;s.timeoutAction=false;s.throwAction=false;s.completeRequest({enabled:false,phase:'idle',lastScanEpoch:1,devices:0})});
    await summary.filter({hasText:'已恢复'}).waitFor();
    assert.equal(await scan.isDisabled(),false);assert.equal(await repair.isDisabled(),false);
    assert.equal(await f.getByRole('button',{name:'重试恢复',exact:true}).count(),0);
    assert.equal(await f.evaluate(()=>window.__staticWakeFixture.requests.length),1);
  });
  await actionCase('Sleep initial preference loading and stale reads cannot overwrite reentry',height,async()=>{
    f=await open('/sleep',height);
    await f.evaluate(()=>{const s=window.__staticWakeFixture;s.holdPreferenceRead=true;s.holdRead=true;window.__navigate('/settings')});
    await f.locator('.route-view[data-route="/settings"]').waitFor();await f.evaluate(()=>window.__navigate('/sleep'));
    await f.waitForFunction(()=>window.__staticWakeFixture.preferenceReadReleases.length===1&&window.__staticWakeFixture.readReleases.length>0);
    const scan=f.getByRole('switch',{name:'自动扫描并屏蔽',exact:true});assert.equal(await scan.isDisabled(),true);
    await scan.evaluate(el=>{el.click();el.click()});assert.deepEqual(await f.evaluate(()=>window.__staticWakeFixture.preferenceWrites),[]);
    await f.evaluate(()=>window.__navigate('/settings'));await f.locator('.route-view[data-route="/settings"]').waitFor();
    await f.evaluate(()=>{const s=window.__staticWakeFixture;s.holdPreferenceRead=false;s.holdRead=false;window.__sleepFixture.staticWakeAutoScanEnabled=false;s.status={...s.status,enabled:false,phase:'idle',lastScanEpoch:1};window.__navigate('/sleep')});
    await f.waitForFunction(()=>document.querySelector('[role="switch"][aria-label="自动扫描并屏蔽"]')?.getAttribute('aria-checked')==='false');
    await f.evaluate(()=>{const s=window.__staticWakeFixture;s.preferenceReadReleases.splice(0).forEach(release=>release());s.readReleases.splice(0).forEach(release=>release())});
    await page.waitForTimeout(50);assert.equal(await scan.getAttribute('aria-checked'),'false');
    assert.match(await f.getByRole('status',{name:'静态唤醒屏蔽状态',exact:true}).innerText(),/已恢复/);
    assert.deepEqual(await f.evaluate(()=>window.__staticWakeFixture.calls.filter(c=>c!=='get')),[]);
    await f.evaluate(()=>{window.__staticWakeFixture.throwRead=true;void window.__staticWakeFixture.refresh()});
    await f.getByRole('status',{name:'静态唤醒屏蔽状态',exact:true}).filter({hasText:'屏蔽状态读取失败'}).waitFor();
    await f.evaluate(()=>{window.__staticWakeFixture.throwRead=false;void window.__staticWakeFixture.refresh()});
    await f.getByRole('status',{name:'静态唤醒屏蔽状态',exact:true}).filter({hasText:'已恢复'}).waitFor();
    assert.deepEqual(await f.evaluate(()=>window.__staticWakeFixture.preferenceWrites),[]);
  });
  for(const enabled of [true,false])await actionCase('Sleep late '+(enabled?'ON':'OFF')+' save and restore ACK never replay after navigation',height,async()=>{
    f=await open('/sleep',height);
    if(enabled){await press(f,f.getByRole('switch',{name:'自动扫描并屏蔽',exact:true}));}
    const baseline=await f.evaluate(()=>({writes:window.__staticWakeFixture.preferenceWrites.length,restores:window.__staticWakeFixture.requests.length}));
    await f.evaluate(()=>{const s=window.__staticWakeFixture;s.holdAutoScanSave=true;s.holdReply=true;s.releaseAutoScanSave=null;s.releaseReply=null});
    await press(f,f.getByRole('switch',{name:'自动扫描并屏蔽',exact:true}));
    await f.waitForFunction(()=>window.__staticWakeFixture.releaseAutoScanSave!==null);
    await f.evaluate(()=>window.__navigate('/settings'));await f.locator('.route-view[data-route="/settings"]').waitFor();
    await f.evaluate(()=>{const s=window.__staticWakeFixture;s.holdAutoScanSave=false;s.releaseAutoScanSave()});
    await f.waitForFunction(enabled=>window.__sleepFixture.staticWakeAutoScanEnabled===enabled,enabled);
    if(!enabled)await f.waitForFunction(()=>window.__staticWakeFixture.releaseReply!==null);
    await f.evaluate(()=>window.__navigate('/sleep'));await f.locator('.sleep-console').waitFor();
    const scan=f.getByRole('switch',{name:'自动扫描并屏蔽',exact:true});
    await f.waitForFunction(enabled=>document.querySelector('[role="switch"][aria-label="自动扫描并屏蔽"]')?.getAttribute('aria-checked')===String(enabled),enabled);
    await f.evaluate(()=>{const s=window.__staticWakeFixture;s.holdReply=false;s.releaseReply?.()});await page.waitForTimeout(50);
    assert.equal(await scan.getAttribute('aria-checked'),String(enabled));
    assert.equal(await f.evaluate(()=>window.__staticWakeFixture.preferenceWrites.length),baseline.writes+1);
    assert.equal(await f.evaluate(()=>window.__staticWakeFixture.requests.length),baseline.restores+Number(!enabled));
    assert.equal(await f.evaluate(()=>window.__staticWakeFixture.calls.filter(c=>c==='scan').length),0);
    assert.equal(await f.evaluate(()=>window.__sleepFixture.plan),true);
  });
  f=await open('/sleep',height);
  for(const id of ['protection','power','timeouts']){await press(f,f.locator('[data-settings-section="'+id+'"]'));await state(f,'sleep/'+id,height);
    await actionCase('Sleep '+id+' copy stays on one readable line',height,async()=>{
      const copy=await f.locator('.settings-section-panel:not([hidden]) .toggle-label,.settings-section-panel:not([hidden]) .toggle-desc,.settings-section-panel:not([hidden]) .sleep-group-head h3,.settings-section-panel:not([hidden]) .sleep-group-head>span,.settings-section-panel:not([hidden]) .sleep-warning,.settings-section-panel:not([hidden]) .sleep-time-help>span').evaluateAll(els=>els.filter(el=>el.getBoundingClientRect().height>0).map(el=>{const style=getComputedStyle(el);return {text:el.textContent,whiteSpace:style.whiteSpace,width:el.clientWidth,scrollWidth:el.scrollWidth}}));
      assert.ok(copy.length>0,'sleep '+id+' must render actual copy');
      assert.deepEqual(copy.filter(el=>el.whiteSpace!=='nowrap'||el.scrollWidth>el.width+1),[]);await shot('sleep-'+id+'-'+height);
    });
  }
  await actionCase('Sleep timeout keeps independent AC/DC data',height,async()=>{const trigger=f.getByRole('button',{name:'已接通电源：关闭屏幕超时',exact:true});await press(f,trigger);await command(f,'nav-down');await command(f,'confirm');await f.waitForFunction(()=>!document.querySelector('[role="listbox"]'));assert.equal(await f.evaluate(()=>window.__sleepFixture.timeouts.dcScreen),5);assert.ok(await f.evaluate(()=>window.__sleepWrites.some(item=>item.name==='acScreen')));});
  await press(f,f.locator('[data-settings-section="power"]'));
  await actionCase('Sleep hibernate off requires confirmation; B/actions preserve original state',height,async()=>{await press(f,f.getByRole('switch',{name:'系统休眠开关',exact:true}));assert.equal(await f.locator('.sleep-confirm').count(),1);assert.equal(await f.evaluate(()=>window.__sleepFixture.hibernate),true);await state(f,'sleep/hibernate-confirm',height,'.sleep-confirm');await command(f,'back');assert.equal(await f.locator('.sleep-confirm').count(),0);assert.equal(await f.evaluate(()=>window.__sleepFixture.hibernate),true);});
  if(uiScope==='sleep')return;
  f=await open('/gyro-motion',height);
  await actionCase('Gyro unrecognized state retains equal actions and usable settings',height,async()=>{await f.evaluate(async()=>{window.__fixture.profileInput.gyroOverride='off';await window.__profileChanged();});await f.locator('.gyro-power-btn.unknown').waitFor();assert.ok(await f.locator('.gyro-power-btn.unknown').isDisabled());await state(f,'gyro/unrecognized',height);const layout=await f.locator('.gyro-power-row>.gyro-power-btn').evaluateAll(els=>els.map(el=>el.getBoundingClientRect().toJSON()));assert.ok(Math.abs(layout[0].y-layout[1].y)<1&&Math.abs(layout[0].width-layout[1].width)<1);});
  f=await open('/gyro-motion',height);await press(f,f.getByRole('button',{name:'展开',exact:true}));await state(f,'gyro/advanced',height);await press(f,f.getByRole('button',{name:'赛车',exact:true}));await state(f,'gyro/racing',height);await press(f,f.locator('.seg-btn').filter({hasText:/^Steam$/}));await state(f,'gyro/steam',height);await press(f,f.getByRole('button',{name:'自定义',exact:true}));await state(f,'gyro/custom',height);
  f=await open('/gyro-motion',height);await press(f,f.getByRole('button',{name:'模拟陀螺仪',exact:true}));await f.locator('.gyro-simulation-window').waitFor();await page.waitForTimeout(130);await state(f,'gyro/simulation',height,'.gyro-simulation-window');
  await gyroNavigationCases({f,height,command,press,actionCase,task});
  await actionCase('Gyro simulation dropdown B stays inside; LB/RB page lock',height,async()=>{const trigger=f.locator('.gyro-simulation-window .dd-trigger').first();await press(f,trigger);await f.locator('[role="listbox"]').waitFor();await command(f,'nav-down');await command(f,'back');await f.waitForFunction(()=>!document.querySelector('[role="listbox"]'));assert.equal(await f.locator('.gyro-simulation-window').count(),1);await command(f,'page-next');assert.equal(await f.locator('.gyro-simulation-window').count(),1);assert.ok(await f.locator('.route-view[data-route="/gyro-motion"]').count());});
  await press(f,f.getByRole('button',{name:'展开',exact:true}));await state(f,'gyro/simulation-advanced',height,'.gyro-simulation-window');await shot('gyro-simulation-'+height);
  await actionCase('Gyro nested deletion B stays in simulation then B exits',height,async()=>{await press(f,f.locator('[data-gyro-profile-exit]'));await f.locator('.profile-remove-dialog').waitFor();await state(f,'gyro/simulation-delete',height,'.profile-remove-dialog');await command(f,'back');assert.equal(await f.locator('.profile-remove-dialog').count(),0);assert.equal(await f.locator('.gyro-simulation-window').count(),1);await command(f,'back');assert.equal(await f.locator('.gyro-simulation-window').count(),0);assert.equal(await f.locator('.gyro-integrated-page').count(),1);});
  await actionCase('Native Start outside simulator does not open global debug',height,async()=>{
   const before=await f.evaluate(()=>window.__startDebugEvents);assert.equal(before,0);await command(f,'debug');assert.equal(await f.evaluate(()=>window.__startDebugEvents),before);
  });
  await actionCase('Browser Start outside simulator does not open global debug',height,async()=>{
   const before=await f.evaluate(()=>window.__startDebugEvents);assert.equal(before,0);await f.evaluate(()=>window.__gamepadAudit.browserStart(false,true,false));assert.equal(await f.evaluate(()=>window.__startDebugEvents),before);
  });
  f=await open('/rtss',height);
  await actionCase('Real route scroll policy restores gyro title below fixed monitor',height,async()=>{await f.evaluate(()=>{const content=document.querySelector('.app-content');content.scrollTop=content.scrollHeight;window.__navigate('/gyro-motion');});await f.locator('.gyro-page-heading').waitFor();await page.waitForTimeout(80);const result=await f.evaluate(()=>{const content=document.querySelector('.app-content'),header=document.querySelector('.gyro-page-heading'),bar=document.querySelector('.topmon-bar');return {scrollTop:content.scrollTop,header:header.getBoundingClientRect().top,bar:bar.getBoundingClientRect().bottom,content:content.getBoundingClientRect().top};});assert.ok(result.scrollTop<1&&result.header>=result.content&&result.header>result.bar,JSON.stringify(result));});
  for(const [route,ids] of [['/settings',['appearance','device','diagnostics','updates']],['/power',[]],['/rtss',[]]]){
   f=await open(route,height);for(const id of ids){await press(f,f.locator('[data-settings-section="'+id+'"]'));await state(f,route+'/'+id,height);await actionCase(route+'/'+id+' B returns to selected category',height,async()=>{const body=f.locator('.settings-section-panel:not([hidden])');const target=body.locator('button:not(:disabled):visible,input:not(:disabled):visible').first();if(await target.count()){await target.evaluate(el=>window.__gamepadAudit.focus(el));await command(f,'back');assert.ok(await f.locator('[data-settings-section="'+id+'"]').evaluate(el=>document.activeElement===el));}});}
   if(route==='/power'){
    for(const id of ['services','tools']){const group=f.locator('[data-boot-startup-group="'+id+'"]');await group.waitFor();await group.evaluate(el=>el.scrollIntoView({block:'nearest'}));await state(f,route+'/'+id,height);}
    await actionCase('Boot startup and system tools share the page scope and return to mode choices',height,async()=>{
     assert.equal(await f.locator('.boot-common-startup summary').count(),0);assert.equal(await f.locator('.boot-panel-actions>button').count(),2);
     const service=f.locator('[data-boot-startup-group="services"] button').last(),tool=f.locator('[data-boot-startup-group="tools"] button').first();await service.evaluate(el=>window.__gamepadAudit.focus(el));await command(f,'nav-down');assert.ok(await tool.evaluate(el=>document.activeElement===el));await command(f,'nav-up');assert.ok(await service.evaluate(el=>document.activeElement===el));
     for(let count=0;count<20;count++){if(await f.locator('.boot-mode-cards').evaluate(el=>el.contains(document.activeElement)))break;await command(f,'nav-up');}assert.ok(await f.locator('.boot-mode-cards').evaluate(el=>el.contains(document.activeElement)));
    });
   }
   if(route==='/settings'){
    await backgroundPreviewCases({f,height,press,actionCase,command});
    await batterySettingsCases({f,height,press,state,actionCase});
    await fxSoundRecoveryCases({f,height,press,state,actionCase,command});
    await actionCase('Updater clear cache uses A, stays reachable during background download and reports partial failure',height,async()=>{
     await press(f,f.locator('[data-settings-section="updates"]'));
     const clear=f.locator('.update-cache-clear-btn');await clear.waitFor();
     await f.evaluate(()=>{Object.assign(window.__updater.updateSnapshot,{phase:'downloading',background:true});window.__updateCacheFixture.hold=true;});
     assert.equal(await clear.isEnabled(),true);await press(f,clear);
     assert.equal(await clear.isDisabled(),true);
     await f.evaluate(()=>window.__updateCacheFixture.release());await f.waitForFunction(()=>!window.__updater.updateCacheState.busy);
     assert.match(await f.locator('.update-cache-maintenance [role="status"]').innerText(),/已清理/);
     await f.evaluate(()=>{window.__updateCacheFixture.hold=false;Object.assign(window.__updateCacheFixture.next,{ok:false,message:'部分缓存被占用，请重试'});});
     await press(f,clear);await f.waitForFunction(()=>!window.__updater.updateCacheState.busy);
     assert.match(await f.locator('.update-cache-maintenance [role="status"]').innerText(),/占用/);assert.equal(await clear.isEnabled(),true);
     await f.evaluate(()=>Object.assign(window.__updater.updateSnapshot,{phase:'downloading',background:false}));assert.equal(await clear.isDisabled(),true);
     await f.evaluate(()=>Object.assign(window.__updater.updateSnapshot,{phase:'installing'}));assert.equal(await clear.isDisabled(),true);
     await f.evaluate(()=>Object.assign(window.__updater.updateSnapshot,{phase:'idle'}));
     assert.equal(await f.evaluate(()=>window.__updateCacheFixture.clears),2);
    });
   }
   if(route==='/rtss'){
    await monitorEditorCases({f,height,command,press,actionCase,state,page,task});
    f=await open('/rtss',height,{keepAlive:'1'});
    await actionCase('Monitor cached route deactivation removes editor and reentry restores normal page',height,async()=>{
     await press(f,f.locator('[data-monitor-edit]'));await f.locator('.monitor-editor-window').waitFor();
     await f.evaluate(()=>window.__navigate('/settings'));await f.locator('.route-view[data-route="/settings"]').waitFor();await f.locator('.monitor-editor-window').waitFor({state:'detached'});
     await f.evaluate(()=>window.__navigate('/rtss'));await f.locator('[data-monitor-edit]').waitFor();assert.equal(await f.locator('.monitor-editor-window').count(),0);
     await press(f,f.locator('[data-monitor-edit]'));await f.locator('.monitor-editor-window').waitFor();await press(f,f.getByRole('button',{name:'返回',exact:true}));await f.locator('.monitor-editor-window').waitFor({state:'detached'});
    });
   }
   if(route==='/settings'){
    await press(f,f.locator('[data-settings-section="diagnostics"]'));await actionCase('Logs modal B cancels without delete',height,async()=>{const trigger=f.getByRole('button',{name:'删除日志',exact:true});await press(f,trigger);await f.locator('.diagnostic-log-delete-dialog').waitFor();await state(f,'settings/log-confirm',height,'.diagnostic-log-delete-dialog');await command(f,'back');assert.equal(await f.locator('.diagnostic-log-delete-dialog').count(),0);assert.equal(await f.evaluate(()=>window.__previewCalls.filter(c=>c.cmd==='logs.deleteCurrent').length),0);});
   }
  }
 }finally{await context.close();uiTiming('viewport-end','full-suite',height,viewportTimingStart);}
}
try{
 const viewportResults=await Promise.allSettled([720,800].map(runViewport));
 for(const result of viewportResults)if(result.status==='rejected')throw result.reason;
}catch(error){failures.push({kind:'runner',error:String(error),stack:error.stack});console.error(error);}finally{
 const inputsUnchanged=proofInputs.every(file=>testedInputs[file]===hash(file));
 if(!inputsUnchanged)failures.push({kind:'test-inputs-changed'});
 const buildIdentity=JSON.parse(fs.readFileSync(path.join(task,'build-result.json'),'utf8'));
 const sourceRoot=process.env.YMCC_UI_SOURCE_ROOT;
 const changedSourceInputs=uiScope==='sleep'?Object.entries(buildIdentity.sourceHashes).filter(([file,expected])=>{const absolute=path.isAbsolute(file)?file:path.resolve(sourceRoot,file);return !fs.existsSync(absolute)||createHash('sha256').update(fs.readFileSync(absolute)).digest('hex')!==expected;}).map(([file])=>file):[];
 if(changedSourceInputs.length)failures.push({kind:'source-inputs-changed',files:changedSourceInputs});
 const report={scope:uiScope,formalReleaseGate:'NOT_RUN',changedSourceInputs,pass:failures.length===0&&pageErrors.length===0,testedAt:new Date().toISOString(),testedInputs,inputsUnchanged,source:'Current mainline Vue and complete production gamepad engine; only memory IPC and test adapter',physicalController:false,viewportContracts:['1280x720 / YMCC 535x720','1280x800 / YMCC 595x800'],states:states.length,focusStops:states.reduce((n,s)=>n+s.count,0),directions:states.reduce((n,s)=>n+s.directions,0),actionCases:actions.length,failures,pageErrors,actions,shortcutRetirement,results:states};fs.writeFileSync(path.join(task,'navigation-gate.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({pass:report.pass,states:report.states,focusStops:report.focusStops,directions:report.directions,actions:report.actionCases,failures:failures.length}));await browser.close();await uiBrowserServer.close();const uiTerminal=await uiBrowserTerminal;
 if(uiTools){
  const owned=uiGuard.checkUiBrowserTerminal(uiTools,uiBrowserProcess,uiTerminal);
  uiGuard.observeUiNodeStage({stage:'verify',requireCache:req.cache,sourceRoot:process.env.YMCC_UI_SOURCE_ROOT,evidenceRoot:task});
  const toolReceipt=path.join(task,'verify-tool-runtime.json'),receipt=JSON.parse(fs.readFileSync(toolReceipt,'utf8'));receipt.browser=owned;fs.writeFileSync(toolReceipt,JSON.stringify(receipt,null,2)+String.fromCharCode(10));
 }
 if(!report.pass)process.exitCode=1;
}
