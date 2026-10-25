import fs from 'node:fs';
import path from 'node:path';
import http from 'node:http';
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';

// Exercise the complete shipping HTML/JS in an isolated Edge profile. All
// native bridge replies are local fixtures; no real Steam/game data is used.
const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const build = path.resolve(repo, '../../Build/CustomSteamLibrary');
const root = path.resolve(process.argv[2] || path.join(build, 'controller-input-' + Date.now()));
assert(root.startsWith(build + path.sep) && !fs.existsSync(root), 'Use a fresh isolated Build directory');
fs.mkdirSync(root, { recursive: true });
const ui = process.env.YEMAN_LIBRARY_UI_DIRECTORY ? path.resolve(process.env.YEMAN_LIBRARY_UI_DIRECTORY) : path.join(repo, 'CustomSteamLibrary/workspace-ui');
assert(ui === path.join(repo, 'CustomSteamLibrary/workspace-ui') || ui.startsWith(build + path.sep), 'UI override must be an isolated Build package');
const edge = process.env.YEMAN_BROWSER_EXECUTABLE || 'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe';
assert(fs.existsSync(edge), 'Microsoft Edge is required');
const identityCorpusPath = process.env.YMCC_LOCAL_IDENTITY_UI_SNAPSHOT;
const identityCorpus = identityCorpusPath ? JSON.parse(fs.readFileSync(identityCorpusPath, 'utf8').replace(/^\uFEFF/, '')) : null;
if(identityCorpusPath) assert(path.resolve(identityCorpusPath).startsWith(build + path.sep), 'Actual identity snapshot must stay in Build');
const mock = `
  window.fixtureCalls = [];
  window.fixtureGame = { gameDirectory:'G:/Fixture/Games/Portal', directoryName:'Portal',
    primaryExecutable:'G:/Fixture/Games/Portal/Game.exe', primaryProductName:'Portal', status:'ready',
    candidates:[], steam:{appId:620, suggestedName:'Portal', status:'waiting-steam-verification'} };
  window.fixtureGame.gameDirectory = window.fixtureGame.gameDirectory.replaceAll('/',String.fromCharCode(92));
  window.fixtureGame.primaryExecutable = window.fixtureGame.primaryExecutable.replaceAll('/',String.fromCharCode(92));
  window.fixtureSnapshot = {config:{roots:['G:/Fixture/Games'],automaticScrapingEnabled:false},
    library:{games:[window.fixtureGame]},manualOverrides:{items:{}},
    steamPlan:{items:[],summary:{}},steamAccounts:{accounts:[],steamStatus:'installed-no-account'}};
  window.fixtureSteamAccounts={accounts:[{accountId:'1000',steamRoot:'G:/FixtureSteam'}],steamStatus:'ready'};
  const listeners = [];
  window.chrome ||= {};
  window.chrome.webview = {
    addEventListener(type, listener) { if(type==='message')listeners.push(listener); },
    postMessage(request) {
      window.fixtureCalls.push(structuredClone(request));
      if(request.id===0)return;
      let result = structuredClone(window.fixtureSnapshot);
      if(request.command==='removeRoot') {
        window.fixtureSnapshot.config.roots = window.fixtureSnapshot.config.roots.filter(root=>root!==request.arguments.path);
        result = structuredClone(window.fixtureSnapshot);
      }
      if(request.command==='steamAccounts')result=structuredClone(window.fixtureSteamAccounts);
      if(request.command==='searchArtwork')result={candidates:[]};
      if(request.command==='steamState')result={running:false,steamPath:''};
      setTimeout(()=>listeners.forEach(listener=>listener({data:{kind:'response',id:request.id,ok:true,result}})),0);
    }
  };
`;
const server = http.createServer((req, res) => {
  const name = new URL(req.url, 'http://localhost').pathname;
  const file = name === '/' ? 'index.html' : name.slice(1);
  if (!['index.html','app.js','styles.css'].includes(file)) { res.writeHead(404); res.end(); return; }
  let text = fs.readFileSync(path.join(ui, file), 'utf8');
  if (file === 'index.html') text = text.replace('<head>', '<head><script>' + mock + (identityCorpus ? '\nwindow.actualLocalIdentitySnapshot=' + JSON.stringify(identityCorpus).replaceAll('<','\\u003c') + ';' : '') + '</script>');
  res.writeHead(200, {'Content-Type': file.endsWith('.js') ? 'text/javascript' : file.endsWith('.css') ? 'text/css' : 'text/html'});
  res.end(text);
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const origin = 'http://127.0.0.1:' + server.address().port;
const log = fs.openSync(path.join(root, 'browser.log'), 'w');
const browser = spawn(edge, ['--headless=new','--disable-gpu','--no-first-run','--disable-extensions',
  '--disable-background-networking','--disable-background-timer-throttling','--disable-renderer-backgrounding','--remote-debugging-port=0','--remote-debugging-address=127.0.0.1',
  '--window-size=1280,900','--user-data-dir=' + path.join(root,'profile'),'about:blank'],
  {windowsHide:true,stdio:['ignore',log,log]});
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
const waiting = new Map(), cases = [];
let socket, nextId = 1;
function command(method, params = {}) {
  const id = nextId++;
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => {waiting.delete(id);reject(new Error('CDP timeout ' + method));},15000);
    waiting.set(id,{resolve,reject,timer}); socket.send(JSON.stringify({id,method,params}));
  });
}
async function evaluate(expression) {
  const result = await command('Runtime.evaluate',{expression,awaitPromise:true,returnByValue:true});
  if(result.exceptionDetails)throw new Error(result.exceptionDetails.exception?.description || result.exceptionDetails.text);
  return result.result?.value;
}
async function test(name, body) {
  try { await evaluate(`(async()=>{${body};return true;})()`);cases.push({name,passed:true}); }
  catch(error){cases.push({name,passed:false,error:error.message});}
}
try {
  let port;
  for(let i=0;i<120;i++) {
    const activePort=path.join(root,'profile/DevToolsActivePort');
    if(fs.existsSync(activePort)){port=Number(fs.readFileSync(activePort,'utf8').split(/\r?\n/)[0]);break;}
    if(browser.exitCode!==null)throw new Error('Isolated browser exited during startup');
    await sleep(100);
  }
  assert(port,'DevTools did not start');
  const targets=await(await fetch('http://127.0.0.1:'+port+'/json/list')).json();
  const page=targets.find(target=>target.type==='page');assert(page);
  socket=new WebSocket(page.webSocketDebuggerUrl);
  await new Promise((resolve,reject)=>{socket.addEventListener('open',resolve,{once:true});socket.addEventListener('error',reject,{once:true});});
  socket.addEventListener('message',event=>{
    const message=JSON.parse(event.data),pending=waiting.get(message.id);if(!pending)return;
    waiting.delete(message.id);clearTimeout(pending.timer);
    if(message.error)pending.reject(new Error(JSON.stringify(message.error)));else pending.resolve(message.result);
  });
  await command('Page.enable');await command('Runtime.enable');
  await command('Page.navigate',{url:origin});
  for(let i=0;i<120;i++) {
    if(await evaluate(`Boolean(document.querySelector('#library-view:not(.hidden)') && typeof openEdit==='function')`))break;
    await sleep(50);
  }
  await evaluate(`
    window.check=(value,text)=>{if(!value)throw new Error(text);};
    window.frames=()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)));
    window.keyboardCalls=()=>fixtureCalls.filter(call=>call.command==='keyboard');
    window.editorOpen=()=>!document.querySelector('#edit-modal').classList.contains('hidden');
    window.resetEditor=async(native=false)=>{
      if(editorOpen())closeEdit();controllerBackLastAt=0;
      const game=structuredClone(fixtureGame);if(native)game.steamNative=true;
      openEdit(game);await frames();fixtureCalls.length=0;
    };
  `);
  for(let i=0;i<160;i++) {
    if(await evaluate(`Boolean(snapshot?.config?.roots?.length && mergedGames().length && pending.size===0)`))break;
    await sleep(50);
  }
  await test('X-opens-editor-without-editable-focus-or-keyboard', `
    activeTab=category(mergedGames()[0]);renderLibrary();const card=libraryCards()[0];check(card,'Missing game card');
    focusLibraryCard(card);fixtureCalls.length=0;routeWorkspaceAction('edit');await frames();
    check(editorOpen(),'X did not open editor');
    check(!isFormInputTarget(document.activeElement),'X focused editable input');
    check($('#identity-name').readOnly && $('#identity-steam-id').readOnly,'X entered text mode');
    check(keyboardCalls().length===0,'X requested software keyboard');`);
  await test('direction-keeps-text-fields-locked-without-keyboard', `
    fixtureCalls.length=0;for(let i=0;i<4;i++){focusWorkspaceElement($('#identity-name'));lastDirectionalActionAt=0;routeWorkspaceAction('navigate-down');}
    check($('#identity-name').readOnly && $('#identity-steam-id').readOnly,'Direction entered text mode');
    check(keyboardCalls().length===0,'Direction requested keyboard');`);
  await test('explicit-keyboard-action-can-request-keyboard', `
    await resetEditor();focusWorkspaceElement($('#identity-name'));check(requestGamepadKeyboard($('#identity-name')),'Explicit keyboard action failed');
    check(keyboardCalls().length===1 && !$('#identity-name').readOnly,'Explicit keyboard action did not target the name');`);
  await test('mouse-doubleclick-uses-the-same-safe-editor-entry', `
    closeEdit();activeTab=category(mergedGames()[0]);renderLibrary();fixtureCalls.length=0;
    const card=libraryCards()[0];card.dispatchEvent(new MouseEvent('dblclick',{bubbles:true}));await frames();
    check(editorOpen() && keyboardCalls().length===0,'Mouse open differed from X');`);
  await test('cancel-last-root-removal-preserves-scan-scope', `
    closeEdit();renderEntry();fixtureCalls.length=0;const button=document.querySelector('.root-row button');
    check(button && !button.disabled,'Last root disabled');button.click();$('#confirm-cancel').click();await frames();
    check(snapshot.config.roots.length===1 && !fixtureCalls.some(c=>c.command==='removeRoot'),'Cancel removed root');`);
  await test('confirmed-last-root-removal-submits-and-renders-empty', `
    renderEntry();const expected=snapshot.config.roots[0];fixtureCalls.length=0;document.querySelector('.root-row button').click();
    $('#confirm-confirm').click();
    for(let i=0;i<50;i++){if(snapshot.config.roots.length===0)break;await new Promise(r=>setTimeout(r,40));}
    check(fixtureCalls.some(c=>c.command==='removeRoot' && normalizedUiPath(c.arguments.path)===normalizedUiPath(expected)),'Missing correct removal request');
    check(snapshot.config.roots.length===0 && document.querySelectorAll('.root-row').length===0,'Last root stayed visible');`);

  await evaluate(`
    window.installLargeLibrary=async()=>{
      if(editorOpen())closeEdit();
      const games=Array.from({length:100},(_,i)=>({
        ...structuredClone(fixtureGame),gameDirectory:'G:/Fixture/Games/Game '+String(i).padStart(3,'0'),
        primaryExecutable:'G:/Fixture/Games/Game '+String(i).padStart(3,'0')+'/Game.exe',
        directoryName:'Game '+String(i).padStart(3,'0'),primaryProductName:'Game '+String(i).padStart(3,'0')
      }));
      fixtureSnapshot={config:{roots:[],automaticScrapingEnabled:false},library:{games},manualOverrides:{items:{}},
        steamPlan:{items:games.map(game=>({...game,status:'waiting-steam-verification'})),summary:{}},steamAccounts:{accounts:[],steamStatus:'installed-no-account'}};
      snapshot=normalizeWorkspaceSnapshot(structuredClone(fixtureSnapshot));activeTab='not-in-steam';renderLibrary();await frames();
      focusWorkspaceElement(libraryCards()[0],false);window.scrollTo(0,document.documentElement.scrollHeight);await frames();
      check(window.scrollY>1000,'Fixture does not scroll: '+JSON.stringify({y:scrollY,h:document.documentElement.scrollHeight,cards:libraryCards().length,tab:activeTab,view:$('#library-view').className}));
    };
    window.visibleLibraryAnchor=()=>libraryCards().find(card=>{const r=card.getBoundingClientRect();return r.bottom>0&&r.top<innerHeight;});
  `);
  await test('background-snapshot-at-bottom-keeps-scroll-and-unchanged-card-nodes', `
    await installLargeLibrary();const y=scrollY,first=libraryCards()[0],last=libraryCards().at(-1),image=first.querySelector('.cover-placeholder');
    applyLibrarySnapshot(structuredClone(fixtureSnapshot));await frames();
    check(Math.abs(scrollY-y)<=2,'Background snapshot jumped: '+y+' -> '+scrollY);
    check(first===libraryCards()[0]&&last===libraryCards().at(-1)&&image===first.querySelector('.cover-placeholder'),'Unchanged cards were rebuilt');`);
  await test('manual-plan-refresh-keeps-bottom-and-focus-without-scrolling-to-old-first-card', `
    const y=scrollY;await refreshSteamPlan();await frames();check(Math.abs(scrollY-y)<=2,'Manual refresh jumped to remembered card');
    check(document.activeElement===libraryCards()[0],'Refresh lost original controller focus');`);

  await test('actual-manual-refresh-action-keeps-bottom', `
    manualRefreshProgress={phase:'idle'};const y=scrollY;fixtureCalls.length=0;
    await startNonBlockingRefresh();await frames();
    check(fixtureCalls.some(call=>call.command==='prepare'),'Manual refresh did not call production bridge command');
    check(Math.abs(scrollY-y)<=2,'Actual manual refresh action reset viewport');
    manualRefreshProgress={phase:'idle'};renderArtworkRefreshControl();`);
  await test('changed-card-keeps-other-dom-and-viewport', `
    const y=scrollY,unchanged=libraryCards().at(-1),changed=libraryCards()[1];
    const next=structuredClone(fixtureSnapshot);next.library.games[1].primaryProductName='Updated title';
    applyLibrarySnapshot(next);await frames();check(Math.abs(scrollY-y)<=2,'Changed card reset viewport');
    check(libraryCards().at(-1)===unchanged&&libraryCards()[1]!==changed,'Changed card was not updated in isolation');`);

  await test('metadata-only-update-reuses-card-and-editor-sees-latest-executable', `
    const card=libraryCards().at(-1),key=card.dataset.directory,y=scrollY;
    const next=structuredClone(snapshot),game=next.library.games.find(g=>normalizedUiPath(g.gameDirectory)===normalizedUiPath(key));
    game.primaryExecutable=game.gameDirectory+'/Updated.exe';game.lastScannedAt=Date.now();
    applyLibrarySnapshot(next);await frames();check(libraryCards().at(-1)===card,'Metadata-only update reloaded artwork');
    check(Math.abs(scrollY-y)<=2,'Metadata-only update shifted viewport');
    card.dispatchEvent(new MouseEvent('dblclick',{bubbles:true}));await frames();
    check(normalizedUiPath(selectedActionGame.primaryExecutable)===normalizedUiPath(game.primaryExecutable),'Reused card edited stale executable');closeEdit();
    focusWorkspaceElement(libraryCards()[0],false);window.scrollTo(0,y);await frames();`);
  await test('focused-card-update-does-not-reveal-it-above-user-scroll', `
    const y=scrollY,focusedKey=document.activeElement?.dataset.directory;check(focusedKey,'Fixture card was not focused');const next=structuredClone(snapshot);next.library.games[0].primaryProductName='New focused title';
    applyLibrarySnapshot(next);await frames();check(Math.abs(scrollY-y)<=2,'Replacing focused card scrolled to it');
    check(document.activeElement===libraryCards().find(card=>card.dataset.directory===focusedKey),'Updated focused card did not retain controller focus');`);
  await test('adding-a-row-above-preserves-visible-card-offset', `
    const card=visibleLibraryAnchor(),key=card.dataset.directory,top=card.getBoundingClientRect().top;
    const next=structuredClone(snapshot);for(let i=0;i<7;i++)next.library.games.push({...structuredClone(fixtureGame),
      directoryName:'AAA '+i,gameDirectory:'G:/Fixture/Games/AAA '+i,primaryExecutable:'G:/Fixture/Games/AAA '+i+'/Game.exe'});
    next.steamPlan.items=next.library.games.map(game=>({...game,status:'waiting-steam-verification'}));
    applyLibrarySnapshot(next);await frames();const restored=libraryCards().find(c=>c.dataset.directory===key);
    check(restored&&Math.abs(restored.getBoundingClientRect().top-top)<=2,'Inserted games shifted current view');`);
  await test('removing-games-above-preserves-visible-card-offset', `
    const card=visibleLibraryAnchor(),key=card.dataset.directory,top=card.getBoundingClientRect().top;
    const next=structuredClone(snapshot);next.library.games=next.library.games.filter(g=>!g.directoryName.startsWith('AAA'));
    applyLibrarySnapshot(next);await frames();check(Math.abs(libraryCards().find(c=>c.dataset.directory===key).getBoundingClientRect().top-top)<=2,'Removed earlier games shifted current view');`);
  await test('rapid-snapshots-do-not-leave-delayed-focus-scroll', `
    const y=scrollY;for(let i=0;i<12;i++)applyLibrarySnapshot(structuredClone(snapshot));await frames();
    window.scrollBy(0,-120);const moved=scrollY;await new Promise(r=>setTimeout(r,300));
    check(Math.abs(scrollY-moved)<=2&&moved<y,'Late refresh callback overwrote newer user scroll');`);
  await test('selected-card-update-keeps-viewport-and-current-selection', `
    const card=visibleLibraryAnchor(),game=mergedGames().find(g=>normalizedUiPath(g.gameDirectory)===normalizedUiPath(card.dataset.directory));
    const y=scrollY,selected=isPageSelected(game);toggleGameSelection(game);await frames();
    check(isPageSelected(game)!==selected,'Selection toggle stopped working');check(Math.abs(scrollY-y)<=2,'Selection reset viewport: '+JSON.stringify({before:y,after:scrollY,anchor:card.dataset.directory,anchorOldTop:card.getBoundingClientRect().top,active:document.activeElement?.dataset.directory}));`);

  await test('removing-the-first-visible-anchor-keeps-a-surviving-visible-neighbor', `
    const old=visibleLibraryAnchor(),neighbor=libraryCards().find(c=>c!==old&&c.offsetTop===old.offsetTop);
    check(neighbor,'Missing visible neighbor');const key=neighbor.dataset.directory,top=neighbor.getBoundingClientRect().top;
    const next=structuredClone(snapshot);next.library.games=next.library.games.filter(g=>normalizedUiPath(g.gameDirectory)!==normalizedUiPath(old.dataset.directory));
    applyLibrarySnapshot(next);await frames();check(Math.abs(libraryCards().find(c=>c.dataset.directory===key).getBoundingClientRect().top-top)<=2,'Removed anchor reset surviving neighbor');`);
  await test('controller-explicit-navigation-still-reveals-the-selected-card', `
    focusLibraryCard(libraryCards()[0]);await frames();check(scrollY<500,'Controller navigation no longer reveals target card');
    focusLibraryCard(libraryCards().at(-1));await frames();check(scrollY>1000,'Controller navigation cannot reach bottom');`);
  await test('refreshing-at-top-does-not-follow-old-first-game-after-insertion', `
    window.scrollTo(0,0);await frames();const next=structuredClone(snapshot);
    for(let i=0;i<6;i++)next.library.games.push({...structuredClone(fixtureGame),directoryName:'AAA '+i,gameDirectory:'G:/Fixture/AAA '+i,primaryExecutable:'G:/Fixture/AAA '+i+'/Game.exe'});
    next.steamPlan.items=next.library.games.map(game=>({...game,status:'waiting-steam-verification'}));
    applyLibrarySnapshot(next);await frames();check(scrollY===0,'Top view followed displaced first card');`);
  await test('native-steam-local-horizontal-preview-renders-in-editor', `
    const image='data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jY4sAAAAASUVORK5CYII=';
    const game={...structuredClone(fixtureGame),steamNative:true,nativeSteamAppId:620,contentType:'steam-native'};
    const plan={...game,status:'steam-native',artworkPreview:{long:{url:image,provider:'steam-librarycache-native',file:'G:/FixtureSteam/appcache/librarycache/620/hash/library_header.jpg'}}};
    fixtureSnapshot={config:{roots:[],automaticScrapingEnabled:false},library:{games:[game]},manualOverrides:{items:{}},steamPlan:{items:[plan],summary:{}},steamAccounts:{accounts:[]}};
    applyLibrarySnapshot(structuredClone(fixtureSnapshot));activeTab='native-steam';renderLibrary();openEdit(mergedGames()[0]);
    await new Promise(r=>setTimeout(r,100));await frames();
    check($('#artwork-long-preview img')?.getAttribute('src')===image,'Native horizontal preview missing in editor');
    check(keyboardCalls().length===0,'Native artwork refresh requested keyboard');closeEdit();`);


  await test('verified-steam-identity-is-independent-of-artwork-and-candidate-numbers', `
    const steam={status:'needs-minimum-artwork',steamStoreAppId:620,identityStatus:'verified',steamVerificationStatus:'steam-verified'};
    check(isSteamReady({steam}),'Verified identity was gated by artwork');
    check(!isSteamReady({steam:{...steam,identityStatus:'unverified'}}),'Unverified numeric candidate classified as identified');
    check(!isSteamReady({steam:{...steam,steamVerificationStatus:'network-pending'}}),'Network-pending candidate classified as identified');
    check(!isSteamReady({steam:{...steam,steamStoreAppId:''}}),'Cleared ID restored by old verification status');`);
  if(identityCorpus) await test('entire-actual-local-corpus-keeps-verified-steam-identities-recognized-without-artwork', `
    if(editorOpen())closeEdit();fixtureSnapshot=structuredClone(actualLocalIdentitySnapshot);
    applyLibrarySnapshot(structuredClone(fixtureSnapshot));const games=mergedGames();check(games.length>=40,'Full local scan missing in actual UI');
    const verified=games.filter(game=>!isNativeSteamGame(game)&&game.steam?.steamVerificationStatus==='steam-verified'&&game.steam?.identityStatus==='verified');
    check(verified.length>=20,'Too few live Steam-verified games in UI corpus');
    const wrong=verified.filter(game=>category(game)!=='not-in-steam');
    check(wrong.length===0,'Verified SteamID was displayed as unidentified: '+JSON.stringify(wrong.map(g=>({exe:g.primaryExecutable,status:g.steam?.status,category:category(g)}))));
    activeTab='not-in-steam';renderLibrary();await frames();
    for(const game of verified)check(libraryCards().some(card=>normalizedUiPath(card.dataset.executable)===normalizedUiPath(game.primaryExecutable)),'Verified game missing from recognized page');`);
  const uiErrors=await evaluate(`fixtureCalls.filter(call=>call.command==='uiError')`);
  cases.push({name:'complete-shipping-UI-has-no-unhandled-errors',passed:uiErrors.length===0,...(uiErrors.length?{errors:uiErrors}:{})});
  const report={allPassed:cases.every(item=>item.passed),caseCount:cases.length,cases,realBrowserUsed:true,
    nativeBridge:'fixture-only',realSteamFilesModified:false,realGamesLaunched:false};
  fs.writeFileSync(path.join(root,'summary.json'),JSON.stringify(report,null,2));
  console.log(JSON.stringify(report,null,2));console.log('REPORT='+path.join(root,'summary.json'));
  if(!report.allPassed)process.exitCode=1;
} finally {
  socket?.close();for(const item of waiting.values()){clearTimeout(item.timer);item.reject(new Error('Browser test closing'));}
  server.close();browser.kill();await sleep(150);fs.closeSync(log);
}
