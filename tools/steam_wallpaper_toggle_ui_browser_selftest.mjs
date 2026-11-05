import fs from 'node:fs';
import path from 'node:path';
import http from 'node:http';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {createHash} from 'node:crypto';
const evidence=path.resolve(process.argv[2]||''),out=path.resolve(process.argv[3]||'');
if(!process.argv[2]||!process.argv[3])throw Error('Usage: node tools/steam_wallpaper_toggle_ui_browser_selftest.mjs <built UI evidence> <output> [--pause-on-battery]');
const batteryOnly=process.argv[4]==='--pause-on-battery';
if(process.argv.length>5||(process.argv[4]&&!batteryOnly))throw Error('Unknown wallpaper UI selector');
const root=path.resolve(import.meta.dirname,'..');
const hash=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const built=JSON.parse(fs.readFileSync(path.join(evidence,'build-result.json'),'utf8'));
const builtHashes=Object.fromEntries(Object.entries(built.sourceHashes).map(([name,sha])=>[name.replaceAll('\\','/'),sha]));
const sourceHashes={};
for(const name of ['src/views/SteamView.vue','src/components/SteamWallpaperPlaybackToggle.vue','src/components/Toggle.vue','src/components/SwitchControl.vue','src/bridge/uiSettings.ts','src/bridge/settingsRepository.ts','src/bridge/wallpaperPowerPolicy.ts','src/gamepad/engine.ts','src/gamepad/spatial.ts','src/gamepad/focus.ts']){
 const sha=hash(path.join(root,name));assert.equal(builtHashes[name],sha,'Current built component/engine input: '+name);sourceHashes[name]=sha;
}
sourceHashes['tools/steam_wallpaper_toggle_ui_browser_selftest.mjs']=hash(import.meta.filename);
fs.mkdirSync(out,{recursive:true});
const site=path.join(evidence,'site');
const server=http.createServer((request,response)=>{
 const name=new URL(request.url,'http://localhost').pathname;
 const file=path.join(site,name==='/'?'product.html':name.slice(1));
 if(!path.resolve(file).startsWith(site+path.sep)||!fs.existsSync(file)){response.writeHead(404).end();return;}
 response.setHeader('Content-Type',file.endsWith('.js')?'text/javascript':file.endsWith('.css')?'text/css':'text/html');
 fs.createReadStream(file).pipe(response);
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const {chromium}=createRequire(process.env.YMCC_SIM_BROWSER_RUNTIME_ENTRY||'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/package.json')('playwright');
const browser=await chromium.launch({channel:'msedge',headless:true});
const errors=[],rows=[];let checks=0;
const check=(value,label)=>{checks++;assert.ok(value,label);};
async function verifyBatteryToggle(page,height,playback,download,battery){
 const readSaved=()=>page.evaluate(()=>JSON.parse(window.__previewCalls.filter(call=>call.cmd==='settings.write').at(-1).args.content).ui);
 const settled=async value=>page.waitForFunction(expected=>{const el=document.querySelector('[data-steam-wallpaper-setting="pause-on-battery"] [role="switch"]');return el?.getAttribute('aria-checked')===String(expected)&&!el.disabled;},value);
 const before={playback:await playback.getAttribute('aria-checked'),download:await download.getAttribute('aria-checked')};
 check(await battery.getAttribute('aria-checked')==='false','battery pause initially defaults OFF @'+height);
 check(await page.locator('[data-steam-wallpaper-setting="pause-on-battery"] .toggle-label').innerText()==='离电关闭','exact Chinese battery label @'+height);
 check(await battery.evaluate(el=>el.closest('[data-steam-wallpaper-setting]').previousElementSibling.matches('.steam-wallpaper-playback')),'battery setting immediately follows playback @'+height);
 check(await page.evaluate(()=>window.__wallpaperToggleTest.loadedEvents>0),'actual settings loaded before initial OFF @'+height);
 await battery.scrollIntoViewIfNeeded();const rect=await battery.boundingBox();
 check(rect&&rect.x>=0&&rect.y>=0&&rect.x+rect.width<=1280&&rect.y+rect.height<=height,'battery switch fits viewport @'+height);
 await page.screenshot({path:path.join(out,'battery-off-'+height+'.png')});
 await playback.evaluate(el=>{window.__gamepadAudit.focus(el);window.__command('down');});
 check(await battery.evaluate(el=>document.activeElement===el),'official gamepad down reaches battery from playback @'+height);
 await battery.evaluate(()=>window.__command('confirm'));await settled(true);
 let saved=await readSaved();const personalVideoPause=saved.videoBatteryPause;
 check(saved.steamWallpaperPauseOnBattery===true,'official gamepad A saves battery ON @'+height);
 check((saved.steamDynamicWallpaperEnabled!==false)===(before.playback==='true')&&(saved.steamWallpaperBackgroundDownload!==false)===(before.download==='true'),'battery ON preserves playback/download choices @'+height);
 await battery.evaluate(()=>window.__command('down'));
 check(await download.evaluate(el=>document.activeElement===el),'official gamepad down reaches download from battery @'+height);
 await download.evaluate(()=>window.__command('up'));
 check(await battery.evaluate(el=>document.activeElement===el),'official gamepad up returns to battery @'+height);
 await battery.evaluate(()=>window.__command('up'));
 check(await playback.evaluate(el=>document.activeElement===el),'official gamepad up returns to playback @'+height);
 await page.evaluate(()=>{window.__wallpaperToggleTest.rejectNextWrite=true;});await battery.click();
 const failure=page.locator('[data-steam-wallpaper-error="pause-on-battery"]');await failure.waitFor();await settled(true);
 check((await failure.innerText()).includes('BATTERY_PAUSE_WRITE_REJECTED'),'save failure remains visible @'+height);
 check(await page.evaluate(()=>window.__wallpaperToggleTest.rejectedWrites===1),'one failed IPC write, no automatic retry @'+height);
 check(await battery.getAttribute('aria-checked')==='true','failed OFF retains last confirmed ON @'+height);
 await page.screenshot({path:path.join(out,'battery-save-failed-'+height+'.png')});
 await battery.click();await settled(false);saved=await readSaved();
 check(saved.steamWallpaperPauseOnBattery===false,'explicit battery OFF is persisted @'+height);
 check(await failure.count()===0,'successful retry clears visible failure @'+height);
 check(saved.videoBatteryPause===personalVideoPause,'personal video battery preference is independent @'+height);
 // Advance an unrelated native field at the existing memory IPC boundary.
 // Production settings publish changed from their actual committed receipt;
 // only that event can update this otherwise dormant battery control.
 await page.evaluate(()=>{window.__wallpaperToggleTest.concurrentBattery=true;});await download.click();await settled(true);
 check(await download.getAttribute('aria-checked')!==before.download,'unrelated download change committed @'+height);
 check(await page.evaluate(()=>window.__wallpaperToggleTest.concurrentWrites===1),'one concurrent native battery change injected @'+height);
 check(await battery.getAttribute('aria-checked')==='true','settings changed synchronizes external battery ON @'+height);
 await battery.click();await settled(false);await download.click();
 await page.waitForFunction(expected=>document.querySelector('[data-steam-wallpaper-setting="download"] [role="switch"]').getAttribute('aria-checked')===expected,before.download);
 saved=await readSaved();
 check(saved.steamWallpaperPauseOnBattery===false&&saved.videoBatteryPause===personalVideoPause,'saved OFF and old personal setting survive unrelated save @'+height);
 check(await playback.getAttribute('aria-checked')===before.playback,'battery preference never changes playback choice @'+height);
 await page.locator('[data-settings-section="library"]').click();await page.locator('[data-settings-section="wallpaper"]').click();
 check(await battery.getAttribute('aria-checked')==='false','return to wallpaper preserves battery OFF @'+height);
 await battery.scrollIntoViewIfNeeded();await page.screenshot({path:path.join(out,'battery-saved-off-'+height+'.png')});
 check(await page.evaluate(()=>window.__previewErrors.length===0),'no component errors in battery scenario @'+height);
 return {default:false,saved:saved.steamWallpaperPauseOnBattery,personalVideoPause,original:before,writeFailureVisible:true,changedSynchronized:true};
}
try{
 for(const height of [720,800]){
  const page=await browser.newPage({viewport:{width:1280,height}});
  page.on('pageerror',error=>errors.push(error.message));
  await page.addInitScript(()=>{
   const probe={rejectNextWrite:false,rejectedWrites:0,concurrentBattery:null,concurrentWrites:0,loadedEvents:0};
   window.__wallpaperToggleTest=probe;window.addEventListener('ui-settings:loaded',()=>probe.loadedEvents++);
   // The original memory IPC logs before dispatch; reject before mutation.
   // Production UI/repository/event/engine implementations remain unchanged.
   const calls=[];calls.push=function(...rows){const count=Array.prototype.push.apply(this,rows);for(const row of rows){if(row.cmd!=='settings.write')continue;if(probe.rejectNextWrite){probe.rejectNextWrite=false;probe.rejectedWrites++;throw Error('BATTERY_PAUSE_WRITE_REJECTED');}if(probe.concurrentBattery!==null){const next=JSON.parse(row.args.content);next.ui.steamWallpaperPauseOnBattery=probe.concurrentBattery;row.args.content=JSON.stringify(next);probe.concurrentBattery=null;probe.concurrentWrites++;}}return count;};window.__previewCalls=calls;
  });
  await page.goto(`http://127.0.0.1:${server.address().port}/?route=/steam`);
  await page.waitForFunction(()=>window.__previewReady===true);
  await page.locator('[data-settings-section="wallpaper"]').click();
  const playback=page.getByRole('switch',{name:'动态壁纸播放',exact:true});
  const download=page.locator('[data-steam-wallpaper-setting="download"] [role="switch"]');
  await playback.waitFor();
  const battery=page.getByRole('switch',{name:'离电关闭',exact:true});await battery.waitFor();
  if(!batteryOnly){
  const backgroundBefore=await download.getAttribute('aria-checked');
  check(await playback.getAttribute('aria-checked')==='true','saved/default ON is presented');
  check(await page.locator('.steam-wallpaper-playback-state').innerText()==='ON','explicit ON next to switch');
  await page.screenshot({path:path.join(out,`toggle-on-${height}.png`)});
  await playback.click();
  await page.waitForFunction(()=>document.querySelector('[aria-label="动态壁纸播放"]').getAttribute('aria-checked')==='false'&&!document.querySelector('[aria-label="动态壁纸播放"]').disabled);
  check(await page.locator('.steam-wallpaper-playback-state').innerText()==='OFF','explicit OFF after click');
  check((await page.locator('.steam-wallpaper-playback-help').innerText()).includes('静态背景'),'OFF explains static background');
  check(await download.getAttribute('aria-checked')===backgroundBefore,'playback OFF preserves independent download switch');
  const stored=await page.evaluate(()=>window.__previewCalls.filter(call=>call.cmd==='settings.write').map(call=>JSON.parse(call.args.content)).at(-1));
  check(stored.ui.steamDynamicWallpaperEnabled===false,'OFF saved by actual settings repository');
  await page.waitForFunction(()=>{const knob=document.querySelector('[aria-label="动态壁纸播放"] .ymcc-switch-knob');const transform=getComputedStyle(knob).transform;return transform==='none'||new DOMMatrix(transform).m41===0;});
  check(await playback.evaluate(element=>!element.classList.contains('on')),'OFF switch settles in its disabled-color left position');
  await page.screenshot({path:path.join(out,`toggle-off-${height}.png`)});
  await playback.evaluate(element=>{window.__gamepadAudit.focus(element);window.__command('confirm');});
  await page.waitForFunction(()=>document.querySelector('[aria-label="动态壁纸播放"]').getAttribute('aria-checked')==='true'&&!document.querySelector('[aria-label="动态壁纸播放"]').disabled);
  check(await playback.getAttribute('aria-checked')==='true','official gamepad A reenables playback');
  await playback.evaluate(element=>{window.__gamepadAudit.focus(element);window.__command('down');});
  check(await battery.evaluate(element=>document.activeElement===element),'gamepad down reaches battery pause control');
  await battery.evaluate(()=>window.__command('down'));
  check(await download.evaluate(element=>document.activeElement===element),'gamepad down continues to independent download control');
  await download.click();
  await page.waitForFunction(expected=>document.querySelector('[data-steam-wallpaper-setting="download"] [role="switch"]').getAttribute('aria-checked')!==expected,backgroundBefore);
  check(await playback.getAttribute('aria-checked')==='true','download changes do not disable playback');
  await playback.click();
  await page.waitForFunction(()=>document.querySelector('[aria-label="动态壁纸播放"]').getAttribute('aria-checked')==='false'&&!document.querySelector('[aria-label="动态壁纸播放"]').disabled);
  await page.locator('[data-settings-section="library"]').click();
  await page.locator('[data-settings-section="wallpaper"]').click();
  check(await playback.getAttribute('aria-checked')==='false','returning to wallpaper section preserves OFF');
  check(await page.evaluate(()=>window.__previewErrors.length===0),'no Vue component errors');
  }
  const batteryResult=await verifyBatteryToggle(page,height,playback,download,battery);
  rows.push({viewport:{width:1280,height},legacyPlaybackScenario:!batteryOnly,battery:batteryResult});
  await page.close();
 }
 check(errors.length===0,'no browser errors');
 for(const [name,sha] of Object.entries(sourceHashes))assert.equal(hash(path.join(root,name)),sha,'Tested source remained stable: '+name);
 const result={ok:true,selection:batteryOnly?'pause-on-battery':'all',checks,rows,errors,sourceHashes,scope:'Production SteamView, playback toggle, settings repository and official gamepad engine; native IPC in memory, no installed YMCC/Steam mutation.'};
 fs.writeFileSync(path.join(out,'toggle-ui-results.json'),JSON.stringify(result,null,2));console.log(JSON.stringify(result));
}finally{await browser.close();await new Promise(resolve=>server.close(resolve));}
