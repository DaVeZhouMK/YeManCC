import fs from 'node:fs';
import path from 'node:path';
import http from 'node:http';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
const evidence=path.resolve(process.argv[2]||''),out=path.resolve(process.argv[3]||'');
if(!process.argv[2]||!process.argv[3])throw Error('Usage: node tools/steam_wallpaper_toggle_ui_browser_selftest.mjs <built UI evidence> <output>');
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
const {chromium}=createRequire('C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/package.json')('playwright');
const browser=await chromium.launch({channel:'msedge',headless:true});
const errors=[],rows=[];let checks=0;
const check=(value,label)=>{checks++;assert.ok(value,label);};
try{
 for(const height of [720,800]){
  const page=await browser.newPage({viewport:{width:1280,height}});
  page.on('pageerror',error=>errors.push(error.message));
  await page.goto(`http://127.0.0.1:${server.address().port}/?route=/steam`);
  await page.waitForFunction(()=>window.__previewReady===true);
  await page.locator('[data-settings-section="wallpaper"]').click();
  const playback=page.getByRole('switch',{name:'动态壁纸播放',exact:true});
  const download=page.locator('[data-steam-wallpaper-setting="download"] [role="switch"]');
  await playback.waitFor();
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
  check(await download.evaluate(element=>document.activeElement===element),'gamepad down reaches independent download control');
  await download.click();
  await page.waitForFunction(expected=>document.querySelector('[data-steam-wallpaper-setting="download"] [role="switch"]').getAttribute('aria-checked')!==expected,backgroundBefore);
  check(await playback.getAttribute('aria-checked')==='true','download changes do not disable playback');
  await playback.click();
  await page.waitForFunction(()=>document.querySelector('[aria-label="动态壁纸播放"]').getAttribute('aria-checked')==='false'&&!document.querySelector('[aria-label="动态壁纸播放"]').disabled);
  await page.locator('[data-settings-section="library"]').click();
  await page.locator('[data-settings-section="wallpaper"]').click();
  check(await playback.getAttribute('aria-checked')==='false','returning to wallpaper section preserves OFF');
  check(await page.evaluate(()=>window.__previewErrors.length===0),'no Vue component errors');
  rows.push({viewport:{width:1280,height},saved:stored.ui.steamDynamicWallpaperEnabled,backgroundBefore});
  await page.close();
 }
 check(errors.length===0,'no browser errors');
 const result={ok:true,checks,rows,errors,scope:'Production SteamView, playback toggle, settings repository and official gamepad engine; native IPC in memory, no installed YMCC/Steam mutation.'};
 fs.writeFileSync(path.join(out,'toggle-ui-results.json'),JSON.stringify(result,null,2));console.log(JSON.stringify(result));
}finally{await browser.close();await new Promise(resolve=>server.close(resolve));}
