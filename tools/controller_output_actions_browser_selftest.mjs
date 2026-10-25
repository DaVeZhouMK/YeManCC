import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import{createRequire}from'node:module';import{buildEditorPreview}from'./controller_shortcut_editor_preview.mjs';
const{chromium}=createRequire('C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/_task.cjs')('playwright');
const out=process.env.YMCC_UI_BROWSER_OUT||'G:/YeManCC-Work/Build/Validation/ControllerEditorDeck-20261017/OutputActions-20261017';
fs.mkdirSync(out,{recursive:true});
const settingsPath=path.join(out,'memory-settings.json');
fs.writeFileSync(settingsPath,JSON.stringify({input:{},gamepad:{enabled:true,bDoubleMinimize:true,startDoubleF7:true,tdpShortcut:true,fpsShortcut:true,killGame:true,openKeyboard:true,returnDesktop:true,mouseToggle:true,mouseBackend:'joyxoff'},quickApps:{apps:[]}},null,2));
await buildEditorPreview({settingsPath,out:out+'/actual-editor'});
const browser=await chromium.launch({headless:true,executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'}),checks=[],errors=[];
const check=(ok,name)=>{assert(ok,name);checks.push(name)};
try{
 const page=await browser.newPage({viewport:{width:1280,height:720},deviceScaleFactor:1});page.on('pageerror',e=>errors.push(e.message));await page.goto('file:///'+out+'/actual-editor/editor.html');await page.locator('.rule-row').first().waitFor();
 await page.evaluate(()=>window.__editorPreview.setCatalog('two'));await page.locator('.new-action').click();
 check(await page.locator('.manual-key,.oem-back-slot,.combo-input-editor .seg').count()===0,'one recording entry replaces three source pickers and manual binding');
 const start=async()=>{await page.locator('.record-action').click();await page.waitForFunction(()=>window.__editorPreview.recording)};
 const keys=async codes=>page.evaluate(codes=>{for(const code of codes)window.dispatchEvent(new KeyboardEvent('keydown',{code,key:code,bubbles:true,cancelable:true}));for(const code of [...codes].reverse())window.dispatchEvent(new KeyboardEvent('keyup',{code,key:code,bubbles:true,cancelable:true}))},codes);
 const native=async(inputs,released)=>page.evaluate(({inputs,released})=>window.__editorPreview.emit('shortcut.recording.gamepad',{inputs,released}),{inputs,released});
 const finish=()=>page.waitForFunction(()=>document.querySelector('.combo-input-editor')?.dataset.recording==='false');
 const save=async()=>{if(await page.locator('.rule-row.creating').count()){if(await page.locator('.creation-next').isVisible())await page.locator('.creation-next').click();if(!await page.locator('.creation-finish').count()){await page.locator('.output-summary').click();await page.getByRole('button',{name:'键盘按键',exact:true}).click();await page.locator('.keyboard-hit-layer button[data-key-code="KeyK"]').click()}await page.locator('.creation-finish').click()}await page.locator('[data-editor-action="apply"]').click();return page.evaluate(()=>structuredClone(window.__editorPreview.settings.input.buttonMapping.rules.shortcutRules.filter(r=>r.origin!=='native-default').at(-1)))};
 await start();await native([{source:'controller',code:'a'},{source:'oem',code:'m1'}],false);await keys(['F14','KeyK']);await page.waitForTimeout(70);
 check(await page.locator('.combo-input-editor').getAttribute('data-recording')==='true','keyboard release cannot finish while controller/rear lane is still held');
 await native([{source:'controller',code:'a'},{source:'oem',code:'m1'}],true);await finish();
 check(await page.locator('.combo-chip').count()===4,'native rear + A + F14 + K all enter the same recorded combination');
 check(await page.locator('.device-diagram').count()===1&&await page.locator('.keyboard-preview').count()===0,'mixed source preview keeps complete controller');
 check((await page.locator('.mixed-notice').innerText()).includes('K')&&!(await page.locator('.mixed-notice').innerText()).includes('F14'),'mixed notice explicitly names ordinary keyboard keys and classifies F14 as handheld');
 check((await page.locator('.handheld-key-bubbles').innerText()).includes('掌机扩展键 · F14'),'handheld extended code has a dedicated labeled capsule');
 let rule=await save();check(rule.inputs.length===4&&rule.inputs.some(i=>i.source==='keyboard'&&i.code==='F14')&&rule.inputs.some(i=>i.source==='oem'&&i.code==='m1'),'CAS saves original source/code identities without inventing OEM IDs');
 await start();await keys(['ControlLeft','KeyK']);await finish();check(await page.locator('.keyboard-preview').count()===1&&await page.locator('.device-diagram').count()===0,'ordinary keyboard-only capture switches to existing complete keyboard SVG');
 check(await page.locator('.keyboard-asset path').count()===349,'keyboard reuses all original W3C SVG paths');await page.screenshot({path:out+'/keyboard-1280x720.png'});
 await start();await keys(['F14']);await finish();check(await page.locator('.device-diagram').count()===1&&await page.locator('.mixed-notice').count()===0,'F14 alone is a handheld preview and does not falsely warn of mixed ordinary keyboard');
 rule=await save();if(rule.inputs[0]?.code!=='F14')console.log(JSON.stringify({saved:rule,display:await page.locator('.combo-chips').innerText(),applyDisabled:await page.locator('[data-editor-action="apply"]').isDisabled(),status:await page.locator('.status-bubble').allTextContents()}));check(rule.inputs[0].source==='keyboard'&&rule.inputs[0].code==='F14','extended function key keeps actual keyboard transport in saved data');
 // Cancel and over-limit captures preserve the previous F14 rule.
 await start();await native([{source:'controller',code:'a'}],false);await page.locator('.record-action').click();check((await page.locator('.combo-chips').innerText()).includes('F14'),'cancel capture preserves previous inputs');
 await start();await keys(['KeyA','KeyB','KeyC','KeyD','KeyE']);await page.waitForTimeout(70);check(await page.locator('.combo-input-editor').getAttribute('data-recording')==='true','over-limit capture does not silently commit first four keys');await page.locator('.record-action').click();
 const metrics=[];
 for(const height of[720,800]){await page.setViewportSize({width:1280,height});
  for(const variant of['tiles','steps','library']){
   await page.evaluate(v=>window.__editorPreview.setOutput(v),variant);await page.waitForFunction(v=>document.querySelector('.output-action-editor')?.dataset.outputVariant===v,variant);
   const closed=await page.locator('.mirror-card').boundingBox();
   const entry=await page.locator('.output-type-change').count()?page.locator('.output-type-change'):page.locator('.output-summary');await entry.click();
   if(variant==='steps')await page.getByRole('button',{name:'键盘按键',exact:true}).click();else await page.locator('.output-categories').getByRole('button',{name:'键盘按键',exact:true}).click();
   await page.locator('.keyboard-output-dialog').waitFor();
   check(await page.locator('.keyboard-hit-layer button').count()===104,variant+' opens all 104 keyboard keys together at '+height);
   check(await page.locator('.keyboard-asset path').count()===349,variant+' keyboard output reuses the same existing SVG at '+height);
   check(await page.locator('.output-key-grid,.keyboard-categories').count()===0,variant+' has no embedded partial keyboard/category key grid');
   check(await page.locator('.keyboard-hit-layer button:disabled').count()===0,variant+' all standard physical keys map to existing output actions');
   const bounds=await page.locator('.keyboard-hit-layer button').evaluateAll(ns=>ns.map(n=>{const r=n.getBoundingClientRect();return{x:r.x,y:r.y,right:r.right,bottom:r.bottom,width:r.width,height:r.height}}));
   check(bounds.every(r=>r.x>=0&&r.right<=1280&&r.bottom<=height&&r.height>=38),variant+' full keyboard stays visible with original proportions at '+height);
   const now=await page.locator('.mirror-card').boundingBox();check(JSON.stringify(closed)===JSON.stringify(now),variant+' popup does not move controller stage at '+height);
   await page.locator('.keyboard-hit-layer button[data-key-code="KeyA"]').focus();
   await page.locator('.keyboard-hit-layer button[data-key-code="KeyA"]').evaluate(n=>n.dispatchEvent(new CustomEvent('gp:spatial-edit',{bubbles:true,cancelable:true,detail:{dx:1,dy:0}})));
   await page.waitForFunction(()=>document.activeElement?.getAttribute('data-key-code')==='KeyS');
   check(await page.locator('.keyboard-hit-layer button[data-key-code="KeyS"]').evaluate(n=>n.getRootNode().activeElement===n),variant+' uses the actual controller spatial-edit event to move A→S');
   await page.screenshot({path:out+'/'+variant+'-keyboard-popup-1280x'+height+'.png'});
   await page.locator('.keyboard-hit-layer button[data-key-code="KeyK"]').click();check((await page.locator('.output-summary').innerText()).includes('键盘 · K'),variant+' selection returns to prominent output summary');
   rule=await save();check(rule.actionId==='keyboard.letter'&&rule.params.key==='KeyK',variant+' reuses original keyboard action ID and parameter');
   const m=await page.evaluate(()=>{const q=s=>document.querySelector(s),b=[...document.querySelectorAll('.editor-global-actions button,.new-action,.record-action')].map(n=>[n.offsetWidth,n.offsetHeight,getComputedStyle(n).fontSize]);return{columns:getComputedStyle(q('.rule-list')).gridTemplateColumns.split(' ').length,count:document.querySelectorAll('.rule-row').length,stage:getComputedStyle(q('.editor-stage')).transform,buttons:b}});metrics.push({height,variant,...m});check(m.columns===2&&m.count>0&&m.count<=10&&m.stage==='none',variant+' preserves two-column ten-card list and unscaled stage at '+height);
  }
 }
 for(const v of['tiles','steps','library'])check(JSON.stringify(metrics.find(m=>m.variant===v&&m.height===720).buttons)===JSON.stringify(metrics.find(m=>m.variant===v&&m.height===800).buttons),v+' button sizes are identical across both handheld resolutions');
 await page.locator('.output-summary').click();await page.locator('.keyboard-output-dialog').waitFor();
 await page.evaluate(()=>window.dispatchEvent(new CustomEvent('ipc:gamepad-back',{cancelable:true})));
 check(await page.locator('.keyboard-output-dialog').count()===0&&await page.locator('.editor-dialog').count()===1,'B closes only the keyboard popup and preserves the editor');
 check(await page.locator('.output-summary').evaluate(n=>n.getRootNode().activeElement===n),'keyboard cancel restores focus to its output trigger');
 await page.locator('.output-summary').click();await page.getByRole('button',{name:'符号层 关',exact:true}).click();await page.locator('.keyboard-hit-layer button[data-key-code="Digit1"]').click();rule=await save();check(rule.actionId==='keyboard.symbol'&&rule.params.key==='Digit1','symbol layer preserves original shifted-digit action and parameter');
 await page.locator('.output-summary').click();await page.getByRole('button',{name:'符号层 开',exact:true}).click();await page.locator('.keyboard-hit-layer button[data-key-code="Space"]').click();rule=await save();check(rule.actionId==='keyboard.leftFunction'&&rule.params.key==='Space','complete keyboard exposes Space through existing function action group');
 // Input controller diagram model changes retain recorded data and complete geometry.
 for(const persona of['steamdeck','dualsense-edge','elite']){await page.evaluate(p=>window.__editorPreview.setPersona(p),persona);await page.waitForTimeout(40);check(await page.locator('.device-diagram').count()===1&&await page.locator('.device-diagram filter,.device-diagram image').count()===0,persona+' preserves original Steam vector path renderer with no shadow/bitmap');}
 // A callback's source cancellation also cancels keyboard-only sessions.
 await start();await page.evaluate(()=>window.__editorPreview.emit('shortcut.recording.stopped',{reason:'focus-lost'}));await finish();check(true,'native cancellation stops a unified capture regardless of last input preview');
 await page.locator('.new-action').click();await start();await keys(['KeyA']);await finish();await page.locator('.creation-next').click();
 await page.locator('.output-summary').click();await page.locator('.output-categories').getByRole('button',{name:'键盘按键',exact:true}).click();
 await page.locator('.keyboard-hit-layer button[data-key-code="KeyA"]').focus();const reached=new Set(['KeyA']);
 for(const direction of[[1,0],[0,1],[-1,0],[0,-1]]){for(let n=0;n<40;n++){const current=await page.evaluate(()=>document.activeElement?.getAttribute('data-key-code'));if(!current)break;await page.locator('.keyboard-hit-layer button[data-key-code="'+current+'"]').evaluate((node,d)=>node.dispatchEvent(new CustomEvent('gp:spatial-edit',{bubbles:true,cancelable:true,detail:{dx:d[0],dy:d[1]}})),direction);await page.waitForTimeout(1);reached.add(await page.evaluate(()=>document.activeElement?.getAttribute('data-key-code')))}}
 check([...reached].every(Boolean)&&reached.size>15,'controller navigation stays on real keyboard targets across main/direction/numeric rows');
 await page.getByRole('button',{name:'B 返回',exact:true}).click();
 // The retired preset is absent; creating/editing custom F7 output is still supported.
 const defaultPage=await browser.newPage({viewport:{width:1280,height:720},deviceScaleFactor:1});defaultPage.on('pageerror',e=>errors.push(e.message));await defaultPage.goto('file:///'+out+'/actual-editor/editor.html');await defaultPage.locator('.rule-row').first().waitFor();
 check(await defaultPage.locator('.rule-row').count()===8&&await defaultPage.locator('.rule-row').filter({hasText:'键盘 · F7'}).count()===0,'legacy true flag cannot restore the retired default F7 row; eight other defaults remain');
 await defaultPage.locator('.new-action').click();await defaultPage.locator('.record-action').click();await defaultPage.waitForFunction(()=>window.__editorPreview.recording);
 await defaultPage.evaluate(()=>{const inputs=[{source:'controller',code:'start'}];window.__editorPreview.emit('shortcut.recording.gamepad',{inputs,released:false});window.__editorPreview.emit('shortcut.recording.gamepad',{inputs,released:true})});
 await defaultPage.waitForFunction(()=>document.querySelector('.combo-input-editor')?.dataset.recording==='false');await defaultPage.locator('.creation-next').click();await defaultPage.locator('.output-summary').click();await defaultPage.locator('.output-categories').getByRole('button',{name:'键盘按键',exact:true}).click();await defaultPage.locator('.keyboard-output-dialog').waitFor();
 check(await defaultPage.locator('.keyboard-hit-layer button:disabled').count()===0,'custom F7 output still opens the entire enabled keyboard');await defaultPage.locator('.keyboard-hit-layer button[data-key-code="F7"]').click();await defaultPage.locator('.creation-finish').click();await defaultPage.locator('[data-editor-action="apply"]').click();
 const customF7=await defaultPage.evaluate(()=>structuredClone(window.__editorPreview.settings.input.buttonMapping.rules.shortcutRules.find(r=>r.origin==='custom'&&r.params.key==='F7')));
 check(customF7?.actionId==='keyboard.numeric'&&customF7.inputs[0]?.code==='start'&&!customF7.defaultKey,'user can create and save Start to F7 without recreating a native default');
 const defaultsBefore=await defaultPage.evaluate(()=>structuredClone(window.__editorPreview.settings.input.buttonMapping.rules.shortcutRules.filter(r=>r.origin==='native-default')));
 check(defaultsBefore.length===8&&defaultsBefore.every(r=>r.defaultKey!=='startDoubleF7'),'saving custom F7 keeps other defaults and excludes the retired slot');
 await defaultPage.locator('.output-summary').click();await defaultPage.locator('.keyboard-hit-layer button[data-key-code="KeyK"]').click();await defaultPage.locator('[data-editor-action="apply"]').click();
 const edited=await defaultPage.evaluate(id=>structuredClone(window.__editorPreview.settings.input.buttonMapping.rules.shortcutRules.find(r=>r.id===id)),customF7.id);
 check(edited.origin==='custom'&&edited.actionId==='keyboard.letter'&&edited.params.key==='KeyK'&&JSON.stringify(edited.inputs)===JSON.stringify(customF7.inputs),'custom keyboard output changes F7 to K while preserving its identity and inputs');
 check(JSON.stringify(await defaultPage.evaluate(()=>window.__editorPreview.settings.input.buttonMapping.rules.shortcutRules.filter(r=>r.origin==='native-default')))===JSON.stringify(defaultsBefore),'custom key editing leaves all remaining native default slots intact');
 await defaultPage.evaluate(()=>window.__editorPreview.reopen());await defaultPage.locator('.rule-row').filter({hasText:'键盘 · K'}).waitFor();check(await defaultPage.locator('.rule-row').filter({hasText:'键盘 · F7'}).count()===0,'reopen keeps the edited custom key and never resurrects default F7');await defaultPage.close();
 check(errors.length===0,'no browser runtime errors');fs.writeFileSync(out+'/browser-acceptance.json',JSON.stringify({pass:true,boundary:"actual editor with explicit memory settings; no installed settings or physical input",checks,metrics,errors},null,2));console.log('PASS '+checks.length+' output/unified mixed recorder browser checks');
}finally{await browser.close()}
