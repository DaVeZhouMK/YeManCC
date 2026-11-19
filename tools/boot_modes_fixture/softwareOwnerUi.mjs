import assert from 'node:assert/strict';
const APP_NAME='Four-mode software owner',APP_PATH='C:/fixture/software-owner.exe';

async function addLibraryWithoutConsent({page,article,focus,command,read,name:appName,path:appPath}){
 const before=read(),inputBefore=structuredClone(before.input),templatesBefore=structuredClone(before.extensions?.bootModes);
 const manage=article.getByRole('button',{name:'管理全局快捷应用',exact:true});assert.equal(await manage.isEnabled(),true);
 await focus(page,manage);await command(page,'confirm');
 const modal=page.locator('[data-gp-modal="boot-mode-apps"]');await modal.waitFor();
 const name=modal.getByRole('textbox',{name:'应用名称',exact:true}),path=modal.getByRole('textbox',{name:'应用路径',exact:true});
 await focus(page,name);await name.fill(appName);await command(page,'nav-down');
 assert.equal(await path.evaluate(el=>el===document.activeElement),true,'app name -> path natural down');await path.fill(appPath);
 await focus(page,modal.getByRole('button',{name:'添加到全局应用库',exact:true}));await command(page,'confirm');await modal.waitFor({state:'detached'});
 const saved=read();assert.deepEqual(saved.extensions?.bootModes,templatesBefore,'adding an app must not select any startup policy');
 assert.deepEqual(saved.input,inputBefore,'adding an app must not apply input');
 assert.equal(saved.quickApps.apps.filter(row=>row.path===appPath).length,1);
}

export async function editSoftwareOwner({page,mode,focus,command,read,ids}){
 const category=page.locator('[data-boot-category="optimization"]');await focus(page,category);await command(page,'confirm');
 const article=page.locator('[data-boot-module="softwareLaunch"]');
 const baseline=await page.evaluate(()=>structuredClone(window.__bootFileDocument));
 const durableBefore=read(),inputBefore=structuredClone(durableBefore.input),templatesBefore=structuredClone(durableBefore.extensions?.bootModes);
 if(mode!=='global'){
  const own=article.getByRole('button',{name:'模式单独设置',exact:true});
  if(await own.getAttribute('aria-pressed')!=='true'){await focus(page,own);await command(page,'confirm');}
 }
 await addLibraryWithoutConsent({page,article,focus,command,read,name:APP_NAME,path:APP_PATH});
 const library=read();assert.deepEqual(library.extensions?.bootModes,templatesBefore,'adding an app must not select any startup policy');
 assert.deepEqual(library.input,inputBefore,'adding an app must not apply input');
 assert.equal(library.quickApps.apps.filter(row=>row.path===APP_PATH).length,1);
 const timing=page.locator('[data-field="softwareLaunch.timing"] button');await focus(page,timing);await command(page,'confirm');
 await page.locator('[role="listbox"]').waitFor();
 const positions=await page.getByRole('option').evaluateAll(nodes=>({current:nodes.findIndex(el=>el===document.activeElement),target:nodes.findIndex(el=>el.textContent.trim()==='仅手动')}));
 assert(positions.current>=0&&positions.target>=0,'manual timing must be reachable');
 for(let n=0;n<Math.abs(positions.target-positions.current);n++)await command(page,positions.target>positions.current?'nav-down':'nav-up');
 await command(page,'confirm');await page.locator('[role="listbox"]').waitFor({state:'detached'});
 const slider=page.locator('[data-field="softwareLaunch.delay"] input'),delay=ids.indexOf(mode)+2;
 await focus(page,timing);await command(page,'nav-down');assert.equal(await slider.evaluate(el=>el===document.activeElement),true,'timing -> delay must follow the adjacent visual row');
 await command(page,'confirm');assert.equal(await page.evaluate(()=>window.__bootGamepad.editing()),true,'slider requires explicit edit');
 const initial=Number(await slider.inputValue());
 for(let n=0;n<Math.abs(delay-initial);n++)await command(page,delay>initial?'nav-right':'nav-left');
 await command(page,'confirm');assert.equal(Number(await slider.inputValue()),delay);
 const toggle=article.getByRole('switch',{name:APP_NAME,exact:true});await focus(page,toggle);await command(page,'confirm');
 assert.equal(await toggle.getAttribute('aria-checked'),'true');
 // All changes above are still the workbench draft; only the library was saved.
 assert.deepEqual(read().extensions?.bootModes,templatesBefore,'editing draft must not write mode intent');
 return {baseline,expected:{paths:[APP_PATH],timing:'manual',delay},libraryAddedWithoutStartupConsent:true,inputUnchanged:true,naturalTimingSliderDirection:true};
}

export async function verifySoftwareOwner({page,mode,owner,saved,focus,command,read}){
 const actual=mode==='global'?saved.global.softwareLaunch:saved.scenes[mode].overrides.softwareLaunch;
 assert.deepEqual(actual,owner.expected,'saved policy differs from actual editor selection');
 if(mode!=='global')assert.deepEqual(saved.global.softwareLaunch,owner.baseline.global.softwareLaunch,'independent scene contaminated global launch policy');
 for(const id of ['xbox','steam','xbox-steam'])if(id!==mode)
  assert.deepEqual(saved.scenes[id].overrides.softwareLaunch,owner.baseline.scenes[id].overrides.softwareLaunch,'editing one mode contaminated sibling policy '+id);
 await focus(page,page.locator('[data-boot-category="optimization"]'));await command(page,'confirm');
 assert.equal((await page.locator('[data-field="softwareLaunch.timing"] button').innerText()).trim(),'仅手动');
 assert.equal(Number(await page.locator('[data-field="softwareLaunch.delay"] input').inputValue()),owner.expected.delay);
 assert.equal(await page.locator('[data-boot-module="softwareLaunch"]').getByRole('switch',{name:APP_NAME,exact:true}).getAttribute('aria-checked'),'true');
 await addLibraryWithoutConsent({page,article:page.locator('[data-boot-module="softwareLaunch"]'),focus,command,read,
  name:APP_NAME+' after save',path:'C:/fixture/software-owner-after-save.exe'});
 assert.deepEqual(read().extensions.bootModes,saved,'library edit after saving must preserve all mode policies');
 return {policy:owner.expected,libraryAddedWithoutStartupConsent:true,existingPresetLibraryEditWithoutConsent:true,inputUnchanged:true,naturalTimingSliderDirection:true,reopened:true,siblingsPreserved:true};
}
