import assert from 'node:assert/strict';
import path from 'node:path';

const ceiling=page=>page.locator('[data-field="performance.controls.ac.tdpCeiling"] input');
async function auditOriginDirections(page){
 const result=await page.evaluate(()=>{
  const pane=document.querySelector('.boot-config-scroll'),category=document.querySelector('[data-boot-category="performance"]');
  const controls=[...pane.querySelectorAll('button,input:not([type="hidden"]),select,textarea,[tabindex="0"]')]
   .filter(el=>!el.matches(':disabled,[aria-disabled="true"],[tabindex="-1"],[data-gp-ignore]')&&!el.closest('[inert],[hidden],[aria-hidden="true"]')&&el.getClientRects().length&&getComputedStyle(el).visibility!=='hidden');
  const items=controls.map((el,index)=>{const rect=el.getBoundingClientRect();return {el,id:index,x:rect.left+rect.width/2,y:el.closest('.boot-field')?.getBoundingClientRect().top??rect.top+rect.height/2};}).sort((a,b)=>a.y-b.y||a.x-b.x);
  const rows=[];for(const item of items){let row=rows.at(-1);if(!row||Math.abs(row[0].y-item.y)>12)rows.push(row=[]);row.push(item);}for(const row of rows)row.sort((a,b)=>a.x-b.x);
  const failures=[];let checks=0;
  for(let r=0;r<rows.length;r++)for(let c=0;c<rows[r].length;c++)for(const [dx,dy,action]of [[-1,0,'nav-left'],[1,0,'nav-right'],[0,-1,'nav-up'],[0,1,'nav-down']]){
   if(dy&&r+dy<0)continue;
   const item=rows[r][c],next=dy?rows[r+dy]:null;
   const expected=dx?(rows[r][c+dx]?.el||(dx<0?category:item.el)):
    next?[...next].sort((a,b)=>Math.abs(a.x-item.x)-Math.abs(b.x-item.x))[0].el:item.el;
   window.__bootGamepad.focus(item.el);window.__bootGamepad.command(action);checks++;
   const actual=document.activeElement;
   if(actual!==expected)failures.push({from:item.el.getAttribute('data-boot-adopt-global')||item.el.textContent?.trim(),action,actual:actual?.textContent?.trim(),expected:expected.textContent?.trim()});
   if(pane.contains(actual)){const box=pane.getBoundingClientRect(),rect=actual.getBoundingClientRect();if(rect.top<box.top-2||rect.bottom>box.bottom+2)failures.push({action,clipped:true});}
  }
  return {checks,controls:controls.length,failures};
 });
 assert.deepEqual(result.failures,[],'origin control directions must follow visual rows and stay visible');return result;
}
export async function inspectLegacyOrigin({page,focus,command,read}){
 await focus(page,page.locator('[data-boot-category="performance"]'));await command(page,'confirm');
 const warning=page.locator('[data-boot-unresolved="performance"]');await warning.waitFor();
 assert.match(await warning.innerText(),/仅供参考/);
 assert.equal(await ceiling(page).isDisabled(),true);assert.equal(Number(await ceiling(page).inputValue()),49);
 const adopt=page.locator('[data-boot-adopt-global="performance"]');assert.equal(await adopt.isEnabled(),true);
 const directions=await auditOriginDirections(page);
 await focus(page,adopt);assert.equal(await adopt.evaluate(el=>el===document.activeElement),true);
 await command(page,'nav-left');assert.equal(await page.locator('[data-boot-category="performance"]').evaluate(el=>el===document.activeElement),true);
 await command(page,'nav-right');assert.equal(await adopt.evaluate(el=>el===document.activeElement),true,'category must return to the origin action');
 assert.equal(read().extensions.bootModes.global.performance?.controls,undefined,'opening editor manufactured global intent');
 return {referenceCeiling:49,fieldsReadOnly:true,explicitAdoptionReachable:true,directions};
}

export async function adoptLegacyOrigin({page,focus,command,read,out,name}){
 const edit=page.getByRole('button',{name:'编辑配置',exact:true});await focus(page,edit);await command(page,'confirm');
 await page.locator('.boot-workbench').waitFor();await inspectLegacyOrigin({page,focus,command,read});
 const before=read(),scenes=structuredClone(before.extensions.bootModes.scenes),input=structuredClone(before.input);
 const beforeShot=path.join(out,name+'-unresolved.png');await page.screenshot({path:beforeShot});
 await command(page,'confirm');await page.locator('[data-boot-adopt-global="performance"]').waitFor({state:'detached'});
 assert.equal(await ceiling(page).isEnabled(),true);assert.equal(await ceiling(page).evaluate(el=>el===document.activeElement),true,'adoption must return focus to a usable field');
 assert.equal(await page.evaluate(()=>window.__bootGamepad.editing()),false,'adoption must not start slider edit');
 await command(page,'confirm');assert.equal(await page.evaluate(()=>window.__bootGamepad.editing()),true);
 await command(page,'nav-right');await command(page,'back');assert.equal(Number(await ceiling(page).inputValue()),49,'B must cancel the slider edit');
 assert.equal(await page.evaluate(()=>window.__bootGamepad.editing()),false);
 assert.equal(read().extensions.bootModes.global.performance?.controls,undefined,'adoption draft wrote before Save');
 const save=page.locator('.boot-workbench-top button').filter({hasText:'保存预设'});await focus(page,save);await command(page,'confirm');
 await page.waitForFunction(revision=>window.__bootFileDocument?.revision>revision,before.extensions.bootModes.revision);
 const saved=read();assert.equal(saved.extensions.bootModes.global.performance.controls.ac.tdpCeiling,49);
 assert.equal(saved.extensions.bootModes.unresolvedGlobal?.includes('performance')??false,false);
 assert.deepEqual(saved.extensions.bootModes.scenes,scenes);assert.deepEqual(saved.input,input,'explicit preset save hot-applied input');
 await command(page,'back');await page.locator('.boot-workbench').waitFor({state:'detached'});
 await focus(page,edit);await command(page,'confirm');await page.locator('.boot-workbench').waitFor();
 await focus(page,page.locator('[data-boot-category="performance"]'));await command(page,'confirm');
 assert.equal(await page.locator('[data-boot-unresolved="performance"]').count(),0);assert.equal(await ceiling(page).isEnabled(),true);
 assert.equal(Number(await ceiling(page).inputValue()),49);
 const afterShot=path.join(out,name+'-adopted.png');await page.screenshot({path:afterShot});
 await command(page,'back');await page.locator('.boot-workbench').waitFor({state:'detached'});
 return {referenceNotAutomaticallySaved:true,explicitGlobalAdoption:49,fieldFocusRestored:true,sliderNotAutoEditing:true,siblingsPreserved:true,reopened:true,screenshots:[beforeShot,afterShot]};
}
