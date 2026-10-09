import fs from 'node:fs';
import path from 'node:path';
import http from 'node:http';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import assert from 'node:assert/strict';

const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const build=path.resolve(repo,'../../Build/CustomSteamLibrary');
const root=path.resolve(process.argv[2]||path.join(build,'preview-browser-'+Date.now()));
assert(root.startsWith(build+path.sep)&&!fs.existsSync(root),'Use a fresh isolated Build directory');
fs.mkdirSync(root,{recursive:true});
const source=fs.readFileSync(path.join(repo,'CustomSteamLibrary/workspace-ui/app.js'),'utf8');
const begin=source.indexOf('function artworkProviderLabel('),end=source.indexOf('async function searchArtwork(',begin);
assert(begin>=0&&end>begin);
const production=source.slice(begin,end);
const cancel=source.split(/\r?\n/).find(line=>line.startsWith('function cancelArtworkTask('));
assert(cancel);
const edge=process.env.YEMAN_BROWSER_EXECUTABLE||'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe';
assert(fs.existsSync(edge),'Microsoft Edge is required for the isolated real-browser test');
const png=Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+a5R0AAAAASUVORK5CYII=','base64');
const server=http.createServer((req,res)=>{if(req.url?.startsWith('/good')){res.writeHead(200,{'Content-Type':'image/png'});res.end(png);}else{res.writeHead(404,{'Content-Type':'text/plain'});res.end('isolated missing cover');}});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const origin='http://127.0.0.1:'+server.address().port;
const log=fs.openSync(path.join(root,'browser.log'),'w');
const browser=spawn(edge,['--headless=new','--disable-gpu','--no-first-run','--disable-extensions','--disable-background-networking','--remote-debugging-port=0','--remote-debugging-address=127.0.0.1','--user-data-dir='+path.join(root,'profile'),'about:blank'],{windowsHide:true,stdio:['ignore',log,log]});
const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
let socket;let nextId=1;const waiting=new Map();const browserEvents=[];const cases=[];
function command(method,params={}){const id=nextId++;return new Promise((resolve,reject)=>{const timer=setTimeout(()=>{waiting.delete(id);reject(new Error('CDP timeout '+method));},15000);waiting.set(id,{resolve,reject,timer});socket.send(JSON.stringify({id,method,params}));});}
const setup=`
 document.body.innerHTML='<div id="artwork-search-modal"><h2 id="artwork-search-title"></h2><div id="artwork-search-summary"></div><div id="artwork-search-list"></div><div id="artwork-search-empty"></div></div><div id="notice"></div>';
 const $=s=>document.querySelector(s);const libraryNotice=$('#notice');
 let selectedActionGame={primaryExecutable:'G:/Fixture/GameA.exe'};
 let artworkSearchSlot='cover',artworkSearchType='cover',artworkTaskToken=1,artworkTaskInFlight=false,downloadingCandidateUrl='';
 let artworkCandidates=[];const artworkUnavailableUrls=new Set();
 let artworkDraft={cover:{state:'inherit',asset:{url:'old-manual-image'}},long:{state:'inherit'},wallpaper:{state:'inherit'},dirty:{cover:false,long:false,wallpaper:false}};
 const calls=[],notices=[],errors=[],deferred=[];let mode='ok',renders=0;
 function invoke(command,args){calls.push({command,args});if(command!=='downloadArtworkCandidate')return Promise.resolve({});if(mode==='fail')return Promise.reject(new Error('fixture download failed'));if(mode==='defer')return new Promise((resolve,reject)=>deferred.push({resolve,reject}));return Promise.resolve({path:'G:/Fixture/download.png',width:600,height:900});}
 function renderArtworkDraft(){renders++;}function showNotice(message){notices.push(message);}function showError(error){errors.push(String(error));}
 function artworkLabel(slot){return slot;}function artworkSearchQuery(){return 'Fixture Game';}
 function escapeHtml(value){return String(value).replaceAll('&','&amp;').replaceAll('"','&quot;').replaceAll('<','&lt;');}
 function moveArtworkCandidateFocus(){return false;}
 function check(value,why){if(!value)throw new Error(why);}
 const sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms));
 async function until(fn){for(let i=0;i<100;i++){if(fn())return;await sleep(15);}throw new Error('Browser event did not settle');}
 const base=${JSON.stringify(origin)};
 function hit(provider,n,broken=false){return {provider,url:base+(broken?'/missing-':'/good-')+n+'.png',title:'Fixture '+n,width:600,height:900};}
 function downloads(){return calls.filter(c=>c.command==='downloadArtworkCandidate');}
 function open(){return !$('#artwork-search-modal').classList.contains('hidden');}
 function render(items){renderArtworkCandidates({query:'Fixture Game',candidates:items});}
`;
async function test(name,body){try{const result=await command('Runtime.evaluate',{expression:`(async()=>{${setup}\n${production}\n${cancel}\n${body}\nreturn {passed:true};})()`,awaitPromise:true,returnByValue:true});if(result.exceptionDetails)throw new Error(result.exceptionDetails.text+' '+(result.exceptionDetails.exception?.description||''));assert(result.result?.value?.passed===true);cases.push({name,passed:true});}catch(error){cases.push({name,passed:false,error:String(error.message||error)});}}
try{
 let port;for(let i=0;i<120;i++){const p=path.join(root,'profile/DevToolsActivePort');if(fs.existsSync(p)){port=Number(fs.readFileSync(p,'utf8').split(/\r?\n/)[0]);break;}if(browser.exitCode!==null)throw new Error('Isolated browser exited during startup');await pause(100);}assert(port,'DevTools did not start');
 const targets=await (await fetch('http://127.0.0.1:'+port+'/json/list')).json();const page=targets.find(t=>t.type==='page');assert(page);
 socket=new WebSocket(page.webSocketDebuggerUrl);await new Promise((resolve,reject)=>{socket.addEventListener('open',resolve,{once:true});socket.addEventListener('error',reject,{once:true});});
 socket.addEventListener('message',event=>{const message=JSON.parse(event.data);if(message.id){const p=waiting.get(message.id);if(!p)return;waiting.delete(message.id);clearTimeout(p.timer);if(message.error)p.reject(new Error(JSON.stringify(message.error)));else p.resolve(message.result);}else if(message.method?.includes('crash'))browserEvents.push(message);});
 await command('Page.enable');await command('Runtime.enable');
 await test('four-steam-404-previews-never-download-apply-or-dismiss',`const steam=[0,1,2,3].map(n=>hit('steam-store',n,true));const igdb=hit('playnite-igdb','igdb');render([...steam,igdb]);await until(()=>artworkUnavailableUrls.size===4);await sleep(80);check(open(),'Preview failure dismissed gallery');check(downloads().length===0,'Preview error performed an unsolicited native download');check(!artworkDraft.dirty.cover&&artworkDraft.cover.asset.url==='old-manual-image','Preview failure replaced existing/manual image');check(artworkCandidateCard(igdb)===document.activeElement,'IGDB fallback did not receive safe focus');`);
 await test('igdb-selection-applies-only-after-explicit-click',`const steam=hit('steam-store',1,true),igdb=hit('playnite-igdb','igdb');render([steam,igdb]);await until(()=>artworkUnavailableUrls.size===1);check(open()&&downloads().length===0,'Preview error auto-applied fallback');artworkCandidateCard(igdb).click();await until(()=>!open());check(downloads().length===1&&artworkDraft.cover.asset.provider==='playnite-igdb','User selection not applied exactly once');check(!artworkDraft.dirty.long&&!artworkDraft.dirty.wallpaper,'Cover selection changed another slot');`);
 await test('all-providers-preview-failure-keeps-gallery-and-old-image',`render([hit('steam-store',1,true),hit('playnite-igdb',2,true),hit('baidu-image',3,true)]);await until(()=>artworkUnavailableUrls.size===3);check(open()&&downloads().length===0&&!artworkDraft.dirty.cover,'All providers failing closed or changed editor');`);
 await test('thumbnail-failure-does-not-disable-valid-original-download',`const c=hit('steam-store','original');c.previewUrl=base+'/missing-thumb.png';render([c]);await sleep(100);check(!artworkCandidateCard(c).disabled,'Bad thumbnail disabled valid original');artworkCandidateCard(c).click();await until(()=>!open());check(downloads().length===1&&downloads()[0].args.url===c.url,'Original URL was not used');`);
 await test('explicit-download-failure-focuses-backup-without-auto-download',`mode='fail';const steam=hit('steam-store','valid'),igdb=hit('playnite-igdb','igdb');render([steam,igdb]);artworkCandidateCard(steam).click();await until(()=>!artworkTaskInFlight);check(open()&&downloads().length===1&&!artworkDraft.dirty.cover,'Download failure silently chose another image/closed gallery');check(document.activeElement===artworkCandidateCard(igdb),'Failed download did not expose fallback selection');`);
 await test('cancelled-old-download-cannot-clear-new-download-guard',`mode='defer';const a=hit('playnite-igdb','a');render([a]);artworkCandidateCard(a).click();await until(()=>deferred.length===1);cancelArtworkTask();selectedActionGame={primaryExecutable:'G:/Fixture/GameB.exe'};$('#artwork-search-modal').classList.remove('hidden');const b=hit('playnite-igdb','b');render([b]);artworkCandidateCard(b).click();await until(()=>deferred.length===2);deferred[0].resolve({path:'G:/Fixture/old.png'});await sleep(50);check(open()&&downloadingCandidateUrl===b.url&&artworkTaskInFlight,'Old completion cleared newer request guard');check(!artworkDraft.dirty.cover,'Cancelled old game image applied');deferred[1].resolve({path:'G:/Fixture/new.png'});await until(()=>!open());check(artworkDraft.cover.asset.path==='G:/Fixture/new.png','Wrong game result applied');`);
 await test('changing-game-or-slot-during-download-rejects-old-result',`mode='defer';const a=hit('playnite-igdb','a');render([a]);artworkCandidateCard(a).click();await until(()=>deferred.length===1);selectedActionGame={primaryExecutable:'G:/Fixture/GameB.exe'};artworkSearchSlot='long';artworkSearchType='long';deferred[0].resolve({path:'G:/Fixture/old.png'});await sleep(60);check(open()&&!artworkDraft.dirty.cover&&!artworkDraft.dirty.long,'Stale completion applied to a new game/slot');`);
 await test('detached-old-image-error-cannot-poison-new-candidates',`const a=hit('steam-store','shared');render([a]);const oldImage=artworkCandidateCard(a).querySelector('img');artworkTaskToken++;selectedActionGame={primaryExecutable:'G:/Fixture/GameB.exe'};const b={...a};render([b]);oldImage.dispatchEvent(new Event('error'));await sleep(60);check(open()&&artworkUnavailableUrls.size===0&&!artworkCandidateCard(b).disabled&&downloads().length===0,'Detached old callback affected new gallery');`);
 await test('display-dimensions-use-decoded-image-not-guessed-2x-size',`const c=hit('steam-store','dimensions');c.width=1200;c.height=1800;render([c]);await until(()=>artworkCandidateCard(c).querySelector('img').naturalWidth>0);await sleep(20);check(artworkCandidateCard(c).querySelector('.artwork-candidate-meta').children[1].textContent==='1×1','Guessed API dimensions were displayed instead of decoded pixels');`);
 await test('repeated-cover-long-wallpaper-preview-failures-stay-alive',`for(let i=0;i<12;i++)for(const slot of ['cover','long','wallpaper']){artworkTaskToken++;artworkSearchSlot=slot;artworkSearchType=slot;render([hit('steam-store','s'+i+slot,true),hit('playnite-igdb','i'+i+slot)]);await until(()=>artworkUnavailableUrls.size===1);check(open()&&downloads().length===0,'Repeated preview failure exited/applied gallery');}check(Object.values(artworkDraft.dirty).every(v=>!v),'Repeated failures changed draft');`);
 if(process.env.YEMAN_STEAM_ARTWORK_LIVE_REPORT){
  const reportPath=path.resolve(process.env.YEMAN_STEAM_ARTWORK_LIVE_REPORT);assert(reportPath.startsWith(build+path.sep));
  const gallery=JSON.parse(fs.readFileSync(reportPath,'utf8').replace(/^\uFEFF/,''));const steam=gallery.candidates.filter(c=>String(c.provider).startsWith('steam'));
  assert(steam.length===2&&steam.every(c=>c.url.startsWith('https://shared.steamstatic.com/store_item_assets/steam/apps/4012810/')));
  await test('live-steam-cover-jpegs-load-in-browser-without-format-error',`const steam=${JSON.stringify(steam)};render(steam);await until(()=>[...document.querySelectorAll('#artwork-search-list img')].every(img=>img.complete&&img.naturalWidth>0));check(open()&&artworkUnavailableUrls.size===0&&downloads().length===0,'Official JPEG cover preview failed or auto-applied');`);
 }
 assert(browserEvents.length===0,'Browser process crash event observed');
 const report={allPassed:cases.every(c=>c.passed),caseCount:cases.length,cases,realBrowserUsed:true,network:process.env.YEMAN_STEAM_ARTWORK_LIVE_REPORT?'localhost-fixtures-and-live-steam-covers':'localhost-fixture-only',realSteamFilesModified:false,realGamesLaunched:false};fs.writeFileSync(path.join(root,'summary.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));console.log('REPORT='+path.join(root,'summary.json'));if(!report.allPassed)process.exitCode=1;
}finally{socket?.close();for(const p of waiting.values()){clearTimeout(p.timer);p.reject(new Error('Browser test closing'));}server.close();browser.kill();await pause(150);fs.closeSync(log);}
