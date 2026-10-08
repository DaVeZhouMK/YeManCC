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
const ui = path.join(repo, 'CustomSteamLibrary/workspace-ui');
const edge = process.env.YEMAN_BROWSER_EXECUTABLE || 'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe';
assert(fs.existsSync(edge), 'Microsoft Edge is required');
const mock = `
  window.fixtureCalls = [];
  window.fixtureGame = { gameDirectory:'G:/Fixture/Games/Portal', directoryName:'Portal',
    primaryExecutable:'G:/Fixture/Games/Portal/Game.exe', primaryProductName:'Portal', status:'ready',
    candidates:[], steam:{appId:620, suggestedName:'Portal', status:'waiting-steam-verification'} };
  window.fixtureGame.gameDirectory = window.fixtureGame.gameDirectory.replaceAll('/',String.fromCharCode(92));
  window.fixtureGame.primaryExecutable = window.fixtureGame.primaryExecutable.replaceAll('/',String.fromCharCode(92));
  window.fixtureSnapshot = {config:{roots:['G:/Fixture/Games'],automaticScrapingEnabled:false},
    library:{games:[window.fixtureGame]},manualOverrides:{items:{}},
    steamPlan:{items:[],summary:{}},steamAccounts:{accounts:[]}};
  const listeners = [];
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
