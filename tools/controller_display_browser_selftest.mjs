// Real controller, gyro and preview views. Storage/IPC are in-memory only.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
import {createServer} from 'node:http';
import {createHash} from 'node:crypto';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const req=createRequire(path.join(root,'package.json'));
const {build}=req('esbuild'),{parse,compileScript,compileStyle}=req('vue/compiler-sfc');
const runtime=process.env.YMCC_UI_BROWSER_MODULE_DIR||'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const {chromium}=createRequire(path.join(runtime,'_controller-display-test.cjs'))('playwright');
const outputArgs=process.argv.slice(2);let positionalOut,explicitOut;
for(let i=0;i<outputArgs.length;i++){const arg=outputArgs[i];if(arg==='--out'||arg==='--output'){assert(outputArgs[i+1],'output directory is required');explicitOut=outputArgs[++i]}else{assert(!arg.startsWith('--'),'unknown option: '+arg);assert(!positionalOut,'only one positional output directory is accepted');positionalOut=arg}}
const out=path.resolve(explicitOut||positionalOut||process.env.YMCC_UI_BROWSER_OUT||path.resolve(root,'../../../Build/Validation/VirtualInput-CapabilityClosure-20261018/Browser','controller-display-'+new Date().toISOString().replace(/[:.]/g,'-')));
assert(!fs.existsSync(path.join(out,'results.json'))&&!fs.existsSync(path.join(out,'source-bindings.json')),'choose a fresh output directory; prior run evidence is retained');
fs.mkdirSync(out,{recursive:true});
const styles=[],cases=[],errors=[],screenshots=[],screenshotStates=[],environmentStates=[];
// Bind the exact product bytes bundled in this run; parallel edits fail this run.
const sources=new Map();
function source(filename){
  const data=fs.readFileSync(filename),sha256=createHash('sha256').update(data).digest('hex');
  const relative=path.relative(root,filename).replaceAll('\\','/');
  const prior=sources.get(relative);assert(!prior||prior.sha256===sha256,'source changed while bundling: '+relative);
  if(!prior){sources.set(relative,{path:relative,sha256});const copy=path.join(out,'sources',relative);fs.mkdirSync(path.dirname(copy),{recursive:true});fs.writeFileSync(copy,data)}
  return data;
}
source(fileURLToPath(import.meta.url));
let browser,browserServer,server,passed=false,failure=null,browserPid=null,httpPort=null,observations=null;
try{
const mocks={
  // Current views use GameInputEditor. This seam models only the base-settings
  // editor; the actual normalizeSettings/evaluateInputCasWrite remain below.
  '@/bridge/gameInputEditor':`import {loadSettings,compareAndSwapInputSettings} from '@/bridge/settingsRepository';export async function readGameInputEditor(){return {input:(await loadSettings()).input,target:{exe:'',pid:0,processCreated:'',generation:1},ownerEpoch:1,gameName:''}};export async function saveGameInputEditor(editor,patch,isCurrent=()=>true){if(!isCurrent())throw Error('stale editor');return compareAndSwapInputSettings(editor.input.revision,patch)};export function onGameInputEditorChanged(cb){const listeners=window.__fixture.editorListeners;listeners.add(cb);return ()=>listeners.delete(cb)}`,
  '@/bridge/ipc':`export const isNativeRuntime=true;export function getLastGamepadState(){return window.__fixture.lastGamepadState};export function on(name,cb){const f=window.__fixture;const list=f.events[name]??=new Set();list.add(cb);return ()=>list.delete(cb)};export async function invoke(method,args){const f=window.__fixture;f.requests.push(method);f.ipcCalls.push({method,args:structuredClone(args??{})});if(method==='gyro.windowsSensors.get')return {contextStableAtCheck:true,selectedAtQuery:{gyro:{present:true}}};if(method==='oem.keys.get')return {familyId:'msi-claw',supported:true,keys:[{keyId:'m1',label:'M1',backIndex:1,captureImplemented:true},{keyId:'m2',label:'M2',backIndex:2,captureImplemented:true}]};if(method==='screenTouchpads.get'){const state=f.touchpads[args?.persona??'steamdeck'];if(!state)throw Error('unknown fixture touchpad persona');return structuredClone(state)}if(method==='screenTouchpads.set'){f.apiCalls.push('screenTouchpads.set');throw Error('touchpad writes outside display fixture')}if(method==='input.shortcutRuntime.get')return f.shortcut??{active:false};if(method==='input.shortcutRuntime.clear'){f.shortcut={active:false};window.__emit('input.shortcutRuntime',f.shortcut)}return {ok:true}}`,
  '@/bridge/settingsRepository':`import {evaluateInputCasWrite} from ${JSON.stringify(path.join(root,'src/bridge/settingsRepository.ts'))};export {mergeSettings,VirtualStartProtectionActive} from ${JSON.stringify(path.join(root,'src/bridge/settingsRepository.ts'))};const clone=v=>JSON.parse(JSON.stringify(v));export function onInputSettingsChanged(cb){const listeners=window.__fixture.settingsListeners;listeners.add(cb);return ()=>listeners.delete(cb)};export async function loadSettings(){return clone(window.__fixture.settings)};export async function compareAndSwapInputSettings(revision,patch){const f=window.__fixture;const detached=clone(patch);f.writes.push(detached);const r=evaluateInputCasWrite(f.settings.input,revision,detached,true);if(r.ok){f.settings.input=clone(r.value);queueMicrotask(()=>{for(const cb of f.settingsListeners)cb()})}return r}`,
  // Profile deletion/performance restoration is outside this display fixture.
  // Keep this IO boundary inert; accidental actions are recorded and fail.
  '@/bridge/performanceSchedule':`export async function loadGameCustomConfig(){return {entries:{},rtss:{}}};export async function removeGameCustomProfile(){window.__fixture.apiCalls.push('profile.remove');throw Error('profile removal outside display fixture')};export async function restorePerformanceScheduleIfConfigured(){window.__fixture.apiCalls.push('performance.restore');throw Error('performance restore outside display fixture')}`,
  '@/bridge/gameInputOverride':`export function getGameInputOverrideState(){return {locked:false,identity:''}};export function subscribeGameInputOverrideState(cb){cb(getGameInputOverrideState());return ()=>{}}`,
  '@/bridge/api':`export const fs={},settingsStore={},shell={open:async()=>{window.__fixture.apiCalls.push('shell.open')}},windowApi={controllerEditorWidth:async()=>{window.__fixture.apiCalls.push('window.controllerEditorWidth');return {ok:true}}}`,
  '@/bridge/gameproc':`export async function closeJoyxoffIfRunning(){};export async function getGameInputRedistState(){return {mouseInterfaceAvailable:true}}`,
  '@/gamepad/focus':`export function focusGamepadElement(n){n?.focus()};export function getGamepadPopupPlacement(){return {style:{},above:false}}`,
  '@/components/SteamDeckMouseSensitivity.vue':`export default {render(){return null}}`,
  '@/components/ControllerShortcutsCard.vue':`export default {render(){return null}}`,
  '@/components/ControllerFeedbackCapabilitiesCard.vue':`export default {render(){return null}}`,
  '@/views/ControllerShortcutEditorView.vue':`export default {render(){return null}}`,
};
const result=await build({metafile:true,loader:{'.png':'dataurl'},stdin:{resolveDir:root,loader:'ts',contents:`
import {DEFAULT_GAMEPAD_REDIST_DIRECTORY,HIDHIDE_INSTALLER_NAME} from './src/bridge/hidHideSetupPrompt.ts';import {createApp,nextTick} from 'vue';import {screenTouchpadDefaults} from './src/bridge/screenTouchpads.ts';import Controller from './src/views/ButtonMappingView.vue';import Gyro from './src/views/GyroMotionView.vue';import Preview from './src/components/GamepadVisualizer.vue';import {normalizeSettings} from './src/bridge/settingsRepository.ts';
const initial=normalizeSettings({input:{outputTarget:{persona:'steamdeck',buttonMappingEnabled:true,gyroEnabled:true},gyroMotion:{enabled:true,preset:'custom',outputStick:'right',outputMode:'virtual-stick',motionMode:'on',virtualPadLink:true}}});
window.__fixture={settings:initial,events:{},writes:[],requests:[],ipcCalls:[],apiCalls:[],touchpads:Object.fromEntries(['disabled','steamdeck','dualsense-edge','elite'].map(persona=>[persona,{...screenTouchpadDefaults(persona),persona,ok:true,available:true,steamDeckAvailable:false,ps5Available:false,visible:false,reason:'isolated-display-fixture'}])),shortcut:null,lastGamepadState:null,settingsListeners:new Set(),editorListeners:new Set()};window.__emit=(name,data)=>{if(name==='gamepad.state')window.__fixture.lastGamepadState=data;for(const cb of window.__fixture.events[name]??[])cb(data)};window.__normalize=normalizeSettings;window.__tick=nextTick;window.__environmentInstallerDefaults={directory:DEFAULT_GAMEPAD_REDIST_DIRECTORY,name:HIDHIDE_INSTALLER_NAME};
let app;window.__mount=(mode,persona='elite')=>{app?.unmount();app=createApp(mode==='controller'?Controller:mode==='gyro'?Gyro:Preview,mode==='preview'?{persona,settings:{enabled:false},showTestMode:false}:{});app.mount('#app')};window.__mount('controller');
`},bundle:true,write:false,format:'iife',platform:'browser',alias:{'@':path.join(root,'src')},define:{'process.env.NODE_ENV':'"production"'},plugins:[{name:'controller-display-fixture',setup(b){
  b.onResolve({filter:/.*/},args=>{
    if(args.path in mocks)return {path:args.path,namespace:'fixture'};
    // Pure helpers import IPC/API relatively; route those imports to the same
    // isolated boundary rather than bundling a second, real bridge instance.
    if(args.path.startsWith('.')&&path.isAbsolute(args.importer)){
      const target=path.resolve(path.dirname(args.importer),args.path).replace(/\.ts$/,'');
      for(const name of ['ipc','api','gameInputEditor','gameInputOverride','performanceSchedule'])if(target===path.join(root,'src/bridge',name))return {path:'@/bridge/'+name,namespace:'fixture'};
    }
  });
  b.onLoad({filter:/.*/,namespace:'fixture'},args=>({contents:mocks[args.path],loader:'ts',resolveDir:root}));
  b.onLoad({filter:/\.(?:ts|tsx|json)$/},args=>{if(args.path.startsWith(path.join(root,'src')+path.sep))return {contents:source(args.path).toString('utf8'),loader:path.extname(args.path).slice(1),resolveDir:path.dirname(args.path)}});
  b.onLoad({filter:/\.vue$/},args=>{
    const {descriptor,errors}=parse(source(args.path).toString('utf8'),{filename:args.path});assert.deepEqual(errors,[]);
    const id='data-v-'+createHash('sha256').update(args.path).digest('hex').slice(0,8);
    const compiled=compileScript(descriptor,{id,inlineTemplate:true});
    for(const style of descriptor.styles){const css=compileStyle({source:style.content,filename:args.path,id,scoped:style.scoped});assert.deepEqual(css.errors,[]);styles.push(css.code)}
    return {contents:compiled.content.replace('export default','const _component =')+'\n_component.__scopeId='+JSON.stringify(id)+';export default _component;',loader:'ts',resolveDir:path.dirname(args.path)};
  });
}}]});
const js=result.outputFiles[0].text;
const css=source(path.join(root,'src/styles/tokens.css')).toString('utf8')+'\n'+styles.join('\n');
const html='<!doctype html><meta charset="utf-8"><title>Controller display validation</title><style>'+css+'\nbody{margin:0;background:var(--bg-solid);color:var(--text);font-family:system-ui}#app{padding:24px;max-width:800px;margin:auto}</style><main id="app"></main><script src="/fixture.js"></script>';
server=createServer((request,response)=>{
  if(request.url.startsWith('/gamepad-base.png')){response.setHeader('Content-Type','image/png');response.end(source(path.join(root,'public/gamepad-base.png')));return}
  response.setHeader('Content-Type',request.url==='/fixture.js'?'text/javascript; charset=utf-8':'text/html; charset=utf-8');response.end(request.url==='/fixture.js'?js:html);
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
httpPort=server.address().port;
  browserServer=await chromium.launchServer({headless:true,executablePath:process.env.YMCC_UI_BROWSER_EXE||'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
  browserPid=browserServer.process().pid;browser=await chromium.connect(browserServer.wsEndpoint());
  const page=await browser.newPage({viewport:{width:1120,height:900}});page.on('pageerror',e=>errors.push(e.message));
  const origin='http://127.0.0.1:'+httpPort;
  await page.route('**/*',route=>{const url=route.request().url();if(url.startsWith(origin+'/')||url.startsWith('data:'))return route.continue();errors.push('external request blocked: '+url);return route.abort()});
  await page.goto(origin);
  await page.waitForFunction(()=>document.querySelector('.persona-btn.active')?.textContent==='SteamDeck');
  const baseline=await page.evaluate(()=>structuredClone(window.__fixture.settings));
  assert.deepEqual(await page.locator('.persona-btn').allTextContents(),['关闭虚拟手柄','SteamDeck','PS5','Xbox']);cases.push('current selector exposes disabled/Deck/Edge/Elite only');
  // ButtonMappingView now renders ControllerChannelStatus, whose compact labels
  // replace the removed persona-capabilities spans; retain all runtime verdicts.
  const channels=page.locator('.controller-channel-list .controller-channel');
  const status=channels.first().locator('strong');
  const values=()=>channels.locator('strong').allTextContents();
  assert.deepEqual(await channels.locator('span').allTextContents(),['虚拟输出','陀螺仪','背键']);
  assert.equal(await status.textContent(),'待确认');cases.push('saved intent alone never claims runtime success');
  const personaLabels={disabled:'关闭虚拟手柄',steamdeck:'SteamDeck','dualsense-edge':'PS5',elite:'Xbox'};
  const assertSelected=async persona=>{
    await page.waitForFunction(label=>document.querySelectorAll('.persona-btn.active').length===1&&document.querySelector('.persona-btn.active')?.textContent===label,personaLabels[persona]);
    assert.deepEqual(await page.locator('.persona-btn.active').allTextContents(),[personaLabels[persona]],'selected button must match named persona '+persona);
  };
  const assertTouchpadDefaults=async persona=>{
    const panel=page.locator('.controller-touchpads-card');
    const state=await page.evaluate(persona=>window.__fixture.touchpads[persona],persona);assert.equal(state.scale,100);assert.equal(state.transparency,80);
    // ScreenTouchpadsSettings renders sliders only when overlaysEnabled. The
    // disabled default has every overlay off, so its complete defaults stay hidden.
    if(persona==='disabled')assert.equal(await panel.locator('.screen-touchpad-sliders').count(),0);
    else{
      assert.equal(await panel.getByRole('slider',{name:'触摸板缩放',exact:true}).inputValue(),'100');
      assert.equal(await panel.getByRole('slider',{name:'触摸板显示透明度',exact:true}).inputValue(),'80');
      const sliderValues=await panel.locator('.slider-val').allTextContents();assert.deepEqual(sliderValues.map(v=>v.replace(/\s/g,'')),['100%','80%']);
    }
    assert(!/undefined|NaN/.test(await panel.textContent()),'full display defaults must survive get reply');
    const actual=await page.evaluate(persona=>window.__fixture.ipcCalls.filter(call=>call.method==='screenTouchpads.get').at(-1)?.args.persona,persona);assert.equal(actual,persona,'touchpad get must use current persona');
    cases.push(persona+' complete touchpad get reply retains real scale100/opacity80 display defaults');
  };
  const personaShot=async persona=>{
    await assertSelected(persona);
    const before=await page.locator('.persona-btn').evaluateAll(nodes=>nodes.map(n=>({label:n.textContent,active:n.classList.contains('active'),color:getComputedStyle(n).color,background:getComputedStyle(n).backgroundColor})));
    // ButtonMappingView uses 120ms color/background transitions. Finish finite
    // animations when capturing so a named screenshot shows its settled state.
    const shot=path.join(out,persona+'-steam-stopped.png');await page.screenshot({path:shot,animations:'disabled'});screenshots.push(shot);
    const after=await page.locator('.persona-btn').evaluateAll(nodes=>nodes.map(n=>({label:n.textContent,active:n.classList.contains('active'),color:getComputedStyle(n).color,background:getComputedStyle(n).backgroundColor})));
    screenshotStates.push({persona,expectedLabel:personaLabels[persona],before,after});
    assert.deepEqual(after.filter(n=>n.active).map(n=>n.label),[personaLabels[persona]]);
    assert(after.filter(n=>!n.active).every(n=>n.color!==after.find(n=>n.active).color),'settled paint must distinguish selected persona');
  };
  const snapshot=async(persona,steamRunning,state='active')=>{
    await page.evaluate(async({persona,steamRunning,state})=>{const f=window.__fixture;if(f.nativePersona!==persona){f.nativePersona=persona;f.nativeRequestId=(f.nativeRequestId||0)+1;}window.__emit('gamepad.state',{virtualOutput:{startProtection:{requestId:f.nativeRequestId,persona,locked:false,phase:state==='suspected-failed'?'failure':'success',elapsedMs:0,remainingMs:0,maxMs:30000,settleMs:10000,verificationPending:false},hostEpoch:2,sourceCurrent:true,state,reason:state==='suspected-failed'?'transport-fault':'current-session-frame-accepted',requestedPersona:persona,boundPersona:persona,hostAlive:true,prepared:true,neutralized:true,frameAccepted:true,sourceLive:true,suppressionState:'verified',steamRunning}});await window.__tick()},{persona,steamRunning,state});await assertSelected(persona);
  };
  const temporary=async(persona,enabled=true)=>{await page.evaluate(async({persona,enabled})=>{const state={active:true,persona,virtualEnabled:enabled,gyroEnabled:enabled};window.__fixture.shortcut=state;window.__emit('input.shortcutRuntime',state);await window.__tick()}, {persona,enabled});await assertSelected(persona)};
  // Exercise the actual Vue consumer with read-only gamepad.state snapshots.
  // A generic child exit (12/13) cannot identify which dependency failed.
  const environmentNote=page.locator('.persona-block > .environment-note, .persona-block > .fault-line');
  const environmentDefaults=await page.evaluate(()=>window.__environmentInstallerDefaults);
  const environmentCases=[
    {name:'idle has no notice',setup:{driverSetup:0},none:true},
    {name:'Ready has no notice',setup:{driverSetup:2},none:true},
    {name:'Ready ignores stale installer notice',setup:{driverSetup:2,hidHideSetupNotice:5},none:true},
  ];
  for(const [driverSetup,driverSetupResult] of [[6,12],[7,13]]){
    for(const hidHideSetupNotice of [undefined,0,1,2,3,4,7,8,99,driverSetup===6?6:5]){
      environmentCases.push({name:'newer driver running: generic exit '+driverSetupResult+' with notice '+hidHideSetupNotice+' stays neutral',setup:{driverSetup,driverSetupResult,...(hidHideSetupNotice===undefined?{}:{hidHideSetupNotice})},neutral:true,pattern:/未能确认|未确认/});
    }
  }
  const customDirectory='D:\\YMCC Fixture\\redist';
  for(const [driverSetup,hidHideSetupNotice,title] of [[6,5,'缺少 HidHide 安装器'],[7,6,'HidHide 安装器校验失败']]){
    environmentCases.push({name:'confirmed installer notice '+hidHideSetupNotice+' uses reported directory',setup:{driverSetup,hidHideSetupNotice,driverSetupRedistDir:customDirectory},title,installer:customDirectory+'\\'+environmentDefaults.name});
    environmentCases.push({name:'confirmed installer notice '+hidHideSetupNotice+' reuses default directory',setup:{driverSetup,hidHideSetupNotice,driverSetupRedistDir:''},title,installer:environmentDefaults.directory+'\\'+environmentDefaults.name});
  }
  environmentCases.push(
    {name:'unverified preparation stays neutral',setup:{driverSetup:3,hidHideSetupNotice:8},neutral:true,pattern:/未能确认|未确认/},
    {name:'unknown inventory stays advisory',setup:{driverSetup:8,hidHideSetupNotice:8},neutral:true,pattern:/清单.*未识别/},
    {name:'Pending owns the existing preparation',setup:{driverSetup:1,hidHideSetupNotice:5},neutral:true,pattern:/后台进行/},
    {name:'Slow observes the same installation',setup:{driverSetup:5,hidHideSetupNotice:6},neutral:true,pattern:/同一次安装/},
    {name:'unknown future status stays neutral',setup:{driverSetup:99,hidHideSetupNotice:5},neutral:true,pattern:/未确认/},
    {name:'reboot remains a reported fact',setup:{driverSetup:4,hidHideSetupNotice:7},title:'手柄驱动安装后需要重启',pattern:/重启 Windows/},
    {name:'reboot is preserved without an installer diagnosis',setup:{driverSetup:4,hidHideSetupNotice:8},title:'手柄驱动安装后需要重启',pattern:/重启 Windows/},
  );
  const emitEnvironment=async(setup,extra={})=>{
    await page.evaluate(async({setup,extra})=>{window.__emit('gamepad.state',{virtualOutput:window.__fixture.lastGamepadState?.virtualOutput,...setup,...extra});await window.__tick()},{setup,extra});
  };
  const writesBeforeEnvironment=await page.evaluate(()=>window.__fixture.writes.length);
  for(const height of [720,800]){
    await page.setViewportSize({width:1280,height});
    // Active runtime feedback models a usable installed/newer driver. This
    // fixture cannot prove an installed version or run a native driver.
    await snapshot('steamdeck',true);
    for(const scenario of environmentCases){
      await emitEnvironment(scenario.setup);
      const actual={viewport:{width:1280,height},name:scenario.name,setup:scenario.setup,notices:await environmentNote.allTextContents(),titles:await environmentNote.locator('strong').allTextContents(),faultLines:await page.locator('.persona-block > .fault-line').count(),buttons:await page.locator('.persona-btn').evaluateAll(nodes=>nodes.map(n=>({label:n.textContent,disabled:n.disabled}))),runtime:await status.textContent()};
      environmentStates.push(actual);
      const label=height+' '+scenario.name;
      if(scenario.none)assert.deepEqual(actual.notices,[],label);
      else{
        assert.equal(actual.notices.length,1,label);
        if(scenario.title)assert.deepEqual(actual.titles,[scenario.title],label);
        if(scenario.pattern)assert.match(actual.notices[0],scenario.pattern,label);
        if(scenario.installer)assert(actual.notices[0].includes(scenario.installer),label+' must use the snapshot/default installer path');
        if(scenario.neutral){
          if(actual.faultLines||/HidHide|缺少|校验失败/.test(actual.notices[0])){const shot=path.join(out,'environment-regression-'+height+'.png');await page.screenshot({path:shot,animations:'disabled'});screenshots.push(shot)}
          assert.equal(actual.faultLines,0,label+' must be advisory');
          assert.doesNotMatch(actual.notices[0],/HidHide|缺少|校验失败/,label+' must not blame HidHide');
        }
      }
      assert.equal(actual.buttons.length,4,label);
      assert(actual.buttons.every(button=>!button.disabled),label+' must leave every persona selectable');
      assert.equal(actual.runtime,'已开启',label+' must preserve actual runtime success');
      cases.push('environment '+label);
    }
    await emitEnvironment({driverSetup:6,hidHideSetupNotice:8,driverSetupResult:12},{backendFaultHold:true});
    assert(await page.getByText('虚拟输出已停写',{exact:true}).isVisible(),'real backendFaultHold remains visible');
    assert.match(await page.locator('.persona-block').textContent(),/后端创建或输入输出失败/);
    assert.doesNotMatch(await page.locator('.persona-block').textContent(),/缺少 HidHide|HidHide 安装器校验失败/);
    cases.push('environment '+height+' backendFaultHold remains independent of preparation notice');
    for(const recoveryFallback of [1,2]){
      await emitEnvironment({driverSetup:2},{recoveryFallback});
      assert(await page.getByText('虚拟未恢复',{exact:true}).isVisible(),'recovery fallback remains visible');
      if(recoveryFallback===2)assert.match(await page.locator('.persona-block').textContent(),/读回：物理可用/);
      cases.push('environment '+height+' recovery fallback '+recoveryFallback+' remains visible');
    }
    await emitEnvironment({driverSetup:6,hidHideSetupNotice:8,driverSetupResult:12});
    const shot=path.join(out,'environment-neutral-'+height+'.png');await page.screenshot({path:shot,animations:'disabled'});screenshots.push(shot);
  }
  assert.equal(await page.evaluate(()=>window.__fixture.writes.length),writesBeforeEnvironment,'environment feedback never writes settings');
  assert.deepEqual(await page.evaluate(()=>window.__fixture.settings),baseline,'environment feedback preserves saved input');
  await emitEnvironment({driverSetup:2});
  await page.setViewportSize({width:1120,height:900});
  for(const persona of ['steamdeck','dualsense-edge']){
    await temporary(persona);await snapshot(persona,false);
    assert.equal(await status.textContent(),'Steam未运行');
    assert.equal(await channels.first().getAttribute('data-tone'),'warning');assert.match(await channels.first().getAttribute('title'),/需要 Steam/);
    assert.deepEqual(await values(),['Steam未运行','已检测','支持 2 键']);assert.match(await channels.nth(1).getAttribute('title'),/物理陀螺仪/);assert.match(await channels.nth(2).getAttribute('title'),/2 个物理背键/);
    cases.push(persona+' shows missing Steam warning, preserves gyro/back-button capabilities and follows runtime persona');
    await assertTouchpadDefaults(persona);await personaShot(persona);
    await snapshot(persona,true);assert.equal(await status.textContent(),'已开启');assert.deepEqual(await values(),['已开启','已检测','支持 2 键']);cases.push(persona+' returns to backend success after Steam starts');
    await snapshot(persona,null);assert.equal(await status.textContent(),'已开启');cases.push(persona+' unknown Steam state does not invent missing Steam');
    await snapshot(persona,true,'suspected-failed');assert.equal(await status.textContent(),'失败');assert.equal(await channels.first().getAttribute('data-tone'),'warning');cases.push(persona+' retains independent backend failure');
    await temporary(persona,false);await snapshot(persona,false);assert.equal(await status.textContent(),'未开启');assert.equal(await channels.first().getAttribute('data-tone'),'off');cases.push(persona+' disabled runtime is off, not missing Steam');
  }
  await temporary('elite');await snapshot('elite',false);assert.equal(await status.textContent(),'已开启');
  assert.deepEqual(await values(),['已开启','已检测','支持 2 键']);await assertTouchpadDefaults('elite');cases.push('Xbox is unaffected by missing Steam');
  await temporary('disabled',false);await snapshot('disabled',false);assert.deepEqual(await values(),['未开启','已检测','支持 2 键']);await assertTouchpadDefaults('disabled');cases.push('disabled runtime selects close-output button without claiming capability');
  assert.deepEqual(await page.evaluate(()=>window.__fixture.settings),baseline);assert.equal(await page.evaluate(()=>window.__fixture.writes.length),0);cases.push('runtime feedback never writes saved configuration');
  // controllerPersona.ts performs one-way settings migration. Retired identities
  // must not reappear as selectable buttons, including disabled saved requests.
  for(const [old,current,label] of [['xbox360','elite','Xbox'],['dualshock4','dualsense-edge','PS5'],['dualsense','dualsense-edge','PS5']])for(const enabled of [false,true]){
    await page.evaluate(({old,enabled})=>{const f=window.__fixture;f.shortcut=null;f.settings=window.__normalize({input:{outputTarget:{persona:old,buttonMappingEnabled:enabled,descriptorHash:'retired'}}});window.__mount('controller')},{old,enabled});
    await page.waitForFunction(label=>document.querySelector('.persona-btn.active')?.textContent===label,label);
    await assertSelected(current);assert.deepEqual(await page.locator('.persona-btn').allTextContents(),['关闭虚拟手柄','SteamDeck','PS5','Xbox']);
    const saved=await page.evaluate(()=>window.__fixture.settings.input.outputTarget);
    assert.equal(saved.persona,current);assert.equal(saved.buttonMappingEnabled,enabled);assert.equal(saved.descriptorHash,null);
    assert.equal(await page.evaluate(()=>window.__fixture.writes.length),0);cases.push(old+' migrates only to '+current+' without hydration writes; enabled='+enabled);
  }
  await page.evaluate(baseline=>{window.__fixture.settings=structuredClone(baseline);window.__fixture.shortcut=null;window.__mount('gyro')},baseline);
  // GyroMotionView's current 输出到 field moved from row 5 to row 12.
  // Bind to its semantic field, preserving left/right order and saved values.
  const stick=page.locator('.field').filter({has:page.getByText('输出到',{exact:true})}).locator('.seg');
  await page.waitForFunction(()=>[...document.querySelectorAll('.field')].some(field=>field.querySelector(':scope > span')?.textContent==='输出到'&&field.querySelector('button.active')?.textContent?.trim()==='右摇杆'));
  assert.equal(await stick.count(),1);
  assert.deepEqual(await stick.locator('button .seg-main').allTextContents(),['左摇杆','右摇杆']);
  const positions=await stick.locator('button').evaluateAll(nodes=>nodes.map(node=>node.getBoundingClientRect().x));assert(positions[0]<positions[1]);
  assert.equal(await page.evaluate(()=>window.__fixture.writes.length),0);assert.deepEqual(await page.evaluate(()=>window.__fixture.settings),baseline);cases.push('gyro UI places left before right while saved right selection and hydration remain unchanged');
  const gyroShot=path.join(out,'gyro-left-right-layout.png');await page.screenshot({path:gyroShot,fullPage:true,animations:'disabled'});screenshots.push(gyroShot);
  await stick.getByRole('button',{name:'左摇杆',exact:true}).click();
  await page.waitForFunction(()=>window.__fixture.settings.input.gyroMotion.outputStick==='left');cases.push('left UI still saves left output value');
  await stick.getByRole('button',{name:'右摇杆',exact:true}).click();
  await page.waitForFunction(()=>window.__fixture.settings.input.gyroMotion.outputStick==='right');cases.push('right UI still saves right output value');
  const writesBeforePreview=await page.evaluate(()=>window.__fixture.writes.length);
  // GamepadVisualizer now uses ControllerDeviceDiagram/ControllerVectorArtwork.
  // controllerDiagramData.json supplies these exact display travel distances;
  // test real SVG translations rather than the removed fallback circle centers.
  const geometry={'steamdeck':{diagram:'steamdeck',travel:[14,14]},'dualsense-edge':{diagram:'ps5',travel:[20.04,22.01486268174475]},elite:{diagram:'xbox',travel:[19.98,19.98]}};
  const models=JSON.parse(source(path.join(root,'src/bridge/controllerDiagramData.json')));
  const nodes=model=>model.nodes.flatMap(function walk(n){return [n,...(n.children||[]).flatMap(walk)]});
  const coords=()=>page.locator('.device-vector [data-stick]').evaluateAll(nodes=>nodes.map(n=>{const m=n.transform.baseVal.consolidate()?.matrix;return {stick:+n.dataset.stick,x:m?.e??0,y:m?.f??0}}).sort((a,b)=>a.stick-b.stick));
  const sendAxes=async axes=>page.evaluate(async axes=>{window.__emit('gamepad.state',{connected:true,name:'Validation Controller',buttons:[],axes});await window.__tick()},axes);
  const close=(actual,expected)=>assert(Math.abs(actual-expected)<1e-5,'SVG translation '+actual+' != '+expected);
  for(const [persona,{diagram,travel}] of Object.entries(geometry)){
    const stickNodes=nodes(models[diagram]).filter(n=>n.stick!=null);assert.equal(stickNodes.length,2);for(const n of stickNodes)assert.deepEqual(n.travel,travel,'source-backed preview travel changed');
    await page.evaluate(persona=>window.__mount('preview',persona),persona);
    assert.equal(await page.locator('.device-diagram').getAttribute('data-diagram-persona'),diagram);
    await sendAxes([0,0,0,0]);const neutral=await coords();assert.deepEqual(neutral,[{stick:0,x:0,y:0},{stick:1,x:0,y:0}]);
    for(const axes of [[0,1,0,1],[0,-1,0,-1],[1,0,-1,0],[0,1,0,-1],[0.25,0.125,-0.25,-0.125]]){
      await sendAxes(axes);const drawn=await coords();assert.equal(drawn.length,2);
      for(let i=0;i<2;i++){close(drawn[i].x,neutral[i].x+axes[i*2]*travel[0]);close(drawn[i].y,neutral[i].y-axes[i*2+1]*travel[1])}
      cases.push(persona+' both actual SVG stick caps retain display-only X/Y for '+axes.join(','));
    }
    await sendAxes([0,1,0,-1]);const previewShot=path.join(out,persona+'-stick-left-up-right-down.png');await page.screenshot({path:previewShot,animations:'disabled'});screenshots.push(previewShot);
    const visible=await coords();await page.evaluate(()=>window.dispatchEvent(new CustomEvent('ipc:window.hidden')));
    await sendAxes([1,0,-1,0]);await sendAxes([0,-1,0,1]);assert.deepEqual(await coords(),visible);
    await page.evaluate(async()=>{window.dispatchEvent(new CustomEvent('ipc:window.shown'));await window.__tick()});const resumed=await coords();
    close(resumed[0].y,neutral[0].y+travel[1]);close(resumed[1].y,neutral[1].y-travel[1]);close(resumed[0].x,0);close(resumed[1].x,0);cases.push(persona+' hidden preview freezes both sticks and resumes only the latest sample with corrected Y');
    await sendAxes([0,0,0,0]);assert.deepEqual(await coords(),neutral);cases.push(persona+' neutral returns both sticks to unchanged centers');
  }
  assert.equal(await page.evaluate(()=>window.__fixture.writes.length),writesBeforePreview);cases.push('preview never changes settings');
  assert.deepEqual(await page.evaluate(()=>window.__fixture.apiCalls),[],'no window/shell APIs requested');
  assert(!Object.keys(result.metafile.inputs).some(p=>/(^|[/\\])src[/\\]bridge[/\\]ipc\.ts$/.test(p)),'real native bridge was not bundled');
  for(const binding of sources.values())assert.equal(createHash('sha256').update(fs.readFileSync(path.join(root,binding.path))).digest('hex'),binding.sha256,'source drift: '+binding.path);
  assert.deepEqual(errors,[],'no browser runtime errors');
  observations=await page.evaluate(()=>({requests:window.__fixture.requests,ipcCalls:window.__fixture.ipcCalls,touchpads:window.__fixture.touchpads,apiCalls:window.__fixture.apiCalls,writes:window.__fixture.writes,input:window.__fixture.settings.input}));
  passed=true;
}catch(error){failure={name:error.name,message:error.message,stack:error.stack};throw error}
finally{
  let cleanupError;
  for(const close of [()=>browser?.close(),()=>browserServer?.close()])try{await close()}catch(error){cleanupError=String(error);passed=false}
  try{if(server?.listening)await new Promise((resolve,reject)=>server.close(error=>error?reject(error):resolve()))}catch(error){cleanupError=String(error);passed=false}
  const bindings=[...sources.values()].map(binding=>({...binding,endSha256:createHash('sha256').update(fs.readFileSync(path.join(root,binding.path))).digest('hex')}));
  const sourceError=bindings.every(b=>b.sha256===b.endSha256)?null:'source changed before final cleanup';if(sourceError)passed=false;
  fs.writeFileSync(path.join(out,'source-bindings.json'),JSON.stringify({sources:bindings,unchanged:!sourceError},null,2));
  if(observations)fs.writeFileSync(path.join(out,'fixture-observations.json'),JSON.stringify(observations,null,2));
  fs.writeFileSync(path.join(out,'persona-screenshot-states.json'),JSON.stringify(screenshotStates,null,2));
  fs.writeFileSync(path.join(out,'environment-states.json'),JSON.stringify(environmentStates,null,2));
  fs.writeFileSync(path.join(out,'results.json'),JSON.stringify({pass:passed,cases,screenshots,errors,failure,cleanupError,sourceError,hardwareOperations:0,scope:'actual current controller/gyro/preview components; in-memory base-editor/CAS/IPC only; no per-game ownership or native delivery claim'},null,2));
  fs.writeFileSync(path.join(out,'process-exit.json'),JSON.stringify({browserPid,httpPort,browserConnected:browser?.isConnected()??false,httpListening:server?.listening??false,cleanupError:cleanupError??null},null,2));
  if((cleanupError||sourceError)&&!failure)throw Error(cleanupError||sourceError);
  if(passed){console.log('CONTROLLER_DISPLAY_BROWSER_PASS cases='+cases.length+' hardwareOperations=0');console.log(screenshots.join('\n'))}
}
