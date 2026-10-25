// Real bundled CSS + captured local Valve rules in an isolated browser, never Steam/CDP.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createRequire} from 'node:module';
import {createHash} from 'node:crypto';
const root=process.cwd(), out=path.resolve(process.env.YMCC_APPEARANCE_TEST_OUT || 'G:/YeManCC-Work/Build/V/SteamFocus22/focus-css');
fs.mkdirSync(out,{recursive:true});
const {chromium}=createRequire('C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/_fixture.cjs')('playwright');
const pack='PowerControl/steam-appearance', read=name=>fs.readFileSync(path.join(root,name),'utf8');
const manifest=JSON.parse(read(pack+'/manifest.json'));
const native=read('tools/fixtures/steam-appearance-focus-native-20261022.css');
const prefix=pack+'/themes/Darken Unfocused Games/';
const hashes={};
function input(name){hashes[name]=createHash('sha256').update(fs.readFileSync(name)).digest('hex');return read(name);}
input('tools/fixtures/steam-appearance-focus-native-20261022.css');
function themeStyles(name,apply){
 const dir=pack+'/themes/'+name+'/',meta=JSON.parse(input(dir+'theme.json')),config=JSON.parse(input(dir+'config_USER.json'));
 if(name==='Darken Unfocused Games')config.Apply=apply;
 const files=new Set(),variables=[];
 for(const [file,targets] of Object.entries(meta.inject||{}))if(targets.includes('SP'))files.add(file);
 for(const [key,patch] of Object.entries(meta.patches||{})){
  const chosen=String(config[key]??patch.default),value=patch.values?.[chosen];
  if(value)for(const [entry,targets]of Object.entries(value)){
   if(entry.startsWith('--')&&targets[1]==='SP')variables.push(entry+':'+targets[0]);
   else if(entry.endsWith('.css')&&targets.includes('SP'))files.add(entry);
  }
 }
 return [...files].map(file=>input(dir+file)).join('\n')+'\n:root{'+variables.join(';')+'}';
}
const tileClass='appportrait_LibraryItemBox_WYgDg WYgDg9NyCcMIVuMyZ_NBC Focusable';
const tile=(id)=>`<div id="${id}" class="${tileClass}" tabindex="0"><div class="libraryassetimage_Container_1R9r2 _1R9r2OBCxAmtuUVrgBEUBw"><img class="libraryassetimage_Image_24_Au _24_AuLm54JVe1Zc0AApCDR" width="140" height="210" alt="cover" src="data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='140' height='210'%3E%3Crect width='140' height='210' fill='%236677cc'/%3E%3C/svg%3E"></div><button class="badge" tabindex="-1">菜单</button></div>`;
const html=`<body class="BasicUI"><main class="_54PuCatl_tYG836TOs4Mv"><section class="gamepadhome_RecentSection_39tNv"><div class="basicgamecarousel_BasicGameCarousel_3MdH5 ReactVirtualized__Grid__innerScrollContainer"><div id="recentParent"><div class="libraryassetimage_Container_1R9r2 basicgamecarousel_CarouselCapsuleBackgroundGlow_1H3Kf"></div>${tile('recent')}</div></div></section><section>${tile('library')}</section><button id="outside">outside</button></main></body>`;
let browser;const cases=[],errors=[],measurements=[];
try{
 browser=await chromium.launch({headless:true,executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
 const page=await browser.newPage({viewport:{width:1280,height:800}});page.on('pageerror',e=>errors.push(e.message));
 await page.setContent(html);await page.mouse.move(-100,-100);
 await page.addStyleTag({content:'body{margin:0}main{display:flex;gap:100px;padding:40px}section{width:140px}img{display:block}button.badge{position:absolute;right:0;bottom:0}'+native});
 const measure=async()=>page.evaluate(()=>Object.fromEntries(['recent','library'].map(id=>{const el=document.getElementById(id),s=getComputedStyle(el),r=el.getBoundingClientRect();return [id,{filter:s.filter,coverFilter:getComputedStyle(el.querySelector('img')).filter,badgeFilter:getComputedStyle(el.querySelector('.badge')).filter,transform:s.transform,outline:s.outline,transitionProperty:s.transitionProperty,width:r.width,height:r.height}]})));
 async function settle(){await page.evaluate(async()=>{await Promise.all(document.getAnimations().filter(a=>a.constructor.name==='CSSTransition').map(a=>a.finished.catch(()=>{})));});}
 for(const apply of ['Globally','Recent Only']){
  // Actual default CSS of all eight effects, with each manifest's SP patches.
  let css=manifest.themes.map(t=>themeStyles(t.name,apply)).join('\n');
  if(process.env.YMCC_APPEARANCE_FOCUS_NEGATIVE==='1'){
   const old='/* designated old selector mutation */\n'+(apply==='Globally'?'.BasicUI ':'.gamepadhome_RecentSection_39tNv .basicgamecarousel_BasicGameCarousel_3MdH5 ')+'.appportrait_LibraryItemBox_WYgDg {filter:brightness(var(--ren-game-tile-brightness)) saturate(var(--ren-game-tile-saturate));}';
   css=css.replace(input(prefix+(apply==='Globally'?'global.css':'recent-games.css')),old);
  }
  const style=await page.addStyleTag({content:css});await settle();
  let m=await measure();assert.equal(m.recent.filter,'brightness(0.9)',apply+': parent keeps native filter');assert.equal(m.recent.coverFilter,'brightness(0.5) saturate(0.5)');assert.equal(m.recent.badgeFilter,'none');
  assert.equal(m.library.filter,'brightness(0.9)');if(apply==='Globally')assert.equal(m.library.coverFilter,'brightness(0.5) saturate(0.5)');else assert.equal(m.library.coverFilter,'none');
  const baseline=m;
  cases.push(apply+': only its configured scope is dimmed, all eight source themes loaded');
  for(const state of ['gpfocus','gpfocuswithin','gpfocus-within']){
   await page.locator('#recent').evaluate((el,state)=>el.classList.add(state),state);await settle();m=await measure();
   assert.equal(m.recent.filter,'brightness(0.9)',apply+': '+state+' must leave dimming to Steam');assert.equal(m.recent.coverFilter,'none');
   assert.equal(m.recent.transform,baseline.recent.transform);assert.equal(m.recent.width,baseline.recent.width);assert.equal(m.recent.height,baseline.recent.height);
   await page.locator('#recent').evaluate((el,state)=>el.classList.remove(state),state);await settle();
  }
  cases.push(apply+': gamepad focus classes exclude dimming without changing transform or size');
  await page.locator('#recent').evaluate(el=>el.classList.add('appportrait_ShowAsHovered_2YsY_','_2YsY_x7hLyKlUqjX9bhn0R'));await settle();m=await measure();assert.equal(m.recent.coverFilter,'none');assert.equal(m.recent.filter,'brightness(1) contrast(0.95) saturate(1)');
  await page.locator('#recent').evaluate(el=>el.classList.remove('appportrait_ShowAsHovered_2YsY_','_2YsY_x7hLyKlUqjX9bhn0R'));await settle();
  cases.push(apply+': Steam programmatic hover state stays bright without a pointer or DOM focus');
  await page.locator('#recent').focus();await settle();m=await measure();assert.equal(m.recent.filter,'brightness(1)');assert.equal(m.recent.coverFilter,'none');assert.notEqual(m.recent.transform,'none');
  await page.locator('#outside').focus();await settle();
  await page.locator('#recent .badge').focus();await settle();m=await measure();assert.equal(m.recent.filter,'brightness(0.9)');assert.equal(m.recent.coverFilter,'none');
  await page.locator('#outside').focus();await settle();
  cases.push(apply+': real DOM focus and child focus preserve native focus visuals');
  await page.locator('#recent').hover();await settle();m=await measure();assert.equal(m.recent.coverFilter,'none');
  await page.mouse.down();await settle();m=await measure();assert.equal(m.recent.coverFilter,'none');await page.mouse.up();await page.mouse.move(-100,-100);await settle();
  cases.push(apply+': pointer hover/press retain Steam filters and menu remains clickable');
  let clicks=0;await page.exposeFunction('recordBadgeClick'+apply,()=>clicks++);await page.locator('#recent .badge').evaluate((el,fn)=>el.addEventListener('click',()=>window[fn]()),'recordBadgeClick'+apply);await page.locator('#recent .badge').click();assert.equal(clicks,1);await page.locator('#outside').focus();await page.mouse.move(-100,-100);await settle();
  // Class-first and DOM-first handoffs are separate frames in Steam. Check each frame's selector membership.
  const handoff=await page.evaluate(async()=>{
   const a=document.getElementById('recent'),b=document.getElementById('library'),observed=[];
   const rules=[...document.styleSheets].flatMap(sheet=>[...sheet.cssRules]).filter(r=>r.selectorText?.includes('appportrait_LibraryItemBox_WYgDg')&&r.style.filter?.includes('--ren-game-tile-brightness'));
   for(let i=0;i<40;i++){
    const el=i%2?a:b,other=i%2?b:a;other.classList.remove('gpfocus');el.classList.add('gpfocus');
    observed.push(rules.some(r=>el.matches(r.selectorText)||el.querySelector('img').matches(r.selectorText)));await new Promise(requestAnimationFrame);el.focus();
    observed.push(rules.some(r=>el.matches(r.selectorText)||el.querySelector('img').matches(r.selectorText)));await new Promise(requestAnimationFrame);
   }
   a.blur();b.blur();a.classList.remove('gpfocus');b.classList.remove('gpfocus');return observed;
  });
  assert(handoff.every(v=>!v));await settle();cases.push(apply+': 80 focus-handoff frames never match the dimming rule while focused');
  await page.addStyleTag({content:':root{--ren-game-tile-brightness:0.2;--ren-game-tile-saturate:0.8}'}).then(async vars=>{await settle();m=await measure();assert.equal(m.recent.coverFilter,'brightness(0.2) saturate(0.8)');assert.equal(m.recent.filter,'brightness(0.9)');await vars.evaluate(el=>el.remove());});
  cases.push(apply+': existing brightness and saturation controls remain effective');
  await page.locator('#recent').evaluate(el=>el.classList.add('gpfocus'));await page.locator('#recent').focus();await settle();
  const dwell=await page.evaluate(async()=>{const el=document.getElementById('recent'),values=[];for(let i=0;i<120;i++){await new Promise(requestAnimationFrame);values.push({parent:getComputedStyle(el).filter,cover:getComputedStyle(el.querySelector('img')).filter});}return values;});
  assert(dwell.every(v=>v.parent==='brightness(1)'&&v.cover==='none'));
  await page.locator('#recent').evaluate(el=>el.classList.remove('gpfocus'));await page.locator('#outside').focus();await settle();
  cases.push(apply+': 120 stationary-focus frames with native keyframes keep cover and parent filter stable');
  measurements.push({apply,baseline,handoffFrames:handoff.length});
  if(apply==='Globally')await page.screenshot({path:path.join(out,'themes-unfocused.png')});
  await style.evaluate(el=>el.remove());await settle();
 }
 assert.deepEqual(errors,[]);
 const record={ok:true,cases,passed:cases.length,sourceHashes:hashes,measurements,errors,realSteamOperations:0,realCssOperations:0,limitation:'CSS selector/cascade browser regression; not a real Steam GPU frame/flicker acceptance'};
 fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(record,null,2));console.log(JSON.stringify(record,null,2));
}finally{await browser?.close();}
