// Offline tests of the production component and bridge parser. No native API or registry access.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import { createServer } from 'node:http';
import {createHash,randomInt} from 'node:crypto';
import {startBatteryControllerBridge} from './ymcc_sim/global/battery-signals/controller-proxy.mjs';
import {runLiveBatteryCases} from './ymcc_sim/global/battery-signals/live-browser-cases.mjs';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const req = createRequire(path.join(root,'package.json'));
const { build } = req('esbuild');
const { parse, compileScript, compileStyle } = req('vue/compiler-sfc');
const runtime = process.env.YMCC_UI_BROWSER_MODULE_DIR || 'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const { chromium } = createRequire(path.join(runtime,'_battery-charge-tests.cjs'))('playwright');
const out = path.resolve(process.env.YMCC_BATTERY_TEST_OUT || path.join(root,'../../../Build/Validation/BatteryCharge-20261013/browser'));
fs.mkdirSync(out,{recursive:true});
const usedSourceHashes={};
const recordSource=(file,bytes)=>{const relative=path.relative(root,file).replaceAll('\\','/');if(relative.startsWith('src/'))usedSourceHashes[relative]=createHash('sha256').update(bytes).digest('hex');};
const tokenBytes=fs.readFileSync(path.join(root,'src/styles/tokens.css'));recordSource(path.join(root,'src/styles/tokens.css'),tokenBytes);
const styles = [tokenBytes.toString('utf8')];
const fixture = `
import {createApp,h,ref,KeepAlive} from 'vue';
import Component from './src/components/BatteryChargeSettings.vue';
import * as model from './src/bridge/batteryCharge';
window.__model=model;
window.__fixture={calls:[],state:null,defer:false,fail:false,failSet:false,pending:[],actualController:new URLSearchParams(location.search).get('actual-controller')==='1'};
const fixtureApp=createApp({setup(){const shown=ref(true),version=ref(0);window.__show=v=>shown.value=v;window.__reset=v=>{window.__fixture.state=v;shown.value=true;version.value++};return()=>h(KeepAlive,{key:version.value},{default:()=>shown.value?h(Component):null})}});window.__destroy=()=>fixtureApp.unmount();fixtureApp.mount('#app');
`;
const ipcMock = `export const isNativeRuntime=true;export async function invoke(cmd,args,options){const f=window.__fixture;f.calls.push({cmd,args,options});
if(cmd==='diagnostics.frontendError')return {ok:true};
if(f.actualController){const response=await fetch('/controller',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({command:cmd,args:args??{}})});if(!response.ok)throw Error('controller bridge rejected request');return await response.json();}
if(cmd==='battery.charge.refresh'&&f.state.verification==='unconfirmed'&&!f.fail){f.state={...f.state,ok:true,supported:true,verification:'readback',fence:'b'.repeat(64),reason:''}}
if(cmd==='battery.charge.get'||cmd==='battery.charge.refresh'){if(f.fail)throw Error('fixture read failure');if(f.defer)return new Promise(resolve=>f.pending.push(resolve));return structuredClone(f.state)}
if(cmd==='battery.charge.set'){
 if(f.failSet)throw Error('fixture lost acknowledgement');
 const software=f.state.kind==='software-bypass-threshold',ack=f.state.family==='ASUS';
 f.state={...f.state,requestedLimit:args.target,currentLimit:software||ack?null:args.target,managed:!software||args.target<100,verification:software?(args.target===100?'unmanaged':'software-active'):ack?'acknowledged':'readback'};return structuredClone(f.state);
}
if(cmd==='battery.charge.release'){f.state={...f.state,ok:true,supported:true,managed:false,requestedLimit:100,reason:'',verification:'unmanaged'};return structuredClone(f.state)}
throw Error('unexpected command '+cmd)}`;
const output = await build({metafile:true,absWorkingDir:root,stdin:{contents:fixture,resolveDir:root,loader:'ts'},bundle:true,write:false,platform:'browser',format:'iife',define:{'process.env.NODE_ENV':'"production"'},plugins:[{
 name:'battery-charge-offline',setup(b){
  b.onResolve({filter:/.*/},args=>{
    const normalized=args.importer.replaceAll('\\','/');
    if(args.path==='@/bridge/ipc'||(args.path==='./ipc'&&normalized.includes('/src/bridge/')))return {path:'ipc',namespace:'mock'};
    if(args.path==='@/gamepad/focus')return {path:'focus',namespace:'mock'};
    return null;
  });
  b.onLoad({filter:/.*/,namespace:'mock'},args=>({contents:args.path==='ipc'?ipcMock:`export const focusGamepadElement=el=>el?.focus();export const getGamepadPopupPlacement=()=>({style:{left:'24px',top:'180px',maxHeight:'360px'}});`,loader:'ts'}));
  b.onResolve({filter:/^@\//},args=>({path:path.join(root,'src',args.path.slice(2))+(path.extname(args.path)?'':'.ts')}));
  b.onLoad({filter:/\.[cm]?[jt]sx?$/},args=>{
    const relative=path.relative(root,args.path).replaceAll('\\','/');if(!relative.startsWith('src/'))return;
    const bytes=fs.readFileSync(args.path);recordSource(args.path,bytes);
    return {contents:bytes.toString('utf8'),loader:path.extname(args.path).slice(1),resolveDir:path.dirname(args.path)};
  });
  b.onLoad({filter:/\.png$/},args=>{const bytes=fs.readFileSync(args.path);recordSource(args.path,bytes);return {contents:bytes,loader:'dataurl'};});
  b.onLoad({filter:/\.vue$/},args=>{
    const bytes=fs.readFileSync(args.path);recordSource(args.path,bytes);const {descriptor}=parse(bytes.toString('utf8'));
    const id='data-v-'+path.basename(args.path).replace(/\W/g,'');
    for(const style of descriptor.styles){const compiled=compileStyle({source:style.content,id,scoped:style.scoped});assert.deepEqual(compiled.errors,[]);styles.push(compiled.code)}
    const code=compileScript(descriptor,{id,inlineTemplate:true}).content.replace('export default','const component=')+`\ncomponent.__scopeId=${JSON.stringify(id)};export default component;`;
    return {contents:code,loader:'ts',resolveDir:path.dirname(args.path)};
  });
 }
}]});
const html=`<!doctype html><meta charset="utf-8"><style>${styles.join('\n')}body{background:#0b1016;color:var(--text);font-family:system-ui;padding:24px}.card{padding:20px;background:var(--bg-panel);border-radius:12px;max-width:700px}.muted{color:var(--text-dim)}.card-title{margin:0;font-size:18px}</style><div id="app"></div><script src="/fixture.js"></script>`;
let liveBridge=null;
const server=createServer(async(request,response)=>{
 if(request.url==='/controller'){
  try{if(!liveBridge||request.method!=='POST')throw Error('NO_LIVE_CONTROLLER');let text='';for await(const chunk of request){text+=chunk.toString();if(text.length>16384)throw Error('REQUEST_TOO_LARGE');}const input=JSON.parse(text),reply=await liveBridge.request(input.command,input.args??{});response.setHeader('Content-Type','application/json');response.end(JSON.stringify(reply));}
  catch{response.writeHead(400);response.end(JSON.stringify({error:'CONTROLLER_FIXTURE_REQUEST_FAILED'}));}return;
 }
 response.setHeader('Content-Type',request.url==='/fixture.js'?'text/javascript':'text/html;charset=utf-8');response.end(request.url==='/fixture.js'?output.outputFiles[0].text:html);
});
// Keep the existing browser environment on safe high loopback ports.
for(let attempt=0;attempt<32&&!server.listening;attempt++){
 try{await new Promise((resolve,reject)=>{const bad=e=>{server.off('listening',ready);reject(e)};const ready=()=>{server.off('error',bad);resolve()};server.once('error',bad);server.once('listening',ready);server.listen(randomInt(49152,65536),'127.0.0.1')});}
 catch(error){if(!['EADDRINUSE','EACCES'].includes(error.code))throw error;}
}
if(!server.listening)throw Error('BATTERY_BROWSER_PORT_UNAVAILABLE');
const sourceFiles=new Set(['tools/battery_charge_browser_selftest.mjs','src/styles/tokens.css']);
for(const input of Object.keys(output.metafile.inputs)){const absolute=path.resolve(root,input),relative=path.relative(root,absolute).replaceAll('\\','/');if(relative.startsWith('src/')&&fs.existsSync(absolute))sourceFiles.add(relative);}
const hash=file=>createHash('sha256').update(fs.readFileSync(path.join(root,file))).digest('hex');
const sourceHashes=Object.fromEntries([...sourceFiles].sort().map(file=>[file,usedSourceHashes[file]??hash(file)]));
let receipt;
const state={schema:1,ok:true,supported:true,batteryPresent:true,model:'Fixture',family:'Lenovo',kind:'firmware-fixed',choices:[80,100],currentLimit:100,requestedLimit:null,managed:false,verification:'readback',fence:'a'.repeat(64),reason:''};
let browser;const cases=[],errors=[];
try{
 if(process.env.YMCC_BATTERY_BRIDGE_DLL)liveBridge=await startBatteryControllerBridge({dll:process.env.YMCC_BATTERY_BRIDGE_DLL,evidenceDir:path.join(out,'live-controller'),dotnet:process.env.YMCC_BATTERY_TEST_DOTNET});
 browser=await chromium.launch({headless:true,executablePath:process.env.YMCC_UI_BROWSER_EXE || 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
 const page=await browser.newPage({viewport:{width:900,height:650}});await page.clock.install();page.on('pageerror',e=>errors.push(e.message));await page.goto(`http://127.0.0.1:${server.address().port}`);
 const panel=page.getByTestId('battery-charge-settings');
 const refresh=async value=>{await page.evaluate(v=>{window.__fixture.state=v;window.dispatchEvent(new Event('focus'));},value);await page.waitForTimeout(80)};
 const choose=async text=>{await page.getByRole('button',{name:'充电上限档位',exact:true}).click();await page.getByRole('option',{name:text,exact:true}).click()};
 assert.equal(await panel.count(),1);assert.match(await panel.textContent(),/当前读取失败/);cases.push('charging card stays visible during initial unknown or failed read');
 const win5={...state,ok:false,supported:false,batteryPresent:false,model:'G1618-05',family:'',kind:'unsupported',choices:[],currentLimit:null,requestedLimit:null,verification:'unknown',reason:'此设备暂无可确认的充电上限接口'};
 await refresh(win5);assert.equal(await panel.count(),1);assert.match(await panel.textContent(),/不支持（未检测到内置电池）/);
 await refresh({...win5,batteryPresent:true});assert.equal(await panel.count(),1);assert.match(await panel.textContent(),/此设备暂不支持充电上限/);assert.equal(await page.getByRole('button',{name:'应用',exact:true}).count(),0);assert.equal(await page.getByRole('button',{name:'充电上限档位',exact:true}).count(),0);
 assert.equal(await page.evaluate(()=>window.__fixture.calls.filter(c=>c.cmd.endsWith('.set')).length),0);cases.push('unsupported and no-battery devices retain the card with explicit unsupported state, no invented route or write');

 await refresh(state);await panel.waitFor();await page.getByRole('button',{name:'充电上限档位',exact:true}).click();assert.equal(await page.getByRole('option').count(),2);await page.getByRole('option',{name:'80%',exact:true}).click();
 assert.equal(await page.evaluate(()=>window.__fixture.calls.filter(c=>c.cmd.endsWith('.set')).length),0);cases.push('fixed 80/100 dropdown; selecting never writes');
 await page.getByRole('button',{name:'应用',exact:true}).click();await page.waitForTimeout(60);assert.equal(await page.evaluate(()=>window.__fixture.calls.find(c=>c.cmd.endsWith('.set')).args.target),80);assert.match(await panel.textContent(),/当前：80%/);cases.push('explicit apply submits selected fixed target');
 assert.equal(await panel.locator('.description').textContent(),'部分厂家理解的[旁路供电]');assert.equal(await page.getByRole('button',{name:'重新读取',exact:true}).count(),0);assert.equal(await panel.getByRole('button').count(),2);cases.push('compact card has requested caption, dropdown and Apply only');
 const staleChoices=await page.evaluate(s=>window.__model.batteryChargeOptions({...s,kind:'firmware-percent',choices:[20,40,50,80,100],currentLimit:40}),state);
 assert.deepEqual(staleChoices.filter(x=>!x.disabled).map(x=>x.value),[50,80,100]);assert.equal(staleChoices[0].disabled,true);cases.push('product floor filters old choices while retaining actual low current value as read-only');
 const percent={...state,family:'OneXPlayer',kind:'firmware-percent',choices:[50,60,70,80,90,100],currentLimit:77};
 await refresh(percent);assert.match(await page.getByRole('button',{name:'充电上限档位',exact:true}).textContent(),/77%/);assert.equal(await page.getByRole('button',{name:'应用',exact:true}).isDisabled(),true);cases.push('nonpreset 77 readback preserved without rounding');
 const msiUnknown={...percent,family:'MSI Claw',model:'MS-1T91',choices:Array.from({length:11},(_,i)=>50+i*5),currentLimit:null,verification:'unknown'};
 await refresh(msiUnknown);assert.match(await panel.textContent(),/当前上限未知/);const beforeMsi=await page.evaluate(()=>window.__fixture.calls.filter(c=>c.cmd.endsWith('.set')).length);
 await choose('80%');assert.equal(await page.getByRole('button',{name:'应用',exact:true}).isEnabled(),true);assert.equal(await page.evaluate(()=>window.__fixture.calls.filter(c=>c.cmd.endsWith('.set')).length),beforeMsi);
 await page.getByRole('button',{name:'应用',exact:true}).click();await page.waitForTimeout(60);assert.match(await panel.textContent(),/当前：80%/);cases.push('MSI flag-only unknown limit allows explicit apply without fabricating or auto-writing a value');
 const asus={...percent,family:'ASUS',currentLimit:null};await refresh(asus);assert.match(await panel.textContent(),/当前上限未知/);await choose('80%');await page.getByRole('button',{name:'应用',exact:true}).click();await page.waitForTimeout(70);assert.match(await panel.textContent(),/当前设置：80%（已接受）/);assert.doesNotMatch(await panel.textContent(),/当前：80%/);cases.push('ASUS write acknowledgement does not fabricate readback');
 const soft={...percent,kind:'software-bypass-threshold',family:'AYANEO',currentLimit:null,requestedLimit:null,verification:'unknown'};await refresh(soft);await choose('80%');await page.getByRole('button',{name:'应用',exact:true}).click();await page.waitForTimeout(60);assert.match(await panel.textContent(),/当前目标：80%/);
 const releases=await page.evaluate(()=>window.__fixture.calls.filter(c=>c.cmd.endsWith('.release')).length);await page.evaluate(()=>window.__show(false));await page.waitForTimeout(50);await page.evaluate(()=>window.__show(true));await page.waitForTimeout(70);assert.equal(await page.evaluate(()=>window.__fixture.calls.filter(c=>c.cmd.endsWith('.release')).length),releases);cases.push('KeepAlive navigation leaves software policy active');
 await choose('100%');await page.getByRole('button',{name:'应用',exact:true}).click();await page.waitForTimeout(60);assert.equal(await page.getByRole('button',{name:'停止软件限充'}).count(),0);assert.match(await page.getByRole('button',{name:'充电上限档位',exact:true}).textContent(),/100%/);cases.push('software 100 means stopped and remains selected');await choose('80%');await page.getByRole('button',{name:'应用',exact:true}).click();await page.waitForTimeout(60);
 await page.evaluate(()=>window.__fixture.failSet=true);await choose('70%');await page.getByRole('button',{name:'应用',exact:true}).click();await page.waitForTimeout(60);assert.match(await panel.textContent(),/当前设置未确认/);assert.equal(await page.getByRole('button',{name:'充电上限档位',exact:true}).isEnabled(),true);assert.equal(await page.getByRole('button',{name:'应用',exact:true}).isDisabled(),true);cases.push('unknown software set result keeps 100 percent release available');
 await choose('100%');await page.getByRole('button',{name:'应用',exact:true}).click();await page.waitForTimeout(60);assert.equal(await page.getByRole('button',{name:'停止软件限充'}).count(),0);assert.match(await page.getByRole('button',{name:'充电上限档位',exact:true}).textContent(),/100%/);cases.push('100 percent Apply releases software ownership after uncertain write');
 await page.evaluate(()=>{window.__fixture.failSet=false;window.__fixture.defer=true;window.dispatchEvent(new Event('focus'))});await page.waitForTimeout(50);await page.evaluate(()=>window.__show(false));await page.waitForTimeout(30);await page.evaluate(()=>window.__show(true));await page.waitForTimeout(30);await page.evaluate(v=>{const f=window.__fixture;f.defer=false;f.state=v;for(const resolve of f.pending.splice(0))resolve(v)},state);await page.waitForTimeout(100);assert.equal(await page.getByRole('button',{name:'应用',exact:true}).isEnabled(),true);cases.push('reactivation during pending read refreshes and releases loading state');
 await page.evaluate(()=>{window.__fixture.fail=true;window.dispatchEvent(new Event('focus'))});await page.waitForTimeout(60);assert.equal(await page.getByRole('button',{name:'应用',exact:true}).isDisabled(),true);cases.push('read failure disables mutation');
 const failures=await page.evaluate(()=>window.__fixture.calls.filter(c=>c.cmd==='diagnostics.frontendError').map(c=>c.args.where));assert.ok(failures.includes('battery-charge.get')&&failures.includes('battery-charge.set'));cases.push('frontend read and write failures reach the exported error log');
 await page.evaluate(()=>window.__fixture.fail=false);await refresh({...state,supported:false,reason:'此设备暂无可确认的充电上限接口'});assert.match(await panel.textContent(),/当前不可用/);assert.equal(await page.getByRole('button',{name:'应用',exact:true}).isDisabled(),true);cases.push('unsupported battery shows concise unavailable status');
 await refresh({...percent,ok:false,supported:false,currentLimit:null,verification:'unconfirmed'});
 const recoveryStart=await page.evaluate(()=>window.__fixture.calls.length);await choose('80%');
 assert.equal(await page.evaluate(n=>window.__fixture.calls.slice(n).filter(x=>x.cmd.endsWith('.refresh')||x.cmd.endsWith('.set')).length,recoveryStart),0);
 await page.getByRole('button',{name:'应用',exact:true}).click();await page.waitForTimeout(60);
 const recoveryCalls=await page.evaluate(n=>window.__fixture.calls.slice(n).filter(x=>x.cmd.endsWith('.refresh')||x.cmd.endsWith('.set')),recoveryStart);
 assert.deepEqual(recoveryCalls.map(x=>x.cmd),['battery.charge.refresh','battery.charge.set']);assert.equal(recoveryCalls[1].args.fence,'b'.repeat(64));cases.push('explicit Apply rereads uncertain firmware result and uses the fresh fence');
 await refresh({...percent,ok:false,supported:false,currentLimit:null,verification:'unconfirmed'});await choose('80%');await page.evaluate(()=>window.__fixture.fail=true);
 const blockedStart=await page.evaluate(()=>window.__fixture.calls.filter(x=>x.cmd.endsWith('.set')).length);await page.getByRole('button',{name:'应用',exact:true}).click();await page.waitForTimeout(60);
 assert.equal(await page.evaluate(()=>window.__fixture.calls.filter(x=>x.cmd.endsWith('.set')).length),blockedStart);await page.evaluate(()=>window.__fixture.fail=false);cases.push('failed recovery read never sends a setting');
 if(process.env.YMCC_BATTERY_FEEDBACK_FILE){
  const native=JSON.parse(fs.readFileSync(process.env.YMCC_BATTERY_FEEDBACK_FILE,'utf8').replace(/^\uFEFF/,''));
  assert.equal(native.status,'PASS');const feedback=native.controllerFeedback;
  assert.equal(feedback.consumer,'YeManBatteryHost.Controller.Apply');assert.equal(feedback.physicalWrites,0);
  await refresh(feedback.before);assert.match(await panel.textContent(),/当前：100%/);
  await refresh(feedback.after);assert.match(await panel.textContent(),/当前：80%/);
  const parsed=await page.evaluate(value=>window.__model.parseBatteryChargeState(value),feedback.after);
  assert.equal(parsed.currentLimit,80);assert.equal(parsed.fence,feedback.after.fence);
  cases.push('actual Controller snapshot JSON reaches the production bridge parser and component');
 }
 const parser=await page.evaluate(s=>{
  const m=window.__model;let rejected=0;for(const bad of [{...s,choices:[75,100]},{...s,currentLimit:101},{...s,fence:''},{...s,choices:[80,80]},{...s,managed:'yes'}]){try{m.parseBatteryChargeState(bad)}catch{rejected++}}return rejected;
 },state);assert.equal(parser,5);cases.push('malformed IPC schemas rejected');
 await refresh({...percent,currentLimit:80});await page.setViewportSize({width:420,height:720});await page.screenshot({path:path.join(out,'battery-charge-settings.png'),fullPage:true});assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);cases.push('compact settings card fits handheld layout');
 await refresh({...soft,managed:true,requestedLimit:80,verification:'software-active'});
 const released=await page.evaluate(()=>window.__model.batteryCharge.release());assert.equal(released.managed,false);cases.push('production release bridge consumes an unmanaged receipt');
 const transfer=await page.evaluate(()=>window.__fixture.calls.filter(call=>call.cmd.startsWith('battery.charge.')).map(call=>({operation:call.cmd,target:call.args?.target??null,hasFence:typeof call.args?.fence==='string',timeoutMs:call.options?.timeoutMs??null})));
 const executedOperations=[...new Set(transfer.map(call=>call.operation))].sort();
 assert.deepEqual(executedOperations,['battery.charge.get','battery.charge.refresh','battery.charge.release','battery.charge.set']);assert(transfer.every(call=>call.timeoutMs===35000));cases.push('all four production bridge commands carry the bounded request budget');
 // Charging UI recovery uses the actual component, real virtual timers and
 // read-only IPC mocks. No route switch, focus event or write can rescue reads.
 await page.evaluate(()=>{window.__fixture.fail=false;window.__fixture.failSet=false;window.__fixture.defer=false;window.__show(false)});await page.waitForTimeout(30);
 await page.evaluate(v=>window.__reset(v),state);await page.waitForTimeout(60);
 const reads=()=>page.evaluate(()=>window.__fixture.calls.filter(x=>x.cmd==='battery.charge.get').length);
 const writes=()=>page.evaluate(()=>window.__fixture.calls.filter(x=>x.cmd==='battery.charge.set'||x.cmd==='battery.charge.refresh'||x.cmd==='battery.charge.release').length);
 const recoveryWrites=await writes();
 await page.evaluate(()=>window.__fixture.fail=true);await page.clock.runFor(15010);await page.waitForTimeout(40);
 assert.match(await panel.textContent(),/当前读取失败/);const failedReads=await reads();
 await page.evaluate(v=>{window.__fixture.fail=false;window.__fixture.state={...v,currentLimit:80}},state);
 await page.clock.runFor(15010);await page.waitForTimeout(40);
 assert((await reads())>failedReads,'temporary read failure permanently stopped automatic refresh until route/focus switch');
 assert.match(await panel.textContent(),/当前：80%/);assert.equal(await writes(),recoveryWrites);cases.push('temporary read failure heals read-only without tab or focus switch');
 await page.evaluate(()=>window.__fixture.state.currentLimit=100);await page.clock.runFor(15010);await page.waitForTimeout(40);
 assert.match(await panel.textContent(),/当前：100%/);cases.push('visible page automatically rereads external limit changes without remount');
 const pauseReads=await reads();await page.evaluate(()=>window.dispatchEvent(new CustomEvent('ipc:window.minimized')));await page.clock.runFor(60000);await page.waitForTimeout(40);
 assert.equal(await reads(),pauseReads,'native-hidden page still polls');
 await page.evaluate(()=>window.dispatchEvent(new CustomEvent('ipc:window.restored')));await page.waitForTimeout(50);assert.equal(await reads(),pauseReads+1);cases.push('native hidden stops owned timer and restore reads once');
 await page.evaluate(()=>{window.__fixture.fail=true;window.dispatchEvent(new Event('focus'))});await page.waitForTimeout(40);
 const exhaustedStart=await reads();for(const ms of [1600,5100,15100,60000]){await page.clock.runFor(ms);await page.waitForTimeout(30)}
 const exhaustedEnd=await reads();assert.equal(exhaustedEnd-exhaustedStart,3,'transient read retry episode must have exactly three bounded retries');
 await page.clock.runFor(120000);await page.waitForTimeout(30);assert.equal(await reads(),exhaustedEnd,'exhausted retry must remain idle');
 await page.evaluate(v=>{window.__fixture.fail=false;window.__fixture.state=v;window.dispatchEvent(new CustomEvent('ipc:window.focus'))},state);await page.waitForTimeout(50);
 assert.match(await panel.textContent(),/当前：100%/);assert((await reads())>exhaustedEnd);cases.push('bounded retry exhaustion remains idle and native foreground reopens read budget');
 await page.evaluate(()=>window.dispatchEvent(new CustomEvent('ipc:power.suspending',{detail:{generation:7}})));const suspendedReads=await reads();await page.clock.runFor(60000);await page.waitForTimeout(30);assert.equal(await reads(),suspendedReads);
 await page.evaluate(()=>{window.__fixture.state.currentLimit=80;window.dispatchEvent(new CustomEvent('ipc:power.resumed',{detail:{generation:7}}))});await page.waitForTimeout(50);assert.match(await panel.textContent(),/当前：80%/);cases.push('logical sleep pauses reads; native resumed refreshes actual displayed value');
 await page.evaluate(()=>{window.__fixture.defer=true;window.dispatchEvent(new Event('focus'))});await page.waitForTimeout(30);
 const stoppedReads=await reads();await page.evaluate(()=>window.__show(false));await page.waitForTimeout(30);
 await page.evaluate(v=>{const f=window.__fixture;f.defer=false;for(const done of f.pending.splice(0))done(v)},state);await page.clock.runFor(60000);await page.waitForTimeout(40);assert.equal(await reads(),stoppedReads);assert.equal(await panel.count(),0);
 await page.evaluate(()=>{window.dispatchEvent(new Event('focus'));window.dispatchEvent(new CustomEvent('ipc:window.focus'));window.dispatchEvent(new CustomEvent('ipc:power.resumed',{detail:{generation:7}}));});await page.clock.runFor(60000);await page.waitForTimeout(30);assert.equal(await reads(),stoppedReads);cases.push('deactivation cancels owned timers and rejects late read and wake/focus callbacks');
 await page.evaluate(v=>{window.__fixture.state=v;window.__show(true)},state);await page.waitForTimeout(40);assert.match(await panel.textContent(),/当前：100%/);
 await choose('80%');await page.evaluate(()=>window.__fixture.state.currentLimit=100);await page.clock.runFor(15010);await page.waitForTimeout(30);assert.match(await page.getByRole('button',{name:'充电上限档位',exact:true}).textContent(),/80%/);assert.equal(await writes(),recoveryWrites);cases.push('background read preserves unapplied selection and never acknowledges or replays a write');
 await page.evaluate(v=>window.__reset(v),win5);await page.waitForTimeout(40);const unsupportedReads=await reads();await page.clock.runFor(60000);await page.waitForTimeout(30);
 assert.equal(await panel.count(),1);assert.match(await panel.textContent(),/不支持/);assert.equal(await reads(),unsupportedReads);cases.push('permanent unsupported card remains idle with no repeated hardware read');
 await page.evaluate(()=>{window.__fixture.fail=true;window.__reset(null)});await page.waitForTimeout(30);assert.equal(await panel.count(),1);assert.match(await panel.textContent(),/当前读取失败/);
 await page.evaluate(v=>{window.__fixture.fail=false;window.__fixture.state=v},state);await page.clock.runFor(1600);await page.waitForTimeout(40);assert.match(await panel.textContent(),/当前：100%/);cases.push('failed first read recovers in place while card stays visible');
 for(const event of ['ipc:window.shown','ipc:window.maximized','ipc:window.summoned']) {
  await page.evaluate(()=>window.dispatchEvent(new CustomEvent('ipc:window.hidden')));const hiddenReads=await reads();await page.clock.runFor(60000);await page.waitForTimeout(30);assert.equal(await reads(),hiddenReads);
  await page.evaluate(name=>{window.__fixture.state.currentLimit=80;window.dispatchEvent(new CustomEvent(name))},event);await page.waitForTimeout(40);assert.equal(await reads(),hiddenReads+1);assert.match(await panel.textContent(),/当前：80%/);
 }
 cases.push('shown maximized and summoned each refresh once on appearance without background polling');
 await page.evaluate(()=>window.dispatchEvent(new CustomEvent('ipc:power.suspending',{detail:{generation:9}})));const staleResumeReads=await reads();
 await page.evaluate(()=>window.dispatchEvent(new CustomEvent('ipc:power.resumed',{detail:{generation:8}})));await page.clock.runFor(60000);await page.waitForTimeout(30);assert.equal(await reads(),staleResumeReads);
 await page.evaluate(()=>window.dispatchEvent(new CustomEvent('ipc:power.resume-ready',{detail:{generation:9}})));await page.waitForTimeout(40);assert.match(await panel.textContent(),/当前：80%/);cases.push('older power wake cannot resume a newer suspended read session');
 await page.evaluate(()=>{window.__fixture.defer=true;window.dispatchEvent(new Event('focus'))});await page.waitForTimeout(30);const destroyedReads=await reads();await page.evaluate(()=>window.__destroy());
 await page.evaluate(v=>{const f=window.__fixture;f.defer=false;for(const done of f.pending.splice(0))done(v);for(const name of ['focus','ipc:window.focus','ipc:window.restored','ipc:power.resumed'])window.dispatchEvent(new CustomEvent(name,{detail:{generation:10}}));},state);
 await page.clock.runFor(120000);await page.waitForTimeout(40);assert.equal(await reads(),destroyedReads);assert.equal(await panel.count(),0);cases.push('unmount removes owned event listeners and timer; late read and wake cannot recreate the card or refresh');
 const liveController=liveBridge?await runLiveBatteryCases({browser,url:'http://127.0.0.1:'+server.address().port+'/',bridge:liveBridge}):null;
 assert.deepEqual(errors,[]);receipt={liveController,ok:true,status:'PASS',cases,errors,hardwareWrites:false,sourceHashes,browserPort:server.address().port,executedOperations,inputConsumerStateFeedback:transfer,transportScope:'memory IPC only; native pipe and physical provider IO not executed'};
}catch(error){receipt={ok:false,status:'FAIL',cases,errors,failure:'BATTERY_BROWSER_ASSERTION_FAILED',sourceHashes,browserPort:server.address()?.port};process.exitCode=2;console.error(error);}
finally{
 await browser?.close();await new Promise(resolve=>server.close(resolve));
 if(liveBridge){const liveResult=await liveBridge.close();receipt??={ok:false,status:'FAIL',cases,errors};receipt.liveControllerProcess=liveResult;if(liveResult.status!=='PASS'){receipt.ok=false;receipt.status='FAIL';process.exitCode=2;}}
 receipt??={ok:false,status:'FAIL',cases,errors,failure:'BROWSER_RECEIPT_MISSING'};
 receipt.ownServerClosed=!server.listening;
 receipt.sourceChanges=Object.entries(sourceHashes).filter(([file,before])=>hash(file)!==before).map(([file])=>file);
 if(!receipt.ownServerClosed||receipt.sourceChanges.length){receipt.ok=false;receipt.status='FAIL';process.exitCode=2;}
 fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(receipt,null,2));
}
console.log(JSON.stringify({ok:receipt.ok,groups:cases.length,cases,out},null,2));
