// Real native Steam/DFL input + memory business owner. Never invoke hardware or product YMCC.
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import assert from 'node:assert/strict';
import {root,task} from './fixtures/decky_task_paths.mjs';import {targets,connect} from './fixtures/decky_cdp.mjs';
const sessionFile=path.join(task,'validation/MAINLINE18-NATIVE-UI.json'),out=path.join(task,'validation/MAINLINE18-QAM-ACCEPTANCE.json');
const readSession=()=>JSON.parse(fs.readFileSync(sessionFile,'utf8'));
const report={project:'YMCC 控制台',stage:'Mainline18',sessionFile,actualSteam:true,actualDflWidgets:true,physicalHardwareWrites:0,realBusinessConfigWrites:0,productYmccExeExecuted:false,javascriptCredentialInjection:false,physicalControllerTested:false,steps:[]};
const persist=()=>fs.writeFileSync(out,JSON.stringify(report,null,2));persist();
const sleep=ms=>new Promise(r=>setTimeout(r,ms));let q,c,m;
const read=`(()=>{const rows=Array.from(document.querySelectorAll('.ymcc-decky-slider'));return {text:document.body.innerText,sliders:document.querySelectorAll('.ymcc-decky-sidebar [role=slider]').length,checks:Array.from(document.querySelectorAll('.ymcc-decky-sidebar [role=checkbox]')).map(e=>({label:document.getElementById(e.getAttribute('aria-labelledby'))?.innerText,checked:e.getAttribute('aria-checked'),rect:e.getBoundingClientRect().toJSON()})),combos:Array.from(document.querySelectorAll('.ymcc-decky-sidebar [role=combobox]')).map(e=>({text:e.innerText,font:getComputedStyle(e).fontSize,rect:e.getBoundingClientRect().toJSON()})),rows:rows.map(e=>({text:e.innerText,value:e.querySelector('[role=slider]').getAttribute('aria-valuenow'),font:getComputedStyle(e.querySelector('span')).fontSize,rect:e.getBoundingClientRect().toJSON()})),tabs:Array.from(document.querySelectorAll('[role=tab]')).map(e=>{let f=e[Object.keys(e).find(k=>k.startsWith('__reactFiber$'))],key=null,order=null;for(let i=0;f&&i<20;i++,f=f.return){if(key===null&&f.key!=null)key=String(f.key);if(Array.isArray(f.memoizedProps?.tabs))order=f.memoizedProps.tabs.map(t=>String(t.key));}return {key,order,label:e.getAttribute('aria-label'),y:e.getBoundingClientRect().top};}),height:innerHeight,width:innerWidth};})()`;
async function poll(predicate){for(let i=0;i<70;i++){const state=await q.evaluate(read);if(predicate(state))return state;await sleep(100);}throw Error('QAM state deadline');}
async function mouse(client,rect){const x=rect.left+rect.width/2,y=rect.top+rect.height/2;for(const type of ['mousePressed','mouseReleased'])await client.send('Input.dispatchMouseEvent',{type,x,y,button:'left',buttons:type==='mousePressed'?1:0,clickCount:1});}
async function screenshot(name){const image=await q.send('Page.captureScreenshot',{format:'png',fromSurface:false});fs.writeFileSync(path.join(task,'validation/'+name),Buffer.from(image.data,'base64'));}
try {
 const before=readSession();assert.equal(before.hostCounts.saves,0);assert.equal(before.mutationRequests?.length||0,0);report.memorySavesBeforeInput=0;
 const ts=await targets();q=await connect(ts.find(t=>t.title.startsWith('QuickAccess')).webSocketDebuggerUrl);c=await connect(ts.find(t=>t.title==='SharedJSContext').webSocketDebuggerUrl);
 // First prove real cleanup: unloading our plugin returns the original Decky tab to the bottom.
 await c.evaluate(`(()=>{DFL.Navigation.CloseSideMenus();return true;})()`);await sleep(400);
 await c.evaluate(`(()=>{DeckyPluginLoader.unloadPlugin('ymcc-sidebar');DFL.Navigation.OpenQuickAccessMenu(0);return true;})()`);
 report.unloaded=await poll(s=>s.tabs.length>1&&s.tabs[0].key==='0'&&s.tabs.at(-1).key==='999');report.steps.push('Actual native tab order restored on unload');persist();
 await c.evaluate(`(()=>{DFL.Navigation.CloseSideMenus();return true;})()`);await sleep(400);
 await c.evaluate(`(()=>{window.__YMCC_UI_TEST_IMPORT__={state:'pending'};const receipt=window.__YMCC_UI_TEST_IMPORT__;receipt.promise=DeckyPluginLoader.importPlugin('ymcc-sidebar').then(()=>{DeckyPluginLoader.deckyState.setActivePlugin('ymcc-sidebar');DFL.Navigation.OpenQuickAccessMenu(999);receipt.state='ready';}).catch(error=>{receipt.state='failed';receipt.error=error.message;});return true;})()`);
 await q.send('Page.bringToFront');report.initial=await poll(s=>s.sliders===4&&s.checks.length===2&&s.combos.length===1);
 assert.match(report.initial.text,/YMCC 控制台/);assert.equal(report.initial.tabs[0].key,'999');assert.equal(report.initial.tabs[1].key,'0');assert.ok(report.initial.tabs[0].y<report.initial.tabs[1].y);
 assert.deepEqual(report.initial.tabs.filter(t=>t.key!=='999').map(t=>t.key),report.unloaded.tabs.filter(t=>t.key!=='999').map(t=>t.key));
 assert.match(report.initial.text,/跟随 YMCC 全局挡位：均衡转速/);assert.ok(report.initial.rows.every(r=>r.font==='12px'));assert.equal(report.initial.combos[0].font,'12px');
 report.steps.push('Actual tab999 at index0 above Notifications; native other-order unchanged, 4 sliders/1 dropdown/2 switches');await sleep(300);persist();
 const hyper=report.initial.checks.find(w=>w.label==='超线程');assert.equal(hyper.checked,'false');await mouse(q,hyper.rect);
 report.hyper=await poll(s=>s.checks.find(w=>w.label==='超线程')?.checked==='true');report.steps.push('Native mouse switches original SMT policy to on once');persist();
 await sleep(250);assert.equal(readSession().hostCounts.saves,1);
 const hand=report.hyper.combos[0];await mouse(q,hand.rect);
 let menuTarget;for(let i=0;i<50;i++){for(const t of (await targets()).filter(t=>t.title!=='SharedJSContext')){const candidate=await connect(t.webSocketDebuggerUrl);const text=await candidate.evaluate('document.body.innerText');if(text.includes('SteamDeck')&&text.includes('Xbox')&&text.includes('本机手柄')){menuTarget=t;m=candidate;break;}candidate.close();}if(menuTarget)break;await sleep(100);}
 if(!m)throw Error('Native hand dropdown popup not found');
 const popup=await m.evaluate(`(()=>{const exact=Array.from(document.querySelectorAll('*')).filter(e=>e.childElementCount===0&&e.textContent.trim()==='Xbox');const element=exact.find(e=>e.getBoundingClientRect().width>0);return {text:document.body.innerText,rect:element?.getBoundingClientRect().toJSON()};})()`);
 assert.ok(popup.rect);report.handPopup=popup;const start=Date.now();await mouse(m,popup.rect);m.close();m=null;
 for(let i=0;i<60;i++){if(readSession().hostCounts.saves===2)break;await sleep(100);}assert.equal(readSession().hostCounts.saves,2);
 await c.evaluate(`(()=>{DeckyPluginLoader.deckyState.setActivePlugin('ymcc-sidebar');DFL.Navigation.OpenQuickAccessMenu(999);return true;})()`);
 report.selected=await poll(s=>s.combos[0]?.text.includes('Xbox')&&s.checks.find(w=>w.label==='超线程')?.checked==='true');report.handSelectionRoundTripMs=Date.now()-start;
 report.steps.push('Native hand dropdown selects Xbox/elite; one EXE memory save, no slider intermediate transitions');persist();
 await c.evaluate(`(()=>{DFL.Navigation.CloseSideMenus();return true;})()`);await sleep(250);await c.evaluate(`(()=>{DFL.Navigation.OpenQuickAccessMenu(999);return true;})()`);
 report.reopened=await poll(s=>s.combos[0]?.text.includes('Xbox')&&s.sliders===4&&s.checks.find(w=>w.label==='超线程')?.checked==='true');assert.equal(report.reopened.tabs[0].key,'999');
 await sleep(200);const final=readSession();assert.equal(final.hostCounts.saves,2);assert.equal(final.hostCounts.fanSaves,0);assert.deepEqual(final.mutationRequests.map(r=>({command:r.command,args:r.args})),[{command:'game.setField',args:{field:'hyperThreadPolicy',value:'on'}},{command:'game.setField',args:{field:'padPersona',value:'elite'}}]);
 report.memorySavesAfterInput=final.hostCounts.saves;report.mutations=final.mutationRequests;report.globalFanUntouched=true;report.steps.push('Both original EXE fields survive close/reopen; no fan preset request or Fan save');
 const production=fs.readFileSync(path.join(root,'PowerControl/decky/plugins/ymcc-sidebar/dist/index.js')),served=fs.readFileSync(path.join(final.home,'plugins/ymcc-sidebar/dist/index.js')),hash=b=>crypto.createHash('sha256').update(b).digest('hex');assert.equal(hash(served),hash(production));report.productionJsBytes=production.length;report.productionJsSha256=hash(production);
 report.nativeBinding=await c.evaluate(`(()=>{const d=Object.getOwnPropertyDescriptor(window,'__YMCC_DECKY_MIRROR__');return {frozen:Object.isFrozen(d?.value),writable:d?.writable};})()`);assert.equal(report.nativeBinding.frozen,true);assert.equal(report.nativeBinding.writable,false);
 try { await screenshot('MAINLINE18-QAM-FINAL.png');report.finalScreenshot=true; } catch(error){report.finalScreenshot=false;report.screenshotFailure=error.message;}await c.evaluate(`(()=>{delete window.__YMCC_UI_TEST_IMPORT__;return true;})()`);report.status='passed';persist();console.log('REAL_QAM_CONSOLE_TOP_AND_CONTROLS_PASS');
} catch(error){report.status='failed';report.failure=error.message;persist();throw error;} finally {m?.close();q?.close();c?.close();}
