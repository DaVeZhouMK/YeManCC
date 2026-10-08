// Real shortcut editor / shared components in Chromium. Native and user storage are mocked.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
import { createServer } from 'node:http';
import { createHash } from 'node:crypto';
const root=process.cwd(), req=createRequire(path.join(root,'package.json'));
const {build}=req('esbuild'), {parse,compileScript,compileStyle}=req('vue/compiler-sfc');
const runtime=process.env.YMCC_UI_BROWSER_MODULE_DIR || 'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const {chromium}=createRequire(path.join(runtime,'_editor-responsive-selftest.cjs'))('playwright');
const out=process.env.YMCC_UI_BROWSER_OUT || 'G:/YeManCC-Work/Mainline/Build/Validation/controller-shortcut-editor-responsive-v0.0.33-20261008';
const baseline=process.env.YMCC_EDITOR_BASELINE;
fs.mkdirSync(out,{recursive:true});
const styles=[];
const mocks={
  '@/bridge/ipc': 'export function on(){return ()=>{}};export function isNativeRuntime(){return false};export async function invoke(){return {}}',
  '@/bridge/uiLifecycle': 'export function isUiVisible(){return true};export function onUiVisibilityChange(){return ()=>{}}',
  '@/bridge/yeman': 'export async function summonGet(){return structuredClone(window.__fixture.gamepad)};export async function summonSet(p){window.__fixture.nativeWrites++;Object.assign(window.__fixture.gamepad,p);return summonGet()};export async function oemKeysGet(){return structuredClone(window.__fixture.oem)};export async function setShortcutRecording(){window.__fixture.recordingCalls++;return {ok:true}}',
  '@/bridge/settingsRepository': 'export async function loadSettings(){return structuredClone(window.__fixture.settings)};export async function compareAndSwapInputSettings(){window.__fixture.writes++;return {ok:false,reason:"fixture-only"}}',
  '@/bridge/api': 'export const fs={},settingsStore={},shell={}',
  '@/gamepad/focus': 'export function focusGamepadElement(node){node?.focus()};export function getGamepadPopupPlacement(){return {style:{},above:false}}',
};
const result=await build({stdin:{resolveDir:root,loader:'ts',contents:
  "import {createApp} from 'vue';import Editor from './src/views/ControllerShortcutEditorView.vue';"+
  "import {normalizeSettings} from './src/bridge/settingsRepository.ts';"+
  "import {NATIVE_DEFAULT_SHORTCUTS,nativeDefaultRule,newControllerShortcutRule,CONTROLLER_SHORTCUT_ACTIONS} from './src/bridge/controllerShortcutRules.ts';"+
  "const gamepad={enabled:true,mouseBackend:'joyxoff',...Object.fromEntries(NATIVE_DEFAULT_SHORTCUTS.map(d=>[d.defaultKey,true]))};"+
  "const custom=CONTROLLER_SHORTCUT_ACTIONS.filter(a=>a.available&&!NATIVE_DEFAULT_SHORTCUTS.some(d=>d.actionId===a.id)).map((a,i)=>({...newControllerShortcutRule('fixture-'+i,a.id),params:Object.fromEntries((a.parameters||[]).map(p=>[p.id,p.options?.[0]?.value??p.default])),trigger:'hold',holdMs:2000,inputs:[{source:'controller',code:'lb'},{source:'controller',code:'rb'}]}));"+
  "window.__fixture={gamepad,writes:0,nativeWrites:0,recordingCalls:0,closes:0,oem:{familyId:'fixture',supported:true,keys:[...Array.from({length:4},(_,i)=>({keyId:'back'+(i+1),label:'M'+(i+1),backIndex:i+1,triggerCapability:'all'})),{keyId:'special',label:'专用键',backIndex:null,triggerCapability:'all'}]},settings:normalizeSettings({input:{buttonMapping:{rules:{schemaVersion:2,shortcutRules:[...NATIVE_DEFAULT_SHORTCUTS.map(d=>nativeDefaultRule(d.defaultKey)),...custom]}}}})};"+
  "createApp(Editor,{onClose(){window.__fixture.closes++}}).mount('#app');"},
  bundle:true,write:false,format:'iife',platform:'browser',target:'es2020',plugins:[{name:'real-editor-fixture',setup(b){
    b.onResolve({filter:/^@\//},args=>mocks[args.path]?{path:args.path,namespace:'mock'}:{path:(()=>{const p=path.join(root,'src',args.path.slice(2));return [p,p+'.ts',p+'.js',path.join(p,'index.ts')].find(f=>fs.existsSync(f)&&fs.statSync(f).isFile())||p})()});
    b.onLoad({filter:/.*/,namespace:'mock'},args=>({contents:mocks[args.path],loader:'ts',resolveDir:root}));
    b.onLoad({filter:/\.vue$/},args=>{
      const source=baseline&&args.path.endsWith('ControllerShortcutEditorView.vue')?fs.readFileSync(baseline,'utf8'):fs.readFileSync(args.path,'utf8');
      const {descriptor}=parse(source,{filename:args.path});const id='data-v-'+createHash('sha1').update(args.path).digest('hex').slice(0,8);
      const compiled=compileScript(descriptor,{id,inlineTemplate:true});
      for(const style of descriptor.styles){const css=compileStyle({source:style.content,filename:args.path,id,scoped:style.scoped});assert.equal(css.errors.length,0);styles.push(css.code);}
      return {contents:compiled.content.replace('export default','const _component =')+'\n_component.__scopeId='+JSON.stringify(id)+';export default _component;',loader:'ts',resolveDir:path.dirname(args.path)};
    });
  }}]});
const js=result.outputFiles[0].text;
const css=fs.readFileSync(path.join(root,'src/styles/tokens.css'),'utf8')+'\n'+styles.join('\n');
const html='<!doctype html><meta charset="utf-8"><title>Shortcut editor responsive validation</title><style>'+css+'\nbody{background:var(--bg-solid)}</style><main id="app"></main><script src="/fixture.js"></script>';
const server=createServer((request,response)=>{if(request.url?.startsWith('/gamepad-base.png')){response.setHeader('Content-Type','image/png');response.end(fs.readFileSync(path.join(root,'public/gamepad-base.png')));return}response.setHeader('Content-Type',request.url==='/fixture.js'?'text/javascript; charset=utf-8':'text/html; charset=utf-8');response.end(request.url==='/fixture.js'?js:html)});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const errors=[],cases=[],layouts=[],screenshots=[],visualRects=[];let browser;
const matrix=[
  {name:'1080p-100',width:1920,height:1080,dpr:1},
  {name:'1200p-100',width:1920,height:1200,dpr:1},
  {name:'1200p-150',width:1280,height:800,dpr:1.5},
  {name:'1280x800-100',width:1280,height:800,dpr:1},
  {name:'1280x720-100',width:1280,height:720,dpr:1},
  {name:'1080p-portrait-150',width:720,height:1280,dpr:1.5},
  {name:'1200p-portrait-150',width:800,height:1280,dpr:1.5},
  {name:'reference-normal',width:907,height:1217,dpr:1},
  {name:'reference-normal-scaled',width:726,height:974,dpr:1.25},
  {name:'reference-problem-150',width:596,height:798,dpr:1.5},
  {name:'1280x720-150-extra',width:854,height:480,dpr:1.5},
  {name:'narrow-extra',width:420,height:800,dpr:1},
];
async function checkLayout(page,name){
  const layout=await page.evaluate(()=>{
    const dialog=document.querySelector('.editor-dialog');
    const selectors=['.rule-row b','.rule-row small','.seg-main','.rule-row i','.input-slot-number','.input-slot-value','.oem-back-slot-name','.oem-back-slot-key','.oem-action','.oem-clear','.dd-selected-label','.parameter-grid>label>span','.parameter-toggle-action','.keyboard-key-action'];
    const clipped=[];
    for(const selector of selectors)for(const n of document.querySelectorAll(selector)){
      const r=n.getBoundingClientRect(),s=getComputedStyle(n);if(!r.width||!r.height)continue;
      const range=document.createRange();range.selectNodeContents(n);const text=range.getBoundingClientRect();
      const parent=n.closest('.dd-value');
      if((s.textOverflow==='ellipsis'&&s.overflow==='hidden'&&n.scrollWidth-n.clientWidth>2)||(!parent&&n.scrollHeight>n.clientHeight+1)||(parent&&(text.right>parent.getBoundingClientRect().right+1||text.bottom>parent.getBoundingClientRect().bottom+3)))clipped.push({selector,text:n.textContent.trim(),width:r.width,scroll:n.scrollWidth,height:r.height,clientHeight:n.clientHeight,scrollHeight:n.scrollHeight,textBounds:{right:text.right,bottom:text.bottom},parentBounds:parent?{right:parent.getBoundingClientRect().right,bottom:parent.getBoundingClientRect().bottom}:null});
    }
    const outside=[];for(const n of document.querySelectorAll('.editor-dialog button,.editor-dialog input,.editor-dialog .card')){const r=n.getBoundingClientRect();if(r.width&&r.height&&(r.left<dialog.getBoundingClientRect().left-1||r.right>dialog.getBoundingClientRect().right+1))outside.push(n.className)}
    const firstRows=[...document.querySelectorAll('.rule-row')].slice(0,3).map(n=>{const r=n.getBoundingClientRect();return {x:r.x,y:r.y,width:r.width}});
    return {firstRows,client:dialog.clientWidth,scroll:dialog.scrollWidth,clipped,outside,columns:getComputedStyle(document.querySelector('.rule-list')).gridTemplateColumns,fonts:[...document.querySelectorAll('.action-trigger-grid .seg-main')].map(n=>getComputedStyle(n).fontSize)};
  });
  layouts.push({name,...layout});
  if(!baseline){const [left,right,next]=layout.firstRows;assert(Math.abs(left.y-right.y)<=1&&right.x>left.x+left.width,name+': first two actions must be side by side');assert(next.y>left.y&&Math.abs(next.x-left.x)<=1,name+': next action must begin the next row');assert.equal(layout.columns.split(' ').length,2,name+': actions must stay in two columns');assert(layout.scroll<=layout.client+1,name+': horizontal dialog overflow');assert.deepEqual(layout.clipped,[],name+': clipped control text');assert.deepEqual(layout.outside,[],name+': controls outside dialog');}
  cases.push(name+': two columns, complete control text and horizontal bounds');
}
try{
  browser=await chromium.launch({headless:true,executablePath:process.env.YMCC_UI_BROWSER_EXE || 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
  for(const size of matrix){
    const context=await browser.newContext({viewport:{width:size.width,height:size.height},deviceScaleFactor:size.dpr});
    const page=await context.newPage();page.on('pageerror',error=>errors.push(error.message));
    await page.goto('http://127.0.0.1:'+server.address().port);await page.waitForSelector('.rule-row');
    const custom=page.locator('.rule-row').filter({hasText:'虚拟手柄开关'});await custom.click();
    await page.locator('.edit-column .card-head').scrollIntoViewIfNeeded();
    await checkLayout(page,size.name+' / controller');
    for(const [mode,label] of [['keyboard','键盘 + 鼠标'],['oem','背部和专用']]){
      await page.getByRole('button',{name:label,exact:true}).click();await checkLayout(page,size.name+' / '+mode);
    }
    await page.getByRole('button',{name:'手柄录制',exact:true}).click();
    // Capture the default/native view for exact before/after visual comparisons.
    await page.locator('.rule-row').filter({hasText:'呼出 YMCC'}).click();
    await page.evaluate(()=>document.querySelector('.editor-dialog').scrollTop=0);
    await page.evaluate(()=>document.fonts.ready);await page.waitForTimeout(160);
    const image=path.join(out,'editor-'+size.name+'.png');await page.screenshot({path:image});screenshots.push(image);
    visualRects.push({name:size.name,dpr:size.dpr,preview:await page.locator('.gamepad-visualizer, .viz').first().boundingBox()});
    // Every available action, including keyboard grids and multi-parameter forms.
    if(size.name==='reference-problem-150'||size.name==='1280x720-100'||size.name==='narrow-extra'){
      const count=await page.locator('.rule-row').count();
      for(let i=0;i<count;i++){await page.locator('.rule-row').nth(i).click();await checkLayout(page,size.name+' / action '+i)}
    }
    if(size.name==='reference-problem-150'){
      await custom.click();
      for(const trigger of ['按下','长按','松开','双击','连发']){
        await page.locator('.action-trigger-grid .seg-btn').filter({hasText:trigger}).click();
        await checkLayout(page,size.name+' / trigger '+trigger);
      }
      await page.setViewportSize({width:1280,height:720});
      await checkLayout(page,size.name+' / live resize');
      assert((await page.locator('.edit-column .card-head h3').textContent()).includes('虚拟手柄开关'));
    }
    assert.equal(await page.evaluate(()=>window.__fixture.writes),0);assert.equal(await page.evaluate(()=>window.__fixture.nativeWrites),0);
    // All bottom actions remain reachable by vertical scrolling; resize never saves.
    await page.locator('.apply-action').scrollIntoViewIfNeeded();assert.equal(await page.locator('.apply-action').isVisible(),true);
    await context.close();
  }
  assert.deepEqual(errors,[]);
  const report={suite:'Shortcut editor responsive UI',baseline:Boolean(baseline),passed:cases.length,cases,layouts,screenshots,visualRects,pageErrors:errors,scope:'real Vue editor and shared CSS/components, mock native/user storage; CSS viewport + DPR, not installed WebView2'};
  fs.writeFileSync(path.join(out,'browser-results.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({passed:report.passed,clipped:layouts.filter(x=>x.clipped.length||x.outside.length||x.scroll>x.client+1),screenshots},null,2));
}finally{if(browser)await browser.close();await new Promise(resolve=>server.close(resolve));}
