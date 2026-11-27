import fs from'node:fs';import path from'node:path';import{createRequire}from'node:module';
const out=process.argv[2];if(!out)throw Error('validation directory required');const root=path.resolve(out,'..');
const selection=process.argv[4];if(selection&&(!process.argv[3]||selection!=='--playback-popup'))throw Error('unsupported editor scenario selection');
const require=createRequire('C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/package.json');const{chromium}=require('playwright');const browser=await chromium.launch({headless:true,executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});const page=await browser.newPage({hasTouch:selection==='--playback-popup'});page.setDefaultTimeout(12000);const errors=[],results=[];page.on('pageerror',e=>errors.push(e.message));let checks=0;const check=(ok,label)=>{checks++;if(!ok)throw Error(label);};
// Optional explicit source input makes this existing consumer self-contained.
// With no override, retain the original local preview-server contract.
if(process.argv[3]){
 const ui=path.resolve(path.dirname(process.argv[3])),samples='G:/YeManCC-Work/Build/Tasks/YMCC-Decky-SharedWallpaper-20261017/preview';
 const oldPlayback="if(args.action==='playback')media.playback={start:args.start,end:args.end,fit:args.fit};";
 const fixtureSource=fs.readFileSync(path.join(samples,'library-fixture.js'),'utf8');
 if(!fixtureSource.includes(oldPlayback))throw Error('preview fixture playback contract changed');
 const fixture=fixtureSource.replace(oldPlayback,"if(args.action==='playback'){media.playback={start:args.start,end:args.end,fit:args.fit};if(args.startMode==='from-start')media.playback.startMode=args.startMode;}")
   .replace("const command=message.command,args=message.arguments||{};let result;", "const command=message.command,args=message.arguments||{};let result;if(command==='mediaEdit'&&args.action==='playback'&&window.__LIBRARY_H5_FIXTURE__.rejectPlayback){queueMicrotask(()=>{for(const listener of listeners)listener({data:{kind:'response',id:message.id,ok:false,error:'fixture rejected playback write'}});});return;}");
 await page.route('http://127.0.0.1:13139/**',route=>{
  const name=path.basename(new URL(route.request().url()).pathname)||'index.html';
  if(['index.html','library','app.js','styles.css'].includes(name)){
   const file=name==='library'?'index.html':name;let body=fs.readFileSync(path.join(ui,file));
   if(file==='index.html')body=Buffer.from(body.toString('utf8').replace('</head>','<script>'+fixture+'</script></head>'));
   return route.fulfill({contentType:file.endsWith('.js')?'text/javascript':file.endsWith('.css')?'text/css':'text/html; charset=utf-8',body});
  }
  if(['cover.jpg','header.jpg','wallpaper.jpg','sample.mp4'].includes(name)){
   const bytes=fs.readFileSync(path.join(samples,name)),range=route.request().headers()['range'];
   if(name==='sample.mp4'&&range){const m=/^bytes=(\d+)-(\d*)$/.exec(range);if(m){const start=Number(m[1]),end=Math.min(bytes.length-1,m[2]?Number(m[2]):bytes.length-1);
    if(start<=end&&start<bytes.length)return route.fulfill({status:206,contentType:'video/mp4',headers:{'Accept-Ranges':'bytes','Content-Range':`bytes ${start}-${end}/${bytes.length}`},body:bytes.subarray(start,end+1)});
    return route.fulfill({status:416,headers:{'Content-Range':`bytes */${bytes.length}`},body:''});}}
   return route.fulfill({contentType:name.endsWith('.mp4')?'video/mp4':'image/jpeg',body:bytes});
  }
  return route.fulfill({status:404,body:''});
 });
}
fs.mkdirSync(out,{recursive:true});fs.mkdirSync(path.join(root,'preview'),{recursive:true});
try{
 if(selection==='--playback-popup'){
  await page.setViewportSize({width:1280,height:720});
  await page.goto('http://127.0.0.1:13139/library/');
  await page.evaluate(()=>{document.hasFocus=()=>true;window.dispatchEvent(new Event('focus'));});
  const launch=page.locator('#shared-wallpaper-playback-open'),popup=page.locator('#shared-wallpaper-playback-modal');
  const trigger=name=>popup.locator(`[data-select-id="shared-wallpaper-${name}"] .workspace-dropdown-trigger`);
  const visibleList=page.locator('.workspace-dropdown-menu.is-open');
  const focused=loc=>loc.evaluate(node=>document.activeElement===node);
  const pad=action=>page.evaluate(action=>window.dispatchEvent(new CustomEvent('workspace-action',{detail:action})),action);
  const fields=()=>page.evaluate(()=>structuredClone(__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries['steam-570'].playback));
  const idle=()=>page.waitForFunction(()=>!sharedWallpaperBusy&&!sharedWallpaperActivePlayback&&!sharedWallpaperPendingPlayback.size);
  const choose=async(name,label)=>{await trigger(name).click();await visibleList.getByRole('option',{name:label,exact:true}).click();};
  await page.waitForFunction(()=>document.querySelector('#artwork-wallpaper-preview video')?.currentTime>8.25);
  check(!await popup.isVisible(),'no playback form before explicit open');
  await page.locator('#artwork-wallpaper-preview').focus();await pad('navigate-down');await pad('navigate-right');
  await page.waitForTimeout(180);await pad('navigate-right');check(await focused(launch),'controller navigation reaches playback from the wallpaper');
  await pad('accept');check(await popup.isVisible()&&await focused(trigger('start-mode')),'A opens the real modal and focuses playback form');
  check((await trigger('start-mode').innerText()).includes('随机播放'),'legacy row opens as random');
  await pad('accept');await visibleList.getByRole('option',{name:'随机播放'}).waitFor();
  await page.waitForFunction(()=>document.activeElement?.matches('.workspace-dropdown-option[aria-selected=true]'));
  await pad('back');check(!await visibleList.isVisible()&&await popup.isVisible()&&await focused(trigger('start-mode')),'first B closes only the list and restores its trigger');
  await pad('accept');await page.waitForFunction(()=>document.activeElement?.matches('.workspace-dropdown-option[aria-selected=true]'));
  await pad('navigate-down');await pad('accept');
  await idle();
  check((await fields()).startMode==='from-start'&&(await fields()).start===8,'from-start persists without overwriting the random intro');
  check(await trigger('skip').isDisabled(),'from-start visibly disables the ignored intro setting');
  const current=await page.locator('#artwork-wallpaper-preview video').evaluate(v=>v.currentTime);
  check(current<3,'the editing preview really sought to the start');
  await page.screenshot({path:root+'/preview/editor-playback-popup-1280x720.png'});
  await page.waitForTimeout(120);await pad('navigate-down');check(await focused(trigger('length')),'controller skips the disabled intro setting');
  await pad('back');check(!await popup.isVisible()&&await focused(launch)&&await page.locator('#edit-modal').isVisible(),'second-level B returns to the wallpaper button');
  await launch.click();await page.evaluate(()=>{__LIBRARY_H5_FIXTURE__.latency=120;});
  await choose('length','60 秒');await popup.getByRole('button',{name:'返回',exact:true}).tap();
  await idle();check(!await popup.isVisible()&&(await fields()).end===68,'touch back closes the popup while its accepted write continues');
  await page.evaluate(()=>{const game=mergedGames().find(g=>!g.mediaOnly);closeEdit();openEdit(game);});
  await launch.click();check((await trigger('start-mode').innerText()).includes('从头播放')&&(await trigger('length').innerText()).includes('60 秒'),'saved form and duration load again when the game is reopened');
  await page.evaluate(()=>{__LIBRARY_H5_FIXTURE__.rejectPlayback=true;});
  await choose('start-mode','随机播放');await idle();
  check((await fields()).startMode==='from-start'&&(await trigger('start-mode').innerText()).includes('从头播放')&&await popup.getByRole('status').isVisible(),'rejected save restores the persisted selection and reports failure in the modal');
  await page.evaluate(()=>{__LIBRARY_H5_FIXTURE__.rejectPlayback=false;});
  await choose('start-mode','随机播放');await idle();
  const random=await fields();check(!('startMode' in random)&&random.start===8&&random.end===68&&random.fit==='cover'&&!await trigger('skip').isDisabled(),'explicit random restores the old trim and fit with legacy-compatible storage');
  check(errors.length===0,'no unexpected browser exception');
  fs.writeFileSync(out+'/editor-playback-popup-results.json',JSON.stringify({ok:true,selector:selection,checks,errors,scope:'Production editor UI and real video in isolated Edge; semantic controller events and fixture native persistence.'},null,2));
  console.log('EDITOR_PLAYBACK_POPUP_OK '+checks);
 }else{
 for(const viewport of [{width:1440,height:1100},{width:1920,height:1080},{width:1280,height:800},{width:1280,height:720},{width:1024,height:640}]){
  await page.setViewportSize(viewport);await page.goto('http://127.0.0.1:13139/library/');if(process.argv[3])await page.evaluate(()=>{document.hasFocus=()=>true;window.dispatchEvent(new Event('focus'));});await page.locator('#edit-modal:not(.hidden)').waitFor();await page.waitForFunction(()=>document.querySelector('#artwork-cover-preview img')?.naturalWidth>0&&document.querySelector('#artwork-wallpaper-preview video')?.currentTime>8.25);
  const layout=await page.evaluate(()=>{const box=id=>{const r=document.querySelector(id).getBoundingClientRect();return {x:r.x,y:r.y,width:r.width,height:r.height,right:r.right,bottom:r.bottom};};return {headerControls:[...document.querySelectorAll('.shared-wallpaper-modes .button:not(.hidden)')].map(n=>{const r=n.getBoundingClientRect();return {label:n.textContent.trim(),top:r.top,bottom:r.bottom,height:r.height,left:r.left,right:r.right};}),cover:box('#artwork-cover-preview'),long:box('#artwork-long-preview'),wallpaper:box('#artwork-wallpaper-preview'),settings:box('.shared-wallpaper-settings'),wallpaperFrame:box('.artwork-pane[data-artwork-type=wallpaper]'),staticButton:box('[data-shared-wallpaper-mode=static]'),extraArtworkFrames:document.querySelectorAll('.artwork-preview-frame').length,artworkBorders:[...document.querySelectorAll('.edit-artwork-panel,.edit-artwork-panel .artwork-pane,.edit-artwork-panel .artwork-dropzone')].map(n=>getComputedStyle(n).borderTopWidth),imageFit:getComputedStyle(document.querySelector('#artwork-wallpaper-preview video')).objectFit,artworkPanel:box('.edit-artwork-panel'),coverPane:box('.artwork-pane[data-artwork-type=cover]'),longPane:box('.artwork-pane[data-artwork-type=long]'),wallpaperPane:box('.artwork-pane[data-artwork-type=wallpaper]'),artworkTitles:document.querySelectorAll('.edit-artwork-panel .artwork-pane h3').length,dialog:box('#edit-modal .edit-modal-card'),footer:box('#edit-modal .edit-actions'),title:document.querySelector('#edit-modal').innerText,scroll:document.querySelector('#edit-modal .edit-modal-card').scrollHeight,client:document.querySelector('#edit-modal .edit-modal-card').clientHeight};});
  check(Math.abs(layout.cover.width/layout.cover.height-2/3)<.01,'portrait retains2:3');check(Math.abs(layout.wallpaper.width-(layout.wallpaperFrame.width-20*layout.headerControls[0].height/42))<1&&Math.abs(layout.wallpaper.height-(layout.wallpaperFrame.height-20*layout.headerControls[0].height/42))<1,'wallpaper fits inside its single padded bubble');check(layout.imageFit==='cover','video preserves source proportions while filling');check(layout.settings.y>=layout.wallpaper.y&&layout.settings.bottom<=layout.wallpaper.bottom&&layout.settings.x>=layout.wallpaper.x&&layout.settings.right<=layout.wallpaper.right,'wallpaper toolbar sits inside its image');check(layout.artworkTitles===0,'three artwork headings removed');check(layout.extraArtworkFrames===0&&parseFloat(layout.artworkBorders[0])>0&&layout.artworkBorders.slice(1).every(w=>parseFloat(w)===0),'one outer border with three background bubbles and no inner image frames');check(Math.abs(layout.coverPane.y-layout.wallpaperPane.y)<.7&&Math.abs(layout.longPane.bottom-layout.wallpaperPane.bottom)<.7,'left artwork bubbles align with wallpaper at top and bottom');check(Math.abs(layout.cover.y-layout.wallpaper.y)<.7&&Math.abs(layout.long.bottom-layout.wallpaper.bottom)<.7,'actual cover and landscape image edges align with wallpaper');check([layout.coverPane,layout.longPane,layout.wallpaperPane].every(r=>r.x>=layout.artworkPanel.x&&r.y>=layout.artworkPanel.y&&r.right<=layout.artworkPanel.right&&r.bottom<=layout.artworkPanel.bottom),'all three bubbles stay inside the outer frame');check(layout.headerControls.length===3&&layout.headerControls.every(r=>r.right<=layout.settings.right&&r.left>=layout.settings.x),'three toolbar controls fit');check(layout.dialog.x>=0&&layout.dialog.right<=viewport.width+1&&layout.dialog.y>=0&&layout.footer.bottom<=viewport.height+1,'dialog and footer inside viewport');check(!layout.title.includes('游戏视觉')&&!layout.title.includes('指定封面、横版与壁纸'),'removed descriptive heading');check(layout.scroll<=layout.client+2,'no internal clipping or scrollbar');results.push({viewport,layout});
  await page.screenshot({path:root+'/preview/editor-'+viewport.width+'x'+viewport.height+'.png'});
 }
 await page.setViewportSize({width:1440,height:1100});await page.goto('http://127.0.0.1:13139/library/');if(process.argv[3])await page.evaluate(()=>{document.hasFocus=()=>true;window.dispatchEvent(new Event('focus'));});await page.locator('#edit-modal:not(.hidden)').waitFor();await page.locator('#shared-wallpaper-playback-open').click();await page.locator('[data-select-id="shared-wallpaper-length"] .workspace-dropdown-trigger').click();await page.getByRole('option',{name:'60 秒',exact:true}).click();await page.waitForFunction(()=>__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries['steam-570'].playback.end===68);
 const remaining=await page.evaluate(()=>{const v=document.querySelector('#artwork-wallpaper-preview video'),p=__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries['steam-570'].playback;return Math.max(1,Math.round(Math.min(v.duration,p.end)-p.start));});check(await page.locator('#shared-wallpaper-duration-hint').textContent()==='本片播放约 '+remaining+' 秒','duration label uses actual remaining clip metadata');
 await page.evaluate(()=>{__LIBRARY_H5_FIXTURE__.latency=80;const skip=document.getElementById('shared-wallpaper-skip'),length=document.getElementById('shared-wallpaper-length');skip.value='15';skip.dispatchEvent(new Event('change'));skip.value='5';skip.dispatchEvent(new Event('change'));length.value='30';length.dispatchEvent(new Event('change'));});
 await page.waitForFunction(()=>{const p=__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries['steam-570'].playback;return p.start===5&&p.end===35;});check(true,'rapid changes save latest combined selection');
 await page.waitForFunction(()=>!sharedWallpaperBusy&&sharedWallpaperPendingPlayback.size===0);await page.locator('#shared-wallpaper-playback-back').click();await page.locator('[data-shared-wallpaper-mode="static"]').click();await page.waitForFunction(()=>document.querySelector('#artwork-wallpaper-preview img'));check(!await page.locator('#shared-wallpaper-playback-open').isVisible(),'static mode hides video controls');check(await page.locator('.shared-wallpaper-modes').isVisible(),'type selection remains available');await page.screenshot({path:root+'/preview/editor-static-1440x1100.png'});
 check(await page.locator('[data-shared-wallpaper-mode=static]').textContent()==='静态壁纸'&&await page.locator('[data-shared-wallpaper-mode=dynamic]').textContent()==='动态壁纸','wallpaper buttons use complete names');
 await page.evaluate(async()=>{const media=__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries['steam-570'];media.wallpaperMode='static';media.asset=media.assets.static;media.previewUrl=media.staticPreviewUrl;document.querySelector('#edit-modal').classList.add('hidden');snapshot=await invoke('mediaState');editDraft=null;openEdit(mergedGames().find(game=>!game.mediaOnly));});
 await page.waitForFunction(()=>document.querySelector('#artwork-wallpaper-preview img')?.naturalWidth>0);check(await page.locator('[data-shared-wallpaper-mode=static]').getAttribute('aria-pressed')==='true','opening editor honors the saved static choice with cached video present');check(await page.locator('#artwork-wallpaper-preview video').count()===0,'saved static mode releases the video preview');check(await page.evaluate(()=>__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries['steam-570'].wallpaperMode==='static'),'automatic preview never changes saved wallpaper choice');
 await page.evaluate(async()=>{const media=__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries['steam-570'];delete media.assets.dynamic;media.dynamicPreviewUrl='';media.dynamicReady=false;document.querySelector('#edit-modal').classList.add('hidden');snapshot=await invoke('mediaState');editDraft=null;openEdit(mergedGames().find(game=>!game.mediaOnly));});
 await page.waitForFunction(()=>document.querySelector('#artwork-wallpaper-preview img')?.naturalWidth>0);check(await page.locator('[data-shared-wallpaper-mode=static]').getAttribute('aria-pressed')==='true','saved static choice remains usable when cached video is removed');check(await page.locator('#artwork-wallpaper-preview video').count()===0,'static fallback releases the old video');
 check(errors.length===0,'no page errors');fs.writeFileSync(out+'/editor-layout-browser-results.json',JSON.stringify({ok:true,checks,results,errors,scope:'actual production library HTML CSS and JS; fixture native transport, real official video and static art'},null,2));console.log('EDITOR_LAYOUT_BROWSER_OK '+checks);
 }
}catch(error){fs.writeFileSync(path.join(out,'failure-state.json'),JSON.stringify(await page.evaluate(()=>{const v=document.querySelector('#artwork-wallpaper-preview video');return {focused:document.hasFocus(),visibility:document.visibilityState,renderAllowed:libraryRenderAllowed(),video:v?{time:v.currentTime,paused:v.paused,readyState:v.readyState,duration:v.duration,error:v.error?.code,source:v.currentSrc}:null};}),null,2));await page.screenshot({path:root+'/preview/editor-layout-failure.png'});throw error;}finally{await browser.close();}
