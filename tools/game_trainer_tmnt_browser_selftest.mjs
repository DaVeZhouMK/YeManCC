// Real top-menu/profile/rule components and extracted production focus engine.
// All storage, processes and native actions are in-memory fixtures.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { createRequire } from 'node:module';
import { createServer } from 'node:http';
import { createHash } from 'node:crypto';
const root=process.cwd(),require=createRequire(path.join(root,'package.json'));
const {build}=require('esbuild'),{parse,compileScript,compileStyle}=require('vue/compiler-sfc');
const runtime=process.env.YMCC_UI_BROWSER_MODULE_DIR||'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const {chromium}=createRequire(path.join(runtime,'_top-menu-power.cjs'))('playwright');
const evidence=path.resolve(root,'../../Build/Validation/TrainerTmntConnect-20261015');const out=process.env.YMCC_TRAINER_BROWSER_OUTPUT||path.join(evidence,'tmnt-browser');
fs.mkdirSync(out,{recursive:true});
// Reuse the maintained dedicated-profile native fixtures rather than duplicate them.
const shared=fs.readFileSync('tools/independent_frame_rate_ui_selftest.mjs','utf8');
const mockStart=shared.indexOf('const mocks={'),mockEnd=shared.indexOf('const result=',mockStart);
assert(mockStart>=0&&mockEnd>mockStart);
const mocks=vm.runInNewContext('(function(){'+shared.slice(mockStart,mockEnd)+'return mocks;})()');
mocks['@/bridge/yeman'] += 'export const getTdpExecutionCapability=async()=>null;export const formatTdpTarget=(value)=>String(value)+" W";';
const engine=fs.readFileSync('src/gamepad/engine.ts','utf8');
const focusStart=engine.indexOf('function focusables(): HTMLElement[] {'),focusEnd=engine.indexOf('function isRangeInput(',focusStart);
assert(focusStart>=0&&focusEnd>focusStart);
const focusCode=engine.slice(focusStart,focusEnd);
const pageCommandCode=engine.slice(engine.indexOf('function offerPageCommand('),engine.indexOf('function navigate('));
const backCode=pageCommandCode+'\n'+engine.slice(engine.indexOf('function handleGamepadBack('),engine.indexOf('function handleGamepadEditGame('));
Object.assign(mocks,{
 '@/gamepad/engine': `import {focusGamepadElement,setGamepadFocused} from './src/gamepad/focus.ts';import {spatialNavigationTarget} from './src/gamepad/spatial.ts';const FOCUSABLE='button:not([disabled]):not([tabindex="-1"]):not([data-gp-ignore]), input:not([disabled]):not([tabindex="-1"]):not([data-gp-ignore]), select:not([disabled]):not([tabindex="-1"]):not([data-gp-ignore]), [tabindex]:not([tabindex="-1"]):not([data-gp-ignore])';const activeFanNodeEditor=()=>null;export const isMouseModeSuppressed=()=>false;${focusCode}\nexport const testMoveFocus=moveFocus;let sliderEditMode=false;const log=()=>{};${backCode} export const testControllerBack=handleGamepadBack;`,
 '@/bridge/uiSettings': `export const getUiSetting=()=>window.__fixture.autoTrainer!==false;export const loadUiSettings=async()=>{};export const setUiSettings=async patch=>{window.__fixture.autoTrainer=patch.trainerAutoPrepare;window.dispatchEvent(new CustomEvent('ui-settings:changed'))};`,
 '@/bridge/gamePolicyTarget': `export const getPolicyGame=()=>window.__fixture.game;export function subscribePolicyGameStatus(callback){callback(window.__fixture.game);return ()=>{}};`,
 '@/bridge/gameQuickSession': `export const getLockedGameTarget=()=>null;export const validateLockedGameTarget=async()=>window.__fixture.game;export const unlockGameTarget=()=>{};export const lockGameTarget=game=>game;export const captureAndLockGameTarget=async game=>game;`,
 '@/bridge/gameTrainer': `export const cancelGameTrainerPreparation=async()=>{};export const getGameTrainerState=()=>window.__fixture.trainerState||null;export const getReadyGameTrainer=(name,path)=>window.__fixture.trainerState?.state==='ready'&&path===window.__fixture.readyPath?{path:'fixture-trainer.exe'}:null;export const subscribeGameTrainerState=cb=>{window.__fixture.trainerChanged=cb;return()=>{window.__fixture.trainerChanged=null}};export class GameTrainerCancelledError extends Error{};export const openGameTrainerManager=async()=>{window.__fixture.managerOpens=(window.__fixture.managerOpens||0)+1};export const openOrSearchGameTrainer=async()=>{window.__fixture.trainerLaunches=(window.__fixture.trainerLaunches||0)+1;return {action:'opened'}};`,
 '@/bridge/quickapp': `export const OPTISCALER_CLIENT_DIR='fixture',OPTISCALER_CLIENT_EXE='fixture.exe',OPTISCALER_CLIENT_URL='https://fixture.invalid/';export const getLosslessGameState=async()=>({enabled:false}),setLosslessScalingEnabled=async()=>true,oneClickOptiScaler=async()=>true,optiscalerAnalyze=async()=>({}),optiConsoleInstalled=async()=>false,openOptiConsole=async()=>{};`,
 '@/bridge/speedhack': `export const SPEED_PRESETS=[0.5,1,1.5,2,3],getGameSpeedState=()=>null,isMinecraftTarget=()=>false,applyGameSpeed=async()=>true,clearGameSpeed=async()=>true;`,
 '@/bridge/gameproc': `export const QUICKAPP_SUSPENDED_EVENT='fixture-suspended';export const hasSuspendedState=async()=>({suspended:false}),closeGame=async()=>true,resumeGame=async()=>({ok:true}),suspendGame=async()=>({ok:true}),toggleMouseMode=async()=>false,waitForProcessExit=async()=>true;export async function closeJoyxoffIfRunning(){window.__fixture.joyCloses++};`,
 '@/bridge/api': `export const dialog={openFile:async()=>null,confirm:async()=>false},keyboard={open:async()=>false},proc={kill:async()=>false},shell={open:async()=>false},windowApi={minimize:async()=>{},taskView:async()=>{}};`,
 '@/bridge/gameInputOverride': `export const effectiveInputPersona=()=> 'disabled';`,
 '@/bridge/settingsRepository': `export const loadSettings=async()=>({input:{}});`,
 '@/bridge/gamedetect': `export const detectedGameName=game=>game?.name||'',detectGame=async()=>window.__fixture.game;`,
 '@/bridge/gameRules': String.raw`export const gameRuleNameFromPath=path=>path.replaceAll('\\','/').split('/').at(-1).replace(/\.exe$/i,'').toLowerCase();export const getGameRules=async()=>({blacklist:['launcher'],whitelist:[]});export async function setGameRuleList(){window.__fixture.ruleWrites++};`,
});

