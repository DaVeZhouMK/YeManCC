import assert from 'node:assert/strict';
import fs from 'node:fs';import path from 'node:path';import {createHash,randomUUID} from 'node:crypto';
import {createServer} from 'node:http';import {spawn,spawnSync} from 'node:child_process';import {createRequire} from 'node:module';
import {build} from 'esbuild';import {parse,compileScript,compileStyle} from 'vue/compiler-sfc';
const root=process.cwd(),out=path.resolve(process.env.YMCC_CE_VALIDATION_ROOT || path.resolve(root,'../../Build/Validation/CeEngineBridge-selftest'),'browser-'+randomUUID().slice(0,8));fs.mkdirSync(out,{recursive:true});
const pc=path.join(out,'PowerControl'),data=path.join(out,'Data');fs.mkdirSync(path.join(pc,'MemoryScan'),{recursive:true});fs.mkdirSync(data);
const compiler=path.join(process.env.WINDIR,'Microsoft.NET/Framework64/v4.0.30319/csc.exe'),exe=path.join(out,'ValueFixture.exe');
const compilation=spawnSync(compiler,['/nologo','/optimize+','/target:winexe','/platform:x64','/r:System.Web.Extensions.dll','/out:'+exe,path.join(root,'tools/game_value_scan_fixture.cs')],{windowsHide:true,encoding:'utf8'});assert.equal(compilation.status,0,compilation.stdout+compilation.stderr);
const host=path.join(pc,'MemoryScan/MemoryScanHost.exe');fs.copyFileSync(path.join(process.env.YMCC_CE_VALIDATION_ROOT || path.resolve(root,'../../Build/Validation/CeEngineBridge-selftest'),'helper/MemoryScanHost.exe'),host);fs.copyFileSync(path.join(root,'PowerControl/MemoryScan/CeScanEngine.lua'),path.join(pc,'MemoryScan/CeScanEngine.lua'));
const fixture=spawn(exe,[out],{windowsHide:true,stdio:'ignore'});let fixtureExited=false;fixture.once('exit',()=>fixtureExited=true);process.once('exit',()=>{if(fixture.exitCode===null)fixture.kill()});
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
function atomic(file,value){fs.mkdirSync(path.dirname(file),{recursive:true});const temp=file+'.'+randomUUID()+'.tmp';fs.writeFileSync(temp,JSON.stringify(value));fs.renameSync(temp,file)}
async function read(file){for(let i=0;;i++){try{return JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''))}catch(error){if(i>=8)throw error;await delay(20)}}}
const until=Date.now()+8000;while(!fs.existsSync(path.join(out,'fixture-ready.json'))&&Date.now()<until)await delay(50);
const ready=await read(path.join(out,'fixture-ready.json'));
const identity=spawnSync('powershell.exe',['-NoProfile','-Command',`$p=Get-Process -Id ${ready.pid};[string]$p.StartTime.ToUniversalTime().ToFileTimeUtc()`],{windowsHide:true,encoding:'utf8'});assert.equal(identity.status,0);const created=identity.stdout.trim();
const ownerBirth=spawnSync('powershell.exe',['-NoProfile','-Command',`[string](Get-Process -Id ${process.pid}).StartTime.ToUniversalTime().ToFileTimeUtc()`],{windowsHide:true,encoding:'utf8'}).stdout.trim();
const owner={valid:true,pid:process.pid,path:process.execPath,processCreated:ownerBirth};
const target={pid:ready.pid,processCreated:created,path:exe,name:'ValueFixture',title:'数值搜索测试游戏',lockGeneration:1,lockReason:'action'};
function gate(){atomic(path.join(pc,'game-target.json'),{valid:true,pid:ready.pid,path:exe,processCreated:created,generation:1,lastSeen:Date.now()})}gate();const pulse=setInterval(gate,1500);
const runtime='C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';const {chromium}=createRequire(path.join(runtime,'_scan-test.cjs'))('playwright');
const mocks={
 '@/bridge/gamedetect':`export const detectedGameName=g=>g?.title||g?.name||'';`,
 '@/bridge/gameQuickSession':`export const getLockedGameTarget=()=>window.__target;export const validateLockedGameTarget=async()=>window.__target;`,
 '@/bridge/settingsRepository':`export const getSettingsGeneration=()=>0;`,
 '@/bridge/api':`const call=async(action,args)=>{const r=await fetch('/api',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({action,args})});const result=await r.json();if(result.error)throw new Error(result.error);return result.value};export const app={powerControlDir:async()=>${JSON.stringify(pc)},dataDir:async()=>${JSON.stringify(data)},pid:async()=>${process.pid},exeDir:async()=>${JSON.stringify(path.dirname(process.execPath))}};export const proc={identity:async()=>(${JSON.stringify(owner)})};export const fs={exists:p=>call('exists',[p]),mkdir:p=>call('mkdir',[p]),readTextFile:(p,n)=>call('read',[p,n]),writeTextFileAtomic:(p,v)=>call('write',[p,v]),remove:p=>call('remove',[p])};export const shell={hidden:(p,a)=>call('launch',[p,a])};export const keyboard={open:async()=>true};export const windowApi={minimize:async()=>{window.__returns=(window.__returns||0)+1;return true}};`,
};
const styles=[];
const bundled=await build({stdin:{contents:`import{createApp,h,ref}from'vue';import Scanner from'./src/components/GameValueScanner.vue';import * as scan from './src/bridge/gameValueScan';window.__target=${JSON.stringify(target)};window.__scan=scan;const shown=ref(true);window.__reopen=()=>{shown.value=true};createApp({render:()=>shown.value?h(Scanner,{target:window.__target,onClose:()=>{window.__closed=true;shown.value=false}}):null}).mount('#app');`,loader:'ts',resolveDir:root},bundle:true,format:'iife',write:false,plugins:[{name:'scan-fixture',setup(b){
 b.onResolve({filter:/.*/},a=>{const key=a.path.startsWith('./')?'@/bridge/'+a.path.slice(2):a.path;if(key in mocks)return{path:key,namespace:'mock'};if(a.path.startsWith('@/'))return{path:path.join(root,'src',a.path.slice(2))+(path.extname(a.path)?'':'.ts')}});
 b.onLoad({filter:/.*/,namespace:'mock'},a=>({contents:mocks[a.path],loader:'ts',resolveDir:root}));
 b.onLoad({filter:/\.vue$/},a=>{const{descriptor,errors}=parse(fs.readFileSync(a.path,'utf8'),{filename:a.path});assert.deepEqual(errors,[]);const id='data-v-'+createHash('sha256').update(a.path).digest('hex').slice(0,8),script=compileScript(descriptor,{id,inlineTemplate:true});for(const style of descriptor.styles){const c=compileStyle({source:style.content,filename:a.path,id,scoped:style.scoped});assert.deepEqual(c.errors,[]);styles.push(c.code)}return{contents:script.content.replace('export default','const component=')+'\ncomponent.__scopeId='+JSON.stringify(id)+';export default component;',loader:'ts',resolveDir:path.dirname(a.path)}});
}}]});
const children=[],events=[];let browser,delayResponses=false;
const allowed=p=>{const resolved=path.resolve(p);if(!resolved.startsWith(out+path.sep))throw new Error('Test filesystem boundary');return resolved};
const html='<meta charset="utf-8"><style>'+fs.readFileSync('src/styles/tokens.css','utf8')+styles.join('\n')+'body{background:var(--bg-solid);font-family:system-ui}</style><div id="app"></div><script src="/test.js"></script>';
const server=createServer(async(req,res)=>{try{
 if(req.url==='/api'){
  let raw='';for await(const chunk of req){raw+=chunk;if(raw.length>200000)throw Error('Request too large')};const{action,args}=JSON.parse(raw);let value;
  switch(action){
   case'exists':value=fs.existsSync(allowed(args[0]));break;
   case'mkdir':fs.mkdirSync(allowed(args[0]),{recursive:true});value=true;break;
   case'read':{const file=allowed(args[0]);if(delayResponses&&file.includes(path.sep+'responses'+path.sep))await delay(500);if(fs.statSync(file).size>args[1])throw Error('Read too large');value=fs.readFileSync(file,'utf8');break}
   case'write':{const file=allowed(args[0]),temp=file+'.'+randomUUID()+'.tmp';fs.writeFileSync(temp,args[1]);fs.renameSync(temp,file);value=true;break}
   case'remove':try{fs.unlinkSync(allowed(args[0]))}catch{}value=true;break;
   case'launch':{assert.equal(path.normalize(args[0]),path.normalize(host));assert.ok(args[1].length===1||(args[1].length===2&&args[1][0]==='--probe'));const a=args[1];allowed(a[a.length-1]);const child=spawn(host,a,{windowsHide:true,stdio:'ignore'});children.push(child);events.push({action:args[1][0]==='--probe'?'probe':'launch',pid:child.pid});value={ok:true,pid:child.pid};break}
   default:throw Error('Unknown test operation');
  }
  res.setHeader('Content-Type','application/json');res.end(JSON.stringify({value}));
 }else{res.setHeader('Content-Type',req.url==='/test.js'?'text/javascript':'text/html; charset=utf-8');res.end(req.url==='/test.js'?bundled.outputFiles[0].text:html)}
 }catch(error){res.setHeader('Content-Type','application/json');res.end(JSON.stringify({error:error.message}))}});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const checks=[],errors=[];
