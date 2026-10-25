import fs from 'node:fs';import path from 'node:path';import {createRequire} from 'node:module';
const out=process.argv[2];if(!out)throw Error('output directory required');fs.mkdirSync(out,{recursive:true});
const require=createRequire('C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/package.json');const {chromium}=require('playwright');
const browser=await chromium.launch({headless:true,executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});const page=await browser.newPage({viewport:{width:1440,height:1100}});page.setDefaultTimeout(5000);
let sourceOverridesUsed=0;const sourceOverride=process.argv[3];if(sourceOverride)await page.route(url=>url.pathname==='/library/app.js',route=>{sourceOverridesUsed++;return route.fulfill({contentType:'text/javascript; charset=utf-8',body:fs.readFileSync(sourceOverride)});});
const cases=[],errors=[];let checks=0;page.on('pageerror',e=>errors.push(e.message));
const check=(ok,label)=>{checks++;if(!ok)throw Error(label);};
async function reset(){await page.goto('http://127.0.0.1:13139/library/');await page.waitForFunction(()=>selectedActionGame?.gameMedia?.key==='steam-570');await page.evaluate(()=>{
 const base=invoke;window.reverseIO={calls:[],finishPick:null};invoke=async(command,args={})=>{
  reverseIO.calls.push({command,args:structuredClone(args)});
  if(command==='pickVideo'||command==='pickArtwork')return new Promise((resolve,reject)=>{reverseIO.finishPick=resolve;reverseIO.failPick=reject;});
  if(command==='mediaEdit'){const entry=__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries[args.key];if(entry&&args.bindingRevision!==entry.bindingRevision)throw Error('media-conflict');}
  if(command==='mediaImport'){const entry=__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries[args.key];if(!entry)throw Error('missing-target');if(args.bindingRevision!==entry.bindingRevision)throw Error('media-conflict');entry.bindingRevision++;return structuredClone(entry);}
  return base(command,args);
 };
 window.reverseChoose=(start,length=30)=>{const skip=document.getElementById('shared-wallpaper-skip'),duration=document.getElementById('shared-wallpaper-length');if(![...skip.options].some(o=>o.value===String(start)))skip.add(new Option(start+' 秒',String(start)));skip.value=String(start);duration.value=String(length);skip.dispatchEvent(new Event('change'));};
 window.reverseOpenOther=()=>{const game=mergedGames().find(g=>g.mediaOnly&&g.mediaKey==='steam-1245620');if(!game)throw Error('second-fixture-game-missing');openEdit(game);};
 });}