const live=JSON.parse(fs.readFileSync(process.env.YMCC_TRAINER_BROWSER_RECEIPT||path.join(evidence,'live-download.json'),'utf8').replace(/^﻿/,''));
if(process.env.YMCC_TRAINER_BROWSER_INERT_CT==='1'){const file=path.join(out,'inert-trainer.ct');fs.writeFileSync(file,'<?xml version="1.0"?><CheatTable><CheatEntries/></CheatTable>');live.trainerPath=file;live.trainerFolder=out;live.libraryRoot=path.dirname(out);live.sha256=createHash('sha256').update(fs.readFileSync(file)).digest('hex')}
const mocked=JSON.parse(fs.readFileSync(path.join(evidence,'tmnt-mocked-ready.json'),'utf8').replace(/^﻿/,''));
const archive=JSON.parse(fs.readFileSync(path.resolve(root,'../../Build/Validation/TrainerLogReview-20261015-170538/evidence.json'),'utf8'));
const target=process.env.YMCC_TRAINER_BROWSER_RECEIPT?{pid:live.identity.processId,processCreated:live.identity.processCreated,path:live.identity.executablePath,name:live.gameName,title:live.identity.windowTitle}:{pid:9132,processCreated:archive.gameDetected.originalGame.created,path:archive.gameDetected.originalGame.path,name:live.gameName,title:live.gameName};
const receipt=process.env.YMCC_TRAINER_BROWSER_RECEIPT?live:{...live,identity:{...mocked.identity,executablePath:target.path}};
assert.equal(createHash('sha256').update(fs.readFileSync(live.trainerPath)).digest('hex'),live.sha256);
mocks['@/bridge/gameValueScan']=`export const isGameValueScanAvailable=async()=>window.__fixture.ceAvailable!==false;export const startGameValueSession=async()=>null,subscribeGameValueState=()=>()=>{},gameValueCommand=async()=>null,cancelGameValueScan=async()=>{},stopGameValueSession=async()=>{},getGameValueDraft=()=>({firstValue:'',nextValue:'',desiredValue:'99999'}),saveGameValueDraft=()=>{};`;
mocks['@/robust/frontendDiagnostics']='export const reportFrontendError=()=>{};';
mocks['@/bridge/gameTrainer']=fs.readFileSync('src/bridge/gameTrainer.ts','utf8').replace("from './api'","from '@/bridge/api'").replace("from './gamedetect'","from '@/bridge/gamedetect'").replace("from './settingsRepository'","from '@/bridge/settingsRepository'");
const detectSource=fs.readFileSync('src/bridge/gamedetect.ts','utf8');
mocks['@/bridge/gamedetect']='const gameTitlePolicy='+fs.readFileSync('PowerControl/GameTitlePolicy.json','utf8')+';\n'+detectSource.slice(detectSource.indexOf('export function stripLaunchModeSuffix'),detectSource.indexOf('let preferredRefreshEpoch'))+'\nexport const detectGame=async()=>window.__fixture.game;';
mocks['@/bridge/settingsRepository']+='export const getSettingsGeneration=()=>0;';
mocks['@/bridge/api']=`export const dialog={openFile:async()=>null,confirm:async()=>false},keyboard={open:async()=>false},proc={kill:async()=>false},windowApi={minimize:async()=>{},taskView:async()=>{}};
const get=()=>window.__fixture,key=p=>p.replaceAll('/',String.fromCharCode(92)).toLowerCase();
export const app={powerControlDir:async()=> 'R:/Backend',dataDir:async()=> 'R:/Data',exeDir:async()=> 'R:/App'};
export const fs={exists:async()=>true,mkdir:async()=>{},writeTextFileAtomic:async(p,v)=>{get().files||=new Map();get().files.set(key(p),v)},readTextFile:async p=>get().files.get(key(p)),remove:async p=>get().files.delete(key(p)),sha256File:async p=>key(p)===key(get().game.path)?get().receipt.identity.executableSha256:get().receipt.sha256};
export const shell={open:async()=>false,hidden:async(_program,args)=>{const f=get(),arg=k=>args[args.indexOf(k)+1];let result;if(arg('-File').endsWith('YeManTrainerPrepare.ps1')){f.preparations=(f.preparations||0)+1;f.lastPrepare=args;result={...f.receipt,requestId:arg('-RequestId'),gameName:arg('-GameName')}}else{f.launches=(f.launches||0)+1;f.lastLaunch=args;result={requestId:arg('-RequestId'),ok:true}}f.files.set(key(arg('-ResultPath')),JSON.stringify(result));return{ok:true}}};`;
let entry=`import {createApp,h,ref,defineComponent} from 'vue';import Menu from './src/components/GameQuickActions.vue';import {testMoveFocus,testControllerBack} from '@/gamepad/engine';
const modes=['eco','balanced','medium','performance','elite','extreme'],profiles=Object.fromEntries(modes.map((mode,index)=>[mode,{cpuPreset:'turbo',coreMode:'big',tdpMax:12+index*4,fpsTarget:120,cpuTarget:'none',tdpStrategy:'none'}]));
window.__fixture={game:${JSON.stringify(target)},receipt:${JSON.stringify(receipt)},events:{},side:'ac',global:{ac:{fps:120,ceiling:120,lastFps:120},dc:{fps:60,ceiling:60,lastFps:60}},custom:{version:1,entries:{'a.exe':{displayName:'Test Game',enabled:true,acMode:'balanced',dcMode:'eco',ac:profiles.balanced,dc:profiles.eco,padPersona:'follow',gyroOverride:'follow'}},rtss:{'a.exe':{enabled:true,acFps:120,dcFps:60,acCeiling:120,dcCeiling:60,acLastFps:120,dcLastFps:60}}},schedule:{enabled:true,configured:true,active:{ac:'balanced',dc:'eco'},profiles:{ac:profiles,dc:structuredClone(profiles)}},globalWrites:[],customWrites:[],performanceWrites:[],applied:[],coreWrites:0,joyCloses:0,ruleWrites:0,status:{}};
let app;const mount=()=>{app=createApp(defineComponent({setup(){const game=ref(window.__fixture.game);return()=>h('div',{'data-gp-modal':''},[h(Menu,{game:game.value,open:true,onStatus:value=>window.__fixture.status=value})])}}));app.mount('#app')};window.__mount=mount;window.__unmount=()=>app.unmount();window.__move=(dx,dy)=>testMoveFocus(dx,dy);window.__controllerBack=()=>testControllerBack({});mount();`;
entry+=`import * as trainer from '@/bridge/gameTrainer';window.__trainer=trainer;`;
const styles=[];
const result=await build({stdin:{contents:entry,resolveDir:root,loader:'ts'},bundle:true,write:false,format:'iife',platform:'browser',alias:{'@':path.join(root,'src')},define:{'process.env.NODE_ENV':'"production"'},plugins:[{name:'top-menu-fixture',setup(builder){
 builder.onResolve({filter:/.*/},args=>args.path in mocks?{path:args.path,namespace:'fixture'}:undefined);
 builder.onLoad({filter:/.*/,namespace:'fixture'},args=>({contents:mocks[args.path],loader:'ts',resolveDir:root}));
 builder.onLoad({filter:/\.vue$/},args=>{if(['ContinuousPerformancePanel.vue','GameCustomProfilePanel.vue'].includes(path.basename(args.path)))return{contents:'export default {render(){return null}};',loader:'js'};const {descriptor,errors}=parse(fs.readFileSync(args.path,'utf8'),{filename:args.path});assert.deepEqual(errors,[]);const id='data-v-'+createHash('sha256').update(args.path).digest('hex').slice(0,8),script=compileScript(descriptor,{id,inlineTemplate:true});for(const style of descriptor.styles){const compiled=compileStyle({source:style.content,filename:args.path,id,scoped:style.scoped});assert.deepEqual(compiled.errors,[]);styles.push(compiled.code)}return {contents:script.content.replace('export default','const _component=')+'\n_component.__scopeId='+JSON.stringify(id)+';export default _component;',loader:'ts',resolveDir:path.dirname(args.path)}});
}}]});
const css=fs.readFileSync('src/styles/tokens.css','utf8')+'\n'+styles.join('\n');
const html='<!doctype html><meta charset="utf-8"><title>YMCC top menu</title><style>'+css+'\nbody{margin:0;background:var(--bg-solid);color:var(--text);font-family:system-ui}#app{max-width:580px;box-sizing:border-box;margin:auto;padding:14px}</style><main id="app"></main><script src="/fixture.js"></script>';
const server=createServer((request,response)=>{response.setHeader('Content-Type',request.url==='/fixture.js'?'text/javascript':'text/html; charset=utf-8');response.end(request.url==='/fixture.js'?result.outputFiles[0].text:html)});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const cases=[],screenshots=[],errors=[];let browser;
try{
 browser=await chromium.launch({headless:true,executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
 const page=await browser.newPage({viewport:{width:580,height:1250}});page.on('pageerror',error=>errors.push(error.message));
 await page.goto('http://127.0.0.1:'+server.address().port);await page.waitForFunction(()=>document.querySelector('[data-gp-game-control="trainer-menu"]')?.disabled===false);
 async function move(dx,dy){return await page.evaluate(({dx,dy})=>{window.__move(dx,dy);return document.activeElement?.closest('[data-gp-game-row]')?.dataset.gpGameRow||''},{dx,dy})}

 assert.equal(await page.locator('[data-gp-game-control="trainer-menu"] strong').innerText(),'游戏修改器');
 await page.evaluate(async()=>{const g=window.__fixture.game;await window.__trainer.prepareGameTrainer(g.title,{automatic:true,processPath:g.path,processTarget:{pid:g.pid,processCreated:g.processCreated}})});
 const button=page.locator('[data-gp-game-control="trainer-menu"]');await page.waitForFunction(()=>document.querySelector('[data-gp-game-control="trainer-menu"] strong')?.textContent==='已发现修改器');
 assert.equal(await button.locator('small').innerText(),'已就绪 · 点击启动');assert.equal(await page.evaluate(()=>window.__fixture.launches||0),0);
 assert.equal(await page.evaluate(()=>{const a=window.__fixture.lastPrepare;return a[a.indexOf('-GameProcessId')+1]}),String(target.pid));
 cases.push('Production trainer owner consumes the supplied verified game receipt and renders 已发现修改器');
 for(const width of [580,420]){await page.setViewportSize({width,height:1000});const image=path.join(out,'tmnt-found-'+width+'.png');await page.locator('.game-quick-menu').screenshot({path:image,animations:'disabled'});screenshots.push(image);assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false)}
 cases.push('Full selected game title and found state fit both handheld menu widths');
 await button.click();const menu=page.locator('[data-trainer-menu]');await menu.waitFor();assert.equal(await page.evaluate(()=>window.__fixture.launches||0),0);
 for(const width of [580,420,320]){
  await page.setViewportSize({width,height:900});
  const bounds=await menu.evaluate(n=>({width:n.getBoundingClientRect().width,overflow:n.scrollWidth>n.clientWidth}));
  assert.ok(bounds.width<=340);assert.equal(bounds.overflow,false);
  const heading=menu.getByRole('button').first().locator('strong');
  assert.ok((await heading.innerText()).includes(target.title));
  assert.equal(await heading.evaluate(n=>getComputedStyle(n).webkitLineClamp),'2');
  await menu.evaluate(n=>n.scrollTop=0);
  const image=path.join(out,'trainer-ce-long-title-'+width+'.png');await menu.screenshot({path:image});screenshots.push(image);
 }
 cases.push('Long actual game title wraps within the compact two-line launcher at three handheld widths');
 await page.evaluate(()=>window.__controllerBack());await menu.waitFor({state:'detached'});assert.equal(await button.evaluate(n=>document.activeElement===n),true);
 cases.push('Opening the submenu and physical-B production handler never launch a table');
 await button.click();await menu.getByRole('button').first().click();await page.waitForFunction(()=>window.__fixture.launches===1);
 assert.equal(await page.evaluate(()=>window.__fixture.status.error||''),'');cases.push('Player click reaches the original launch worker once with the confirmed game target');
 await page.evaluate(()=>{window.__unmount();window.__fixture.game={...window.__fixture.game,pid:window.__fixture.game.pid+1,processCreated:String(BigInt(window.__fixture.game.processCreated)+1n)};window.__mount()});
 await page.waitForFunction(()=>document.querySelector('[data-gp-game-control="trainer-menu"] strong')?.textContent==='游戏修改器');cases.push('Same EXE restart cannot display the previous process instance as found');
 await page.evaluate(()=>{window.__unmount();window.__fixture.game={...window.__fixture.game,path:'R:/Other/Game.exe',pid:999};window.__mount()});
 await page.waitForFunction(()=>document.querySelector('[data-gp-game-control="trainer-menu"] strong')?.textContent==='游戏修改器');cases.push('Switching to a different game EXE clears the found label');
 // The CE launcher remains useful when no running game is admitted.
 await page.evaluate(()=>{window.__unmount();window.__fixture.game=null;window.__mount()});
 await page.locator('[data-gp-game-control="trainer-menu"]').click();
 await menu.waitFor();
 assert.equal(await menu.getByRole('button').first().isDisabled(),true);
 assert.ok((await menu.getByRole('button').first().innerText()).includes('未识别到游戏'));
 assert.equal(await menu.locator('[data-trainer-ce]').isEnabled(),true);
 const beforeCe=await page.evaluate(()=>({launches:window.__fixture.launches||0,preparations:window.__fixture.preparations||0}));
 for(const width of [580,420,320]){
  await page.setViewportSize({width,height:640});
  const dimensions=await menu.evaluate(n=>({width:n.getBoundingClientRect().width,left:n.getBoundingClientRect().left,right:n.getBoundingClientRect().right,overflow:n.scrollWidth>n.clientWidth}));
  assert.ok(dimensions.width<=340);assert.ok(dimensions.left>=0&&dimensions.right<=width);assert.equal(dimensions.overflow,false);
  await menu.locator('.trainer-menu-cancel').scrollIntoViewIfNeeded();
  const image=path.join(out,'trainer-ce-menu-'+width+'.png');await menu.screenshot({path:image});screenshots.push(image);
 }
 assert.equal((await menu.locator('.trainer-menu-cancel').innerText()).replace(/\s/g,''),'B关闭页面');
 const footer=await menu.locator('.trainer-menu-cancel').evaluate(n=>({size:getComputedStyle(n).fontSize,height:n.getBoundingClientRect().height,keyBorder:getComputedStyle(n.querySelector('strong')).borderWidth}));
 assert.equal(footer.size,'10px');assert.equal(footer.height,30);assert.equal(footer.keyBorder,'0px');
 await menu.locator('[data-trainer-ce]').click();await menu.waitFor({state:'detached'});
 assert.equal(await page.evaluate(()=>window.__fixture.launches),beforeCe.launches+1);
 assert.equal(await page.evaluate(()=>window.__fixture.preparations),beforeCe.preparations);
 const ceArgs=await page.evaluate(()=>window.__fixture.lastLaunch);
 assert.equal(ceArgs[ceArgs.indexOf('-Mode')+1],'CheatEngine');assert.equal(ceArgs.includes('-GameProcessId'),false);assert.equal(ceArgs.includes('-TrainerPath'),false);
 cases.push('Independent CE uses production launch worker without game, preparation or CT argument');
 cases.push('Compact menu fits handheld widths and B footer matches parent styling');
 await page.locator('[data-gp-game-control="trainer-menu"]').click();await menu.waitFor();
 await menu.locator('.trainer-menu-cancel').click();await menu.waitFor({state:'detached'});
 assert.equal(await page.locator('.game-quick-menu').count(),1);
 await page.locator('[data-gp-game-control="trainer-menu"]').click();await menu.waitFor();
 await page.evaluate(()=>window.__controllerBack());await menu.waitFor({state:'detached'});
 assert.equal(await page.locator('.game-quick-menu').count(),1);
 cases.push('Mouse close and real controller B handler return one menu level without launching');
 await page.evaluate(()=>{window.__fixture.ceAvailable=false});await page.locator('[data-gp-game-control="trainer-menu"]').click();await menu.waitFor();await page.waitForTimeout(150);assert.equal(await menu.locator('[data-trainer-value-scan]').count(),0);await menu.locator('.trainer-menu-cancel').click();await menu.waitFor({state:'detached'});
 await page.evaluate(()=>{window.__fixture.ceAvailable=true});await page.locator('[data-gp-game-control="trainer-menu"]').click();await menu.waitFor();await menu.locator('[data-trainer-value-scan]').waitFor();await menu.locator('.trainer-menu-cancel').click();await menu.waitFor({state:'detached'});cases.push('Simple value scan entry is absent without CE and restored only after CE capability returns');
 await page.locator('[data-gp-game-control="trainer-menu"]').click();await menu.waitFor();await menu.getByRole('button',{name:/关闭自动下载修改器/}).click();await menu.getByRole('button',{name:/开启自动下载修改器/}).waitFor();assert.equal(await page.evaluate(()=>window.__fixture.autoTrainer),false);await menu.getByRole('button',{name:/开启自动下载修改器/}).click();await menu.getByRole('button',{name:/关闭自动下载修改器/}).waitFor();assert.equal(await page.evaluate(()=>window.__fixture.autoTrainer),true);cases.push('Automatic trainer downloading toggles off and back on through the production menu and settings event');
 await menu.getByRole('button',{name:/打开修改器管理器/}).click();await menu.waitFor({state:'detached'});const managerArgs=await page.evaluate(()=>window.__fixture.lastLaunch);assert.equal(managerArgs[managerArgs.indexOf('-Mode')+1],'Manager');assert.equal(managerArgs.includes('-TrainerPath'),false);assert.equal(managerArgs.includes('-GameProcessId'),false);cases.push('Manager button dispatches Manager mode without trainer or game arguments and closes one menu level');
 assert.deepEqual(errors,[]);const report={status:'passed',checks:cases.length,cases,screenshots,pageErrors:errors,scope:'Production Vue menu, trainer owner and controller B handler; historical exported game receipt with '+(process.env.YMCC_TRAINER_BROWSER_INERT_CT==='1'?'an explicitly requested inert local CT fixture':'the original downloaded CT')+'. All process, file and launch IPC remains isolated.'};fs.writeFileSync(path.join(out,'results.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
}finally{if(browser)await browser.close();await new Promise(resolve=>server.close(resolve))}
