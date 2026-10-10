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
const out=process.env.YMCC_TOP_MENU_OUT||path.resolve(root,'../../Build/Validation/top-menu-power-groups-20261010/top-menu-browser');
fs.mkdirSync(out,{recursive:true});
// Reuse the maintained dedicated-profile native fixtures rather than duplicate them.
const shared=fs.readFileSync('tools/independent_frame_rate_ui_selftest.mjs','utf8');
const mockStart=shared.indexOf('const mocks={'),mockEnd=shared.indexOf('const result=',mockStart);
assert(mockStart>=0&&mockEnd>mockStart);
const mocks=vm.runInNewContext('(function(){'+shared.slice(mockStart,mockEnd)+'return mocks;})()');
const engine=fs.readFileSync('src/gamepad/engine.ts','utf8');
const focusStart=engine.indexOf('function focusables(): HTMLElement[] {'),focusEnd=engine.indexOf('function isRangeInput(',focusStart);
assert(focusStart>=0&&focusEnd>focusStart);
const focusCode=engine.slice(focusStart,focusEnd);
Object.assign(mocks,{
 '@/gamepad/engine': `import {focusGamepadElement,setGamepadFocused} from './src/gamepad/focus.ts';import {spatialNavigationTarget} from './src/gamepad/spatial.ts';const FOCUSABLE='button:not([disabled]):not([tabindex="-1"]):not([data-gp-ignore]), input:not([disabled]):not([tabindex="-1"]):not([data-gp-ignore]), select:not([disabled]):not([tabindex="-1"]):not([data-gp-ignore]), [tabindex]:not([tabindex="-1"]):not([data-gp-ignore])';const activeFanNodeEditor=()=>null;export const isMouseModeSuppressed=()=>false;${focusCode}\nexport const testMoveFocus=moveFocus;`,
 '@/bridge/gamePolicyTarget': `export const getPolicyGame=()=>window.__fixture.game;export function subscribePolicyGameStatus(callback){callback(window.__fixture.game);return ()=>{}};`,
 '@/bridge/gameQuickSession': `export const getLockedGameTarget=()=>null;export const validateLockedGameTarget=async()=>window.__fixture.game;export const unlockGameTarget=()=>{};export const lockGameTarget=game=>game;`,
 '@/bridge/gameTrainer': `export class GameTrainerCancelledError extends Error{};export const openOrSearchGameTrainer=async()=>{};`,
 '@/bridge/quickapp': `export const OPTISCALER_CLIENT_DIR='fixture',OPTISCALER_CLIENT_EXE='fixture.exe',OPTISCALER_CLIENT_URL='https://fixture.invalid/';export const getLosslessGameState=async()=>({enabled:false}),setLosslessScalingEnabled=async()=>true,oneClickOptiScaler=async()=>true,optiscalerAnalyze=async()=>({}),optiConsoleInstalled=async()=>false,openOptiConsole=async()=>{};`,
 '@/bridge/speedhack': `export const SPEED_PRESETS=[0.5,1,1.5,2,3],getGameSpeedState=()=>null,isMinecraftTarget=()=>false,applyGameSpeed=async()=>true,clearGameSpeed=async()=>true;`,
 '@/bridge/gameproc': `export const QUICKAPP_SUSPENDED_EVENT='fixture-suspended';export const hasSuspendedState=async()=>({suspended:false}),closeGame=async()=>true,resumeGame=async()=>({ok:true}),suspendGame=async()=>({ok:true}),toggleMouseMode=async()=>false,waitForProcessExit=async()=>true;export async function closeJoyxoffIfRunning(){window.__fixture.joyCloses++};`,
 '@/bridge/api': `export const dialog={openFile:async()=>null,confirm:async()=>false},keyboard={open:async()=>false},proc={kill:async()=>false},shell={open:async()=>false},windowApi={minimize:async()=>{},taskView:async()=>{}};`,
 '@/bridge/gameInputOverride': `export const effectiveInputPersona=()=> 'disabled';`,
 '@/bridge/settingsRepository': `export const loadSettings=async()=>({input:{}});`,
 '@/bridge/gamedetect': `export const detectedGameName=game=>game?.name||'',detectGame=async()=>window.__fixture.game;`,
 '@/bridge/gameRules': String.raw`export const gameRuleNameFromPath=path=>path.replaceAll('\\','/').split('/').at(-1).replace(/\.exe$/i,'').toLowerCase();export const getGameRules=async()=>({blacklist:['launcher'],whitelist:[]});export async function setGameRuleList(){window.__fixture.ruleWrites++};`,
});
const entry=`import {createApp,h,ref,defineComponent} from 'vue';import Menu from './src/components/GameQuickActions.vue';import {testMoveFocus} from '@/gamepad/engine';
const modes=['eco','balanced','medium','performance','elite','extreme'],profiles=Object.fromEntries(modes.map((mode,index)=>[mode,{cpuPreset:'turbo',coreMode:'big',tdpMax:12+index*4,fpsTarget:120,cpuTarget:'none',tdpStrategy:'none'}]));
window.__fixture={game:{pid:101,processCreated:'1001',path:'R:/memory/a.exe',name:'Test Game'},events:{},side:'ac',global:{ac:{fps:120,ceiling:120,lastFps:120},dc:{fps:60,ceiling:60,lastFps:60}},custom:{version:1,entries:{'a.exe':{displayName:'Test Game',enabled:true,acMode:'balanced',dcMode:'eco',ac:profiles.balanced,dc:profiles.eco,padPersona:'follow',gyroOverride:'follow'}},rtss:{'a.exe':{enabled:true,acFps:120,dcFps:60,acCeiling:120,dcCeiling:60,acLastFps:120,dcLastFps:60}}},schedule:{enabled:true,configured:true,active:{ac:'balanced',dc:'eco'},profiles:{ac:profiles,dc:structuredClone(profiles)}},globalWrites:[],customWrites:[],performanceWrites:[],applied:[],coreWrites:0,joyCloses:0,ruleWrites:0,status:{}};
let app;const mount=()=>{app=createApp(defineComponent({setup(){const game=ref(window.__fixture.game);return()=>h('div',{'data-gp-modal':''},[h(Menu,{game:game.value,open:true,onStatus:value=>window.__fixture.status=value})])}}));app.mount('#app')};window.__mount=mount;window.__unmount=()=>app.unmount();window.__move=(dx,dy)=>testMoveFocus(dx,dy);mount();`;
const styles=[];
const result=await build({stdin:{contents:entry,resolveDir:root,loader:'ts'},bundle:true,write:false,format:'iife',platform:'browser',alias:{'@':path.join(root,'src')},define:{'process.env.NODE_ENV':'"production"'},plugins:[{name:'top-menu-fixture',setup(builder){
 builder.onResolve({filter:/.*/},args=>args.path in mocks?{path:args.path,namespace:'fixture'}:undefined);
 builder.onLoad({filter:/.*/,namespace:'fixture'},args=>({contents:mocks[args.path],loader:'ts',resolveDir:root}));
 builder.onLoad({filter:/\.vue$/},args=>{const {descriptor,errors}=parse(fs.readFileSync(args.path,'utf8'),{filename:args.path});assert.deepEqual(errors,[]);const id='data-v-'+createHash('sha256').update(args.path).digest('hex').slice(0,8),script=compileScript(descriptor,{id,inlineTemplate:true});for(const style of descriptor.styles){const compiled=compileStyle({source:style.content,filename:args.path,id,scoped:style.scoped});assert.deepEqual(compiled.errors,[]);styles.push(compiled.code)}return {contents:script.content.replace('export default','const _component=')+'\n_component.__scopeId='+JSON.stringify(id)+';export default _component;',loader:'ts',resolveDir:path.dirname(args.path)}});
}}]});
const css=fs.readFileSync('src/styles/tokens.css','utf8')+'\n'+styles.join('\n');
const html='<!doctype html><meta charset="utf-8"><title>YMCC top menu</title><style>'+css+'\nbody{margin:0;background:var(--bg-solid);color:var(--text);font-family:system-ui}#app{max-width:580px;box-sizing:border-box;margin:auto;padding:14px}</style><main id="app"></main><script src="/fixture.js"></script>';
const server=createServer((request,response)=>{response.setHeader('Content-Type',request.url==='/fixture.js'?'text/javascript':'text/html; charset=utf-8');response.end(request.url==='/fixture.js'?result.outputFiles[0].text:html)});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const cases=[],screenshots=[],errors=[];let browser;
try{
 browser=await chromium.launch({headless:true,executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
 const page=await browser.newPage({viewport:{width:580,height:1250}});page.on('pageerror',error=>errors.push(error.message));
 await page.goto('http://127.0.0.1:'+server.address().port);await page.waitForFunction(()=>document.querySelector('[data-gp-custom-entry]')?.getAttribute('aria-disabled')==='false');
 async function move(dx,dy){return await page.evaluate(({dx,dy})=>{window.__move(dx,dy);return document.activeElement?.closest('[data-gp-game-row]')?.dataset.gpGameRow||''},{dx,dy})}
 const controls=page.locator('[data-gp-game-control="switch-program"]');
 const collapsed=['custom-entry','rules-entry','actions-1','actions-2','footer'];
 await controls.focus();for(const row of collapsed)assert.equal(await move(0,1),row);
 for(const row of ['actions-2','actions-1','rules-entry','custom-entry','controls'])assert.equal(await move(0,-1),row);
 cases.push('Production controller navigation follows controls → profile → rules → quick actions in both directions');
 const duck=page.getByRole('button',{name:'小黄鸭插帧',exact:false}),speed=page.getByRole('button',{name:'游戏变速倍率',exact:true}),fsr=page.locator('[data-gp-game-control="fsr-import"]'),trainer=page.getByRole('button',{name:'游戏修改器',exact:false});
 assert.equal(await duck.count(),1);assert.equal(await page.locator('.quick-action strong').filter({hasText:/^Lossless Scaling$/}).count(),0);
 for(const [from,dy,to] of [[speed,-1,duck],[duck,1,speed],[fsr,1,trainer],[trainer,-1,fsr]]){await from.focus();await move(0,dy);assert.equal(await to.evaluate(node=>document.activeElement===node),true,'Vertical quick action must keep its visual column');}
 cases.push('Right column is 小黄鸭插帧 ↔ game speed; left column is FSR ↔ trainer; display title is renamed without changing the action');
 for(const width of [580,420]){await page.setViewportSize({width,height:1250});const order=await page.evaluate(()=>{const rect=selector=>{const r=document.querySelector(selector).getBoundingClientRect();return {y:r.y,bottom:r.bottom}};return {controls:rect('.quick-game-controls'),custom:rect('[data-gp-custom-panel]'),rules:rect('[data-gp-game-rules]'),actions:rect('.game-quick-actions'),overflow:document.documentElement.scrollWidth>innerWidth}});assert(order.custom.y>=order.controls.bottom);assert(order.rules.y>=order.custom.bottom);assert(order.actions.y>=order.rules.bottom);assert.equal(order.overflow,false);const image=path.join(out,'top-menu-collapsed-'+width+'.png');await page.locator('.game-quick-menu').screenshot({path:image,animations:'disabled'});screenshots.push(image);cases.push('Profile is row 2 directly under controls, rules row 3, no overflow at '+width+'px')}
 await page.locator('[data-gp-custom-entry]').click();await page.waitForSelector('[data-gp-custom-body]');await page.waitForSelector('.custom-submenu-pop-enter-active',{state:'detached'});await page.waitForFunction(()=>document.activeElement?.getAttribute('aria-label')==='AC 专属性能档位');
 const expanded=['专用frame-ac','custom-dc','专用frame-dc','custom-core-big-picker'];for(const row of expanded)assert.equal(await move(0,1),row);
 assert.equal(await move(1,0),'custom-core-smt-picker');assert.equal(await move(0,1),'custom-input-gyro');assert.equal(await move(-1,0),'custom-input-pad');assert.equal(await move(0,1),'custom-actions');assert.equal(await move(0,1),'rules-entry');
 cases.push('Expanded controller order keeps AC preset+frame together before DC preset+frame, then paired core/input controls and rules');
 for(const width of [580,420]){await page.setViewportSize({width,height:1250});const layout=await page.evaluate(()=>{const rect=node=>{const r=node.getBoundingClientRect();return {y:r.y,bottom:r.bottom,height:r.height,x:r.x,right:r.right}};return {overflow:document.documentElement.scrollWidth>innerWidth,rows:[...document.querySelectorAll('[data-gp-custom-body] .power-mode-row')].map(row=>({row:rect(row),icon:rect(row.querySelector('.side-icon')),name:rect(row.querySelector('.side-name')),mode:rect(row.querySelector('.mode-picker')),frames:rect(row.querySelector('.mode-frame-controls')),sliders:row.querySelectorAll('input[type=range]').length,ceilings:row.querySelectorAll('.frame-rate-controls .dd-trigger').length,track:rect(row.querySelector('input[type=range]')),ceiling:rect(row.querySelector('.frame-rate-controls .dd-trigger'))})),labels:document.querySelectorAll('[data-gp-custom-body] .frame-rate-side').length}});assert.equal(layout.overflow,false);assert.equal(layout.rows.length,2);assert.equal(layout.labels,0);for(const row of layout.rows){assert(row.frames.y>=row.mode.bottom);assert.equal(row.sliders,1);assert.equal(row.ceilings,1);assert(Math.abs(row.track.y+row.track.height/2-row.ceiling.y-row.ceiling.height/2)<=1,'Dedicated frame track and dropdown must share their visual centerline');assert(Math.abs(row.icon.y+row.icon.height/2-row.row.y-row.row.height/2)<=1);assert(Math.abs(row.name.y+row.name.height/2-row.row.y-row.row.height/2)<=1)}const image=path.join(out,'top-menu-profile-'+width+'.png');await page.locator('.game-quick-menu').screenshot({path:image,animations:'disabled'});screenshots.push(image);cases.push('Both profile groups contain preset+frame, with icon and AC/DC name vertically centered at '+width+'px')}
 const ac=page.locator('[data-gp-custom-body] .power-mode-row.ac input');await ac.focus();assert.equal(await move(1,0),'专用frame-ac');assert.equal(await page.evaluate(()=>document.activeElement?.getAttribute('aria-label')),'专用插电锁帧上限');assert.equal(await move(1,0),'专用frame-ac');assert.equal(await move(-1,0),'专用frame-ac');assert.equal(await page.evaluate(()=>document.activeElement?.tagName),'INPUT');cases.push('Horizontal frame navigation stays within its own side despite reused local coordinates');await ac.press('ArrowLeft');await page.waitForFunction(()=>window.__fixture.custom.rtss['a.exe'].acFps===110);assert.equal(await page.locator('[data-gp-custom-body] .power-mode-row.dc input').inputValue(),'60');assert.equal(await page.evaluate(()=>window.__fixture.global.ac.fps),120);assert.equal(await page.evaluate(()=>window.__fixture.performanceWrites.length),0);
 cases.push('Grouped dedicated frame edit remains per-game/per-side and does not replay the power preset');
 await page.locator('[data-gp-custom-entry]').click();await page.waitForSelector('[data-gp-custom-body]',{state:'detached'});await page.locator('[data-gp-game-rules-entry]').click();await page.waitForSelector('[data-gp-game-rules-body]');await page.locator('[data-gp-game-control="fsr-import"]').focus();assert.equal(await move(0,-1),'rules-footer');
 cases.push('Expanded rule body stays in row-3 navigation; Up from FSR enters its bottom rather than jumping to switch-program');
 await page.evaluate(()=>window.__unmount());await page.evaluate(()=>{window.__fixture.game=null;window.__fixture.status={};window.__mount()});await page.waitForFunction(()=>document.querySelector('[data-gp-custom-entry]')?.getAttribute('aria-disabled')==='true');await controls.focus();assert.equal(await move(0,1),'rules-entry');await page.locator('[data-gp-custom-entry]').dispatchEvent('click');assert.equal(await page.locator('[data-gp-custom-body]').count(),0);assert.equal(await page.evaluate(()=>window.__fixture.customWrites.length),1);assert.equal(await page.evaluate(()=>window.__fixture.ruleWrites),0);
 cases.push('No-game menu keeps profile disabled and controller navigation skips it without creating configuration');
 await page.evaluate(()=>window.__unmount());assert.deepEqual(errors,[]);assert.equal(await page.evaluate(()=>Object.values(window.__fixture.events).reduce((total,set)=>total+set.size,0)),0);
 const report={suite:'YMCC top-menu power/frame groups',passed:cases.length,cases,screenshots,pageErrors:errors,scope:'Real Vue top-menu/profile/rule components and production moveFocus code; all native/storage mocked'};fs.writeFileSync(path.join(out,'results.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
}finally{if(browser)await browser.close();await new Promise(resolve=>server.close(resolve))}
