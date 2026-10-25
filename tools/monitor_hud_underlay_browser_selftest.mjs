// Actual shared HUD + actual settings component, offline and without hardware I/O.
import assert from 'node:assert/strict';
import fs from 'node:fs';import path from 'node:path';import {pathToFileURL} from 'node:url';import {createRequire} from 'node:module';
const root=process.cwd(),out=path.resolve(process.env.YMCC_MONITOR_UNDERLAY_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/tifa-underlay-20261013');
const {build}=createRequire(path.join(root,'package.json'))('esbuild');
const {chromium}=createRequire('C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/_hud-underlay.cjs')('playwright');
const helper=(await build({stdin:{contents:`import {normalizeMonitorOutput,sameHudAppearance} from './src/bridge/monitorHudModel.ts';import {prepareHudUnderlayFile} from './src/bridge/monitorHudUnderlayImport.ts';window.__underlayTest={normalizeMonitorOutput,sameHudAppearance,prepareHudUnderlayFile};`,resolveDir:root,loader:'ts'},bundle:true,format:'iife',write:false})).outputFiles[0].text;
let browser;const cases=[],errors=[];
try{
 browser=await chromium.launch({executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',headless:true});
 const page=await browser.newPage({viewport:{width:1920,height:1080}});page.on('pageerror',e=>errors.push(e.message));
 await page.goto(pathToFileURL(path.join(out,'gallery/index.html')).href+'?qa');await page.addScriptTag({content:helper});
 const layouts=['corner','bar','capsule','card','dashboard','glass','split','cinema','neon','circuit','prism','holo','tactical'];
 const hud=page.locator('[data-monitor-hud]');
 const settle=async()=>page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));
 const geometry=async()=>hud.evaluate(e=>{const r=e.getBoundingClientRect();return [r.width,r.height,...[...e.querySelectorAll('.hud-group')].flatMap(g=>{const b=g.getBoundingClientRect();return[b.x-r.x,b.y-r.y,b.width,b.height]})]});
 for(const width of [1280,1920]){
  await page.setViewportSize({width,height:width===1280?720:1080});
  for(const layout of layouts)for(const preset of ['minimal','standard','detailed'])for(const background of ['tifa','ac8-pink']){
   await page.evaluate(a=>window.__qa.set(...a),[layout,preset,1,.86]);await settle();const before=await geometry();
   await page.evaluate(a=>window.__qa.set(...a),[layout,preset,1,.86,null,null,false,true,background]);await settle();
   assert.deepEqual(await geometry(),before,layout+' picture must not change geometry');
   await page.waitForFunction(()=>{const e=document.querySelector('.hud-image-underlay');return e?.complete&&e.naturalWidth>0});
   const style=await page.locator('.hud-image-underlay').evaluate(e=>{const s=getComputedStyle(e),r=e.getBoundingClientRect(),h=e.parentElement.getBoundingClientRect();return {pointer:s.pointerEvents,filter:s.filter,blend:s.mixBlendMode,animation:s.animationName,opacity:Number(s.opacity),inside:r.width<=h.width+.1&&r.height<=h.height+.1}});
   assert.equal(style.pointer,'none');assert.equal(style.filter,'none');assert.equal(style.blend,'normal');assert.equal(style.animation,'none');assert(style.opacity>0&&style.opacity<.4);assert(style.inside);
   const hit=await hud.evaluate(e=>{const r=e.getBoundingClientRect(),x=Math.min(innerWidth-1,r.left+Math.min(20,r.width/2)),y=Math.min(innerHeight-1,r.top+Math.min(20,r.height/2));return document.elementFromPoint(x,y)?.closest('[data-monitor-hud]')===null});assert(hit);
   cases.push({width,layout,preset,background,geometryUnchanged:true,staticRaster:true,clickThrough:true});
  }
 }
 await page.evaluate(()=>window.__qa.set('neon','standard',1,0,null,null,false,true,'tifa'));await settle();assert.equal(await page.locator('.hud-image-underlay').evaluate(e=>Number(getComputedStyle(e).opacity)),.28);
 await page.evaluate(()=>window.__qa.set('neon','standard',1,.86,null,null,false,true,'tifa','',0));await settle();assert.equal(await page.locator('.hud-image-underlay').evaluate(e=>Number(getComputedStyle(e).opacity)),0);
 await page.evaluate(()=>window.__qa.set('neon','standard',1,.86,null,null,false,true,'tifa','',1));await settle();assert.equal(await page.locator('.hud-image-underlay').evaluate(e=>Number(getComputedStyle(e).opacity)),1);cases.push({label:'picture opacity is independent of panel background and supports both endpoints'});
 const invalid=await page.evaluate(()=>{const n=window.__underlayTest.normalizeMonitorOutput;return ['https://invalid/image.png','data:image/svg+xml;base64,PHN2Zz4=','data:image/webp;base64,'+'A'.repeat(45000)].map(underlayImage=>n({ymcc:{underlay:'custom',underlayImage}}).ymcc)});assert(invalid.every(x=>x.underlay==='none'&&x.underlayImage===''));
 const migration=await page.evaluate(()=>{const n=window.__underlayTest.normalizeMonitorOutput;return [n({ymcc:{underlay:'tifa'}}).ymcc.underlay,n({ymcc:{underlay:'none',underlayPreset:'ac8-pink'}}).ymcc.underlay,n({ymcc:{underlay:'tifa',underlayPreset:'none'}}).ymcc.underlay,n({ymcc:{underlay:'invented',underlayPreset:'invented'}}).ymcc.underlay]});assert.deepEqual(migration,['tifa','ac8-pink','none','none']);
 const pngCases=await page.evaluate(async()=>{
  const prepare=window.__underlayTest.prepareHudUnderlayFile;
  async function convert(c,name='sample.png',type='image/png'){const blob=await new Promise(r=>c.toBlob(r,'image/png'));const uri=await prepare(new File([blob],name,{type}));const img=new Image();img.src=uri;await img.decode();const out=document.createElement('canvas');out.width=img.width;out.height=img.height;const ctx=out.getContext('2d');ctx.drawImage(img,0,0);return {width:img.width,height:img.height,pixels:Array.from(ctx.getImageData(0,0,img.width,img.height).data),length:uri.length};}
  const transparent=document.createElement('canvas');transparent.width=240;transparent.height=320;const tc=transparent.getContext('2d');tc.fillStyle='rgba(220,50,90,.5)';tc.fillRect(40,30,160,260);tc.clearRect(80,90,30,40);const trimmed=await convert(transparent);const hole=(60*trimmed.width+40)*4+3;
  const opaque=document.createElement('canvas');opaque.width=96;opaque.height=120;opaque.getContext('2d').fillRect(0,0,96,120);const solid=await convert(opaque);
  let emptyRejected=false;try{await convert(document.createElement('canvas'))}catch{emptyRejected=true}
  const untyped=await convert(transparent,'photo.png','');
  return {trimmed:{width:trimmed.width,height:trimmed.height,alpha:trimmed.pixels[3],holeAlpha:trimmed.pixels[hole],gray:trimmed.pixels.slice(0,3),length:trimmed.length},solid:{width:solid.width,height:solid.height,alpha:solid.pixels[3]},emptyRejected,untyped:{width:untyped.width,height:untyped.height}};
 });
 assert.deepEqual([pngCases.trimmed.width,pngCases.trimmed.height],[160,260]);assert(Math.abs(pngCases.trimmed.alpha-128)<=1);assert.equal(pngCases.trimmed.holeAlpha,0);assert(Math.max(...pngCases.trimmed.gray)-Math.min(...pngCases.trimmed.gray)<=3);assert.deepEqual(pngCases.solid,{width:96,height:120,alpha:255});assert(pngCases.emptyRejected);assert.deepEqual(pngCases.untyped,{width:160,height:260});cases.push({label:'PNG transparent margins removed; soft alpha and holes retained; opaque background kept; empty PNG rejected; missing MIME accepted by signature',pngCases});
 const inputBytes=[...fs.readFileSync(path.join(out,'tifa-official-source.png'))];
 const imported=await page.evaluate(async bytes=>{const t=window.__underlayTest;const uri=await t.prepareHudUnderlayFile(new File([new Uint8Array(bytes)],'tifa.png',{type:'image/png'}));const img=new Image();img.src=uri;await img.decode();const c=document.createElement('canvas');c.width=img.width;c.height=img.height;const ctx=c.getContext('2d');ctx.drawImage(img,0,0);const p=ctx.getImageData(0,0,c.width,c.height).data;let maxColor=0;for(let i=0;i<p.length;i+=4)if(p[i+3]>0)maxColor=Math.max(maxColor,Math.abs(p[i]-p[i+1]),Math.abs(p[i+1]-p[i+2]));const prefs=t.normalizeMonitorOutput({ymcc:{underlay:'custom',underlayImage:uri}});return {uri,maxColor,width:img.width,height:img.height,normal:prefs.ymcc.underlay,payloadSize:new TextEncoder().encode(JSON.stringify({preferences:prefs,data:{frameTimes:Array(120).fill(8.2)}})).length,appearanceChanges:!t.sameHudAppearance(t.normalizeMonitorOutput().ymcc,prefs.ymcc)}},inputBytes);
 assert(imported.uri.length<=45000);assert(imported.width<=480&&imported.height<=480);assert(imported.maxColor<=3);assert.equal(imported.normal,'custom');assert(imported.payloadSize<65536);assert(imported.appearanceChanges);
 await page.evaluate(uri=>window.__qa.set('neon','standard',1,.86,null,null,false,true,'custom',uri),imported.uri);await settle();assert.equal(await page.locator('.hud-image-underlay').getAttribute('src'),imported.uri);
 await page.evaluate(()=>{window.__pictureMutations=0;new MutationObserver(es=>window.__pictureMutations+=es.length).observe(document.querySelector('.hud-image-underlay'),{attributes:true})});assert.equal(await page.evaluate(()=>window.__qa.updateSame()),0);assert.equal(await page.evaluate(()=>window.__pictureMutations),0);cases.push({label:'custom import becomes bounded grayscale once; repeated data does not change image or resize',bytes:imported.uri.length,maxColor:imported.maxColor,payloadBytes:imported.payloadSize});
 for(const type of ['image/svg+xml','image/gif']){assert(await page.evaluate(async type=>{try{await window.__underlayTest.prepareHudUnderlayFile(new File(['bad'],'bad',{type}));return false}catch{return true}},type))}
 await page.goto(pathToFileURL(path.join(out,'preview/index.html')).href);await page.waitForSelector('.monitor-image-underlay');
 assert.equal(await page.locator('.monitor-image-underlay [role=switch]').count(),0);
 assert.equal(await page.locator('.monitor-underlay-actions .dd-trigger').innerText(),'不使用');assert.equal(await page.locator('input[aria-label="图片透明度"]').inputValue(),'72');
 const selectPicture=async label=>{await page.locator('.monitor-underlay-actions .dd-trigger').click();assert.equal(await page.getByRole('option').count(),4);await page.getByRole('option',{name:label,exact:true}).click();};
 await selectPicture('Tifa Lockhart');await page.waitForSelector('.hud-image-underlay');await page.waitForFunction(()=>window.__fixture.settings.rtss.monitorOutput.ymcc.underlay==='tifa');
 const transparency=page.locator('input[aria-label="图片透明度"]');
 for(const value of [0,100,72]){await transparency.evaluate((el,v)=>{el.value=String(v);el.dispatchEvent(new Event('input',{bubbles:true}));el.dispatchEvent(new Event('change',{bubbles:true}));},value);await page.waitForFunction(v=>Math.abs(window.__fixture.settings.rtss.monitorOutput.ymcc.underlayOpacity-(1-v/100))<.00001,value);assert(Math.abs((await page.locator('.hud-image-underlay').evaluate(e=>Number(getComputedStyle(e).opacity)))-(1-value/100))<.00001);}
 const tifaSrc=await page.locator('.hud-image-underlay').getAttribute('src');
 await selectPicture('Tasha Seversky');await page.waitForFunction(()=>window.__fixture.settings.rtss.monitorOutput.ymcc.underlay==='ac8-pink');const ac8Src=await page.locator('.hud-image-underlay').getAttribute('src');assert.notEqual(ac8Src,tifaSrc);
 await selectPicture('不使用');await page.waitForFunction(()=>window.__fixture.settings.rtss.monitorOutput.ymcc.underlay==='none');assert.equal(await page.locator('.hud-image-underlay').count(),0);
 await selectPicture('Tasha Seversky');await page.waitForFunction(()=>window.__fixture.settings.rtss.monitorOutput.ymcc.underlay==='ac8-pink');assert.equal(await page.locator('.hud-image-underlay').getAttribute('src'),ac8Src);
 await page.getByRole('button',{name:'重置为预设',exact:true}).click();await page.waitForFunction(()=>!document.querySelector('.output-card button:disabled'));assert.equal(await page.evaluate(()=>window.__fixture.settings.rtss.monitorOutput.ymcc.underlay),'ac8-pink');
 await page.waitForSelector('[role=listbox]',{state:'hidden'});await page.screenshot({path:path.join(out,'settings-ac8-background.png'),fullPage:true});
 await selectPicture('自定义');await page.waitForFunction(()=>window.__fixture.settings.rtss.monitorOutput.ymcc.underlayPreset==='custom');assert.equal(await page.locator('.hud-image-underlay').count(),0);
 cases.push({label:'none, exactly two English built-in choices and custom; selection directly activates AC8 and survives reset; empty custom draws no false picture'});
 await page.locator('.monitor-image-underlay input[type=file]').setInputFiles(path.join(out,'tifa-official-source.png'));await page.waitForFunction(()=>window.__fixture.settings.rtss.monitorOutput.ymcc.underlay==='custom');assert((await page.locator('.hud-image-underlay').getAttribute('src')).startsWith('data:image/webp;base64,'));
 await selectPicture('不使用');await page.waitForFunction(()=>window.__fixture.settings.rtss.monitorOutput.ymcc.underlay==='none');assert.equal(await page.locator('.hud-image-underlay').count(),0);
 await selectPicture('自定义');await page.waitForFunction(()=>window.__fixture.settings.rtss.monitorOutput.ymcc.underlay==='custom');await page.getByRole('button',{name:'重置为预设',exact:true}).click();await page.waitForFunction(()=>!document.querySelector('.output-card button:disabled'));assert.equal(await page.evaluate(()=>window.__fixture.settings.rtss.monitorOutput.ymcc.underlay),'custom');
 await page.screenshot({path:path.join(out,'settings-picture-underlay.png'),fullPage:true});cases.push({label:'actual settings import immediately displays custom image; none/custom selection/reset preserves image'});
 assert.deepEqual(errors,[]);fs.writeFileSync(path.join(out,'UNDERLAY-QA.json'),JSON.stringify({ok:true,cases,errors,scope:'offline actual production Vue renderer/settings; no installed native/Steam/game test'},null,2));console.log('Underlay browser checks PASS:',cases.length);
}finally{await browser?.close()}
