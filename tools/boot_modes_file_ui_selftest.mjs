/** Four-mode UI -> real settings repository/native file writer -> next boot.
 * Canonical ID settings.boot-modes-file-ui; complements, never substitutes for,
 * the existing browser navigation, native boundaries and runtime negatives. */
import fs from 'node:fs';import path from 'node:path';import http from 'node:http';import assert from 'node:assert/strict';import {createRequire} from 'node:module';
import {outputDirectory,repo,fixture,hash,fingerprints,runtimeBundle,listenBootFixture} from './boot_modes_fixture/harness.mjs';
import {buildBrowser} from './boot_modes_fixture/buildBrowser.mjs';
import {prepareDiskSettings,createDiskRuntime,nativeInputs} from './boot_modes_fixture/diskSettings.mjs';
const out=outputDirectory(process.argv.slice(2),'file-ui');
const report={schema:1,suite:'boot-modes-file-ui',pass:false,caseId:'settings.boot-modes-file-ui',scope:'Actual Vue/PowerView shell and gamepad engine -> actual boot bridge/settings repository -> production Win32 atomic file IO/metadata and native delta/CAS writers -> native startup policy -> fresh frontend repository. System app mutex, InputHost admission, services, registry and hardware owners are isolated. No installed app or physical reboot.',assertions:[],policyFailures:[],presetObservations:[],navigationFailures:[],sourceHashes:{},pageErrors:[],networkRejected:[],screenshots:[],coverage:[],timing:{}};
const started=performance.now(),ids=['global','xbox','steam','xbox-steam'];let browser,server,active,native;
const fileRead=f=>JSON.parse(fs.readFileSync(f,'utf8'));
const sources=new Set([...nativeInputs,'tools/boot_modes_file_ui_selftest.mjs','tools/boot_modes_fixture/diskSettings.mjs','tools/boot_modes_fixture/diskApiPort.ts','tools/boot_modes_fixture/fileBrowserPorts.ts','PowerControl/settings-defaults-v1.json']);
const pinned=new Map();const pin=p=>{const value=hash(path.join(repo,p));if(pinned.has(p))assert.equal(value,pinned.get(p),'source changed during UI round '+p);else pinned.set(p,value);sources.add(p);};const collect=a=>a.inputs.forEach(pin);
const command=async(page,action)=>{await page.evaluate(action=>window.__bootGamepad.command(action),action);if(action==='confirm'||action==='back')await page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(resolve)));};
const focus=async(page,locator)=>locator.evaluate(el=>window.__bootGamepad.focus(el));
const chooseButton=page=>page.getByRole('button',{name:'设为开机模式',exact:true});
async function choose(page,mode){
 const cards=page.locator('.boot-mode-cards button');await focus(page,cards.nth(0));for(let i=0;i<ids.indexOf(mode);i++)await command(page,'nav-right');assert.equal(await cards.nth(ids.indexOf(mode)).evaluate(el=>el===document.activeElement),true,'mode row gamepad reachability');await command(page,'confirm');
 const before=fs.readFileSync(active.file,'utf8');await command(page,'nav-down');if(!await chooseButton(page).evaluate(el=>el===document.activeElement))await command(page,'nav-right');assert.equal(await chooseButton(page).evaluate(el=>el===document.activeElement),true,'mode -> startup action natural direction');assert.equal(fs.readFileSync(active.file,'utf8'),before,'preview selection wrote settings');
 await command(page,'confirm');
 await page.waitForFunction(()=>document.querySelector('[data-gp-modal="boot-mode-connection"]')||document.querySelector('.boot-status')||document.querySelector('.boot-error'));
 if(await page.locator('[data-gp-modal="boot-mode-connection"]').count()){
  const modal=page.locator('[data-gp-modal="boot-mode-connection"]');await focus(page,modal.getByRole('button',{name:'仍保存开机模式',exact:true}));await command(page,'confirm');await modal.waitFor({state:'detached'});
 }
 // Mode equality may already be true before committing the same mode again.
 // Wait for the real write to finish instead of reading old settings mid-commit.
 await page.waitForFunction(()=>!document.querySelector('.boot-panel-actions button')?.disabled&&!document.querySelector('[data-gp-modal="boot-mode-connection"]'));
}
async function editorPeek(page,mode,height){
 const edit=page.getByRole('button',{name:'编辑配置',exact:true});await focus(page,edit);await command(page,'confirm');await page.locator('.boot-workbench').waitFor();
 assert.equal(await page.locator('.boot-workbench').evaluate(el=>el.scrollWidth<=el.clientWidth+1),true,'editor horizontal overflow');
 const file=path.join(out,height+'-'+mode+'-editor.png');await page.screenshot({path:file});report.screenshots.push(file);
 await command(page,'back');await page.locator('.boot-workbench').waitFor({state:'detached'});await page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));const returned=await edit.evaluate(el=>el===document.activeElement),actual=await page.evaluate(()=>({tag:document.activeElement?.tagName,text:document.activeElement?.textContent?.trim().slice(0,80),className:document.activeElement?.className}));if(!returned)report.navigationFailures.push({height,mode,expected:'edit trigger',actual,rule:'B must restore focus to the reachable opener after asynchronous loading'});return {returned,actual};
}
async function editPersonaThroughUi(page,mode,persona){
 const labels={disabled:'关闭虚拟手柄',elite:'Xbox',steamdeck:'SteamDeck','dualsense-edge':'PS5'};
 const card=page.locator('.boot-mode-cards button').nth(ids.indexOf(mode));await focus(page,card);await command(page,'confirm');
 const edit=page.getByRole('button',{name:'编辑配置',exact:true});await focus(page,edit);await command(page,'confirm');await page.locator('.boot-workbench').waitFor();
 const category=page.locator('.boot-workbench nav button').filter({hasText:'手柄与陀螺仪'});await focus(page,category);await command(page,'confirm');
 const module=page.locator('[data-boot-module="input"]');
 if(mode!=='global'){const own=module.getByRole('button',{name:'模式单独设置',exact:true});await focus(page,own);await command(page,'confirm');}
 const dropdown=page.locator('[data-field="input.persona"] button');assert.equal(await dropdown.isEnabled(),true,'persona input locked');await focus(page,dropdown);await command(page,'confirm');await page.locator('[role="listbox"]').waitFor();
 const positions=await page.getByRole('option').evaluateAll((nodes,label)=>({current:nodes.findIndex(el=>el===document.activeElement),target:nodes.findIndex(el=>el.textContent.trim()===label)}),labels[persona]);assert(positions.current>=0&&positions.target>=0,'persona option is not reachable');
 for(let n=0;n<Math.abs(positions.target-positions.current);n++)await command(page,positions.target>positions.current?'nav-down':'nav-up');await command(page,'confirm');await page.locator('[role="listbox"]').waitFor({state:'detached'});assert.equal((await dropdown.innerText()).trim(),labels[persona]);
 const beforeRevision=active.read().extensions?.bootModes?.revision??0,save=page.locator('.boot-workbench-top button').filter({hasText:'保存预设'});await focus(page,save);await command(page,'confirm');await page.waitForFunction(revision=>window.__bootFileDocument?.revision>revision,beforeRevision);
 const saved=active.read().extensions.bootModes;assert.equal(active.a.model.resolveBootMode(saved,mode).input.persona,persona,'save replaced the chosen persona');await command(page,'back');await page.locator('.boot-workbench').waitFor({state:'detached'});
 await focus(page,edit);await command(page,'confirm');await page.locator('.boot-workbench').waitFor();await focus(page,category);await command(page,'confirm');assert.equal((await page.locator('[data-field="input.persona"] button').innerText()).trim(),labels[persona],'reopening reset the saved persona');await command(page,'back');await page.locator('.boot-workbench').waitFor({state:'detached'});
 return {changedThroughDropdown:true,savedOnce:true,reopened:true,persona};
}
const cases=[];
for(const target of ids)cases.push({kind:'absent',source:'global',target});
for(const target of ids)cases.push({kind:'legacy',source:'global',target});
for(const source of ids)for(const target of ids)cases.push({kind:'transition',source,target});
for(const target of ids)cases.push({kind:'custom',source:'global',target,custom:{global:'elite',xbox:'disabled',steam:'steamdeck','xbox-steam':'dualsense-edge'}[target]});
for(const fault of ['write-failure','corrupt-backup','corrupt-no-backup','future-schema'])cases.push({kind:'fault',source:'global',target:'xbox-steam',fault});
try{
 for(const p of sources)pin(p);const workspaceParent=path.resolve(repo,'../../..','Testing/YMCC/Workspace/BootModeFileUI');fs.mkdirSync(workspaceParent,{recursive:true});report.storageWorkspace=fs.mkdtempSync(path.join(workspaceParent,'run-'));native=prepareDiskSettings(out+'/native',{storageRoot:report.storageWorkspace});const seedRuntime=await runtimeBundle(out),seed=await seedRuntime.boot.captureBootModeSnapshot();collect(seedRuntime);
 const ui=out+'/ui';fs.mkdirSync(ui,{recursive:true});const built=await buildBrowser(ui,seed,{bootBridgePath:fixture+'/fileBrowserPorts.ts'});for(const p of built.inputs)pin(p);
 report.timing.buildMs=performance.now()-started;const publicRoot=path.join(repo,'public');
 server=http.createServer((req,res)=>{const relative=decodeURIComponent((req.url||'/').split('?')[0]).replace(/^\//,'')||'index.html';let file;if(['index.html','boot-browser.js','boot-browser.css'].includes(relative))file=path.join(ui,relative);else{const candidate=path.resolve(publicRoot,relative);if(candidate.startsWith(publicRoot+path.sep)&&fs.existsSync(candidate)&&fs.statSync(candidate).isFile()){file=candidate;pin(path.relative(repo,file).replaceAll('\\','/'));}}if(!file){res.writeHead(404);res.end();return;}res.setHeader('Content-Type',file.endsWith('.js')?'text/javascript':file.endsWith('.css')?'text/css':file.endsWith('.svg')?'image/svg+xml':file.endsWith('.mp4')?'video/mp4':file.endsWith('.jpg')?'image/jpeg':'text/html');res.end(fs.readFileSync(file));});await listenBootFixture(server);const origin='http://127.0.0.1:'+server.address().port;
 const {chromium}=createRequire('C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/_boot_preview.cjs')('playwright');browser=await chromium.launch({channel:'msedge',headless:true});
 for(const height of [720,800]){
  const page=await browser.newPage({viewport:{width:1280,height}});page.setDefaultTimeout(2500);page.on('pageerror',e=>report.pageErrors.push({height,error:e.message}));
  await page.exposeFunction('__bootFileBackend',async(operation,args)=>{const allowed=['loadBootModes','saveBootModes','chooseBootMode','readBootModeSupport','bootModeConnections','addBootSharedApplication'];assert(allowed.includes(operation),'forbidden UI call '+operation);active.bind();const value=await active.a.boot[operation](...args);return structuredClone(value);});
  await page.route('**/*',async route=>{if(route.request().url().startsWith(origin+'/')||route.request().url().startsWith('data:'))await route.continue();else{report.networkRejected.push(route.request().url());await route.abort();}});
  for(let index=0;index<cases.length;index++){
   const c=cases[index],name=height+'-'+c.kind+'-'+c.source+'-to-'+c.target+(c.fault?'-'+c.fault:''),dir=out+'/files/'+name;let next;
   const entry={name,height,...c,pass:false};let began=performance.now();
   try{
    active=await createDiskRuntime(dir,native,{storageDirectory:path.join(report.storageWorkspace,height+'-'+String(index).padStart(2,'0')),moduleDirectory:out+'/runtime-module'});collect(active.a);const defaults=JSON.parse(fs.readFileSync(repo+'/PowerControl/settings-defaults-v1.json','utf8'));
    if(c.kind==='legacy')active.writeRaw({schemaVersion:1,input:{revision:7,outputTarget:{persona:'disabled',buttonMappingEnabled:false},startupAppliedSession:'legacy-native-process'},startupDesired:{virtualGamepadPersona:'disabled'},future:{sentinel:'keep'}});
    else if(c.kind!=='absent'){defaults.future={sentinel:'keep'};active.writeRaw(defaults);}
    entry.fileExistedBeforeRead=fs.existsSync(active.file);
    if(c.fault==='corrupt-no-backup'||c.fault==='future-schema'){
     const original=c.fault==='corrupt-no-backup'?'{broken-original':JSON.stringify({...defaults,schemaVersion:99});active.writeRaw(original);await page.goto(origin+'/');await page.locator('.boot-error').waitFor();assert.equal(fs.readFileSync(active.file,'utf8'),original,'invalid original overwritten');assert.equal(await chooseButton(page).isEnabled(),false);entry.pass=true;entry.invalidFilePreserved=true;entry.noStartupCommit=true;report.assertions.push(entry);continue;
    }
    if(c.fault==='corrupt-backup'){fs.writeFileSync(active.file+'.bak',JSON.stringify(defaults));active.writeRaw('{broken-recoverable');}
    const loaded=await active.a.repository.loadSettings();Object.assign(active.a.ports.settings,loaded);
    if(c.kind==='transition'){
     const document=active.a.model.createBootModeDocument(await active.a.boot.captureBootModeSnapshot());document.current=c.source;document.boot=c.source;document.bootEnabled=true;
     if(c.custom){const module=c.target==='global'?document.global.input:(document.scenes[c.target].overrides.input??={});Object.assign(module,{persona:c.custom,mouse:c.custom==='steamdeck'?'steamdeck':'off',outputTarget:{...document.global.input.outputTarget,persona:c.custom,buttonMappingEnabled:c.custom!=='disabled'}});}
     const current=active.a.model.resolveBootMode(document,c.source);document.activeSnapshot=current;
     await active.a.repository.updateSettings(s=>{s.extensions={...s.extensions,bootModes:document,bootModesAppliedBoot:'fixture-windows-boot-1'};s.input.outputTarget={...s.input.outputTarget,...current.input.outputTarget,persona:current.input.persona};s.input.startupAppliedBoot='fixture-windows-boot-1';s.startupDesired.virtualGamepadPersona='disabled';});
     Object.assign(active.a.ports.settings,await active.a.repository.loadSettings());
    }
    await page.goto(origin+'/');await page.waitForFunction(()=>!document.querySelector('.boot-panel-actions button')?.disabled&&!!window.__bootFileDocument);
    const before=active.read(),currentPersona=before.input.outputTarget.persona;entry.currentPersonaBefore=currentPersona;
    if(c.kind==='absent'){
     const card=page.locator('.boot-mode-cards button').nth(ids.indexOf(c.target));await focus(page,card);await command(page,'confirm');entry.editorReturn=await editorPeek(page,c.target,height);assert.equal(await page.evaluate(()=>window.__bootFileCalls.includes('saveBootModes')),false);
    }
    if(c.kind==='custom')entry.realEdit=await editPersonaThroughUi(page,c.target,c.custom);
    entry.editorSavesBeforeSelection=await page.evaluate(()=>window.__bootFileCalls.filter(op=>op==='saveBootModes').length);const requested=await page.evaluate(()=>window.__bootFileDocument);entry.requestedPersona=active.a.model.resolveBootMode(requested,c.target).input.persona;
    if(c.fault==='write-failure')active.faults.failWrite=true;
    await choose(page,c.target);
    if(c.fault==='write-failure'){
     await page.locator('.boot-error').waitFor();assert.equal(active.read().extensions?.bootModes?.bootEnabled??false,before.extensions?.bootModes?.bootEnabled??false,'failed commit changed startup state');assert.equal(await chooseButton(page).isEnabled(),true);await choose(page,c.target);
    }
    await page.waitForFunction(mode=>window.__bootFileDocument?.boot===mode&&window.__bootFileDocument?.bootEnabled,c.target);
    const selected=active.read();assert.equal(selected.extensions.bootModes.boot,c.target);assert.equal(selected.extensions.bootModes.bootEnabled,true);assert.equal(selected.input.outputTarget.persona,currentPersona,'selection hot-applied the controller');assert.equal(selected.extensions.bootModes.current,before.extensions?.bootModes?.current||'global');assert.equal(await page.evaluate(()=>window.__bootFileCalls.filter(op=>op==='saveBootModes').length),entry.editorSavesBeforeSelection,'selection performed an extra editor save');
    entry.savedPersona=selected.startupDesired.virtualGamepadPersona;entry.summary=await page.locator('.boot-summary').innerText();fs.copyFileSync(active.file,dir+'/selected-settings.json');
    const expected=c.custom??entry.requestedPersona;
    assert.equal(entry.savedPersona,expected,'intent differs from configured/global scene');
    report.presetObservations.push({name,height,mode:c.target,requestedPersona:entry.requestedPersona,savedPersona:entry.savedPersona,custom:c.custom??null});
    const same=await active.startup('fixture-windows-boot-1','same-boot-new-process');assert.equal(same.ok,true);assert.equal(JSON.parse(same.content).input.outputTarget.persona,currentPersona,'app restart replayed next boot');
    const first=await active.startup('fixture-windows-boot-2','first-process-after-reboot');assert.equal(first.ok,true);assert.equal(JSON.parse(first.content).input.outputTarget.persona,expected);entry.firstBootPersona=JSON.parse(first.content).input.outputTarget.persona;
    next=await active.moduleAt('next-boot');collect(next);Object.assign(next.ports.settings,await next.repository.loadSettings());next.ports.hooks.bootIdentity=()=> 'fixture-windows-boot-2';await next.boot.restoreSelectedBootMode();const restored=active.read();assert.equal(restored.extensions.bootModes.current,c.target);assert.equal(restored.input.outputTarget.persona,expected);if(c.kind!=='absent')assert.deepEqual(restored.future,{sentinel:'keep'});entry.restoredPersona=restored.input.outputTarget.persona;
    const revision=restored.extensions.bootModes.revision;await next.boot.restoreSelectedBootMode();assert.equal(active.read().extensions.bootModes.revision,revision,'same boot restored more than once');const reopened=await active.startup('fixture-windows-boot-2','another-process-after-wake');assert.equal(reopened.ok,true);assert.equal(JSON.parse(reopened.content).input.outputTarget.persona,expected);
    fs.copyFileSync(active.file,dir+'/restored-settings.json');
    if(c.kind==='absent'){const screenshot=out+'/'+height+'-'+c.target+'-overview.png';await page.screenshot({path:screenshot});report.screenshots.push(screenshot);}
    entry.noExtraEditorSave=true;entry.noHotApply=true;entry.sameBootDoesNotReplay=true;entry.oneRestorePerNewBoot=true;entry.storageAndStartupPass=true;entry.pass=entry.editorReturn?.returned!==false;report.assertions.push(entry);
   }catch(error){entry.error=error.stack||String(error);report.assertions.push(entry);console.error('FAIL',name,error.message);try{await page.screenshot({path:out+'/'+name+'-failure.png'});}catch{}}
   finally{entry.elapsedMs=performance.now()-began;active?.a.boot.cancelBootModeDeferredMouse?.();next?.boot.cancelBootModeDeferredMouse?.();active?.flush();}
  }
  await page.close();
 }
 report.pass=report.assertions.every(x=>x.pass)&&report.policyFailures.length===0&&report.navigationFailures.length===0&&report.pageErrors.length===0&&report.networkRejected.length===0;
}catch(error){report.assertions.push({name:'build and initialize four-mode file UI',pass:false,error:error.stack||String(error)});}
finally{await browser?.close();await native?.close();if(server)await new Promise(resolve=>server.close(resolve));report.inputsUnchanged=[...pinned].every(([p,h])=>fs.existsSync(path.join(repo,p))&&hash(path.join(repo,p))===h);report.sourceHashes=Object.fromEntries(pinned);report.pass=report.pass&&report.inputsUnchanged;report.coverage={resolutions:[{width:1280,height:720},{width:1280,height:800}],modes:ids,configuredCasesPerResolution:cases.length,allSourceTargetTransitionsPerResolution:16,editorSaveRequired:false,physicalReboot:false,deviceEnumeration:false};report.counts={scenarios:report.assertions.length,passed:report.assertions.filter(x=>x.pass).length,failed:report.assertions.filter(x=>!x.pass).length,policyFailures:report.policyFailures.length,navigationFailures:report.navigationFailures.length,storageAndStartupPassed:report.assertions.filter(x=>x.storageAndStartupPass).length};report.timing.totalMs=performance.now()-started;fs.writeFileSync(out+'/boot-modes-file-ui.json',JSON.stringify(report,null,2));}
console.log(JSON.stringify({pass:report.pass,counts:report.counts,totalMs:report.timing.totalMs,failures:report.assertions.filter(x=>!x.pass).map(x=>({name:x.name,error:x.error?.split('\n')[0]})),policyFailures:report.policyFailures,navigationFailures:report.navigationFailures},null,2));if(!report.pass)process.exitCode=1;
