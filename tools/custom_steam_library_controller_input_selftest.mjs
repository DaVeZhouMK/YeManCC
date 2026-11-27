import fs from 'node:fs';
import { createHash } from 'node:crypto';
import path from 'node:path';
import http from 'node:http';
import assert from 'node:assert/strict';
import { spawn, spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

// Exercise the complete shipping HTML/JS in an isolated Edge profile. All
// native bridge replies are local fixtures; no real Steam/game data is used.
const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const build = path.resolve(repo, '../../Build/CustomSteamLibrary');
const root = path.resolve(process.argv[2] || path.join(build, 'controller-input-' + Date.now()));
const coreEvidence=path.resolve(repo,'../../../Testing/YMCC/Evidence');
assert([build,coreEvidence].some(base=>root.startsWith(base+path.sep)) && !fs.existsSync(root), 'Use a fresh isolated Build or Testing/YMCC/Evidence directory');
fs.mkdirSync(root, { recursive: true });
fs.mkdirSync(path.join(root,'case-observations'),{recursive:false});
const selectedSidebarCases=process.env.YMCC_LIBRARY_CONTROLLER_CASES?JSON.parse(process.env.YMCC_LIBRARY_CONTROLLER_CASES):null;
const sidebarCaseNames=[[1280,720],[1280,800],[1920,1080]].map(([w,h])=>'single-card-action-row-and-controller-navigation-'+w+'x'+h).concat([...fs.readFileSync(fileURLToPath(import.meta.url),'utf8').matchAll(/await test\('([^']+)'\s*,/g)].map(match=>match[1]).filter(name=>!name.endsWith('-')));
if(selectedSidebarCases)assert(Array.isArray(selectedSidebarCases)&&selectedSidebarCases.length>0&&new Set(selectedSidebarCases).size===selectedSidebarCases.length&&selectedSidebarCases.every(name=>sidebarCaseNames.includes(name)),'Unknown/empty/duplicate controller sidebar selection');
const ui = process.env.YEMAN_LIBRARY_UI_DIRECTORY ? path.resolve(process.env.YEMAN_LIBRARY_UI_DIRECTORY) : path.join(repo, 'CustomSteamLibrary/workspace-ui');
assert(ui === path.join(repo, 'CustomSteamLibrary/workspace-ui') || ui.startsWith(build + path.sep), 'UI override must be an isolated Build package');
const sha = file => createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const inputFiles = ['index.html','app.js','styles.css','controller-keys.css'].map(name=>path.join(ui,name)).concat(fileURLToPath(import.meta.url));
const sourceHashes = Object.fromEntries(inputFiles.map(file=>[file,sha(file)]));
const edge = process.env.YEMAN_BROWSER_EXECUTABLE || 'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe';
assert(fs.existsSync(edge), 'Microsoft Edge is required');
const mock = `
  window.fixtureCalls = [];
  window.fixtureSteamRunning = false;
  window.fixtureFailures = {};
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
  window.fixtureEmit=message=>listeners.forEach(listener=>listener({data:message}));
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
      if(window.fixtureLaunchReplies?.[request.command])result=structuredClone(window.fixtureLaunchReplies[request.command]);
      if(request.command==='steamState' || request.command==='steamCommitPreflight')result={steamRunning:window.fixtureSteamRunning,steamPath:''};
      if(request.command==='steamCommitPreflight' && window.fixtureArtworkPreflight)result=structuredClone(window.fixtureArtworkPreflight);
      if(request.command==='closeSteam'){window.fixtureSteamRunning=false;result={steamRunning:false,steamPath:''};}
      if(window.fixtureFailures[request.command]) {
        setTimeout(()=>listeners.forEach(listener=>listener({data:{kind:'response',id:request.id,ok:false,error:window.fixtureFailures[request.command]}})),0);
        return;
      }
      const complete=()=>{
        if(request.command==='commit'&&window.fixtureSteamRunning){listeners.forEach(listener=>listener({data:{kind:'response',id:request.id,ok:false,error:'fixture Steam is running'}}));return;}
        listeners.forEach(listener=>listener({data:{kind:'response',id:request.id,ok:true,result}}));
      };
      if(request.command==='plan'&&window.fixtureHoldPlan){(window.fixturePlanReleases||=[]).push(complete);return;}
      if(request.command==='commit'&&window.fixtureHoldCommit){(window.fixtureCommitReleases||=[]).push(complete);return;}
      if(request.command==='refreshSteamArtwork'&&window.fixtureHoldArtworkRepair){window.fixtureReleaseArtworkRepair=complete;return;}
      setTimeout(complete,request.command==='commit'?80:0);
    }
  };
`;
const server = http.createServer((req, res) => {
  const name = new URL(req.url, 'http://localhost').pathname;
  const file = name === '/' ? 'index.html' : name.slice(1);
  if (!['index.html','app.js','styles.css','controller-keys.css'].includes(file)) { res.writeHead(404); res.end(); return; }
  let text = fs.readFileSync(path.join(ui, file), 'utf8');
  if (file === 'index.html') text = text.replace('<head>', '<head><script>' + mock + '</script>');
  res.writeHead(200, {'Content-Type': file.endsWith('.js') ? 'text/javascript' : file.endsWith('.css') ? 'text/css' : 'text/html'});
  res.end(text);
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const origin = 'http://127.0.0.1:' + server.address().port;
const log = fs.openSync(path.join(root, 'browser.log'), 'w');
const browser = spawn(edge, ['--headless=new','--disable-gpu','--no-first-run','--disable-extensions',
  '--disable-background-networking','--remote-debugging-port=0','--remote-debugging-address=127.0.0.1',
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
  if(selectedSidebarCases&&!selectedSidebarCases.includes(name))return;
  try { await evaluate(`(async()=>{${body};return true;})()`);cases.push({name,passed:true}); }
  catch(error){cases.push({name,passed:false,error:error.message});}
  fs.writeFileSync(path.join(root,'case-observations',String(cases.length)+'.json'),JSON.stringify(cases.at(-1),null,2),{flag:'wx'});
  if(!cases.at(-1).passed)throw Error('Original controller case failed: '+name+' '+cases.at(-1).error);
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
    window.waitFor=async(predicate)=>{for(let i=0;i<150;i++){if(predicate())return;await new Promise(resolve=>setTimeout(resolve,10));}throw Error('Fixture wait timed out');};
    window.keyboardCalls=()=>fixtureCalls.filter(call=>call.command==='keyboard');
    window.editorOpen=()=>!document.querySelector('#edit-modal').classList.contains('hidden');
    window.resetEditor=async(native=false)=>{
      if(editorOpen())closeEdit();controllerBackLastAt=0;
      const game=structuredClone(fixtureGame);if(native)game.steamNative=true;
      openEdit(game);await frames();fixtureCalls.length=0;
    };
  `);
  await test('native-covers-without-primary-are-pointer-and-controller-editable', `
    const previous=snapshot,previousFixtures=fixtureSnapshot,previousTab=activeTab;
    const previousFocus=Object.getOwnPropertyDescriptor(document,'hasFocus');
    const previousCalls=fixtureCalls.slice();
    const native=(name,id,primaryExecutable=null)=>({gameDirectory:'G:/FixtureSteam/steamapps/common/'+name,directoryName:name,primaryExecutable,steamNative:true,nativeSteamAppId:id,candidates:[]});
    const newer=[native('A1',4209920),native('Project Zomboid',108600)],old=native('Old native',620,'G:/FixtureSteam/steamapps/common/Old native/game.exe'),unknown=native('Unconfirmed',0);
    const games=[...newer,old,unknown];
    const calls=name=>fixtureCalls.filter(item=>item.command===name);
    const nav=action=>{lastDirectionalActionAt=0;controllerBackLastAt=0;routeWorkspaceAction(action);};
    const hidden=id=>$(id).classList.contains('hidden');
    const preview=type=>$('#artwork-'+type+'-preview');
    const image='G:/FixtureSteam/portrait.png';
    fixtureSnapshot={...previousFixtures,library:{games},steamPlan:{items:games.map(row=>({...row,status:'already-in-steam'}))}};
    window.fixtureLaunchReplies={pickArtwork:{path:image}};
    try{
      Object.defineProperty(document,'hasFocus',{configurable:true,value:()=>true});
      snapshot=normalizeWorkspaceSnapshot(structuredClone(fixtureSnapshot),snapshot);
      activeTab='native-steam';renderLibrary();
      for(const row of newer){
        const card=libraryCards().find(item=>item.dataset.directory===keyFor(row));
        check(card,'Native fixture card missing');focusWorkspaceElement(card);nav('edit');await frames();
        check(editorOpen()&&keyFor(selectedActionGame)===keyFor(row),'Controller did not open the intended editor');
        check(preview('cover').tabIndex===0&&preview('long').tabIndex===0&&getComputedStyle(preview('cover')).cursor==='pointer','Unconfirmed EXE disabled the cover');
        check($('#edit-save-content').disabled,'Unchanged native editor allowed saving');
        check(document.activeElement===preview('cover'),'Expected initial cover focus, got '+document.activeElement?.outerHTML.slice(0,150)+'; modal='+activeWorkspaceModal()?.id+'; active='+libraryRenderAllowed()+'; visible='+visibleFocusables($('#edit-modal')).includes(preview('cover')));
        nav('navigate-down');check(document.activeElement===preview('long'),'Horizontal cover unreachable');
        nav('accept');await frames();check(!hidden('#artwork-action-modal')&&artworkActionType==='long','A did not open horizontal cover actions');
        nav('accept');await waitFor(()=>calls('searchArtwork').some(item=>item.arguments.steamAppId===String(row.nativeSteamAppId)));
        check(!hidden('#artwork-search-modal'),'A did not open the cover candidate selector');nav('back');await frames();
        check(hidden('#artwork-search-modal')&&document.activeElement===preview('long'),'B did not return from candidates to the horizontal cover');
        preview('cover').click();await frames();check(!hidden('#artwork-action-modal')&&artworkActionType==='cover','Mouse did not open vertical cover actions');
        $('#artwork-action-replace').click();await waitFor(()=>artworkDraft.dirty.cover);check(!$('#edit-save-content').disabled,'Cover edit did not enable Save');
        preview('long').click();await frames();$('#artwork-action-delete').click();
        const id=String(row.nativeSteamAppId),before=calls('saveEditor').length;
        fixtureSnapshot.manualOverrides={items:{},nativeCovers:{[id]:{cover:{file:image},artworkProtection:{cover:'manual',long:'deleted'}}}};
        $('#edit-save-content').click();await waitFor(()=>calls('saveEditor').length===before+1&&!editSaveInFlight);
        const batch=calls('saveEditor').at(-1).arguments,ops=batch.operations;
        check(batch.refreshNativeArtwork&&ops.length===1&&ops[0].command==='saveArtworkOverride','Cover save changed unrelated settings');
        const args=ops[0].arguments;
        check(args.nativeSteamAppId===id&&keyFor(args)===keyFor(row)&&args.coverPath===image&&args.clearLong===true&&!('executable' in args)&&!('wallpaperPath' in args),'Cover save had incorrect identity or slots');
        check(artworkDraft.cover.asset?.file===image&&artworkDraft.long.state==='deleted','Saved native covers did not project back into the editor');
        nav('back');await frames();check(!editorOpen()&&libraryCards().some(item=>item===document.activeElement&&item.dataset.directory===keyFor(row)),'Back did not restore the native card');
      }
      openEdit(mergedGames().find(row=>keyFor(row)===keyFor(old)));await frames();
      preview('cover').click();await frames();check(!hidden('#artwork-action-modal'),'Old native cover lost mouse access');
      nav('back');await frames();check(document.activeElement===preview('cover'),'Old native cover lost B focus return');closeEdit();
      openEdit(mergedGames().find(row=>keyFor(row)===keyFor(unknown)));await frames();
      preview('cover').click();check(hidden('#artwork-action-modal')&&preview('cover').tabIndex<0,'No confirmed AppID granted cover access');
    } finally {
      if(editorOpen())closeEdit();fixtureSnapshot=previousFixtures;delete window.fixtureLaunchReplies;snapshot=previous;activeTab=previousTab;renderLibrary();
      if(previousFocus)Object.defineProperty(document,'hasFocus',previousFocus);else delete document.hasFocus;
      fixtureCalls.splice(0,fixtureCalls.length,...previousCalls);
    }
  `);
  await test('startup-detects-account-despite-cached-no-account', `
    if(steamAccountRequest)await steamAccountRequest;
    check(fixtureCalls.some(call=>call.command==='steamAccounts'),'Startup did not detect accounts');
    check($('#steam-target-status').textContent.includes('已发现 1 个'),'Cached no-account warning survived startup');
    check($('#confirm-modal').classList.contains('hidden'),'False login/install modal opened');
    check($('#busy').classList.contains('hidden'),'Account probe blocked the library');`);
  await test('older-game-snapshot-does-not-reintroduce-login-warning', `
    applyLibrarySnapshot(structuredClone(fixtureSnapshot));
    check($('#steam-target-status').textContent.includes('已发现 1 个'),'Older scan replaced fresh account state');`);
  await test('no-account-directory-does-not-claim-signed-out-or-offer-install', `
    fixtureSteamAccounts={accounts:[],steamStatus:'installed-no-account'};
    await refreshSteamAccountDiscovery(true);
    check($('#steam-target-status').textContent.includes('暂未检测到'),'Missing account was hidden');
    check(!/未登录|尚未登录/.test($('#steam-target-status').textContent),'Missing directory was called signed out');
    check($('#confirm-modal').classList.contains('hidden'),'Installed Steam was offered installation');`);
  await test('focus-return-recovers-accounts-without-another-click', `
    fixtureSteamAccounts={accounts:[{accountId:'1000',steamRoot:'G:/FixtureSteam'}],steamStatus:'ready'};
    steamAccountLastCheck=Date.now()-STEAM_ACCOUNT_RECHECK_MS-1;
    window.dispatchEvent(new Event('focus'));
    if(steamAccountRequest)await steamAccountRequest;
    check($('#steam-target-status').textContent.includes('已发现 1 个'),'Focus did not recheck after login');
    check($('#in-steam-limitation').classList.contains('hidden'),'Stale warning remained visible');`);
  await test('controller-X-opens-editor-with-locked-inputs-and-no-keyboard', `
    activeTab=category(mergedGames()[0]);renderLibrary();const card=libraryCards()[0];check(card,'Fixture card missing');
    focusLibraryCard(card);fixtureCalls.length=0;routeWorkspaceAction('edit');await frames();
    check(editorOpen(),'X did not open editor');check($('#identity-name').readOnly && $('#identity-steam-id').readOnly,'Opened editable inputs');
    check(keyboardCalls().length===0,'Opening X requested keyboard');`);
  await test('controller-focus-and-direction-do-not-request-keyboard', `
    await resetEditor();
    for(let i=0;i<8;i++){const input=$(i%2?'#identity-name':'#identity-steam-id');focusWorkspaceElement(input);routeWorkspaceAction(i%2?'navigate-down':'navigate-up');}
    check($('#identity-name').readOnly && $('#identity-steam-id').readOnly,'Navigation unlocked input');
    check(keyboardCalls().length===0,'Navigation requested keyboard');`);
  await evaluate("focusWorkspaceElement($('#identity-name'));window.lockedInputValue=$('#identity-name').value;");
  await command('Input.insertText',{text:'must-not-be-entered'});
  await test('navigation-selected-readonly-field-does-not-accept-text', `
    check($('#identity-name').value===lockedInputValue && !editDraft.identityDirty,'Locked field accepted typing');
    check(keyboardCalls().length===0,'Locked text attempt requested keyboard');`);
  await test('A-unlocks-only-selected-name-and-requests-keyboard-once', `
    await resetEditor();focusWorkspaceElement($('#identity-name'));routeWorkspaceAction('accept');
    check(!$('#identity-name').readOnly && $('#identity-name').dataset.controllerEditing==='true','A did not enter text mode');
    check($('#identity-steam-id').readOnly,'A unlocked unrelated input');check(keyboardCalls().length===1,'Keyboard was not explicitly requested once');`);
  await command('Input.insertText',{text:' — edited'});
  await test('real-browser-text-input-updates-draft', `
    check($('#identity-name').value.includes('edited'),'Text input was lost');
    check(editDraft.identityDirty && editDraft.game.override.name===$('#identity-name').value.trim(),'Text input did not update draft');`);
  await test('physical-caret-arrows-stay-in-text-mode', `
    const input=$('#identity-name'),event=new KeyboardEvent('keydown',{key:'ArrowLeft',bubbles:true,cancelable:true});
    input.dispatchEvent(event);check(!event.defaultPrevented && !input.readOnly && document.activeElement===input,'Physical caret arrows were routed as navigation');`);
  await test('B-leaves-input-without-closing-editor-or-arming-double-B', `
    controllerBackLastAt=performance.now();const name=$('#identity-name');routeWorkspaceAction('back');
    check(editorOpen() && name.readOnly && !name.dataset.controllerEditing,'B closed editor instead of ending input');
    check(document.activeElement===name,'B did not keep selected field');
    check(controllerBackLastAt===0 && !fixtureCalls.some(call=>call.command==='closeWorkspace'),'Input B armed workspace exit');`);
  await test('next-B-closes-editor-and-returns-to-card', `
    routeWorkspaceAction('back');await frames();check(!editorOpen(),'Second B did not return to library');
    check(document.activeElement?.classList.contains('game-card'),'Editor did not return to original card');
    check(!fixtureCalls.some(call=>call.command==='closeWorkspace'),'Single navigation B exited workspace');`);
  await test('controller-direction-relocks-active-input-before-navigation', `
    await resetEditor();focusWorkspaceElement($('#identity-name'));routeWorkspaceAction('accept');
    lastDirectionalActionAt=0;routeWorkspaceAction('navigate-down');
    check($('#identity-name').readOnly && !$('#identity-name').dataset.controllerEditing,'Direction left text mode unlocked');
    check(document.activeElement===$('#identity-steam-id') && $('#identity-steam-id').readOnly,'Direction did not select locked AppID');
    check(keyboardCalls().length===1,'Navigation reopened keyboard');`);
  await test('A-enters-AppID-input-and-blur-relocks', `
    await resetEditor();focusWorkspaceElement($('#identity-steam-id'));routeWorkspaceAction('accept');
    check(!$('#identity-steam-id').readOnly && keyboardCalls().length===1,'AppID A failed');
    focusWorkspaceElement($('#edit-save-content'));check($('#identity-steam-id').readOnly,'Blur did not relock AppID');`);
  await test('physical-Enter-enters-input-without-touch-keyboard-request', `
    await resetEditor();const name=$('#identity-name');focusWorkspaceElement(name);
    name.dispatchEvent(new KeyboardEvent('keydown',{key:'Enter',bubbles:true,cancelable:true}));
    check(!name.readOnly && keyboardCalls().length===0,'Physical Enter failed or requested touch keyboard');`);
  await test('physical-Escape-first-leaves-input-only', `
    $('#identity-name').dispatchEvent(new KeyboardEvent('keydown',{key:'Escape',bubbles:true,cancelable:true}));
    check(editorOpen() && $('#identity-name').readOnly,'Escape discarded editor while typing');`);
  await evaluate(`resetEditor().then(()=>{const input=$('#identity-name');input.scrollIntoView({block:'center'});})`);
  const point=await evaluate(`(()=>{const rect=$('#identity-name').getBoundingClientRect();return{x:rect.left+rect.width/2,y:rect.top+rect.height/2};})()`);
  await command('Input.dispatchMouseEvent',{type:'mousePressed',button:'left',clickCount:1,...point});
  await command('Input.dispatchMouseEvent',{type:'mouseReleased',button:'left',clickCount:1,...point});
  await test('real-mouse-click-enters-text-input-without-A', `
    check(document.activeElement===$('#identity-name') && !$('#identity-name').readOnly,'Mouse click could not edit name');
    check(keyboardCalls().length===0,'Mouse click issued controller keyboard request');`);
  await test('close-and-reopen-clears-input-mode', `
    closeEdit();await resetEditor();check($('#identity-name').readOnly && $('#identity-steam-id').readOnly,'Text mode leaked across editors');
    check(keyboardCalls().length===0,'Reopening requested keyboard');`);
  await test('native-Steam-AppID-stays-immutable-for-A-and-pointer', `
    await resetEditor(true);const id=$('#identity-steam-id');focusWorkspaceElement(id);routeWorkspaceAction('accept');
    id.dispatchEvent(new PointerEvent('pointerdown',{button:0,bubbles:true}));
    check(id.readOnly && !id.dataset.controllerEditing && keyboardCalls().length===0,'Native AppID was unlocked');
    check($('#identity-steam-id-clear').disabled,'Native AppID clear enabled');
    focusWorkspaceElement($('#identity-name'));routeWorkspaceAction('accept');
    check(!$('#identity-name').readOnly && keyboardCalls().length===1,'Native game name could not be edited');`);
  await evaluate(`
    window.resetCommitFixture=async()=>{
      if(editorOpen())closeEdit();
      steamAddNotices.length=0;if(!$('#steam-add-notice-modal').classList.contains('hidden'))closeSteamAddNotice();
      if(!$('#steam-commit-modal').classList.contains('hidden'))closeSteamCommit();
      if(!$('#confirm-modal').classList.contains('hidden'))closeConfirm(false);
      for(const release of (window.fixtureCommitReleases||[]))release();window.fixtureCommitReleases=[];window.fixtureHoldCommit=false;
      await new Promise(resolve=>setTimeout(resolve,100));steamCommitTasks.clear();
      fixtureSteamRunning=false;fixtureFailures={};controllerBackLastAt=0;
      const games=['Portal','Other','Third'].map(name=>({...structuredClone(fixtureGame),directoryName:name,primaryProductName:name,gameDirectory:'G:/Fixture/Games/'+name,primaryExecutable:'G:/Fixture/Games/'+name+'/Game.exe'}));
      fixtureSnapshot.library.games=games;fixtureSnapshot.steamPlan={items:games.map(game=>({...game,status:'waiting-steam-verification'})),summary:{}};
      snapshot=normalizeWorkspaceSnapshot(structuredClone(fixtureSnapshot),snapshot);
      activeTab='not-in-steam';selectedGameDirectories=null;renderLibrary();
      openEdit(mergedGames()[0]);await frames();fixtureCalls.length=0;
    };

    window.sidebarLayoutObservations=[];
    window.prepareSidebarLayout=async()=>{
      if(editorOpen())closeEdit();
      const image='data:image/svg+xml,'+encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" width="200" height="300"><rect width="200" height="300" fill="#1f8ac0"/><circle cx="105" cy="100" r="62" fill="#eebf4a"/><path d="M0 240L200 120V300H0Z" fill="#075768"/></svg>');
      const games=Array.from({length:36},(_,i)=>({...structuredClone(fixtureGame),directoryName:'Sidebar Game '+String(i).padStart(2,'0'),primaryProductName:'Sidebar Game '+i,gameDirectory:'G:/Fixture/Sidebar/'+i,primaryExecutable:'G:/Fixture/Sidebar/'+i+'/Game.exe'}));
      const overrides={};for(const game of games)overrides[normalizedUiPath(game.primaryExecutable)]={cover:{url:image}};
      snapshot={...snapshot,library:{games},manualOverrides:{items:overrides},gameMedia:{entries:{}},steamPlan:{items:games.map(game=>({...game,status:'already-in-steam'})),summary:{}}};
      activeTab='in-steam';selectedSteamDirectories=null;renderLibrary();focusLibraryCard(libraryCards()[1]);await frames();await Promise.all([...document.querySelectorAll('.game-card img')].flatMap(image=>image.getAnimations().filter(animation=>animation.transitionProperty==='filter').map(animation=>animation.finished.catch(()=>{}))));
    };
    window.checkSidebarLayout=async()=>{
      await prepareSidebarLayout();
      const cards=libraryCards(),current=activeLibraryCard(),hint=current.querySelector('.sidebar-card-edit'),key=hint.querySelector('[data-controller-key="X"]');
      const box=hint.getBoundingClientRect(),cover=current.querySelector('.cover-placeholder').getBoundingClientRect();
      check(getComputedStyle(hint).display!=='none'&&getComputedStyle(key).display!=='none'&&hint.textContent.includes('编辑'),'Focused-card X edit disappeared');
      check(box.width>=42&&box.left>=cover.left&&box.right<=cover.right+1&&box.top>=cover.top&&box.bottom<=cover.bottom+1,'Focused-card X edit was clipped');
      const focused=getComputedStyle(current.querySelector('.cover-placeholder')),other=getComputedStyle(cards[0].querySelector('.cover-placeholder'));
      check(focused.outlineColor!==other.outlineColor&&parseFloat(focused.outlineWidth)>=3,'Current card is not distinct from other checked cards');
      const activeImage=current.querySelector('img'),otherImage=cards[0].querySelector('img');
      if(activeImage&&otherImage)check(getComputedStyle(activeImage).filter==='none'&&getComputedStyle(otherImage).filter.includes('brightness'),'Other cards did not retain dimming');
      cards[9].dispatchEvent(new PointerEvent('pointerenter',{pointerId:1,isPrimary:true}));
      await Promise.all([...document.querySelectorAll('.game-card img')].flatMap(image=>image.getAnimations().filter(animation=>animation.transitionProperty==='filter').map(animation=>animation.finished.catch(()=>{}))));
      check(document.activeElement===current,'Pointer hover stole controller focus');
      for(const card of cards)if(card!==current&&card.querySelector('img'))check(getComputedStyle(card.querySelector('img')).filter.includes('brightness'),'Hovered/stale current card outshone controller focus');
      check(!activeImage||getComputedStyle(activeImage).filter==='none','Controller-focused image lost full brightness after hover');
      const tabs=[...document.querySelectorAll('#library-sidebar .page-tab')];focusWorkspaceElement(tabs[0]);await frames();
      let style=getComputedStyle(tabs[0]);check(parseFloat(style.outlineWidth)>=2&&style.outlineStyle!=='none','Controller sidebar focus has no visible ring');
      lastDirectionalActionAt=0;routeWorkspaceAction('navigate-down');await frames();check(document.activeElement===tabs[1],'Sidebar direction did not reach the adjacent item');
      style=getComputedStyle(tabs[1]);check(parseFloat(style.outlineWidth)>=2&&style.outlineStyle!=='none','Moved sidebar focus became invisible');
      const search=$('.library-sidebar-search').getBoundingClientRect(),refresh=$('#refresh-artwork-button').getBoundingClientRect();
      check(!$('#library-sidebar').contains($('#refresh-artwork-button'))&&refresh.left>=search.right-1&&Math.abs(refresh.top-search.top)<2,'Manual refresh is not immediately right of search');
      for(const button of document.querySelectorAll('#library-sidebar .steam-bar .button'))if(button.getBoundingClientRect().width){check(getComputedStyle(button).whiteSpace==='nowrap'&&button.scrollWidth<=button.clientWidth+1,'Steam sidebar action wrapped or overflowed');}
      document.querySelector('[data-library-density=compact]').click();focusLibraryCard(current);await frames();
      const compactHint=hint.getBoundingClientRect(),compactCover=current.querySelector('.cover-placeholder').getBoundingClientRect();
      check(getComputedStyle(hint).display!=='none'&&getComputedStyle(key).display!=='none'&&hint.textContent.includes('编辑'),'Compact cards hid the controller X edit hint');
      check(compactHint.width>=42&&compactHint.left>=compactCover.left&&compactHint.right<=compactCover.right+1&&compactHint.bottom<=compactCover.bottom+1,'Compact X edit was clipped or compressed');
      const compactWidth=compactCover.width;
      document.querySelector('[data-library-density=standard]').click();
      sidebarLayoutObservations.push({width:innerWidth,height:innerHeight,visibleCards:cards.length,hintWidth:box.width,imageDimmingChecked:Boolean(activeImage&&otherImage),refreshRightOfSearch:true,sidebarFocusVisible:true,compactHintVisible:true,compactWidth,hoverCannotOutshineController:true});
      await resetCommitFixture();
    };
    window.commitCalls=()=>fixtureCalls.filter(call=>call.command==='commit');
    window.openSingleCommit=async()=>{
      focusWorkspaceElement($('#edit-add-to-steam'));routeWorkspaceAction('accept');
      await waitFor(()=>commitCalls().length===1);
    };
  `);
  for(const [width,height] of [[1280,720],[1280,800],[1920,1080]]) {
    await command('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:1,mobile:false});
    await test('single-card-action-row-and-controller-navigation-'+width+'x'+height, `
      await resetCommitFixture();
      check($('#edit-launch-open').classList.contains('hidden'),'Ordinary mode exposed the advanced VBS action');
      const buttons=[...document.querySelectorAll('.edit-actions button')].filter(isVisuallyPresent);
      check(buttons.map(button=>button.id).join(',')==='edit-folder,edit-search-game,edit-save-content,edit-add-to-steam,edit-back-button','Incorrect action order');
      check(!$('#edit-add-to-steam').disabled,'Single-card action disabled');
      for(let i=0;i<buttons.length;i++){
        focusWorkspaceElement(buttons[i]);
        for(const [action,step] of [['navigate-left',-1],['navigate-right',1]]){
          lastDirectionalActionAt=0;routeWorkspaceAction(action);
          const expected=buttons[Math.max(0,Math.min(buttons.length-1,i+step))];
          check(document.activeElement===expected,'Horizontal direction left the visual action row: '+buttons[i].id+' '+action+' -> '+document.activeElement?.id+' expected '+expected.id);
          focusWorkspaceElement(buttons[i]);
        }
      }
      const rects=buttons.map(button=>button.getBoundingClientRect());
      check(rects.every(rect=>Math.abs(rect.top-rects[0].top)<1 && rect.left>=0 && rect.right<=innerWidth),'Action row clipped or wrapped');
      focusWorkspaceElement($('#edit-add-to-steam'));await frames();const rect=$('#edit-add-to-steam').getBoundingClientRect();
      check(rect.top>=0 && rect.bottom<=innerHeight,'Focused add button is not fully visible');
      lastDirectionalActionAt=0;routeWorkspaceAction('navigate-up');
      check(document.activeElement!==$('#edit-add-to-steam') && $('#edit-modal').contains(document.activeElement),'Cannot navigate up from single-card action');
      focusWorkspaceElement($('#edit-add-to-steam'));
      await checkSidebarLayout();
      fixtureSteamRunning=true;const key=keyFor(selectedActionGame);
      fixtureFailures.commit='请先关闭 Steam，再点击卡片中的加入 Steam 重试。'.repeat(6);
      await openSingleCommit();await waitFor(()=>steamCommitTasks.get(key)?.phase==='failed');await frames();
      check(activeWorkspaceModal()?.id==='steam-add-notice-modal','Steam failure did not own its result popup');
      check($('#edit-status').textContent==='加入失败'&&!$('#edit-status').textContent.includes('请先关闭'),'Detailed failure returned to editor header');
      const dialog=$('#steam-add-notice-modal .modal-card').getBoundingClientRect(),ok=$('#steam-add-notice-confirm').getBoundingClientRect();
      check(dialog.left>=0&&dialog.right<=innerWidth&&dialog.top>=0&&dialog.bottom<=innerHeight,'Steam result popup clipped');
      check(ok.height>=44&&ok.top>=0&&ok.bottom<=innerHeight&&$('#steam-add-notice-message').textContent===fixtureFailures.commit,'Popup text lost or dismiss action clipped');
      lastDirectionalActionAt=0;routeWorkspaceAction('navigate-up');check($('#steam-add-notice-modal').contains(document.activeElement),'Direction escaped result popup');
      focusWorkspaceElement($('#steam-add-notice-confirm'));routeWorkspaceAction('accept');await frames();
      check($('#steam-add-notice-modal').classList.contains('hidden')&&editorOpen()&&document.activeElement===$('#edit-add-to-steam'),'A did not return to the same editor action');
      check(commitCalls().length===1&&!fixtureCalls.some(call=>call.command==='closeSteam'),'Popup dismissed by changing Steam or retrying');
    `);
    const screenshot = await command('Page.captureScreenshot',{format:'png',captureBeyondViewport:false});
    fs.writeFileSync(path.join(root,'single-card-editor-'+height+'.png'),Buffer.from(screenshot.data,'base64'));
    const actionRow=await evaluate(`(()=>{const r=$('.edit-actions').getBoundingClientRect();return{x:r.left,y:r.top,width:r.width,height:r.height,scale:1};})()`);
    const rowScreenshot=await command('Page.captureScreenshot',{format:'png',clip:actionRow,captureBeyondViewport:false});
    fs.writeFileSync(path.join(root,'single-card-actions-'+height+'.png'),Buffer.from(rowScreenshot.data,'base64'));
    await test('A-queues-single-game-inline-and-B-keeps-the-write-'+height, `
      window.fixtureHoldCommit=true;const key=keyFor(selectedActionGame);await openSingleCommit();await frames();
      check($('#steam-commit-modal').classList.contains('hidden')&&$('#busy').classList.contains('hidden'),'Single-card operation opened a modal');
      check(steamCommitTasks.get(key)?.phase==='queued'&&$('#edit-add-to-steam').disabled,'Inline queued state missing');
      check(activeWorkspaceModal().id==='edit-modal','Queue took ownership from the editor');
      controllerBackLastAt=0;routeWorkspaceAction('back');await frames();check(!editorOpen(),'B could not return during a write');
      window.fixtureCommitReleases.shift()();await waitFor(()=>steamCommitTasks.get(key)?.phase==='completed');
      check(!editorOpen()&&commitCalls().length===1,'B cancelled or duplicated the accepted write');window.fixtureHoldCommit=false;
    `);
  }
  await test('single-card-write-progress-stays-inline-until-complete', `
    await resetCommitFixture();window.fixtureHoldCommit=true;const key=keyFor(selectedActionGame);await openSingleCommit();await frames();
    check($('#busy').classList.contains('hidden')&&$('#steam-commit-modal').classList.contains('hidden')&&activeWorkspaceModal().id==='edit-modal','Commit progress took modal ownership');
    check($('#edit-status').textContent.includes('排队')&&$('#edit-add-to-steam').disabled,'Current card omitted its pending status');
    window.fixtureCommitReleases.shift()();await waitFor(()=>steamCommitTasks.get(key)?.phase==='completed');await frames();
    check(editorOpen()&&!$('#edit-add-to-steam').disabled&&$('#edit-status').textContent.includes('已加入')&&activeWorkspaceModal()?.id==='steam-add-notice-modal','Completion did not restore controls with a result popup');window.fixtureHoldCommit=false;
  `);
  await test('single-card-target-survives-selection-page-and-background-changes', `
    await resetCommitFixture();window.fixtureHoldCommit=true;const key=keyFor(selectedActionGame);await openSingleCommit();
    selectedGameDirectories=new Set([keyFor(mergedGames()[1])]);closeEdit();activeTab='in-steam';renderLibrary();
    window.dispatchEvent(new Event('blur'));window.fixtureCommitReleases.shift()();await waitFor(()=>steamCommitTasks.get(key)?.phase==='completed');
    check(JSON.stringify(commitCalls()[0].arguments.selectedGameDirectories)===JSON.stringify([key]),'Mutable page selection replaced captured one-game scope');
    check(!editorOpen()&&$('#busy').classList.contains('hidden'),'Background completion reopened an editor or progress dialog');window.fixtureHoldCommit=false;
  `);
  await test('single-card-works-with-no-page-checkboxes-selected', `
    await resetCommitFixture();selectedGameDirectories=new Set();const key=keyFor(selectedActionGame);await openSingleCommit();
    await waitFor(()=>steamCommitTasks.get(key)?.phase==='completed');
    check(JSON.stringify(commitCalls()[0].arguments.selectedGameDirectories)===JSON.stringify([key]),'Empty page selection replaced editor target');
  `);
  await test('ordinary-mode-save-separates-library-from-steam-update', `
    await resetCommitFixture();const key=keyFor(selectedActionGame),exe=normalizedUiPath(selectedActionGame.primaryExecutable);closeEdit();
    const row=fixtureSnapshot.steamPlan.items.find(game=>keyFor(game)===key);row.status='already-in-steam';row.steamMembershipSource='game-launch-script';
    fixtureSnapshot.manualOverrides.items[exe]={launchConfiguration:{mode:'auto'}};
    applyLibrarySnapshot(structuredClone(fixtureSnapshot));openEdit(mergedGames().find(game=>keyFor(game)===key));await frames();
    check($('#edit-launch-mode').value==='auto','Existing VBS mode was not loaded');
    changeEditLaunch({mode:'direct'});
    await joinEditedGameToSteam();check(commitCalls().length===0,'Unsaved mode reached Steam');closeSteamAddNotice();
    fixtureSnapshot.manualOverrides.items[exe]={launchConfiguration:{mode:'direct'}};
    try {
      check(await persistEditDraft(),'Library save failed');await frames();
      const save=fixtureCalls.find(call=>call.command==='saveEditor');
      check(save?.arguments.operations.some(op=>op.command==='saveLaunchConfiguration'&&op.arguments.configuration.mode==='direct'),'Ordinary mode was not saved');
      check($('#edit-launch-mode').value==='direct'&&!editDraft.launchEdits&&commitCalls().length===0,'Save changed Steam or failed to rebase the mode');
      const ready=$('#edit-add-to-steam');
      check(ready.textContent.includes('更新 Steam 启动')&&!ready.disabled,'Explicit update action unavailable');
      const feedback=$('#edit-launch-feedback').textContent;
      fixtureSnapshot.steamAddCompletion={mode:'steam-library-add-commit',committed:true,items:[{...row,launchUpdated:true}]};
      await openSingleCommit();await waitFor(()=>steamCommitTasks.get(key)?.phase==='completed');await frames();
      check(commitCalls()[0].arguments.updateVbsLaunch===true,'Explicit update did not use the existing transaction');
      check($('#steam-add-notice-message').textContent.includes('已更新原 Steam 卡片'),'Verified launch update was not reported');
      check(feedback.includes('Steam 卡片未改写'),'Library save did not say that the Steam card was untouched');
      check(!$('#edit-launch-feedback').textContent.includes('未改写'),'Verified update left stale save feedback in the editor');
    } finally {
      delete fixtureSnapshot.manualOverrides.items[exe];delete fixtureSnapshot.steamAddCompletion;
    }
  `);
  await evaluate(`
    window.openJoinedArtworkFixture=async()=>{
      await resetCommitFixture();
      const key=keyFor(selectedActionGame);closeEdit();
      fixtureSnapshot.steamPlan.items.forEach(row=>{row.status='already-in-steam';});
      snapshot=normalizeWorkspaceSnapshot(structuredClone(fixtureSnapshot),snapshot);
      activeTab='in-steam';renderLibrary();
      openEdit(mergedGames().find(row=>keyFor(row)===key));await frames();fixtureCalls.length=0;
      return key;
    };
  `);
  await test('joined-editor-repairs-only-its-original-card', `
    const key=await openJoinedArtworkFixture(),initial=keyFor(selectedActionGame),dir=selectedActionGame.gameDirectory;
    const button=$('#edit-repair-steam-artwork');check(button&&!button.classList.contains('hidden'),'Joined editor lacks a single-card Steam artwork action');
    const checkAction=()=>fixtureCalls.filter(call=>call.command==='refreshSteamArtwork');
    const fixtureExec=normalizedUiPath(selectedActionGame.primaryExecutable),source='G:/Fixture/new.png';
    artworkDraft.cover={state:'set',asset:{path:source}};artworkDraft.dirty.cover=true;confirmArtworkDraft();
    check(button.disabled&&checkAction().length===0,'Unsaved artwork can enter Steam repair');
    fixtureSnapshot.manualOverrides.items[fixtureExec]={cover:{file:source},artworkProtection:{cover:'manual'}};
    try {
      check(await persistEditDraft(),'Library artwork save failed');await frames();
      check(!button.disabled&&checkAction().length===0&&!fixtureCalls.some(call=>call.command==='commit'),'Saving artwork secretly wrote Steam or did not re-enable repair');
      selectedSteamDirectories=new Set(mergedGames().filter(row=>keyFor(row)!==key).map(keyFor));
      focusWorkspaceElement($('#artwork-long-preview'));lastDirectionalActionAt=0;routeWorkspaceAction('navigate-down');
      check(document.activeElement===button,'Steam artwork repair is not reachable by the controller');
      const rect=button.getBoundingClientRect(),panel=button.closest('.edit-artwork-panel').getBoundingClientRect();
      check(rect.width>0&&rect.height>0&&rect.left>=panel.left&&rect.right<=panel.right&&rect.top>=0&&rect.bottom<=innerHeight,'Steam artwork repair is clipped');
      routeWorkspaceAction('accept');await waitFor(()=>checkAction().length===1&&$('#busy').classList.contains('hidden'));
      const request=checkAction()[0].arguments;
      check(request.selectionMode==='all-steam-accounts'&&request.selectedGameDirectories.length===1&&request.selectedGameDirectories[0]===dir&&!request.forceCloseSteam,'Repair used another card, bulk selection or forced closure');
      check(editorOpen()&&keyFor(selectedActionGame)===initial,'Single-card repair changed editor context');
    } finally {delete fixtureSnapshot.manualOverrides.items[fixtureExec];}
  `);
  await command('Emulation.setDeviceMetricsOverride',{width:1280,height:720,deviceScaleFactor:1,mobile:false});
  await test('joined-editor-artwork-repair-rejects-stale-close-confirmation', `
    const key=await openJoinedArtworkFixture();
    const button=$('#edit-repair-steam-artwork');check(button&&!button.disabled,'Joined editor lacks a usable Steam artwork action');
    focusWorkspaceElement($('#artwork-long-preview'));lastDirectionalActionAt=0;routeWorkspaceAction('navigate-down');await frames();
    const rect=button.getBoundingClientRect(),panel=button.closest('.edit-artwork-panel').getBoundingClientRect();
    check(document.activeElement===button&&rect.width>0&&rect.left>=panel.left&&rect.right<=panel.right&&rect.top>=0&&rect.bottom<=innerHeight,'Handheld repair action is clipped or unreachable');
    window.fixtureArtworkPreflight={steamRunning:true,steamClient:{pid:77,birth:'115',powerGeneration:'1',powerSequence:'0',powerSerial:'0'}};
    try {
      button.click();await waitFor(()=>!$('#confirm-modal').classList.contains('hidden'));closeConfirm(false);await frames();
      check(!fixtureCalls.some(call=>call.command==='refreshSteamArtwork'),'Cancelled confirmation wrote Steam');
      button.click();await waitFor(()=>!$('#confirm-modal').classList.contains('hidden'));
      openEdit(mergedGames().find(row=>keyFor(row)!==key));closeConfirm(true);await frames();
      check(!fixtureCalls.some(call=>call.command==='refreshSteamArtwork'||call.command==='closeSteam'),'Stale confirmation closed Steam or repaired a different card');
    } finally {delete window.fixtureArtworkPreflight;}
  `);
  await command('Emulation.setDeviceMetricsOverride',{width:1920,height:1080,deviceScaleFactor:1,mobile:false});
  await evaluate(`
    window.seedEditorRepairPreview=async()=>{
      const key=await openJoinedArtworkFixture();closeEdit();
      const row=fixtureSnapshot.steamPlan.items.find(row=>keyFor(row)===key);
      const image=color=>'data:image/svg+xml,'+encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" width="90" height="120"><rect width="90" height="120" fill="'+color+'"/></svg>');
      Object.assign(row,{shortAppId:'3221225473',longAppId:'13835058059610685440',gridDirectory:'G:/FixtureSteam/userdata/1000/config/grid',artworkPreview:{tall:{url:image('blue')},long:{url:image('red')}}});
      snapshot=normalizeWorkspaceSnapshot(structuredClone(fixtureSnapshot),snapshot);
      openEdit(mergedGames().find(game=>keyFor(game)===key));await frames();
      const original=row.artworkPreview.tall.url;
      row.artworkPreview={tall:{url:image('green')},long:{url:image('gold')}};
      fixtureSnapshot.steamArtworkFilesRepaired=1;
      fixtureSnapshot.steamArtworkViewStale=false;
      return {key,row,original,expected:row.artworkPreview.tall.url,image};
    };
  `);
  await test('joined-editor-repair-refreshes-static-preview-without-replacing-draft', `
    const {key,expected,image}=await seedEditorRepairPreview();
    const draft=editDraft,button=$('#edit-repair-steam-artwork');
    $('#identity-name').value='Unsaved game title';syncIdentityDraft('name');
    window.fixtureHoldArtworkRepair=true;
    focusWorkspaceElement(button);button.click();await waitFor(()=>!!window.fixtureReleaseArtworkRepair);
    const chosen={state:'set',asset:{url:image('purple')}};
    artworkDraft.long=chosen;artworkDraft.dirty.long=true;
    try {
      window.fixtureReleaseArtworkRepair();await waitFor(()=>!steamArtworkRequestInFlight&&$('#busy').classList.contains('hidden'));
      check(artworkDraft.cover.asset?.url===expected&&$('#artwork-cover-preview img')?.getAttribute('src')===expected,'Verified repair left the original editor preview stale');
      check(editDraft===draft&&keyFor(selectedActionGame)===key&&draftName()==='Unsaved game title'&&artworkDraft.long===chosen&&artworkDraft.dirty.long,'Static repair replaced the draft or a newer per-slot selection');
      check(!fixtureCalls.some(call=>call.command==='saveEditor'),'Repair saved editor fields');
    } finally {delete window.fixtureHoldArtworkRepair;delete window.fixtureReleaseArtworkRepair;}
  `);
  await test('joined-editor-repair-refuses-unverified-or-cross-account-preview', `
    const {row,original}=await seedEditorRepairPreview();
    const button=$('#edit-repair-steam-artwork');
    const run=async()=>{button.click();await waitFor(()=>!steamArtworkRequestInFlight&&$('#busy').classList.contains('hidden'));check(artworkDraft.cover.asset?.url===original,'Unverified result replaced the original editor preview');};
    fixtureCalls.length=0;fixtureSnapshot.steamArtworkViewStale=true;await run();
    fixtureSnapshot.steamArtworkViewStale=false;row.gridDirectory='G:/FixtureSteam/userdata/1001/config/grid';await run();
    check(fixtureCalls.filter(call=>call.command==='refreshSteamArtwork').length===2,'Skipped an original repair result');
  `);
  await test('single-card-new-account-import-distinguishes-untouched-old-card', `
    await resetCommitFixture();const key=keyFor(selectedActionGame),row=fixtureSnapshot.steamPlan.items.find(game=>keyFor(game)===key);
    const account=(accountId,committed,fields)=>({mode:'steam-library-add-commit',accountId,committed,
      ...(!committed?{reason:'nothing-ready-to-add'}:{}),items:[{...row,...fields}]});
    fixtureSnapshot.steamAddCompletion={mode:'steam-library-add-commit-all-accounts',accountResults:[
      account('1000',true,{status:'added-to-steam'}),account('1001',false,{status:'already-in-steam',steamMembershipSource:'game-launch-script'})]};
    try {
      await openSingleCommit();await waitFor(()=>steamCommitTasks.get(key)?.phase==='completed');await frames();
      check(!commitCalls()[0].arguments.updateVbsLaunch,'New import enabled conversion of a historical card');
      const feedback=$('#steam-add-notice-message').textContent;
      check(feedback.includes('已加入 Steam')&&feedback.includes('新加入 1 个账户')&&feedback.includes('保留原卡 1 个账户'),'Mixed accounts hid the untouched card');
    } finally {delete fixtureSnapshot.steamAddCompletion;}
  `);
  await test('vbs-file-actions-use-saved-launch-draft', `
    await resetCommitFixture();const exe=normalizedUiPath(selectedActionGame.primaryExecutable),key=keyFor(selectedActionGame);
    const path='G:/Fixture/Games/Other/StartGame.vbs',source='G:/Fixture/Games/Other/Start.bat';
    window.fixtureLaunchReplies={launchFileState:{fileState:'present',script:path},updateLaunchFile:{fileState:'present',script:path,updated:true,backup:path+'.bak'}};
    const updates=()=>fixtureCalls.filter(call=>call.command==='updateLaunchFile');
    try {
      changeEditLaunch({mode:'auto',source});
      await openEditLaunchMenu();
      if(editLaunchMenu){
        const update=performEditLaunchFile('updateLaunchFile');
        if(!$('#confirm-modal').classList.contains('hidden'))closeConfirm(true);
        await update;closeEditLaunchMenu();
      }
      check(updates().length===0&&$('#edit-launch-feedback').textContent.includes('请先保存启动配置'),'Unsaved launch draft rewrote the VBS referenced by Steam');
      fixtureSnapshot.manualOverrides.items[exe]={launchConfiguration:{mode:'auto',source}};
      check(await persistEditDraft(),'Configuration save failed');await frames();
      check(!editDraft.launchEdits&&keyFor(selectedActionGame)===key,'Saved configuration was not rebased');
      await openEditLaunchMenu();
      const declined=performEditLaunchFile('updateLaunchFile');closeConfirm(false);await declined;
      check(updates().length===0,'Cancelling the explicit update still wrote a script');
      const accepted=performEditLaunchFile('updateLaunchFile');closeConfirm(true);await accepted;
      check(updates().length===1&&updates()[0].arguments.configuration.source===source,'Saved explicit update did not use this card and saved entry');
      check($('#edit-launch-status').textContent.includes('已更新启动文件'),'Completion did not show script result');

      const reads=()=>fixtureCalls.filter(call=>call.command==='launchFileState');
      const actions=()=>fixtureCalls.filter(call=>['editLaunchFile','createLaunchFile','updateLaunchFile','deleteLaunchFile'].includes(call.command));
      const beforeRead=reads().length,beforeAction=actions().length;
      fixtureLaunchReplies.launchFileState={fileState:'missing',script:path};
      fixtureFailures.editLaunchFile='启动文件不存在或不可用';
      $('#edit-launch-main').focus();
      await performEditLaunchFile('editLaunchFile');
      check(reads().length===beforeRead+1&&reads().at(-1).arguments.executable===selectedActionGame.primaryExecutable,'Failed Edit did not recheck the same saved game exactly once');
      check(actions().length===beforeAction+1&&commitCalls().length===0,'Recovery retried a mutation or reached Steam');
      const main=$('#edit-launch-main');
      check(main.textContent==='新建'&&!main.disabled&&$('#edit-launch-update').disabled&&$('#edit-launch-delete').disabled,'Disappeared VBS still showed stale Edit/Update/Delete actions');
      check($('#edit-launch-status').textContent.includes('启动文件不存在或不可用')&&document.activeElement===main,'Recovery suppressed the failure or lost controller focus');

      fixtureFailures.createLaunchFile='测试：生成失败';
      fixtureFailures.launchFileState='测试：读取失败';
      await performEditLaunchFile('createLaunchFile');
      check(reads().length===beforeRead+2&&actions().length===beforeAction+2&&commitCalls().length===0,'Unconfirmed state caused retries or Steam writes');
      check(main.disabled&&$('#edit-launch-update').disabled&&$('#edit-launch-delete').disabled&&document.activeElement===$('#edit-launch-cancel'),'Unknown file state kept stale actions or stranded focus');
      check($('#edit-launch-status').textContent.includes('测试：生成失败'),'Failed state check hid the original error');
    }finally{
      closeEditLaunchMenu(false);delete window.fixtureLaunchReplies;delete fixtureSnapshot.manualOverrides.items[exe];
      for(const command of ['editLaunchFile','createLaunchFile','launchFileState'])delete fixtureFailures[command];
    }
  `);
  await test('vbs-file-confirmation-rejects-changed-library-root', `
    await resetCommitFixture();const key=keyFor(selectedActionGame),exe=normalizedUiPath(selectedActionGame.primaryExecutable);
    closeEdit();fixtureSnapshot.manualOverrides.items[exe]={launchConfiguration:{mode:'auto'}};
    applyLibrarySnapshot(structuredClone(fixtureSnapshot));openEdit(mergedGames().find(row=>keyFor(row)===key));
    window.fixtureLaunchReplies={launchFileState:{fileState:'present',script:'G:/Fixture/StartGame.vbs'},updateLaunchFile:{fileState:'present',updated:true}};
    const original=snapshot;
    try {
      await openEditLaunchMenu();const accepted=performEditLaunchFile('updateLaunchFile');
      check(!$('#confirm-modal').classList.contains('hidden'),'Saved file update skipped confirmation');
      snapshot={...snapshot,dataRoot:'G:/Fixture/ReplacedRoot'};
      closeConfirm(true);await accepted;
      check(!fixtureCalls.some(call=>call.command==='updateLaunchFile'),'Old confirmation reached a different library root');
    }finally{snapshot=original;closeEditLaunchMenu(false);delete window.fixtureLaunchReplies;delete fixtureSnapshot.manualOverrides.items[exe];}
  `);
  await test('steam-card-conflict-blocks-editor-commit-until-refreshed', `
    await resetCommitFixture();const key=keyFor(selectedActionGame),draft=editDraft;
    const original=structuredClone(snapshot),conflict=structuredClone(original);
    const item=conflict.steamPlan.items.find(row=>keyFor(row)===key);item.status='shortcut-id-conflict';
    applyLibrarySnapshot(conflict);await frames();
    check(editorOpen()&&editDraft===draft&&$('#edit-add-to-steam').disabled&&$('#edit-add-to-steam').textContent.includes('冲突'),'New Steam conflict did not disable the open editor action');
    await joinEditedGameToSteam();check(commitCalls().length===0,'Conflict reached the commit bridge');
    closeSteamAddNotice();
    applyLibrarySnapshot(original);await frames();
    check(editorOpen()&&editDraft===draft&&!$('#edit-add-to-steam').disabled,'Fresh clear result did not reenable the same draft');
    await openSingleCommit();await waitFor(()=>steamCommitTasks.get(key)?.phase==='completed');
    check(commitCalls().length===1&&commitCalls()[0].arguments.selectedGameDirectories[0]===key,'Recovery did not restore the original single-card action');
  `);
  await test('return-from-single-card-keeps-batch-selection-independent', `
    await resetCommitFixture();window.fixtureHoldCommit=true;const key=keyFor(selectedActionGame);await openSingleCommit();closeEdit();window.fixtureCommitReleases.shift()();await waitFor(()=>steamCommitTasks.get(key)?.phase==='completed');window.fixtureHoldCommit=false;fixtureCalls.length=0;
    if(!$('#steam-add-notice-modal').classList.contains('hidden'))closeSteamAddNotice();
    const expected=selectedCommitDirectories();check(expected.length===3,'Batch fixture missing');
    $('#commit-button').click();await waitFor(()=>!$('#steam-commit-modal').classList.contains('hidden'));
    check($('#commit-summary').textContent.includes('3 个'),'Batch kept single-card count');
    $('#steam-commit-confirm').click();await waitFor(()=>commitCalls().length===1&&$('#busy').classList.contains('hidden'));
    check(JSON.stringify(commitCalls()[0].arguments.selectedGameDirectories)===JSON.stringify(expected),'Single target leaked into batch');
    check(activeWorkspaceModal()?.id==='steam-add-notice-modal'&&$('#steam-add-notice-message').textContent.includes('已加入 Steam'),'Batch result did not use the same popup');
  `);
  await test('running-Steam-shows-popup-and-B-returns-to-editor', `
    await resetCommitFixture();fixtureSteamRunning=true;const key=keyFor(selectedActionGame);await openSingleCommit();await waitFor(()=>steamCommitTasks.get(key)?.phase==='failed');await frames();
    check(activeWorkspaceModal()?.id==='steam-add-notice-modal'&&$('#steam-add-notice-message').textContent.includes('fixture Steam')&&$('#edit-status').textContent==='加入失败','Running Steam did not produce a popup with a short header');
    check(editorOpen()&&!fixtureCalls.some(call=>call.command==='closeSteam'),'Single-card failure silently closed Steam or the editor');
    fixtureEmit({event:'steam-commit-progress',directories:[key],phase:'failed',error:'Late native details '.repeat(30)});await frames();
    check($('#edit-status').textContent==='加入失败'&&$('#steam-add-notice-message').textContent.includes('fixture Steam'),'Late native progress restored verbose header or duplicated feedback');
    controllerBackLastAt=0;routeWorkspaceAction('back');await frames();check(editorOpen()&&$('#steam-add-notice-modal').classList.contains('hidden')&&document.activeElement===$('#edit-add-to-steam')&&commitCalls().length===1,'B failed to return to the editor without retry '+JSON.stringify({editor:editorOpen(),popup:!$('#steam-add-notice-modal').classList.contains('hidden'),focus:document.activeElement?.id,commits:commitCalls().length,queued:steamAddNotices.length}));
    routeWorkspaceAction('back');await frames();check(!editorOpen(),'Next B did not return to the library');
  `);
  if(!selectedSidebarCases||selectedSidebarCases.includes('running-Steam-shows-popup-and-B-returns-to-editor')) {
    await evaluate(`resetCommitFixture().then(()=>{fixtureFailures.commit='Pointer dismissal';return joinEditedGameToSteam();})`);
    const point=await evaluate(`(()=>{const r=$('#steam-add-notice-confirm').getBoundingClientRect();return{x:r.left+r.width/2,y:r.top+r.height/2};})()`);
    await command('Input.dispatchMouseEvent',{type:'mousePressed',button:'left',clickCount:1,...point});
    await command('Input.dispatchMouseEvent',{type:'mouseReleased',button:'left',clickCount:1,...point});
    await evaluate(`check($('#steam-add-notice-modal').classList.contains('hidden')&&editorOpen(),'Pointer did not return one level');showSteamAddNotice('Escape dismissal');`);
    await command('Input.dispatchKeyEvent',{type:'keyDown',key:'Escape',code:'Escape',windowsVirtualKeyCode:27});
    await command('Input.dispatchKeyEvent',{type:'keyUp',key:'Escape',code:'Escape',windowsVirtualKeyCode:27});
    await evaluate(`check($('#steam-add-notice-modal').classList.contains('hidden')&&editorOpen(),'Physical Escape closed the editor');showSteamAddNotice('触摸返回检查');`);
    await command('Emulation.setTouchEmulationEnabled',{enabled:true});
    const touch=await evaluate(`(()=>{const r=$('#steam-add-notice-confirm').getBoundingClientRect();return{x:r.left+r.width/2,y:r.top+r.height/2};})()`);
    await command('Input.dispatchTouchEvent',{type:'touchStart',touchPoints:[{...touch,id:1,radiusX:4,radiusY:4,force:1}]});
    await command('Input.dispatchTouchEvent',{type:'touchEnd',touchPoints:[]});
    await evaluate(`check($('#steam-add-notice-modal').classList.contains('hidden')&&editorOpen(),'Touch did not return one level');`);
    await command('Emulation.setTouchEmulationEnabled',{enabled:false});
  }
  await test('explicit-retry-after-Steam-stops-keeps-one-game', `
    await resetCommitFixture();fixtureSteamRunning=true;const key=keyFor(selectedActionGame);await openSingleCommit();await waitFor(()=>steamCommitTasks.get(key)?.phase==='failed');
    check(!fixtureCalls.some(call=>call.command==='closeSteam'),'Failure silently closed Steam');closeSteamAddNotice();fixtureSteamRunning=false;fixtureCalls.length=0;
    await openSingleCommit();await waitFor(()=>steamCommitTasks.get(key)?.phase==='completed');
    check(JSON.stringify(commitCalls()[0].arguments.selectedGameDirectories)===JSON.stringify([key]),'Explicit retry broadened target');
  `);
  await test('Steam-starts-after-queue-admission-and-is-never-silently-closed', `
    await resetCommitFixture();window.fixtureHoldCommit=true;const key=keyFor(selectedActionGame);await openSingleCommit();fixtureSteamRunning=true;window.fixtureCommitReleases.shift()();await waitFor(()=>steamCommitTasks.get(key)?.phase==='failed');
    check(commitCalls().length===1&&!fixtureCalls.some(call=>call.command==='closeSteam'),'Late Steam start retried or implicitly closed it');window.fixtureHoldCommit=false;
  `);
  await test('admission-and-batch-preflight-failures-never-expand-selection', `
    await resetCommitFixture();const key=keyFor(selectedActionGame);fixtureFailures.commit='fixture admission failure';await openSingleCommit();await waitFor(()=>steamCommitTasks.get(key)?.phase==='failed');
    check(JSON.stringify(commitCalls()[0].arguments.selectedGameDirectories)===JSON.stringify([key])&&editorOpen(),'Native admission failure expanded the single target or discarded editor');
    await resetCommitFixture();closeEdit();fixtureFailures.steamCommitPreflight='fixture preflight failure';$('#commit-button').click();await waitFor(()=>!steamCommitRequestInFlight&&fixtureCalls.some(call=>call.command==='steamCommitPreflight'));
    check(commitCalls().length===0&&$('#steam-commit-modal').classList.contains('hidden')&&activeWorkspaceModal()?.id==='steam-add-notice-modal'&&$('#steam-add-notice-message').textContent==='fixture preflight failure','Batch preflight failure committed stale scope or missed shared popup');
  `);
  await test('commit-failure-does-not-retry-or-expand-selection', `
    await resetCommitFixture();const key=keyFor(selectedActionGame);fixtureFailures.commit='fixture write failure';await openSingleCommit();await waitFor(()=>steamCommitTasks.get(key)?.phase==='failed');await frames();
    check(commitCalls().length===1&&JSON.stringify(commitCalls()[0].arguments.selectedGameDirectories)===JSON.stringify([key]),'Failed write retried or broadened target');
    check(editorOpen()&&!$('#edit-add-to-steam').disabled&&$('#edit-status').textContent==='加入失败'&&$('#steam-add-notice-message').textContent.includes('fixture write failure'),'Write failure discarded editor or did not show popup');fixtureFailures={};
  `);
  await test('native-and-media-only-editors-cannot-create-shortcuts', `
    await resetCommitFixture();closeEdit();openEdit({...structuredClone(fixtureGame),steamNative:true});
    check($('#edit-add-to-steam').disabled && !editModalFocusItems().includes($('#edit-add-to-steam')),'Native action enabled');
    closeEdit();openEdit({mediaOnly:true,mediaKey:'fixture',directoryName:'Wallpaper',steam:{},override:{}});
    check($('#edit-add-to-steam').disabled && !editModalFocusItems().includes($('#edit-add-to-steam')),'Media-only action enabled');
    closeEdit();await resetCommitFixture();closeEdit();
  `);
  await test('cancel-removing-last-scan-root-keeps-current-scope', `
    closeEdit();renderEntry();const button=document.querySelector('.root-row button');check(button && !button.disabled,'Last root disabled');
    const before=fixtureCalls.filter(call=>call.command==='removeRoot').length;
    button.click();$('#confirm-cancel').click();await frames();
    check(snapshot.config.roots.length===1 && fixtureCalls.filter(call=>call.command==='removeRoot').length===before,'Cancel removed root');`);
  await test('last-scan-root-remove-button-is-enabled-and-confirmation-submits', `
    closeEdit();renderEntry();const button=document.querySelector('#roots-list .root-row button') || document.querySelector('.root-row button');
    check(button && !button.disabled,'Last scan root cannot be removed');button.click();
    check(!$('#confirm-modal').classList.contains('hidden'),'Root removal skipped confirmation');
    $('#confirm-confirm').click();await new Promise(resolve=>setTimeout(resolve,60));
    check(fixtureCalls.some(call=>call.command==='removeRoot' && call.arguments.path==='G:/Fixture/Games'),'Wrong root removal request');
    check(snapshot.config.roots.length===0 && document.querySelectorAll('.root-row').length===0,'Removed root stayed in UI');`);
  for(const [width,height] of [[1280,720],[1280,800],[1920,1080]]){
    await command('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor:1,mobile:false});
    await evaluate('prepareSidebarLayout()');
    const image=await command('Page.captureScreenshot',{format:'png',captureBeyondViewport:false});
    fs.writeFileSync(path.join(root,'sidebar-grid-'+width+'x'+height+'.png'),Buffer.from(image.data,'base64'));
  }
  if(!selectedSidebarCases||selectedSidebarCases.includes('running-Steam-shows-popup-and-B-returns-to-editor')) {
    await command('Emulation.setDeviceMetricsOverride',{width:1280,height:720,deviceScaleFactor:1,mobile:false});
    await evaluate(`resetCommitFixture().then(()=>{fixtureFailures.commit='请先关闭 Steam，再点击卡片中的加入 Steam 重试。';return joinEditedGameToSteam();}).then(()=>frames())`);
    const image=await command('Page.captureScreenshot',{format:'png',captureBeyondViewport:false});
    fs.writeFileSync(path.join(root,'steam-add-notice-1280x720.png'),Buffer.from(image.data,'base64'));
    await evaluate('closeSteamAddNotice()');
  }


  if(!selectedSidebarCases||selectedSidebarCases.includes('cards-prefer-readable-names-over-executable-placeholders')){
    try{
      await test('cards-prefer-readable-names-over-executable-placeholders', `
        await resetCommitFixture();closeEdit();
        window.fixtureNameRestore={snapshot,fixtureSnapshot,activeTab};window.fixtureNameRows=[];
        const slash=String.fromCharCode(92),labels=['Game.exe','START.BAT','helper.dll','library.bin','launch.cmd','setup123','unins000','launcher7','start3','game42','Portal','Game Dev Tycoon','manual Game.exe'];
        const games=labels.map((name,index)=>{const directory=['G:','Fixture','Naming',String(index)].join(slash);return {...structuredClone(fixtureGame),gameDirectory:directory,directoryName:'Readable title '+index,primaryExecutable:directory+slash+'Game.exe',primaryProductName:'',primaryFileDescription:'',status:'ready',contentType:'game'};});
        const identities={},overrides={};games.forEach((game,index)=>{identities[normalizedUiPath(game.primaryExecutable)]={executable:game.primaryExecutable,formalName:labels[index]};});
        overrides[normalizedUiPath(games[12].primaryExecutable)]={name:'Game.exe'};
        fixtureSnapshot={...fixtureSnapshot,config:{...fixtureSnapshot.config,manualBuckets:{}},library:{games},steamPlan:{items:games.map((game,index)=>({...game,status:'already-in-steam',formalName:labels[index]})),summary:{}},manualOverrides:{items:overrides},resolvedIdentities:identities};
        snapshot=normalizeWorkspaceSnapshot(structuredClone(fixtureSnapshot),snapshot);activeTab='in-steam';librarySidebarState().query='';renderLibrary();await frames();
        const cards=libraryCards();check(cards.length===labels.length,'Naming fixture lost authoritative installation cards');
        for(let index=0;index<games.length;index++){
          const card=cards.find(item=>item.dataset.directory===normalizedUiPath(games[index].gameDirectory));
          const expected=index<10?games[index].directoryName:index===12?'Game.exe':labels[index];
          const actual=card?.querySelector('.game-card-name')?.textContent;
          const accessible=card?.getAttribute('aria-label')?.startsWith(expected+'，')===true;
          fixtureNameRows.push({variant:labels[index],expected,actual,accessible,passed:actual===expected&&accessible});
        }
        check(fixtureNameRows.every(row=>row.passed),'Executable placeholder title hid readable card name');
        window.fixtureBindingRows=[];
        for(const kind of ['conflicting-cache','metadata-only-cache','same-id-cache','manual-title','native-conflicting-cache']){
          const game={...structuredClone(fixtureGame),gameDirectory:'G:/Fixture/Binding/'+kind,primaryExecutable:'G:/Fixture/Binding/'+kind+'/game.exe',directoryName:'Bound directory',primaryProductName:'Bound identity',status:'ready'};
          const row={...game,status:'already-in-steam',steamStoreAppId:1000,storefrontAppId:1000,appId:1000,identityStatus:'verified',steamVerificationStatus:'steam-verified',formalName:'Bound identity'};
          const key=normalizedUiPath(game.primaryExecutable),identities={},overrides={};
          identities[key]={steamAppId:kind==='same-id-cache'?1000:2000,appId:kind==='same-id-cache'?1000:2000,formalName:kind==='same-id-cache'?'Same identity refreshed title':'Foreign cached title'};
          if(kind==='metadata-only-cache')identities[key]={igdbId:77,formalName:'Auxiliary candidate title'};
          if(kind==='manual-title')overrides[key]={name:'User title'};
          if(kind==='native-conflicting-cache'){game.steamNative=true;game.nativeSteamAppId=1000;row.steamNative=true;row.nativeSteamAppId=1000;}
          fixtureSnapshot={...fixtureSnapshot,library:{games:[game]},steamPlan:{items:[row],summary:{}},manualOverrides:{items:overrides},resolvedIdentities:identities,gameMedia:{entries:{}}};
          const before=JSON.stringify(identities);
          applyLibrarySnapshot(structuredClone(fixtureSnapshot));activeTab=kind==='native-conflicting-cache'?'native-steam':'in-steam';librarySidebarState().query='';renderLibrary();await frames();
          const merged=mergedGames()[0],card=libraryCards()[0],expected=kind==='same-id-cache'?'Same identity refreshed title':kind==='manual-title'?'User title':'Bound identity';
          const observed={variant:kind,expected,actual:card?.querySelector('.game-card-name')?.textContent,accessible:card?.getAttribute('aria-label')?.startsWith(expected+'，')===true,appId:confirmedMediaAppId(merged),cacheUnchanged:JSON.stringify(snapshot.resolvedIdentities)===before,cards:libraryCards().length};
          observed.passed=observed.actual===expected&&observed.accessible&&observed.appId==='1000'&&observed.cacheUnchanged&&observed.cards===1;fixtureBindingRows.push(observed);
        }
        check(fixtureBindingRows.every(row=>row.passed),'Browser card mixed protected Steam identity/cache: '+JSON.stringify(fixtureBindingRows));
      `);
    }finally{
      const observed=await evaluate('window.fixtureNameRows||[]');
      fs.writeFileSync(path.join(root,'card-name-observed.json'),JSON.stringify({rows:observed,bindingRows:await evaluate('window.fixtureBindingRows||[]')},null,2),{flag:'wx'});
      const screenshot=await command('Page.captureScreenshot',{format:'png',captureBeyondViewport:false});
      fs.writeFileSync(path.join(root,'card-name-fallback-1920x1080.png'),Buffer.from(screenshot.data,'base64'),{flag:'wx'});
      await evaluate(`if(window.fixtureNameRestore){snapshot=fixtureNameRestore.snapshot;fixtureSnapshot=fixtureNameRestore.fixtureSnapshot;activeTab=fixtureNameRestore.activeTab;renderLibrary();delete window.fixtureNameRestore;}`);
    }
  }

  if(!selectedSidebarCases||selectedSidebarCases.some(name=>name.startsWith('steam-membership-'))){
  await evaluate(`
    window.membershipFocused=true;window.membershipHidden=false;
    Object.defineProperty(document,'hasFocus',{configurable:true,value:()=>membershipFocused});
    Object.defineProperty(document,'hidden',{configurable:true,get:()=>membershipHidden});
    window.membershipWait=ms=>new Promise(resolve=>setTimeout(resolve,ms));
    window.resetMembershipReturn=async()=>{
      fixtureHoldPlan=false;fixturePlanReleases=[];delete fixtureFailures.plan;
      if(silentRefreshInFlight)await silentRefreshInFlight;
      if(editorOpen())closeEdit();if(refreshAfterScrapeTimer)clearTimeout(refreshAfterScrapeTimer);refreshAfterScrapeTimer=0;
      silentRefreshPending=false;silentRefreshAdvisory=false;silentRefreshPreserveWaiting=false;silentRefreshRetryAt=0;silentRenderPending=false;silentRefreshClosed=false;
      if(typeof silentRefreshSteamPlan!=='undefined')silentRefreshSteamPlan=false;
      membershipFocused=true;membershipHidden=false;steamRuntimeWatchClosed=false;steamRuntimeWatchForeground=true;steamRuntimeLastCheck=Date.now();
      fixtureSnapshot.dataRoot='G:/Fixture/MembershipLibrary';fixtureSnapshot.library.games=[structuredClone(fixtureGame)];
      fixtureSnapshot.steamPlan={items:[{...structuredClone(fixtureGame),status:'ready-to-add'}],summary:{readyToAdd:1,alreadyInSteam:0}};
      applyLibrarySnapshot(structuredClone(fixtureSnapshot));await frames();
      fixtureSnapshot.steamPlan={items:[{...structuredClone(fixtureGame),status:'already-in-steam',shortAppId:'123'}],summary:{readyToAdd:0,alreadyInSteam:1}};
      membershipFocused=false;window.dispatchEvent(new Event('blur'));fixtureCalls.length=0;
    };
    window.returnMembershipForeground=()=>{membershipFocused=true;membershipHidden=false;window.dispatchEvent(new Event('focus'));};
    window.membershipPlanCalls=()=>fixtureCalls.filter(call=>call.command==='plan');
  `);
  await test('steam-membership-return-refreshes-once', `
    await resetMembershipReturn();returnMembershipForeground();for(let i=0;i<100;i++)window.dispatchEvent(new Event('focus'));
    await membershipWait(450);check(membershipPlanCalls().length===1,'return to library did not request exactly one membership plan');
    check(category(mergedGames()[0])==='in-steam','fresh membership did not update card category');
    await membershipWait(3300);check(membershipPlanCalls().length===1,'runtime interval repeated the membership plan');
    membershipFocused=false;window.dispatchEvent(new Event('blur'));await membershipWait(300);check(!steamRuntimeWatchTimer&&!refreshAfterScrapeTimer,'hidden owner retained refresh timer');`);
  await test('steam-membership-hidden-return-waits-for-foreground', `
    await resetMembershipReturn();membershipHidden=true;membershipFocused=false;for(let i=0;i<100;i++){window.dispatchEvent(new Event('focus'));document.dispatchEvent(new Event('visibilitychange'));}
    await membershipWait(350);check(membershipPlanCalls().length===0,'hidden page queried Steam membership');
    membershipFocused=true;membershipHidden=false;document.dispatchEvent(new Event('visibilitychange'));await membershipWait(400);
    check(membershipPlanCalls().length===1&&category(mergedGames()[0])==='in-steam','visible return did not consume pending membership');`);
  await test('steam-membership-slow-return-coalesces-followup', `
    await resetMembershipReturn();fixtureHoldPlan=true;returnMembershipForeground();await membershipWait(350);
    check(membershipPlanCalls().length===1&&fixturePlanReleases.length===1,'slow membership read was not held');
    for(let i=0;i<50;i++){membershipFocused=false;window.dispatchEvent(new Event('blur'));returnMembershipForeground();}
    await membershipWait(300);check(membershipPlanCalls().length===1,'focus storm dispatched overlapping membership reads');
    fixtureHoldPlan=false;fixturePlanReleases.splice(0).forEach(fn=>fn());await membershipWait(600);
    check(membershipPlanCalls().length===2&&!silentRefreshInFlight,'coalesced return did not finish one followup');`);
  await test('steam-membership-old-root-return-is-discarded', `
    await resetMembershipReturn();fixtureHoldPlan=true;returnMembershipForeground();await membershipWait(350);check(fixturePlanReleases.length===1,'old-root membership was not held');
    const newer=structuredClone(snapshot);newer.dataRoot='G:/Fixture/NewMembershipLibrary';newer.library.games[0].directoryName='New root retained';applyLibrarySnapshot(newer);
    fixtureHoldPlan=false;fixturePlanReleases.splice(0).forEach(fn=>fn());await membershipWait(350);
    check(snapshot.dataRoot==='G:/Fixture/NewMembershipLibrary'&&snapshot.library.games[0].directoryName==='New root retained','old membership reply replaced new root');`);
  await test('steam-membership-failed-return-keeps-original-retry', `
    await resetMembershipReturn();fixtureFailures.plan='owned membership read failed';returnMembershipForeground();await membershipWait(400);
    check(membershipPlanCalls().length===1&&silentRefreshPending,'failed return did not retain pending membership');
    check(silentRefreshRetryAt-performance.now()>19000,'membership error shortened original retry window');
    delete fixtureFailures.plan;window.membershipRetryObservedAt=performance.now();`);
  if(!selectedSidebarCases||selectedSidebarCases.includes('steam-membership-failed-return-recovers-on-original-retry'))await sleep(20500);
  await test('steam-membership-failed-return-recovers-on-original-retry', `
    await membershipWait(400);check(performance.now()-membershipRetryObservedAt>=20000,'original retry window was not observed');
    check(membershipPlanCalls().length===2&&category(mergedGames()[0])==='in-steam','original retry did not recover membership');
    membershipFocused=false;window.dispatchEvent(new Event('blur'));await membershipWait(300);check(!steamRuntimeWatchTimer&&!refreshAfterScrapeTimer,'recovered read retained background timer');`);
  }
  const uiErrors=await evaluate(`fixtureCalls.filter(call=>call.command==='uiError')`);
  cases.push({name:'complete-shipping-UI-has-no-unhandled-errors',passed:uiErrors.length===0,...(uiErrors.length?{errors:uiErrors}:{})});
  const inputsUnchanged=inputFiles.every(file=>sourceHashes[file]===sha(file));
  cases.push({name:'tested-shipping-sources-unchanged',passed:inputsUnchanged});
  const report={selectedSidebarCases,sourceHashes,inputsUnchanged,viewportContracts:['1280x720','1280x800','1920x1080'],sidebarLayoutObservations:await evaluate('sidebarLayoutObservations'),physicalController:false,allPassed:cases.every(item=>item.passed),caseCount:cases.length,cases,realBrowserUsed:true,
    nativeBridge:'fixture-only',realSteamFilesModified:false,realGamesLaunched:false};
  fs.writeFileSync(path.join(root,'summary.json'),JSON.stringify(report,null,2));
  console.log(JSON.stringify(report,null,2));console.log('REPORT='+path.join(root,'summary.json'));
  if(!report.allPassed)process.exitCode=1;
} finally {
  const inventory=spawnSync('G:/YeManCC-Work/Environment/Toolchains/PowerShell/7.4.6/pwsh.exe',['-NoLogo','-NoProfile','-NonInteractive','-Command',`$items=Get-CimInstance Win32_Process;$parents=[System.Collections.Generic.HashSet[int]]::new();[void]$parents.Add(${browser.pid});$children=@();do{$added=$false;foreach($item in $items){if($parents.Contains([int]$item.ParentProcessId)-and -not $parents.Contains([int]$item.ProcessId)){[void]$parents.Add([int]$item.ProcessId);$children+=[pscustomobject]@{pid=[int]$item.ProcessId;parentPid=[int]$item.ParentProcessId;created=$item.CreationDate.ToUniversalTime().ToString('o')};$added=$true}}}while($added);ConvertTo-Json -InputObject @($children) -Compress`],{encoding:'utf8',windowsHide:true,timeout:5000});
  const children=inventory.status===0?JSON.parse(inventory.stdout.trim()||'[]'):null;
  fs.writeFileSync(path.join(root,'browser-descendants-before-close.json'),JSON.stringify({rootPid:browser.pid,inventoryExit:inventory.status,children},null,2),{flag:'wx'});
  if(socket?.readyState===1){try{await command('Browser.close');}catch{}}
  const browserDeadline=Date.now()+5000;while(browser.exitCode===null&&Date.now()<browserDeadline)await sleep(25);
  const live=()=>children===null?[]:children.filter(child=>{try{process.kill(child.pid,0);return true;}catch{return false;}});
  while(children!==null&&live().length&&Date.now()<browserDeadline)await sleep(25);
  const remaining=live();fs.writeFileSync(path.join(root,'browser-descendants-after-close.json'),JSON.stringify({rootExited:browser.exitCode!==null,inventoryConfirmed:children!==null,remaining},null,2),{flag:'wx'});
  if(browser.exitCode===null||children===null||remaining.length)process.exitCode=1;
  socket?.close();for(const item of waiting.values()){clearTimeout(item.timer);item.reject(new Error('Browser test closing'));}
  server.close();browser.kill();await sleep(150);fs.closeSync(log);
}
