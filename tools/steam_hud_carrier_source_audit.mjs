// Offline audit of downloaded upstream code + installed Steam source bytes.
// Evaluates ONLY individual Steam popup functions in a VM with inert window/document mocks.
// No CDP, browser launch, Steam process access, input, desktop capture, or production deployment.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import vm from 'node:vm';import crypto from 'node:crypto';import {createRequire} from 'node:module';
const require=createRequire(import.meta.url),ts=require('typescript');
const dir=path.resolve(process.argv[2]||'');if(!dir.startsWith('G:\\YeManCC-Work\\Build\\Tasks\\YMCC-Steam-HUD-Probe\\source-review-'))throw Error('Owned source-review directory required');
const read=n=>fs.readFileSync(path.join(dir,n),'utf8');
const library=read('library-module-11131.source.txt'),popup=read('chunk~2dcc5aaf7-module-83718.source.txt'),hooks=read('library-module-63439.source.txt'),nativePins=read('chunk~2dcc5aaf7-module-51582.source.txt');
const source=ts.createSourceFile('installed-steam-popup-module.js',library,ts.ScriptTarget.Latest,true,ts.ScriptKind.JS);
function pick(predicate){const found=[];function visit(n){if(predicate(n))found.push(n);ts.forEachChild(n,visit);}visit(source);assert.equal(found.length,1,'Unique native source method required');return found[0].getText(source);}
const createPopup=pick(n=>ts.isMethodDeclaration(n)&&n.name?.getText(source)==='CreatePopup'&&n.modifiers?.some(m=>m.kind===ts.SyntaxKind.StaticKeyword));
const renderInternal=pick(n=>ts.isMethodDeclaration(n)&&n.name?.getText(source)==='RenderInternal');
const flagsFunction=pick(n=>ts.isFunctionDeclaration(n)&&n.name?.text==='S');
const flags=Object.fromEntries([...library.matchAll(/e2\[e2\.(\w+)\s*=\s*(\d+)\]\s*=\s*"\1"/g)].map(m=>[m[1],Number(m[2])]).filter(([n])=>!['Overlay','Notification','Tooltip','PopupContextMenu','StandaloneContextMenu'].includes(n)));
const calls=[],documents=[],cssPropagation=[];
const inertDocument=()=>({title:'',write(html){documents.push(html);},close(){},getElementById(id){return {inert:true,id};},body:{className:''}});
const fakeWindow=(name)=>({name,document:inertDocument(),open(url,target,features){calls.push({owner:name,url,target,features});return {document:inertDocument()};}});
const defaults=fakeWindow('inert-default-window');
const context=vm.createContext({n:flags,i:{Overlay:0,Notification:1,Tooltip:2,PopupContextMenu:3,StandaloneContextMenu:4},
  window:defaults,console:{error(){}},v:()=>0,p:{Oe:(window,styles)=>cssPropagation.push({window:!!window,styles}),yU:()=>({inertStyles:true})},
  a:{BK:()=>false},o:{iEc:{k_EWindowBringToFrontInvalid:-1}}});
vm.runInContext(`globalThis.getFlags=(${flagsFunction});globalThis.Popup=class {${createPopup}\n${renderInternal}};`,context,{timeout:1000});
const tests=[];const check=(name,fn)=>{fn();tests.push(name);console.log('PASS '+name);};
const sourceLink={m_unPID:12345,m_nBrowserID:678,m_eBrowserType:0};
const params=()=>({title:'YMCC offline carrier test',target_browser:{...sourceLink},owner_window:fakeWindow('inert-game-owner'),bPinned:true,
  eCreationFlags:context.getFlags(1),dimensions:{width:220,height:190,left:22,top:22},html_class:'ymcc-test',body_class:'ymcc-test',popup_class:'ymcc-test'});