async function settle(){await page.waitForFunction(()=>!sharedWallpaperBusy&&(!sharedWallpaperPendingPlayback||sharedWallpaperPendingPlayback.size===0));}
async function run(name,fn){await reset();try{await fn();cases.push({name,ok:true});}catch(error){cases.push({name,ok:false,error:error.message});}}
try{
 await run('closing the editor preserves the latest accepted playback setting',async()=>{
  await page.evaluate(()=>{__LIBRARY_H5_FIXTURE__.latency=60;reverseChoose(15);reverseChoose(5);closeEdit();});await page.waitForFunction(()=>!sharedWallpaperBusy);const state=await page.evaluate(()=>__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries['steam-570'].playback);check(state.start===5&&state.end===35,'latest setting was lost after closing editor');
 });
 await run('two games retain their own queued playback values',async()=>{
  await page.evaluate(()=>{__LIBRARY_H5_FIXTURE__.latency=60;reverseChoose(15);reverseChoose(5);reverseOpenOther();reverseChoose(3);});await settle();const values=await page.evaluate(()=>Object.fromEntries(Object.entries(__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries).map(([key,entry])=>[key,entry.playback])));check(values['steam-570'].start===5&&values['steam-1245620'].start===3,'switching games dropped or redirected a queued setting');
 });
 await run('a file chosen for the first game cannot import into a newly opened game',async()=>{
  await page.evaluate(()=>{window.reversePick=actOnSharedWallpaper('replace');});await page.waitForFunction(()=>reverseIO.finishPick);await page.evaluate(()=>{reverseOpenOther();reverseIO.finishPick({path:'G:/H5Fixture/replacement.mp4'});});await page.evaluate(()=>reversePick);const imports=await page.evaluate(()=>reverseIO.calls.filter(c=>c.command==='mediaImport'));check(imports.length===1&&imports[0].args.key==='steam-570','file import was redirected to the newly selected game');
 });
 await run('cancelling a file picker drains accepted playback changes',async()=>{
  await page.evaluate(()=>{window.reversePick=actOnSharedWallpaper('replace');});await page.waitForFunction(()=>reverseIO.finishPick);await page.evaluate(()=>{reverseChoose(10);reverseIO.finishPick({cancelled:true});});await page.evaluate(()=>reversePick);await page.waitForFunction(()=>__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries['steam-570'].playback.start===10);check(true,'queued playback resumes after cancelled picker');
 });
 await run('successful import drains pending edits for both original and newly selected game',async()=>{
  await page.evaluate(()=>{window.reversePick=actOnSharedWallpaper('replace');});await page.waitForFunction(()=>reverseIO.finishPick);await page.evaluate(()=>{reverseChoose(5);reverseOpenOther();reverseChoose(10);reverseIO.finishPick({path:'G:/H5Fixture/replacement.mp4'});});await page.evaluate(()=>reversePick);await settle();const state=await page.evaluate(()=>({a:__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries['steam-570'],b:__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries['steam-1245620'],imports:reverseIO.calls.filter(c=>c.command==='mediaImport')}));check(state.imports[0].args.key==='steam-570'&&state.a.playback.start===5&&state.b.playback.start===10,'import or queued values crossed game ownership');check(state.a.bindingRevision===3,'queued playback used the revision returned by import');
 });
 await run('failed picker releases the queue and reports its failure',async()=>{
  await page.evaluate(()=>{window.reversePick=actOnSharedWallpaper('replace');});await page.waitForFunction(()=>reverseIO.failPick);await page.evaluate(()=>{reverseChoose(3);reverseIO.failPick(Error('fixture-picker-failed'));});await page.evaluate(()=>reversePick);await settle();check(await page.evaluate(()=>__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries['steam-570'].playback.start===3),'picker failure stranded accepted edits');check(await page.evaluate(()=>[toastNotice.textContent,...noticeQueue.map(item=>item.message)].some(text=>text.includes('fixture-picker-failed'))),'picker failure was not reported');
 });
 await run('external revision conflicts are preserved without blind retries',async()=>{
  await page.evaluate(()=>{const media=__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries['steam-570'];media.bindingRevision++;media.playback={start:20,end:50,fit:'cover'};reverseChoose(5);});await settle();check(await page.evaluate(()=>__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries['steam-570'].playback.start===20),'external edit was overwritten');check(await page.evaluate(()=>reverseIO.calls.filter(c=>c.command==='mediaEdit').length===1&&[toastNotice.textContent,...noticeQueue.map(item=>item.message)].some(text=>text.includes('media-conflict'))),'conflict retried or silently disappeared');
 });
 await run('a late preparation reply cannot roll back a newly saved preference',async()=>{
  await page.evaluate(()=>{const base=invoke;invoke=async(command,args={})=>{if(command!=='mediaCreate')return base(command,args);const old=await base(command,args);return new Promise(resolve=>window.reverseReleasePreparation=()=>resolve(old));};window.reversePreparation=prepareSharedWallpaperPreview(selectedActionGame);});await page.waitForFunction(()=>window.reverseReleasePreparation);await page.evaluate(()=>reverseChoose(5));await settle();await page.evaluate(async()=>{reverseReleasePreparation();await reversePreparation;});check(await page.evaluate(()=>selectedActionGame.gameMedia.playback.start===5&&__LIBRARY_H5_FIXTURE__.snapshot.gameMedia.entries['steam-570'].playback.start===5),'late metadata rolled the displayed preference back after saving');
 });
 check(errors.length===0,'no uncaught browser error');if(sourceOverride)check(sourceOverridesUsed===cases.length,'negative control did not load its exact source for every case');const result={ok:cases.every(c=>c.ok),cases:cases.length,checks,reports:cases,errors,sourceOverride:sourceOverride||null,sourceOverridesUsed,scope:'actual production editor; memory native transport with revision checks; no real library writes or OS picker'};fs.writeFileSync(path.join(out,'editor-reverse-results.json'),JSON.stringify(result,null,2));console.log(JSON.stringify(result));if(!result.ok)process.exitCode=1;
}finally{await browser.close();}
