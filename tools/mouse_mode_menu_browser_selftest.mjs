import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {createHash} from 'node:crypto';
const args=process.argv.slice(2);assert(args.length===1,'Usage: node tools/mouse_mode_menu_browser_selftest.mjs <existing-ui-evidence-directory>');
const task=path.resolve(args[0]),root=path.resolve(import.meta.dirname,'..');
const {url}=JSON.parse(fs.readFileSync(path.join(task,'server.json'),'utf8'));
const {chromium}=createRequire('C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/_mouse_ui.cjs')('playwright');
const browser=await chromium.launch({channel:'msedge',headless:true});
const cases=[],errors=[];
const need=(condition,name,height)=>{assert(condition,name);cases.push({name,height,pass:true});};
try{
 for(const height of [720,800]){
  const page=await browser.newPage({viewport:{width:1280,height}});page.on('pageerror',error=>errors.push(String(error)));
  await page.goto(url+'?'+new URLSearchParams({route:'/schedule',height,theme:'blue-black'}));
  const frame=await(await page.$('#product')).contentFrame();
  await frame.waitForFunction(()=>window.__previewReady&&window.__gamepadAudit);
  await frame.evaluate(()=>{window.__fixture.mousePersona='xbox';window.__gamepadAudit.command('edit-game');});
  const menu=frame.locator('[data-gp-game-quick-menu]');await menu.waitFor();
  const mouse=menu.getByRole('button',{name:/模拟鼠标/});await mouse.waitFor();await frame.waitForFunction(()=>!document.querySelector('[data-gp-game-quick-menu] button[title="切换 YMCC 模拟鼠标"]')?.disabled);
  need(await mouse.evaluate(el=>window.__gamepadAudit.focusables().includes(el)),'xbox-mouse-reachable-by-controller',height);
  await mouse.evaluate(el=>{window.__gamepadAudit.focus(el);window.__gamepadAudit.command('confirm');});
  await frame.waitForFunction(()=>window.__fixture.mouseOn===true);
  need((await mouse.textContent()).includes('映射待确认'),'process-liveness-never-shown-as-confirmed-mapping',height);
  need(await frame.evaluate(()=>window.__fixture.mouseWrites===1),'controller-A-issues-single-enable',height);
  await page.screenshot({path:path.join(task,'mouse-xbox-'+height+'.png')});
  await mouse.click();await frame.waitForFunction(()=>window.__fixture.mouseOn===false);
  need((await mouse.textContent()).includes('已关闭'),'explicit-backend-off-refreshes-state',height);
  await frame.evaluate(()=>{window.__fixture.mousePersona='steamdeck';window.dispatchEvent(new CustomEvent('gp:pad-persona',{detail:{persona:'steamdeck'}}));});
  const steam=menu.getByRole('button',{name:/Steam鼠标/});await steam.waitFor();
  await frame.waitForFunction(()=>document.querySelector('[data-gp-game-quick-menu] button[title="由 Steam 管理，仅显示状态"]')?.disabled===true);
  need(await steam.isDisabled()&&(await steam.textContent()).includes('Steam管理'),'steamdeck-visible-and-readonly',height);
  need(!(await steam.evaluate(el=>window.__gamepadAudit.focusables().includes(el))),'disabled-steam-entry-excluded-from-navigation',height);
  const writes=await frame.evaluate(()=>window.__fixture.mouseWrites);
  await steam.evaluate(el=>el.dispatchEvent(new MouseEvent('click',{bubbles:true})));
  need(await frame.evaluate(()=>window.__fixture.mouseWrites)===writes,'synthetic-click-cannot-bypass-steamdeck-handler',height);
  await page.screenshot({path:path.join(task,'mouse-steamdeck-'+height+'.png')});
  // Late Xbox read must not overwrite the newer SD admission result.
  await frame.evaluate(()=>{window.__fixture.mousePersona='xbox';window.__fixture.mouseReadDelayMs=180;window.dispatchEvent(new CustomEvent('gp:pad-persona',{detail:{persona:'xbox'}}));});
  await page.waitForTimeout(25);
  await frame.evaluate(()=>{window.__fixture.mousePersona='steamdeck';window.__fixture.mouseReadDelayMs=0;window.dispatchEvent(new CustomEvent('gp:pad-persona',{detail:{persona:'steamdeck'}}));});
  await page.waitForTimeout(230);
  need(await steam.isDisabled(),'late-desktop-snapshot-cannot-reopen-sd-control',height);
  // A stale enabled button is also rejected by the native boundary and then refreshes.
  await frame.evaluate(()=>{window.__fixture.mousePersona='xbox';window.dispatchEvent(new CustomEvent('gp:pad-persona',{detail:{persona:'xbox'}}));});
  await frame.waitForFunction(()=>!document.querySelector('[data-gp-game-quick-menu] button[title="切换 YMCC 模拟鼠标"]')?.disabled);
  await frame.evaluate(()=>{window.__fixture.mousePersona='steamdeck';});
  await mouse.click();await steam.waitFor();
  need(await steam.isDisabled()&&await frame.evaluate(()=>window.__fixture.mouseWrites)===writes,'stale-xbox-ui-cannot-enable-sd-backend',height);
  await frame.evaluate(()=>window.__gamepadAudit.command('back'));
  need(await menu.count()===0,'B-returns-from-menu',height);
  need((await frame.evaluate(()=>window.__previewErrors||[])).length===0,'no-product-runtime-errors',height);
  await page.close();
 }
 need(errors.length===0,'no-browser-errors',0);
 const report={status:'PASS',scope:'real-GameQuickActions-and-production-gamepad-engine;memory-native-boundary',physicalController:'NOT_TESTED',cases,errors,sourceHashes:{'src/components/GameQuickActions.vue':createHash('sha256').update(fs.readFileSync(path.join(root,'src/components/GameQuickActions.vue'))).digest('hex'),'src/gamepad/engine.ts':createHash('sha256').update(fs.readFileSync(path.join(root,'src/gamepad/engine.ts'))).digest('hex')}};
 fs.writeFileSync(path.join(task,'mouse-menu-result.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({status:'PASS',cases:cases.length,heights:[720,800]}));
}finally{await browser.close();}