check('Steam native Notification factory includes transparent, non-focusable, forced-visible flags',()=>{const bits=context.getFlags(1);for(const k of ['NotFocusable','TransparentParentWindow','ForceBrowserVisible','NoTaskbarIcon','NoWindowShadow'])assert.equal(bits&flags[k],flags[k]);});
check('legacy Overlay flag set does not include ForceBrowserVisible',()=>assert.equal(context.getFlags(0)&flags.ForceBrowserVisible,0));
check('native pin call is made against referenced CHILD popup, not the root',()=>assert.ok(nativePins.includes('r2.current.SteamClient.Overlay.SetWindowPinned(!n2)')));
check('Steam native OverlayPopup passes owner-context browser identity and creation-time pin',()=>{assert.ok(popup.includes('target_browser: G.params.browserInfo'));assert.ok(popup.includes('bPinned: D'));assert.ok(popup.includes('createPortal('));});
check('native OverlayPopup allows no title controls and no saved dimensions path',()=>{assert.ok(popup.includes('bHideWindowControls: P'));assert.ok(popup.includes('M ? W : {}'));});
check('native generic popup hook closes window on unmount and can avoid forced focus',()=>{assert.ok(hooks.includes('bNoFocusOnShow'));assert.ok(hooks.includes('k_EWindowBringToFrontWithoutForcingOS'));assert.ok(hooks.includes('i2.current.Close()'));});
check('exact native CreatePopup routes about:blank to game browser and pins at creation',()=>{context.Popup.CreatePopup('ymcc-offline-only',params());const c=calls.at(-1),u=new URL(c.url);assert.equal(u.protocol,'about:');assert.equal(u.pathname,'blank');assert.equal(u.searchParams.get('pid'),'12345');assert.equal(u.searchParams.get('browser'),'678');assert.equal(u.searchParams.get('pinned'),'true');assert.equal(c.owner,'inert-game-owner');});
check('native factory honors dimensions without loading an external HTML server',()=>{assert.ok(calls.at(-1).features.includes('width=220,height=190,left=22,top=22'));assert.ok(documents.at(-1).includes('id="popup_target"'));assert.equal(cssPropagation.length,1);});
check('creation-time non-pinned alternative does not fabricate native pin state',()=>{const p=params();p.bPinned=false;context.Popup.CreatePopup('ymcc-offline-only-2',p);assert.equal(new URL(calls.at(-1).url).searchParams.get('pinned'),null);});
check('explicit native browserType survives factory translation',()=>{const p=params();p.target_browser.m_eBrowserType=9;context.Popup.CreatePopup('ymcc-offline-only-3',p);assert.equal(new URL(calls.at(-1).url).searchParams.get('browserType'),'9');});
check('no OS focus call on native RenderInternal invalid-focus path',()=>{const events=[];const obj={m_bCreated:true,m_bCreateHidden:false,browser_info:sourceLink,Render:()=>events.push('render'),OnLoad:()=>events.push('load')};const w={SteamClient:{Window:{ShowWindow:()=>events.push('show'),BringToFront:()=>events.push('focus')}}};context.Popup.prototype.RenderInternal.call(obj,w,{},-1);assert.deepEqual(events,['render','load','show']);});
check('MagicBlack source is generic composition + DOM, no Windows child popup',()=>{const code=read('magic-BlackBackground.tsx');assert.ok(code.includes('UIComposition.Notification'));assert.ok(code.includes('pointerEvents'));assert.equal(code.includes('target_browser'),false);});
check('MangoPeel uses MangoApp environment/config rather than React HUD pixels',()=>{const code=read('mango-main.py');assert.ok(code.includes('MANGOHUD_CONFIGFILE'));assert.ok(code.includes('overWriteConfig'));assert.ok(code.includes('mangoapp'));});
check('Crosshair delegates glyph overlay to MangoHUD config',()=>{const code=read('Crosshair-main.py');assert.ok(code.includes('background_alpha=0'));assert.ok(code.includes('find_mangohud_config_path'));});
check('PiP separates browser rendering from composition request',()=>{const code=read('decky-pip-src--pip.tsx');for(const k of ['useUIComposition(UIComposition.Notification)','CreateBrowserView("pip")','browser.SetVisible(visible)','browser.SetBounds(x, y, width, height)','view.Destroy()'])assert.ok(code.includes(k));});
check('PiP current upstream has main-window coupling and 250ms bounds polling: not copied',()=>{const code=read('decky-pip-src--pip.tsx');assert.ok(code.includes('GamepadUIMainWindowInstance'));assert.ok(code.includes('}, 250)'));});
check('DimmerDeck is Linux display-LUT/X property, not a Windows HUD carrier',()=>{const code=read('DimmerDeck-main.py');assert.ok(code.includes('xprop'));assert.ok(code.includes('/proc'));assert.ok(code.includes('generate_lut3d'));});
check('actual Steam notifications have a separate render-only BrowserView and portal path',()=>{const code=read('chunk~2dcc5aaf7-module-48197.source.txt');assert.ok(code.includes('GetBrowserView()?.SetVisible(t3)'));assert.ok(code.includes('GetRenderElement()'));assert.ok(code.includes('eCreationFlags: E.Wf.NotFocusable'));});
check('native BrowserView popup resolves owner browser and destroys its own view',()=>{const code=read('chunk~2dcc5aaf7-module-73375.source.txt');assert.ok(code.includes('parentPopupBrowserID: o2'));assert.ok(code.includes('r2?.ownerWindow?.SteamClient.Browser.GetBrowserID()'));assert.ok(code.includes('SteamClient.BrowserView.CreatePopup(u2)'));assert.ok(code.includes('SteamClient.BrowserView.Destroy(this.m_browserView)'));});
check('native notification windows contain an explicit desktop fallback: must not use it as game proof',()=>{const code=read('chunk~2dcc5aaf7-module-48197.source.txt');assert.ok(code.includes('EBrowserType_DirectHWND_Borderless'));assert.ok(code.includes('h2 = m2 ? u2.params.browserInfo : void 0'));});
check('inert factory permits missing target browser, so game-target validation must be external and fail closed',()=>{const p=params();delete p.target_browser;context.Popup.CreatePopup('ymcc-offline-no-game-target',p);assert.equal(new URL(calls.at(-1).url).searchParams.get('pid'),null);});
const files=['magic-uiComposition.tsx','magic-BlackBackground.tsx','mango-main.py','Crosshair-main.py','decky-pip-src--pip.tsx','DimmerDeck-main.py','library-module-11131.source.txt','chunk~2dcc5aaf7-module-83718.source.txt','library-module-63439.source.txt','chunk~2dcc5aaf7-module-51582.source.txt','chunk~2dcc5aaf7-module-48197.source.txt','chunk~2dcc5aaf7-module-73375.source.txt'];
const report={scope:'source+inert-vm-only',pass:true,tests:tests.length,cases:tests,flags,nativeFactoryMockCalls:calls,
  sourceFiles:files.map(file=>({file:path.join(dir,file),sha256:crypto.createHash('sha256').update(fs.readFileSync(path.join(dir,file))).digest('hex')})),
  nativeWindowCreated:false,liveSteamTouched:false,desktopInput:false,desktopCapture:false,gameVisiblePixelsVerified:false,fullscreenVerified:false,inputPassthroughVerified:false,productDeployed:false,
  nextCandidate:'game-bound native CHILD popup or render-only BrowserView; never root DOM alone; child pin/force-visible isolated tests',
  notes:['Source flags request behavior; they do not prove Windows native support or input passthrough.','Exact CreatePopup and RenderInternal executed only with inert VM mocks.','Does not invoke downloaded plugin code, native Steam APIs or game/CDP.']};
fs.writeFileSync(path.join(dir,'OFFLINE-CARRIER-AUDIT.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({tests:tests.length,pass:true,liveSteamTouched:false,report:path.join(dir,'OFFLINE-CARRIER-AUDIT.json')},null,2));
