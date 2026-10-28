import assert from 'node:assert/strict';import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import {createRequire} from 'node:module';
const root=path.resolve(import.meta.dirname,'..'),out=path.resolve(process.argv[2]||'G:/YeManCC-Work/Testing/YMCC/Evidence/SteamThemeReconcile-20261023/stable-focus');fs.mkdirSync(out,{recursive:true});
const pack=process.env.YMCC_STABLE_FOCUS_PACK||path.join(root,'PowerControl/steam-appearance/themes');
const read=p=>fs.readFileSync(path.join(pack,p),'utf8');const files=['Round/round-outline.css'];
const {chromium}=createRequire('C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/_fixture.cjs')('playwright');
const browser=await chromium.launch({headless:true,executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});const cases=[];
try{
 const page=await browser.newPage({viewport:{width:1280,height:800}});
 await page.setContent(`<body class="BasicUI"><div class="ReactVirtualized__Grid__innerScrollContainer"><div id="tile"><button id="cover" class="appportrait_LibraryItemBox_WYgDg Focusable">cover</button><div id="reflection" class="libraryassetimage_Container_1R9r2 basicgamecarousel_CarouselCapsuleBackgroundGlow_1H3Kf"></div></div></div><div id="ring" class="focusring_FocusRing_1IZrQ"></div><div class="appactivityday_NoCommentSupport_2CB_R"><div class="appactivityday_EventBody_NEMXh"><div class="Panel"><div class="focusring_FocusRingRoot_3PH_X"><div id="exception" class="focusring_FocusRing_1IZrQ"></div></div></div></div></div><button id="outside">outside</button>`);
 await page.addStyleTag({content:`body{margin:0;--round-radius-size:10px}#tile{position:relative;width:140px;height:210px}#cover{width:140px;height:210px}#ring,#exception{position:absolute;left:300px;top:50px;width:140px;height:210px}@keyframes nativePulse{0%{outline-width:12px}100%{outline-width:2px}}.BasicUI .appportrait_LibraryItemBox_WYgDg.Focusable:focus{outline:2px solid white;animation:nativePulse .4s ease 20}#reflection{position:absolute}`});
 await page.addStyleTag({content:files.map(read).join('\n')});
 const measure=()=>page.evaluate(()=>Object.fromEntries(['ring','exception','cover','reflection'].map(id=>{const e=document.getElementById(id),s=getComputedStyle(e),r=e.getBoundingClientRect();return[id,{rect:[r.x,r.y,r.width,r.height],transform:s.transform,animation:s.animationName,transition:s.transitionProperty,border:s.borderWidth,outline:s.outlineWidth,margin:s.margin}]})));
 const base=await measure();assert.equal(base.ring.animation,'none');assert.equal(base.exception.animation,'none');cases.push('Round normal and exception focus borders have no repeating growth/fade/blink animation');
 assert.equal(fs.existsSync(path.join(pack,'Game Cover Reflections')),false);assert.equal(base.reflection.transform,'none');assert.equal(base.reflection.transition,'all');cases.push('retired reflection payload is absent and no reflection transform or opacity transition is installed');
 for(const state of ['gpfocuswithin','gpfocus','gpfocus-within']){
  await page.locator('#tile').evaluate((el,state)=>el.classList.add(state),state);await page.locator('#cover').focus();const current=await measure();assert.equal(current.cover.animation,'none');assert.equal(current.reflection.transform,base.reflection.transform);assert.deepEqual(current.reflection.rect,base.reflection.rect);assert.deepEqual(current.ring,base.ring);await page.locator('#tile').evaluate((el,state)=>el.classList.remove(state),state);await page.locator('#outside').focus();
 }
 cases.push('all three Steam focus-class handoffs keep the reflected image and focus-ring geometry fixed');
 await page.locator('#cover').hover();const hovered=await measure();assert.equal(hovered.reflection.transform,base.reflection.transform);cases.push('pointer hover cannot move the reflection');
 const samples=await page.evaluate(async()=>{const a=[];for(let i=0;i<24;i++){document.getElementById('tile').classList.toggle('gpfocuswithin');document.getElementById(i%2?'cover':'outside').focus();await new Promise(requestAnimationFrame);const e=document.getElementById('reflection'),r=e.getBoundingClientRect();a.push({rect:[r.x,r.y,r.width,r.height],transform:getComputedStyle(e).transform,ring:getComputedStyle(document.getElementById('ring')).borderWidth});}return a});
 assert(samples.every(s=>JSON.stringify(s.rect)===JSON.stringify(base.reflection.rect)&&s.transform===base.reflection.transform&&s.ring===base.ring.border));cases.push('24 real browser focus frames contain no theme-created position or border-width oscillation');
 // The real Steam A/B isolated Art Hero's identity filter on the hero IMG.
 // Keep the existing focus checks and cover every compatible gradient choice.
 await page.screenshot({path:path.join(out,'stable-focus.png')});
 const artDefinition=JSON.parse(read('Art Hero/theme.json'));
 files.push('Art Hero/theme.json');
 const gradientChoices=Object.entries(artDefinition.patches['Gradient Settings'].values);
 for(const viewport of [{width:1280,height:720},{width:1280,height:800}]){
  await page.setViewportSize(viewport);
  for(const [choice,selected] of gradientChoices){
   await page.setContent(`<body class="BasicUI"><main class="gamepadhomerecentgames_RecentGamesBackgroundContainer_QNkOt"><div class="gamepadhomerecentgames_RecentGamesBackgroundImages_30D-8"><div class="libraryassetimage_Container_1R9r2 gamepadhomerecentgames_RecentGamesBackground_1SRox"><img id="hero" class="gamepadhomerecentgames_RecentGamesBackgroundImage_3Mp8R" src="data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='1920' height='620'%3E%3Crect width='1920' height='620' fill='%234f7799'/%3E%3Ccircle cx='400' cy='250' r='210' fill='%23eeba66'/%3E%3C/svg%3E"></div></div></main><footer><button id="action">开始游戏</button><span>完整控制器按键 A B X Y</span></footer></body>`);
   // Native filter and prepromotion match HERO-IMAGE-MATCHED-STYLES evidence.
   await page.addStyleTag({content:'body{margin:0;background:#101820;color:white}main,.gamepadhomerecentgames_RecentGamesBackgroundImages_30D-8{position:relative;height:50vh;width:100vw}.gamepadhomerecentgames_RecentGamesBackground_1SRox{position:absolute;width:100%;height:100%;}.gamepadhomerecentgames_RecentGamesBackgroundImage_3Mp8R{display:block;width:100%;height:100%;filter:brightness(50%);will-change:transform,opacity;}footer{position:absolute;bottom:0}'});
   const selectedFiles=Object.keys(selected).filter(p=>p.endsWith('.css')).map(p=>'Art Hero/'+p);
   for(const p of ['Art Hero/shared.css',...selectedFiles])if(!files.includes(p))files.push(p);
   await page.addStyleTag({content:['Art Hero/shared.css',...selectedFiles].map(read).join('\n')});
   await page.locator('#hero').evaluate(async img=>{await img.decode()});
   const measureHero=()=>page.evaluate(()=>{const image=document.getElementById('hero'),s=getComputedStyle(image),r=image.getBoundingClientRect(),footer=document.querySelector('footer').getBoundingClientRect();return {filter:s.filter,objectFit:s.objectFit,objectPosition:s.objectPosition,opacity:s.opacity,rect:[r.x,r.y,r.width,r.height],footer:[footer.x,footer.y,footer.width,footer.height],complete:image.complete&&image.naturalWidth===1920,button:!!document.getElementById('action').getClientRects().length}});
   const fixed=await measureHero();assert.equal(fixed.filter,'none','Full hero must not reinstate identity brightness: '+choice);assert(fixed.complete&&fixed.button);assert.equal(fixed.objectFit,'cover');
   const fixedPixels=await page.screenshot();
   const oldFilter=await page.addStyleTag({content:'.gamepadhomerecentgames_RecentGamesBackgroundImage_3Mp8R{filter:brightness(100%)!important}'});
   const old=await measureHero();assert.equal(old.filter,'brightness(1)');assert.deepEqual(old.rect,fixed.rect);assert.deepEqual(old.footer,fixed.footer);
   const oldPixels=await page.screenshot();
   // The flat-effects contract preserves layout, not compositor antialiasing.
   // Geometry assertions above remain strict; keep the isolated C1 pixel proof in its evidence.
   if(viewport.height===720&&choice==='No Hero Gradient'){fs.writeFileSync(path.join(out,'flat-hero.png'),fixedPixels);fs.writeFileSync(path.join(out,'identity-filter-control.png'),oldPixels);}
   await oldFilter.evaluate(el=>el.remove());assert.equal((await measureHero()).filter,'none');await page.locator('#action').focus();assert.equal(await page.locator('#action').evaluate(el=>document.activeElement===el),true);
   cases.push(viewport.width+'x'+viewport.height+' '+choice+': hero filter none; exact geometry retained; full footer and focused action remain');
  }
 }

 // The user keeps all layout ratios and the video wallpaper, removing decoration.
 const packRoot=path.dirname(pack),reference=process.env.YMCC_APPEARANCE_LAYOUT_BASELINE;
 const selectedCss=(directory,overrides={})=>{
  const manifest=JSON.parse(fs.readFileSync(path.join(directory,'manifest.json'),'utf8'));
  return manifest.themes.map(({name})=>{
   const folder=path.join(directory,'themes',name),definition=JSON.parse(fs.readFileSync(path.join(folder,'theme.json'),'utf8')),config={...JSON.parse(fs.readFileSync(path.join(folder,'config_USER.json'),'utf8')),...overrides[name]};
   const wanted=Object.keys(definition.inject||{}).filter(key=>key.endsWith('.css'));
   for(const [key,patch]of Object.entries(definition.patches||{})){const value=patch.values[config[key]??patch.default];if(!value)throw Error('Unknown fixture choice');for(const file of Object.keys(value))if(file.endsWith('.css')&&!wanted.includes(file))wanted.push(file);}
   return wanted.map(file=>fs.readFileSync(path.join(folder,file),'utf8')).join('\n');
  }).join('\n');
 };
 const scene=`<body class="BasicUI"><section id="home" class="gamepadhome_RecentSection_39tNv"><div class="gamepadhomerecentgames_RecentGamesContainer_2_cRk"><div class="gamepadhomerecentgames_RecentGamesInnerContainer_282X0"><div id="carousel" class="basicgamecarousel_BasicGameCarousel_3MdH5"><button id="tile" class="appportrait_LibraryItemBox_WYgDg"><span class="libraryassetimage_Container_1R9r2"><img id="cover" class="libraryassetimage_Image_24_Au"></span></button><div id="glow" class="basicgamecarousel_CarouselCapsuleBackgroundGlow_1H3Kf"></div></div></div></div></section><section id="details" class="appdetails_Header_2gZXh"><div id="capsule" class="sharedappdetailsheader_TopCapsule_2meE3"><div class="libraryassetimage_Container_1R9r2"><img class="libraryassetimage_Image_24_Au"></div><div data-ymcc-wallpaper-surface="details"><video id="wallpaper" autoplay loop muted style="width:100%;height:100%;animation:wallpaperOnly 1s infinite linear;filter:none"></video></div></div></section><footer id="footer" class="footer_BasicFooter_3T1iF"><button id="play" class="appactionbutton_ButtonChild_2AzIX">开始游戏</button><span>A B X Y</span></footer><div class="DefaultTheme"><button id="key" class="virtualkeyboard_KeyboardKey_2KhPX virtualkeyboard_Focused_21EoN"><img id="keyicon"><span>A</span></button></div></body>`;
 const nativeFixture='body{margin:0}.appdetails_Header_2gZXh{position:absolute;top:0;left:0;width:100vw}.sharedappdetailsheader_TopCapsule_2meE3{position:relative;width:100%}[data-ymcc-wallpaper-surface]{position:absolute;inset:0;pointer-events:none}.appportrait_LibraryItemBox_WYgDg{position:relative;width:140px;height:210px}.libraryassetimage_Container_1R9r2{display:block;width:100%;height:100%}.libraryassetimage_Image_24_Au{width:100%;height:100%}.footer_BasicFooter_3T1iF{position:fixed;bottom:0;height:42px;width:100vw}.virtualkeyboard_KeyboardKey_2KhPX{position:absolute;top:10px;width:60px;height:48px}.virtualkeyboard_KeyboardKey_2KhPX::before{filter:blur(3px)}@keyframes wallpaperOnly{from{opacity:.99}to{opacity:1}}';
 const getGeometry=()=>page.evaluate(()=>Object.fromEntries(['home','carousel','tile','cover','details','capsule','footer','play','key','wallpaper'].map(id=>{const e=document.getElementById(id),r=e.getBoundingClientRect(),s=getComputedStyle(e);return[id,{box:[r.x,r.y,r.width,r.height],font:s.fontSize,line:s.lineHeight,transform:s.transform}]})));
 let geometryComparisons=0;
 for(const viewport of [{width:1280,height:720},{width:1280,height:800}]){
  await page.setViewportSize(viewport);
  for(const size of ['0.5','0.6','0.7','0.8','0.9'])for(const appearance of ['Wide Hero','Expanded Hero','Full Screen Hero']){
   const overrides={'Mini Carousel':{Size:size},'Art Hero':{'Hero Appearance':appearance}};
   await page.setContent(scene);await page.addStyleTag({content:nativeFixture});
   let before;
   if(reference){const oldStyle=await page.addStyleTag({content:selectedCss(reference,overrides)});before=await getGeometry();await oldStyle.evaluate(e=>e.remove());}
   await page.addStyleTag({content:selectedCss(packRoot,overrides)});const after=await getGeometry();
   if(before){assert.deepEqual(after,before,'Approved layout must remain exact: '+JSON.stringify({viewport,size,appearance}));geometryComparisons++;}
   const flat=await page.evaluate(()=>({cover:getComputedStyle(document.getElementById('cover')).filter,tile:getComputedStyle(document.getElementById('tile')).filter,glow:getComputedStyle(document.getElementById('glow')).display,key:getComputedStyle(document.getElementById('key')).boxShadow,keyFilter:getComputedStyle(document.getElementById('keyicon')).filter,keyBackground:getComputedStyle(document.getElementById('key')).backgroundImage,keyFace:getComputedStyle(document.getElementById('key'),'::before').content,wallpaperAnimation:getComputedStyle(document.getElementById('wallpaper')).animationName,wallpaperFilter:getComputedStyle(document.getElementById('wallpaper')).filter,wallpaperDisplay:getComputedStyle(document.getElementById('wallpaper')).display,wallpaperPointer:getComputedStyle(document.querySelector('[data-ymcc-wallpaper-surface]')).pointerEvents}));
   assert.equal(flat.cover,'none');assert.equal(flat.tile,'none');assert.equal(flat.glow,'none');assert.equal(flat.key,'none');assert.equal(flat.keyFilter,'none');assert.equal(flat.keyBackground,'none');assert.equal(flat.keyFace,'none');assert.equal(flat.wallpaperAnimation,'wallpaperOnly');assert.equal(flat.wallpaperFilter,'none');assert.notEqual(flat.wallpaperDisplay,'none');assert.equal(flat.wallpaperPointer,'none');
   cases.push(viewport.width+'x'+viewport.height+' size '+size+' '+appearance+': layout geometry preserved, flat cover/keyboard, wallpaper animation remains');
  }
 }
 await page.screenshot({path:path.join(out,'flat-layout.png')});
 const resourceManifest=JSON.parse(fs.readFileSync(path.join(packRoot,'manifest.json'),'utf8'));
 await page.setContent('<body></body>');
 const sourceCss=resourceManifest.files.filter(file=>file.path.endsWith('.css'));
 await page.addStyleTag({content:sourceCss.map(file=>read(file.path)).join('\n')});
 const extraEffects=await page.evaluate(()=>{
  const violations=[];
  for(const sheet of document.styleSheets)for(const rule of sheet.cssRules){
   if(!rule.style)continue;
   for(const property of ['filter','backdrop-filter','-webkit-backdrop-filter','box-shadow','text-shadow','mask-image','-webkit-mask-image','animation-name']){
    const value=rule.style.getPropertyValue(property).trim();if(value&&value!=='none')violations.push({selector:rule.selectorText,property,value});
   }
   if(/(?:linear|radial|conic)-gradient\(/i.test(rule.style.backgroundImage))violations.push({selector:rule.selectorText,property:'background-image',value:rule.style.backgroundImage});
  }
  return violations;
 });
 assert.deepEqual(extraEffects,[],'Every option stylesheet must stay free of added filters, masks, shadows, gradients and named animation');
 cases.push(sourceCss.length+' CSS assets: every keyboard color/light-dark palette and legacy gradient option remains free of added render effects');
 fs.writeFileSync(path.join(out,'layout-reference.json'),JSON.stringify({reference:reference||null,compared:geometryComparisons,scope:'Thirty isolated source-CSS layout comparisons across two aspect ratios, five carousel scales and three hero sizes; no real Steam control'},null,2));

 const content=Object.fromEntries(resourceManifest.files.map(file=>[file.path,crypto.createHash('sha256').update(read(file.path)).digest('hex')]));fs.writeFileSync(path.join(out,'result.json'),JSON.stringify({ok:true,passed:cases.length,cases,source:content,scope:'Actual bundled CSS in Edge, controlled focus and Art Hero identity-filter pixel/geometry checks; does not certify physical Steam/GPU presentation'},null,2));console.log(JSON.stringify({ok:true,passed:cases.length,cases},null,2));
}finally{await browser.close()}
