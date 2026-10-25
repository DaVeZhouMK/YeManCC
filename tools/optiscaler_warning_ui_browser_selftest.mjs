// Real Vue components and popup/focus code. Native/game/client operations are
// mocked. QuickAppView's intentionally hidden entry is exposed ONLY in this
// compiled fixture, without changing its production visibility settings.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
import { createHash } from 'node:crypto';
import { createServer } from 'node:http';
import { fileURLToPath } from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const req=createRequire(path.join(root,'package.json'));
const {build}=req('esbuild');
const {parse,compileScript,compileStyle}=req('vue/compiler-sfc');
const runtime=process.env.YMCC_UI_BROWSER_MODULE_DIR||'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const {chromium}=createRequire(path.join(runtime,'_opti-ui.cjs'))('playwright');
const out=process.env.YMCC_OPTI_UI_OUT||path.resolve(root,'../../Build/Validation/opti-warning-link-20261010/warning-ui');
fs.mkdirSync(out,{recursive:true});
const mocks={
 '@/bridge/quickapp': `
 export const LS_PRIMARY='C:/fixture/LS.exe',OPTISCALER_CLIENT_DIR='C:/fixture/OPT',OPTISCALER_CLIENT_EXE='OPT.exe',OPTISCALER_CLIENT_URL='about:blank';
 export const dirnameOf=p=>p.slice(0,p.lastIndexOf('\\\\'));
 export const getLosslessGameState=async()=>({enabled:false}),setLosslessScalingEnabled=async()=>({notice:'fixture'});
 export async function optiscalerAnalyze(p){window.__fixture.scans.push(p);return {ok:window.__fixture.scanComplete,scanComplete:window.__fixture.scanComplete,antiCheat:window.__fixture.antiCheat,availableBackends:['fsr','xess'],missing:window.__fixture.scanComplete?[]:['扫描不完整']}}
 export async function oneClickOptiScaler(p,u,b,o){window.__fixture.installs.push({path:p,uninstall:u,backend:b,options:o});return {ok:true,written:3,backend:b,version:'0.9.5-pre4',antiCheat:!u&&window.__fixture.antiCheat,clientSyncMessage:'已登记到 OPT 客户端（模拟）'}};
 export const optiConsoleInstalled=async()=>true,openOptiConsole=async()=>{window.__fixture.clientOpens++;return '登记已同步（模拟）'};
 `,
 '@/bridge/gamedetect': `export const detectGame=async()=>window.__fixture.game,refreshGameStatus=async()=>window.__fixture.game,detectedGameName=g=>g?.title||g?.name||'';export const subscribeGameStatus=cb=>{cb(window.__fixture.game);return ()=>{}}`,
 '@/bridge/uiSettings': `export const getUiSetting=()=>true;export const loadUiSettings=async()=>{};export const setUiSettings=async()=>{};`,
 '@/bridge/gameTrainer': `export const openGameTrainerManager=async()=>{};export const cancelGameTrainerPreparation=async()=>{};export const getGameTrainerState=()=>null;export const subscribeGameTrainerState=()=>()=>{};export class GameTrainerCancelledError extends Error{};export const openOrSearchGameTrainer=async()=>({})`,
 '@/bridge/speedhack': `export const SPEED_PRESETS=[1,1.5,2],getGameSpeedState=()=>null,isMinecraftTarget=()=>false,applyGameSpeed=async()=>({ok:true}),clearGameSpeed=async()=>({ok:true})`,
 '@/bridge/gameproc': `export const QUICKAPP_SUSPENDED_EVENT='fixture-suspended',hasSuspendedState=async()=>({suspended:false}),waitForProcessExit=async()=>true;export const closeGame=async()=>{window.__fixture.closes++;return {ok:true}};export const resumeGame=async()=>({ok:true}),suspendGame=async()=>({ok:true}),toggleMouseMode=async()=>({ok:true})`,
 '@/bridge/gameQuickSession': `export const getLockedGameTarget=()=>window.__fixture.locked||null,lockGameTarget=g=>(window.__fixture.locked={...g,lockGeneration:1}),unlockGameTarget=()=>window.__fixture.locked=null,validateLockedGameTarget=async()=>window.__fixture.locked||null`,
 '@/bridge/gamePolicyTarget': `export const getPolicyGame=()=>window.__fixture.game,subscribePolicyGameStatus=cb=>{cb(window.__fixture.game);return ()=>{}}`,
 '@/bridge/gameInputOverride': `export const effectiveInputPersona=()=> 'disabled'`,
 '@/bridge/settingsRepository': `export const loadSettings=async()=>({input:{}}),readSettingsSection=async()=>({apps:[]}),saveSettingsSection=async()=>{}`,
 '@/bridge/quickActionLock': `export const tryAcquireQuickAction=()=>()=>{}`,
 '@/gamepad/engine': `export const isMouseModeSuppressed=()=>false`,
 '@/bridge/api': `export const fs={exists:async()=>false,getFileIcon:async()=>null},shell={execute:async(p,a)=>{window.__fixture.executions.push({path:p,args:a});return true},open:async()=>true},dialog={openFile:async()=>{window.__fixture.filePickers++;return 'D:\\\\Fixture\\\\Game.exe'}},registry={read:async()=>96,write:async()=>true},display={getModes:async()=>({modes:[]}),setMode:async()=>{},setTopology:async()=>{}},proc={terminateTree:async()=>{}},windowApi={openTaskView:async()=>{}}`,
 '@/bridge/music': `import {ref} from 'vue';export const folder=ref(''),baseUrl=ref(''),tracks=ref([]),index=ref(0),playing=ref(false),mode=ref('order'),error=ref(''),currentName=ref(''),hasFolder=ref(false),volume=ref(1),muted=ref(false);export const chooseFolder=async()=>{},scanFolder=async()=>{},togglePlay=()=>{},playNext=()=>{},playPrev=()=>{},setMode=()=>{},setVolume=()=>{},persistVolume=()=>{},toggleMute=()=>{}`,
};
for(const name of ['AppIcon','InlineIcon','GameCustomProfilePanel','GameRulePanel','Dropdown'])mocks[`@/components/${name}.vue`]=`import {defineComponent,h} from 'vue';export default defineComponent({setup:()=>()=>h('span')})`;
const styles=[];
const bundle=await build({stdin:{contents:`import {createApp} from 'vue';import Top from './src/components/GameQuickActions.vue';import View from './src/views/QuickAppView.vue';
 window.__fixture={game:{pid:4321,name:'Game.exe',title:'测试游戏',path:'D:\\\\Fixture\\\\Game.exe',ts:1,processCreated:'fixture-created'},antiCheat:true,scanComplete:true,scans:[],installs:[],executions:[],closes:0,clientOpens:0,filePickers:0};
 const component=location.search.includes('view')?View:Top;const vm=createApp(component,{game:window.__fixture.game,open:true}).mount('#app');window.__run=()=>{window.__done=false;vm.runOpti().finally(()=>window.__done=true)};`,resolveDir:root,sourcefile:'opti-browser-fixture.ts'},bundle:true,write:false,format:'esm',platform:'browser',plugins:[{name:'fixture',setup(b){
 b.onResolve({filter:/^@\//},a=>mocks[a.path]?{path:a.path,namespace:'mock'}:{path:path.join(root,'src',a.path.slice(2)+(path.extname(a.path)?'':'.ts'))});
 b.onResolve({filter:/^\.\/api$/},()=>({path:'@/bridge/api',namespace:'mock'}));
 b.onLoad({filter:/.*/,namespace:'mock'},a=>({contents:mocks[a.path],loader:'ts',resolveDir:root}));
 b.onLoad({filter:/\.vue$/},a=>{
  let text=fs.readFileSync(a.path,'utf8');
  if(a.path.endsWith('GameQuickActions.vue'))text=text.replace('</script>','defineExpose({runOpti:runFsr});\n</script>');
  if(a.path.endsWith('QuickAppView.vue'))text=text.replace('</script>','defineExpose({runOpti:onOptiScalerCurrent});\n</script>');
  const {descriptor,errors}=parse(text,{filename:a.path});assert.equal(errors.length,0);
  const id='data-v-'+createHash('sha256').update(a.path).digest('hex').slice(0,8);
  const script=compileScript(descriptor,{id,inlineTemplate:true});
  for(const s of descriptor.styles){const css=compileStyle({source:s.content,filename:a.path,id,scoped:s.scoped});assert.equal(css.errors.length,0);styles.push(css.code)}
  return {contents:script.content.replace('export default','const _component=')+`\n_component.__scopeId=${JSON.stringify(id)};export default _component;`,loader:'ts',resolveDir:path.dirname(a.path)};
 });
}}]});
const css=fs.readFileSync(path.join(root,'src/styles/tokens.css'),'utf8')+'\nbody{margin:0;padding:16px;background:#101620;color:#ddd;font-family:"Microsoft YaHei",sans-serif}.game-quick-menu{max-width:460px}'+styles.join('\n');
const server=createServer((q,s)=>{if(q.url.startsWith('/fixture.js')){s.setHeader('Content-Type','text/javascript');s.end(bundle.outputFiles[0].text)}else{s.setHeader('Content-Type','text/html; charset=utf-8');s.end(`<style>${css}</style><div id="app"></div><script type="module" src="/fixture.js"></script>`)}});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
let browser;const errors=[],cases=[],screenshots=[];
try{
 browser=await chromium.launch({headless:true,executablePath:process.env.YMCC_UI_BROWSER_EXE||'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
 const page=await browser.newPage({viewport:{width:1280,height:800}});page.on('pageerror',e=>errors.push(e.message));
 async function load(mode,patch={}){await page.goto(`http://127.0.0.1:${server.address().port}/?${mode}`);await page.waitForFunction(()=>typeof window.__run==='function');await page.evaluate(p=>Object.assign(window.__fixture,p),patch)}
 async function begin(){await page.evaluate(()=>window.__run());await page.getByRole('button',{name:'确认方案并继续',exact:true}).waitFor()}
 async function choose(){await page.getByRole('button',{name:'确认方案并继续',exact:true}).click();await page.locator('.opti-picker').waitFor({state:'detached'})}
 async function finishMessage(mode){if(mode==='view')await page.getByRole('button',{name:'知道了',exact:true}).click();await page.waitForFunction(()=>window.__done)}
 for(const mode of ['top','view']){
  await load(mode);await begin();
  const row=page.locator(mode==='top'?'.quick-opti-anticheat-actions':'.opti-picker-anticheat-actions');
  const boxes=await row.locator('button').evaluateAll(nodes=>nodes.map(n=>{const r=n.getBoundingClientRect();return {x:r.x,y:r.y,width:r.width}}));
  assert.equal(boxes.length,2);assert(Math.abs(boxes[0].y-boxes[1].y)<2);assert(boxes[1].x>boxes[0].x);
  const shot=path.join(out,`${mode}-picker.png`);await page.screenshot({path:shot});screenshots.push(shot);
  await page.getByRole('button',{name:'反作弊离线说明',exact:true}).click();await choose();
  await page.getByText('反作弊离线使用说明',{exact:true}).waitFor();await page.getByRole('button',{name:'知道了',exact:true}).click();await page.waitForFunction(()=>window.__done);
  assert.deepEqual(await page.evaluate(()=>({installs:window.__fixture.installs.length,closes:window.__fixture.closes,pickers:window.__fixture.filePickers})),{installs:0,closes:0,pickers:0});cases.push(`${mode}: parallel help/restore buttons; help is read-only`);

  await load(mode);await begin();await choose();await page.getByRole('button',{name:'我了解风险，仍然导入',exact:true}).waitFor();
  await page.waitForFunction(()=>document.activeElement?.textContent==='取消');
  const warning=path.join(out,`${mode}-warning.png`);await page.screenshot({path:warning});screenshots.push(warning);
  await page.getByRole('button',{name:'取消',exact:true}).click();await page.waitForFunction(()=>window.__done);
  assert.deepEqual(await page.evaluate(()=>({installs:window.__fixture.installs.length,closes:window.__fixture.closes})),{installs:0,closes:0});cases.push(`${mode}: risk defaults to cancel; cancellation never installs or closes game`);

  await load(mode);await begin();await choose();await page.getByRole('button',{name:'我了解风险，仍然导入',exact:true}).click();
  await page.getByRole('button',{name:'结束游戏并继续',exact:true}).click();
  if(mode==='view')await page.getByText('OptiScaler 安装成功',{exact:true}).waitFor();
  await finishMessage(mode);
  const accepted=await page.evaluate(()=>({installs:window.__fixture.installs,executions:window.__fixture.executions}));
  assert.equal(accepted.installs.length,1);assert.equal(accepted.installs[0].options.acknowledgeAntiCheatRisk,true);assert.equal(accepted.installs[0].path,'D:\\Fixture\\Game.exe');assert.deepEqual(accepted.executions,[]);
  assert.equal(await page.getByRole('button',{name:'启动游戏',exact:true}).count(),0);cases.push(`${mode}: accepted warning forwards target-bound acknowledgement and never offers launch`);

  await load(mode,{antiCheat:false});await begin();await choose();await page.getByRole('button',{name:'结束游戏并继续',exact:true}).click();
  if(mode==='view'){await page.getByText('OptiScaler 安装成功',{exact:true}).waitFor();await page.getByRole('button',{name:'知道了',exact:true}).click()}
  await page.getByRole('button',{name:'启动游戏',exact:true}).waitFor();assert.equal(await page.evaluate(()=>window.__fixture.installs[0].options.acknowledgeAntiCheatRisk),false);
  await page.getByRole('button',{name:'取消',exact:true}).click();await page.waitForFunction(()=>window.__done);cases.push(`${mode}: non-anticheat imports preserve optional launch flow`);

  await load(mode);await begin();await page.getByRole('button',{name:'还原',exact:true}).click();await choose();await page.getByRole('button',{name:'结束游戏并继续',exact:true}).click();await finishMessage(mode);
  assert.equal(await page.evaluate(()=>window.__fixture.installs[0].uninstall),true);assert.equal(await page.evaluate(()=>window.__fixture.scans.length),0);cases.push(`${mode}: restore works without requesting anti-cheat acknowledgement`);

  await load(mode,{scanComplete:false});await begin();await choose();
  if(mode==='view')await page.getByRole('button',{name:'知道了',exact:true}).click();await page.waitForFunction(()=>window.__done);
  assert.equal(await page.evaluate(()=>window.__fixture.installs.length),0);assert.equal(await page.getByRole('button',{name:'我了解风险，仍然导入',exact:true}).count(),0);cases.push(`${mode}: incomplete scan remains blocked`);
 }
 for(const mode of ['top','view']){
  await page.setViewportSize({width:420,height:640});await load(mode);await begin();await choose();
  await page.getByRole('button',{name:'我了解风险，仍然导入',exact:true}).waitFor();
  const bounds=await page.locator(mode==='top'?'.quick-dialog':'.fsr-confirm:not(.opti-picker)').evaluate(n=>{const r=n.getBoundingClientRect();return {left:r.left,right:r.right,top:r.top,bottom:r.bottom}});
  assert(bounds.left>=0&&bounds.right<=422&&bounds.top>=0&&bounds.bottom<=642);
  const shot=path.join(out,`${mode}-warning-420x640.png`);await page.screenshot({path:shot});screenshots.push(shot);
  await page.evaluate(()=>window.dispatchEvent(new Event('ipc:gamepad-back',{cancelable:true})));
  await page.waitForFunction(()=>window.__done);assert.equal(await page.evaluate(()=>window.__fixture.installs.length),0);
  cases.push(`${mode}: warning fits 420x640 viewport; gamepad Back cancels without installation`);
 }
 assert.deepEqual(errors,[]);
 const report={suite:'Opti warning real Vue UI',passed:cases.length,cases,screenshots,pageErrors:errors,scope:'Both real Vue handlers/templates/focus code; test-only expose of hidden view handler; native/game/client operations mocked; no real config writes'};
 fs.writeFileSync(path.join(out,'browser-results.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
}finally{if(browser)await browser.close();await new Promise(resolve=>server.close(resolve))}