try{
 browser=await chromium.launch({headless:true,executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});const page=await browser.newPage({viewport:{width:640,height:840}});page.on('pageerror',e=>errors.push(e.message));await page.goto('http://127.0.0.1:'+server.address().port);
 const panel=page.locator('[data-value-scan]');await panel.waitFor();await page.locator('[data-value-first]').waitFor({state:'visible'});
 await page.locator('[data-value-search]').fill('5000');await page.locator('[data-value-first]').click();await page.waitForFunction(()=>window.__scan.getGameValueState()?.stage==='ready',{timeout:70000});
 const first=await page.evaluate(()=>window.__scan.getGameValueState());assert.ok(first.count>=2);checks.push('Production first search uses the shared exact target and finds 5000');
 assert.equal(await panel.locator('.value-game').innerText(),target.title);assert.equal(await panel.locator('select,.value-row,[data-value-freeze]').count(),0);
 assert.equal(await panel.evaluate(n=>getComputedStyle(n).getPropertyValue('--accent').trim()),'#2ea6ff');checks.push('Only the game name heads the scanner; native YMCC blue and a single linear flow replace technical controls');
 atomic(path.join(out,'fixture-control.json'),{value:4321,other:4321});await delay(160);
 await page.locator('[data-value-next]').fill('4321');await page.locator('[data-value-return]').click();assert.equal(await page.evaluate(()=>window.__returns),1);assert.equal(await page.locator('[data-value-next]').inputValue(),'4321');
 await page.evaluate(()=>window.dispatchEvent(new Event('ipc:gamepad-back',{cancelable:true})));await panel.waitFor({state:'detached'});await page.evaluate(()=>window.__reopen());await panel.waitFor();await page.locator('[data-value-next]').waitFor();assert.equal(await page.locator('[data-value-next]').inputValue(),'4321');checks.push('Return-game action retains the entered value; closing and reopening reuses the same helper and draft');
 for(const width of[640,420,320]){await page.setViewportSize({width,height:740});const dimensions=await panel.evaluate(n=>({left:n.getBoundingClientRect().left,right:n.getBoundingClientRect().right,overflow:n.scrollWidth>n.clientWidth}));assert.ok(dimensions.left>=0&&dimensions.right<=width);assert.equal(dimensions.overflow,false);await panel.screenshot({path:path.join(out,'value-change-'+width+'.png')})}
 await page.locator('[data-value-refine]').click();await page.locator('[data-value-new]').waitFor();assert.equal(await page.locator('[data-value-new]').inputValue(),'99999');
 const narrowed=await page.evaluate(()=>window.__scan.getGameValueState());assert.ok(narrowed.scanRound>=1&&narrowed.count>=2&&narrowed.count<=32);checks.push('Changing the number once narrows the set; target defaults to 99999');
 await page.locator('[data-value-write]').click();await page.waitForFunction(()=>window.__scan.getGameValueState()?.lastWriteValue==='99999'&&window.__scan.getGameValueState()?.pauseRestored===true);await page.getByRole('heading',{name:'改好了',exact:true}).waitFor();await delay(150);
 const values=await read(path.join(out,'fixture-values.json'));assert.equal(values.value,99999);assert.equal(values.other,99999);checks.push('One click bulk-writes all refined values and independently observes both fixture fields at 99999 after pause restoration');
 await page.locator('[data-value-return]').click();assert.equal(await page.evaluate(()=>window.__returns),2);checks.push('The completed step also offers Return to game');
 for(const width of[640,420,320]){await page.setViewportSize({width,height:740});const dimensions=await panel.evaluate(n=>({left:n.getBoundingClientRect().left,right:n.getBoundingClientRect().right,overflow:n.scrollWidth>n.clientWidth}));assert.ok(dimensions.left>=0&&dimensions.right<=width);assert.equal(dimensions.overflow,false);await panel.screenshot({path:path.join(out,'value-done-'+width+'.png')})}checks.push('Both the arrow input and completion remain usable at 640, 420, and 320 pixels');
 await page.evaluate(()=>window.dispatchEvent(new Event('ipc:gamepad-back',{cancelable:true})));assert.equal(await page.evaluate(()=>window.__closed),true);await panel.waitFor({state:'detached'});checks.push('Production controller B closes one level');
 await page.evaluate(()=>window.__scan.stopGameValueSession());assert.deepEqual(errors,[]);assert.equal(fixtureExited,false);
 delayResponses=true;await page.evaluate(()=>window.__reopen());await page.locator('[data-value-first]').waitFor();await page.locator('[data-value-search]').fill('99999');await page.locator('[data-value-first]').click();await page.evaluate(()=>window.dispatchEvent(new Event('ipc:gamepad-back',{cancelable:true})));await panel.waitFor({state:'detached'});assert.equal(await page.evaluate(()=>window.__scan.getGameValueState()),null);checks.push('B during a pending scan closes its session and requests CE cleanup without retaining a hidden scan');assert.deepEqual(errors,[]);
 const report={status:'passed',checks:checks.length,cases:checks,errors,helperEvents:events,hostSha256:createHash('sha256').update(fs.readFileSync(host)).digest('hex'),fixtureTarget:target,root:out,realGameMemoryWrites:0,boundary:'Production Vue/bridge and real compiled helper. IPC and native shared target are scoped to one owned inert fixture; no user game is opened or written.'};fs.writeFileSync(path.join(out,'browser-tests.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
}finally{
 clearInterval(pulse);if(browser)await browser.close();await new Promise(resolve=>server.close(resolve));fs.writeFileSync(path.join(out,'fixture-stop'),'stop');for(let i=0;i<100&&children.some(child=>child.exitCode===null);i++)await delay(100);for(let i=0;i<50&&fixture.exitCode===null;i++)await delay(100);
}